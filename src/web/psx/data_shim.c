/*
 * Definitions for symbols the psx build gets from linker scripts or from
 * splat-generated data .s files under asm/us/<seg>.
 *
 * PSX_RAM is the emulated 2 MiB PS1 main memory. Every hard-coded
 * 0x80xxxxxx address literal in the game resolves into it via PSX_PTR
 * (include/dw/types.h), and the *_START overlay bases in utils2.c point
 * at the retail load addresses inside it.
 */

#include <stdint.h>
#include <emscripten/console.h>
#include <emscripten/heap.h>

__attribute__((aligned(16))) unsigned char PSX_RAM[0x200000];

/* the PS1 scratchpad (0x1f800000, 1 KiB) -- see include/emcc/libetc.h */
__attribute__((aligned(16))) unsigned char web_scratchpad[1024];

/*
 * Safety net for baked PS1 pointers. Game data tables still hold literal
 * 0x80xxxxxx (KSEG0) / 0xA0xxxxxx (KSEG1) addresses that were never
 * routed through PSX_PTR; on web those are out of wasm memory and trap.
 * PsyQ shims that take data pointers pass them through here: a PS1
 * address is mapped into PSX_RAM and logged (first 60), so the data
 * table that produced it can be fixed at the source.
 */
/* debug: base of the emulated PS1 RAM, so the browser driver can peek at it
 * (Module.HEAPU8.subarray(base + addr & 0x1fffff, ...)) */
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE unsigned char *web_psx_ram(void)
{
	return PSX_RAM;
}

void *web_psx_ptr(const void *p, const char *who)
{
	uintptr_t v = (uintptr_t)p;

	if ((v >> 24) == 0x80 || (v >> 24) == 0xA0) {
		static int n;
		if (n++ < 60)
			emscripten_console_logf(
				"psxptr: %s got PS1 address %08x -> PSX_RAM+%x",
				who, (unsigned)v, (unsigned)(v & 0x1fffff));
		return PSX_RAM + (v & 0x1fffff);
	}
	if (v < 0x1000 || v + 0x10000 > emscripten_get_heap_size()) {
		static int bad;
		static unsigned char zero[0x10000];
		if (bad++ < 60)
			emscripten_console_logf(
				"psxptr: %s got invalid pointer %08x (heap %u)",
				who, (unsigned)v, (unsigned)emscripten_get_heap_size());
		return zero;
	}
	return (void *)p;
}

/*
 * Linker-script symbols the game reads in initializeHeap()
 * (src/main/main.c): heap = (_end & ~0xf) + 0x10,
 * size = (_stack_addr - (_stack_size << 10)) & ~0xf - heap.
 * On web InitHeap3 is a no-op (host malloc owns the heap), so the values
 * only need to exist.
 */
unsigned long _end;
unsigned long _stack_addr;
unsigned long _stack_size;
