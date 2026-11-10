// What the console draws: a fixed mark, a scrolling transcript, the live area (running tasks
// and the open panel), the input line and the two-line status bar. Full screen, so the mark
// can ripple and the transcript belongs to the console rather than the terminal's scrollback.
// Colors follow apps/web/DESIGN.md: the terminal's own text color and greys, one signal orange
// for what is live, red only for failures.

import {Box, Text, useAnimation, useApp, useBoxMetrics, useInput, useWindowSize, type DOMElement} from 'ink';
import {useEffect, useRef, useState, type ReactNode} from 'react';
import {homedir} from 'node:os';

import {handleKey} from './keys.ts';
import {LOGO_ROWS, LOGO_WIDTH, logoFrame} from './logo.ts';
import {ghost, isInstance, searchHistory} from './model.ts';
import {CLI_VERSION} from './version.ts';
import {
  S, alive, busy, dashRows, doing, init, kb, openDash, openPicker, openToolkits, pickList, sec, sources, stepLine,
  useStore, working, type Out, type Seg, type Task,
} from './store.ts';

const SIGNAL = '#ff6a30';
const SIGNAL_HI = '#ffa064';
const DANGER = '#ff5d55';

/** The launch ripple, then one every 18 s; continuously while work is live. Frames only run during a cycle. */
const RIPPLE_MS = 1400;
const IDLE_MS = 18000;

function useRipple(working: boolean, pulse: number): number {
  const [start, setStart] = useState(() => Date.now());
  const [now, setNow] = useState(() => Date.now());
  const [active, setActive] = useState(true);
  const seen = useRef(pulse);
  const was = useRef(working);

  useEffect(() => {
    if (pulse !== seen.current) {
      seen.current = pulse;
      setStart(Date.now());
      setNow(Date.now());
      setActive(true);
    }
  }, [pulse]);

  useEffect(() => {
    if (working && !was.current) {
      setStart(Date.now());
      setNow(Date.now());
      setActive(true);
    }
    was.current = working;
  }, [working]);

  useEffect(() => {
    if (!active) return;
    const tick = setInterval(() => setNow(Date.now()), 70);
    return () => clearInterval(tick);
  }, [active]);

  useEffect(() => {
    if (!active) {
      const next = setTimeout(() => {
        setStart(Date.now());
        setNow(Date.now());
        setActive(true);
      }, IDLE_MS);
      return () => clearTimeout(next);
    }
    if (working) {
      const next = setTimeout(() => {
        setStart(Date.now());
        setNow(Date.now());
      }, RIPPLE_MS);
      return () => clearTimeout(next);
    }
    // Let the cycle in flight finish, then rest until the idle timer.
    const next = setTimeout(() => setActive(false), Math.max(70, RIPPLE_MS - (Date.now() - start)));
    return () => clearTimeout(next);
  }, [active, working, start]);

  return active ? Math.min(1, Math.max(0, (now - start) / RIPPLE_MS)) : 1;
}

/** The mark itself, at a point in the ripple. */
function Mark({phase}: {phase: number}) {
  const frame = logoFrame(phase);
  return (
    <Box flexDirection="column" width={LOGO_WIDTH} flexShrink={0}>
      {frame.map((row, i) => (
        <Text key={i}>
          {row.map((span, k) =>
            span.tone === 'water' ? (
              <Text key={k} color={SIGNAL}>{span.text}</Text>
            ) : span.tone === 'shade' ? (
              <Text key={k} dimColor>{span.text}</Text>
            ) : (
              <Text key={k}>{span.text}</Text>
            ),
          )}
        </Text>
      ))}
    </Box>
  );
}

function Fact({label, value}: {label: string; value: string}) {
  return (
    <Text wrap="truncate">
      <Text dimColor>{label.padEnd(22)}</Text>
      <Text>{value}</Text>
    </Text>
  );
}

/** What both the banner and `about` say about this install. */
function installedFacts(): {label: string; value: string}[] {
  const toolkits = S.toolkits.filter(t => t.installed).length;
  const apps = S.programs.filter(p => p.installed).length;
  return [
    {label: 'Paperboat CLI version', value: CLI_VERSION},
    {label: 'paperboat version', value: S.version || 'unknown'},
    {label: 'installed toolkits', value: `${toolkits} ${toolkits === 1 ? 'toolkit' : 'toolkits'}`},
    {label: 'installed apps', value: `${apps} ${apps === 1 ? 'app' : 'apps'}`},
  ];
}

