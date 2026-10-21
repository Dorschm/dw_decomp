/*
 * libgs for the web build. The 2D half is decompiled from the game's own
 * disassembly (asm/us/main/psyq/libgs.s) — each function notes its glabel.
 * The 3D/TMD half remains stubbed for a later segment.
 *
 * OT/packet links are 24-bit linear addresses (<16 MiB); reconstruction is
 * ptr = addr with no high byte.
 */

/* NB: <stdio.h>/<string.h> resolve to the PsyQ headers here; memset is
 * avoided via the zmem helper below. */
#include <libgs.h>
#include <stdint.h>
#include <emscripten/console.h>

#include <web/host/log.h>
#include <web/psx/gte.h>

MATRIX GsWSMATRIX;
MATRIX GsIDMATRIX = { { {0x1000,0,0}, {0,0x1000,0}, {0,0,0x1000} }, {0,0,0} };
MATRIX GsLIGHTWSMATRIX;
MATRIX GsIDMATRIX2;
MATRIX _LC;
_GsFCALL GsFCALL4;

/* internal libgs state (bss symbols in the asm; the non-static ones are
 * the exported globals declared in libgs.h) */
short PSDIDX;			/* active buffer index 0/1 */
static short PSDGPU;		/* interlace flag (intmode & 4) */
u_long PSDCNT;
static short PSDOFSX[2], PSDOFSY[2];
short PSDBASEX[2], PSDBASEY[2];
_GsPOSITION POSITION;
static short GsORGOFSX, GsORGOFSY;
DRAWENV GsDRAWENV;
DISPENV GsDISPENV;
RECT CLIP2;
static TILE Clear[2];
long HWD0, VWD0;
long gs_drawot_calls, gs_swapbuf_calls, gsort_obj_calls;
long tmd_prim_counts[32];
extern uint8_t PSX_RAM[];
static int32_t GsCLIP3near, GsCLIP3far;
int GsLIGHT_MODE;
static int32_t GsHND;		/* (VWD0<<14)/HWD0 from valiable_init */
PACKET *GsOUT_PACKET_P;

static void zmem(void *p, unsigned int n)
{
	uint8_t *c = p;
	while (n--)
		*c++ = 0;
}

extern int GetVideoMode(void);

#define PK24(p) ((uint32_t)(uintptr_t)(p) & 0xFFFFFF)
#define OT_END 0x00FFFFFF

/* glabel _make_packet
 * a0=packet, a1=GsOT, a2=ot index, a3=len. Returns next free packet. */
static PACKET *_make_packet(PACKET *p, GsOT *ot, unsigned short idx,
			    int len)
{
	int s1 = (idx & 0xffff) - (int)ot->offset;
	uint32_t *entry;
	if (s1 < 0)
		emscripten_console_log("_make_packet: index underflow");
	entry = (uint32_t *)ot->org + s1;
	*(uint32_t *)p = *entry;
	((uint8_t *)p)[3] = (uint8_t)len;
	*entry = PK24(p);
	return (PACKET *)((uint8_t *)p + (len & 0xff) * 4 + 4);
}

/* glabel GsSetWorkBase */
void GsSetWorkBase(PACKET *outpacketp)
{
	GsOUT_PACKET_P = outpacketp;
}

/* glabel GsGetWorkBase */
PACKET *GsGetWorkBase(void)
{
	return GsOUT_PACKET_P;
}

/* glabel GsSortBoxFill */
void GsSortBoxFill(GsBOXF *bp, GsOT *ot, unsigned short pri)
{
	uint32_t *p;
	uint32_t attr;
	if ((long)bp->attribute < 0)
		return;
	p = (uint32_t *)GsOUT_PACKET_P;
	attr = bp->attribute;
	p[1] = 0xE1000200 | ((attr >> 17) & 0x180) | ((attr >> 23) & 0x60);
	p[2] = 0x60000000 | (((attr >> 29) & 2) << 24) | bp->r |
	       (bp->g << 8) | (bp->b << 16);
	p[3] = ((bp->x + GsORGOFSX) & 0xffff) |
	       ((bp->y + GsORGOFSY) << 16);
	p[4] = (bp->w & 0xffff) | (bp->h << 16);
	GsOUT_PACKET_P = _make_packet(GsOUT_PACKET_P, ot, pri, 4);
}

/* glabel GsSortPoly — copies a prim packet through, adding GsORGOFS to
 * each vertex; code-byte bit2 = uv word present, bit4 = rgb word present,
 * bit3 = quad. Copy order per the asm: rgb/code, xy0, then per vertex
 * v1..v(n-1): [uv word] [rgb word] xy — plus a trailing uv word for quads. */
void GsSortPoly(void *pp, GsOT *ot, unsigned short pri)
{
	uint8_t *s = pp;
	uint8_t code = s[7];
	uint32_t *dst = (uint32_t *)GsOUT_PACKET_P + 1;
	uint8_t *src = s + 4;
	int nv = (code & 8) ? 4 : 3;
	int len;
	int v;

	*dst++ = *(uint32_t *)src;			/* rgb | code */
	src += 4;
	((int16_t *)dst)[0] = *(int16_t *)src + GsORGOFSX;
	((int16_t *)dst)[1] = ((int16_t *)src)[1] + GsORGOFSY;
	dst++;
	src += 4;
	for (v = 1; v < nv; v++) {
		if (code & 4) {
			*dst++ = *(uint32_t *)src;
			src += 4;
		}
		if (code & 0x10) {
			*dst++ = *(uint32_t *)src;
			src += 4;
		}
		((int16_t *)dst)[0] = *(int16_t *)src + GsORGOFSX;
		((int16_t *)dst)[1] = ((int16_t *)src)[1] + GsORGOFSY;
		dst++;
		src += 4;
	}
	if (nv == 4 && (code & 4)) {
		*dst++ = *(uint32_t *)src;
		src += 4;
	}
	len = 1 + nv + ((code & 4) ? nv : 0) + ((code & 0x10) ? nv - 1 : 0);
	GsOUT_PACKET_P = _make_packet(GsOUT_PACKET_P, ot, pri, len);
}

