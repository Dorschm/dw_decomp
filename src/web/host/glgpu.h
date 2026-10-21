/*
 * Native WebGL2 renderer for the web build. Replaces the software
 * rasterizer in src/web/psx/gpu.c when a WebGL2 context is available:
 * gpu.c still decodes the game's GP0 primitive packets (the game builds
 * them itself), but triangles/rects/lines are drawn by the GPU into an
 * upscaled framebuffer laid out like PS1 VRAM. Uploaded texture data
 * stays in the CPU-side gpu_vram array, mirrored to an R16UI texture
 * that the fragment shader decodes (4/8/15 bpp + CLUT).
 *
 * Plain C interface (no PsyQ types) so it compiles with system headers.
 */

#ifndef WEB_HOST_GLGPU_H
#define WEB_HOST_GLGPU_H

#include <stdint.h>

typedef struct GlVtx {
	int x, y;	/* VRAM coords, drawing offset applied */
	int r, g, b;	/* 0..255 */
	int u, v;	/* texel coords 0..255 */
} GlVtx;

/* Per-primitive state, captured by gpu.c at draw time. */
typedef struct GlPrim {
	int textured, raw, semi, blend;	/* blend = E1 bits 5-6 */
	int tbx, tby, depth;		/* texture page base + 0/1/2 */
	unsigned clut, tw;		/* raw CLUT word, raw E2 bits */
	int cx0, cy0, cx1, cy1;		/* draw area (inclusive) */
} GlPrim;

extern int glgpu_on;

int glgpu_init(const uint16_t *vram);
void glgpu_tri(const GlVtx v[3], const GlPrim *p);
void glgpu_fill(int x, int y, int w, int h, int r, int g, int b);
/* CPU gpu_vram changed in this rect (LoadImage/MoveImage/clear): mirror
 * it into the texture and overwrite the framebuffer area with it. */
void glgpu_vram_changed(int x, int y, int w, int h);
void glgpu_present(int dx, int dy, int dw, int dh, int rgb24);

#endif
