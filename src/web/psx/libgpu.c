/*
 * libgpu for the web build: synchronous PsyQ-compatible implementation
 * backed by the software GPU in gpu.c. Semantics taken from the game's own
 * disassembly (asm/us/main/psyq/libgpu.s); each function notes its glabel.
 *
 * Ordering-table links are 24-bit linear addresses; reconstruction is
 * ptr = addr with no high byte. All OTs/primitives must live below 16 MiB.
 */

#include <libgpu.h>
#include <string.h>
#include <stdio.h>

#include "gpu.h"
#include "../host/log.h"

#define OT_END 0x00FFFFFF
#define PK24(p) ((uint32_t)(uintptr_t)(p) & 0xFFFFFF)

/* Terminator packet every empty OT entry resolves to (ClearOTagR asm:
 * each entry ends at a shared 0-word NOP whose link is 0xFFFFFF). */
static uint32_t gpu_nop_pkt = OT_END;

static DRAWENV cur_drawenv;
static int gpu_debug;
static int gpu_vidmode;		/* 0 = NTSC */

/* glabel _otc order: entries chained org[i] -> org[i-1], org[0] -> nop */
u_long *ClearOTagR(u_long *ot, int n)
{
	int i;
	for (i = n - 1; i > 0; i--)
		ot[i] = PK24(&ot[i - 1]);
	ot[0] = PK24(&gpu_nop_pkt);
	return ot;
}

u_long *ClearOTag(u_long *ot, int n)
{
	int i;
	for (i = 0; i < n - 1; i++)
		ot[i] = PK24(&ot[i + 1]);
	ot[n - 1] = PK24(&gpu_nop_pkt);
	return ot;
}

/* glabel AddPrim */
void AddPrim(void *ot, void *p)
{
	uint32_t *o = ot, *q = p;
	*q = (*q & 0xFF000000) | (*o & OT_END);
	*o = (*o & 0xFF000000) | PK24(q);
}

/* glabel AddPrims */
void AddPrims(void *ot, void *p0, void *p1)
{
	uint32_t *o = ot, *a = p0, *b = p1;
	*a = (*a & 0xFF000000) | (*o & OT_END);
	*o = (*o & 0xFF000000) | PK24(b);
}

void CatPrim(void *p0, void *p1)
{
	uint32_t *a = p0, *b = p1;
	*a = (*a & 0xFF000000) | PK24(b);
}

void TermPrim(void *p)
{
	uint32_t *q = p;
	*q = (*q & 0xFF000000) | OT_END;
}

/* glabel MargePrim: merge p1 into p0 if the combined body fits 0x11 */
int MargePrim(void *p0, void *p1)
{
	uint8_t *a = p0, *b = p1;
	int len = a[3] + b[3] + 1;
	if (len >= 0x11)
		return -1;
	a[3] = len;
	*(uint32_t *)b = 0;
	return 0;
}

u_long DrawSyncCallback(void (*func)())
{
	(void)func;
	return 0;
}

void *NextPrim(void *p)
{
	uint32_t a = *(uint32_t *)p & OT_END;
	return a == OT_END ? NULL : (void *)(uintptr_t)a;
}

int IsEndPrim(void *p)
{
	return (*(uint32_t *)p & OT_END) == OT_END;
}

/* ---- environment ----------------------------------------------------- */

/* glabel ResetGraph (mode 0 = full reset, 1 = cancel, 3 = re-init env) */
int ResetGraph(int mode)
{
	static int rlog;
	if (mode == 0 || mode == 3 || mode == 5) {
		if (rlog < 10) {
			rlog++;
			printf("gpu: ResetGraph(%d)\n", mode);
		}
	}
	if (mode == 0) {
		gpu_reset();
		gpu_dispmask = 0;
		gpu_debug = 0;
	} else if (mode == 3 || mode == 5) {
		{
			uint8_t *c = (uint8_t *)&gpu_state;
			unsigned int n = sizeof(gpu_state);
			while (n--)
				*c++ = 0;
		}
		gpu_state.draw_x1 = GPU_VRAM_W - 1;
		gpu_state.draw_y1 = GPU_VRAM_H - 1;
		gpu_dispmask = 0;
	}
	return 0;
}

int SetGraphDebug(int level)
{
	int old = gpu_debug;
	gpu_debug = level;
	return old;
}

