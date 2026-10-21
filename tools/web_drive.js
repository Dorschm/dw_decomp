/* Browser-side test driver for the web port. Load in the page console:
 *   eval(await (await fetch('/dw_decomp/tools/web_drive.js')).text())
 * then e.g. `DW.battleScenario()` (runs in the background; poll DW.log).
 *
 * Needs tools/serve_nocache.py as the dev server: screenshots are POSTed
 * to /__shot/<name>.png and land in .tmp_test/shots/ (gitignored). */
(function () {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const DW = window.DW = { sleep, log: [], t0: performance.now() };
  const say = s => { DW.log.push(((performance.now() - DW.t0) / 1000).toFixed(1) + 's ' + s); };
  DW.say = say;

  /* keep the FIRST few uncaught errors (follow-up errors flood the console) */
  DW.errs = [];
  const keep = (m, st) => { if (DW.errs.length < 3) { DW.errs.push(((performance.now() - DW.t0) / 1000).toFixed(1) + 's ' + m + '\n' + String(st || '').split('\n').slice(0, 9).join('\n')); } };
  window.addEventListener('error', e => keep(e.message, e.error && e.error.stack));
  window.addEventListener('unhandledrejection', e => keep('rejection: ' + (e.reason && e.reason.message), e.reason && e.reason.stack));

  /* run fn with console.log captured (the game's debug dumps go there) */
  const cap = fn => {
    const out = [], ol = console.log;
    console.log = (...a) => out.push(a.join(' '));
    try { fn(); } finally { console.log = ol; }
    return out;
  };
  DW.cap = cap;

  /* parse web_entity_dump */
  DW.state = () => {
    const o = cap(() => Module._web_entity_dump());
    const m = (o[0] || '').match(/state=(-?\d+) scr=(-?\d+) tstate=(-?\d+) paused=(-?\d+) tbmode=(-?\d+) instr=(-?\d+)/) || [];
    const ents = [];
    for (const l of o) {
      const e = l.match(/^ent\[(\d+)\]: type=([0-9a-f]+) .*map=(-?\d+) scr=(-?\d+) sid=(-?\d+) auto=(-?\d+)/);
      if (e) ents.push({ i: +e[1], type: parseInt(e[2], 16), map: +e[3], scr: +e[4], sid: +e[5], auto: +e[6] });
    }
    return { game: +m[1], map: +m[2], tamer: +m[3], paused: +m[4], tbmode: +m[5], instr: +m[6], ents };
  };

  /* hold pad bits for `hold` ms (held via the touch path), then release */
  DW.tap = async (b, hold = 70, gap = 120) => {
    Module._web_pad_set_touch(b); await sleep(hold);
    Module._web_pad_set_touch(0); await sleep(gap);
  };
  DW.hold = b => Module._web_pad_set_touch(b);
  /* peek n bytes of emulated PS1 RAM at a PS1 address (0x8xxxxxxx or offset) */
  DW.peek = (addr, n = 16) => {
    const base = Module._web_psx_ram(), off = addr & 0x1fffff;
    return Array.from(Module.HEAPU8.subarray(base + off, base + off + n)).map(x => x.toString(16).padStart(2, '0')).join(' ');
  };
  DW.PAD = { start: 0x800, cross: 0x40, circle: 0x20, tri: 0x10, sq: 0x80,
             up: 0x1000, down: 0x4000, left: 0x8000, right: 0x2000 };

  /* screenshot of the next presented frame -> .tmp_test/shots/<name>.png */
  DW.shot = name => {
    if (!DW._hooked) {
      const g = GL.currentContext.GLctx, orig = g.drawArrays.bind(g);
      g.drawArrays = function (m, f, n) {
        orig(m, f, n);
        if (DW._want && m === g.TRIANGLE_STRIP &&
            g.getParameter(g.FRAMEBUFFER_BINDING) === null) {
          const nm = DW._want; DW._want = null;
          document.getElementById('canvas').toBlob(
            b => fetch('/__shot/' + nm + '.png', { method: 'POST', body: b }), 'image/png');
        }
      };
      DW._hooked = true;
    }
    DW._want = name;
  };

  /* load the harness save, get past the intro movie + Continue + naming
   * dialogue, then force-warp to a map (default MAYO01, a forest with wild
   * Digimon) -- the same path as stepping on a warp tile */
  DW.boot = async (map = 0) => {
    const buf = new Uint8Array(await (await fetch('/dw_decomp/.tmp_test/card0.mcr')).arrayBuffer());
    try { Module.FS.mkdir('/memcard'); } catch (e) {}
    Module.FS.writeFile('/memcard/card0.mcr', buf);
    Module._web_mc_reload && Module._web_mc_reload();
    await sleep(6000);
    for (let i = 0; i < 6; i++) { await DW.tap(DW.PAD.start, 120, 1100); }
    await sleep(1500); DW.shot('title_menu'); await sleep(1500);
    await DW.tap(DW.PAD.down, 120, 350); await DW.tap(DW.PAD.cross, 120, 350); await sleep(3000);
    for (let i = 0; i < 40; i++) {
      await DW.tap(DW.PAD.cross, 120, 350);
      if (i % 3 == 0) await DW.tap(DW.PAD.right, 120, 350);
      if (i % 4 == 0) await DW.tap(DW.PAD.start, 120, 350);
    }
    const seq = [DW.PAD.left, DW.PAD.cross, DW.PAD.left, DW.PAD.cross, DW.PAD.up,
                 DW.PAD.cross, DW.PAD.start, DW.PAD.cross, DW.PAD.cross];
    for (let k = 0; k < 4; k++) for (const b of seq) await DW.tap(b, 120, 350);
    for (let i = 0; i < 120; i++) await DW.tap(DW.PAD.cross, 70, 130);
    say('boot: dialogue done, warping to map ' + map);
    Module._web_debug_warp(map, 0, 1);
    await sleep(9000);
    Module._web_debug_partner_stats();
    const s = DW.state();
    say('boot: map=' + s.map + ' tamer=' + s.tamer + ' game=' + s.game);
    return s;
  };

  /* one wild battle: teleport onto an auto-talk NPC (retrying candidates),
   * mash buttons through the fight and the result screens until the game
   * state returns to 0. Returns 'won/ended', 'timeout' or 'no-trigger'. */
  DW.fightOnce = async (n, maxMs, maxShots) => {
    let s = DW.state();
    const wild = s.ents.filter(e => e.i >= 2 && e.auto === 1);
    say('fight ' + n + ': candidates ' + JSON.stringify(wild.map(e => e.i + ':' + e.type.toString(16))));
    if (!wild.length) return 'no-trigger';
    for (let k = 0; k < wild.length * 2; k++) {
      const w = wild[k % wild.length];
      Module._web_debug_tp(w.i);
      let t = 0;
      while (t < 3000) { await sleep(250); t += 250; s = DW.state(); if (s.tamer === 6 || s.game !== 0) break; }
      if (s.tamer === 6 || s.game !== 0) break;
    }
    let inBattle = false, shots = 0, last = '';
    const t0 = performance.now();
    while (performance.now() - t0 < maxMs) {
      await DW.tap(DW.PAD.cross, 70, 150);
      if (inBattle) {
        const seq = [DW.PAD.circle, DW.PAD.sq, DW.PAD.tri, DW.PAD.up, DW.PAD.down];
        await DW.tap(seq[(Math.random() * seq.length) | 0], 70, 150);
      }
      s = DW.state();
      const key = s.game + '/' + s.tamer + '/' + s.map;
      if (key !== last) { say('fight ' + n + ': game=' + s.game + ' tamer=' + s.tamer + ' map=' + s.map); last = key; }
      if (s.game !== 0 && !inBattle) { inBattle = true; say('fight ' + n + ': BATTLE ENTERED'); }
      if (inBattle && Math.floor((performance.now() - t0) / 10000) > (DW._dumps || 0)) {
        DW._dumps = (DW._dumps || 0) + 1;
        say('fight ' + n + ' battle state: ' + DW.cap(() => Module._web_debug_battle()).join(' ; '));
      }
      if (inBattle && shots < maxShots && Math.floor((performance.now() - t0) / 3000) > shots)
        DW.shot('f' + n + '_b' + String(++shots).padStart(2, '0') + '_g' + s.game);
      if (inBattle && s.game === 0) {
        say('fight ' + n + ': BATTLE ENDED after ' + Math.round((performance.now() - t0) / 1000) + 's');
        await sleep(3000);
        for (let i = 0; i < 12; i++) await DW.tap(DW.PAD.cross, 70, 250);   /* result/level-up boxes */
        DW.shot('f' + n + '_after');
        await sleep(1500);
        return 'ended';
      }
    }
    return inBattle ? 'timeout' : 'no-trigger';
  };

  DW.battleScenario = async (opts = {}) => {
    DW.log.length = 0;
    try {
      await DW.boot(opts.map || 0);
      for (let n = 1; n <= (opts.fights || 1); n++) {
        DW.shot('field_before_' + n);
        await sleep(500);
        const r = await DW.fightOnce(n, opts.maxMs || 90000, opts.maxShots || 12);
        say('fight ' + n + ' result: ' + r);
        if (r !== 'ended') break;
        Module._web_debug_partner_stats();
        await sleep(2000);
      }
      say('scenario done');
    } catch (e) { say('ERROR ' + e + ' ' + (e.stack || '').slice(0, 300)); }
    DW.done = true;
  };
})();
