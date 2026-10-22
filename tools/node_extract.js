#!/usr/bin/env node
/* Headless map extractor: runs the wasm engine under Node (no browser, so
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


// overlay sprites: definitions + instances + decoded pixels (engine tpage/clut rules)
function dumpObjects(dir) {
  const n = M._web_map_obj_count();
  const dp = M._web_map_obj_defs(), ip = M._web_map_obj_insts();
  const dv = new DataView(M.HEAPU8.buffer);
  const vram = new Uint16Array(M.HEAPU8.buffer, M._web_vram(), 1024 * 512);
  const insts = [], used = new Set();
  for (let k = 0; k < n && k < 188; k++) {
    const o = ip + k * 34, frames = [], times = [];
    for (let j = 0; j < 8; j++) { frames.push(dv.getInt16(o + 6 + j * 2, true)); times.push(dv.getUint8(o + 22 + j)); }
    if (!frames.some((f) => f >= 0)) continue;
    insts.push({ order: dv.getInt16(o, true), x: dv.getInt16(o + 2, true), y: dv.getInt16(o + 4, true), frames, times, flag: dv.getInt8(o + 33) });
    for (const f of frames) if (f >= 0) used.add(f);
  }
  const defs = {};
  const sprites = [];
  for (const i of used) {
    const o = dp + i * 14;
    const d = { texX: dv.getInt16(o, true), texY: dv.getInt16(o + 2, true), ax: dv.getInt16(o + 4, true), ay: dv.getInt16(o + 6, true), az: dv.getInt16(o + 8, true),
      w: dv.getUint8(o + 10), h: dv.getUint8(o + 11), clut: dv.getInt8(o + 12), tr: dv.getInt8(o + 13) };
    if (!d.w || !d.h) continue;
    const bpp8 = d.clut === -1 || d.clut >= 16;
    const tx = (bpp8 ? 384 + ((d.texX >> 8) << 7) : 384 + ((d.texX >> 8) << 6));
    const cx = d.clut === -1 ? 0 : d.clut < 16 ? d.clut * 16 : 0, cy = d.clut === -1 ? 480 : d.clut < 16 ? 486 : d.clut + 468;
    const u0 = d.texX & 255, v0 = d.texY & 255;
    const rgba = new Uint8Array(d.w * d.h * 4);
    for (let y = 0; y < d.h; y++) for (let x = 0; x < d.w; x++) {
      const u = u0 + x, row = (v0 + y) & 511;
      let idx;
      if (bpp8) { const hw = vram[row * 1024 + tx + (u >> 1)]; idx = (u & 1) ? hw >> 8 : hw & 255; }
      else { const hw = vram[row * 1024 + tx + (u >> 2)]; idx = (hw >> ((u & 3) * 4)) & 15; }
      const c = vram[cy * 1024 + cx + idx], q = (y * d.w + x) * 4;
      rgba[q] = (c & 31) << 3; rgba[q + 1] = ((c >> 5) & 31) << 3; rgba[q + 2] = ((c >> 10) & 31) << 3; rgba[q + 3] = c === 0 ? 0 : 255;
    }
    sprites.push({ i, d, rgba });
    defs[i] = { w: d.w, h: d.h, tr: d.tr, ax: d.ax, ay: d.ay, az: d.az };
  }
  // shelf-pack into one sheet
  sprites.sort((a, b) => b.d.h - a.d.h);
  const SW = 1024; let cxp = 0, cyp = 0, rowH = 0;
  for (const sp of sprites) {
    if (cxp + sp.d.w > SW) { cxp = 0; cyp += rowH; rowH = 0; }
    defs[sp.i].sx = cxp; defs[sp.i].sy = cyp; cxp += sp.d.w; rowH = Math.max(rowH, sp.d.h);
  }
  const SH = cyp + rowH;
  if (!sprites.length) return 0;
  const sheet = new Uint8Array(SW * SH * 4);
  for (const sp of sprites) {
    const { sx, sy } = defs[sp.i];
    for (let y = 0; y < sp.d.h; y++) sheet.set(sp.rgba.subarray(y * sp.d.w * 4, (y + 1) * sp.d.w * 4), ((sy + y) * SW + sx) * 4);
  }
  fs.writeFileSync(path.join(dir, 'objs.png'), png(SW, SH, sheet));
  fs.writeFileSync(path.join(dir, 'objects.json'), JSON.stringify({ sheet: [SW, SH], defs, inst: insts }));
  return insts.length;
}

function dumpMap(m, index) {
  const P = M._web_map_info() >> 2, w = M.HEAP32[P], h = M.HEAP32[P + 1];
  if (w <= 0 || h <= 0 || w * h > 35) return false;
  const dir = path.join(OUT, String(m));
  fs.mkdirSync(dir, { recursive: true });
  const tiles = [];
  for (let i = 0; i < w * h; i++) {
    const p = M._web_map_tile_rgba(i);
    if (!p) { tiles.push(null); continue; }
    fs.writeFileSync(path.join(dir, `tile_${i}.png`), png(128, 128, M.HEAPU8.slice(p, p + 128 * 128 * 4)));
    tiles.push(`tile_${i}.png`);
  }
  const cp = M._web_map_collision();
  fs.writeFileSync(path.join(dir, 'coll.bin'), M.HEAPU8.slice(cp, cp + 10000));
  const cm = M._web_map_camera() >> 2, cam = [M.HEAP32[cm], M.HEAP32[cm + 1]];
  const pts = [];
  for (let z = -2000; z <= 2000; z += 1000) for (let x = -2000; x <= 2000; x += 1000) {
    const q = M._web_net_project(x, 0, z) >> 1;
    const px = M.HEAP16[q] + 160 + cam[0], py = M.HEAP16[q + 1] + 120 + cam[1];
    M._web_net_project(x, -167, z);          // characters sort by their origin height
    pts.push([x, z, px, py, M._web_net_depth()]);
  }
  const q0 = M._web_net_project(0, 0, 0) >> 1, a0 = [M.HEAP16[q0], M.HEAP16[q0 + 1]];
  const q1 = M._web_net_project(0, -167, 0) >> 1;
  const up = [M.HEAP16[q1] - a0[0], M.HEAP16[q1 + 1] - a0[1]];   // pixels per 167 world units of height
  const wp = M._web_map_warps() >> 1;
  fs.writeFileSync(path.join(dir, 'nav.json'), JSON.stringify({ w: w * 128, h: h * 128, pts, up, warps: Array.from(M.HEAP16.subarray(wp, wp + 60)) }));
  dumpObjects(dir);
  index.maps[m] = { w, h, tiles };
  return true;
}

(async () => {
  while (!M.calledRun) await sleep(200);
  const idxPath = path.join(OUT, 'index.json');
  let index = { maps: {} };
  try { index = JSON.parse(fs.readFileSync(idxPath, 'utf8')); } catch (e) {}
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(Math.max(from, 1) === 50 ? 51 : Math.max(from, 1));
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  const ok = [], skipped = [];
  for (let m = from; m <= to; m++) {
    if (NO_FILES.includes(m)) continue;
    M._web_debug_warp(m, 0, 1);
    let t = 0, s;
    while (t < 9000) { await sleep(250); t += 250; s = state(); if ((s.map & 0xff) === m && t >= 1500) break; }
    if ((s.map & 0xff) !== m) { skipped.push(m); console.error(`map ${m}: did not settle (map=${s.map & 0xff})`); continue; }
    await sleep(600);
    if (dumpMap(m, index)) ok.push(m); else skipped.push(m);
    if (ok.length % 5 === 0) fs.writeFileSync(idxPath, JSON.stringify(index));
    if (m % 10 === 0) console.error(`map ${m} done (${ok.length} ok, ${skipped.length} skipped)`);
  }
  fs.writeFileSync(idxPath, JSON.stringify(index));
  console.error(`finished: ${ok.length} ok, skipped ${JSON.stringify(skipped)}`);
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
