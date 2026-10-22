#!/usr/bin/env node
/* Headless sprite baker (see below); harness copied from node_extract.js: runs the wasm engine under Node (no browser, so
 * nothing gets throttled) and dumps every map's background tiles, collision
 * grid, warp table and world->pixel samples into dw_remake/client/assets/maps.
 *
 *   node tools/node_extract.js [from=0] [to=233] [disc=<path to .bin>]
 *
 * Same output as tools/extract_maps.js (browser version). The disc image is
 * read locally and nothing leaves the machine. */
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'maps');
const from = +(process.argv[2] ?? 0), to = +(process.argv[3] ?? 233);
const DISC = process.argv[4] || path.resolve(ROOT, '..', 'Digimon World.bin');
const NO_FILES = [50, 61];   // no files on the disc; loading them wedges the engine

// ---- minimal DOM stubs (the engine's SDL layer probes these) ----
globalThis.screen = { width: 640, height: 480 };
globalThis.window = globalThis;
globalThis.navigator = { userAgent: 'node' };
globalThis.ImageData = class ImageData {
  constructor(w, h) { this.width = w; this.height = h; this.data = new Uint8ClampedArray(w * h * 4); }
};
const ctx2d = {
  createImageData: (w, h) => new ImageData(w, h), getImageData: (x, y, w, h) => new ImageData(w, h),
  putImageData() {}, drawImage() {}, fillRect() {}, setTransform() {},
};
const mkCanvas = () => ({ id: 'canvas', width: 640, height: 480, getContext: (k) => (k === '2d' ? ctx2d : null),
  addEventListener() {}, removeEventListener() {}, requestPointerLock() {}, style: {} });
globalThis.document = { addEventListener() {}, removeEventListener() {}, createElement: (t) => (t === 'canvas' ? mkCanvas() : { style: {} }),
  getElementById: () => mkCanvas(), exitPointerLock() {}, querySelector() { return null; } };
window.addEventListener = () => {};
window.removeEventListener = () => {};

// ---- disc over fs ----
const fd = fs.openSync(DISC, 'r');
const readRaw = (lba, nsec) => {
  const buf = Buffer.alloc(nsec * 2352);
  fs.readSync(fd, buf, 0, buf.length, lba * 2352);
  return Promise.resolve(new Uint8Array(buf));
};
const disc = {
  ready: Promise.resolve(), readRaw,
  readUser: (lba, nsec) => readRaw(lba, nsec).then((raw) => {
    const out = new Uint8Array(nsec * 2048);
    for (let i = 0; i < nsec; ++i) {
      const base = i * 2352, mode = raw[base + 15], off = base + (mode === 2 ? 24 : 16);
      out.set(raw.subarray(off, off + 2048), i * 2048);
    }
    return out;
  }),
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const M = require(path.join(ROOT, 'web', 'dw.js'));
M.disc = disc;
M.canvas = mkCanvas();
M.createContext = () => ctx2d;

// ---- tiny PNG writer ----
const crcT = (() => { const t = new Int32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c; } return t; })();
const crc32 = (b) => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (type, data) => {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
};
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 6 })), chunk('IEND', Buffer.alloc(0))]);
}

// ---- driving the game ----
const PAD = { start: 0x800, cross: 0x40, up: 0x1000, down: 0x4000, left: 0x8000, right: 0x2000 };
const tap = async (b, hold = 70, gap = 120) => { M._web_pad_set_touch(b); await sleep(hold); M._web_pad_set_touch(0); await sleep(gap); };
const cap = (fn) => { const out = [], ol = console.log; console.log = (...a) => out.push(a.join(' ')); try { fn(); } finally { console.log = ol; } return out; };
const state = () => {
  const o = cap(() => M._web_entity_dump());
  const m = (o[0] || '').match(/state=(-?\d+) scr=(-?\d+) tstate=(-?\d+)/) || [];
  return { game: +m[1], map: +m[2], tamer: +m[3] };
};

