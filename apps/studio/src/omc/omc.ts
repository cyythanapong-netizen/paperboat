// Studio's line to omc (#160 contracts §5): squero/0 over the Studio server's WebSocket
// (`paperboat squero` / `squero-om`). One call is one omc expression; the server checks it
// against its allow-list and answers in order. Status pushes drive the top-bar chip.

export type OmcState = 'ready' | 'checking' | 'failed';
export type OmcStatus = {state: OmcState; version: string};

type Reply = {id?: number; ok?: boolean; result?: string; error?: string; op?: string} & Partial<OmcStatus>;
type Waiter = {resolve: (result: string) => void; reject: (error: Error) => void};

export class OmcClient {
  status: OmcStatus = {state: 'checking', version: ''};
  private readonly ws: WebSocket;
  private readonly waiting = new Map<number, Waiter>();
  private next = 1;
  private closing = false;
  private readonly open: Promise<void>;

  constructor(url: string, private readonly onStatus: (status: OmcStatus) => void = () => {}) {
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
    await this.open;
    const id = this.next++;
    return new Promise((resolve, reject) => {
      this.waiting.set(id, {resolve, reject});
      this.ws.send(JSON.stringify({id, op: 'omc', expr}));
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