/* sprite packet helpers (shared by GsSortSprite/GsSortFastSprite) */
static inline uint32_t sprite_uv_clut(const GsSPRITE *sp, int u, int v)
{
	return (u & 0xff) | ((v & 0xff) << 8) |
	       (((uint32_t)sp->cx << 12) & 0x3F0000) |
	       ((uint32_t)sp->cy << 22);
}

static inline uint32_t sprite_tpage_word(uint32_t attr, uint16_t tpage)
{
	return 0xE1000200 | ((attr >> 17) & 0x180) | ((attr >> 23) & 0x60) |
	       (tpage & 0x1f);
}

/* glabel GsSortSprite */
void GsSortSprite(GsSPRITE *sp, GsOT *ot, unsigned short pri)
{
	uint32_t attr = sp->attribute;
	uint32_t *p;
	int simple;

	if ((long)attr < 0 || sp->w == 0 || sp->h == 0)
		return;
	/* asm: bit27 of attribute OR (scale==0x1000,0x1000 && rotate==0 &&
	 * (attr & 0xC00000)==0) selects the no-GTE path */
	simple = (attr >> 27) & 1;
	if (sp->scalex == 0x1000 && sp->scaley == 0x1000 &&
	    sp->rotate == 0 && (attr & 0xC00000) == 0)
		simple = 1;

	p = (uint32_t *)GsOUT_PACKET_P;
	if (simple) {
		p[1] = sprite_tpage_word(attr, sp->tpage);
		p[2] = 0x64000000 | ((attr >> 5) & 0x2000000) |
		       ((attr << 18) & 0x1000000) | sp->r | (sp->g << 8) |
		       (sp->b << 16);
		p[3] = ((sp->x + GsORGOFSX - sp->mx) & 0xffff) |
		       (((sp->y + GsORGOFSY - sp->my) & 0xffff) << 16);
		p[4] = sprite_uv_clut(sp, sp->u, sp->v);
		p[5] = (sp->w & 0xffff) | ((uint32_t)sp->h << 16);
		GsOUT_PACKET_P = _make_packet(GsOUT_PACKET_P, ot, pri, 5);
		return;
	}

	/* GTE path in the asm: RotMatrix(rot) * ScaleMatrix(scalex,scaley),
	 * translation (x,y,0), RotTransPers4 over the four corners relative
	 * to (mx,my). Reproduced here in plain C (Segment 4: verify against
	 * the real GTE implementation).
	 * asm rotate conversion: rot = (((rot * 0xB60B60B7) >> 32) + rot)
	 * >> 8) - sign */
	{
		int64_t prod = (int64_t)sp->rotate * (int32_t)0xB60B60B7;
		int ang = (int)(((int32_t)(prod >> 32) + sp->rotate) >> 8) -
			  (sp->rotate >> 31);
		int c = (int)rcos(ang), s = (int)rsin(ang);
		int sx = sp->scalex, sy = sp->scaley;
		int vx[4] = {-sp->mx, sp->w - sp->mx, -sp->mx,
			     sp->w - sp->mx};
		int vy[4] = {-sp->my, -sp->my, sp->h - sp->my,
			     sp->h - sp->my};
		int sxo[4], syo[4];
		int k;
		int fx, fy;
		for (k = 0; k < 4; k++) {
			fx = (vx[k] * sx) >> 12;
			fy = (vy[k] * sy) >> 12;
			sxo[k] = ((fx * c - fy * s) >> 12) + sp->x + GsORGOFSX;
			syo[k] = ((fx * s + fy * c) >> 12) + sp->y + GsORGOFSY;
		}
		p[1] = 0x2C000000 | ((attr >> 5) & 0x2000000) |
		       ((attr << 18) & 0x1000000) | sp->r | (sp->g << 8) |
		       (sp->b << 16);
		p[2] = (sxo[0] & 0xffff) | ((syo[0] & 0xffff) << 16);
		p[3] = sprite_uv_clut(sp, sp->u, sp->v);
		p[4] = (sxo[1] & 0xffff) | ((syo[1] & 0xffff) << 16);
		p[5] = ((sp->u + sp->w - 1) & 0xff) | (((sp->v) & 0xff) << 8) |
		       ((uint32_t)(sprite_tpage_word(attr, sp->tpage) &
				   0xffff) << 16);
		p[6] = (sxo[2] & 0xffff) | ((syo[2] & 0xffff) << 16);
		p[7] = ((sp->u) & 0xff) |
		       (((sp->v + sp->h - 1) & 0xff) << 8);
		p[8] = (sxo[3] & 0xffff) | ((syo[3] & 0xffff) << 16);
		p[9] = ((sp->u + sp->w - 1) & 0xff) |
		       (((sp->v + sp->h - 1) & 0xff) << 8);
		GsOUT_PACKET_P = _make_packet(GsOUT_PACKET_P, ot, pri, 9);
	}
}

/* glabel GsSortFastSprite */
void GsSortFastSprite(GsSPRITE *sp, GsOT *ot, unsigned short pri)
{
	uint32_t attr;
	uint32_t *p, *entry;
	if ((long)sp->attribute < 0 || sp->w == 0 || sp->h == 0)
		return;
	attr = sp->attribute;
	p = (uint32_t *)GsOUT_PACKET_P;
	p[1] = sprite_tpage_word(attr, sp->tpage);
	p[2] = 0x64000000 | ((attr >> 5) & 0x2000000) |
	       ((attr << 18) & 0x1000000) | sp->r | (sp->g << 8) |
	       (sp->b << 16);
	p[3] = ((sp->x + GsORGOFSX) & 0xffff) |
	       (((sp->y + GsORGOFSY) & 0xffff) << 16);
	p[4] = sprite_uv_clut(sp, sp->u, sp->v);
	p[5] = (sp->w & 0xffff) | ((uint32_t)sp->h << 16);
	/* inline link: entry = ot->org + (pri - ot->offset) */
	entry = (uint32_t *)ot->org + ((pri & 0xffff) - (int)ot->offset);
	p[0] = *entry + 0x05000000;
	*entry = PK24(p);
	GsOUT_PACKET_P = (PACKET *)(p + 6);
}