int GetGraphDebug(void)
{
	return gpu_debug;
}

void SetDispMask(int mask)
{
	gpu_dispmask = mask;
}

int SetVideoMode(int mode)
{
	int old = gpu_vidmode;
	gpu_vidmode = mode & 1;
	return old;
}

int GetVideoMode(void)
{
	return gpu_vidmode;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
	env->disp.x = x;
	env->disp.y = y;
	env->disp.w = w;
	env->disp.h = h;
	env->screen.x = 0;
	env->screen.y = 0;
	env->screen.w = 0;
	env->screen.h = 0;
	env->isinter = 0;
	env->isrgb24 = 0;
	return env;
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
	env->clip.x = x;
	env->clip.y = y;
	env->clip.w = w;
	env->clip.h = h;
	env->ofs[0] = x;
	env->ofs[1] = y;
	env->tw.x = env->tw.y = 0;
	env->tw.w = env->tw.h = 0;
	env->tpage = 0x20;
	env->dtd = 1;
	env->dfe = 0;
	env->isbg = 0;
	env->r0 = env->g0 = env->b0 = 0;
	return env;
}

DISPENV *PutDispEnv(DISPENV *env)
{
	gpu_dispenv = *env;
	if (gpu_debug)
		DumpDispEnv(env);
	return env;
}

DRAWENV *PutDrawEnv(DRAWENV *env)
{
	cur_drawenv = *env;
	gpu_draw_env(env);
	if (gpu_debug)
		DumpDrawEnv(env);
	return env;
}

DISPENV *GetDispEnv(DISPENV *env)
{
	*env = gpu_dispenv;
	return env;
}

DRAWENV *GetDrawEnv(DRAWENV *env)
{
	*env = cur_drawenv;
	return env;
}

/* ---- drawing --------------------------------------------------------- */

/* glabel DrawOTag */
void DrawOTag(u_long *p)
{
	uint32_t tag = *p;
	gpu_stats.ot_walks++;
	int guard = 0;
	while ((tag & OT_END) != OT_END) {
		uint32_t *pkt = (uint32_t *)(uintptr_t)(tag & OT_END);
		gpu_stats.ot_pkts++;
		tag = *pkt;
		if (tag >> 24)
			gpu_exec_packet(pkt + 1, tag >> 24);
		if (++guard > 0x100000) {
			printf("gpu: DrawOTag runaway\n");
			break;
		}
	}
}

void DrawPrim(void *p)
{
	uint32_t tag = *(uint32_t *)p;
	if (tag >> 24)
		gpu_exec_packet((uint32_t *)p + 1, tag >> 24);
}

void DrawOTagEnv(u_long *p, DRAWENV *env)
{
	PutDrawEnv(env);
	DrawOTag(p);
}

int DrawSync(int mode)
{
	(void)mode;
	return 0;
}

/* ---- VRAM transfers ---------------------------------------------------- */

int ClearImage(RECT *rect, u_char r, u_char g, u_char b)
{
	gpu_clear_image(rect, r, g, b);
	return 0;
}

int ClearImage2(RECT *rect, u_char r, u_char g, u_char b)
{
	int saved = gpu_state.draw_x0;
	gpu_clear_image(rect, r, g, b);
	(void)saved;
	return 0;
}

extern unsigned long web_vsync_count;
extern uint8_t PSX_RAM[];

static const char *src_class(const void *p, uintptr_t *off)
{
	uintptr_t v = (uintptr_t)p - (uintptr_t)PSX_RAM;
	*off = v;
	return v < 0x200000 ? "PSX_RAM" : "native";
}

int LoadImage(RECT *rect, u_long *p)
{
	static int logged;
	if (web_vsync_count >= 2400 && logged < 300) {
		uintptr_t off;
		const char *cl = src_class(p, &off);
		const uint32_t *w = (const uint32_t *)p;
		logged++;
		printf("gpu: LoadImage %d,%d %dx%d %s+0x%lx w=%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		       rect->x, rect->y, rect->w, rect->h, cl,
		       (unsigned long)off, (unsigned long)w[0],
		       (unsigned long)w[1], (unsigned long)w[2],
		       (unsigned long)w[3], (unsigned long)w[4],
		       (unsigned long)w[5], (unsigned long)w[6],
		       (unsigned long)w[7]);
	}
	gpu_load_image(rect, (const uint32_t *)p);
	return 0;
}

