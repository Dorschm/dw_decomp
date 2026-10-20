/*
 * Software PS1 GPU. VRAM is 1024x512 15-bit pixels (bit 15 = mask/STP).
 * All packet links are 24-bit linear addresses (<16 MiB); reconstruction
 * is ptr = addr, no high-byte recovery.
 */

#ifndef WEB_PSX_GPU_H
#define WEB_PSX_GPU_H

#include <stdint.h>
#include <libgpu.h>

#define GPU_VRAM_W 1024
#define GPU_VRAM_H 512

extern uint16_t gpu_vram[GPU_VRAM_H][GPU_VRAM_W];

/* Draw state (0xE1..0xE6), exposed for libgpu. */
typedef struct GpuState {
	uint16_t draw_mode;	/* raw 0xE1 bits 0-15 */
	int draw_x0, draw_y0;	/* 0xE3 */
	int draw_x1, draw_y1;	/* 0xE4 */
	int ofs_x, ofs_y;	/* 0xE5, signed 11-bit */
	uint32_t tw;		/* raw 0xE2 */
	int mask_set;		/* 0xE6 bit0: force bit15 on write */
	int mask_check;		/* 0xE6 bit1: skip pixels with bit15 set */
} GpuState;

extern GpuState gpu_state;
extern int gpu_dispmask;
extern DISPENV gpu_dispenv;

typedef struct GpuStats {
	uint32_t packets;
	uint32_t pixels;
	uint32_t by_class[16];	/* cmd>>4 histogram */
	uint32_t unknown[8];
	uint32_t ot_walks;	/* DrawOTag calls */
	uint32_t ot_pkts;	/* packets seen across all walks */
	uint32_t clipped;	/* put_pixel rejects via draw area */
	uint32_t loads;		/* gpu_load_image calls */
	uint32_t load_px;	/* pixels uploaded */
	uint32_t tex_px;	/* textured pixels written */
	uint32_t tex_miss;	/* texel==0 skips */
	uint32_t tex_prim[3];	/* textured prims by depth 4/8/15bpp */
	double draw_ms;		/* wall ms inside DrawOTag this vsync frame */
} GpuStats;

extern GpuStats gpu_stats;

void gpu_reset(void);
void gpu_exec_packet(const uint32_t *words, int nwords);
void gpu_draw_env(const DRAWENV *env);		/* apply E1-E6 + isbg clear */
void gpu_load_image(const RECT *r, const uint32_t *data);
void gpu_store_image(const RECT *r, uint32_t *data);
void gpu_move_image(const RECT *r, int dx, int dy);
void gpu_clear_image(const RECT *r, uint8_t rv, uint8_t gv, uint8_t bv);
void gpu_present_rgba(uint8_t *out, const DISPENV *disp);
void gpu_stats_dump(void);
void gpu_dump_vram_ppm(const char *path);

#endif