function Banner({working, pulse, columns}: {working: boolean; pulse: number; columns: number}) {
  const phase = useRipple(working, pulse);
  const cwd = process.cwd().replace(homedir(), '~');
  return (
    <Box flexDirection="column" flexShrink={0}>
      <Box height={LOGO_ROWS} paddingX={1} gap={3} flexShrink={0}>
        <Mark phase={phase} />
        {columns >= 52 && (
          <Box flexDirection="column" justifyContent="center" flexShrink={1} minWidth={0}>
            <Text bold wrap="truncate">Paperboat console</Text>
            <Text dimColor wrap="truncate">C++20 core · protocol paperboat/0</Text>
            {installedFacts().map(f => <Fact key={f.label} {...f} />)}
            <Text dimColor wrap="truncate">{cwd}</Text>
          </Box>
        )}
      </Box>
      <Text dimColor>{` ${'─'.repeat(Math.max(0, columns - 2))} `}</Text>
    </Box>
  );
}

/** `about`: the mark (always rippling) and what this console is made of. */
function About() {
  const phase = useRipple(true, 0);
  return (
    <Panel title={<Text><Text bold>About</Text><Text dimColor> · paperboat cli</Text></Text>}>
      <Box flexDirection="row" flexGrow={1} alignItems="center" paddingX={2} gap={4}>
        <Mark phase={phase} />
        <Box flexDirection="column" gap={1} flexShrink={1} minWidth={0}>
          <Text bold>Paperboat CLI</Text>
          <Box flexDirection="column">
            {installedFacts().map(f => <Fact key={f.label} {...f} />)}
          </Box>
        </Box>
      </Box>
    </Panel>
  );
}

function Segment({seg}: {seg: Seg}) {
  switch (seg.tone) {
    case 'muted':
      return <Text dimColor>{seg.t}</Text>;
    case 'faint':
      return <Text color="gray">{seg.t}</Text>;
    case 'signal':
      return <Text color={SIGNAL}>{seg.t}</Text>;
    case 'hi':
      return <Text color={SIGNAL_HI} bold>{seg.t}</Text>;
    case 'danger':
      return <Text color={DANGER}>{seg.t}</Text>;
    case 'bold':
      return <Text bold>{seg.t}</Text>;
    default:
      return <Text>{seg.t}</Text>;
  }
}

function Row({line, wrap = 'wrap'}: {line: Out; wrap?: 'wrap' | 'truncate'}) {
  return (
    <Text wrap={wrap}>
      {line.length ? line.map((seg, i) => <Segment key={i} seg={seg} />) : ' '}
    </Text>
  );
}

/** The transcript, scrolled by rows: follow the tail unless the reader has paged up. */
function Transcript({columns}: {columns: number}) {
  const viewport = useRef<DOMElement | null>(null);
  const content = useRef<DOMElement | null>(null);
  const vp = useBoxMetrics(viewport);
  const ct = useBoxMetrics(content);
  const max = Math.max(0, ct.height - vp.clientHeight);
  const offset = S.scroll < 0 ? max : Math.min(Math.max(0, S.scroll), max);

  useEffect(() => {
    S.view.height = vp.clientHeight;
    S.view.total = ct.height;
  }, [vp.clientHeight, ct.height]);

  return (
    <Box ref={viewport} width={columns} flexGrow={1} flexShrink={1} minHeight={0} overflow="hidden">
      <Box flexDirection="column" contentOffsetY={offset} paddingX={1}>
        <Box ref={content} flexDirection="column" flexShrink={0}>
          {S.transcript.map((line, i) => <Row key={i} line={line} />)}
        </Box>
      </Box>
    </Box>
  );
}

/** The 5-dot wave from the logo's water line, the crest moving along: the one spinner. */
function Ripple({frame}: {frame: number}) {
  const crest = frame % 5;
  return (
    <Text>
      {[0, 1, 2, 3, 4].map(d =>
        d === crest ? <Text key={d} color={SIGNAL_HI} bold>▀</Text> : <Text key={d} color={SIGNAL}>▄</Text>,
      )}
    </Text>
  );
}

