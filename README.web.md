# Digimon World — web port

This fork (`web-port` branch) runs the decompiled USA build of
*Digimon World* (SLUS_010.32) natively in a browser. It is not a rewrite:
the same game C sources are compiled twice — once to MIPS for the
byte-identical PSX/GCC target, and once to WebAssembly with Emscripten,
where the PsyQ runtime libraries (libgpu, libgte, libgs, libcd, libds,
libpress, libsnd, libspu, libetc, libmcrd, libapi) are reimplemented in C
against the original headers and verified against the original
disassembly.

You need your own copy of the game: a USA `.bin` disc image
(track 1 of the bin/cue pair, 2352-byte sectors). The file is read
locally in the browser — nothing is uploaded.

## Build

Everything builds inside the `emscripten/emsdk:3.1.74` Docker image
(Git Bash, from the repo root):

```sh
# web build → build/us/web/emcc/dw.js + dw.wasm, copied into web/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src \
  emscripten/emsdk:3.1.74 make PLATFORM=web TOOLCHAIN=emcc web

# reference check: psx/gcc output must stay byte-identical
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/dw" \
  -v dw_venv:/dw/.venv dw-build sh -c 'make -j$(nproc) TOOLCHAIN=gcc'
sha1sum build/us/psx/gcc/SLUS_010.32 build/us/psx/gcc/*_REL.BIN
```

## Run

```sh
cd web
python -m http.server 8765
# open http://localhost:8765, click "Select .bin", pick Digimon World (USA).bin
```

Keyboard: arrows = d-pad, X = Cross, Z = Square, S = Triangle,
A = Circle, Q/W = L1/R1, E/R = L2/R2, Enter = Start, Right Shift =
Select. A standard gamepad also works (SDL GameController mapping).
On touch devices an on-screen d-pad/button layout appears.

Saves live on one emulated 128 KiB memory card (slot 1), persisted in
IndexedDB. **Export save** downloads the raw `.mcr` image (loadable in
DuckStation/PCSX); **Import save** writes one back.

## Architecture (`src/web/`)

| Layer | Files | Role |
|---|---|---|
| host | `host/main.c` | entry point, IDBFS sync, calls `dw_main` |
| host | `host/disc.c` | `Module.disc` JS bridge (`readRaw`/`readUser`, EM_ASYNC_JS) — Blob.slice in the page, fd read in node |
| host | `host/input.c` | SDL2 keyboard/gamepad → PsyQ pad bitmask; `web_pad_inject`/`web_pad_set_touch` harness hooks |
| host | `host/memcard.c` | two-slot 128 KiB card images, `/memcard` IDBFS persistence |
| host | `host/overlay.c` | snapshot/restore of overlay `.data` on every `loadDynamicLibrary` (see below) |
| psx  | `psx/gpu.c`, `libgpu.c` | software GPU: command dispatch, textured/shaded primitives, VRAM, display |
| psx  | `psx/gte.c`, `libgte.c` | cop2 register file + geometry ops (`gte_*` macros via `include/emcc/inline_n.h`), libgte matrix/vector API |
| psx  | `psx/libgs.c` | Gs 2D/3D objects, cameras, ordering tables, TMD primitive stream |
| psx  | `psx/libcd.c`, `libds.c` | CD-ROM: raw/user-sector reads, file table, XA/STR sector streaming ring (`St*`), XA-ADPCM audio |
| psx  | `psx/libpress.c` | MDEC/VLC STR frame decode (`DecDCT*`) |
| psx  | `psx/mixer.c`, `libsnd.c`, `libspu.c` | native mixer (no SPU emulation): VAG ADPCM decoded once to cached PCM, 24 software voices with ADSR, libspu reverb presets on a private delay line, CD-audio mix; libsnd SEQ/VAB sequencer |
| psx  | `psx/libetc.c`, `libapi.c`, `libmcrd.c`, `libc.c` | pad/VSync (60 Hz `emscripten_sleep`), kernel events, memory-card API, libc/PsyQ helpers |
| psx  | `psx/call_fixups.c` | per-TU declaration-mismatch wrappers + debug probes (`web_debug_state`, `web_entity_dump`, …) |
| psx  | `psx/data_shim.c` | 2 MiB emulated PS1 RAM (`PSX_RAM`) for the game's hard-coded `0x80xxxxxx` addresses |

Blocking PS1 semantics (VSync waits, CD reads, overlay loads) suspend
the wasm via `-sASYNCIFY`, so game code keeps its original synchronous
structure.

### Overlay state

Retail reloads `*_REL.BIN` into RAM on every `loadDynamicLibrary()`,
re-initialising the overlay's globals. On web the overlay code is
statically linked, so `tools/gen_overlay_syms.py` enumerates every
writable data symbol from the compiled objects (`llvm-nm`), generates
`build/us/web/generated/overlay_syms.c`, and
`web_overlay_snapshot_all()`/`web_overlay_reset()` (a guarded hook in
`loadDynamicLibrary`) restore the startup bytes on each load. The audit
found no `.bss` and only immutable compiler-generated statics in overlay
TUs, so the extern symbol tables cover all mutable overlay state.

## Verified

- Full boot: BIOS-equivalent init → Sony/intro → title → new game /
  continue → File City gameplay (walked across a map transition).
- Software rendering: prerendered backgrounds, textured/lit TMD models
  with correct depth ordering, drop shadows, HUD, menus, name entry.
- STR FMV playback (opening + attract loop): libds sector streaming,
  MDEC decode, 24-bit display, XA stereo audio.
- Sound: VAB SFX, sequenced BGM, reverb. ADPCM decode checked against
  a reference Python decoder (`make PLATFORM=web TOOLCHAIN=emcc
  mixer_test`, also checks the reverb tail decays).
- Memory card: format, save, restart-and-continue round-trip.
- `make TOOLCHAIN=gcc` output remains byte-identical to retail
  extraction (sha1-gated); the only game-source edit for the port is a
  `PLATFORM_WEB`-guarded overlay-reset hook plus mechanical `PSX_PTR`
  address wraps.

## Known gaps

- Two pre-existing wasm-ld signature-mismatch warnings
  (`loadMapSounds2`, `isSoundLoaded`) from conflicting declarations in
  the game sources; auto-adapted, harmless.
- Node harness only: no audio output device (SDL headless), captures go
  through `web_audio_capture` to WAV; `web_audio_capture_on_seq(path,
  secs)` arms a capture that starts at the first sequencer note-on so
  BGM captures line up across runs.
- Untested/less-travelled paths: fishing, tournaments/VS, evolution,
  endgame overlays — they compile and load but have not all been driven
  end-to-end.
