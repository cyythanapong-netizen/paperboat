// Canvas renderers for the mockups: a projected height field, a heatmap, sparklines,
// a run ribbon and a line chart. No dependencies; sized from the element's CSS box.

const DPR = () => window.devicePixelRatio || 1;

function prep(cv) {
  const r = cv.getBoundingClientRect();
  const w = Math.max(1, r.width), h = Math.max(1, r.height);
  cv.width = Math.round(w * DPR());
  cv.height = Math.round(h * DPR());
  const ctx = cv.getContext("2d");
  ctx.setTransform(DPR(), 0, 0, DPR(), 0, 0);
  return { ctx, w, h };
}

const VIR = [
  [68, 1, 84], [65, 68, 135], [42, 120, 142], [34, 168, 132],
  [74, 193, 109], [122, 209, 81], [253, 231, 37],
];

function viridis(t) {
  t = Math.max(0, Math.min(1, t));
  const x = t * (VIR.length - 1);
  const i = Math.min(VIR.length - 2, Math.floor(x));
  const f = x - i;
  const a = VIR[i], b = VIR[i + 1];
  return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}

function css(c, a = 1) {
  return `rgba(${Math.round(c[0])},${Math.round(c[1])},${Math.round(c[2])},${a})`;
}

function height(u, v) {
  const r = Math.hypot(u, v) * 1.55 + 1e-4;
  const s = Math.sin(r * 3.4) / (r * 3.4);
  return s;
}

function normalize(f, n) {
  let lo = Infinity, hi = -Infinity;
  for (let i = 0; i <= n; i++) for (let j = 0; j <= n; j++) {
    const z = f(-1 + (2 * i) / n, -1 + (2 * j) / n);
    if (z < lo) lo = z; if (z > hi) hi = z;
  }
  return (z) => (z - lo) / (hi - lo || 1);
}

export function surface(cv, opts = {}) {
  const { ctx, w, h } = prep(cv);
  const n = opts.n || 84;
  const nz = normalize(opts.f || height, n);
  const f = (u, v) => nz((opts.f || height)(u, v));
  const ax = Math.min(w * 0.30, h * 0.72), ay = h * 0.095, az = h * 0.37;
  const cx = w * 0.48, cy = h * 0.53;

  const project = (u, v, z) => ({
    x: cx + (u - v) * ax,
    y: cy + (u + v) * ay - (z - 0.5) * az,
  });

  const P = [];
  for (let i = 0; i <= n; i++) {
    P[i] = [];
    for (let j = 0; j <= n; j++) {
      const u = -1 + (2 * i) / n, v = -1 + (2 * j) / n;
      const z = f(u, v);
      P[i][j] = { ...project(u, v, z), z };
    }
  }

  // A ghost of the previous run, wireframe only.
  if (opts.ghost) {
    ctx.lineWidth = 0.5;
    ctx.strokeStyle = "rgba(233,233,228,0.16)";
    const step = Math.max(2, Math.floor(n / 18));
    for (let i = 0; i <= n; i += step) {
      ctx.beginPath();
      for (let j = 0; j <= n; j++) { const p = P[i][j]; j ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y); }
      ctx.stroke();
    }
    for (let j = 0; j <= n; j += step) {
      ctx.beginPath();
      for (let i = 0; i <= n; i++) { const p = P[i][j]; i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y); }
      ctx.stroke();
    }
  }

  const quads = [];
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
    const z = (P[i][j].z + P[i + 1][j].z + P[i][j + 1].z + P[i + 1][j + 1].z) / 4;
    quads.push({ i, j, z, d: i + j });
  }
  quads.sort((a, b) => a.d - b.d);

  ctx.lineWidth = 0.4;
  ctx.strokeStyle = "rgba(0,0,0,0.13)";
  const L = (() => { const l = [-0.42, -0.52, 0.74], m = Math.hypot(...l); return l.map((x) => x / m); })();
  for (const q of quads) {
    const { i, j } = q;
    const a = P[i][j], b = P[i + 1][j], c = P[i + 1][j + 1], d = P[i][j + 1];
    const dzdu = (P[i + 1][j].z - a.z) * n * 0.5;
    const dzdv = (P[i][j + 1].z - a.z) * n * 0.5;
    const nx = -dzdu * 1.9, ny = -dzdv * 1.9, nzz = 1;
    const nm = Math.hypot(nx, ny, nzz);
    const lum = Math.max(0, (nx / nm) * L[0] + (ny / nm) * L[1] + (nzz / nm) * L[2]);
    const shade = 0.62 + 0.5 * lum;
    const col = viridis(q.z).map((v) => v * shade);
    ctx.fillStyle = css(col);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.lineTo(c.x, c.y); ctx.lineTo(d.x, d.y); ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }

  if (opts.curtain !== undefined) {
    const u = opts.curtain;
    const corners = [project(u, -1, 0), project(u, 1, 0), project(u, 1, 1), project(u, -1, 1)];
    ctx.beginPath();
    ctx.moveTo(corners[0].x, corners[0].y);
    for (let k = 1; k < 4; k++) ctx.lineTo(corners[k].x, corners[k].y);
    ctx.closePath();
    ctx.fillStyle = "rgba(255,90,31,0.055)";
    ctx.fill();
    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = "rgba(255,90,31,0.38)";
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.beginPath();
    for (let j = 0; j <= n; j++) {
      const v = -1 + (2 * j) / n;
      const p = project(u, v, f(u, v));
      j ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y);
    }
    ctx.strokeStyle = "#ff5a1f";
    ctx.lineWidth = 2.1;
    ctx.shadowColor = "rgba(255,90,31,0.55)";
    ctx.shadowBlur = 8;
    ctx.stroke();
    ctx.shadowBlur = 0;
  }
}

