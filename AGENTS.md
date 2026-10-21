# dw_decomp — agent notes

Digimon World (USA, SLUS_010.32) decompilation. Work happens on branch
`web-port`. `origin` = https://github.com/Dorschm/dw_decomp (fork),
`upstream` = https://github.com/jype0/dw_decomp.

The USA executable is extracted to `disks/us/SLUS_010.32`
(sha1 `31ec666b987d5b007dec40eecc4092d27ed9085b`).

## Game-source policy (updated 2026-10-21)

Game sources (`src/<overlay>/*.c`, `include/dw/*`) MAY be edited or
rewritten where that makes the web build work better (owner's call —
the goal is a native browser port, not hardware emulation). Default to
`#ifdef PLATFORM_WEB` guards so the `psx/gcc` output stays byte-identical
to retail; gate with
`sha1sum build/us/psx/gcc/SLUS_010.32 build/us/psx/gcc/*_REL.BIN` before
and after. If a change genuinely can't be guarded, say so in the commit.
Port-only code still lives under `src/web/`, `include/emcc/`, `mk/`,
`tools/`, `web/`. Declaration conflicts between TUs can now be fixed at
the source instead of via `-D` renames in `mk/toolchain/emcc.mk`.

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
- `src/web/psx/` — PsyQ library implementations (one file per lib) +
  `call_fixups.c` (declaration-mismatch wrappers) + `data_shim.c`
  (`PSX_RAM[0x200000]` emulated PS1 memory + linker symbols
  `_end`/`_stack_addr`/`_stack_size`).
- `src/web/host/disc.c` — `Module.disc` JS bridge (`readRaw`/`readUser`,
  EM_ASYNC_JS). Disc source comes from `web/index.html` (file picker via
  Blob.slice, or `?disc=<url>` fetch) or the node harness; `CdInit`
  suspends until attached.
- Overlay base `*_START` symbols: on web they are `#define`s into PSX_RAM
  inside `src/main/utils2.c` (PLATFORM_WEB guard) at the retail load
  addresses from `config/us/overlay.ld` (0x80052AE0 group: btl/std/vs;
  dget/shop 0x80080800; doo2/fish 0x80070000; dooa 0x80080000;
  eab/endi/evl 0x80060000; kar 0x80053800; mov 0x80010000;
  murd 0x8007C000; trn/trn2 0x80088800).
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
- All hardcoded PS1 address `#define`s are wrapped in `PSX_PTR` and resolve
  into `PSX_RAM` on web (GENERAL_BUFFER `0x80010000`, TEXTURE_BUFFER
  `0x80088800`, BOSS_EFE_TMD_BUFFER `0x80058000`, the `*_FINISHER_*`/
  `*_BUFF_MODEL` group `0x80052ae0`-`0x80055328`, TMD_BUFFER `0x80038000`,
  ordering tables `0x8008c000`/`0x8008e000`, DOOA/EVL MMD+SHARD buffers
  `0x80020000`/`0x80044800`, `shop_START` `0x80080800`).
  The ~4700 remaining `0x80??????` hits are flag bits (`0x80000000`) and
  baked pointer words inside generated `*_bss_us.c` data tables — data, not
  dereferences; revisit when the renderer lands.
- `VSync`/`CdControl` polling: the game blocks in `VSync` loops waiting for CD
  results the stubs don't yet produce — `dw_main` currently stalls after
  `CdInit`/`CdControl`. libcd against `/disc/dw.bin` is next segment.
- `main`: game's entry is `int32_t main(void)` in `src/main/main.c`; renamed
  to `dw_main` for that TU only via `-Dmain=dw_main`
  (`mk/toolchain/emcc.mk`); `src/web/host/main.c` calls `dw_main()`.

## Open issues

- Overlay `.data`/`.bss` is re-initialised from disc on every overlay load
  in retail, but on web the statically linked overlay globals persist
  across loads (e.g. battle -> field -> battle keeps stale state).
  Candidate fix: build each overlay as an Emscripten SIDE_MODULE and
  dlopen it in `loadDynamicLibrary`.
- libds/STR movie playback (`DecDCT*`, `St*` ring streaming) is stubbed;
  `StGetNext` fabricates a header with `frameCount=0x7FFFFFFF` so
  `MOV_playMovie` completes immediately instead of hanging. Real FMV
  decode is a later segment.
- Memory card (`libmcrd`) is stubs; saves are a later segment.
