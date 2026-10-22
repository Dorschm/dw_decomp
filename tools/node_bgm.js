#!/usr/bin/env node
/* Headless BGM capture (see the bottom); harness copied from node_extract.js: Headless map extractor: runs the wasm engine under Node (no browser, so
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
// BGM capture: group maps by track id (getMapSoundId), warp to one map per track and record the
// mixer output (web_audio_capture_on_seq arms at the first note) to WAV on the dw_remake side.
//   node tools/node_bgm.js           (SECS=100 per track; skips tracks that already have a wav)
const BGM = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'bgm');
const SECS = +(process.env.SECS || 100);
(async () => {
  while (!M.calledRun) await sleep(200);
  fs.mkdirSync(path.join(BGM, 'raw'), { recursive: true });
  console.error('booting...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('bad boot; exit 3'); process.exit(3); }
  const byTrack = new Map(), mapTrack = {};
  for (let m = 1; m <= 233; m++) {
    if (NO_FILES.includes(m)) continue;
    const id = M._web_map_sound_id(m);
    mapTrack[m] = id;
    if (id < 0 || id > 200) continue;
    if (!byTrack.has(id)) byTrack.set(id, []);
    byTrack.get(id).push(m);
  }
  fs.writeFileSync(path.join(BGM, 'tracks.json'), JSON.stringify(mapTrack));
  console.error('tracks:', byTrack.size, JSON.stringify([...byTrack].map(([k, v]) => k + ':' + v.length)));
  for (const [id, maps] of byTrack) {
    const out = path.join(BGM, 'raw', id + '.wav');
    if (fs.existsSync(out)) continue;
    let done = false;
    for (const m of maps.slice(0, 3)) {
      // a track that is already playing does not restart when its map loads: go through a map
      // with a different track first so the target's sequencer starts from the top
      const decoy = [...byTrack].find(([k]) => k !== id)[1][0];
      M._web_debug_warp(decoy, 0, 1); await sleep(4000);
      try { M.FS.unlink('/bgm.wav'); } catch (e) {}
      M.ccall('web_audio_capture_on_seq', null, ['string', 'number'], ['/bgm.wav', SECS]);
      M._web_debug_warp(m, 0, 1);
      const t0 = Date.now();
      while (Date.now() - t0 < (SECS + 40) * 1000) {
        await sleep(1000);
        try { if (M.FS.analyzePath('/bgm.wav').exists && M.FS.stat('/bgm.wav').size > SECS * 44100 * 4 * 0.98) break; } catch (e) {}
      }
      let buf = null; try { buf = M.FS.readFile('/bgm.wav'); } catch (e) {}
      if (buf && buf.length > 20 * 44100 * 4) { fs.writeFileSync(out, Buffer.from(buf)); done = true; console.error(`track ${id} (map ${m}) ok ${buf.length}`); break; }
      console.error(`track ${id} map ${m}: no capture, trying next`);
    }
    if (!done) console.error(`track ${id}: FAILED`);
    await sleep(1500);
  }
  console.error('finished');
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
