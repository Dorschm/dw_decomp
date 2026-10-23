#!/usr/bin/env node
/* Extract the dusk and night looks of every outdoor map: the game keeps three palettes per map (day / 16-20h / 20-6h,
 * MAP_CLUTS) and loads one into VRAM by the clock; here each one is loaded in turn and the background tiles and the
 * overlay sprites are decoded again.   node tools/node_tod.js [from=1] [to=233]   (REVERSE=1, SPEED=4 env)
 * Output: dw_remake/client/assets/maps/<m>/tod1|tod2/{tile_<i>.png, objs.png, objects.json}; maps whose flag 0x40 is set
 * (interiors) have no time of day and get a marker file 'tod.none'. Resumable: a finished map has 'tod.done'. */
const { M, ROOT, fs, path, sleep, png, PAD, state, boot } = require('./harness');
const { dumpObjects } = require('./dump_objects');

const MAPS = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'maps');
const NO_FILES = [50, 61];
const from = +(process.argv[2] ?? 1), to = +(process.argv[3] ?? 233);

async function settleOn(m) {
  M._web_debug_warp(m, 0, 1);
  let t = 0, s;
  while (t < 9000) { await sleep(150); t += 150; s = state(); if ((s.map & 0xff) === m && t >= 900) break; }
  if ((s.map & 0xff) !== m) return false;
  await sleep(500);
  return true;
}

(async () => {
  while (!M.calledRun) await sleep(200);
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  M._web_set_skip_battles(1);
  M._web_set_speed(+(process.env.SPEED || 3));
  const list = Array.from({ length: to - from + 1 }, (_, i) => from + i);
  if (process.env.REVERSE) list.reverse();
  for (const m of list) {
    if (NO_FILES.includes(m)) continue;
    const dir = path.join(MAPS, String(m));
    if (!fs.existsSync(path.join(dir, 'nav.json')) || fs.existsSync(path.join(dir, 'tod.done')) || fs.existsSync(path.join(dir, 'tod.none'))) continue;
    if (!(await settleOn(m))) { console.error(`map ${m}: did not settle`); continue; }
    const info = JSON.parse(fs.readFileSync(path.join(dir, 'nav.json'), 'utf8'));
    const P = M._web_map_info() >> 2, w = M.HEAP32[P], h = M.HEAP32[P + 1];
    if (!M._web_set_map_clut(0)) { fs.writeFileSync(path.join(dir, 'tod.none'), '1'); console.error(`map ${m}: no time of day`); continue; }
    for (const n of [1, 2]) {
      M._web_set_map_clut(n);
      await sleep(150);
      const out = path.join(dir, 'tod' + n);
      fs.mkdirSync(out, { recursive: true });
      for (let i = 0; i < w * h; i++) {
        const p = M._web_map_tile_rgba(i);
        if (p) fs.writeFileSync(path.join(out, `tile_${i}.png`), png(128, 128, M.HEAPU8.slice(p, p + 128 * 128 * 4)));
      }
      dumpObjects(out);
    }
    M._web_set_map_clut(0);
    fs.writeFileSync(path.join(dir, 'tod.done'), '1');
    console.error(`map ${m}: dusk + night extracted (${w}x${h} tiles)`);
  }
  console.error('finished');
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
