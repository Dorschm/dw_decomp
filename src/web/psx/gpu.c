/*
 * Software PS1 GPU for the web build. Semantics follow the public nocash
 * psx-spx GPU documentation. All drawing is synchronous: DrawOTag/DrawPrim
 * execute packets immediately and DrawSync is a no-op.
 *
 * GP0 decode (command byte = bits 24-31 of the first body word):
 *   0x00/0x01    nop / clear cache (ignored)
 *   0x02         fill rectangle
 *   0x20-0x3F    polygons (bit0 quad, bit1 semi-trans, bit2 textured,
 *                bit3 gouraud, bit4 raw texture)
 *   0x40-0x5F    lines (bit0 polyline, bit1 semi-trans, bit3 gouraud)
 *   0x60-0x7F    rectangles (bit0-1 size, bit2 textured, bit3 semi-trans)
 *   0x80/0xA0/0xC0 VRAM copy / CPU->VRAM / VRAM->CPU (also reached via
 *                LoadImage/StoreImage/MoveImage helpers)
 *   0xE1..0xE6   draw settings
 */

/* NB: <stdio.h>/<string.h> resolve to the PsyQ headers here (declarations
 * clash with real libc), so this file avoids libc calls: no memset/printf.
 * Logging goes through emscripten_console_logf. */
#include <libgpu.h>
#include <emscripten/console.h>

#include "gpu.h"
#include "../host/log.h"

static void zmem(void *p, unsigned int n)
{
	uint8_t *c = p;
	while (n--)
		*c++ = 0;
}
#define ABS(a) ((a) < 0 ? -(a) : (a))

uint16_t gpu_vram[GPU_VRAM_H][GPU_VRAM_W];
GpuState gpu_state;
int gpu_dispmask;
DISPENV gpu_dispenv;
GpuStats gpu_stats;

static const int8_t dither_mtx[4][4] = {
	{-4, 0, -3, 1},
	{2, -2, 3, -1},
	{-3, 1, -4, 0},
	{3, -1, 2, -2},
};

#define STAT(c) gpu_stats.by_class[((c) >> 4) & 0xf]++

static inline uint8_t vram_r(uint16_t p) { return p & 0x1f; }
static inline uint8_t vram_g(uint16_t p) { return (p >> 5) & 0x1f; }
static inline uint8_t vram_b(uint16_t p) { return (p >> 10) & 0x1f; }

static inline int clip_x(int x)
{
	if (x < gpu_state.draw_x0) x = gpu_state.draw_x0;
	if (x > gpu_state.draw_x1) x = gpu_state.draw_x1;
	return x;
}
static inline int clip_y(int y)
{
	if (y < gpu_state.draw_y0) y = gpu_state.draw_y0;
	if (y > gpu_state.draw_y1) y = gpu_state.draw_y1;
	return y;
}

/* Write one 15-bit pixel, honouring mask bit + semi-transparency. */
static void put_pixel(int x, int y, int r, int g, int b, int abr, int stp,
		      int mask)
{
	uint16_t *px, back;
	int pr, pg, pb;

	(void)stp; /* the stp bit is carried in 'mask' by callers */
	if (x < gpu_state.draw_x0 || x > gpu_state.draw_x1 ||
	    y < gpu_state.draw_y0 || y > gpu_state.draw_y1) {
		gpu_stats.clipped++;
		return;
	}
	px = &gpu_vram[y][x];
	back = *px;
	if (gpu_state.mask_check && (back & 0x8000))
		return;
	if (abr) {
		int br = vram_r(back), bg = vram_g(back), bb = vram_b(back);
		switch ((gpu_state.draw_mode >> 5) & 3) {
		case 0:
			pr = (br + r) / 2; pg = (bg + g) / 2; pb = (bb + b) / 2;
			break;
		case 1:
			pr = br + r; pg = bg + g; pb = bb + b;
			break;
		case 2:
			pr = br - r; pg = bg - g; pb = bb - b;
			break;
		default:
			pr = br + r / 4; pg = bg + g / 4; pb = bb + b / 4;
			break;
		}
		if (pr < 0) pr = 0;
		if (pr > 31) pr = 31;
		if (pg < 0) pg = 0;
		if (pg > 31) pg = 31;
		if (pb < 0) pb = 0;
		if (pb > 31) pb = 31;
	} else {
		pr = r; pg = g; pb = b;
	}
	*px = (uint16_t)(pr | (pg << 5) | (pb << 10) |
			 ((mask | gpu_state.mask_set) << 15));
	gpu_stats.pixels++;
}