/* glabel GsSortClear */
void GsSortClear(unsigned char r, unsigned char g, unsigned char b, GsOT *ot)
{
	TILE *t = &Clear[PSDIDX];
	t->r0 = r;
	t->g0 = g;
	t->b0 = b;
	t->x0 = PSDOFSX[PSDIDX];
	t->y0 = PSDOFSY[PSDIDX];
	t->h = (short)VWD0;
	if (GsDISPENV.isrgb24)
		t->w = (short)(HWD0 * 3 / 2);
	else
		t->w = (short)HWD0;
	AddPrim(ot->tag, t);
}

/* glabel GsGetActiveBuff */
int GsGetActiveBuff(void)
{
	return PSDIDX;
}

/* glabel GsSetDrawBuffOffset (internal, called by swap/init) */
void GsSetDrawBuffOffset(void)
{
	if (PSDGPU) {
		GsORGOFSY = 0;
		GsORGOFSX = 0;
		GsDRAWENV.ofs[0] = POSITION.offx + PSDOFSX[PSDIDX];
		GsDRAWENV.ofs[1] = POSITION.offy + PSDOFSY[PSDIDX];
		PutDrawEnv(&GsDRAWENV);
	} else {
		int ox = POSITION.offx + PSDOFSX[PSDIDX ? 0 : 1];
		int oy = POSITION.offy + PSDOFSY[PSDIDX ? 0 : 1];
		SetGeomOffset(ox, oy);
		GsORGOFSX = ox;
		GsORGOFSY = oy;
	}
}

/* glabel GsSetDrawBuffClip (internal) */
void GsSetDrawBuffClip(void)
{
	GsDRAWENV.clip.x = CLIP2.x + PSDOFSX[PSDIDX];
	GsDRAWENV.clip.y = CLIP2.y + PSDOFSY[PSDIDX];
	GsDRAWENV.clip.w = CLIP2.w;
	GsDRAWENV.clip.h = CLIP2.h;
	PutDrawEnv(&GsDRAWENV);
}

/* glabel GsSwapDispBuff */
void GsSwapDispBuff(void)
{
	gs_swapbuf_calls++;
	GsDISPENV.disp.x = PSDOFSX[PSDIDX];
	GsDISPENV.disp.y = PSDOFSY[PSDIDX];
	PutDispEnv(&GsDISPENV);
	SetDispMask(1);
	PSDCNT++;
	if (PSDCNT == 0)
		PSDCNT = 1;
	PSDIDX = PSDIDX < 1 ? 1 : 0;
	GsSetDrawBuffClip();
	GsSetDrawBuffOffset();
}

/* glabel GsSetOrign */
void GsSetOrign(long x, long y)
{
	POSITION.offx = x;
	POSITION.offy = y;
}

/* glabel GsDefDispBuff */
void GsDefDispBuff(unsigned short x0, unsigned short y0,
		   unsigned short x1, unsigned short y1)
{
	PSDOFSX[0] = x0;
	PSDOFSX[1] = x1;
	PSDOFSY[0] = y0;
	PSDOFSY[1] = y1;
	if (PSDGPU) {
		PSDBASEX[0] = PSDBASEX[1] = 0;
		PSDBASEY[0] = PSDBASEY[1] = 0;
	} else {
		PSDBASEX[0] = x0;
		PSDBASEX[1] = x1;
		PSDBASEY[0] = y0;
		PSDBASEY[1] = y1;
	}
	GsSetDrawBuffClip();
	GsSetDrawBuffOffset();
}

/* glabel GsInit3D */
void GsInit3D(void)
{
	POSITION.offx = HWD0 / 2;
	POSITION.offy = VWD0 / 2;
	GsSetDrawBuffOffset();
	GsCLIP3near = 0xA;
	GsLIGHT_MODE = 0;
	GsCLIP3far = 0x3FFF;
}

/* glabel valiable_init */
static void valiable_init(unsigned short w, unsigned short h)
{
	HWD0 = w;
	VWD0 = h;
	GsIDMATRIX.m[0][0] = 0x1000; GsIDMATRIX.m[0][1] = 0;
	GsIDMATRIX.m[0][2] = 0; GsIDMATRIX.m[1][0] = 0;
	GsIDMATRIX.m[1][1] = 0x1000; GsIDMATRIX.m[1][2] = 0;
	GsIDMATRIX.m[2][0] = 0; GsIDMATRIX.m[2][1] = 0;
	GsIDMATRIX.m[2][2] = 0x1000;
	GsIDMATRIX.t[0] = GsIDMATRIX.t[1] = GsIDMATRIX.t[2] = 0;
	GsIDMATRIX2 = GsIDMATRIX;
	GsLIGHTWSMATRIX = GsIDMATRIX;
	_LC = GsIDMATRIX;
	GsHND = (int32_t)((int64_t)(VWD0 << 14) / HWD0);
	PSDBASEX[0] = PSDBASEX[1] = 0;
	PSDBASEY[0] = PSDBASEY[1] = 0;
	POSITION.offx = POSITION.offy = 0;
	CLIP2.x = CLIP2.y = 0;
	setlen(&Clear[0], 3); setcode(&Clear[0], 2);
	setlen(&Clear[1], 3); setcode(&Clear[1], 2);
	CLIP2.w = HWD0;
	CLIP2.h = VWD0;
	PSDCNT = 1;
}

/* glabel gpu_init */
static void gpu_init(unsigned short w, unsigned short h,
		     unsigned short intmode, unsigned short dith,
		     unsigned short vrammode)
{
	ResetGraph(((intmode >> 4) & 3) == 3 ? 3 : 0);
	zmem(&GsDRAWENV, sizeof(GsDRAWENV));
	GsDRAWENV.dtd = dith;
	PutDrawEnv(&GsDRAWENV);
	zmem(&GsDISPENV, sizeof(GsDISPENV));
	GsDISPENV.disp.w = w;
	GsDISPENV.disp.h = h;
	if (GetVideoMode() == 1) {
		GsDISPENV.screen.y = 0x18;
		GsDISPENV.pad0 = 1;
	}
	GsDISPENV.isinter = intmode & 1;
	PSDGPU = intmode & 4;
	GsDISPENV.isrgb24 = vrammode;
	PutDispEnv(&GsDISPENV);
}

/* glabel gte_init */
static void gte_init(void)
{
	InitGeom();
	SetFarColor(0, 0, 0);
	SetGeomOffset(0, 0);
	GsORGOFSX = 0;
	GsORGOFSY = 0;
}

