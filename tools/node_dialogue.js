#!/usr/bin/env node
/* Dump the game's own NPC / sign dialogue by running the real script interpreter.
 *
 *   node tools/node_dialogue.js [from=1] [to=233]       (MAPS=a,b,c re-does a list)
 *
 * For every extracted map (dw_remake/client/assets/maps/<m>/npcs.json + coll.bin) it warps there in a fresh new
 * game, starts each NPC script section (web_call_script) and each tile-trigger section (collision values 51..109),
 * taps Cross to page through the text and records every textbox page the interpreter fills (web_text_*).
 * Selections pick the first option. Output: maps/<m>/dialogue.json = { "<sid>": [ {sp, rows:[...]}, ... ] }.
 * The text is the game's state at the start of a new game, so later story branches are not covered. */
const { M, ROOT, fs, path, sleep, PAD, tap, state, boot, job } = require('./harness');

const MAPS = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'maps');
const NO_FILES = [50, 61];
const from = +(process.argv[2] ?? 1), to = +(process.argv[3] ?? 233);
const dec = new TextDecoder('shift_jis');

// one textbox page -> {sp, rows}. Rows are TEXTBOX_LINE_SIZE (64, US) bytes: control pairs (01 colour, 0d newline,
// 03 + 3 bytes), 2-byte Shift-JIS glyphs (fullwidth ASCII) and plain ASCII for the speaker's name.
const LINE = 64;
function decodePage() {
  const n = M._web_text_nrows(), p = M._web_text_ptr(), buf = M.HEAPU8.subarray(p, p + n * LINE);
  const rows = [];
  let sp = false, end = false;
  for (let r = 0; r < n && !end; r++) {
    const line = buf.subarray(r * LINE, r * LINE + LINE);
    let s = '', i = 0;
    if (r === 0 && line[0] === 1 && [5, 6, 7, 10].includes(line[1])) sp = true;
    while (i < LINE - 1) {
      const b0 = line[i], b1 = line[i + 1];
      if (b0 === 0) { end = true; break; }                   // end of page
      if (b0 === 13) break;                                   // end of this row
      if (b0 === 3) { i += 4; continue; }
      if (b0 < 0x20) { i += 2; continue; }                    // colour / wait / variable markers
      if (b0 < 0x80) { s += String.fromCharCode(b0); i += 1; continue; }
      s += dec.decode(Uint8Array.of(b0, b1)).normalize('NFKC');
      i += 2;
    }
    s = tidy(s);
    if (s) rows.push(s);
  }
  return { sp, rows };
}
// fullwidth punctuation -> ASCII; undecodable glyph runs are the player's name (stored in the memory card's own encoding)
function tidy(s) {
  const map = { '\u3002': '.', '\u3001': ',', '\u300c': "'", '\u300d': "'", '\u30fb': '.', '\u301c': '~', '\u2015': '-', '\u2018': "'", '\u2019': "'" };
  s = s.replace(/[\u3002\u3001\u300c\u300d\u30fb\u301c\u2015\u2018\u2019]/g, (c) => map[c]);
  s = s.replace(/[\ufffd\u0000-\u001f]+/g, '{you}');
  s = s.replace(/(\{you\}[^A-Za-z0-9]{0,3})+/g, '{you}');
  return s.replace(/\s+/g, ' ').trim();
}

async function settleOn(m) {
  M._web_debug_warp(m, 0, 1);
  let t = 0, s;
  while (t < 9000) { await sleep(250); t += 250; s = state(); if ((s.map & 0xff) === m && t >= 1500) break; }
  if ((s.map & 0xff) !== m) return false;
  await sleep(900);
  return true;
}

// run one section; returns the pages it showed (or null if the engine did not take it)
async function runSection(sid, slot) {
  const seq0 = M._web_text_count();
  if ((await job(4, sid, slot)) !== 1) return null;
  const pages = [];
  let battle = false;
  const b0 = M._web_battles_count();
  let at;
  const hardStop = Date.now() + 120000;
  let seen = seq0, idleSince = 0;
  const startT = Date.now();
  let deadline = Date.now() + 14000, stuck = false;
  while (Date.now() < deadline) {
    await sleep(120);
    if (at === undefined && M._web_battles_count() > b0) at = pages.length;      // pages before this index are shown before the fight
    const c = M._web_text_count();
    if (c !== seen) {
      seen = c;
      const pg = decodePage();
      const last = pages[pages.length - 1];
      if (pg.rows.length && !(last && last.rows.join('|') === pg.rows.join('|'))) pages.push(pg);
    }
    if (state().game !== 0) {                                       // the script started a fight: let the partner finish it
      battle = true;
      M._web_debug_battle_weaken();                                 // enemies down to 1 HP
      deadline = Math.max(deadline, Date.now() + 8000);
      if (Date.now() > hardStop) { stuck = true; break; }
      M._web_pad_set_touch(PAD.cross); await sleep(70); M._web_pad_set_touch(0);
      continue;
    }
    if (M._web_script_idle() === 1) { idleSince ||= Date.now(); if (Date.now() - idleSince > 700) break; } else idleSince = 0;
    M._web_pad_set_touch(PAD.cross); await sleep(70); M._web_pad_set_touch(0);
    if (pages.length > 40) break;
    if (!pages.length && !battle && Date.now() - startT > 6000) break;           // a silent script (cutscene, shop menu...)
  }
  battle = battle || M._web_battles_count() > b0;
  return { pages, battle, at: battle ? (at ?? pages.length) : undefined, stuck };
}

