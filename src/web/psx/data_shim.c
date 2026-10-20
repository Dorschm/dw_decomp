/*
 * Definitions for symbols the psx build gets from linker scripts or from
 * splat-generated data .s files under asm/us/<seg>. On web there are no
 * overlay sections, so the *_START overlay base markers (normally the
 * load address of each *_REL.BIN) become plain heap buffers sized to the
 * largest retail overlay (MOV_REL.BIN = 472104 bytes -> round to 512 KiB).
 * readFile() copies overlay data into these; the linked-in C copies of
 * the overlay code are what actually executes.
 */

#include <stdint.h>

#define OVERLAY_AREA_SIZE 0x80000 /* 512 KiB >= largest *_REL.BIN */

#define DEF_OVERLAY(name) \
	__attribute__((aligned(16))) char name##_START[OVERLAY_AREA_SIZE]

DEF_OVERLAY(btl);
DEF_OVERLAY(std);
DEF_OVERLAY(fish);
DEF_OVERLAY(evl);
DEF_OVERLAY(kar);
DEF_OVERLAY(vs);
DEF_OVERLAY(mov);
DEF_OVERLAY(doo2);
DEF_OVERLAY(dooa);
DEF_OVERLAY(trn);
DEF_OVERLAY(shop);
DEF_OVERLAY(dget);
DEF_OVERLAY(trn2);
DEF_OVERLAY(murd);
DEF_OVERLAY(endi);
DEF_OVERLAY(eab);

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
