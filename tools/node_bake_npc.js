#!/usr/bin/env node
/* Bake NPC / wild Digimon species sprites from the engine's own render (same idea as node_bake.js).
 *
 *   TYPES=3,5,7 node tools/node_bake_npc.js       (default: every type found in the extracted maps' npcs.json)
 *
 * Boots to map 1, hides the map layers and every entity except NPC slot 0, loads each species model
 * (web_bake_load), spawns it where the camera is centred and poses it by hand in 8 facings for the
 * idle (anim 0) and walk (anim 2) animations. Output: dw_remake/client/assets/npc/<type>.png + .json
 * One sheet per species: rows = [idle x 8 dirs, walk x 8 dirs], columns = frames. */
const { M, ROOT, fs, path, sleep, png, PAD, tap, state, boot, job } = require('./harness');

const OUT = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'npc');
const MAPS = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'maps');
const DIRS = ['down', 'down-left', 'left', 'up-left', 'up', 'up-right', 'right', 'down-right'];
const DVEC = { 'down': [0, 1], 'down-left': [-1, 1], 'left': [-1, 0], 'up-left': [-1, -1], 'up': [0, -1], 'up-right': [1, -1], 'right': [1, 0], 'down-right': [1, 1] };
const ANIMS = [0, 2];                                  // idle, walk
const MAXF = 16;

function types() {
  if (process.env.TYPES) return process.env.TYPES.split(',').map(Number);
  const set = new Set();
  for (const d of fs.readdirSync(MAPS)) {
    try { for (const n of JSON.parse(fs.readFileSync(path.join(MAPS, d, 'npcs.json'), 'utf8'))) set.add(n.type); } catch (e) { /* not a map dir */ }
  }
  return [...set].sort((a, b) => a - b);
}

// black-keyed connected components over the 320x240 frame; the species is the component nearest the centre/where it was
let track = [160, 120], first = true;
function grab() {
  const W = M._web_grab_w(), Hh = 240, ptr = M._web_grab_display();
  const px = M.HEAPU8.subarray(ptr, ptr + W * Hh * 4);
  const mask = new Uint8Array(W * Hh);
  for (let i = 0; i < W * Hh; i++) {
    const x = i % W, y = (i / W) | 0;
    if (x < 84 && y < 78) continue;                    // HUD clock (top-left)
    if (x > 228 && y > 192) continue;                  // HUD bars (bottom-right)
    mask[i] = px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] >= 14 ? 1 : 0;
  }
  const comp = new Int32Array(W * Hh).fill(-1), comps = [];
  for (let i = 0; i < W * Hh; i++) {
    if (!mask[i] || comp[i] >= 0) continue;
    const id = comps.length, st = [i]; comp[i] = id;
    let n = 0, x0 = W, y0 = Hh, x1 = 0, y1 = 0;
    while (st.length) {
      const j = st.pop(), x = j % W, y = (j / W) | 0; n++;
      if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
      for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) {     // 5x5 neighbourhood bridges 1-px gaps
        const nx = x + dx, ny = y + dy; if (nx < 0 || ny < 0 || nx >= W || ny >= Hh) continue;
        const k = ny * W + nx; if (mask[k] && comp[k] < 0) { comp[k] = id; st.push(k); }
      }
    }
    comps.push({ id, n, x0, y0, x1, y1 });
  }
  let best = null, bd = 1e9;
  for (const c of comps) {
    if (c.n < 40 || c.y1 - c.y0 < 10) continue;
    const d = first ? -c.n : ((c.x0 + c.x1) / 2 - track[0]) ** 2 + ((c.y0 + c.y1) / 2 - track[1]) ** 2;   // first frame: the biggest blob
    if (d < bd) { bd = d; best = c; }
  }
  if (!best || (!first && bd > 70 * 70)) return null;
  first = false;
  track = [(best.x0 + best.x1) / 2, (best.y0 + best.y1) / 2];
  const fx = Math.round(track[0]), fy = best.y1;
  const w = best.x1 - best.x0 + 1, h = best.y1 - best.y0 + 1;
  const data = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const sx = best.x0 + x, sy = best.y0 + y;
    if (comp[sy * W + sx] !== best.id) continue;
    const o = (sy * W + sx) * 4, q = (y * w + x) * 4;
    data[q] = px[o]; data[q + 1] = px[o + 1]; data[q + 2] = px[o + 2]; data[q + 3] = 255;
  }
  return { w, h, ox: best.x0 - fx, oy: best.y0 - fy, data };   // offset of the crop from the feet anchor (bottom centre)
}
const sig = (f) => { let h = f.w * 7 + f.h; for (let i = 0; i < f.data.length; i += 11) h = (h * 31 + f.data[i]) | 0; return h; };

