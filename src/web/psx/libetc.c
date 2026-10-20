/*
 * libetc for the web build: a real 60 Hz VSync timer plus SDL-backed
 * digital pad input. The game's main loops call VSync() to pace
 * themselves and PadRead(0|1) for input.
 */

#include <stddef.h>
#include <stdint.h>

#include <libetc.h>

/* declared manually: <emscripten.h>/<SDL.h> drag in system headers that
 * clash with the PsyQ headers on this file's include path. */
extern void emscripten_sleep(unsigned int ms);
extern double emscripten_get_now(void);
extern void emscripten_console_logf(const char *fmt, ...);
#define EMSCRIPTEN_KEEPALIVE __attribute__((used))

#include <web/host/log.h>

/* src/web/host/input.c - separate TU because <SDL.h> and the PsyQ
 * headers disagree on system typedefs. Returns PAD* bits, 1=pressed. */
extern uint32_t web_pad_state(void);

extern void web_present(void);
extern void web_audio_pump(void);
extern void web_mc_flush(void);

unsigned int web_vsync_count;

static double next_frame_ms;
static void (*vsync_cb)(void);
static unsigned int pad_spin;

/* per-frame timing: whole VSync-to-VSync iteration and time inside
 * DrawOTag (accumulated by libgpu.c into gpu_stats.draw_ms) */
#include "gpu.h"
#define PERF_N 600
static float perf_iter[PERF_N], perf_draw[PERF_N];
static unsigned int perf_n, perf_pos;
static double perf_last;

static void perf_report(void)
{
	float it[PERF_N], dr[PERF_N];
	unsigned int n = perf_n < PERF_N ? perf_n : PERF_N, i, j, k;
	double si = 0, sd = 0;
	for (k = 0, i = 0; i < n; i++, k++) {
		unsigned int idx = (perf_pos + PERF_N - n + i) % PERF_N;
		it[k] = perf_iter[idx];
		dr[k] = perf_draw[idx];
		si += it[i];
		sd += dr[i];
	}
	if (n == 0)
		return;
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++) {
			if (it[j] < it[i]) { float t = it[i]; it[i] = it[j]; it[j] = t; }
			if (dr[j] < dr[i]) { float t = dr[i]; dr[i] = dr[j]; dr[j] = t; }
		}
	emscripten_console_logf(
		"perf: n=%u iter mean=%.2fms p95=%.2fms | drawot mean=%.2fms "
		"p95=%.2fms | other mean=%.2fms",
		n, si / n, it[(unsigned int)(n * 0.95f)], sd / n,
		dr[(unsigned int)(n * 0.95f)], (si - sd) / n);
	perf_n = 0;
}

EMSCRIPTEN_KEEPALIVE
void web_perf_report(void) { perf_report(); }

int VSync(int mode)
{
	int wait;
	double delay;

	if (mode < 0)
		return (int)(emscripten_get_now() * 6.0); /* ~15625 Hz-ish */

	wait = (mode > 0) ? mode : 1;
	if (next_frame_ms == 0.0 || next_frame_ms < emscripten_get_now())
		next_frame_ms = emscripten_get_now() + 1000.0 / 60.0;

	while (wait-- > 0) {
		pad_spin = 0;
		delay = next_frame_ms - emscripten_get_now();
		if (delay > 0)
			emscripten_sleep((unsigned int)delay);
		next_frame_ms += 1000.0 / 60.0;
		web_vsync_count++;
		if ((web_vsync_count % 300u) == 0u)
			emscripten_console_logf("vsync: %u frames",
						web_vsync_count);
		{
			double now = emscripten_get_now();
			perf_iter[perf_pos] =
				(float)(perf_last ? now - perf_last : 0.0);
			perf_draw[perf_pos] = (float)gpu_stats.draw_ms;
			perf_pos = (perf_pos + 1) % PERF_N;
			if (perf_n < PERF_N)
				perf_n++;
			perf_last = now;
			gpu_stats.draw_ms = 0.0;
		}
		web_present();
		web_audio_pump();
		web_mc_flush();
		if (vsync_cb != NULL)
			vsync_cb();
	}
	return (int)web_vsync_count;
}

int VSyncCallback(void (*f)())
{
	void (*old)(void) = vsync_cb;

	vsync_cb = f;
	return (int)(intptr_t)old;
}

static void (*reset_cb)(void);
static void (*stop_cb)(void);
static void (*restart_cb)(void);

int ResetCallback(void)
{
	return (int)(intptr_t)reset_cb;
}

int StopCallback(void)
{
	return (int)(intptr_t)stop_cb;
}

int RestartCallback(void)
{
	return (int)(intptr_t)restart_cb;
}

long DMACallback(int dma, void (*func)())
{
	(void)dma;
	return (long)(intptr_t)func;
}

int CheckCallback(void)
{
	return 0;
}

/* ---------------------------------------------------------------- */
/* pad                                                               */

void PadInit(int mode)
{
	(void)mode;
}

void PadStop(void)
{
}

int PadStart(int mode)
{
	(void)mode;
	return 0;
}

u_long PadRead(int id)
{
	(void)id;
	/* Busy input loops like handlePause() poll PadRead without any
	 * VSync yield; on real HW the pad edge arrives asynchronously, but
	 * here injected input comes from JS timers that starve unless we
	 * suspend. Detect the spin (many polls without a VSync) and yield
	 * ~1ms per poll until the next VSync. */
	if (++pad_spin > 150)
		emscripten_sleep(1);
	/* pad1 in the low half, pad2 (none yet) in the high half */
	return web_pad_state();
}