/* glabel GsInitGraph */
void GsInitGraph(unsigned short x, unsigned short y, unsigned short intmode,
		 unsigned short dith, unsigned short vrammode)
{
	gpu_init(x, y, intmode, dith, vrammode);
	gte_init();
	PSDIDX = 0;
	valiable_init(x, y);
	GsSetDrawBuffClip();
	GsSetDrawBuffOffset();
}

/* glabel GsClearOt */
void GsClearOt(unsigned short offset, unsigned short point, GsOT *otp)
{
	otp->offset = offset;
	otp->point = point;
	otp->tag = (GsOT_TAG *)(otp->org + (1 << otp->length) - 1);
	ClearOTagR((u_long *)otp->org, 1 << otp->length);
}

/* glabel GsDrawOt */
void GsDrawOt(GsOT *ot)
{
	gs_drawot_calls++;
	DrawOTag((u_long *)ot->tag);
}

/* glabel GsSortOt */
GsOT *GsSortOt(GsOT *ot_src, GsOT *ot_dest)
{
	uint32_t *cur = (uint32_t *)ot_src->org;
	uint32_t *bucket;
	uint32_t *t0 = cur;
	int idx = ot_src->point - (int)ot_dest->offset;
	int guard = 0;
	while ((*t0 & OT_END) != OT_END) {
		t0 = (uint32_t *)(uintptr_t)(*t0 & OT_END);
		if (++guard > 0x100000)
			return ot_dest;
	}
	bucket = (uint32_t *)ot_dest->org + idx;
	*t0 = (*t0 & 0xFF000000) | (*bucket & OT_END);
	*bucket = (*bucket & 0xFF000000) | PK24(ot_src->tag);
	return ot_dest;
}

/* glabel GsGetTimInfo */
extern void *web_psx_ptr(const void *p, const char *who);

extern unsigned long emscripten_get_heap_size(void);
extern unsigned char PSX_RAM[];

void GsGetTimInfo(unsigned long *im, GsIMAGE *tim)
{
	uint8_t *p = (uint8_t *)web_psx_ptr(im, "GsGetTimInfo");
	uint8_t *q;

	{
		/* a TIM header whose block size points outside memory means the
		 * buffer was not (fully) loaded: report it instead of trapping */
		uint32_t *w = (uint32_t *)p;
		uint32_t blk = (w[0] & 8) ? w[1] : 0;
		if (blk > 0x200000 || (uintptr_t)p + 64 + blk >
					      (uintptr_t)emscripten_get_heap_size()) {
			emscripten_console_logf(
				"GsGetTimInfo: BAD TIM at %p (PSX_RAM+%x) words "
				"%08x %08x %08x %08x",
				(void *)p, (unsigned)((uint8_t *)p - PSX_RAM),
				(unsigned)w[0], (unsigned)w[1], (unsigned)w[2],
				(unsigned)w[3]);
			tim->pmode = 0;
			tim->px = tim->py = tim->pw = tim->ph = 0;
			tim->cx = tim->cy = tim->cw = tim->ch = 0;
			tim->pixel = NULL;
			tim->clut = NULL;
			return;
		}
	}
	tim->pmode = *(uint32_t *)p;
	if ((tim->pmode >> 3) & 1) {
		/* CLUT present */
		p += 4;
		q = p + ((*(uint32_t *)p >> 2) << 2); /* skip clut block */
		tim->cx = *(uint16_t *)(p + 4);
		tim->cy = *(uint16_t *)(p + 6);
		tim->cw = *(uint16_t *)(p + 8);
		tim->ch = *(uint16_t *)(p + 10);
		tim->clut = (unsigned long *)(p + 12);
		p = q + 4;
		tim->px = *(uint16_t *)p;
		tim->py = *(uint16_t *)(p + 2);
		tim->pw = *(uint16_t *)(p + 4);
		tim->ph = *(uint16_t *)(p + 6);
		tim->pixel = (unsigned long *)(p + 8);
	} else {
		p += 8;
		tim->px = *(uint16_t *)p;
		tim->py = *(uint16_t *)(p + 2);
		tim->pw = *(uint16_t *)(p + 4);
		tim->ph = *(uint16_t *)(p + 6);
		tim->pixel = (unsigned long *)(p + 8);
	}
}

/* ---- 3D / TMD half (decompiled from libgs.s) ---------------------------
 * InitGeom/SetFarColor/SetGeomOffset and the whole GsTMDfast/GsTMDdiv
 * handler family live in src/web/psx/libgte.c.
 */

MATRIX GsWSMATRIX_ORG;
u_long GsMATE_C, GsLMODE, GsLIGNR, GsLIOFF, GsZOVER, GsBACKC, GsNDIV;
u_long GsTRATE, GsTON, GsDISPON;

static GsCOORDINATE2 *DSTACK[0x64];

/* glabel GsInitCoordinate2 */
void GsInitCoordinate2(GsCOORDINATE2 *super, GsCOORDINATE2 *base)
{
	base->coord = GsIDMATRIX;
	base->flg = 0;
	base->super = super;
	if ((u_long)super >= 2)
		super->sub = base;
}

/* glabel GsSetLsMatrix */
void GsSetLsMatrix(MATRIX *mp)
{
	SetRotMatrix(mp);
	SetTransMatrix(mp);
}

/* glabel GsSetLightMatrix */
void GsSetLightMatrix(MATRIX *mp)
{
	MATRIX m = GsLIGHTWSMATRIX;
	PushMatrix();
	MulMatrix(&m, mp);
	PopMatrix();
	SetLightMatrix(&m);
}

/* glabel GsMulCoord0: m3 = m1 * m2 (m3.t = m1.rot*m2.t + m1.t) */
void GsMulCoord0(MATRIX *m1, MATRIX *m2, MATRIX *m3)
{
	VECTOR t;
	ApplyMatrixLV(m1, (VECTOR *)m2->t, &t);
	MulMatrix0(m1, m2, m3);
	m3->t[0] = t.vx + m1->t[0];
	m3->t[1] = t.vy + m1->t[1];
	m3->t[2] = t.vz + m1->t[2];
}