function Panel({title, children, grow = true}: {title: ReactNode; children: ReactNode; grow?: boolean}) {
  return (
    <Box flexDirection="column" flexGrow={grow ? 1 : 0} flexShrink={grow ? 1 : 0} minHeight={0} borderStyle="round" borderColor="gray" paddingX={1}>
      {title}
      <Box flexDirection="column" flexGrow={grow ? 1 : 0} flexShrink={grow ? 1 : 0} minHeight={0} overflow="hidden">
        {children}
      </Box>
    </Box>
  );
}

/** Highlight matched characters (fuzzy filter, completion). */
function Matched({text, match}: {text: string; match: number[]}) {
  return (
    <Text>
      {[...text].map((c, i) => (match.includes(i) ? <Text key={i} color={SIGNAL} underline>{c}</Text> : c))}
    </Text>
  );
}

function TaskView({task, frame, now}: {task: Task; frame: number; now: number}) {
  if (task.kind === 'check') {
    return (
      <Box flexDirection="column">
        <Row line={[{t: 'check ', tone: 'bold'}, {t: task.title}]} />
        {task.steps.map(step =>
          step.state === 'running' ? (
            <Text key={step.name}>
              {'  '}<Ripple frame={frame} />{' '}{step.name.padEnd(10)}
              <Text color={SIGNAL}>running…</Text>
              <Text dimColor>{`  ${sec(now - step.t0)}`}</Text>
            </Text>
          ) : (
            <Row key={step.name} line={stepLine(step, now)} />
          ),
        )}
      </Box>
    );
  }
  if (task.kind === 'pack' || task.kind === 'unpack') {
    const {files, total, bytes} = task.progress;
    const width = 28;
    const filled = total ? Math.round((files / total) * width) : 0;
    const rate = bytes / Math.max(0.001, (now - task.t0) / 1000);
    return (
      <Text>
        <Text bold>{task.kind} </Text>
        {task.title}
        {'  '}<Ripple frame={frame} />{' '}
        <Text color={SIGNAL}>{'━'.repeat(filled)}</Text>
        <Text color="gray">{'━'.repeat(width - filled)}</Text>
        {` ${files}/${total || '?'} files`}
        <Text dimColor>{` · ${kb(bytes)} · ${kb(rate)}/s`}</Text>
      </Text>
    );
  }
  if (task.kind === 'toolkit') {
    return (
      <Box flexDirection="column">
        {task.ops.map(op => {
          const verb = {install: 'installing', uninstall: 'removing', update: 'updating'}[op.op] ?? op.op;
          return (
            <Text key={op.name + op.op}>
              {'  '}
              {op.state === 'running' ? <Ripple frame={frame} /> : '    '}
              {op.state === 'done' ? <Text bold>✓</Text> : op.state === 'failed' ? <Text color={DANGER}>✗</Text> : op.state === 'queued' ? <Text color="gray">·</Text> : ''}
              {` ${op.name.padEnd(10)}`}
              {op.state === 'running' ? (
                <Text>
                  <Text color={SIGNAL}>{`${verb}… `}</Text>
                  <Text dimColor>{`${sec(now - op.t0)}  ${op.log[op.log.length - 1] ?? ''}`}</Text>
                </Text>
              ) : (
                <Text dimColor>{op.state}</Text>
              )}
            </Text>
          );
        })}
      </Box>
    );
  }
  return (
    <Text>
      {'  '}<Ripple frame={frame} />{' '}
      <Text dimColor>{task.title}</Text>
    </Text>
  );
}

function Picker() {
  const list = pickList(S.picker.query);
  return (
    <Panel title={<Text><Text bold>Run a backend</Text><Text dimColor>{` · ${list.length} of ${S.backends.length}`}</Text></Text>}>
      <Text>
        <Text color={SIGNAL}>› </Text>
        {S.picker.query}
        <Text inverse> </Text>
        {S.picker.query ? '' : <Text color="gray">type to filter</Text>}
      </Text>
      {list.map(({backend: x, match}, i, all) => {
        const width = Math.max(...all.map(r => r.backend.name.length)) + 2;
        const on = i === S.picker.i;
        const running = S.jobs.some(j => isInstance(j.name, x.name) && alive(j) && j.kind !== 'build') || S.external.some(e => e.port === x.port);
        return (
          <Text key={x.name} wrap="truncate" bold={on}>
            {on ? <Text color={SIGNAL}>▸ </Text> : '  '}
            <Matched text={x.name.padEnd(width)} match={match} />
            <Text dimColor>{`${x.language.padEnd(8)}:${String(x.port).padEnd(6)}`}</Text>
            {running ? <Text color={SIGNAL}>● running  </Text> : '           '}
            {x.description}
          </Text>
        );
      })}
      {!list.length && <Text dimColor>{S.backends.length ? 'nothing matches · backspace to widen' : 'no backends found · set PAPERBOAT_EXAMPLES_DIR'}</Text>}
    </Panel>
  );
}