/* ---- texture fetch -------------------------------------------------- */

static int tex_base_x, tex_base_y, tex_depth;

static void set_tpage(uint16_t tpage)
{
	/* E1 bits: 0-3 tpage x (64px), 4 tpage y (256), 5-6 abr,
	 * 7-8 depth, 9 dither, 10 dfe, 11 tpage y2 (512) */
	gpu_state.draw_mode = (gpu_state.draw_mode & 0xF000) |
			      (tpage & 0x0FFF);
	tex_base_x = (tpage & 0xF) * 64;
	tex_base_y = ((tpage >> 4) & 1) * 256 + ((tpage >> 11) & 1) * 512;
	tex_depth = (tpage >> 7) & 3;
	if (tex_depth > 2) tex_depth = 2;
}

static uint16_t fetch_texel(int u, int v, uint16_t clut)
{
	/* Texture window: E2 fields are 8-pixel units; mask 0 means
	 * "window disabled" (u,v pass through unchanged). */
	int mx = (gpu_state.tw & 0x1f) * 8;
	int my = ((gpu_state.tw >> 5) & 0x1f) * 8;
	int ox = ((gpu_state.tw >> 10) & 0x1f) * 8;
	int oy = ((gpu_state.tw >> 15) & 0x1f) * 8;

	u = (u & ~mx) | (ox & mx);
	v = (v & ~my) | (oy & my);

	switch (tex_depth) {
	case 0: {
		uint16_t w = gpu_vram[tex_base_y + v][tex_base_x + u / 4];
		int idx = (w >> ((u & 3) * 4)) & 0xf;
		return gpu_vram[clut >> 6][(clut & 0x3f) * 16 + idx];
	}
	case 1: {
		uint16_t w = gpu_vram[tex_base_y + v][tex_base_x + u / 2];
		int idx = (w >> ((u & 1) * 8)) & 0xff;
		return gpu_vram[clut >> 6][(clut & 0x3f) * 16 + idx];
	}
	default:
		return gpu_vram[tex_base_y + v][tex_base_x + u];
	}
}

/* ---- scanline triangle fill ----------------------------------------- */

typedef struct Vtx {
	int x, y;
	int r, g, b;
	int u, v;
} Vtx;

