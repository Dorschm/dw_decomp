/* Overlay .data re-initialisation.
 *
 * On retail, every loadDynamicLibrary() reloads the overlay's *_REL.BIN,
 * resetting its initialised globals to the file's bytes. On web the
 * overlays are statically linked so their globals persist across loads.
 * We snapshot each overlay's writable data (the extern 'D' symbols
 * enumerated by tools/gen_overlay_syms.py) once at startup and restore
 * it on each overlay load.
 *
 * The audit found no .bss in any overlay TU and only compiler-generated
 * immutable statics (jump tables, string literals), so the extern
 * tables cover all writable overlay state.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/emscripten.h>

typedef struct {
	void *addr;
	uint32_t size;
} OvrSym;

extern const OvrSym *const OVR_SYM_TABLES[17];
extern const uint32_t OVR_SYM_COUNTS[17];

/* saved images: one malloc'd blob per overlay */
static uint8_t *ovr_saved[17];
static int snap_taken;

extern void emscripten_console_log(const char *str);
extern void emscripten_console_logf(const char *fmt, ...);

static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
	uint32_t h = 2166136261u;
	while (n--) {
		h ^= *p++;
		h *= 16777619u;
	}
	return h;
}

EMSCRIPTEN_KEEPALIVE
void web_overlay_snapshot_all(void)
{
	uint32_t total = 0;
	if (snap_taken)
		return;
	/* ?ovreset=0 disables the reset (A/B debugging) */
	if (!emscripten_run_script_int(
		    "typeof location==='undefined'?1:"
		    "(new URLSearchParams(location.search).get('ovreset')==='0'?0:1)")) {
		emscripten_console_log("ovr: reset disabled by ?ovreset=0");
		return;
	}
	for (int lib = 1; lib <= 16; lib++) {
		const OvrSym *t = OVR_SYM_TABLES[lib];
		uint32_t n = OVR_SYM_COUNTS[lib], bytes = 0, off = 0;
		uint8_t *dst;
		for (uint32_t i = 0; i < n; i++)
			bytes += t[i].size;
		dst = malloc(bytes ? bytes : 1);
		for (uint32_t i = 0; i < n; i++) {
			memcpy(dst + off, t[i].addr, t[i].size);
			off += t[i].size;
		}
		ovr_saved[lib] = dst;
		total += bytes;
	}
	snap_taken = 1;
	emscripten_console_logf("ovr: snapshotted %u bytes of overlay .data",
				total);
}

/* called by loadDynamicLibrary() (src/main/utils2.c) under PLATFORM_WEB */
void web_overlay_reset(int lib)
{
	const OvrSym *t;
	uint32_t n, off = 0;
	const uint8_t *src;

	if (!snap_taken || lib < 1 || lib > 16)
		return;
	t = OVR_SYM_TABLES[lib];
	n = OVR_SYM_COUNTS[lib];
	src = ovr_saved[lib];
	/* report the first globals that had drifted (values the overlay
	 * would have kept without the reset) before restoring */
	{
		static int drifted;
		uint32_t o2 = 0;
		for (uint32_t i = 0; i < n && drifted < 40; i++) {
			const uint8_t *live = t[i].addr;
			for (uint32_t j = 0; j < t[i].size; j++) {
				if (live[j] == src[o2 + j])
					continue;
				drifted++;
				emscripten_console_logf(
					"ovr: lib %d drift sym #%u +%u: "
					"%02x (live) -> %02x (reset)",
					lib, i, j, live[j], src[o2 + j]);
				break;
			}
			o2 += t[i].size;
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		memcpy(t[i].addr, src + off, t[i].size);
		off += t[i].size;
	}
	{
		uint32_t o3 = 0;
		int same = 1;
		for (uint32_t i = 0; i < n; i++) {
			if (memcmp(t[i].addr, src + o3, t[i].size) != 0)
				same = 0;
			o3 += t[i].size;
		}
		emscripten_console_logf(
			"ovr: reset lib %d (%u syms, %u bytes) "
			"post-checksum=%s", lib, n, off,
			same ? "snapshot" : "MISMATCH");
	}
}

/* debug: FNV-1a checksum of an overlay's live .data vs the snapshot.
 * Returns 1 when identical. Logs the first differing symbol. */
EMSCRIPTEN_KEEPALIVE
int web_overlay_checksum(int lib)
{
	const OvrSym *t;
	uint32_t n, off = 0;
	const uint8_t *src;
	int same = 1;

	if (!snap_taken || lib < 1 || lib > 16)
		return -1;
	t = OVR_SYM_TABLES[lib];
	n = OVR_SYM_COUNTS[lib];
	src = ovr_saved[lib];
	for (uint32_t i = 0; i < n; i++) {
		uint32_t live = fnv1a(t[i].addr, t[i].size);
		uint32_t saved = fnv1a(src + off, t[i].size);
		if (live != saved && same) {
			same = 0;
			emscripten_console_logf(
				"ovr: lib %d drift at sym #%u off %u "
				"(live %08x != saved %08x)",
				lib, i, off, live, saved);
		}
		off += t[i].size;
	}
	return same;
}