/* glabel GsMulCoord2: m2 = m1 * m2 */
void GsMulCoord2(MATRIX *m1, MATRIX *m2)
{
	VECTOR t;
	ApplyMatrixLV(m1, (VECTOR *)m2->t, &t);
	MulMatrix2(m1, m2);
	m2->t[0] = t.vx + m1->t[0];
	m2->t[1] = t.vy + m1->t[1];
	m2->t[2] = t.vz + m1->t[2];
}

/* glabel GsMulCoord3: m1 = m1 * m2 */
void GsMulCoord3(MATRIX *m1, MATRIX *m2)
{
	VECTOR t;
	ApplyMatrixLV(m1, (VECTOR *)m2->t, &t);
	MulMatrix(m1, m2);
	m1->t[0] = t.vx + m1->t[0];
	m1->t[1] = t.vy + m1->t[1];
	m1->t[2] = t.vz + m1->t[2];
}

/* glabel GsSetNearClip */
void GsSetNearClip(long clip)
{
	GsCLIP3near = clip;
}

/* glabel GsMapModelingData: relocate object tables in place (bit0 of
 * p[0] = already-mapped flag), then set it. */
void GsMapModelingData(unsigned long *p)
{
	unsigned long *base;
	unsigned long n, i;
	struct TMD_STRUCT *o;
	p = web_psx_ptr(p, "GsMapModelingData");
	{
		if ((p[0] & 1) == 0 && p[1] > 4096) {
			emscripten_console_logf("GsMapModelingData: BAD TMD (n=%u), skipped",
						(unsigned)p[1]);
			return;
		}
	}
	if (p[0] & 1)
		return;
	p[0] |= 1;
	n = p[1];
	base = p + 2;
	o = (struct TMD_STRUCT *)base;
	for (i = 0; i < n; i++, o++) {
		o->vertop = (u_long *)((u_long)o->vertop + (u_long)base);
		o->nortop = (u_long *)((u_long)o->nortop + (u_long)base);
		o->primtop = (u_long *)((u_long)o->primtop + (u_long)base);
	}
}

/* glabel GsSetProjection */
void GsSetProjection(long h)
{
	SetGeomScreen(h);
}

/* glabel gte_set_lc: stash m into _LC and load it as the color matrix */
static void gte_set_lc(MATRIX *m)
{
	_LC = *m;
	SetColorMatrix(&_LC);
}

/* glabel gte_read_lc */
static void gte_read_lc(MATRIX *m)
{
	*m = _LC;
}

/* glabel GsSetFlatLight: LLM row id = -dir/len (12.12); LCM column id =
 * rgb * 0x8080.. fixed magic /128 -> c*4096/255. Returns -1 on |dir|==0. */
int GsSetFlatLight(int id, GsF_LIGHT *lt)
{
	MATRIX lm, lc;
	long len;
	int32_t cr, cg, cb;
	lm = GsLIGHTWSMATRIX;
	gte_read_lc(&lc);
	len = SquareRoot0(lt->vx * lt->vx + lt->vy * lt->vy +
			  lt->vz * lt->vz);
	if (len == 0)
		return -1;
	lm.m[id][0] = (short)((-(long)lt->vx << 12) / len);
	lm.m[id][1] = (short)((-(long)lt->vy << 12) / len);
	lm.m[id][2] = (short)((-(long)lt->vz << 12) / len);
	cr = lt->r << 12; cg = lt->g << 12; cb = lt->b << 12;
	lc.m[0][id] = (short)((((int64_t)cr * 0x80808081u >> 32) + cr) >> 7);
	lc.m[1][id] = (short)((((int64_t)cg * 0x80808081u >> 32) + cg) >> 7);
	lc.m[2][id] = (short)((((int64_t)cb * 0x80808081u >> 32) + cb) >> 7);
	GsLIGHTWSMATRIX = lm;
	gte_set_lc(&lc);
	return 0;
}

/* glabel GsSetLightMode */
void GsSetLightMode(int mode)
{
	switch (mode) {
	case 0: GsLIGHT_MODE = 0; break;
	case 1: case 2: case 3:
		GsLIGHT_MODE = mode;
		break;
	default:
		emscripten_console_log("libgs: bad GsSetLightMode");
		break;
	}
}

/* glabel GsSetAmbient */
void GsSetAmbient(long r, long g, long b)
{
	SetBackColor(r >> 4, g >> 4, b >> 4);
}

/* glabel gte_rotate_z_matrix: a0 = matrix to post-multiply by a Z
 * rotation of angle a1 (degrees * 4096/360 via the 0xB60B60B7 magic). */
static void gte_rotate_z_matrix(MATRIX *m, long rot)
{
	MATRIX rz;
	long ang;
	long c, s;
	ang = (long)(((((int64_t)rot * (int32_t)0xB60B60B7) >> 32) + rot) >> 8)
	      - (rot >> 31);
	c = rcos(ang);
	s = rsin(ang);
	if (rot == 0)
		return;
	rz = GsIDMATRIX;
	rz.m[0][0] = (short)c;
	rz.m[0][1] = (short)-s;
	rz.m[1][0] = (short)s;
	rz.m[1][1] = (short)c;
	MulMatrix(m, &rz);
}

/* glabel select_max_param: max |component| over the 6 longs */
static long select_max_param(const long *v)
{
	long m = 0;
	int i;
	for (i = 0; i < 6; i++) {
		long x = v[i];
		if (x < 0)
			x = -x;
		if (m < x)
			m = x;
	}
	return m;
}

/* glabel len_param: bit length (shifts until <= 0) */
static int len_param(long a)
{
	int n = 0;
	while (a > 0) {
		a >>= 1;
		n++;
	}
	return n;
}

/* glabel scale_view_param: copy the 6-long view param, right-shifting by
 * (bits(max)-15) if the max is large. */
static void scale_view_param(const long *src, long *dst)
{
	int sh, i;
	sh = len_param(select_max_param(src)) - 0xF;
	if (sh > 0) {
		for (i = 0; i < 6; i++)
			dst[i] = src[i] >> sh;
	} else {
		for (i = 0; i < 6; i++)
			dst[i] = src[i];
	}
}

/* glabel Gssub_make_matrix: identity matrix with a 2x2 rotation inserted.
 * axis a3: 0x78 -> X rot {m02=c,m12=-s,m21=s,m22=c}; 0x79 -> Y rot;
 * 0x7A -> Z rot {m00=c,m01=-s,m10=s,m11=c}. */