static void fill_tri(const Vtx *tv, int gouraud, int textured, int abr,
		     int raw, uint16_t clut)
{
	Vtx v[3] = {tv[0], tv[1], tv[2]};
	Vtx t;
	long area;
	int x0, y0, x1, y1, x, y;

	area = (long)(v[1].x - v[0].x) * (v[2].y - v[0].y) -
	       (long)(v[1].y - v[0].y) * (v[2].x - v[0].x);
	if (area == 0)
		return;
	if (area < 0) {
		t = v[1]; v[1] = v[2]; v[2] = t;
		area = -area;
	}

	x0 = v[0].x; y0 = v[0].y;
	if (v[1].x < x0) x0 = v[1].x;
	if (v[1].y < y0) y0 = v[1].y;
	if (v[2].x < x0) x0 = v[2].x;
	if (v[2].y < y0) y0 = v[2].y;
	x1 = v[0].x; y1 = v[0].y;
	if (v[1].x > x1) x1 = v[1].x;
	if (v[1].y > y1) y1 = v[1].y;
	if (v[2].x > x1) x1 = v[2].x;
	if (v[2].y > y1) y1 = v[2].y;

	x0 = clip_x(x0); x1 = clip_x(x1);
	y0 = clip_y(y0); y1 = clip_y(y1);
	if (x0 > x1 || y0 > y1)
		return;

	{
		int dx0 = v[2].x - v[1].x, dy0 = v[2].y - v[1].y;
		int dx1 = v[0].x - v[2].x, dy1 = v[0].y - v[2].y;
		int dx2 = v[1].x - v[0].x, dy2 = v[1].y - v[0].y;
		/* top-left rule (y-down screen space): edges going up or
		 * horizontal-left are inclusive, others exclusive */
		int b0 = (dy0 < 0 || (dy0 == 0 && dx0 < 0)) ? 0 : -1;
		int b1 = (dy1 < 0 || (dy1 == 0 && dx1 < 0)) ? 0 : -1;
		int b2 = (dy2 < 0 || (dy2 == 0 && dx2 < 0)) ? 0 : -1;
		int dit = (gpu_state.draw_mode & 0x200) &&
			  (gouraud || (textured && !raw));

		for (y = y0; y <= y1; y++) {
			for (x = x0; x <= x1; x++) {
				long e0 = (long)dx0 * (y - v[1].y) -
					  (long)dy0 * (x - v[1].x) + b0;
				long e1 = (long)dx1 * (y - v[2].y) -
					  (long)dy1 * (x - v[2].x) + b1;
				long e2 = (long)dx2 * (y - v[0].y) -
					  (long)dy2 * (x - v[0].x) + b2;
				int r, g, b, stp = 1, mask = 0, blend = abr;
				if (e0 < 0 || e1 < 0 || e2 < 0)
					continue;
				if (gouraud) {
					r = (int)((e0 * v[0].r + e1 * v[1].r +
						   e2 * v[2].r) / area);
					g = (int)((e0 * v[0].g + e1 * v[1].g +
						   e2 * v[2].g) / area);
					b = (int)((e0 * v[0].b + e1 * v[1].b +
						   e2 * v[2].b) / area);
				} else {
					r = v[0].r; g = v[0].g; b = v[0].b;
				}
				if (textured) {
					int u = (int)((e0 * v[0].u + e1 * v[1].u +
						       e2 * v[2].u) / area);
					int vv = (int)((e0 * v[0].v + e1 * v[1].v +
							e2 * v[2].v) / area);
					uint16_t tx = fetch_texel(u, vv, clut);
					int tr, tg, tb;
					if (tx == 0)
						continue;
					stp = (tx >> 15) & 1;
					if (abr && !stp)
						blend = 0;
					tr = vram_r(tx); tg = vram_g(tx);
					tb = vram_b(tx);
					if (raw) {
						r = tr << 3; g = tg << 3;
						b = tb << 3;
					} else {
						r = (tr << 3) * r / 128;
						g = (tg << 3) * g / 128;
						b = (tb << 3) * b / 128;
						if (r > 255) r = 255;
						if (g > 255) g = 255;
						if (b > 255) b = 255;
					}
					mask = stp;
				}
				if (dit) {
					int d = dither_mtx[y & 3][x & 3];
					r += d; g += d; b += d;
					if (r < 0) r = 0;
					if (r > 255) r = 255;
					if (g < 0) g = 0;
					if (g > 255) g = 255;
					if (b < 0) b = 0;
					if (b > 255) b = 255;
				}
				put_pixel(x, y, r >> 3, g >> 3, b >> 3,
					  blend, stp, mask);
			}
		}
	}
}

/* ---- lines ----------------------------------------------------------- */

