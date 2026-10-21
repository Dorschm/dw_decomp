/*
 * Web override of PsyQ <libetc.h>.
 *
 * The PS1 has a 1 KiB scratchpad at the fixed address 0x1f800000 and the
 * game uses it through getScratchAddr(n) (EFE scripts, models, effects...).
 * That address is far outside wasm memory, so every use trapped. Point it
 * at a real 1 KiB buffer instead (web_scratchpad, src/web/psx/data_shim.c).
 */
#ifndef WEB_LIBETC_H
#define WEB_LIBETC_H

#include_next <libetc.h>

extern unsigned char web_scratchpad[1024];

#undef getScratchAddr
#define getScratchAddr(offset) ((u_long *)(web_scratchpad + (offset) * 4))

#endif
