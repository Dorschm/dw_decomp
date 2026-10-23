#!/usr/bin/env node
/* Capture the game's own UI sounds: system bank 0 (always loaded) notes 0..N via the engine's playSound(), recorded from the
 * SPU mixer (web_audio_capture) with the music stopped.   node tools/node_sfx.js [lastNote=26]
 * Output: dw_remake/client/assets/sfx/raw/ui<n>.wav (1 s, 44.1 kHz stereo; tools/make_sfx.py trims and encodes them). */
const { M, ROOT, fs, path, sleep, state, boot, job } = require('./harness');

const OUT = path.resolve(ROOT, '..', 'dw_remake', 'client', 'assets', 'sfx', 'raw');
const last = +(process.argv[2] ?? 26);

(async () => {
  while (!M.calledRun) await sleep(200);
  console.error('booting (about 2-3 minutes)...');
  const s0 = await boot(1);
  console.error('boot done:', JSON.stringify(s0));
  if (s0.tamer !== 0 || s0.map < 1) { console.error('boot did not reach free roam; exiting 3 (retry)'); process.exit(3); }
  fs.mkdirSync(OUT, { recursive: true });
  await job(6);                                   // stop the music so only the effect is recorded
  await sleep(1500);
  for (let n = 0; n <= last; n++) {
    try { M.FS.unlink('/sfx.wav'); } catch (e) { /* first one */ }
    M.ccall('web_audio_capture', null, ['string', 'number'], ['/sfx.wav', 1]);
    await sleep(120);
    await job(5, 0, n);                           // playSound(0, n) from inside the main loop
    await sleep(1500);
    let buf = null;
    try { buf = M.FS.readFile('/sfx.wav'); } catch (e) { /* not written */ }
    if (buf && buf.length > 1000) { fs.writeFileSync(path.join(OUT, `ui${n}.wav`), Buffer.from(buf)); console.error(`note ${n}: ${buf.length} bytes`); }
    else console.error(`note ${n}: no capture`);
  }
  console.error('finished');
  process.exit(0);
})().catch((e) => { console.error('ERROR', e); process.exit(1); });
