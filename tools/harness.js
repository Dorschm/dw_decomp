/* Shared headless harness: runs the wasm engine under Node (no browser, so nothing is throttled),
 * reads the disc image locally, boots to free roam and offers tiny helpers (PNG writer, pad taps).
 * Used by node_bake_npc.js; node_extract.js / node_bake.js carry their own older copies. */
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '..');
const DISC = process.env.DISC || path.resolve(ROOT, '..', 'Digimon World.bin');

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

const PAD = { start: 0x800, cross: 0x40, up: 0x1000, down: 0x4000, left: 0x8000, right: 0x2000 };
const tap = async (b, hold = 70, gap = 120) => { M._web_pad_set_touch(b); await sleep(hold); M._web_pad_set_touch(0); await sleep(gap); };
const cap = (fn) => { const out = [], ol = console.log; console.log = (...a) => out.push(a.join(' ')); try { fn(); } finally { console.log = ol; } return out; };
const state = () => {
  const o = cap(() => M._web_entity_dump());
  const m = (o[0] || '').match(/state=(-?\d+) scr=(-?\d+) tstate=(-?\d+)/) || [];
  return { game: +m[1], map: +m[2], tamer: +m[3] };
};

/* New game on a pre-made memory card, then warp to `map`; resolves with state() (tamer 0 = free roam). */
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

/* Engine jobs (web_bake_request): anything that reads the disc or mutates entities must run inside the main loop.
 * kind 1 = load NPC model(type), 2 = unload, 3 = spawn(type,x,y,z) as NPC slot 0, 4 = start script section(sid, slot). */
async function job(kind, a = 0, b = 0, c = 0, d = 0, timeout = 20000) {
  const n0 = M._web_bake_done();
  M._web_bake_request(kind, a, b, c, d);
  for (let t = 0; t < timeout && M._web_bake_done() === n0; t += 20) await sleep(20);
  return M._web_bake_done() === n0 ? -1 : M._web_bake_result();
}

module.exports = { M, ROOT, fs, path, sleep, png, PAD, tap, cap, state, boot, job };