static void Gssub_make_matrix(MATRIX *m, long s, long c, int axis)
{
	*m = GsIDMATRIX;
	switch (axis) {
	case 0x78:
		m->m[1][1] = (short)c;
		m->m[2][2] = (short)c;
		m->m[1][2] = (short)-s;
		m->m[2][1] = (short)s;
		break;
	case 0x79:
		m->m[0][0] = (short)c;
		m->m[2][2] = (short)c;
		m->m[0][2] = (short)s;
		m->m[2][0] = (short)-s;
		break;
	case 0x7A:
		m->m[0][0] = (short)c;
		m->m[1][1] = (short)c;
		m->m[0][1] = (short)-s;
		m->m[1][0] = (short)s;
		break;
	default:
		break;
	}
}

/* glabel GsSetRefView2 */
int GsSetRefView2(GsRVIEW2 *pv)
{
	long t[6];
	MATRIX m, lw, tp;
	VECTOR nvp;
	long dx, dy, dz, len, hlen, s, c;
	int i;

	GsWSMATRIX = GsIDMATRIX2;
	gte_rotate_z_matrix(&GsWSMATRIX, -pv->rz);
	scale_view_param(&pv->vpx, t);
	dx = t[3] - t[0];
	dy = t[4] - t[1];
	dz = t[5] - t[2];
	len = SquareRoot0(dx * dx + dy * dy + dz * dz);
	if (len == 0)
		return 1;
	s = -(((t[1] - t[4]) << 12) / len);
	hlen = SquareRoot0(dx * dx + dz * dz);
	c = (hlen << 12) / len;
	Gssub_make_matrix(&m, s, c, 0x78);
	MulMatrix(&GsWSMATRIX, &m);
	if (hlen != 0) {
		s = -(((t[3] - t[0]) << 12) / hlen);
		c = ((t[5] - t[2]) << 12) / hlen;
		Gssub_make_matrix(&m, s, c, 0x79);
		MulMatrix(&GsWSMATRIX, &m);
	}
	nvp.vx = -pv->vpx;
	nvp.vy = -pv->vpy;
	nvp.vz = -pv->vpz;
	ApplyMatrixLV(&GsWSMATRIX, &nvp, (VECTOR *)GsWSMATRIX.t);
	if (pv->super != NULL) {
		GsGetLw(pv->super, &lw);
		TransposeMatrix(&lw, &tp);
		ApplyMatrixLV(&tp, (VECTOR *)lw.t, &nvp);
		tp.t[0] = -nvp.vx;
		tp.t[1] = -nvp.vy;
		tp.t[2] = -nvp.vz;
		GsMulCoord2(&GsWSMATRIX, &tp);
		GsWSMATRIX = tp;
	}
	GsWSMATRIX_ORG = GsWSMATRIX;
	(void)i;
	return 0;
}

/* glabel GsSetView2 */
int GsSetView2(GsVIEW2 *pv)
{
	MATRIX lw, tp;
	VECTOR v;
	GsWSMATRIX = pv->view;
	if (pv->super != NULL) {
		GsGetLw(pv->super, &lw);
		TransposeMatrix(&lw, &tp);
		ApplyMatrixLV(&tp, (VECTOR *)lw.t, &v);
		tp.t[0] = -v.vx;
		tp.t[1] = -v.vy;
		tp.t[2] = -v.vz;
		GsMulCoord2(&GsWSMATRIX, &tp);
		GsWSMATRIX = tp;
	}
	GsWSMATRIX_ORG = GsWSMATRIX;
	return 0;
}

/* shared coordinate-chain walk used by GsGetLw/Ls/Lws: returns the
 * local->world matrix into out, updating workm/flg caches. */
static void gs_get_lw_common(GsCOORDINATE2 *m, MATRIX *out)
{
	int sp = 0;
	int mark = 0x64;
	GsCOORDINATE2 *c = m;
	for (;;) {
		GsCOORDINATE2 *sup;
		DSTACK[sp] = c;
		sup = c->super;
		if (sup == NULL) {
			if (c->flg == PSDCNT || c->flg == 0) {
				c->workm = c->coord;
				*out = c->workm;
				c->flg = PSDCNT;
				break;
			}
			sp = mark + 1;
			if (mark == 0x64) {
				*out = DSTACK[0]->workm;
				sp = 0;
			} else {
				*out = DSTACK[sp]->workm;
			}
			break;
		}
		if (c->flg == PSDCNT) {
			*out = c->workm;
			break;
		}
		if (c->flg == 0)
			mark = sp;
		c = sup;
		sp++;
	}
	/* unwind: asm bases at GsORGOFSX == DSTACK-4, so it composes
	 * DSTACK[sp-1] down to DSTACK[0] (terminal node excluded) */
	while (sp > 0) {
		GsCOORDINATE2 *n = DSTACK[sp - 1];
		GsMulCoord3(out, &n->coord);
		n->workm = *out;
		n->flg = PSDCNT;
		sp--;
	}
}

/* glabel GsGetLw */
void GsGetLw(GsCOORDINATE2 *m, MATRIX *out)
{
	gs_get_lw_common(m, out);
}

/* glabel GsGetLs: local->screen = GsWSMATRIX * local->world */
void GsGetLs(GsCOORDINATE2 *m, MATRIX *out)
{
	gs_get_lw_common(m, out);
	GsMulCoord2(&GsWSMATRIX, out);
}

/* glabel GsGetLws: outw = local->world, outs = local->screen */
void GsGetLws(GsCOORDINATE2 *m, MATRIX *outw, MATRIX *outs)
{
	gs_get_lw_common(m, outw);
	*outs = *outw;
	GsMulCoord2(&GsWSMATRIX, outs);
}

/* TMD prim advances in bytes per primitive (from jtbl_801141FC). The
 * "grd" test is bit2 of the prim's dummy byte (word0 >>16 &4). */