async function boot(map) {
  const card = fs.readFileSync(path.join(ROOT, '.tmp_test', 'card0.mcr'));
  try { M.FS.mkdir('/memcard'); } catch (e) {}
  M.FS.writeFile('/memcard/card0.mcr', card);
  if (M._web_mc_reload) M._web_mc_reload();
  await sleep(6000);
  for (let i = 0; i < 6; i++) await tap(PAD.start, 120, 1100);
  await sleep(3000);
  await tap(PAD.down, 120, 350); await tap(PAD.cross, 120, 350); await sleep(3000);
  for (let i = 0; i < 40; i++) {
    await tap(PAD.cross, 120, 350);
    if (i % 3 === 0) await tap(PAD.right, 120, 350);
    if (i % 4 === 0) await tap(PAD.start, 120, 350);
  }
  const seq = [PAD.left, PAD.cross, PAD.left, PAD.cross, PAD.up, PAD.cross, PAD.start, PAD.cross, PAD.cross];
  for (let k = 0; k < 4; k++) for (const b of seq) await tap(b, 120, 350);
  for (let i = 0; i < 120; i++) await tap(PAD.cross, 70, 130);
  M._web_debug_warp(map, 0, 1);
  await sleep(9000);
  return state();
}



// ---------------------------------------------------------------------------
// Sprite baking: the original draws its 3D characters through a fixed orthographic
// camera, so frames captured from the engine's own render are pixel-exact sprites.
// Map layers are hidden (web_set_hide_map) so only entities draw on black; the tamer
// is walked in 8 directions and its pixels are cropped every frame.
//   node tools/node_bake.js [out_dir]       -> <out>/tamer.png + tamer.json
const CH = process.argv[5] || path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'chars');
const ENT = process.env.BAKE_ENTITY === '1';           // 1 = bake the partner (entity 1) instead of the tamer
const NAME = ENT ? 'partner' : 'tamer';
const FW = ENT ? 96 : 64, FH = ENT ? 96 : 80, AX = FW / 2, AY = FH - 8;   // frame size and feet anchor inside a frame
const DIRS = [['down', PAD.down], ['down-left', PAD.down | PAD.left], ['left', PAD.left], ['up-left', PAD.up | PAD.left],
  ['up', PAD.up], ['up-right', PAD.up | PAD.right], ['right', PAD.right], ['down-right', PAD.down | PAD.right]];

// Track the tamer by image: black-keyed connected components over the whole 320x240 frame; the
// tamer is the component nearest the screen centre at the start and then the one nearest where
// it was last frame (the partner follows close behind; HUD bars are far away).
let track = [160, 120], first = true;
function grab() {
  const W = M._web_grab_w(), H = 240, ptr = M._web_grab_display();
  const px = M.HEAPU8.subarray(ptr, ptr + W * H * 4);
  const mask = new Uint8Array(W * H);
  for (let i = 0; i < W * H; i++) {
    const x = i % W, y = (i / W) | 0;
    if (y < 84 && x > 110 && x < 235) continue;        // HUD clock region: never part of a character
    mask[i] = px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] >= 14 ? 1 : 0;
  }
  const comp = new Int32Array(W * H).fill(-1), comps = [];
  for (let i = 0; i < W * H; i++) {
    if (!mask[i] || comp[i] >= 0) continue;
    const id = comps.length, st = [i]; comp[i] = id;
    let n = 0, x0 = W, y0 = H, x1 = 0, y1 = 0;
    while (st.length) {
      const j = st.pop(), x = j % W, y = (j / W) | 0; n++;
      if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const nx = x + dx, ny = y + dy; if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        const k = ny * W + nx; if (mask[k] && comp[k] < 0) { comp[k] = id; st.push(k); }
      }
    }
    comps.push({ id, n, x0, y0, x1, y1 });
  }
  let best = null, bd = 1e9;
  for (const c of comps) {
    if (c.n < 60 || c.y1 - c.y0 < 14) continue;
    if (c.x1 < 150 && c.y1 < 85) continue;             // HUD clock (top-left) is not the character
    const d = ((c.x0 + c.x1) / 2 - track[0]) ** 2 + ((c.y0 + c.y1) / 2 - track[1]) ** 2;
    if (d < bd) { bd = d; best = c; }
  }
  if (!best || bd > (first ? 140 * 140 : 60 * 60)) return null;
  first = false;
  track = [(best.x0 + best.x1) / 2, (best.y0 + best.y1) / 2];
  const fx = Math.round(track[0]), fy = best.y1;          // feet anchor: bottom-centre of the component
  const out = new Uint8Array(FW * FH * 4);
  for (let y = 0; y < FH; y++) for (let x = 0; x < FW; x++) {
    const sx = fx - AX + x, sy = fy - AY + y;
    if (sx < 0 || sy < 0 || sx >= W || sy >= H || comp[sy * W + sx] !== best.id) continue;
    const o = (sy * W + sx) * 4, q = (y * FW + x) * 4;
    out[q] = px[o]; out[q + 1] = px[o + 1]; out[q + 2] = px[o + 2]; out[q + 3] = 255;
  }
  return out;
}
const sig = (a) => { let h = 0; for (let i = 0; i < a.length; i += 3) h = (h * 31 + a[i]) | 0; return h; };