module.exports = { runSection, settleOn, decodePage };
if (require.main === module) (async () => {
  while (!M.calledRun) await sleep(200);
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  M._web_set_skip_battles(1);                                  // scripts that start a fight carry on as if it was won (and are flagged)
  M._web_set_speed(+(process.env.SPEED || 4));                 // the game counts frames: run it faster than real time after the boot macro
  const list = process.env.MAPS ? process.env.MAPS.split(',').map(Number) : Array.from({ length: to - from + 1 }, (_, i) => from + i);
  if (process.env.REVERSE) list.reverse();
  let total = 0;
  for (const m of list) {
    if (NO_FILES.includes(m)) continue;
    const dir = path.join(MAPS, String(m));
    if (fs.existsSync(path.join(dir, 'dialogue.done')) && !process.env.FORCE) continue;
    let npcs = [], coll = null;
    try { npcs = JSON.parse(fs.readFileSync(path.join(dir, 'npcs.json'), 'utf8')); } catch (e) { /* none */ }
    try { coll = fs.readFileSync(path.join(dir, 'coll.bin')); } catch (e) { /* none */ }
    const secs = new Map();                          // sid -> slot (-1 = tile trigger)
    for (const n of npcs) if (n.sid && n.active !== 0 && !secs.has(n.sid)) secs.set(n.sid, n.slot);      // only NPCs the script spawns at the start of a game
    if (coll) for (let i = 0; i < coll.length; i++) { const v = coll[i]; if (v >= 80 && v < 110 && !secs.has(v)) secs.set(v, -1); }
    if (!secs.size) continue;
    // resume support: a section that took the engine out of the overworld (battle, wedge) killed the last run;
    // 'dialogue.cur' still names it, so it is skipped now. Pages gathered so far live in 'dialogue.partial.json'.
    const curF = path.join(dir, 'dialogue.cur'), partF = path.join(dir, 'dialogue.partial.json'), skipF = path.join(dir, 'dialogue.skip.json');
    let skip = [], out = {};
    try { skip = JSON.parse(fs.readFileSync(skipF, 'utf8')); } catch (e) { /* none */ }
    try { out = JSON.parse(fs.readFileSync(partF, 'utf8')); } catch (e) { /* none */ }
    if (fs.existsSync(curF)) { skip.push(+fs.readFileSync(curF, 'utf8')); fs.writeFileSync(skipF, JSON.stringify(skip)); try { fs.unlinkSync(curF); } catch (e) { /* another worker got there first */ }; }
    console.error(`map ${m}: ${secs.size} sections`);
    if (!(await settleOn(m))) { console.error(`map ${m}: did not settle`); continue; }
    for (const [sid, slot] of secs) {
      if (skip.includes(sid) || out[sid]) continue;
      if (state().game !== 0) { console.error(`map ${m}: engine left the overworld (game ${state().game}); exiting 4 to restart`); process.exit(4); }
      if (state().tamer !== 0 || (state().map & 0xff) !== m) {          // a script moved us: come back
        if (!(await settleOn(m))) { console.error(`map ${m}: could not get back; exiting 4`); process.exit(4); }
      }
      fs.writeFileSync(curF, String(sid));
      let pages;
      const ts = Date.now();
      try { pages = await runSection(sid, slot); } catch (e) { pages = null; }
      console.error(`  map ${m} section ${sid}: ${pages ? pages.pages.length + ' pages' + (pages.battle ? ' +battle' : '') : 'not started'} in ${((Date.now() - ts) / 1000).toFixed(1)}s game=${state().game}`);
      if (pages && (pages.pages.length || pages.battle)) {
        out[sid] = pages; total += pages.pages.length; fs.writeFileSync(partF, JSON.stringify(out));
      }
      if (pages && pages.stuck) {                                       // the fight never ended: record it and restart clean
        fs.writeFileSync(path.join(dir, 'dialogue.json'), JSON.stringify(out));
        console.error(`map ${m}: section ${sid} starts a battle that did not end; restarting`);
        try { fs.unlinkSync(curF); } catch (e) { /* another worker got there first */ }; process.exit(4);
      }
      // leave any menu/script behind before the next one
      for (let k = 0; k < 12 && M._web_script_idle() !== 1; k++) await tap(PAD.cross, 70, 600);   // let the script finish before the next one
      if (state().game === 0) try { fs.unlinkSync(curF); } catch (e) { /* another worker got there first */ };                     // finished cleanly; otherwise the next run skips it
    }
    if (Object.keys(out).length) fs.writeFileSync(path.join(dir, 'dialogue.json'), JSON.stringify(out));
    fs.writeFileSync(path.join(dir, 'dialogue.done'), '1');
    console.error(`map ${m}: ${Object.keys(out).length}/${secs.size} sections with text (${total} pages total)`);
  }
  console.error('finished');
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