function Dashboard({frame, now}: {frame: number; now: number}) {
  const rows = dashRows();
  const live = rows.filter(r => r.ext || alive(r.job!)).length;
  return (
    <Panel title={<Text><Text bold>Processes</Text><Text dimColor>{` · ${live} live · ${rows.length - live} exited`}</Text></Text>}>
      <Text dimColor>{`  ${'STATE'.padEnd(12)}${'NAME'.padEnd(20)}${'PORT'.padEnd(7)}${'PID'.padEnd(9)}${'UPTIME'.padEnd(9)}FROM`}</Text>
      {rows.map((r, i) => {
        const on = i === S.dash.i;
        const j = r.job;
        const state = j ? (
          j.state === 'running' ? <Text color={SIGNAL}>● running   </Text>
          : j.state === 'building' ? <Text><Ripple frame={frame} />{'       '}</Text>
          : j.stopped ? <Text dimColor>■ stopped   </Text>
          : j.code === 0 ? <Text>✓ done      </Text>
          : <Text color={DANGER}>{`✗ exit ${j.code}`.padEnd(12)}</Text>
        ) : <Text dimColor>● running   </Text>;
        const name = j ? j.name : r.ext!.name;
        const port = j ? j.port || '-' : r.ext!.port;
        const pid = j ? j.pid : r.ext!.pid;
        const up = j && j.state === 'running' ? `${Math.round((now - j.ready) / 1000)}s` : '—';
        return (
          <Text key={j ? `j${j.id}` : `e${r.ext!.port}`} wrap="truncate" bold={on}>
            {on ? <Text color={SIGNAL}>▸ </Text> : '  '}
            {state}
            {`${name.padEnd(20)}${String(port).padEnd(7)}${String(pid).padEnd(9)}`}
            <Text dimColor>{`${up.padEnd(9)}${j ? 'console' : 'outside'}`}</Text>
          </Text>
        );
      })}
      {!rows.length && <Text dimColor>nothing running · run &lt;backend&gt; starts one</Text>}
    </Panel>
  );
}

function Toolkits() {
  const changes = S.toolkits.flatMap((t, i) => (t.builtin || S.tk.want[i] === t.installed ? [] : [`${S.tk.want[i] ? '+' : '−'} ${t.name}`]));
  return (
    <Panel title={<Text><Text bold>Toolkits</Text><Text dimColor>{` · ${S.toolkits.filter(t => t.installed).length} installed`}</Text></Text>}>
      {S.toolkits.map((t, i) => {
        const on = i === S.tk.i;
        return (
          <Text key={t.name} wrap="truncate" bold={on}>
            {on ? <Text color={SIGNAL}>▸ </Text> : '  '}
            {t.builtin ? <Text color="cyan">{'[built-in] '}</Text> : S.tk.want[i] ? '[✓]        ' : <Text color="gray">{'[ ]        '}</Text>}
            {t.name.padEnd(10)}
            {t.builtin ? <Text color="cyan">{'built-in'.padEnd(18)}</Text> : t.installed ? `installed ${t.version ?? ''}`.padEnd(18) : <Text color="gray">{'available'.padEnd(18)}</Text>}
            <Text dimColor>{t.description}</Text>
          </Text>
        );
      })}
      {!S.toolkits.length && <Text dimColor>loading toolkits…</Text>}
      <Text>{changes.length ? <Text color={SIGNAL}>{`changes: ${changes.join(', ')}`}</Text> : <Text color="gray">no changes</Text>}</Text>
    </Panel>
  );
}

