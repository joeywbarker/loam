// LOAM pre-alpha core v2: colonies, attention, territory, inherited traits, characters, chronicle.
const Core = (() => {
  const W = 48, H = 72, N = W * H, MAXW = 500, FOOD = 30, EMAX = 300;
  const DX = [1, 0, -1, 0], DY = [0, 1, 0, -1];
  const SP = 32, DOT = 46, WALL = 35, NEST = 64;
  const G = c => c & 255, O = c => (c >> 8) & 15, HT = c => (c >> 12) & 7;
  const MK = (g, o, h) => (g & 255) | ((o & 15) << 8) | ((h & 7) << 12);

  // ---- traits: never chosen, only provoked ----
  const T = { THRIFT: 0, SHELL: 1, BUILDER: 2, SEEKER: 3, HARDY: 4, RESTLESS: 5 };
  const TRAITS = [
    { key: "thrift",   name: "Thrifty",  press: "hardship", desc: "Burns less, breeds less." },
    { key: "shell",    name: "Shelled",  press: "conflict", desc: "Crosses rival ground freely, but moves slower." },
    { key: "builder",  name: "Builder",  press: "building", desc: "Raises walls on its own." },
    { key: "seeker",   name: "Seeker",   press: "roaming",  desc: "Senses food from farther away, at a cost." },
    { key: "hardy",    name: "Hardy",    press: "absence",  desc: "Sleeps without burning anything." },
    { key: "restless", name: "Restless", press: "presence", desc: "Breeds fast, burns fast." },
  ];
  const CLASH = [[T.THRIFT, T.RESTLESS], [T.HARDY, T.RESTLESS]];
  const has = (w, t) => (w.t >> t) & 1;

  let S = null;
  function R() { let x = S.rng; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; S.rng = x; return x; }
  const R01 = () => R() / 4294967296;
  const inb = (x, y) => x >= 0 && y >= 0 && x < W && y < H;
  const col = lin => S.cols.find(c => c.lin === lin);
  const SYL = ["ka","lo","mi","ru","te","va","shi","no","ze","pa","dor","fen","ith","om","ul","sa","ren","tu","bai","wen","ko","ash","mer","yil"];
  function genName() { const n = 2 + (R() % 2); let s = ""; for (let i = 0; i < n; i++) s += SYL[R() % SYL.length]; return s[0].toUpperCase() + s.slice(1); }
  function log(lin, kind, text, extra) {
    S.events.push(Object.assign({ t: S.tick, lin, kind, text }, extra || {}));
    if (S.events.length > 300) S.events.splice(0, S.events.length - 300);
  }

  function newWorld(seed, cols) {
    S = { rng: (seed >>> 0) || 1, tick: 0, cells: new Uint16Array(N), occ: new Int16Array(N), ws: [], cols: [], events: [], nid: 0 };
    S.cells.fill(MK(SP, 0, 0));
    for (let i = 0; i < N; i++) if ((R() % 9) === 0) S.cells[i] = MK(DOT, 0, 0);
    for (const c of cols) addColony(c);
    return S;
  }
  function addColony(c) {
    Object.assign(c, { acc: 0, fed: 0, est: 0, lean: [0, 0, 0, 0, 0, 0], press: { hardship: 0, conflict: 0, building: 0, roaming: 0, absence: 0, presence: 0 } });
    S.cols.push(c);
    S.cells[c.ny * W + c.nx] = MK(NEST, c.lin, 0);
    for (let dy = -3; dy <= 3; dy++) for (let dx = -3; dx <= 3; dx++) {
      const x = c.nx + dx, y = c.ny + dy;
      if (inb(x, y) && (dx || dy)) S.cells[y * W + x] = MK((R() % 3) ? SP : DOT, c.lin, 0);
    }
    for (let k = 0; k < 4; k++) spawnFromNest(c);
  }
  function spawn(x, y, dir, lin, e, traits) {
    if (!inb(x, y)) return -1;
    const i = y * W + x; if (S.occ[i]) return -1;
    const u = S.cells[i], g = G(u); if (g === WALL || g === NEST) return -1;
    let k = -1; for (let j = 0; j < S.ws.length; j++) if (!S.ws[j].alive) { k = j; break; }
    if (k < 0) { if (S.ws.length >= MAXW) return -1; k = S.ws.length; S.ws.push({}); }
    const w = S.ws[k];
    Object.assign(w, { x, y, dir: dir & 3, lin, e, alive: 1, wait: 0, under: u, t: traits || 0, age: 0, nid: 0, name: "", eat: 0, claim: 0, kids: 0, born: S.tick });
    if (g === DOT) { w.e += FOOD; w.under = MK(SP, O(u), HT(u)); }
    S.occ[i] = k + 1; return k;
  }
  function spawnFromNest(c) {
    // nest-born young inherit the colony's established traits
    const d = R() & 3;
    for (let t = 0; t < 4; t++) { const dd = (d + t) & 3; if (spawn(c.nx + DX[dd], c.ny + DY[dd], dd, c.lin, 70, c.est) >= 0) return true; }
    return false;
  }
  function kill(w, why) {
    const i = w.y * W + w.x; let u = w.under;
    if (G(u) === SP) u = MK(DOT, O(u), HT(u));
    S.cells[i] = u; S.occ[i] = 0; w.alive = 0;
    const c = col(w.lin);
    if (why === "starved" && c) c.press.hardship += 0.5;
    if (w.nid) log(w.lin, "death", `${w.name} died after ${Math.round(w.age / 12 / 60 * 10) / 10} min of life — ${w.claim} ground claimed, ${w.kids} young, ${w.eat} meals.`, { nid: w.nid, name: w.name, t2: w.t });
  }
  function budAt(w) {
    const b = (w.dir + 2) & 3, half = (w.e - 4) >> 1;
    const k = spawn(w.x + DX[b], w.y + DY[b], b, w.lin, half, col(w.lin).est);     // the young are born as the colony now is
    if (k >= 0) {
      w.e -= half + 4; w.kids++;
    }
  }
  function move(w, k) {
    const nx = w.x + DX[w.dir], ny = w.y + DY[w.dir];
    if (!inb(nx, ny)) { w.dir = (w.dir + 2) & 3; return; }
    const ni = ny * W + nx;
    if (S.occ[ni]) { const o = S.ws[S.occ[ni] - 1]; if (o.lin !== w.lin) col(w.lin).press.conflict += 0.05; w.wait = 1; return; }
    let c = S.cells[ni]; const g = G(c);
    if (g === WALL || g === NEST) { w.dir = (w.dir + 2) & 3; w.wait = 0; return; }
    const oi = w.y * W + w.x; let u = w.under;
    const sleeper = O(u) && O(u) !== w.lin && S.awake[O(u)] === undefined;   // a sleeping colony's ground can't be taken
    if (G(u) === SP && sleeper) u = (u & 0x8FFF) | (7 << 12);
    else if (G(u) === SP) {
      if (O(u) !== w.lin) {
        w.claim++;
        const cc = col(w.lin); if (Math.abs(w.x - cc.nx) + Math.abs(w.y - cc.ny) > 16) cc.press.roaming += 0.02;
      }
      u = MK(SP, w.lin, 7);
      if (has(w, T.BUILDER) && w.e > 100 && (R() % 150) === 0) { u = MK(WALL, w.lin, 7); w.e -= 10; }
    } else u = (u & 0x8FFF) | (7 << 12);
    S.cells[oi] = u; S.occ[oi] = 0;
    if (O(c) && O(c) !== w.lin) { col(w.lin).press.conflict += 0.01; if (!has(w, T.SHELL)) w.e -= 2; }
    if (g === DOT) { w.e = Math.min(EMAX, w.e + FOOD); w.eat++; c = MK(SP, O(c), HT(c)); }
    w.under = c; w.x = nx; w.y = ny; w.wait = 0; S.occ[ni] = k + 1;
  }
  function sniff(w) {
    const reach = has(w, T.SEEKER) ? 2 : 1;
    for (let r = 1; r <= reach; r++) for (const t of [0, 1, 3]) {
      const d = (w.dir + t) & 3, x = w.x + DX[d] * r, y = w.y + DY[d] * r;
      if (inb(x, y) && !S.occ[y * W + x] && G(S.cells[y * W + x]) === DOT) { w.dir = d; return true; }
    }
    return false;
  }
  function step(w, k) {
    const period = has(w, T.RESTLESS) ? 1 : has(w, T.THRIFT) ? 3 : 2;
    let burn = ((S.tick + k) % period) === 0;
    if (has(w, T.SEEKER) && ((S.tick + k) & 7) === 0) burn = true;
    if (burn && --w.e <= 0) { kill(w, "starved"); return; }
    w.age++;
    if (!w.nid && w.age >= 2000 && w.claim >= 120) promote(w);
    if (has(w, T.SHELL) && ((S.tick + k) % 3) === 0) return;                 // shells are slow
    if (w.wait) { if ((R() & 3) === 0) w.dir = R() & 3; move(w, k); return; }
    switch (G(w.under)) {
      case 62: w.dir = 0; break; case 118: w.dir = 1; break; case 60: w.dir = 2; break; case 94: w.dir = 3; break;
      case 115: if (w.e >= 40) budAt(w); break;
      default: if (!sniff(w) && (R() & 7) === 0) w.dir = (w.dir + ((R() & 1) ? 1 : 3)) & 3;
    }
    const budAtE = has(w, T.RESTLESS) ? 110 : has(w, T.THRIFT) ? 210 : has(w, T.HARDY) ? 180 : 160;
    if (w.e >= budAtE) budAt(w);
    move(w, k);
  }
  function promote(w) {
    let n = 0; for (const o of S.ws) if (o.alive && o.nid && o.lin === w.lin) n++;
    if (n >= 3) return;
    w.nid = ++S.nid; w.name = genName();
    const tr = TRAITS.filter((_, b) => has(w, b)).map(x => x.name.toLowerCase());
    log(w.lin, "notable", `${w.name} has lived long enough to be known${tr.length ? " — " + tr.join(", ") : ""}.`, { nid: w.nid, name: w.name });
  }
  function feed(c) {
    for (let t = 0; t < 14; t++) {
      const i = R() % N, v = S.cells[i];
      if (!S.occ[i] && G(v) === SP && O(v) === c.lin) { S.cells[i] = MK(DOT, c.lin, HT(v)); return; }
    }
    for (let t = 0; t < 10; t++) {
      const x = c.nx + (R() % 9) - 4, y = c.ny + (R() % 9) - 4;
      if (!inb(x, y)) continue; const i = y * W + x, v = S.cells[i];
      if (!S.occ[i] && G(v) === SP && (O(v) === 0 || O(v) === c.lin)) { S.cells[i] = MK(DOT, c.lin, HT(v)); return; }
    }
  }
  function tendAt(x, y, lin) {
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
    const c = col(lin);
    if (glyph === WALL) c.press.building += 6; else if (glyph !== SP) c.press.roaming += 2;
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
        else if (g === SP && o && S.awake[o] !== undefined && (R() % 500) === 0) S.cells[i] = MK(SP, 0, 0);   // sleeping ground holds; only neglect while awake lets it go wild
        else if (g === DOT && (R() % (o ? 400 : 60)) === 0) S.cells[i] = MK(SP, o, 0);
        else if (g === WALL && o && (R() % 3000) === 0) S.cells[i] = MK(SP, o, 0);          // walls crumble eventually
      }
    }
    for (let s = 0; s < 3; s++) {
      const i = R() % N, c = S.cells[i];
      if (!S.occ[i] && c === MK(SP, 0, 0)) S.cells[i] = MK(DOT, 0, 0);
    }
  }
  // Every ~50 s each colony "leans" toward the traits its recent life favours. The chance of a trait
  // arriving grows with its leaning, so a sustained way of life makes it inevitable. Nobody picks.
  function evolve() {
    for (const c of S.cols) {
      const tot = Object.values(c.press).reduce((a, v) => a + v, 0);
      if (tot < 5) continue;
      for (let b = 0; b < TRAITS.length; b++) {
        const share = c.press[TRAITS[b].press] / tot;
        c.lean[b] = c.lean[b] * 0.9 + share;                     // recent life counts most
        if ((c.est >> b) & 1) {
          if (c.lean[b] < 0.4) { c.est &= ~(1 << b); log(c.lin, "lost", `The ${TRAITS[b].name.toLowerCase()} way is fading from the colony.`, { trait: b }); }
          continue;
        }
        if (R01() < 0.12 * Math.max(0, c.lean[b] - 1.2)) gain(c, b);
      }
      for (const k in c.press) c.press[k] *= 0.85;               // pressure is a memory, and memories fade
    }
  }
  function gain(c, b) {
    for (const [a, z] of CLASH) {
      const other = a === b ? z : z === b ? a : -1;
      if (other >= 0 && ((c.est >> other) & 1)) { c.est &= ~(1 << other); log(c.lin, "lost", `Becoming ${TRAITS[b].name.toLowerCase()} has pushed out the ${TRAITS[other].name.toLowerCase()} way.`, { trait: other }); }
    }
    let n = 0; for (let k = 0; k < TRAITS.length; k++) if ((c.est >> k) & 1) n++;
    if (n >= 3) {                                                 // room for three natures at once: the weakest gives way
      let weak = -1; for (let k = 0; k < TRAITS.length; k++) if (((c.est >> k) & 1) && (weak < 0 || c.lean[k] < c.lean[weak])) weak = k;
      c.est &= ~(1 << weak); log(c.lin, "lost", `The ${TRAITS[weak].name.toLowerCase()} way gave way to something new.`, { trait: weak });
    }
    c.est |= 1 << b;
    log(c.lin, "trait", `Young are being born ${TRAITS[b].name.toLowerCase()}. ${TRAITS[b].desc}`, { trait: b });
  }
  // active: lin -> attention multiplier; 0 = awake but unfed; missing = asleep
  function tick(active) {
    S.tick++; S.awake = active;
    const st = S.tick % Math.max(1, S.ws.length);
    for (let j = 0; j < S.ws.length; j++) {
      const k = (st + j) % S.ws.length, w = S.ws[k]; if (!w.alive) continue;
      if (active[w.lin] === undefined) {
        if (!has(w, T.HARDY) && ((S.tick + k) & 127) === 0 && --w.e <= 0) kill(w, "starved");
        continue;
      }
      step(w, k);
    }
    sweep();
    if (S.tick % 600 === 0) evolve();
    const pop = counts().wisps;
    for (const c of S.cols) {
      const m = active[c.lin] || 0; if (!m) continue;
      c.acc += 0.3 * m;
      while (c.acc >= 1) { c.acc -= 1; feed(c); if (++c.fed % 12 === 0 && pop[c.lin] < 40 + 12 * m) spawnFromNest(c); }
      if (!pop[c.lin] && (S.tick % 30) === 0) spawnFromNest(c);
    }
  }
  function counts() {
    const wisps = {}, ground = {};
    for (const w of S.ws) if (w.alive) wisps[w.lin] = (wisps[w.lin] || 0) + 1;
    for (let i = 0; i < N; i++) { const o = O(S.occ[i] ? S.ws[S.occ[i] - 1].under : S.cells[i]); if (o) ground[o] = (ground[o] || 0) + 1; }
    return { wisps, ground };
  }
  function traitShare(lin) {
    let p = 0; const c = new Array(TRAITS.length).fill(0);
    for (const w of S.ws) if (w.alive && w.lin === lin) { p++; for (let b = 0; b < TRAITS.length; b++) if (has(w, b)) c[b]++; }
    return c.map(n => p ? n / p : 0);
  }
  const pressure = (lin, key, n) => { const c = col(lin); if (c) c.press[key] += n; };
  function save() {
    const b = new Uint8Array(S.cells.buffer); let s = ""; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
    return JSON.stringify({ rng: S.rng, tick: S.tick, nid: S.nid, cells: btoa(s), events: S.events, cols: S.cols,
      ws: S.ws.filter(w => w.alive).map(w => [w.x, w.y, w.dir, w.lin, w.e, w.under, w.wait, w.t, w.age, w.nid, w.name, w.eat, w.claim, w.kids, w.born]) });
  }
  function load(json) {
    const d = JSON.parse(json), s = atob(d.cells), b = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i);
    S = { rng: d.rng || 1, tick: d.tick, nid: d.nid || 0, cells: new Uint16Array(b.buffer), occ: new Int16Array(N), ws: [], cols: d.cols, events: d.events || [] };
    for (const a of d.ws) {
      const [x, y, dir, lin, e, under, wait, t, age, nid, name, eat, claim, kids, born] = a, i = y * W + x;
      if (!inb(x, y) || S.occ[i]) continue;
      S.ws.push({ x, y, dir, lin, e, under, wait, t, age, nid, name, eat, claim, kids, born, alive: 1 }); S.occ[i] = S.ws.length;
    }
    return S;
  }
  return { W, H, N, G, O, HT, SP, DOT, WALL, NEST, T, TRAITS, newWorld, tick, tendAt, place, counts, traitShare, pressure, leanings: lin => col(lin).lean.slice(), save, load, state: () => S };
})();
if (typeof module !== "undefined") module.exports = Core;
