/* Overlay-sprite decode shared by the time-of-day extractor (copied from node_extract.js's dumpObjects): definitions + instances +
 * decoded pixels with the engine's own tpage/clut rules, written to <dir>/objs.png + objects.json. */
const { M, fs, path, png } = require('./harness');

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

module.exports = { dumpObjects };
