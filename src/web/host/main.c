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
#include "glgpu.h"
#include <emscripten/console.h>
#include <emscripten.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *sbrk(intptr_t increment);

extern int dw_main(void);
extern void web_overlay_snapshot_all(void);
extern unsigned char PSX_RAM[0x200000];

/* src/web/psx/gpu.c */
extern int gpu_dispmask;
extern uint16_t gpu_vram[1024][1024];
void gpu_present_rgba(uint8_t *out, const void *disp);
void gpu_stats_dump(void);
void gpu_present_gl(void);

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

/* ---------------- audio ---------------------------------------------
 * PsyQ runs the libsnd sequencer from a root-counter interrupt at
 * VBLANK_MINUS Hz (the game selects SS_TICK240 = 240 Hz). Here the
 * pump is driven once per 60 Hz VSync tick and interleaves four
 * sequencer ticks with SPU rendering at exact 183.75-sample spacing
 * (735 = 44100/60 = 4 * 183.75; the fractional sample is accumulated
 * so long-run timing is exact).
 */
#include "../psx/mixer.h"
extern void SsSeqCalledTbyT(void);
extern long web_seq_tick_rate(void);

static SDL_AudioDeviceID audio_dev;
static int audio_ok;
static int16_t audio_frame[735 * 2];

/* headless WAV capture */
static char wav_path[256];
static long wav_target, wav_done;
static int16_t *wav_buf;

static void wav_finish(void)
{
	FILE *f;
	long data = wav_done * 4;
	unsigned char hdr[44] = {
		'R','I','F','F', 0,0,0,0, 'W','A','V','E',
		'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
		0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0, 16,0,
		'd','a','t','a', 0,0,0,0
	};
	long riff = data + 36;
	hdr[4]=riff; hdr[5]=riff>>8; hdr[6]=riff>>16; hdr[7]=riff>>24;
	hdr[40]=data; hdr[41]=data>>8; hdr[42]=data>>16; hdr[43]=data>>24;
	f = fopen(wav_path, "wb");
	if (f) {
		fwrite(hdr, 1, 44, f);
		fwrite(wav_buf, 4, wav_done, f);
		fclose(f);
		emscripten_console_logf("audio: captured %ld samples -> %s",
					wav_done, wav_path);
	} else {
		emscripten_console_logf("audio: cannot write %s", wav_path);
	}
	free(wav_buf);
	wav_buf = NULL;
	wav_path[0] = 0;
}

EMSCRIPTEN_KEEPALIVE
void web_audio_capture(const char *path, int seconds)
{
	if (wav_buf)
		wav_finish();
	strncpy(wav_path, path, sizeof(wav_path) - 1);
	wav_path[sizeof(wav_path) - 1] = 0;
	wav_target = 44100L * seconds;
	wav_done = 0;
	wav_buf = malloc(wav_target * 4);
	if (wav_buf == NULL)
		emscripten_console_log("audio: capture alloc failed");
	else
		emscripten_console_logf("audio: capturing %ds -> %s",
					seconds, path);
}

/* harness hook: arm a capture that starts at the first sequencer
 * note-on (libsnd calls web_audio_seq_note), so captures of the BGM
 * line up across runs regardless of wall-clock timing */
static char seq_cap_path[256];
static int seq_cap_secs;

EMSCRIPTEN_KEEPALIVE
void web_audio_capture_on_seq(const char *path, int seconds)
{
	strncpy(seq_cap_path, path, sizeof(seq_cap_path) - 1);
	seq_cap_secs = seconds;
}

void web_audio_seq_note(void)
{
	if (seq_cap_secs > 0) {
		web_audio_capture(seq_cap_path, seq_cap_secs);
		seq_cap_secs = 0;
	}
}