function LogView() {
  const job = S.jobs.find(j => j.id === S.logs.job)!;
  const viewport = useRef<DOMElement | null>(null);
  const vp = useBoxMetrics(viewport);
  const offset = Math.max(0, job.log.length - Math.max(1, vp.clientHeight));
  return (
    <Panel
      title={
        <Text>
          <Text bold>logs </Text>
          {job.name}
          <Text dimColor>{` · ${job.port ? `:${job.port} · ` : ''}pid ${job.pid} · `}</Text>
          {alive(job) ? <Text color={SIGNAL}>following</Text> : <Text color={DANGER}>{`exited ${job.code}`}</Text>}
        </Text>
      }
    >
      <Box ref={viewport} flexGrow={1} flexShrink={1} minHeight={0} overflow="hidden">
        <Box flexDirection="column" contentOffsetY={offset}>
          <Box flexDirection="column" flexShrink={0}>
            {job.log.map((l, i) => (
              <Text key={i} wrap="truncate">
                <Text color="gray">│ </Text>
                {/error|fail/i.test(l) ? <Text color={DANGER}>{l}</Text> : l}
              </Text>
            ))}
          </Box>
        </Box>
      </Box>
    </Panel>
  );
}

function Input({menuRows}: {menuRows: number}) {
  if (S.search) {
    const match = searchHistory(S.history, S.search.query)[S.search.i];
    const at = match ? match.indexOf(S.search.query) : -1;
    return (
      <Box flexDirection="column">
        <Box borderStyle="round" borderColor="gray" paddingX={1}>
          <Text>
            <Text color={SIGNAL}>› </Text>
            {match ? <Matched text={match} match={S.search.query ? [...S.search.query].map((_, k) => at + k) : []} /> : <Text color="gray">no match</Text>}
          </Text>
        </Box>
        <Text>
          {'  '}<Text dimColor>history search </Text>
          <Text color={SIGNAL}>{S.search.query || ' '}</Text>
          <Text color="gray"> · ctrl+r older · enter take · esc cancel</Text>
        </Text>
      </Box>
    );
  }
  const {text, cursor} = S.line;
  const suggestion = cursor === text.length && !S.menu ? ghost(text, S.history, sources()) : '';
  const under = text[cursor] ?? suggestion[0] ?? ' ';
  const rest = cursor < text.length ? text.slice(cursor + 1) : '';
  return (
    <Box flexDirection="column">
      <Box borderStyle="round" borderColor="gray" paddingX={1}>
        <Text>
          <Text color={SIGNAL}>› </Text>
          {text.slice(0, cursor)}
          <Text inverse>{under}</Text>
          {rest}
          <Text color="gray">{cursor === text.length ? suggestion.slice(1) : ''}</Text>
          {text ? '' : <Text color="gray">run, check, top, toolkit… or help</Text>}
        </Text>
      </Box>
      {S.menu && <Menu rows={menuRows} />}
    </Box>
  );
}

function Menu({rows}: {rows: number}) {
  const {candidates, i} = S.menu!;
  const height = Math.max(1, rows);
  const top = Math.max(0, Math.min(i - Math.floor(height / 2), candidates.length - height));
  return (
    <Box flexDirection="column">
      {candidates.slice(top, top + height).map((c, k) => {
        const on = top + k === i;
        return (
          <Text key={c.value} wrap="truncate" bold={on}>
            {on ? <Text color={SIGNAL}>{'  ▸ '}</Text> : '    '}
            <Matched text={c.value.padEnd(18)} match={c.match} />
            <Text dimColor>{c.detail}</Text>
          </Text>
        );
      })}
      {candidates.length > height && <Text color="gray">{`      ${i + 1}/${candidates.length}`}</Text>}
    </Box>
  );
}

const TASK_LABEL: Partial<Record<Task['kind'], string>> = {check: 'checking', pack: 'packing', unpack: 'unpacking', toolkit: 'toolkits'};

const HINTS = {
  prompt: 'pgup/pgdn scroll · tab complete · → suggestion · ↑↓ history · ctrl+r search · top processes',
  picker: '↑↓ move · type to filter · enter run · esc cancel',
  dash: '↑↓ select · r restart · s stop · enter logs · esc back',
  toolkit: '↑↓ move · space toggle · u update · enter apply · esc cancel',
  logs: 'esc back',
  about: 'esc back',
};

