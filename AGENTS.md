# dw_decomp — agent notes

Digimon World (USA, SLUS_010.32) decompilation. Work happens on branch
`web-port`. `origin` = https://github.com/Dorschm/dw_decomp (fork),
`upstream` = https://github.com/jype0/dw_decomp.

The USA executable is extracted to `disks/us/SLUS_010.32`
(sha1 `31ec666b987d5b007dec40eecc4092d27ed9085b`).

## Golden rule

**Never edit game sources** (`src/<overlay>/*.c`, `include/dw/*`,
`external/psyq_headers`, `config/`). All port work goes into new files under
`src/web/`, `include/emcc/`, `mk/`, `tools/`, `web/`. Behavioural differences
between TUs (conflicting declarations of the same symbol) are handled with
per-file `-D` renames in `mk/toolchain/emcc.mk` pointing at wrappers in
`src/web/psx/call_fixups.c`.

## Docker builds (Git Bash, from repo root)

PSX (non-matching GCC):

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/dw" \
  -v dw_venv:/dw/.venv dw-build sh -c 'make -j$(nproc) TOOLCHAIN=gcc'
```

Produces `build/us/psx/gcc/SLUS_010.32` + all `*_REL.BIN` overlays.
The mwcc matching build additionally needs `bin/cc_mips/cc_mips_40.dll`
(CodeWarrior for PlayStation R4 — not publicly downloadable).

Web (Emscripten):

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src \
  emscripten/emsdk:3.1.74 make PLATFORM=web TOOLCHAIN=emcc
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src \
  emscripten/emsdk:3.1.74 make PLATFORM=web TOOLCHAIN=emcc web
```

Produces `build/us/web/emcc/dw.js` + `dw.wasm`; the `web` target copies them
next to `web/index.html`. Serve with `python -m http.server 8765` from `web/`.

## Layout

- `mk/platform/web.mk`, `mk/toolchain/emcc.mk`, `mk/version/us_web.mk` —
  web config; all game C TUs statically linked, no asm/no overlays.
- `include/emcc/inline_n.h` — `gte_*` macros -> C stubs (`src/web/psx/gte.*`).
- `include/emcc/hps_dispatch.h` — variadic arity dispatch (needs `-std=gnu99`,
  applied per-file, for `,##__VA_ARGS__` comma deletion).
- `src/web/host/` — SDL2 host main + `web_unimplemented()` logging.
- `src/web/psx/` — PsyQ library stubs (one file per lib) + `call_fixups.c`
  (declaration-mismatch wrappers) + `data_shim.c` (`*_START` overlay buffers,
  linker symbols `_end`/`_stack_addr`/`_stack_size`).
- `src/web/psx/libc.c` — real implementations where trivial: PsyQ LCG
  `rand`/`srand`, `rsin`/`rcos`, fixed-point `ratan2`/`SquareRoot0`, libc
  mappings, `VSync` = `emscripten_sleep(16)`.

## Known hazards

- Conflicting declarations across game TUs make `wasm-ld` emit
  `signature_mismatch:*` trap thunks. Caller-expects-return vs void-def is
  FATAL (invalid wasm, finalize fails at parse). Fix by adding a per-TU
  `-D` rename + wrapper — never by editing the game file. Caller-void vs
  i32-def is auto-adapted (warning only). NOTE: `make` does not rebuild
  objects when these `-D` flags change — delete the affected `.o` files.
- `src/main/utils2.c:24` `#define shop_START ((char *)0x80080800)` is a real
  hardcoded PS1 address baked into `REL_BIN_OFFSETS[10]`; SHOP overlay loads
  will write to raw address 0x80080800 in wasm memory — needs a shim strategy
  in a later segment (cannot `-D` it; the file's `#define` wins).
- Hardcoded PS1 address `#define`s used as scratch/load buffers (audit):
  `include/dw/main.h` GENERAL_BUFFER `0x80010000`, TEXTURE_BUFFER `0x80088800`,
  BOSS_EFE_TMD_BUFFER `0x80058000`; `include/dw/vs.h`, `src/btl/btl.c`,
  `src/std/std_setup.c` {VS,BTL,STD}_FINISHER_TIM `0x80052ae0` …
  *_BUFF_MODEL `0x80055328`; `src/vs/vs_hud.c`/`src/std/std_hud.c`
  TMD_BUFFER `0x80038000`; `src/murd/murd.c` MURD_ORDERING_TABLE_0 `0x8008c000`.
  The ~4700 remaining `0x80??????` hits are flag bits (`0x80000000`) and
  baked pointer words inside generated `*_bss_us.c` data tables — data, not
  dereferences; revisit when the renderer/CD loader lands.
- `VSync`/`CdControl` polling: the game blocks in `VSync` loops waiting for CD
  results the stubs don't yet produce — `dw_main` currently stalls after
  `CdInit`/`CdControl`. libcd against `/disc/dw.bin` is next segment.
- `main`: game's entry is `int32_t main(void)` in `src/main/main.c`; renamed
  to `dw_main` for that TU only via `-Dmain=dw_main`
  (`mk/toolchain/emcc.mk`); `src/web/host/main.c` calls `dw_main()`.
