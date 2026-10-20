/*
 * Web host entry point. The game's own entry point is `int32_t main(void)`
 * in src/main/main.c, renamed to dw_main for that TU only via
 * -Dmain=dw_main (see mk/toolchain/emcc.mk). It runs an infinite loop and
 * blocks on VSync(), which becomes emscripten_sleep() under Asyncify.
 */

#include <SDL.h>
#include <emscripten/console.h>

extern int dw_main(void);

static SDL_Window *window;

void web_present(void)
{
	/* placeholder: GPU upload + SDL_RenderPresent land in a later segment */
}

int main(void)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		emscripten_console_logf("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	window = SDL_CreateWindow("Digimon World", SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED, 640, 480, 0);
	if (window == NULL) {
		emscripten_console_logf("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}
	emscripten_console_log("web: SDL up, calling dw_main()");
	return dw_main();
}