function Hud({columns, frame, now}: {columns: number; frame: number; now: number}) {
  const services = S.jobs.filter(j => alive(j) && j.kind !== 'build');
  const building = S.jobs.find(j => j.state === 'building');
  const task = S.tasks[0];
  const hint =
    S.hint ||
    (S.scroll >= 0 ? 'scrolled · pgdn to follow' : S.confirm ? 'y stop them and quit · n stay' : S.menu ? 'tab next · shift+tab back · enter take · esc close' : HINTS[S.mode]);
  return (
    <Box flexDirection="column" width={columns} paddingX={1}>
      <Box gap={2}>
        <Box flexGrow={1} flexShrink={1} minWidth={0}>
          <Text wrap="truncate">
            {services.map(j => (
              <Text key={j.id}>
                <Text color={SIGNAL}>{j.state === 'running' ? '● ' : '◌ '}</Text>
                {j.name}
                <Text dimColor>{`:${j.port}   `}</Text>
              </Text>
            ))}
            {S.external.map(e => (
              <Text key={e.port}>
                <Text dimColor>● </Text>
                {e.name.startsWith(':') ? '' : e.name}
                <Text dimColor>{`:${e.port}   `}</Text>
              </Text>
            ))}
            {!services.length && !S.external.length && <Text color="gray">nothing running</Text>}
          </Text>
        </Box>
        <Box flexShrink={0}>
          {building ? (
            <Text>
              <Ripple frame={frame} />
              <Text color={SIGNAL}>{` ${doing(building)} ${building.name} `}</Text>
              <Text dimColor>{sec(now - building.t0)}</Text>
            </Text>
          ) : task ? (
            <Text>
              <Ripple frame={frame} />
              <Text color={SIGNAL}>{` ${TASK_LABEL[task.kind] ?? task.title.split(' ')[0]}`}</Text>
            </Text>
          ) : (
            <Text dimColor>{services.some(j => j.state === 'running') || S.external.length ? 'running' : 'idle'}</Text>
          )}
        </Box>
      </Box>
      <Box gap={2}>
        <Box flexGrow={1} flexShrink={1} minWidth={0}>
          <Text wrap="truncate" color={S.hint ? SIGNAL : 'gray'}>{hint}</Text>
        </Box>
        <Box flexShrink={0} maxWidth={Math.max(12, Math.floor(columns * 0.65))}>
          <Text color="gray" wrap="truncate">{[S.git, S.version && `engine ${S.version}`].filter(Boolean).join(' · ')}</Text>
        </Box>
      </Box>
    </Box>
  );
}

export function App({start}: {start?: string}) {
  useStore();
  const {exit} = useApp();
  const {columns, rows} = useWindowSize();
  const live = working();
  const {frame} = useAnimation({interval: 90, isActive: busy()});
  const now = Date.now();

  useEffect(() => {
    void init().then(() => {
      if (start === 'run') openPicker();
      else if (start === 'toolkit') openToolkits();
      else if (start === 'top') openDash();
    });
  }, [start]);
  useEffect(() => {
    if (S.quit) exit();
  });
  useInput((input, key) => handleKey(input, key));

  if (S.quit) return null;

  const showBanner = rows >= 18 && columns >= 30;
  const bannerRows = showBanner ? LOGO_ROWS + 1 : 0;
  const menuRows = Math.max(1, Math.min(7, rows - bannerRows - 9));
  const body =
    S.mode === 'picker' ? <Picker /> :
    S.mode === 'dash' ? <Dashboard frame={frame} now={now} /> :
    S.mode === 'toolkit' ? <Toolkits /> :
    S.mode === 'logs' && S.jobs.some(j => j.id === S.logs.job) ? <LogView /> :
    S.mode === 'about' ? <About /> :
    <Transcript columns={columns} />;

  return (
    <Box flexDirection="column" height={rows} width={columns} overflow="hidden">
      {showBanner && <Banner working={live} pulse={S.clears} columns={columns} />}
      <Box flexDirection="column" flexGrow={1} flexShrink={1} minHeight={0} overflow="hidden">
        {body}
        {S.tasks.length > 0 && (
          <Box flexDirection="column" flexShrink={0}>
            {S.tasks.map(t => <TaskView key={t.id} task={t} frame={frame} now={now} />)}
          </Box>
        )}
        {S.confirm && (
          <Box flexShrink={0}>
            <Panel grow={false} title={<Text><Text color={SIGNAL}>? </Text>{`stop ${S.jobs.filter(alive).map(j => j.name).join(', ')} and quit?`}</Text>}>
              <Text>
                <Text bold>y</Text><Text dimColor> stop and quit    </Text><Text bold>n</Text><Text dimColor> stay</Text>
              </Text>
            </Panel>
          </Box>
        )}
      </Box>
      <Box flexDirection="column" flexShrink={0}>
        {S.mode === 'prompt' && !S.confirm && <Input menuRows={menuRows} />}
        <Hud columns={columns} frame={frame} now={now} />
      </Box>
    </Box>
  );
}
