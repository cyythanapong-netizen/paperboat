// The console's mark: the web logo's paper boat on five water dots, drawn as a half-cell
// bitmap so the boat can bob and the dots can ripple without moving the layout. Pure
// geometry, so test/logo.test.ts runs it directly.

export type LogoTone = 'light' | 'shade' | 'water';

/** One run of cells with the same ink on one terminal row. */
export interface LogoSpan {
  text: string;
  tone: LogoTone;
}

export const LOGO_WIDTH = 23;
export const LOGO_ROWS = 9;

/** Two-tone boat, six rows tall, as the banner has always drawn it. */
const BOAT: {tone: LogoTone; text: string}[][] = [
  [{tone: 'light', text: '          ▄█'}, {tone: 'shade', text: '▄'}, {tone: 'light', text: '          '}],
  [{tone: 'light', text: '        ▄███'}, {tone: 'shade', text: '██▄'}, {tone: 'light', text: '        '}],
  [{tone: 'light', text: '      ▄█████'}, {tone: 'shade', text: '████▄'}, {tone: 'light', text: '      '}],
  [{tone: 'light', text: '▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄'}],
  [{tone: 'light', text: ' ▀███████████████████▀ '}],
  [{tone: 'light', text: '   ▀███████████████▀   '}],
];

/** A box-drawing cell's two vertical pixels: [top, bottom]. */
const HALVES: Record<string, [number, number]> = {
  '█': [1, 1],
  '▀': [1, 0],
  '▄': [0, 1],
  ' ': [0, 0],
};

/** The boat starts one cell down, leaving half a row for the bob. */
const BOAT_TOP = 2;
/** Water at rest: the bottom cell of the mark. */
const WATER_Y = 16;
const WATER_XS = [0, 5, 10, 15, 20];
const WATER_W = 3;
/** 1.4 s cycle, dots 0.12 s apart. */
const DOT_STAGGER = 0.12 / 1.4;

/** The mark at a point in the ripple: phase 0 is the start, 1 is at rest. */
export function logoFrame(phase: number): LogoSpan[][] {
  const p = Math.min(1, Math.max(0, phase));
  const px: (LogoTone | null)[][] = Array.from({length: LOGO_ROWS * 2}, () => Array<LogoTone | null>(LOGO_WIDTH).fill(null));

  const boat: (LogoTone | null)[][] = Array.from({length: BOAT.length * 2}, () => Array<LogoTone | null>(LOGO_WIDTH).fill(null));
  BOAT.forEach((row, r) => {
    let x = 0;
    for (const part of row) {
      for (const ch of part.text) {
        const [top, bottom] = HALVES[ch] ?? HALVES[' '];
        if (top) boat[r * 2][x] = part.tone;
        if (bottom) boat[r * 2 + 1][x] = part.tone;
        x++;
      }
    }
  });

  // The web mark bobs one pixel up as the crest passes under the hull.
  const bob = p <= 0.3 ? -p / 0.3 : p <= 0.6 ? -(1 - (p - 0.3) / 0.3) : 0;
  const shift = Math.round(bob - 1e-9);
  boat.forEach((row, y) => {
    row.forEach((tone, x) => {
      const at = BOAT_TOP + y + shift;
      if (tone && at >= 0 && at < px.length) px[at][x] = tone;
    });
  });

  // The crest travels left to right: each dot lifts, then settles.
  for (const [i, x] of WATER_XS.entries()) {
    const local = p - i * DOT_STAGGER;
    const lift = local <= 0 || local >= 0.6 ? 0 : local < 0.3 ? Math.round((2 * local) / 0.3) : Math.round(2 * (1 - (local - 0.3) / 0.3));
    for (let row = WATER_Y - lift; row < WATER_Y - lift + 2; row++) {
      if (row < 0 || row >= px.length) continue;
      for (let c = x; c < x + WATER_W; c++) px[row][c] = 'water';
    }
  }

  const frame: LogoSpan[][] = [];
  for (let r = 0; r < LOGO_ROWS; r++) {
    const spans: LogoSpan[] = [];
    let span: LogoSpan | null = null;
    for (let c = 0; c < LOGO_WIDTH; c++) {
      const top = px[r * 2][c];
      const bottom = px[r * 2 + 1][c];
      const ch = top && bottom ? '█' : top ? '▀' : bottom ? '▄' : ' ';
      const tone: LogoTone = top ?? bottom ?? 'light';
      if (span && span.tone === tone) span.text += ch;
      else spans.push((span = {text: ch, tone}));
    }
    frame.push(spans);
  }
  return frame;
}

/** A row of the frame as plain text, for tests and measurements. */
export const logoRow = (frame: LogoSpan[][], row: number): string => frame[row].map(span => span.text).join('');
