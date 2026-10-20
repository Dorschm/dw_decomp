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

#include <web/host/log.h>

/* src/web/host/input.c - separate TU because <SDL.h> and the PsyQ
 * headers disagree on system typedefs. Returns PAD* bits, 1=pressed. */
extern uint32_t web_pad_state(void);

extern void web_present(void);

unsigned int web_vsync_count;

static double next_frame_ms;
static void (*vsync_cb)(void);

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
		delay = next_frame_ms - emscripten_get_now();
		if (delay > 0)
			emscripten_sleep((unsigned int)delay);
		next_frame_ms += 1000.0 / 60.0;
		web_vsync_count++;
		if ((web_vsync_count % 300u) == 0u)
			emscripten_console_logf("vsync: %u frames",
						web_vsync_count);
		web_present();
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
	/* pad1 in the low half, pad2 (none yet) in the high half */
	return web_pad_state();
}