export function ghostSurface(cv, opts = {}) {
  surface(cv, { ...opts, ghost: false });
}

export function heatmap(cv, opts = {}) {
  const { ctx, w, h } = prep(cv);
  const n = 64;
  const nz = normalize(height, n);
  const cw = w / n, ch = h / n;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
    const z = nz(height(-1 + (2 * i) / n, -1 + (2 * j) / n));
    ctx.fillStyle = css(viridis(z));
    ctx.fillRect(i * cw, j * ch, cw + 0.5, ch + 0.5);
  }
  ctx.strokeStyle = "rgba(255,255,255,0.06)";
  ctx.lineWidth = 0.5;
  for (let i = 8; i < n; i += 8) {
    ctx.beginPath(); ctx.moveTo(i * cw, 0); ctx.lineTo(i * cw, h); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, i * ch); ctx.lineTo(w, i * ch); ctx.stroke();
  }
  if (opts.cell) {
    const { i, j } = opts.cell;
    ctx.strokeStyle = "#ff5a1f";
    ctx.lineWidth = 1.6;
    ctx.strokeRect(i * cw, j * ch, cw, ch);
  }
}

export function spark(cv, opts = {}) {
  const { ctx, w, h } = prep(cv);
  const n = opts.n || 60;
  const color = opts.color || "#ff5a1f";
  const pad = 2;
  let lo = Infinity, hi = -Infinity;
  const ys = [];
  for (let i = 0; i < n; i++) {
    const t = i / (n - 1);
    const y = opts.f ? opts.f(t) : Math.sin(t * 9 + (opts.phase || 0)) * 0.6 + Math.sin(t * 23) * 0.18 + t * 0.4;
    ys.push(y); if (y < lo) lo = y; if (y > hi) hi = y;
  }
  const span = hi - lo || 1;
  ctx.beginPath();
  for (let i = 0; i < n; i++) {
    const x = pad + (i / (n - 1)) * (w - pad * 2);
    const y = h - pad - ((ys[i] - lo) / span) * (h - pad * 2);
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  }
  ctx.strokeStyle = color;
  ctx.lineWidth = opts.width || 1.2;
  ctx.stroke();
  if (opts.fill) {
    ctx.lineTo(w - pad, h - pad); ctx.lineTo(pad, h - pad); ctx.closePath();
    ctx.fillStyle = color.replace("rgb(", "rgba(").replace(")", ",0.14)");
    ctx.fill();
  }
}