(async () => {
  while (!M.calledRun) await sleep(200);
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  // arrival is next to the left-edge warp strip: walk east into the open middle first
  M._web_pad_set_touch(PAD.right); await sleep(3600); M._web_pad_set_touch(0); await sleep(1200);
  M._web_set_hide_map(1);
  M._web_set_hide_entities(ENT ? 0x3fd : 0x3fe);              // draw only the tamer (entity 0): hides the partner and NPCs
  const rows = [], idle = [];
  for (const pass of [1, 2]) for (let di = 0; di < DIRS.length; di++) {
    if (pass === 2 && rows[di] && rows[di].length >= 8 && idle[di]) continue;     // second pass retries gaps
    const [name, bits] = DIRS[di];
    // recentre: walk the opposite way first (unrecorded), make sure the tamer is in free-roam
    const opp = (b) => ((b & PAD.up) ? PAD.down : 0) | ((b & PAD.down) ? PAD.up : 0) | ((b & PAD.left) ? PAD.right : 0) | ((b & PAD.right) ? PAD.left : 0);
    for (let k = 0; k < 6 && state().tamer !== 0; k++) await tap(PAD.cross, 100, 400);
    M._web_pad_set_touch(opp(bits)); await sleep(900); M._web_pad_set_touch(0);
    await sleep(900);
    track = [160, 120]; first = true;
    console.error(`  ${name}: state=${JSON.stringify(state())}`);
    M._web_pad_set_touch(bits);
    await sleep(700);                          // skip the start-up scroll
    const frames = [];
    const t0 = Date.now();
    while (Date.now() - t0 < 1300) {
      const f = grab();
      if (f) { const h = sig(f); if (!frames.length || frames[frames.length - 1].h !== h) frames.push({ h, f }); }
      await sleep(4);
    }
    M._web_pad_set_touch(0);
    await sleep(400);
    const id = grab();
    // one walk cycle: from the first frame to its next repeat (after at least 3 distinct frames)
    const seen = new Set(); let cut = Math.min(frames.length, 16);
    for (let i = 0; i < frames.length; i++) { if (i >= 3 && frames[i].h === frames[0].h) { cut = i; break; } seen.add(frames[i].h); }
    if (frames.length >= (rows[di] || []).length) rows[di] = frames.slice(0, cut).map((x) => x.f);
    if (id) idle[di] = id;
    console.error(`${name}: ${frames.length} distinct captures, cycle ${cut}, idle ${id ? 'ok' : 'none'}`);
  }
  for (let i = 0; i < DIRS.length; i++) { rows[i] = rows[i] || []; if (!idle[i] && rows[i][0]) idle[i] = rows[i][0]; }
  const cols = Math.max(...rows.map((r) => r.length), 8);
  const SW = cols * FW, SH = (DIRS.length + 1) * FH, sheet = new Uint8Array(SW * SH * 4);
  const blit = (f, cx, cy) => { for (let y = 0; y < FH; y++) sheet.set(f.subarray(y * FW * 4, (y + 1) * FW * 4), ((cy * FH + y) * SW + cx * FW) * 4); };
  rows.forEach((r, ri) => r.forEach((f, ci) => blit(f, ci, ri)));
  idle.forEach((f, ci) => { if (f) blit(f, ci, DIRS.length); });
  fs.mkdirSync(CH, { recursive: true });
  fs.writeFileSync(path.join(CH, NAME + '.png'), png(SW, SH, sheet));
  fs.writeFileSync(path.join(CH, NAME + '.json'), JSON.stringify({ fw: FW, fh: FH, anchor: [AX, AY], dirs: DIRS.map((d) => d[0]),
    walk: rows.map((r) => r.length), idleRow: DIRS.length, fps: 12 }));
  console.error('baked ->', CH, 'sheet', SW + 'x' + SH);
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
