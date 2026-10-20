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
void GsGetTimInfo(unsigned long *im, GsIMAGE *tim)
{
	uint8_t *p = (uint8_t *)im;
	uint8_t *q;
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

/* ---- 3D / TMD stubs (Segment 4/5) -------------------------------------- */

/* gte_init helpers: GTE-side setup, inert until the GTE lands */
void InitGeom(void) { }
void SetFarColor(long r, long g, long b) { (void)r; (void)g; (void)b; }
void SetGeomOffset(long x, long y) { (void)x; (void)y; }

void GsSetNearClip(long clip)
{
	(void)clip;
	web_unimplemented("GsSetNearClip");
}

PACKET *GsTMDdivTF3NL(void){web_unimplemented("GsTMDdivTF3NL");return 0;}
PACKET *GsTMDdivTF4L(void){web_unimplemented("GsTMDdivTF4L");return 0;}
PACKET *GsTMDdivTF4NL(void){web_unimplemented("GsTMDdivTF4NL");return 0;}
PACKET *GsTMDdivTG3NL(void){web_unimplemented("GsTMDdivTG3NL");return 0;}
PACKET *GsTMDdivTG4NL(void){web_unimplemented("GsTMDdivTG4NL");return 0;}
PACKET *GsTMDdivTNF3(void){web_unimplemented("GsTMDdivTNF3");return 0;}
PACKET *GsTMDdivTNF4(void){web_unimplemented("GsTMDdivTNF4");return 0;}
PACKET *GsTMDdivTNG3(void){web_unimplemented("GsTMDdivTNG3");return 0;}
PACKET *GsTMDdivTNG4(void){web_unimplemented("GsTMDdivTNG4");return 0;}
PACKET *GsTMDfastF3L(void){web_unimplemented("GsTMDfastF3L");return 0;}
PACKET *GsTMDfastF4L(void){web_unimplemented("GsTMDfastF4L");return 0;}
PACKET *GsTMDfastF4NL(void){web_unimplemented("GsTMDfastF4NL");return 0;}
PACKET *GsTMDfastG3L(void){web_unimplemented("GsTMDfastG3L");return 0;}
PACKET *GsTMDfastG4L(void){web_unimplemented("GsTMDfastG4L");return 0;}
PACKET *GsTMDfastNF4(void){web_unimplemented("GsTMDfastNF4");return 0;}
PACKET *GsTMDfastTF3L(void){web_unimplemented("GsTMDfastTF3L");return 0;}
PACKET *GsTMDfastTF3NL(void){web_unimplemented("GsTMDfastTF3NL");return 0;}
PACKET *GsTMDfastTF4L(void){web_unimplemented("GsTMDfastTF4L");return 0;}
PACKET *GsTMDfastTF4NL(void){web_unimplemented("GsTMDfastTF4NL");return 0;}
PACKET *GsTMDfastTG3L(void){web_unimplemented("GsTMDfastTG3L");return 0;}
PACKET *GsTMDfastTG3NL(void){web_unimplemented("GsTMDfastTG3NL");return 0;}
PACKET *GsTMDfastTG4L(void){web_unimplemented("GsTMDfastTG4L");return 0;}
PACKET *GsTMDfastTG4NL(void){web_unimplemented("GsTMDfastTG4NL");return 0;}
PACKET *GsTMDfastTNF3(void){web_unimplemented("GsTMDfastTNF3");return 0;}
PACKET *GsTMDfastTNF4(void){web_unimplemented("GsTMDfastTNF4");return 0;}
PACKET *GsTMDfastTNG3(void){web_unimplemented("GsTMDfastTNG3");return 0;}
PACKET *GsTMDfastTNG4(void){web_unimplemented("GsTMDfastTNG4");return 0;}

void GsGetLs(GsCOORDINATE2 *m, MATRIX *out)
{
	web_unimplemented("GsGetLs");
	(void)m; (void)out;
}

void GsGetLw(GsCOORDINATE2 *m, MATRIX *out)
{
	web_unimplemented("GsGetLw");
	(void)m; (void)out;
}

void GsGetLws(GsCOORDINATE2 *m, MATRIX *outw, MATRIX *outs)
{
	web_unimplemented("GsGetLws");
	(void)m; (void)outw; (void)outs;
}

void GsInitCoordinate2(GsCOORDINATE2 *super, GsCOORDINATE2 *base)
{
	web_unimplemented("GsInitCoordinate2");
	(void)super; (void)base;
}

void GsLinkObject4(unsigned long tmd_base, GsDOBJ2 *objp, int n)
{
	web_unimplemented("GsLinkObject4");
	(void)tmd_base; (void)objp; (void)n;
}

void GsMapModelingData(unsigned long *p)
{
	web_unimplemented("GsMapModelingData");
	(void)p;
}

void GsMulCoord0(MATRIX *m1, MATRIX *m2, MATRIX *m3)
{
	web_unimplemented("GsMulCoord0");
	(void)m1; (void)m2; (void)m3;
}

void GsMulCoord3(MATRIX *m1, MATRIX *m2)
{
	web_unimplemented("GsMulCoord3");
	(void)m1; (void)m2;
}

void GsSetAmbient(long r, long g, long b)
{
	web_unimplemented("GsSetAmbient");
	(void)r; (void)g; (void)b;
}

int GsSetFlatLight(int id, GsF_LIGHT *lt)
{
	web_unimplemented("GsSetFlatLight");
	(void)id; (void)lt;
	return 0;
}

void GsSetLightMatrix(MATRIX *mp)
{
	web_unimplemented("GsSetLightMatrix");
	(void)mp;
}

void GsSetLightMode(int mode)
{
	web_unimplemented("GsSetLightMode");
	(void)mode;
}

void GsSetLsMatrix(MATRIX *mp)
{
	web_unimplemented("GsSetLsMatrix");
	(void)mp;
}

void GsSetProjection(long h)
{
	web_unimplemented("GsSetProjection");
	(void)h;
}

int GsSetRefView2(GsRVIEW2 *pv)
{
	web_unimplemented("GsSetRefView2");
	(void)pv;
	return 0;
}

int GsSetView2(GsVIEW2 *pv)
{
	web_unimplemented("GsSetView2");
	(void)pv;
	return 0;
}

void GsSortObject4J(GsDOBJ2 *objp, GsOT *otp, int shift, u_long *scratch)
{
	web_unimplemented("GsSortObject4J");
	(void)objp; (void)otp; (void)shift; (void)scratch;
}

void GsSortSpriteFast(GsSPRITE *sp, GsOT *ot, unsigned short pri)
{
	GsSortFastSprite(sp, ot, pri);
}
