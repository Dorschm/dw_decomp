#!/usr/bin/env node
/* Persistent engine REPL: boots once (about 2-3 minutes), then runs snippets dropped into a folder.
 *
 *   node tools/repl.js <dir> [map=1]
 *
 * Write <dir>/cmd.js (an async function body; locals: M, sleep, PAD, tap, state, fs, path, png, ROOT) and it is
 * run once, the result (JSON, or the error) lands in <dir>/cmd.out and cmd.js is renamed to cmd.done.js.
 * <dir>/ready appears once the boot finished. A wasm abort ends the process (the .out then says so). */
const H = require('./harness');
const { M, sleep, PAD, tap, state, fs, path, png, ROOT } = H;
const dir = path.resolve(process.argv[2] || '.');
const map = +(process.argv[3] || 1);
fs.mkdirSync(dir, { recursive: true });
for (const f of ['ready', 'cmd.out', 'cmd.js', 'cmd.done.js']) { try { fs.unlinkSync(path.join(dir, f)); } catch (e) { /* not there */ } }
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;

process.on('uncaughtException', (e) => { fs.writeFileSync(path.join(dir, 'cmd.out'), 'PROCESS ERROR ' + (e && e.message || e)); process.exit(9); });

(async () => {
  while (!M.calledRun) await sleep(200);
  const s0 = await H.boot(map);
  if (s0.tamer !== 0 || s0.map < 1) { fs.writeFileSync(path.join(dir, 'cmd.out'), 'BOOT FAILED ' + JSON.stringify(s0)); process.exit(3); }
  fs.writeFileSync(path.join(dir, 'ready'), JSON.stringify(s0));
  for (;;) {
    await sleep(300);
    const f = path.join(dir, 'cmd.js');
    if (!fs.existsSync(f)) continue;
    const code = fs.readFileSync(f, 'utf8');
    fs.renameSync(f, path.join(dir, 'cmd.done.js'));
    let out;
    try {
      const logs = [];
      const ol = console.log; console.log = (...a) => logs.push(a.join(' '));
      let r;
      try { r = await new AsyncFunction('M', 'sleep', 'PAD', 'tap', 'state', 'fs', 'path', 'png', 'ROOT', 'H', code)(M, sleep, PAD, tap, state, fs, path, png, ROOT, H); }
      finally { console.log = ol; }
      out = JSON.stringify({ result: r === undefined ? null : r, logs: logs.slice(0, 40) });
    } catch (e) { out = 'ERROR ' + (e && e.stack || e); }
    fs.writeFileSync(path.join(dir, 'cmd.out'), out);
  }
})().catch((e) => { fs.writeFileSync(path.join(dir, 'cmd.out'), 'FATAL ' + (e && e.stack || e)); process.exit(1); });