static void draw_line(int x0, int y0, int x1, int y1,
		      int r0, int g0, int b0, int r1, int g1, int b1,
		      int abr)
{
	int dx = x1 - x0, dy = y1 - y0;
	int n = ABS(dx) > ABS(dy) ? ABS(dx) : ABS(dy);
	int i;

	if (n == 0) {
		put_pixel(x0 + gpu_state.ofs_x, y0 + gpu_state.ofs_y,
			  r0 >> 3, g0 >> 3, b0 >> 3, abr, 0, 0);
		return;
	}
	for (i = 0; i < n; i++) { /* last pixel not drawn (PS1) */
		int x = x0 + dx * i / n + gpu_state.ofs_x;
		int y = y0 + dy * i / n + gpu_state.ofs_y;
		int r = r0 + (r1 - r0) * i / n;
		int g = g0 + (g1 - g0) * i / n;
		int b = b0 + (b1 - b0) * i / n;
		put_pixel(x, y, r >> 3, g >> 3, b >> 3, abr, 0, 0);
	}
}

/* ---- packets --------------------------------------------------------- */

static int sxy(uint32_t w, int *x, int *y)
{
	*x = ((int32_t)(w << 16) >> 16);
	*y = ((int32_t)w >> 16);
	return 0;
}

static void poly_common(const uint32_t *w, int nwords, int code)
{
	/* GP0 poly bits: 0=raw tex, 1=semi-trans, 2=textured, 3=quad,
	 * 4=gouraud */
	Vtx v[4];
	int quad = code & 8;
	int abr = code & 2;
	int textured = code & 4;
	int gouraud = code & 0x10;
	int raw = code & 1;
	int n = quad ? 4 : 3;
	int i = 1, k;
	uint16_t clut = 0;

	zmem(v, sizeof(v));
	if (nwords < 4)
		return;
	for (k = 0; k < n; k++) {
		uint32_t c;
		if (k != 0 && gouraud) {
			c = w[i++];
			v[k].r = c & 0xff; v[k].g = (c >> 8) & 0xff;
			v[k].b = (c >> 16) & 0xff;
		} else {
			/* flat + gouraud vertex0 colour: rgb0 shares the
			 * command word (low 24 bits) */
			c = w[0];
			v[k].r = c & 0xff; v[k].g = (c >> 8) & 0xff;
			v[k].b = (c >> 16) & 0xff;
		}
		if (i >= nwords)
			return;
		sxy(w[i], &v[k].x, &v[k].y);
		i++;
		v[k].x += gpu_state.ofs_x;
		v[k].y += gpu_state.ofs_y;
		if (textured) {
			uint32_t tw;
			if (i >= nwords)
				return;
			tw = w[i++];
			v[k].u = tw & 0xff;
			v[k].v = (tw >> 8) & 0xff;
			if (k == 0)
				clut = (tw >> 16) & 0xffff;
			if (k == 1)
				set_tpage((tw >> 16) & 0xffff);
		}
	}
	{
		int dx = v[0].x, dx2 = v[0].x, dy = v[0].y, dy2 = v[0].y;
		for (k = 1; k < n; k++) {
			if (v[k].x < dx) dx = v[k].x;
			if (v[k].x > dx2) dx2 = v[k].x;
			if (v[k].y < dy) dy = v[k].y;
			if (v[k].y > dy2) dy2 = v[k].y;
		}
		if (dx2 - dx > 1023 || dy2 - dy > 511)
			return;
	}
	fill_tri(&v[0], gouraud, textured, abr, raw, clut);
	if (quad) {
		Vtx q[3] = {v[1], v[2], v[3]};
		fill_tri(q, gouraud, textured, abr, raw, clut);
	}
}