static int tmd_prim_size(uint32_t w0)
{
	uint32_t cd = w0 >> 24;
	int grd = (w0 >> 16) & 4;
	switch (cd & 0xFD) {
	case 0x20: return grd ? 0x18 : 0x10;	/* F3 / F3G */
	case 0x21: return 0x10;			/* NF3 */
	case 0x24: return 0x18;			/* TF3 */
	case 0x25: return 0x1C;			/* TNF3 */
	case 0x28: return grd ? 0x20 : 0x14;	/* F4 / F4G */
	case 0x29: return 0x10;			/* NF4 */
	case 0x2C: return 0x20;			/* TF4 */
	case 0x2D: return 0x20;			/* TNF4 */
	case 0x30: return grd ? 0x1C : 0x14;	/* G3 / G3G */
	case 0x31: return 0x18;			/* NG3 */
	case 0x34: return 0x1C;			/* TG3 */
	case 0x35: return 0x24;			/* TNG3 */
	case 0x38: return grd ? 0x24 : 0x18;	/* G4 / G4G */
	case 0x39: return 0x1C;			/* NG4 */
	case 0x3C: return 0x24;			/* TG4 */
	case 0x3D: return 0x2C;			/* TNG4 */
	default:   return -1;
	}
}

/* glabel GsLinkObject4: point objp->tmd at object n of the table and
 * stamp each primitive run's word0 low half with the run length. */
void GsLinkObject4(unsigned long tmd_base, GsDOBJ2 *objp, int n)
{
	struct TMD_STRUCT *tmd =
		(struct TMD_STRUCT *)(tmd_base + n * 0x1C);
	uint8_t *p, *runstart;
	unsigned long cnt;
	unsigned short run;
	uint32_t prevcd, previn;
	int sz;

	objp->tmd = (unsigned long *)tmd;
	p = (uint8_t *)tmd->primtop;
	cnt = tmd->primn;
	{
		static int link_log;
		uint32_t w0 = p ? *(uint32_t *)p : 0;
		if (link_log < 40 &&
		    (cnt == 0 ||
		     (((w0 >> 24) & 0xFC) != 0x20 &&
		      ((w0 >> 24) & 0xFC) != 0x30 &&
		      ((w0 >> 24) & 0xFC) != 0x38))) {
			uint32_t *q = (uint32_t *)p;
			link_log++;
			emscripten_console_logf(
				"GsLinkObject4: base=%x n=%d primtop=%s+0x%x "
				"primn=%ld w0=%08lx %08lx %08lx %08lx",
				(unsigned)tmd_base, n,
				((uintptr_t)p - (uintptr_t)PSX_RAM) < 0x200000
					? "PSX_RAM" : "native",
				(unsigned)((uintptr_t)p & 0x1fffff), cnt,
				(unsigned long)q[0], (unsigned long)q[1],
				(unsigned long)q[2], (unsigned long)q[3]);
		}
	}
	if (cnt == 0)
		return;
	run = 0;
	runstart = p;
	prevcd = 0;
	previn = 0;
	while (cnt--) {
		uint32_t w0 = *(uint32_t *)p;
		uint32_t cd = w0 >> 24;
		uint32_t inb = (w0 >> 16) & 0xFF;
		if (prevcd != 0 && (cd != prevcd || inb != previn)) {
			*(uint16_t *)runstart = run;
			run = 0;
			runstart = p;
		}
		prevcd = cd;
		previn = inb;
		sz = tmd_prim_size(w0);
		if (sz < 0) {
			{
				uint32_t *q = (uint32_t *)tmd->primtop;
				int i;
				emscripten_console_logf(
					"GPU CODE not assigned: %x w0=%08lx "
					"pofs=%x base=%x n=%d primn=%ld\n",
					cd, (unsigned long)w0,
					(unsigned)(p - (uint8_t *)tmd->primtop),
					(unsigned)tmd_base, n, tmd->primn);
				for (i = 0; i < 24; i++)
					emscripten_console_logf(
						"  q[%d]=%08lx", i,
						(unsigned long)q[i]);
			}
			run++;		/* asm counts it without advancing */
			continue;
		}
		p += sz;
		run++;
	}
	*(uint16_t *)runstart = run;
}

/* Handler call signatures used by the dispatch (asm arg layout):
 *   lit : (prim, vertop, nortop, pk, n, shift, otp, scratch)
 *   non-lit/nf: (prim, vertop, pk, n, shift, otp, scratch) */
typedef PACKET *(*GsPfnL)(void *, SVECTOR *, SVECTOR *, PACKET *,
			  u_long, u_long, GsOT *, u_long *);
typedef PACKET *(*GsPfnN)(void *, SVECTOR *, PACKET *,
			  u_long, u_long, GsOT *, u_long *);

int gs_trace_n;
void web_gs_trace(int n) { gs_trace_n = n; }

