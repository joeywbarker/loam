// LOAM pre-alpha core: colonies, attention, territory. Same spirit as loam.h, bounded world.
const Core = (() => {
  const W = 48, H = 72, N = W * H, MAXW = 500, FOOD = 30, EMAX = 300;
  const DX = [1, 0, -1, 0], DY = [0, 1, 0, -1];
  const SP = 32, DOT = 46, WALL = 35, NEST = 64;
  const G = c => c & 255, O = c => (c >> 8) & 15, HT = c => (c >> 12) & 7;
  const MK = (g, o, h) => (g & 255) | ((o & 15) << 8) | ((h & 7) << 12);
  let S = null;   // state
  function R() { let x = S.rng; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; S.rng = x; return x; }
  const inb = (x, y) => x >= 0 && y >= 0 && x < W && y < H;

  function newWorld(seed, cols) {
    S = { rng: (seed >>> 0) || 1, tick: 0, cells: new Uint16Array(N), occ: new Int16Array(N), ws: [], cols: [] };
    S.cells.fill(MK(SP, 0, 0));
    for (let i = 0; i < N; i++) if ((R() % 9) === 0) S.cells[i] = MK(DOT, 0, 0);
    for (const c of cols) addColony(c);
    return S;
  }
  function addColony(c) {
    c.acc = 0; c.fed = 0;
    S.cols.push(c);
    S.cells[c.ny * W + c.nx] = MK(NEST, c.lin, 0);
    for (let dy = -3; dy <= 3; dy++) for (let dx = -3; dx <= 3; dx++) {
      const x = c.nx + dx, y = c.ny + dy;
      if (inb(x, y) && (dx || dy)) S.cells[y * W + x] = MK((R() % 3) ? SP : DOT, c.lin, 0);
    }
    for (let k = 0; k < 4; k++) spawnFromNest(c);
  }
  function spawn(x, y, dir, lin, e) {
    if (!inb(x, y)) return -1;
    const i = y * W + x; if (S.occ[i]) return -1;
    const u = S.cells[i], g = G(u); if (g === WALL || g === NEST) return -1;
    let k = -1; for (let j = 0; j < S.ws.length; j++) if (!S.ws[j].alive) { k = j; break; }
    if (k < 0) { if (S.ws.length >= MAXW) return -1; k = S.ws.length; S.ws.push({}); }
    const w = S.ws[k]; w.x = x; w.y = y; w.dir = dir & 3; w.lin = lin; w.e = e; w.alive = 1; w.wait = 0; w.under = u;
    if (g === DOT) { w.e += FOOD; w.under = MK(SP, O(u), HT(u)); }
    S.occ[i] = k + 1; return k;
  }
  function spawnFromNest(c) {
    const d = R() & 3; for (let t = 0; t < 4; t++) { const dd = (d + t) & 3; if (spawn(c.nx + DX[dd], c.ny + DY[dd], dd, c.lin, 70) >= 0) return true; }
    return false;
  }
  function kill(w) {
    const i = w.y * W + w.x; let u = w.under;
    if (G(u) === SP) u = MK(DOT, O(u), HT(u));
    S.cells[i] = u; S.occ[i] = 0; w.alive = 0;
  }
  function bud(w) {
    const b = (w.dir + 2) & 3, half = (w.e - 4) >> 1;
    if (spawn(w.x + DX[b], w.y + DY[b], b, w.lin, half) >= 0) w.e -= half + 4;
  }
  function move(w, k) {
    const nx = w.x + DX[w.dir], ny = w.y + DY[w.dir];
    if (!inb(nx, ny)) { w.dir = (w.dir + 2) & 3; return; }
    const ni = ny * W + nx;
    if (S.occ[ni]) { w.wait = 1; return; }
    let c = S.cells[ni]; const g = G(c);
    if (g === WALL || g === NEST) { w.dir = (w.dir + 2) & 3; w.wait = 0; return; }
    const oi = w.y * W + w.x; let u = w.under;
    u = G(u) === SP ? MK(SP, w.lin, 7) : ((u & 0x8FFF) | (7 << 12));       // walking claims bare ground
    S.cells[oi] = u; S.occ[oi] = 0;
    if (O(c) && O(c) !== w.lin) w.e -= 2;                                    // foreign ground is costly
    if (g === DOT) { w.e = Math.min(EMAX, w.e + FOOD); c = MK(SP, O(c), HT(c)); }
    w.under = c; w.x = nx; w.y = ny; w.wait = 0; S.occ[ni] = k + 1;
  }
  function sniff(w) {               // turn toward adjacent food, preferring straight ahead
    for (const t of [0, 1, 3]) {
      const d = (w.dir + t) & 3, x = w.x + DX[d], y = w.y + DY[d];
      if (inb(x, y) && !S.occ[y * W + x] && G(S.cells[y * W + x]) === DOT) { w.dir = d; return true; }
    }
    return false;
  }
  function step(w, k) {
    if (((S.tick + k) & 1) && --w.e <= 0) { kill(w); return; }   // metabolism: 1 energy / 2 ticks
    if (w.wait) { if ((R() & 3) === 0) w.dir = R() & 3; move(w, k); return; }
    switch (G(w.under)) {
      case 62: w.dir = 0; break; case 118: w.dir = 1; break; case 60: w.dir = 2; break; case 94: w.dir = 3; break;
      case 115: if (w.e >= 40) bud(w); break;
      default: if (!sniff(w) && (R() & 7) === 0) w.dir = (w.dir + ((R() & 1) ? 1 : 3)) & 3;
    }
    if (w.e >= 160) bud(w);
    move(w, k);
  }
  function feed(c) {
    for (let t = 0; t < 14; t++) {                                     // own ground first
      const i = R() % N, v = S.cells[i];
      if (!S.occ[i] && G(v) === SP && O(v) === c.lin) { S.cells[i] = MK(DOT, c.lin, HT(v)); return; }
    }
    for (let t = 0; t < 10; t++) {                                     // else near the nest
      const x = c.nx + (R() % 9) - 4, y = c.ny + (R() % 9) - 4;
      if (!inb(x, y)) continue; const i = y * W + x, v = S.cells[i];
      if (!S.occ[i] && G(v) === SP && (O(v) === 0 || O(v) === c.lin)) { S.cells[i] = MK(DOT, c.lin, HT(v)); return; }
    }
  }
  function tendAt(x, y, lin) {                                          // food appears under a caring finger
    for (let t = 0; t < 6; t++) {
      const xx = x + (R() % 3) - 1, yy = y + (R() % 3) - 1;
      if (!inb(xx, yy)) continue; const i = yy * W + xx, v = S.cells[i];
      if (!S.occ[i] && G(v) === SP && (O(v) === 0 || O(v) === lin)) { S.cells[i] = MK(DOT, lin, 7); return true; }
    }
    return false;
  }
  function place(x, y, glyph, lin) {
    if (!inb(x, y)) return "edge";
    const i = y * W + x, v = S.occ[i] ? S.ws[S.occ[i] - 1].under : S.cells[i];
    if (G(v) === NEST) return "nest";
    if (O(v) && O(v) !== lin) return "foreign";
    const nv = glyph === SP ? MK(SP, O(v), HT(v)) : MK(glyph, lin, HT(v));
    if (S.occ[i]) S.ws[S.occ[i] - 1].under = nv; else S.cells[i] = nv;
    return "ok";
  }
  function sweep() {
    const rows = 12;
    for (let r = 0; r < rows; r++) {
      const y = (S.tick * rows + r) % H;
      for (let x = 0; x < W; x++) {
        const i = y * W + x; if (S.occ[i]) continue;
        const c = S.cells[i], h = HT(c), g = G(c), o = O(c);
        if (h) S.cells[i] = (c & 0x8FFF) | ((h - 1) << 12);
        else if (g === SP && o && (R() % 500) === 0) S.cells[i] = MK(SP, 0, 0);            // untended ground goes wild
        else if (g === DOT && (R() % (o ? 400 : 60)) === 0) S.cells[i] = MK(SP, o, 0);   // food rots
      }
    }
    for (let s = 0; s < 3; s++) {                                                          // rain on the wilds
      const i = R() % N, c = S.cells[i];
      if (!S.occ[i] && c === MK(SP, 0, 0)) S.cells[i] = MK(DOT, 0, 0);
    }
  }
  // active: lin -> attention multiplier; 0 = awake but unfed; missing = asleep
  function tick(active) {
    S.tick++;
    const st = S.tick % Math.max(1, S.ws.length);
    for (let j = 0; j < S.ws.length; j++) {
      const k = (st + j) % S.ws.length, w = S.ws[k]; if (!w.alive) continue;
      if (active[w.lin] === undefined) {                    // keeper away: the colony sleeps (still, barely burning)
        if (((S.tick + k) & 63) === 0 && --w.e <= 0) kill(w);
        continue;
      }
      step(w, k);
    }
    sweep();
    const pop = counts().wisps;
    for (const c of S.cols) {
      const m = active[c.lin] || 0; if (!m) continue;
      c.acc += 0.3 * m;
      while (c.acc >= 1) { c.acc -= 1; feed(c); if (++c.fed % 12 === 0 && pop[c.lin] < 40 + 12 * m) spawnFromNest(c); }
      if (!pop[c.lin] && (S.tick % 30) === 0) spawnFromNest(c);                            // a tended nest always re-wakes
    }
  }
  function counts() {
    const wisps = {}, ground = {};
    for (const w of S.ws) if (w.alive) wisps[w.lin] = (wisps[w.lin] || 0) + 1;
    for (let i = 0; i < N; i++) { const o = O(S.occ[i] ? S.ws[S.occ[i] - 1].under : S.cells[i]); if (o) ground[o] = (ground[o] || 0) + 1; }
    return { wisps, ground };
  }
  function save() {
    const b = new Uint8Array(S.cells.buffer); let s = ""; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
    return JSON.stringify({ rng: S.rng, tick: S.tick, cells: btoa(s), ws: S.ws.filter(w => w.alive).map(w => [w.x, w.y, w.dir, w.lin, w.e, w.under, w.wait]), cols: S.cols });
  }
  function load(json) {
    const d = JSON.parse(json), s = atob(d.cells), b = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i);
    S = { rng: d.rng || 1, tick: d.tick, cells: new Uint16Array(b.buffer), occ: new Int16Array(N), ws: [], cols: d.cols };
    for (const a of d.ws) {
      const [x, y, dir, lin, e, under, wait] = a, i = y * W + x; if (!inb(x, y) || S.occ[i]) continue;
      S.ws.push({ x, y, dir, lin, e, under, wait, alive: 1 }); S.occ[i] = S.ws.length;
    }
    return S;
  }
  return { W, H, N, G, O, HT, SP, DOT, WALL, NEST, newWorld, tick, tendAt, place, counts, save, load, state: () => S };
})();
if (typeof module !== "undefined") module.exports = Core;
