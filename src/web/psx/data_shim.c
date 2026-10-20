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

__attribute__((aligned(16))) unsigned char PSX_RAM[0x200000];

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