/* glabel GsSortObject4J */
void GsSortObject4J(GsDOBJ2 *objp, GsOT *otp, int shift, u_long *scratch)
{
	struct TMD_STRUCT *tmd;
	gsort_obj_calls++;
	uint8_t *p;
	long remain;
	SVECTOR *vertop, *nortop;
	int lmode, divmode;

	if ((long)objp->attribute < 0)
		return;
	GsMATE_C = objp->attribute & 7;
	GsLMODE = (objp->attribute >> 3) & 3;
	GsLIGNR = objp->attribute & 1;
	GsLIOFF = (objp->attribute >> 6) & 1;
	GsNDIV = (objp->attribute >> 9) & 7;
	GsTON = (objp->attribute >> 30) & 1;
	divmode = (GsNDIV != 0);
	if (divmode) {
		scratch[0] = GsNDIV;
		scratch[1] = (u_long)HWD0;
		scratch[2] = (u_long)VWD0;
	}
	if (GsLIOFF == 1)
		lmode = 2;
	else if (GsLIGNR == 0)
		lmode = (GsLIGHT_MODE & 1) ? 1 : 0;
	else if (GsLIGNR == 1)
		lmode = (GsLMODE & 1) ? 1 : 0;
	else
		lmode = 0;

	tmd = (struct TMD_STRUCT *)objp->tmd;
	p = (uint8_t *)tmd->primtop;
	remain = (long)tmd->primn;
	vertop = (SVECTOR *)tmd->vertop;
	nortop = (SVECTOR *)tmd->nortop;
	if (remain == 0)
		return;
	int trace = gs_trace_n > 0;
	PACKET *trace_pk0 = GsOUT_PACKET_P;
	int trace_emitted = 0;

	while (remain > 0) {
		uint32_t w0 = *(uint32_t *)p;
		uint32_t cd = w0 >> 24;
		u_long n = *(uint16_t *)p;
		PACKET *r = NULL;
		long adv;
		tmd_prim_counts[(cd & 0x3f) & 0x1f]++;
		switch ((cd & 0xFD) - 0x20) {
		case 0x00:	/* F3 */
			if ((w0 >> 16) & 4) {
				r = ((GsPfnL)GsFCALL4.f3g[lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 24;
			} else {
				r = ((GsPfnL)GsFCALL4.f3[divmode][lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 16;
			}
			break;
		case 0x01:	/* NF3 */
			r = ((GsPfnN)GsFCALL4.nf3[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 16;
			break;
		case 0x04:	/* TF3 */
			r = ((GsPfnL)GsFCALL4.tf3[divmode][lmode])(
				p, vertop, nortop, GsOUT_PACKET_P,
				n, shift, otp, scratch);
			adv = (long)n * 24;
			break;
		case 0x05:	/* TNF3 */
			r = ((GsPfnN)GsFCALL4.ntf3[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 0x1C;
			break;
		case 0x08:	/* F4 */
			if ((w0 >> 16) & 4) {
				r = ((GsPfnL)GsFCALL4.f4g[lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 32;
			} else {
				r = ((GsPfnL)GsFCALL4.f4[divmode][lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 20;
			}
			break;
		case 0x09:	/* NF4 */
			r = ((GsPfnN)GsFCALL4.nf4[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 16;
			break;
		case 0x0C:	/* TF4 */
			r = ((GsPfnL)GsFCALL4.tf4[divmode][lmode])(
				p, vertop, nortop, GsOUT_PACKET_P,
				n, shift, otp, scratch);
			adv = (long)n * 32;
			break;
		case 0x0D:	/* TNF4 */
			r = ((GsPfnN)GsFCALL4.ntf4[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 32;
			break;
		case 0x10:	/* G3 */
			if ((w0 >> 16) & 4) {
				r = ((GsPfnL)GsFCALL4.g3g[lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 28;
			} else {
				r = ((GsPfnL)GsFCALL4.g3[divmode][lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 20;
			}
			break;
		case 0x11:	/* NG3 */
			r = ((GsPfnN)GsFCALL4.ng3[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 24;
			break;
		case 0x14:	/* TG3 */
			r = ((GsPfnL)GsFCALL4.tg3[divmode][lmode])(
				p, vertop, nortop, GsOUT_PACKET_P,
				n, shift, otp, scratch);
			adv = (long)n * 28;
			break;
		case 0x15:	/* TNG3 */
			r = ((GsPfnN)GsFCALL4.ntg3[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 36;
			break;
		case 0x18:	/* G4 */
			if ((w0 >> 16) & 4) {
				r = ((GsPfnL)GsFCALL4.g4g[lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 36;
			} else {
				r = ((GsPfnL)GsFCALL4.g4[divmode][lmode])(
					p, vertop, nortop, GsOUT_PACKET_P,
					n, shift, otp, scratch);
				adv = (long)n * 24;
			}
			break;
		case 0x19:	/* NG4 */
			r = ((GsPfnN)GsFCALL4.ng4[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 28;
			break;
		case 0x1C:	/* TG4 */
			r = ((GsPfnL)GsFCALL4.tg4[divmode][lmode])(
				p, vertop, nortop, GsOUT_PACKET_P,
				n, shift, otp, scratch);
			adv = (long)n * 36;
			break;
		case 0x1D:	/* TNG4 */
			r = ((GsPfnN)GsFCALL4.ntg4[divmode])(
				p, vertop, GsOUT_PACKET_P, n, shift, otp,
				scratch);
			adv = (long)n * 44;
			break;
		default:
			emscripten_console_log(
				"GsSortObject4J: non supported code");
			adv = 0;
			break;
		}
		if (r != NULL) {
			GsOUT_PACKET_P = r;
			if (trace)
				trace_emitted += (int)n;
		}
		if (adv <= 0)
			break;
		remain -= (long)n;
		p += adv;
	}
	if (trace) {
		gs_trace_n--;
		uint32_t *q = (uint32_t *)trace_pk0;
		long *tt = objp->coord2 ? objp->coord2->coord.t : 0;
		/* count emitted prims whose first vertex lands on-screen
		 * (PSDGPU mode: raster offset adds (160,120) or (160,360)) */
		int vis = 0;
		uint8_t *pp = (uint8_t *)trace_pk0;
		int k;
		for (k = 0; k < trace_emitted; k++) {
			uint32_t tag = *(uint32_t *)pp;
			uint32_t xy = *(uint32_t *)(pp + 8);
			int sx = (int16_t)(xy & 0xffff);
			int sy = (int16_t)(xy >> 16);
			if (sx >= -160 && sx <= 160 && sy >= -360 && sy <= 120)
				vis++;
			pp += ((tag >> 24) + 1) * 4;
		}
		emscripten_console_logf(
			"sobj attr=%08lx primn=%ld emitted=%d vis=%d t=%ld,%ld,%ld "
			"pk0=%08x sxy=%08x %08x %08x otz=%x "
			"rt=[%04x %04x %04x %04x %04x] tr=%lx,%lx,%lx h=%lx of=%lx,%lx",
			objp->attribute, (long)tmd->primn, trace_emitted, vis,
			tt ? (long)tt[0] : 0, tt ? (long)tt[1] : 0,
			tt ? (long)tt[2] : 0,
			q ? q[0] : 0,
			trace_emitted ? q[2] : 0,
			trace_emitted ? q[3] : 0,
			trace_emitted ? q[4] : 0,
			gte_ldr(7),
			gte_regs.c[0] & 0xffff, gte_regs.c[0] >> 16,
			gte_regs.c[1] & 0xffff, gte_regs.c[1] >> 16,
			gte_regs.c[2] & 0xffff,
			(unsigned long)gte_regs.c[5],
			(unsigned long)gte_regs.c[6],
			(unsigned long)gte_regs.c[7],
			(unsigned long)gte_regs.c[26],
			(unsigned long)gte_regs.c[24],
			(unsigned long)gte_regs.c[25]);
	}
}

void GsSortSpriteFast(GsSPRITE *sp, GsOT *ot, unsigned short pri)
{
	GsSortFastSprite(sp, ot, pri);
}