int StoreImage(RECT *rect, u_long *p)
{
	if (web_vsync_count >= 2400) {
		uintptr_t off;
		printf("gpu: StoreImage %d,%d %dx%d %s+0x%lx\n", rect->x,
		       rect->y, rect->w, rect->h, src_class(p, &off),
		       (unsigned long)off);
	}
	gpu_store_image(rect, (uint32_t *)p);
	return 0;
}

int MoveImage(RECT *rect, int x, int y)
{
	if (web_vsync_count >= 2400)
		printf("gpu: MoveImage %d,%d %dx%d -> %d,%d\n", rect->x,
		       rect->y, rect->w, rect->h, x, y);
	gpu_move_image(rect, x, y);
	return 0;
}

/* glabel LoadClut */
u_short LoadClut(u_long *clut, int x, int y)
{
	RECT r;
	r.x = x;
	r.y = y;
	r.w = 256;
	r.h = 1;
	LoadImage(&r, clut);
	return GetClut(x, y);
}

u_short LoadClut2(u_long *clut, int x, int y)
{
	RECT r;
	r.x = x;
	r.y = y;
	r.w = 16;
	r.h = 1;
	LoadImage(&r, clut);
	return GetClut(x, y);
}

/* glabel LoadTPage */
u_short LoadTPage(u_long *pix, int tp, int abr, int x, int y, int w, int h)
{
	RECT r;
	r.x = x;
	r.y = y;
	r.h = h;
	switch (tp) {
	case 0: r.w = (w + 3) >> 2; break;
	case 1: r.w = (w + 1) >> 1; break;
	default: r.w = w; break;
	}
	LoadImage(&r, pix);
	return GetTPage(tp, abr, x, y);
}

u_short GetTPage(int tp, int abr, int x, int y)
{
	return ((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) |
	       ((x & 0x3ff) >> 6) | ((y & 0x200) << 2);
}

u_short GetClut(int x, int y)
{
	return (y << 6) | ((x >> 4) & 0x3f);
}

u_short LoadTPage2(u_long *pix, int tp, int abr, int x, int y, int w, int h)
{
	return LoadTPage(pix, tp, abr, x, y, w, h);
}

/* ---- packet setup ----------------------------------------------------- */

void SetPolyF3(POLY_F3 *p)	{ setlen(p, 4);  setcode(p, 0x20); }
void SetPolyFT3(POLY_FT3 *p)	{ setlen(p, 7);  setcode(p, 0x24); }
void SetPolyG3(POLY_G3 *p)	{ setlen(p, 6);  setcode(p, 0x30); }
void SetPolyGT3(POLY_GT3 *p)	{ setlen(p, 9);  setcode(p, 0x34); }
void SetPolyF4(POLY_F4 *p)	{ setlen(p, 5);  setcode(p, 0x28); }
void SetPolyFT4(POLY_FT4 *p)	{ setlen(p, 9);  setcode(p, 0x2c); }
void SetPolyG4(POLY_G4 *p)	{ setlen(p, 8);  setcode(p, 0x38); }
void SetPolyGT4(POLY_GT4 *p)	{ setlen(p, 12); setcode(p, 0x3c); }

void SetSprt(SPRT *p)		{ setlen(p, 4);  setcode(p, 0x64); }
void SetSprt8(SPRT_8 *p)	{ setlen(p, 3);  setcode(p, 0x74); }
void SetSprt16(SPRT_16 *p)	{ setlen(p, 3);  setcode(p, 0x7c); }

void SetTile(TILE *p)		{ setlen(p, 3);  setcode(p, 0x60); }
void SetTile1(TILE_1 *p)	{ setlen(p, 2);  setcode(p, 0x68); }
void SetTile8(TILE_8 *p)	{ setlen(p, 2);  setcode(p, 0x70); }
void SetTile16(TILE_16 *p)	{ setlen(p, 2);  setcode(p, 0x78); }

void SetLineF2(LINE_F2 *p)	{ setlen(p, 3);  setcode(p, 0x40); }
void SetLineG2(LINE_G2 *p)	{ setlen(p, 4);  setcode(p, 0x50); }
void SetLineF3(LINE_F3 *p)	{ setlen(p, 5);  setcode(p, 0x48);
				  p->pad = 0x55555555; }
void SetLineG3(LINE_G3 *p)	{ setlen(p, 7);  setcode(p, 0x58);
				  p->pad = 0x55555555; }
void SetLineF4(LINE_F4 *p)	{ setlen(p, 6);  setcode(p, 0x4c);
				  p->pad = 0x55555555; }
void SetLineG4(LINE_G4 *p)	{ setlen(p, 9);  setcode(p, 0x5c);
				  p->pad = 0x55555555; }

/* glabel SetSemiTrans / SetShadeTex: bit1/bit0 of the code byte */
void SetSemiTrans(void *p, int abe)
{
	u_char *c = (u_char *)p + 7;
	*c = abe ? (*c | 0x2) : (*c & 0xfd);
}

void SetShadeTex(void *p, int tge)
{
	u_char *c = (u_char *)p + 7;
	*c = tge ? (*c & 0xfe) : (*c | 0x1);
}

/* ---- DR_* builders ---------------------------------------------------- */

void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw)
{
	uint32_t w;
	setlen(p, 2);
	p->code[0] = 0xE1000000 | (tpage & 0x9ff) |
		     (dtd ? 0x200 : 0) | (dfe ? 0x400 : 0);
	if (tw) {
		w = ((tw->w - 1) >> 3) & 0x1f;
		w |= (((tw->h - 1) >> 3) & 0x1f) << 5;
		w |= ((tw->x >> 3) & 0x1f) << 10;
		w |= ((tw->y >> 3) & 0x1f) << 15;
	} else {
		w = 0;
	}
	p->code[1] = 0xE2000000 | w;
}