void web_audio_pump(void)
{
	static double tick_pos;
	int rate = (int)web_seq_tick_rate();
	int ntick, k;
	int done = 0;
	double step;
	const int total = 735;

	if (rate <= 0)
		rate = 240;
	ntick = rate / 60;
	if (ntick < 1)
		ntick = 1;
	step = 44100.0 / (double)rate;

	for (k = 0; k < ntick; k++) {
		int next;
		tick_pos += step;
		next = (int)(tick_pos + 0.5);
		if (next > total)
			next = total;
		if (next > done) {
			mix_render(audio_frame + done * 2, next - done);
			done = next;
		}
		SsSeqCalledTbyT();
	}
	if (done < total)
		mix_render(audio_frame + done * 2, total - done);
	tick_pos -= total;

	if (audio_ok) {
		if (SDL_GetQueuedAudioSize(audio_dev) <= 4u * sizeof(audio_frame))
			SDL_QueueAudio(audio_dev, audio_frame,
				       sizeof(audio_frame));
	}
	if (wav_buf && wav_path[0]) {
		long n = wav_target - wav_done;
		if (n > total)
			n = total;
		memcpy(wav_buf + wav_done * 2, audio_frame, n * 4);
		wav_done += n;
		if (wav_done >= wav_target)
			wav_finish();
	}
}

static void audio_init(void)
{
	SDL_AudioSpec want, got;

	/* main() only inits video/events; the audio subsystem must be
	 * brought up here or OpenAudioDevice fails (it does under node,
	 * which has no Web Audio — the pump still runs for capture) */
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		emscripten_console_logf("audio: SDL audio init failed (%s)",
					SDL_GetError());
	SDL_zero(want);
	want.freq = 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
	if (audio_dev == 0) {
		emscripten_console_logf("audio: OpenAudioDevice failed (%s); "
					"pump still runs for capture",
					SDL_GetError());
		return;
	}
	audio_ok = 1;
	SDL_PauseAudioDevice(audio_dev, 0);
	emscripten_console_logf("audio: device up %dHz fmt=%d ch=%d",
				got.freq, got.format, got.channels);
}

EMSCRIPTEN_KEEPALIVE
void web_audio_resume(void)
{
	if (audio_ok)
		SDL_PauseAudioDevice(audio_dev, 0);
}

void web_present(void)
{
	int w = gpu_dispenv.w ? gpu_dispenv.w : 320;
	int h = gpu_dispenv.h ? gpu_dispenv.h : 240;
	(void)gpu_dispmask;

	if (glgpu_on) {
		gpu_present_gl();
		return;
	}
	if (renderer == NULL)
		return;
	if (w != tex_w || h != tex_h) {
		if (texture)
			SDL_DestroyTexture(texture);
		texture = SDL_CreateTexture(renderer,
					    SDL_PIXELFORMAT_ABGR8888,
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

/* sprite baking (tools/node_bake.js): hide the map background + overlay layers so only
 * entities draw on black, and read the display buffer back as RGBA */
int web_hide_map;
int web_hide_entities;	/* bit i = skip drawing ENTITY_TABLE[i] (sprite baking) */

EMSCRIPTEN_KEEPALIVE
uint8_t *web_grab_display(void)
{
	static uint8_t *buf;

	if (buf == NULL)
		buf = malloc(1024 * 512 * 4);
	gpu_present_rgba(buf, &gpu_dispenv);
	return buf;
}

EMSCRIPTEN_KEEPALIVE
int web_grab_w(void)
{
	return gpu_dispenv.w ? gpu_dispenv.w : 320;
}

EMSCRIPTEN_KEEPALIVE
void web_set_hide_entities(int v)
{
	web_hide_entities = v;
}

EMSCRIPTEN_KEEPALIVE
void web_set_hide_map(int v)
{
	web_hide_map = v;
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
	/* Native WebGL2 renderer when available; otherwise the software
	 * GPU + SDL 2D renderer (also the headless node path). */
	if (!glgpu_init(&gpu_vram[0][0]))
		renderer = SDL_CreateRenderer(window, -1,
					      SDL_RENDERER_SOFTWARE);
	if (!glgpu_on && renderer == NULL)
		emscripten_console_logf("web: no renderer (%s); running "
					"headless", SDL_GetError());
	audio_init();
	mix_reset();
	web_overlay_snapshot_all();
	emscripten_console_log("web: SDL up, calling dw_main()");
	return dw_main();
}