// Which rotY faces which screen direction? Measured, not assumed: pose each of 8 yaw values, let the species walk
// for a moment (the walk animation moves it along its facing) and read the screen displacement of the sprite.
let FAC = null;
const CANON = { 'right': 0, 'down-right': 45, 'down': 90, 'down-left': 135, 'left': 180, 'up-left': -135, 'up': -90, 'up-right': -45 };
function centre() {
  const W = M._web_grab_w(), ptr = M._web_grab_display(), px = M.HEAPU8.subarray(ptr, ptr + W * 240 * 4);
  let sx = 0, sy = 0, n = 0;
  for (let y = 0; y < 240; y++) for (let x = 0; x < W; x++) {
    if ((x < 84 && y < 78) || (x > 228 && y > 192)) continue;
    const o = (y * W + x) * 4;
    if (px[o] + px[o + 1] + px[o + 2] >= 14) { sx += x; sy += y; n++; }
  }
  return n > 40 ? [sx / n, sy / n] : null;
}
async function calibrate() {
  const meas = [];
  for (let k = 0; k < 8; k++) {
    M._web_bake_pose(k * 512, 2);                       // pose + put it back on the spawn point
    await sleep(300);
    const a = centre();
    await sleep(450);                                   // no pose calls: it walks
    const b = centre();
    if (!a || !b) continue;
    meas.push({ r: k * 512, ang: Math.atan2(b[1] - a[1], b[0] - a[0]) * 180 / Math.PI, d: Math.hypot(b[0] - a[0], b[1] - a[1]) });
  }
  const out = {}, used = new Set();
  const pairs = [];
  for (const [name, ca] of Object.entries(CANON)) for (const m of meas) {
    let e = Math.abs(m.ang - ca); if (e > 180) e = 360 - e;
    pairs.push({ name, m, e });
  }
  pairs.sort((x, y) => x.e - y.e);
  for (const p of pairs) if (!out[p.name] && !used.has(p.m.r)) { out[p.name] = { r: p.m.r, e: p.e }; used.add(p.m.r); }
  console.error('facing calibration:', JSON.stringify(meas.map((m) => [m.r, Math.round(m.ang), Math.round(m.d)])), '->', JSON.stringify(out));
  return Object.keys(out).length === 8 ? out : null;
}

// world point (near the tamer) that projects to the screen target, via the engine's own camera (affine: 2 finite differences)
function spawnPoint(tx, y, tz, target = [170, 138]) {
  const pr = (a, c) => { const q = M._web_net_project(a, y, c) >> 1; return [M.HEAP16[q] + 160, M.HEAP16[q + 1] + 120]; };
  const p0 = pr(tx, tz), px = pr(tx + 100, tz), pz = pr(tx, tz + 100);
  const a = (px[0] - p0[0]) / 100, b = (pz[0] - p0[0]) / 100, c = (px[1] - p0[1]) / 100, d = (pz[1] - p0[1]) / 100;
  const det = a * d - b * c, dx = target[0] - p0[0], dy = target[1] - p0[1];
  return { x: Math.round(tx + (d * dx - b * dy) / det), z: Math.round(tz + (-c * dx + a * dy) / det) };
}

const probe = () => { const p = M._web_npc_probe() >> 2; return { id: M.HEAP32[p + 8], frame: M.HEAP32[p + 9], onScreen: M.HEAP32[p + 6] }; };

// One animation cycle at one facing: one image per animation tick until the engine's frame counter wraps.
// Returns { frames, tps } (tps = engine ticks per second, measured) or null when the species has no such animation.
async function capture(rotY, anim) {
  M._web_bake_pose(rotY, anim);
  await sleep(250);
  track = [160, 120]; first = true;
  const frames = [];
  let last = -1, t0 = 0, f0 = 0, tEnd = Date.now() + 6000;
  while (Date.now() < tEnd) {
    M._web_bake_pose(rotY, anim);
    const pr = probe();
    if (pr.id === anim && pr.frame !== last) {
      if (last >= 0 && pr.frame < last) break;                    // wrapped: one full cycle captured
      if (!frames.length) { t0 = Date.now(); f0 = pr.frame; }
      last = pr.frame;
      const f = grab();
      if (f) { f.fr = pr.frame; frames.push(f); }
    }
    await sleep(2);
  }
  if (!frames.length) return null;
  const dt = (Date.now() - t0) / 1000;
  return { frames, tps: last > f0 && dt > 0.2 ? (last - f0) / dt : 30, wrapped: Date.now() < tEnd };
}