void SetDrawArea(DR_AREA *p, RECT *r)
{
	setlen(p, 2);
	p->code[0] = 0xE3000000 | (r->x & 0x3ff) | ((r->y & 0x1ff) << 10);
	p->code[1] = 0xE4000000 | ((r->x + r->w - 1) & 0x3ff) |
		     (((r->y + r->h - 1) & 0x1ff) << 10);
}

void SetDrawOffset(DR_OFFSET *p, u_short *ofs)
{
	setlen(p, 2);
	p->code[0] = 0xE5000000 | (ofs[0] & 0x7ff) | ((ofs[1] & 0x7ff) << 11);
	p->code[1] = 0;
}

void SetDrawStp(DR_STP *p, int pb)
{
	setlen(p, 2);
	p->code[0] = 0xE6000000 | (pb & 3);
	p->code[1] = 0;
}

void SetTexWindow(DR_TWIN *p, RECT *tw)
{
	uint32_t w;
	setlen(p, 2);
	w = ((tw->w - 1) >> 3) & 0x1f;
	w |= (((tw->h - 1) >> 3) & 0x1f) << 5;
	w |= ((tw->x >> 3) & 0x1f) << 10;
	w |= ((tw->y >> 3) & 0x1f) << 15;
	p->code[0] = 0xE2000000 | w;
	p->code[1] = 0;
}

void SetDrawMove(DR_MOVE *p, RECT *rect, int x, int y)
{
	setlen(p, 5);
	p->code[0] = 0x80000000;
	p->code[1] = 0;
	p->code[2] = (rect->x & 0xffff) | (rect->y << 16);
	p->code[3] = (x & 0xffff) | (y << 16);
	p->code[4] = (rect->w & 0xffff) | (rect->h << 16);
}

/* glabel SetDrawLoad */
void SetDrawLoad(DR_LOAD *p, RECT *rect)
{
	int npix = rect->w * rect->h;
	int n = (npix + 1) / 2;
	int len = n + 4;
	uint32_t *w;
	if (n >= 0xD) { /* sanity from asm: >=0xD -> len 0 malformed? keep */
	}
	setlen(p, len);
	p->code[0] = 0xA0000000;
	p->code[1] = (rect->x & 0xffff) | (rect->y << 16);
	p->code[2] = (rect->w & 0xffff) | (rect->h << 16);
	w = (uint32_t *)p + 3 + n;
	*w = 0x01000000; /* terminating nop word per asm */
}

/* SetDrawEnv builds the packed env inside DRAWENV.dr_env per
 * SetDrawEnv2 in the asm: DR_MODE, texture window, clip top-left,
 * clip bottom-right, offset, STP. */
