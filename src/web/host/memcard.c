/*
 * Memory-card persistence backend.
 *
 * The card images themselves live in src/web/psx/libmcrd.c (the card
 * logic needs the PsyQ libmcrd.h result codes); this file owns only the
 * on-disk /memcard/cardN.mcr store and the browser IDBFS sync hook.
 *
 * Browser: web/index.html mounts IDBFS at /memcard and does the initial
 * FS.syncfs(true) before dw_main starts; writes are flushed lazily by
 * web_mc_flush() (called from the present pump) which then schedules
 * FS.syncfs(false) through the JS hook below.
 *
 * Headless node: MEMFS is enough - run_node.js pre-loads a saved image
 * into /memcard before starting and copies it out afterwards.
 */

#include <emscripten.h>
#include <emscripten/console.h>
#include <stdio.h>
#include <stdint.h>

#define MC_IMG_SIZE 0x20000
#define MC_NCARDS 2

static uint8_t mc_dirty[MC_NCARDS];

static const char *mc_path(int slot, char *buf)
{
	snprintf(buf, 64, "/memcard/card%d.mcr", slot);
	return buf;
}

/* Load a persisted image; returns bytes read (0 = none -> caller
 * formats a fresh card). */
long web_mc_load(int slot, void *dst, long cap)
{
	char path[64];
	FILE *f = fopen(mc_path(slot, path), "rb");
	long n = 0;

	if (f == NULL)
		return 0;
	n = (long)fread(dst, 1, cap, f);
	fclose(f);
	emscripten_console_logf("mc: loaded %s (%ld bytes)", path, n);
	return n;
}

/* Mark a card image dirty; the next web_mc_flush() writes it out. */
void web_mc_mark_dirty(int slot)
{
	if (slot >= 0 && slot < MC_NCARDS)
		mc_dirty[slot] = 1;
}

EM_JS(void, web_mc_idb_sync, (void), {
	if (typeof FS !== "undefined" && FS.syncfs) {
		if (Module._mcSyncPending)
			return;
		Module._mcSyncPending = true;
		FS.syncfs(false, function (err) {
			Module._mcSyncPending = false;
			if (err)
				console.warn("mc: syncfs failed: " + err);
		});
	}
});

/* Called once per VSync from the host pump. */
void web_mc_flush(void)
{
	char path[64];
	int slot;

	for (slot = 0; slot < MC_NCARDS; slot++) {
		FILE *f;
		if (!mc_dirty[slot])
			continue;
		mc_dirty[slot] = 0;
		f = fopen(mc_path(slot, path), "wb");
		if (f == NULL) {
			emscripten_console_logf("mc: cannot write %s", path);
			continue;
		}
		/* image lives in libmcrd.c */
		{
			extern const unsigned char *web_mc_image(int);
			const unsigned char *img = web_mc_image(slot);
			if (img != NULL)
				fwrite(img, 1, MC_IMG_SIZE, f);
		}
		fclose(f);
		emscripten_console_logf("mc: stored %s", path);
		web_mc_idb_sync();
	}
}

/* Browser import: JS replaced /memcard/card0.mcr; reload the live
 * image. Implemented in libmcrd.c via this hook. */
EMSCRIPTEN_KEEPALIVE
void web_mc_reload(int slot)
{
	extern void web_mc_reload_image(int);
	web_mc_reload_image(slot);
}