// at most n evenly spaced frames of a cycle
function sample(frames, n = 16) {
  if (frames.length <= n) return frames;
  return Array.from({ length: n }, (_, i) => frames[Math.floor(i * frames.length / n)]);
}

async function bakeType(type) {
  await job(1, type);
  await sleep(800);
  const loc = M._web_tamer_loc() >> 2, tx = M.HEAP32[loc], y = M.HEAP32[loc + 1], tz = M.HEAP32[loc + 2];
  const { x, z } = spawnPoint(tx, y, tz);
  let ok = false;
  for (let k = 0; k < 3 && !ok; k++) {
    if ((await job(3, type, x, y, z)) !== 1) continue;
    await sleep(500);
    const p = M._web_npc_probe() >> 2;
    ok = M.HEAP32[p] === 1 && M.HEAP32[p + 6] === 1;            // present and on screen
    if (!ok) { await job(2, type); await sleep(300); }
  }
  if (!ok) return { type, error: 'spawn refused / off screen' };
  if (!FAC) FAC = await calibrate();
  if (!FAC) return { type, error: 'facing calibration failed' };
  const fac = FAC;
  const rows = [], fpsOf = [];                                            // [anim][dir] -> frames
  for (const anim of ANIMS) {
    const row = [];
    let tps = 0, n = 0, cyc = 0;
    for (const d of DIRS) {
      const c = fac[d] ? await capture(fac[d].r, anim) : null;
      const fr = c ? sample(c.frames) : [];
      row.push(fr);
      if (c) { tps += c.tps; n++; cyc += c.frames.length / c.tps; }
    }
    rows.push(row);
    // playback rate so one sampled cycle lasts as long as the original's
    const per = Math.max(1, ...row.map((r) => r.length));
    fpsOf.push(n ? Math.max(2, Math.min(24, per / (cyc / n))) : 10);
  }
  await job(2, type);
  await sleep(300);
  const all = rows.flat(2);
  if (!all.length) return { type, error: 'nothing rendered' };
  // common frame: feet anchor bottom-centre
  let L = 0, R = 0, T = 0;
  for (const f of all) { L = Math.min(L, f.ox); R = Math.max(R, f.ox + f.w); T = Math.min(T, f.oy); }
  const hw = Math.max(-L, R) + 2, FW = hw * 2, FH = -T + 3, AX = hw, AY = FH - 3;
  const cols = Math.max(...rows.flat().map((r) => r.length), 1);
  const SW = cols * FW, SH = rows.length * DIRS.length * FH, sheet = new Uint8Array(SW * SH * 4);
  rows.forEach((row, ai) => row.forEach((frames, di) => frames.forEach((f, ci) => {
    const rr = ai * DIRS.length + di;
    for (let yy = 0; yy < f.h; yy++) {
      const dy = rr * FH + AY + f.oy + yy;
      sheet.set(f.data.subarray(yy * f.w * 4, (yy + 1) * f.w * 4), (dy * SW + ci * FW + AX + f.ox) * 4);
    }
  })));
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, type + '.png'), png(SW, SH, sheet));
  const meta = { type, fw: FW, fh: FH, anchor: [AX, AY], dirs: DIRS, idle: rows[0].map((r) => r.length), walk: rows[1].map((r) => r.length), idleFps: +fpsOf[0].toFixed(1), walkFps: +fpsOf[1].toFixed(1), fps: +fpsOf[1].toFixed(1) };
  fs.writeFileSync(path.join(OUT, type + '.json'), JSON.stringify(meta));
  return meta;
}

module.exports = { bakeType, types };
if (require.main === module) (async () => {
  while (!M.calledRun) await sleep(200);
  const list = types();
  console.error('species:', list.join(','));
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  M._web_pad_set_touch(PAD.right); await sleep(3600); M._web_pad_set_touch(0); await sleep(1200);
  M._web_set_hide_map(1);
  M._web_set_hide_entities(0x3fb);                     // hide everything but entity 2 (NPC slot 0)
  for (const t of list) {
    if (fs.existsSync(path.join(OUT, t + '.json')) && !process.env.FORCE) continue;
    try {
      const r = await bakeType(t);
      console.error(r.error ? `type ${t}: ${r.error}` : `type ${t}: ${r.fw}x${r.fh} idle ${r.idle.join('/')} @${r.idleFps} walk ${r.walk.join('/')} @${r.walkFps}`);
    } catch (e) { console.error(`type ${t}: ERROR ${e}`); }
  }
  console.error('finished');
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