void SetDrawEnv(DR_ENV *dr_env, DRAWENV *env)
{
	uint32_t *w = (uint32_t *)dr_env->code;
	uint32_t tw = 0;

	dr_env->tag = 0;
	setlen(dr_env, 15);

	w[0] = 0xE1000000 | (env->tpage & 0x1ff) |
	       (env->dtd ? 0x200 : 0) | (env->dfe ? 0x400 : 0);
	tw = env->tw.w ? (((env->tw.w - 1) >> 3) & 0x1f) |
	     (((env->tw.h - 1) >> 3) & 0x1f) << 5 |
	     ((env->tw.x >> 3) & 0x1f) << 10 |
	     ((env->tw.y >> 3) & 0x1f) << 15 : 0;
	w[1] = 0xE2000000 | tw;
	w[2] = 0xE3000000 | (env->clip.x & 0x3ff) |
	       ((env->clip.y & 0x1ff) << 10);
	w[3] = 0xE4000000 | ((env->clip.x + env->clip.w - 1) & 0x3ff) |
	       (((env->clip.y + env->clip.h - 1) & 0x1ff) << 10);
	w[4] = 0xE5000000 | (env->ofs[0] & 0x7ff) |
	       ((env->ofs[1] & 0x7ff) << 11);
	w[5] = 0xE6000000;
	w[6] = 0;
	if (env->isbg) {
		w[7] = 0x02000000 | env->r0 | (env->g0 << 8) | (env->b0 << 16);
		w[8] = (env->clip.x & 0xffff) | (env->clip.y << 16);
		w[9] = (env->clip.w & 0xffff) | (env->clip.h << 16);
		setlen(dr_env, 10);
	}
}

/* ---- TIM -------------------------------------------------------------- */

/* glabel OpenTIM / ReadTIM + get_tim_addr (libgpu.s):
 * OpenTIM stores the TIM pointer in a static; ReadTIM parses it and
 * advances tmd_tim past the TIM (block bnum is a byte count; the CLUT
 * block is skipped by (bnum & ~3) bytes exactly as the asm does). */
static u_long *tmd_tim;

int OpenTIM(u_long *addr)
{
	tmd_tim = addr;
	return 0;
}

TIM_IMAGE *ReadTIM(TIM_IMAGE *timimg)
{
	u_long *p = tmd_tim;
	int a0 = 0;

	if (*p != 0x10)
		return 0;
	p++;
	timimg->mode = *p++;
	if (timimg->mode & 8) {
		timimg->crect = (RECT *)(p + 1);
		timimg->caddr = p + 3;
		a0 = *p >> 2;	/* clut block size in words */
		p = (u_long *)((char *)p + (a0 << 2));
	} else {
		timimg->crect = 0;
		timimg->caddr = 0;
	}
	timimg->prect = (RECT *)(p + 1);
	timimg->paddr = p + 3;
	tmd_tim += a0 + (*p >> 2) + 2;
	return timimg;
}

/* ---- debug dumps ------------------------------------------------------- */

void DumpTPage(u_short tpage)
{
	printf("tpage=%04x tp=%d abr=%d x=%d y=%d\n", tpage, (tpage >> 7) & 3,
	       (tpage >> 5) & 3, (tpage & 0xf) * 64,
	       ((tpage >> 4) & 1) * 256 + ((tpage >> 15) & 1) * 512);
}

void DumpClut(u_short clut)
{
	printf("clut=%04x x=%d y=%d\n", clut, (clut & 0x3f) * 16, clut >> 6);
}

void DumpDispEnv(DISPENV *env)
{
	printf("disp=(%d,%d,%d,%d) screen=(%d,%d) rgb24=%d inter=%d\n",
	       env->disp.x, env->disp.y, env->disp.w, env->disp.h,
	       env->screen.x, env->screen.y, env->isrgb24, env->isinter);
}

void DumpDrawEnv(DRAWENV *env)
{
	printf("drawenv clip=(%d,%d,%d,%d) ofs=(%d,%d) tpage=%04x dtd=%d\n",
	       env->clip.x, env->clip.y, env->clip.w, env->clip.h,
	       env->ofs[0], env->ofs[1], env->tpage, env->dtd);
}

void DumpOTag(u_long *p)
{
	printf("otag=%08lx\n", *p);
}

void DumpDrawEnv2(void) { DumpDrawEnv(&cur_drawenv); }
