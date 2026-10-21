/* Dump every map's background tiles as PNGs, straight from the running
 * engine (web_map_info / web_map_tile_rgba), through tools/serve_nocache.py
 * POST /__asset/ -> dw_remake/client/assets/maps/<id>/tile_<n>.png, plus
 * maps/index.json ({maps:{id:{w,h,tiles:[file|null]}}}).
 *
 * Needs the page loaded with ?disc=... and tools/web_drive.js evaluated:
 *   eval(await (await fetch('/dw_decomp/tools/web_drive.js')).text());
 *   eval(await (await fetch('/dw_decomp/tools/extract_maps.js')).text());
 *   EX.run({from: 0, to: 233});      // then poll EX.status()
 * The output is derived from the player's own disc and stays on their
 * machine unless they choose to publish it. */
(() => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const post = (path, body) => fetch('/__asset/' + path, { method: 'POST', body });
  const EX = window.EX = { index: { maps: {} }, ok: [], skipped: [], cur: -1, done: false, err: null };

  EX.status = () => ({ cur: EX.cur, ok: EX.ok.length, skipped: EX.skipped, done: EX.done, err: EX.err });

  async function dumpMap(m) {
    const P = Module._web_map_info() >> 2;
    const w = Module.HEAP32[P], h = Module.HEAP32[P + 1];
    if (w <= 0 || h <= 0 || w * h > 35) return false;
    const c = document.createElement('canvas');
    c.width = c.height = 128;
    const g = c.getContext('2d');
    const tiles = [];
    for (let i = 0; i < w * h; i++) {
      const p = Module._web_map_tile_rgba(i);
      if (!p) { tiles.push(null); continue; }
      g.putImageData(new ImageData(new Uint8ClampedArray(Module.HEAPU8.buffer.slice(p, p + 128 * 128 * 4)), 128, 128), 0, 0);
      const blob = await new Promise(r => c.toBlob(r, 'image/png'));
      await post(`maps/${m}/tile_${i}.png`, blob);
      tiles.push(`tile_${i}.png`);
    }
    // navigation: 100x100 collision grid (bit 0x80 = blocked, other non-zero =
    // trigger/warp id) + world->map-pixel samples on the ground plane (y=0).
    // mapPx = projected sx + 160 + cameraX (verified camera-invariant), pixel
    // origin = top-left of the map image. A homography is fitted from these
    // samples by the remake's tools/pack_assets.py.
    const cp = Module._web_map_collision();
    await post(`maps/${m}/coll.bin`, new Blob([Module.HEAPU8.slice(cp, cp + 10000)]));
    const cm = Module._web_map_camera() >> 2;
    const cam = [Module.HEAP32[cm], Module.HEAP32[cm + 1]];
    const pts = [];
    for (let z = -2000; z <= 2000; z += 1000) for (let x = -2000; x <= 2000; x += 1000) {
      const P = Module._web_net_project(x, 0, z) >> 1;
      pts.push([x, z, Module.HEAP16[P] + 160 + cam[0], Module.HEAP16[P + 1] + 120 + cam[1]]);
    }
    await post(`maps/${m}/nav.json`, new Blob([JSON.stringify({ w: w * 128, h: h * 128, pts, warps: Array.from(Module.HEAP16.subarray(Module._web_map_warps() >> 1, (Module._web_map_warps() >> 1) + 60)) })]));
    EX.index.maps[m] = { w, h, tiles };
    return true;
  }

  /* maps 50 (YAKA01) and 61 (YAKA21) have no files on the disc; loading them wedges the engine */
  const NO_FILES = [50, 61];
  EX.run = async (opts = {}) => {
    try {
      try { EX.index = await (await fetch('/dw_remake/client/assets/maps/index.json', { cache: 'no-store' })).json(); } catch (e) {}
      await DW.boot(opts.first === undefined ? 1 : opts.first);
      const list = opts.maps || Array.from({ length: (opts.to === undefined ? 233 : opts.to) - (opts.from || 0) + 1 }, (_, i) => (opts.from || 0) + i);
      for (const m of list.filter(x => !NO_FILES.includes(x))) {
        EX.cur = m;
        Module._web_debug_warp(m, 0, 1);
        let t = 0, s;
        while (t < 9000) {
          await sleep(250); t += 250; s = DW.state();
          if ((s.map & 0xff) === m && t >= 1500) break;
        }
        if ((s.map & 0xff) !== m) { EX.skipped.push(m); continue; }
        await sleep(600);
        (await dumpMap(m) ? EX.ok : EX.skipped).push(m);
        if (EX.ok.length % 10 === 0) await post('maps/index.json', new Blob([JSON.stringify(EX.index)]));
      }
      await post('maps/index.json', new Blob([JSON.stringify(EX.index)]));
    } catch (e) { EX.err = String(e) + ' ' + (e.stack || '').slice(0, 200); }
    EX.done = true;
  };
})();
