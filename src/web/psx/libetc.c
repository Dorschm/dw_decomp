/* libetc stubs for the web build - real PsyQ signatures. */

#include <libetc.h>

/* declared manually: <emscripten.h> drags in system headers that clash
 * with the PsyQ headers also on this file's include path. */
extern void emscripten_sleep(unsigned int ms);

#include <web/host/log.h>

static int vsync_count;

/*
 * The game spins on VSync() in its main loops; under Asyncify this sleep
 * yields to the browser event loop (~one 60Hz frame).
 */
int VSync(int mode)
{
	(void)mode;
	emscripten_sleep(16);
	return ++vsync_count;
}

int VSyncCallback(void (*f)(void))
{
	(void)f;
	return web_unimplemented("VSyncCallback");
}

void PadInit(int mode)
{
	(void)mode;
	web_unimplemented("PadInit");
}

u_long PadRead(int id)
{
	(void)id;
	return (u_long)web_unimplemented("PadRead");
}

int ResetCallback(void)
{
	return (int)web_unimplemented("ResetCallback");
}
