/*
 * libc-flavored PsyQ functions for the web build.
 *
 * memcpy/memset/strcpy/strlen/strcmp/strncpy/sprintf/printf/abs/bzero/
 * bcopy/malloc/free all map directly to host libc, so only the functions
 * with PsyQ-specific semantics live here:
 *   - rand/srand must reproduce PsyQ's LCG exactly (game logic depends
 *     on the sequence): seed = seed*0x41C64E6D + 0x3039, return top bits.
 *   - InitHeap* are no-ops (the host CRT owns the heap).
 */

#include <stdlib.h>
#include <string.h>

#include <web/host/log.h>

/*
 * fish.c declares bzero() K&R-style and expects an int return; musl's
 * void bzero() would leave the caller reading a result that was never
 * pushed, so this def wins over the libc.a member.
 */
void *bzero(unsigned char *s, int n)
{
	memset(s, 0, n);
	return s;
}

static unsigned int psx_seed = 1;

int rand(void)
{
	psx_seed = psx_seed * 0x41C64E6D + 0x3039;
	return (psx_seed >> 16) & 0x7FFF;
}

void srand(unsigned int seed)
{
	psx_seed = seed;
}

void InitHeap(unsigned long *head, unsigned long size)
{
	(void)head;
	(void)size;
}

void InitHeap2(unsigned long *head, unsigned long size)
{
	(void)head;
	(void)size;
}

void InitHeap3(unsigned long *head, unsigned long size)
{
	(void)head;
	(void)size;
}

/* PsyQ allocator pair: forward to the host heap. */
void *malloc3(size_t size)
{
	return malloc(size);
}

void free3(void *p)
{
	free(p);
}