static void rect_common(const uint32_t *w, int nwords, int code)
{
	/* 0x60-0x7F: bits3-4 size (0 var,1 1x1,2 8x8,3 16x16), bit2 textured,
	 * bit1 semi-trans (SetSemiTrans ORs 0x02 into the code byte) */
	int size = (code >> 3) & 3, abr = code & 2, textured = code & 4;
	int x, y, ww = 0, hh = 0, i = 1;
	uint8_t r = w[0] & 0xff, g = (w[0] >> 8) & 0xff, b = (w[0] >> 16) & 0xff;
	uint16_t clut = 0;
	int u = 0, v = 0;

	if (nwords < 2)
		return;
	sxy(w[i++], &x, &y);
	x += gpu_state.ofs_x;
	y += gpu_state.ofs_y;
	if (textured) {
		uint32_t tw;
		if (i >= nwords)
			return;
		tw = w[i++];
		u = tw & 0xff;
		v = (tw >> 8) & 0xff;
		clut = (tw >> 16) & 0xffff;
	}
	switch (size) {
	case 0:
		if (i >= nwords)
			return;
		ww = w[i] & 0xffff;
		hh = (w[i] >> 16) & 0xffff;
		break;
	case 1: ww = hh = 1; break;
	case 2: ww = hh = 8; break;
	case 3: ww = hh = 16; break;
	}
	{
		int xx, yy;
		int x1 = clip_x(x), x2 = clip_x(x + ww - 1);
		int y1 = clip_y(y), y2 = clip_y(y + hh - 1);
		int dit = (gpu_state.draw_mode & 0x200) && textured;
		if (x1 > x2 || y1 > y2)
			return;
		for (yy = y1; yy <= y2; yy++) {
			for (xx = x1; xx <= x2; xx++) {
				int pr = r, pg = g, pb = b;
				int blend = abr, mask = 0;
				if (textured) {
					uint16_t tx = fetch_texel(
						u + (xx - x), v + (yy - y),
						clut);
					int tr, tg, tb;
					if (tx == 0)
						continue;
					if (abr && !(tx & 0x8000))
						blend = 0;
					mask = (tx >> 15) & 1;
					tr = vram_r(tx) << 3;
					tg = vram_g(tx) << 3;
					tb = vram_b(tx) << 3;
					pr = tr * r / 128;
					pg = tg * g / 128;
					pb = tb * b / 128;
					if (pr > 255) pr = 255;
					if (pg > 255) pg = 255;
					if (pb > 255) pb = 255;
				}
				if (dit) {
					int d = dither_mtx[yy & 3][xx & 3];
					pr += d; pg += d; pb += d;
					if (pr < 0) pr = 0;
					if (pr > 255) pr = 255;
					if (pg < 0) pg = 0;
					if (pg > 255) pg = 255;
					if (pb < 0) pb = 0;
					if (pb > 255) pb = 255;
				}
				put_pixel(xx, yy, pr >> 3, pg >> 3, pb >> 3,
					  blend, 0, mask);
			}
		}
	}
}

static void cmd_polyline(const uint32_t *w, int nwords, int code)
{
	/* line bits: 1=semi-trans, 3=polyline, 4=gouraud */
	int abr = code & 2, gouraud = code & 0x10;
	int i = 1, have_prev = 0;
	uint8_t r0 = w[0] & 0xff, g0 = (w[0] >> 8) & 0xff,
		b0 = (w[0] >> 16) & 0xff;
	int px = 0, py = 0;
	uint8_t pr = r0, pg = g0, pb = b0;

	while (i < nwords) {
		int x, y;
		uint8_t r = r0, g = g0, b = b0;
		if (gouraud && !have_prev) {
			/* first vertex: colour already read */
		} else if (gouraud) {
			uint32_t c = w[i++];
			if (c == 0x55555555) break;
			r = c & 0xff; g = (c >> 8) & 0xff;
			b = (c >> 16) & 0xff;
		}
		if (i >= nwords) break;
		if (w[i] == 0x55555555 || w[i] == 0x50005000)
			break;
		sxy(w[i++], &x, &y);
		if (have_prev)
			draw_line(px, py, x, y, pr, pg, pb, r, g, b, abr);
		px = x; py = y; pr = r; pg = g; pb = b;
		have_prev = 1;
		if (!(code & 8))
			break; /* single line */
	}
}