export function ribbon(cv) {
  const { ctx, w, h } = prep(cv);
  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 0.5;
  for (let x = 0; x < w; x += w / 12) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke(); }
  const traces = [
    { c: "rgba(255,90,31,0.95)", w: 1.4, f: (t) => Math.sin(t * 12) * 0.5 + Math.sin(t * 31) * 0.18 },
    { c: "rgba(240,240,235,0.55)", w: 0.9, f: (t) => Math.cos(t * 7) * 0.35 + Math.sin(t * 19) * 0.1 },
    { c: "rgba(122,209,81,0.55)", w: 0.9, f: (t) => Math.sin(t * 5 + 1.4) * 0.3 },
  ];
  for (const tr of traces) {
    ctx.beginPath();
    for (let i = 0; i <= 200; i++) {
      const t = i / 200;
      const x = t * w;
      const y = h * 0.56 - tr.f(t) * h * 0.36;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.strokeStyle = tr.c; ctx.lineWidth = tr.w; ctx.stroke();
  }
}

export function linechart(cv) {
  const { ctx, w, h } = prep(cv);
  const pad = { l: 30, r: 10, t: 12, b: 18 };
  ctx.strokeStyle = "rgba(255,255,255,0.075)";
  ctx.lineWidth = 0.5;
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + (i / 4) * (h - pad.t - pad.b);
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    const x = pad.l + (i / 4) * (w - pad.l - pad.r);
    ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, h - pad.b); ctx.stroke();
  }
  ctx.fillStyle = "#8a8a84";
  ctx.font = "9px 'Geist Mono', monospace";
  ctx.fillText("0.5", 6, pad.t + 3);
  ctx.fillText("−0.5", 2, h - pad.b + 2);
  const series = [
    { c: "#ff5a1f", w: 1.5, f: (t) => Math.sin(t * 11) * 0.5 + Math.sin(t * 27) * 0.14 },
    { c: "rgba(240,240,235,0.6)", w: 1, f: (t) => Math.cos(t * 6 + 0.8) * 0.34 },
  ];
  for (const s of series) {
    ctx.beginPath();
    for (let i = 0; i <= 240; i++) {
      const t = i / 240;
      const x = pad.l + t * (w - pad.l - pad.r);
      const y = h / 2 - s.f(t) * (h - pad.t - pad.b) * 0.42;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.strokeStyle = s.c; ctx.lineWidth = s.w; ctx.stroke();
  }
}

export function scatter(cv) {
  const { ctx, w, h } = prep(cv);
  const cx = w * 0.52, cy = h * 0.54;
  const ax = w * 0.30, ay = h * 0.14, az = h * 0.34;
  const pts = [];
  for (let i = 0; i < 260; i++) {
    const t = i / 259;
    const a = t * Math.PI * 5.2;
    const r = 0.15 + t * 0.85;
    const u = Math.cos(a) * r, v = Math.sin(a) * r;
    const z = (Math.sin(a * 1.7) * 0.5 + 0.5) * 0.8 + t * 0.1;
    pts.push({
      x: cx + (u - v) * ax,
      y: cy + (u + v) * ay - (z - 0.5) * az,
      z, d: u + v + z * 0.2,
    });
  }
  pts.sort((a, b) => a.d - b.d);
  for (const p of pts) {
    ctx.fillStyle = css(viridis(p.z), 0.92);
    ctx.beginPath(); ctx.arc(p.x, p.y, 1.7, 0, Math.PI * 2); ctx.fill();
  }
}
