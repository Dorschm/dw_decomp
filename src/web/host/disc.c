/*
 * Disc access over JS. Module.disc is provided by web/index.html (file
 * picker or ?disc= fetch) or by the node test harness. EM_ASYNC_JS makes
 * these awaitable from anywhere in the game's call stack via Asyncify,
 * so CdReadSync stays a blocking call exactly like PsyQ's.
 */

#include "disc.h"

#include <emscripten.h>

EM_ASYNC_JS(int, js_disc_wait_ready, (void), {
	if (Module.disc && Module.disc.ready)
		await Module.disc.ready;
	return (Module.disc && Module.disc.readUser) ? 0 : -1;
});

EM_ASYNC_JS(int, js_disc_read_user, (uint32_t lba, uint32_t nsec,
				     uintptr_t dst), {
	var u8 = await Module.disc.readUser(lba, nsec);
	if (!u8 || u8.length < nsec * 2048)
		return -1;
	HEAPU8.set(u8.subarray(0, nsec * 2048), dst);
	return 0;
});

EM_ASYNC_JS(int, js_disc_read_raw, (uint32_t lba, uint32_t nsec,
				    uintptr_t dst), {
	var u8 = await Module.disc.readRaw(lba, nsec);
	if (!u8 || u8.length < nsec * 2352)
		return -1;
	HEAPU8.set(u8.subarray(0, nsec * 2352), dst);
	return 0;
});

int disc_wait_ready(void)
{
	return js_disc_wait_ready();
}

int disc_read_user(uint32_t lba, uint32_t count, void *dst)
{
	return js_disc_read_user(lba, count, (uintptr_t)dst);
}

int disc_read_raw(uint32_t lba, uint32_t count, void *dst)
{
	return js_disc_read_raw(lba, count, (uintptr_t)dst);
}