void gpu_exec_packet(const uint32_t *words, int nwords)
{
	int i = 0;

	gpu_stats.packets++;
	while (i < nwords) {
		uint32_t w = words[i];
		int code = w >> 24;
		STAT(code);
		switch (code & 0xE0) {
		case 0x00:
			if (code == 0x02) {
				/* fill rectangle */
				uint8_t r = w & 0xff, g = (w >> 8) & 0xff,
					b = (w >> 16) & 0xff;
				int x, y, ww, hh;
				if (i + 3 > nwords)
					return;
				sxy(words[i + 1], &x, &y);
				ww = words[i + 2] & 0x3ff;
				hh = (words[i + 2] >> 16) & 0x1ff;
				{
					int x1 = clip_x(x), x2 =
						clip_x(x + ww - 1);
					int y1 = clip_y(y), y2 =
						clip_y(y + hh - 1);
					int xx, yy;
					uint16_t c = (r >> 3) | ((g >> 3) << 5) |
						     ((b >> 3) << 10);
					for (yy = y1; yy <= y2; yy++)
						for (xx = x1; xx <= x2; xx++) {
							if (gpu_state.mask_check &&
							    (gpu_vram[yy][xx] &
							     0x8000))
								continue;
							gpu_vram[yy][xx] = c;
							gpu_stats.pixels++;
						}
				}
				i += 3;
			} else {
				i++;
			}
			break;
		case 0x20: {
			int nv = (code & 8) ? 4 : 3;
			int n = 1 + nv + ((code & 4) ? nv : 0) +
				((code & 0x10) ? nv - 1 : 0);
			poly_common(&words[i], nwords - i, code);
			i += n;
			break;
		}
		case 0x40:
		case 0x60:
			if ((code & 0xE0) == 0x40) {
				int gouraud = code & 0x10;
				cmd_polyline(&words[i], nwords - i, code);
				if (!(code & 8)) {
					/* single line: F2=3 words, G2=4 */
					i += gouraud ? 4 : 3;
				} else {
					/* polyline: scan to 0x55555555 */
					int j = i + 1;
					while (j < nwords &&
					       (words[j] & 0xff000000) !=
						       0x55000000 &&
					       words[j] != 0x55555555 &&
					       words[j] != 0x50005000)
						j++;
					i = (j < nwords) ? j + 1 : nwords;
				}
				if (i > nwords)
					i = nwords;
			} else {
				int size = (code >> 3) & 3;
				rect_common(&words[i], nwords - i, code);
				i += 1 /* hdr */ + 1 /* xy */ +
				     ((code & 4) ? 1 : 0) /* uv+clut */ +
				     (size ? 0 : 1) /* wh */;
			}
			break;
		case 0x80: /* vram->vram: src, dst, wh */
		case 0xA0: /* cpu->vram: dst, wh, data */
		case 0xC0: /* vram->cpu: src, wh */
			if (code == 0x80) {
				int sx, sy, dx, dy, ww, hh, x, y;
				if (i + 3 >= nwords)
					return;
				sxy(words[i + 1], &sx, &sy);
				sxy(words[i + 2], &dx, &dy);
				ww = words[i + 3] & 0xffff;
				hh = (words[i + 3] >> 16) & 0xffff;
				for (y = 0; y < hh; y++)
					for (x = 0; x < ww; x++)
						gpu_vram[(dy + y) & 0x1ff]
							[(dx + x) & 0x3ff] =
							gpu_vram[(sy + y) &
								 0x1ff]
							[(sx + x) & 0x3ff];
				i += 4;
			} else if (code == 0xA0) {
				int dx, dy, ww, hh;
				RECT r;
				int nw;
				if (i + 2 >= nwords)
					return;
				sxy(words[i + 1], &dx, &dy);
				ww = words[i + 2] & 0xffff;
				hh = (words[i + 2] >> 16) & 0xffff;
				r.x = dx; r.y = dy; r.w = ww; r.h = hh;
				nw = (ww * hh + 1) / 2;
				if (i + 3 + nw > nwords)
					nw = nwords - i - 3;
				if (nw > 0)
					gpu_load_image(&r, &words[i + 3]);
				i += 3 + (nw > 0 ? nw : 0);
				if (nw <= 0)
					i = nwords;
			} else {
				i += 3; /* VRAM->CPU in packet stream: skip */
			}
			break;
		case 0xE0:
			switch (code) {
			case 0xE1:
				set_tpage(w & 0xffff);
				break;
			case 0xE2:
				gpu_state.tw = w & 0xfffff;
				break;
			case 0xE3:
				gpu_state.draw_x0 = w & 0x3ff;
				gpu_state.draw_y0 = (w >> 10) & 0x1ff;
				break;
			case 0xE4:
				gpu_state.draw_x1 = w & 0x3ff;
				gpu_state.draw_y1 = (w >> 10) & 0x1ff;
				break;
			case 0xE5:
				gpu_state.ofs_x =
					((int32_t)(w << 21) >> 21);
				gpu_state.ofs_y =
					((int32_t)(w << 10) >> 21);
				break;
			case 0xE6:
				gpu_state.mask_set = w & 1;
				gpu_state.mask_check = w & 2;
				break;
			default:
				break;
			}
			i++;
			break;
		default:
			gpu_stats.unknown[0]++;
			i++;
			break;
		}
	}
}

