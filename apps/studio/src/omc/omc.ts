// Studio's line to omc (#160 contracts §5): squero/0 over the Studio server's WebSocket
// (`paperboat squero` / `squero-om`). One call is one omc expression; the server checks it
// against its allow-list and answers in order. Status pushes drive the top-bar chip.

export type OmcState = 'ready' | 'checking' | 'failed';
export type OmcStatus = {state: OmcState; version: string};

type Reply = {id?: number; ok?: boolean; result?: unknown; error?: string; op?: string; stage?: string; fraction?: number} & Partial<OmcStatus>;
type Waiter = {resolve: (result: unknown) => void; reject: (error: Error) => void; progress?: (stage: string, fraction: number) => void};

/** The two calls the store makes; tests give it a fake. */
export type OmcCalls = Pick<OmcClient, 'call' | 'op'>;

export class OmcClient {
  status: OmcStatus = {state: 'checking', version: ''};
  private readonly ws: WebSocket;
  private readonly waiting = new Map<number, Waiter>();
  private next = 1;
  private closing = false;
  private readonly open: Promise<void>;

  private readonly onStatus: (status: OmcStatus) => void;

  constructor(url: string, onStatus: (status: OmcStatus) => void = () => {}) {
    this.onStatus = onStatus;
    this.ws = new WebSocket(url);
    this.open = new Promise((resolve, reject) => {
      this.ws.addEventListener('open', () => resolve(), {once: true});
      this.ws.addEventListener('error', () => reject(new Error(`no Studio server at ${url}`)), {once: true});
    });
    this.ws.addEventListener('message', event => this.receive(String(event.data)));
    this.ws.addEventListener('close', () => {
      for (const w of this.waiting.values()) w.reject(new Error('the Studio server went away'));
      this.waiting.clear();
      if (!this.closing) this.setStatus({state: 'failed', version: this.status.version});
    });
  }

  // omc's answer as text (`true`, `"…"`, `{…}`); rejects with omc's or the server's words.
  async call(expr: string): Promise<string> {
    return String(await this.send({op: 'omc', expr}));
  }

  // One of Studio's own squero/0 ops (open, save, icon, set_icon, generate, simulate); `progress`
  // hears the server's {op: "progress"} pushes for this call.
  async op<T = unknown>(op: string, args: Record<string, unknown> = {}, progress?: (stage: string, fraction: number) => void): Promise<T> {
    return (await this.send({op, ...args}, progress)) as T;
  }

  private async send(message: Record<string, unknown>, progress?: Waiter['progress']): Promise<unknown> {
    await this.open;
    const id = this.next++;
    return new Promise((resolve, reject) => {
      this.waiting.set(id, {resolve, reject, progress});
      this.ws.send(JSON.stringify({id, ...message}));
    });
  }

  close(): void {
    this.closing = true;
    this.ws.close();
  }

  private receive(text: string): void {
    let msg: Reply;
    try {
      msg = JSON.parse(text) as Reply;
    } catch {
      return;
    }
    if (msg.op === 'status' && msg.state) {
      this.setStatus({state: msg.state, version: msg.version ?? ''});
      return;
    }
    const waiter = msg.id === undefined ? undefined : this.waiting.get(msg.id);
    if (!waiter || msg.id === undefined) return;
    if (msg.op === 'progress') {
      waiter.progress?.(msg.stage ?? '', msg.fraction ?? 0);
      return;
    }
    this.waiting.delete(msg.id);
    if (msg.ok) waiter.resolve(msg.result ?? '');
    else waiter.reject(new Error(msg.error ?? 'omc call failed'));
  }

  private setStatus(status: OmcStatus): void {
    this.status = status;
    this.onStatus(status);
  }
}

// The top bar's chip (contracts §1): `omc 1.27.0 · ready`.
export function chip(status: OmcStatus): string {
  const version = /\d+\.\d+\.\d+/.exec(status.version)?.[0];
  return `omc${version ? ` ${version}` : ''} · ${status.state}`;
}
