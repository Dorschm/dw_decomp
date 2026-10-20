/*
 * Web host entry point. The game's own entry point is `int32_t main(void)`
 * in src/main/main.c, renamed to dw_main for that TU only via
 * -Dmain=dw_main (see mk/toolchain/emcc.mk). It runs an infinite loop and
 * blocks on VSync(), which becomes emscripten_sleep() under Asyncify.
 *
 * All OT/primitive links are 24-bit addresses: assert at startup that the
 * wasm static-data break and PSX_RAM both sit below 16 MiB.
 */

#include <SDL.h>
#include <emscripten/console.h>
#include <emscripten.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

extern void *sbrk(intptr_t increment);

extern int dw_main(void);
extern unsigned char PSX_RAM[0x200000];

/* src/web/psx/gpu.c */
extern int gpu_dispmask;
extern uint16_t gpu_vram[512][1024];
void gpu_present_rgba(uint8_t *out, const void *disp);
void gpu_stats_dump(void);

/* gpu_dispenv is a DISPENV; declared opaquely here to avoid the PsyQ
 * headers clashing with SDL.h */
typedef struct {
	short x, y, w, h;
	short sx, sy, sw, sh;
	uint8_t isinter, isrgb24, pad0, pad1;
} WebDispEnv;
extern WebDispEnv gpu_dispenv;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int tex_w = 640, tex_h = 480;
static uint8_t *frame_buf;
static int frame_cap;

void web_present(void)
{
	int w = gpu_dispenv.w ? gpu_dispenv.w : 320;
	int h = gpu_dispenv.h ? gpu_dispenv.h : 240;
	(void)gpu_dispmask;

	if (renderer == NULL)
		return;
	if (w != tex_w || h != tex_h) {
		if (texture)
			SDL_DestroyTexture(texture);
		texture = SDL_CreateTexture(renderer,
					    SDL_PIXELFORMAT_RGBA8888,
					    SDL_TEXTUREACCESS_STREAMING,
					    w, h);
		tex_w = w;
		tex_h = h;
	}
	if (frame_cap < w * h * 4) {
		free(frame_buf);
		frame_buf = malloc(w * h * 4);
		frame_cap = w * h * 4;
	}
	if (texture == NULL || frame_buf == NULL)
		return;
	gpu_present_rgba(frame_buf, &gpu_dispenv);
	SDL_UpdateTexture(texture, NULL, frame_buf, w * 4);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

/* Debug exports: callable from JS via ccall/cwrap. Write PPM (P6) through
 * the Emscripten FS. */
EMSCRIPTEN_KEEPALIVE
void web_dump_display(const char *path)
{
	int w = gpu_dispenv.w ? gpu_dispenv.w : 320;
	int h = gpu_dispenv.h ? gpu_dispenv.h : 240;
	FILE *f = fopen(path, "wb");
	uint8_t *buf;
	int i;

	if (f == NULL) {
		emscripten_console_logf("web_dump_display: cannot open %s", path);
		return;
	}
	buf = malloc(w * h * 4);
	gpu_present_rgba(buf, &gpu_dispenv);
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (i = 0; i < w * h; i++)
		fwrite(buf + i * 4, 1, 3, f);
	free(buf);
	fclose(f);
	emscripten_console_logf("web_dump_display: wrote %s (%dx%d)", path,
				w, h);
}

EMSCRIPTEN_KEEPALIVE
void web_dump_vram(const char *path)
{
	FILE *f = fopen(path, "wb");
	int x, y;

	if (f == NULL) {
		emscripten_console_logf("web_dump_vram: cannot open %s", path);
		return;
	}
	fprintf(f, "P6\n1024 512\n255\n");
	for (y = 0; y < 512; y++)
		for (x = 0; x < 1024; x++) {
			uint16_t p = gpu_vram[y][x];
			uint8_t c[3] = {(uint8_t)((p & 0x1f) << 3),
					(uint8_t)(((p >> 5) & 0x1f) << 3),
					(uint8_t)(((p >> 10) & 0x1f) << 3)};
			fwrite(c, 1, 3, f);
		}
	fclose(f);
	emscripten_console_logf("web_dump_vram: wrote %s", path);
}

EMSCRIPTEN_KEEPALIVE
void web_gpu_stats(void)
{
	gpu_stats_dump();
}

int main(void)
{
	uintptr_t brk = (uintptr_t)sbrk(0);
	uintptr_t ram = (uintptr_t)PSX_RAM;

	emscripten_console_logf("web: sbrk(0)=%p psx_ram=%p..%p (24bit safe: %s)",
				(void *)brk, (void *)ram,
				(void *)(ram + sizeof(char) * 0x200000),
				brk < 0x1000000 &&
				ram + 0x200000 < 0x1000000 ? "yes" : "NO");
	if (brk >= 0x1000000 || ram + 0x200000 >= 0x1000000)
		emscripten_console_log("web: FATAL: data above 16MiB breaks "
				       "24-bit OT links");

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		emscripten_console_logf("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	/* gamecontroller init is optional (fails headless under node) */
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
	window = SDL_CreateWindow("Digimon World", SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED, 320, 240, 0);
	if (window == NULL) {
		emscripten_console_logf("SDL_CreateWindow failed: %s",
					SDL_GetError());
		return 1;
	}
	/* Software renderer; tolerated to fail under headless node. */
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (renderer == NULL)
		emscripten_console_logf("web: no renderer (%s); running "
					"headless", SDL_GetError());
	emscripten_console_log("web: SDL up, calling dw_main()");
	return dw_main();
}