/* ---- direct helpers -------------------------------------------------- */

void gpu_load_image(const RECT *r, const uint32_t *data)
{
	int x, y, n = 0;
	gpu_stats.loads++;
	gpu_stats.load_px += r->w * r->h;
	for (y = 0; y < r->h; y++)
		for (x = 0; x < r->w; x += 2) {
			uint32_t w = data[n++];
			gpu_vram[(r->y + y) & 0x1ff][(r->x + x) & 0x3ff] =
				w & 0xffff;
			if (x + 1 < r->w)
				gpu_vram[(r->y + y) & 0x1ff]
					[(r->x + x + 1) & 0x3ff] = w >> 16;
		}
}

void gpu_store_image(const RECT *r, uint32_t *data)
{
	int x, y, n = 0;
	for (y = 0; y < r->h; y++)
		for (x = 0; x < r->w; x += 2) {
			uint32_t w = gpu_vram[(r->y + y) & 0x1ff]
					     [(r->x + x) & 0x3ff];
			if (x + 1 < r->w)
				w |= gpu_vram[(r->y + y) & 0x1ff]
					     [(r->x + x + 1) & 0x3ff] << 16;
			data[n++] = w;
		}
}

void gpu_move_image(const RECT *r, int dx, int dy)
{
	int x, y;
	if (dy > r->y)
		for (y = r->h - 1; y >= 0; y--)
			for (x = r->w - 1; x >= 0; x--)
				gpu_vram[(dy + y) & 0x1ff][(dx + x) & 0x3ff] =
					gpu_vram[(r->y + y) & 0x1ff]
						[(r->x + x) & 0x3ff];
	else
		for (y = 0; y < r->h; y++)
			for (x = 0; x < r->w; x++)
				gpu_vram[(dy + y) & 0x1ff][(dx + x) & 0x3ff] =
					gpu_vram[(r->y + y) & 0x1ff]
						[(r->x + x) & 0x3ff];
}

void gpu_clear_image(const RECT *r, uint8_t rv, uint8_t gv, uint8_t bv)
{
	int x, y;
	uint16_t c = (rv >> 3) | ((gv >> 3) << 5) | ((bv >> 3) << 10);
	for (y = 0; y < r->h; y++)
		for (x = 0; x < r->w; x++)
			gpu_vram[(r->y + y) & 0x1ff][(r->x + x) & 0x3ff] = c;
}

void gpu_draw_env(const DRAWENV *env)
{
	uint32_t w;
	set_tpage(env->tpage);
	gpu_state.draw_mode = (gpu_state.draw_mode & 0x600) |
			      (env->tpage & 0x0bff) |
			      (env->dtd ? 0x200 : 0) | (env->dfe ? 0x400 : 0);
	gpu_state.draw_x0 = env->clip.x;
	gpu_state.draw_y0 = env->clip.y;
	gpu_state.draw_x1 = env->clip.x + env->clip.w - 1;
	gpu_state.draw_y1 = env->clip.y + env->clip.h - 1;
	gpu_state.ofs_x = env->ofs[0];
	gpu_state.ofs_y = env->ofs[1];
	w = ((env->tw.w - 1) >> 3) & 0x1f;
	w |= (((env->tw.h - 1) >> 3) & 0x1f) << 5;
	w |= ((env->tw.x >> 3) & 0x1f) << 10;
	w |= ((env->tw.y >> 3) & 0x1f) << 15;
	if (env->tw.w == 0 && env->tw.h == 0)
		w = 0;
	gpu_state.tw = w;
	if (env->isbg)
		gpu_clear_image(&env->clip, env->r0, env->g0, env->b0);
}

/* ResetGraph(0) resets GPU state only — VRAM contents are preserved
 * (the real hardware does not clear VRAM on reset). */
void gpu_reset(void)
{
	emscripten_console_log("gpu: reset");
	zmem(&gpu_state, sizeof(gpu_state));
	gpu_state.draw_x1 = GPU_VRAM_W - 1;
	gpu_state.draw_y1 = GPU_VRAM_H - 1;
	zmem(&gpu_dispenv, sizeof(gpu_dispenv));
	tex_base_x = tex_base_y = tex_depth = 0;
}

/* ---- presentation ---------------------------------------------------- */

void gpu_present_rgba(uint8_t *out, const DISPENV *disp)
{
	int x, y;
	uint16_t p;

	for (y = 0; y < disp->disp.h; y++) {
		for (x = 0; x < disp->disp.w; x++) {
			int sx = disp->disp.x + x, sy = disp->disp.y + y;
			uint8_t *o = out + (y * disp->disp.w + x) * 4;
			if (disp->isrgb24) {
				/* 24bpp: 3 bytes/pixel from VRAM */
				int off = sy * GPU_VRAM_W * 2 + sx * 3;
				uint8_t *v = (uint8_t *)gpu_vram;
				o[0] = v[off];
				o[1] = v[off + 1];
				o[2] = v[off + 2];
			} else {
				p = gpu_vram[sy & 0x1ff][sx & 0x3ff];
				o[0] = vram_r(p) << 3;
				o[1] = vram_g(p) << 3;
				o[2] = vram_b(p) << 3;
			}
			o[3] = 255;
		}
	}
}

void gpu_stats_dump(void)
{
	static const char *names[16] = {
		"misc", "poly", "poly", "poly", "line", "line", "rect", "rect",
		"copy", "copy", "cpu2vram", "cpu2vram", "vram2cpu", "vram2cpu",
		"env", "env"
	};
	int i;
	emscripten_console_logf("gpu: packets=%u pixels=%u",
				gpu_stats.packets, gpu_stats.pixels);
	for (i = 0; i < 16; i++)
		if (gpu_stats.by_class[i])
			emscripten_console_logf("gpu:   %s=%u", names[i],
						gpu_stats.by_class[i]);
	emscripten_console_logf(
		"gpu: ot_walks=%u ot_pkts=%u clipped=%u loads=%u load_px=%u",
		gpu_stats.ot_walks, gpu_stats.ot_pkts, gpu_stats.clipped,
		gpu_stats.loads, gpu_stats.load_px);
	emscripten_console_logf(
		"gpu: draw area %d,%d..%d,%d ofs %d,%d mode %04x tw %05x",
		gpu_state.draw_x0, gpu_state.draw_y0, gpu_state.draw_x1,
		gpu_state.draw_y1, gpu_state.ofs_x, gpu_state.ofs_y,
		gpu_state.draw_mode, gpu_state.tw);
}
