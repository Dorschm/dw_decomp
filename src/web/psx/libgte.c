/*
 * libgte for the web build - decompiled from asm/us/main/psyq/libgte.s.
 * Each function notes its source glabel; cop2 sequences map 1:1 onto
 * src/web/psx/gte.c (gte_ldr/gte_str/gte_scr/gte_lcr/gte_cmd).
 * Lookup tables come from the authentic libgte.data.s via
 * tools/gen_web_data.py (gte_rsin_tbl, gte_SQRT, gte_ratan_tbl,
 * gte_rcossin_tbl).
 */

#include <libgte.h>
#include <string.h>

#include <web/host/log.h>
#include <web/psx/gte.h>

extern const int16_t gte_rsin_tbl[];
extern const int16_t gte_SQRT[];
extern const int16_t gte_ratan_tbl[];
extern const uint32_t gte_rcossin_tbl[];

/* cop2 command bodies (bit25 tag stripped) as they appear in the asm. */
#define C_RTPS	0x00180001	/* 0100184A */
#define C_RTPT	0x00280030	/* 3000284A */
#define C_NCLIP	0x01400006	/* 0600404B */
#define C_AVZ3	0x0158002D	/* 2D00584B */
#define C_AVZ4	0x0168002E	/* 2E00684B */
#define C_NCCS	0x0148041B	/* 1B04084B */
#define C_NCCT	0x0168043F	/* 3F04184B */
#define C_MVMVA_V0	0x00480012	/* mvmva 1,0,0,3,0 */
#define C_MVMVA_HI	0x0041E012	/* mvmva 0,0,3,3,0 */
#define C_MVMVA_LO	0x0049E012	/* mvmva 1,0,3,3,0 */
#define C_OP	0x0170000C
#define C_DCPL	0x01400029
#define C_DPCS	0x01400010
#define C_DPCT	0x0140002A
#define C_INTPL	0x01400011
#define C_SQR	0x01480428
#define C_GPF	0x0140003D
#define C_GPL	0x0140003E
#define C_NCDS	0x01480413
#define C_NCDT	0x01680416
#define C_CDP	0x01480414
#define C_NCS	0x0148041E
#define C_NCT	0x01680420
#define C_CC	0x0148041C

/* MVMVA command encoder (per psx-spx bit layout). */
static inline uint32_t mvmva_w(int sf, int mx, int v, int cv, int lm)
{
	return 0x00400012 | ((uint32_t)sf << 19) | ((uint32_t)mx << 17) |
	       ((uint32_t)v << 15) | ((uint32_t)cv << 13) | ((uint32_t)lm << 10);
}

/* MATRIX (PsyQ): s16 m[3][3] packed in 5 words + VECTOR t at +0x14. */
static inline void ld_rt(const MATRIX *m)
{
	const uint32_t *w = (const uint32_t *)m;
	int i;
	for (i = 0; i < 5; i++)
		gte_scr(i, w[i]);
}

static inline void ld_tr(const MATRIX *m)
{
	const int32_t *t = (const int32_t *)m;
	gte_scr(5, (uint32_t)t[5]);
	gte_scr(6, (uint32_t)t[6]);
	gte_scr(7, (uint32_t)t[7]);
}

static inline void ld_vn(int n, const SVECTOR *v)
{
	uint32_t xy;
	uint16_t z;
	memcpy(&xy, v, 4);
	memcpy(&z, &v->vz, 2);
	gte_str(n * 2, xy);
	gte_str(n * 2 + 1, z);
}

static inline int16_t r16(uint32_t v) { return (int16_t)(v & 0xffff); }

/* columns of m1 through the loaded RT; r[i][c] = saturated 16-bit IRs,
 * matching the asm's mfc2 $9-$11 readback. */
static void mul_rt_cols(const MATRIX *m1, int32_t r[3][3])
{
	const uint16_t *s = (const uint16_t *)m1;
	int c, i;
	for (c = 0; c < 3; c++) {
		gte_str(0, (uint32_t)s[c] | ((uint32_t)s[c + 3] << 16));
		gte_str(1, (uint32_t)s[c + 6]);
		gte_cmd(mvmva_w(1, 0, 0, 3, 0));
		for (i = 0; i < 3; i++)
			r[i][c] = r16(gte_ldr(9 + i));
	}
}

static void store_rot(int32_t r[3][3], MATRIX *m)
{
	uint32_t *w = (uint32_t *)m;
	w[0] = (uint16_t)r[0][0] | ((uint32_t)(uint16_t)r[0][1] << 16);
	w[1] = (uint16_t)r[1][0] | ((uint32_t)(uint16_t)r[1][1] << 16);
	w[2] = (uint16_t)r[2][0] | ((uint32_t)(uint16_t)r[2][1] << 16);
	w[3] = (uint16_t)r[0][2] | ((uint32_t)(uint16_t)r[1][2] << 16);
	((uint16_t *)m)[8] = (uint16_t)r[2][2];
}

/* glabel MulMatrix0: m2 = m0 * m1 (rotation part only). */
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	int32_t r[3][3];
	ld_rt(m0);
	mul_rt_cols(m1, r);
	store_rot(r, m2);
	return m2;
}

/* glabel CompMatrix: m2.rot = m0.rot*m1.rot; m2.t = m0.t + m0.rot*m1.t */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	int32_t r[3][3];
	ld_rt(m0);
	mul_rt_cols(m1, r);
	store_rot(r, m2);
	/* translation: V0 = m1.t (packed xy + z), MVMVA, MAC + m0.t */
	gte_str(0, (uint32_t)(uint16_t)(int16_t)m1->t[0] |
		(((uint32_t)(uint16_t)(int16_t)m1->t[1]) << 16));
	gte_str(1, (uint32_t)(uint16_t)(int16_t)m1->t[2]);
	gte_cmd(mvmva_w(1, 0, 0, 3, 0));
	m2->t[0] = (int32_t)gte_ldr(25) + m0->t[0];
	m2->t[1] = (int32_t)gte_ldr(26) + m0->t[1];
	m2->t[2] = (int32_t)gte_ldr(27) + m0->t[2];
	return m2;
}

/* glabel MulMatrix: m0 = m0 * m1 (rotation part, in place). */
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1)
{
	MATRIX tmp;
	MulMatrix0(m0, m1, &tmp);
	memcpy(m0->m, tmp.m, sizeof(tmp.m));
	return m0;
}

/* glabel MulMatrix2: m1 = m0 * m1 (rotation part, in place on m1). */
MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1)
{
	MATRIX tmp;
	MulMatrix0(m0, m1, &tmp);
	memcpy(m1->m, tmp.m, sizeof(tmp.m));
	return m1;
}

/* glabel ApplyMatrixLV: v1 = m * v0 (long vector, split hi/lo). */
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	ld_rt(m);
	{
		int32_t lo[3], hi[3];
		int i;
		for (i = 0; i < 3; i++) {
			int32_t x = (&v0->vx)[i];
			if (x < 0) {
				hi[i] = -((-x) >> 15);
				lo[i] = -((-x) & 0x7FFF);
			} else {
				hi[i] = x >> 15;
				lo[i] = x & 0x7FFF;
			}
		}
		/* mvmva 0,0,3,3 (sf=0) on hi, then <<3 with sign fix; mvmva
		 * 1,0,3,3 on lo, sum */
		gte_str(9, (uint32_t)hi[0]);
		gte_str(10, (uint32_t)hi[1]);
		gte_str(11, (uint32_t)hi[2]);
		gte_cmd(mvmva_w(0, 0, 3, 3, 0));
		for (i = 0; i < 3; i++) {
			int32_t t = (int32_t)gte_ldr(25 + i);
			if (t < 0) t = -((-t) << 3); else t <<= 3;
			hi[i] = t;
		}
		gte_str(9, (uint32_t)lo[0]);
		gte_str(10, (uint32_t)lo[1]);
		gte_str(11, (uint32_t)lo[2]);
		gte_cmd(mvmva_w(1, 0, 3, 3, 0));
		v1->vx = (int32_t)gte_ldr(25) + hi[0];
		v1->vy = (int32_t)gte_ldr(26) + hi[1];
		v1->vz = (int32_t)gte_ldr(27) + hi[2];
	}
	return v1;
}

/* glabel ApplyMatrix: v1 = m * v0 (SVECTOR in, MAC out). */
VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{
	ld_rt(m);
	ld_vn(0, v0);
	gte_cmd(mvmva_w(1, 0, 0, 3, 0));
	v1->vx = (int32_t)gte_ldr(25);
	v1->vy = (int32_t)gte_ldr(26);
	v1->vz = (int32_t)gte_ldr(27);
	return v1;
}

/* glabel ApplyMatrixSV: same, s16 output through IR. */
SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{
	ld_rt(m);
	ld_vn(0, v0);
	gte_cmd(mvmva_w(1, 0, 0, 3, 0));
	v1->vx = r16(gte_ldr(9));
	v1->vy = r16(gte_ldr(10));
	v1->vz = r16(gte_ldr(11));
	v1->pad = 0;
	return v1;
}

/* glabel TransMatrix: m.t = v */
MATRIX *TransMatrix(MATRIX *m, VECTOR *v)
{
	m->t[0] = v->vx;
	m->t[1] = v->vy;
	m->t[2] = v->vz;
	return m;
}

/* glabel ScaleMatrix: column j scaled by v[j] >>12 each. */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v)
{
	uint32_t *w = (uint32_t *)m;
	int32_t s[3] = { v->vx, v->vy, v->vz };
	int i;
	for (i = 0; i < 5; i++) {
		int16_t lo = (int16_t)(w[i] & 0xffff);
		int16_t hi = (int16_t)(w[i] >> 16);
		uint32_t nlo;
		nlo = (uint16_t)(((int64_t)lo * s[(i * 2) % 3]) >> 12);
		/* element index: word i holds m[i/2.. ] - column is (2i)%3 */
		w[i] = nlo | ((uint32_t)(uint16_t)(((int64_t)hi * s[(i * 2 + 1) % 3]) >> 12) << 16);
	}
	return m;
}

/* glabel SetRotMatrix / SetLightMatrix / SetColorMatrix / SetTransMatrix */
void SetRotMatrix(MATRIX *m) { ld_rt(m); }
void SetLightMatrix(MATRIX *m)
{
	const uint32_t *w = (const uint32_t *)m;
	int i;
	for (i = 0; i < 5; i++)
		gte_scr(8 + i, w[i]);
}
void SetColorMatrix(MATRIX *m)
{
	const uint32_t *w = (const uint32_t *)m;
	int i;
	for (i = 0; i < 5; i++)
		gte_scr(16 + i, w[i]);
}
void SetTransMatrix(MATRIX *m) { ld_tr(m); }

/* glabel ReadSZfifo3 / ReadSZfifo4 / ReadGeomScreen */
void ReadSZfifo3(long *a, long *b, long *c)
{
	*a = (long)gte_ldr(17); *b = (long)gte_ldr(18); *c = (long)gte_ldr(19);
}
void ReadSZfifo4(long *a, long *b, long *c, long *d)
{
	*a = (long)gte_ldr(16); *b = (long)gte_ldr(17);
	*c = (long)gte_ldr(18); *d = (long)gte_ldr(19);
}
long ReadGeomScreen(void) { return (long)gte_lcr(26); }

/* glabel SetBackColor / SetFarColor / SetGeomOffset / SetGeomScreen */
void SetBackColor(long r, long g, long b)
{
	gte_scr(13, (uint32_t)(r << 4));
	gte_scr(14, (uint32_t)(g << 4));
	gte_scr(15, (uint32_t)(b << 4));
}
void SetFarColor(long r, long g, long b)
{
	gte_scr(21, (uint32_t)(r << 4));
	gte_scr(22, (uint32_t)(g << 4));
	gte_scr(23, (uint32_t)(b << 4));
}
void SetGeomOffset(long x, long y)
{
	gte_scr(24, (uint32_t)(x << 16));
	gte_scr(25, (uint32_t)(y << 16));
}
void SetGeomScreen(long h) { gte_scr(26, (uint32_t)h); }

/* glabel NormalColorCol: NCCS on v0 with rgb v1 -> v2 */
void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2)
{
	ld_vn(0, v0);
	gte_str(6, *(const uint32_t *)v1);
	gte_cmd(C_NCCS);
	*(uint32_t *)v2 = gte_ldr(22);
}

/* glabel NormalColorCol3: NCCT on v0..v2, rgb v3 -> v4..v6 */
void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, CVECTOR *v3,
		     CVECTOR *v4, CVECTOR *v5, CVECTOR *v6)
{
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_str(6, *(const uint32_t *)v3);
	gte_cmd(C_NCCT);
	*(uint32_t *)v4 = gte_ldr(20);
	*(uint32_t *)v5 = gte_ldr(21);
	*(uint32_t *)v6 = gte_ldr(22);
}

/* glabel RotTransPers: returns SZ3>>2; sxy, p=IR0?, flag */
long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag)
{
	ld_vn(0, v0);
	gte_cmd(C_RTPS);
	*(uint32_t *)sxy = gte_ldr(14);
	*(uint32_t *)p = gte_ldr(8);
	*(uint32_t *)flag = gte_lcr(31);
	return (long)gte_ldr(19) >> 2;
}

/* glabel RotTransPers3 */
long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
		   long *sxy0, long *sxy1, long *sxy2, long *p, long *flag)
{
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	*(uint32_t *)sxy0 = gte_ldr(12);
	*(uint32_t *)sxy1 = gte_ldr(13);
	*(uint32_t *)sxy2 = gte_ldr(14);
	*(uint32_t *)p = gte_ldr(8);
	*(uint32_t *)flag = gte_lcr(31);
	return (long)gte_ldr(19) >> 2;
}

/* glabel RotTransPers4 */
long RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
		   long *sxy0, long *sxy1, long *sxy2, long *sxy3,
		   long *p, long *flag)
{
	uint32_t f;
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	*(uint32_t *)sxy0 = gte_ldr(12);
	*(uint32_t *)sxy1 = gte_ldr(13);
	*(uint32_t *)sxy2 = gte_ldr(14);
	f = gte_lcr(31);
	ld_vn(0, v3);
	gte_cmd(C_RTPS);
	*(uint32_t *)sxy3 = gte_ldr(14);
	*(uint32_t *)p = gte_ldr(8);
	*(uint32_t *)flag = f | gte_lcr(31);
	return (long)gte_ldr(19) >> 2;
}

/* glabel RotAverage3: RTPT + AVSZ3, returns OTZ. */
long RotAverage3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
		 long *sxy0, long *sxy1, long *sxy2, long *p, long *flag)
{
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	*(uint32_t *)sxy0 = gte_ldr(12);
	*(uint32_t *)sxy1 = gte_ldr(13);
	*(uint32_t *)sxy2 = gte_ldr(14);
	*(uint32_t *)p = gte_ldr(8);
	*(uint32_t *)flag = gte_lcr(31);
	gte_cmd(C_AVZ3);
	return (long)gte_ldr(7);
}

/* glabel RotNclip3: returns MAC0; writes outputs only if MAC0 > 0. */
long RotNclip3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
	       long *sxy0, long *sxy1, long *sxy2, long *p, long *otz,
	       long *flag)
{
	uint32_t f;
	long mac0;
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	f = gte_lcr(31);
	gte_cmd(C_NCLIP);
	*(uint32_t *)flag = f;
	mac0 = (long)gte_ldr(24);
	if (mac0 > 0) {
		*(uint32_t *)sxy0 = gte_ldr(12);
		*(uint32_t *)sxy1 = gte_ldr(13);
		*(uint32_t *)sxy2 = gte_ldr(14);
		*(uint32_t *)p = gte_ldr(8);
		*(uint32_t *)otz = (uint32_t)gte_ldr(19) >> 2;
	}
	return mac0;
}

/* glabel RotNclip4 */
long RotNclip4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
	       long *sxy0, long *sxy1, long *sxy2, long *sxy3,
	       long *p, long *otz, long *flag)
{
	uint32_t f;
	long mac0;
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	f = gte_lcr(31);
	gte_cmd(C_NCLIP);
	mac0 = (long)gte_ldr(24);
	if (mac0 <= 0) {
		*(uint32_t *)flag = f;
		return mac0;
	}
	*(uint32_t *)sxy0 = gte_ldr(12);
	*(uint32_t *)sxy1 = gte_ldr(13);
	*(uint32_t *)sxy2 = gte_ldr(14);
	ld_vn(0, v3);
	gte_cmd(C_RTPS);
	*(uint32_t *)sxy3 = gte_ldr(14);
	*(uint32_t *)p = gte_ldr(8);
	*(uint32_t *)flag = f | gte_lcr(31);
	*(uint32_t *)otz = (uint32_t)gte_ldr(19) >> 2;
	return mac0;
}

/* glabel RotAverageNclip3 / RotAverageNclip4 */
long RotAverageNclip3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
		      long *sxy0, long *sxy1, long *sxy2, long *p, long *otz,
		      long *flag)
{
	uint32_t f;
	long mac0;
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	f = gte_lcr(31);
	gte_cmd(C_NCLIP);
	mac0 = (long)gte_ldr(24);
	if (mac0 > 0) {
		*(uint32_t *)sxy0 = gte_ldr(12);
		*(uint32_t *)sxy1 = gte_ldr(13);
		*(uint32_t *)sxy2 = gte_ldr(14);
		*(uint32_t *)p = gte_ldr(8);
		gte_cmd(C_AVZ3);
		*(uint32_t *)otz = gte_ldr(7);
	}
	*(uint32_t *)flag = f | gte_lcr(31);
	return mac0;
}

long RotAverageNclip4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
		      long *sxy0, long *sxy1, long *sxy2, long *sxy3,
		      long *p, long *otz, long *flag)
{
	uint32_t f;
	long mac0;
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_RTPT);
	f = gte_lcr(31);
	gte_cmd(C_NCLIP);
	mac0 = (long)gte_ldr(24);
	if (mac0 > 0) {
		*(uint32_t *)sxy0 = gte_ldr(12);
		*(uint32_t *)sxy1 = gte_ldr(13);
		*(uint32_t *)sxy2 = gte_ldr(14);
		ld_vn(0, v3);
		gte_cmd(C_RTPS);
		*(uint32_t *)sxy3 = gte_ldr(14);
		*(uint32_t *)p = gte_ldr(8);
		f |= gte_lcr(31);
		gte_cmd(C_AVZ4);
		*(uint32_t *)otz = gte_ldr(7);
	}
	*(uint32_t *)flag = f | gte_lcr(31);
	return mac0;
}

/* glabel TransposeMatrix */
MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1)
{
	int i, j;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			m1->m[j][i] = m0->m[i][j];
	return m1;
}

/* ---- rotation matrices (RotMatrix/RotMatrixYXZ/RotMatrixZYX) ---- */

/* fetch sin/cos pair for angle a from rcossin_tbl (hi=cos, lo=sin),
 * handling negative input exactly like the asm (index &0xFFF, negate). */
static inline int32_t rot_sincos(int32_t a, int32_t *s, int32_t *c)
{
	uint32_t w;
	if (a < 0) {
		a = -a;
		a &= 0xFFF;
		w = gte_rcossin_tbl[a];
		*s = -((int16_t)(w & 0xffff));
		*c = (int16_t)(w >> 16);
	} else {
		w = gte_rcossin_tbl[a & 0xFFF];
		*s = (int16_t)(w & 0xffff);
		*c = (int16_t)(w >> 16);
	}
	return 0;
}

#define FMUL(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 12))

/* glabel RotMatrix: Rz(z)*Ry(y)*Rx(x). Element products use the asm's
 * exact intermediate >>12 truncations. */
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
	int32_t sx, cx, sy, cy, sz, cz;
	rot_sincos(r->vx, &sx, &cx);
	rot_sincos(r->vy, &sy, &cy);
	rot_sincos(r->vz, &sz, &cz);
	m->m[0][2] = (int16_t)sy;
	m->m[0][0] = (int16_t)FMUL(cz, cy);
	m->m[0][1] = (int16_t)(-FMUL(sz, cy));
	m->m[1][2] = (int16_t)(-FMUL(cy, sx));
	m->m[2][2] = (int16_t)FMUL(cy, cx);
	{
		int32_t a = FMUL(cz, -sy);	/* -(cz*sy) >>12 */
		int32_t b = FMUL(sz, -sy);	/* -(sz*sy) >>12 */
		m->m[1][0] = (int16_t)(FMUL(sz, cx) - FMUL(a, sx));
		m->m[1][1] = (int16_t)(FMUL(cz, cx) + FMUL(b, sx));
		m->m[2][0] = (int16_t)(FMUL(sz, sx) + FMUL(a, cx));
		m->m[2][1] = (int16_t)(FMUL(cz, sx) - FMUL(b, cx));
	}
	return m;
}

/* glabel RotMatrixYXZ: R = Ry(y)*Rx(x)*Rz(z). Verified element products
 * against the asm: m02 = cx*sy, m10 = sz*cx, m11 = cz*cx, m12 = -sx,
 * m22 = cy*cx; two-product terms truncate per product like the asm. */
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
	int32_t sx, cx, sy, cy, sz, cz;
	rot_sincos(r->vx, &sx, &cx);
	rot_sincos(r->vy, &sy, &cy);
	rot_sincos(r->vz, &sz, &cz);
	m->m[1][2] = (int16_t)(-sx);
	m->m[0][2] = (int16_t)FMUL(sy, cx);
	m->m[2][2] = (int16_t)FMUL(cy, cx);
	m->m[1][0] = (int16_t)FMUL(sz, cx);
	m->m[1][1] = (int16_t)FMUL(cz, cx);
	{
		int32_t a = FMUL(sy, sx);	/* sx*sy>>12 */
		m->m[0][0] = (int16_t)(FMUL(cy, cz) + FMUL(a, sz));
		m->m[0][1] = (int16_t)(-FMUL(cy, sz) + FMUL(a, cz));
	}
	{
		int32_t b = FMUL(cy, sx);	/* cx*sy>>12 */
		m->m[2][0] = (int16_t)(-FMUL(sy, cz) + FMUL(b, sz));
		m->m[2][1] = (int16_t)(FMUL(sy, sz) + FMUL(b, cz));
	}
	return m;
}

/* glabel RotMatrixZYX: R = Rz(z)*Ry(y)*Rx(x) with the asm's sin/cos
 * pairing; singles verified: m00 = cy*cz, m10 = sz*cy, m20 = sy,
 * m21 = sx*cy, m22 = cx*cy, m01 = -sx*sy*cz - cx*sz, m02 = sx*sz -
 * cx*sy*cz. NOTE: the y sign convention here uses -sy inside products
 * (the asm negates the y sine). */
MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m)
{
	int32_t sx, cx, sy, cy, sz, cz;
	rot_sincos(r->vx, &sx, &cx);
	rot_sincos(r->vy, &sy, &cy);
	rot_sincos(r->vz, &sz, &cz);
	m->m[2][0] = (int16_t)sy;
	m->m[2][1] = (int16_t)FMUL(sx, cy);
	m->m[2][2] = (int16_t)FMUL(cx, cy);
	m->m[0][0] = (int16_t)FMUL(cy, cz);
	m->m[1][0] = (int16_t)FMUL(sz, cy);
	{
		int32_t a = FMUL(sx, -sy);	/* -(sx*sy)>>12 */
		m->m[0][1] = (int16_t)(FMUL(a, cz) - FMUL(sz, cx));
		m->m[1][1] = (int16_t)(FMUL(a, sz) + FMUL(cz, cx));
	}
	{
		int32_t b = FMUL(-sy, cx);	/* -(cx*sy)>>12 */
		m->m[0][2] = (int16_t)(FMUL(b, cz) + FMUL(sx, sz));
		m->m[1][2] = (int16_t)(FMUL(b, sz) - FMUL(sx, cz));
	}
	return m;
}

/* ---- trig/sqrt/atan from the authentic tables ---- */

/* glabel sin_1 (inner): quarter-wave fold on rsin_tbl, a = 0..0xFFF. */
static int32_t sin_1(int32_t a)
{
	if (a <= 0x800) {
		if (a <= 0x400)
			return gte_rsin_tbl[a];
		return gte_rsin_tbl[0x800 - a];
	}
	if (a <= 0xC00)
		return -gte_rsin_tbl[a - 0x800];
	return -gte_rsin_tbl[0x1000 - a];
}

/* glabel rsin / rcos */
int rsin(int a)
{
	if (a < 0)
		return -sin_1((-a) & 0xFFF);
	return sin_1(a & 0xFFF);
}
int rcos(int a)
{
	if (a < 0)
		a = -a;
	a &= 0xFFF;
	if (a <= 0x800) {
		if (a <= 0x400)
			return gte_rsin_tbl[0x400 - a];
		return -gte_rsin_tbl[a - 0x400];
	}
	if (a <= 0xC00)
		return -gte_rsin_tbl[0xC00 - a];
	return gte_rsin_tbl[a - 0xC00];
}

/* glabel SquareRoot0: LZCS/LZCR + SQRT table. */
long SquareRoot0(long a)
{
	uint32_t l, t2;
	int32_t t1;
	int64_t t4;
	long idx;
	gte_str(30, (uint32_t)a);
	l = gte_ldr(31);
	if (l == 32)
		return 0;
	t2 = l & ~1u;
	t1 = (int32_t)(0x1F - t2) >> 1;
	{
		int32_t t3 = (int32_t)t2 - 0x18;
		if (t3 >= 0)
			t4 = (int32_t)a << t3;
		else
			t4 = (int32_t)a >> (0x18 - (int32_t)t2);
	}
	idx = (long)t4 - 0x40;
	return ((long)gte_SQRT[idx] << t1) >> 12;
}

/* SquareRoot12: same core << 12. */
long SquareRoot12(long a)
{
	uint32_t l, t2;
	int32_t t1;
	int64_t t4;
	long idx;
	if (a <= 0)
		return 0;
	gte_str(30, (uint32_t)a);
	l = gte_ldr(31);
	if (l == 32)
		return 0;
	t2 = l & ~1u;
	t1 = (int32_t)(0x1F - t2) >> 1;
	{
		int32_t t3 = (int32_t)t2 - 0x18;
		if (t3 >= 0)
			t4 = (int32_t)a << t3;
		else
			t4 = (int32_t)a >> (0x18 - (int32_t)t2);
	}
	idx = (long)t4 - 0x40;
	return ((long)gte_SQRT[idx] << t1);
}

/* glabel ratan2(y, x): 4096/rev, via ratan_tbl[ratio]. */
long ratan2(long y, long x)
{
	int xneg = 0, yneg = 0, swap;
	long t, ang;
	if (x < 0) { xneg = 1; x = -x; }
	if (y < 0) { yneg = 1; y = -y; }
	if (y == 0) {
		if (x == 0)
			return 0;
	}
	swap = (x < y);
	if (swap) { t = x; x = y; y = t; }
	/* ratio = (smaller<<10)/larger; avoid overflow per asm */
	if (y & 0x7FE00000)
		ang = (x / (y >> 10));
	else
		ang = (x << 10) / y;
	ang = gte_ratan_tbl[ang];
	if (swap)
		ang = 0x400 - ang;
	if (xneg)
		ang = 0x800 - ang;
	if (yneg)
		ang = -ang;
	return ang;
}

void InitGeom(void)
{
	/* glabel InitGeom: sets OFX/OFY to screen centre half, H=0,
	 * ZSF3 = 0x155, ZSF4 = 0x100 (default averages), DQA/DQB = 0. */
	gte_scr(24, 160 << 16);
	gte_scr(25, 112 << 16);
	gte_scr(26, 0);
	gte_scr(27, 0);
	gte_scr(28, 0);
	gte_scr(29, 0x155);
	gte_scr(30, 0x100);
}

void InvSquareRoot(long a, long *b, long *c)
{
	(void)a;
	web_unimplemented("InvSquareRoot");
	if (b) *b = 0;
	if (c) *c = 0;
}

/* glabel PushMatrix / PopMatrix: GTE register stack (MSP/MSTACK). */
#define GTE_MSTACK_MAX 0x280
static uint32_t gte_mstack[GTE_MSTACK_MAX / 4];
static int32_t gte_msp;

void PushMatrix(void)
{
	uint32_t *p;
	if (gte_msp >= GTE_MSTACK_MAX) {
		web_unimplemented("PushMatrix overflow");
		return;
	}
	p = gte_mstack + gte_msp / 4;
	p[0] = gte_lcr(0); p[1] = gte_lcr(1); p[2] = gte_lcr(2);
	p[3] = gte_lcr(3); p[4] = gte_lcr(4);
	p[5] = gte_lcr(5); p[6] = gte_lcr(6); p[7] = gte_lcr(7);
	gte_msp += 0x20;
}

void PopMatrix(void)
{
	uint32_t *p;
	if (gte_msp <= 0) {
		web_unimplemented("PopMatrix empty");
		return;
	}
	gte_msp -= 0x20;
	p = gte_mstack + gte_msp / 4;
	gte_scr(0, p[0]); gte_scr(1, p[1]); gte_scr(2, p[2]);
	gte_scr(3, p[3]); gte_scr(4, p[4]);
	gte_scr(5, p[5]); gte_scr(6, p[6]); gte_scr(7, p[7]);
}

/* glabel ApplyTransposeMatrixLV: v1 = m^T * v0 (hi/lo split like
 * ApplyMatrixLV but on columns). */
VECTOR *ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	int32_t lo[3], hi[3], out[3];
	int i, j;
	for (i = 0; i < 3; i++) {
		int32_t x = (&v0->vx)[i];
		if (x < 0) { hi[i] = -((-x) >> 15); lo[i] = -((-x) & 0x7FFF); }
		else { hi[i] = x >> 15; lo[i] = x & 0x7FFF; }
	}
	for (i = 0; i < 3; i++) {
		int64_t acc = 0;
		for (j = 0; j < 3; j++)
			acc += (int64_t)m->m[j][i] * hi[j];
		out[i] = (int32_t)acc << 3;
	}
	for (i = 0; i < 3; i++) {
		int64_t acc = 0;
		for (j = 0; j < 3; j++)
			acc += (int64_t)m->m[j][i] * lo[j];
		out[i] += (int32_t)(acc >> 12);
	}
	v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
	return v1;
}

void NormalColorDpq(SVECTOR *v0, CVECTOR *v1, long p, CVECTOR *v2)
{
	/* NCDS: light*normal, *colour, depth cue by IR0 */
	ld_vn(0, v0);
	gte_str(6, *(const uint32_t *)v1);
	gte_str(8, (uint32_t)p);
	gte_cmd(C_NCDS);
	*(uint32_t *)v2 = gte_ldr(22);
}

long NormalClip(long sxy0, long sxy1, long sxy2)
{
	gte_str(12, (uint32_t)sxy0);
	gte_str(13, (uint32_t)sxy1);
	gte_str(14, (uint32_t)sxy2);
	gte_cmd(C_NCLIP);
	return (long)gte_ldr(24);
}

long AverageZ3(long sz0, long sz1, long sz2)
{
	gte_str(17, (uint32_t)sz0);
	gte_str(18, (uint32_t)sz1);
	gte_str(19, (uint32_t)sz2);
	gte_cmd(C_AVZ3);
	return (long)gte_ldr(7);
}
long AverageZ4(long sz0, long sz1, long sz2, long sz3)
{
	gte_str(16, (uint32_t)sz0);
	gte_str(17, (uint32_t)sz1);
	gte_str(18, (uint32_t)sz2);
	gte_str(19, (uint32_t)sz3);
	gte_cmd(C_AVZ4);
	return (long)gte_ldr(7);
}

void NormalColor(SVECTOR *v0, CVECTOR *v1)
{
	ld_vn(0, v0);
	gte_cmd(C_NCS);
	*(uint32_t *)v1 = gte_ldr(22);
}

void NormalColor3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
		  CVECTOR *v3, CVECTOR *v4, CVECTOR *v5)
{
	ld_vn(0, v0); ld_vn(1, v1); ld_vn(2, v2);
	gte_cmd(C_NCT);
	*(uint32_t *)v3 = gte_ldr(20);
	*(uint32_t *)v4 = gte_ldr(21);
	*(uint32_t *)v5 = gte_ldr(22);
}

void RotTrans(SVECTOR *v0, VECTOR *v1, long *flag)
{
	ld_vn(0, v0);
	gte_cmd(mvmva_w(1, 0, 0, 0, 0));
	v1->vx = (int32_t)gte_ldr(25);
	v1->vy = (int32_t)gte_ldr(26);
	v1->vz = (int32_t)gte_ldr(27);
	*flag = (long)gte_lcr(31);
}

/* csin/ccos: aliases via rsin_tbl */
int csin(int a) { return rsin(a); }
int ccos(int a) { return rcos(a); }


/* ================= TMD primitive handlers =================
 * GsTMDfast* — all from libgte.s. Common skeleton per prim:
 *   lwc2 V0..V2 = verts; rtpt; FLAG<0 -> skip; nclip; MAC0<=0 -> skip;
 *   swc2 SXY0..2; (quads: lwc2 V0 = v3; rtps; FLAG check; swc2 SXY2);
 *   avsz3/4 -> OTZ; (L: NCCS/NCCT on normals); write packet; OT link.
 * P_TAG len = data-word count; bucket = otp->org[(otz-offset)>>shift].
 */
#include <libgs.h>

#define RU8(p, o)	(*(const uint8_t *)((const uint8_t *)(p) + (o)))
#define RU16(p, o)	(*(const uint16_t *)((const uint8_t *)(p) + (o)))
#define RU32(p, o)	(*(const uint32_t *)((const uint8_t *)(p) + (o)))

#define SXY0W	gte_ldr(12)
#define SXY1W	gte_ldr(13)
#define SXY2W	gte_ldr(14)

static inline const SVECTOR *tmd_v(const SVECTOR *top, uint32_t i)
{
	return (const SVECTOR *)((const uint8_t *)top + (i << 3));
}

static inline void tmd_link(uint32_t *pk, GsOT *otp, uint32_t shift,
			    uint32_t taglen)
{
	uint32_t *e = (uint32_t *)otp->org +
	    ((((uint32_t)gte_ldr(7) - otp->offset) >> shift) & 0xFFFF);
	uint32_t old = *e;
	pk[0] = (taglen << 24) | (old & 0xFFFFFFu);
	*e = (old & 0xFF000000u) | ((uint32_t)(uintptr_t)pk & 0xFFFFFFu);
}

static inline uint32_t flag31(void) { return gte_lcr(31) & 0x80000000u; }

/* ---- flat untextured ---- */

/* glabel GsTMDfastF3L - TMD_P_F3: {hdr, rgb, n0v0, v1v2} */
u_long *GsTMDfastF3L(TMD_P_F3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		     POLY_F3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t nv0 = RU32(pr, 8), v12 = RU32(pr, 12);
		ld_vn(0, tmd_v(vertop, nv0 >> 16));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		ld_vn(0, tmd_v(nortop, nv0 & 0xffff));
		gte_cmd(C_NCCS);
		tmd_link(pk, otp, shift, 4);
		pk[1] = gte_ldr(22);
		pk[2] = SXY0W; pk[3] = SXY1W; pk[4] = SXY2W;
		pk += 5;
	next:
		pr += 0x10;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastNF4 - TMD_P_NF4: {hdr, rgb, v0v1, v2v3} */
u_long *GsTMDfastNF4(TMD_P_NF4 *primtop, SVECTOR *vertop,
		     POLY_F4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v01 = RU32(pr, 8), v23 = RU32(pr, 12);
		ld_vn(0, tmd_v(vertop, v01 & 0xffff));
		ld_vn(1, tmd_v(vertop, v01 >> 16));
		ld_vn(2, tmd_v(vertop, v23 & 0xffff));
		gte_cmd(C_RTPT);
		gte_str(22, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[3] = SXY1W; pk[4] = SXY2W;
		ld_vn(0, tmd_v(vertop, v23 >> 16));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 5);
		pk[1] = gte_ldr(22);
		pk[5] = SXY2W;
		pk += 6;
	next:
		pr += 0x10;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastF4NL - TMD_P_F4, lighting off */
u_long *GsTMDfastF4NL(TMD_P_F4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		      POLY_F4 *s, u_long n, u_long shift, GsOT *otp)
{
	(void)nortop;
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t nv0 = RU32(pr, 8), v12 = RU32(pr, 12);
		ld_vn(0, tmd_v(vertop, nv0 >> 16));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		gte_str(22, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[3] = SXY1W; pk[4] = SXY2W;
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x10)));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 5);
		pk[1] = gte_ldr(22);
		pk[5] = SXY2W;
		pk += 6;
	next:
		pr += 0x14;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastF4L */
u_long *GsTMDfastF4L(TMD_P_F4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		     POLY_F4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t nv0 = RU32(pr, 8), v12 = RU32(pr, 12);
		ld_vn(0, tmd_v(vertop, nv0 >> 16));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[3] = SXY1W; pk[4] = SXY2W;
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x10)));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		gte_cmd(C_AVZ4);
		ld_vn(0, tmd_v(nortop, nv0 & 0xffff));
		gte_cmd(C_NCCS);
		tmd_link(pk, otp, shift, 5);
		pk[1] = gte_ldr(22);
		pk[5] = SXY2W;
		pk += 6;
	next:
		pr += 0x14;
	}
	return (u_long *)pk;
}

/* ---- gouraud untextured ---- */

/* glabel GsTMDfastG3L - TMD_P_G3: {hdr, rgb, n0v0, n1v1, n2v2} */
u_long *GsTMDfastG3L(TMD_P_G3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		     POLY_G3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t n0v0 = RU32(pr, 8), n1v1 = RU32(pr, 12),
			n2v2 = RU32(pr, 0x10);
		ld_vn(0, tmd_v(vertop, n0v0 >> 16));
		ld_vn(1, tmd_v(vertop, n1v1 >> 16));
		ld_vn(2, tmd_v(vertop, n2v2 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		ld_vn(0, tmd_v(nortop, n0v0 & 0xffff));
		ld_vn(1, tmd_v(nortop, n1v1 & 0xffff));
		ld_vn(2, tmd_v(nortop, n2v2 & 0xffff));
		gte_cmd(C_NCCT);
		tmd_link(pk, otp, shift, 6);
		pk[1] = gte_ldr(20); pk[2] = SXY0W;
		pk[3] = gte_ldr(21); pk[4] = SXY1W;
		pk[5] = gte_ldr(22); pk[6] = SXY2W;
		pk += 7;
	next:
		pr += 0x14;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastG4L - TMD_P_G4: {hdr, rgb, n0v0..n3v3} */
u_long *GsTMDfastG4L(TMD_P_G4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		     POLY_G4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t n0v0 = RU32(pr, 8), n1v1 = RU32(pr, 12),
			n2v2 = RU32(pr, 0x10), n3v3 = RU32(pr, 0x14);
		ld_vn(0, tmd_v(vertop, n0v0 >> 16));
		ld_vn(1, tmd_v(vertop, n1v1 >> 16));
		ld_vn(2, tmd_v(vertop, n2v2 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, RU32(pr, 4) | (GsTON << 25));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[4] = SXY1W; pk[6] = SXY2W;
		ld_vn(0, tmd_v(vertop, n3v3 >> 16));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[8] = SXY2W;
		gte_cmd(C_AVZ4);
		ld_vn(0, tmd_v(nortop, n3v3 & 0xffff));
		gte_cmd(C_NCCS);	/* lit n3 -> RGB2 */
		{
			uint32_t c3 = gte_ldr(22);
			ld_vn(0, tmd_v(nortop, n0v0 & 0xffff));
			ld_vn(1, tmd_v(nortop, n1v1 & 0xffff));
			ld_vn(2, tmd_v(nortop, n2v2 & 0xffff));
			gte_cmd(C_NCCT);
			tmd_link(pk, otp, shift, 8);
			pk[1] = gte_ldr(20); pk[3] = gte_ldr(21);
			pk[5] = gte_ldr(22); pk[7] = c3;
		}
		pk += 9;
	next:
		pr += 0x18;
	}
	return (u_long *)pk;
}

/* ---- flat textured ---- */

/* glabel GsTMDfastTNF3 - TMD_P_TNF3: {hdr,uv0c,uv1t,uv2,rgb,v0v1,v2p} */
u_long *GsTMDfastTNF3(TMD_P_TNF3 *primtop, SVECTOR *vertop,
		      POLY_FT3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v01 = RU32(pr, 0x14);
		ld_vn(0, tmd_v(vertop, v01 & 0xffff));
		ld_vn(1, tmd_v(vertop, v01 >> 16));
		ld_vn(2, tmd_v(vertop, RU16(pr, 0x18)));
		gte_cmd(C_RTPT);
		gte_str(22, (RU32(pr, 0x10) & 0xffffff) |
			((uint32_t)((RU8(pr, 3) | (GsTON << 1)) & 0xfe) << 24));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		tmd_link(pk, otp, shift, 7);
		pk[1] = gte_ldr(22);
		pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = SXY1W; pk[5] = RU32(pr, 8);
		pk[6] = SXY2W; pk[7] = RU32(pr, 0xC);
		pk += 8;
	next:
		pr += 0x1C;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTF3NL - TMD_P_TF3, lighting off: rgb0 = 0x808080|code */
u_long *GsTMDfastTF3NL(TMD_P_TF3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		       POLY_FT3 *s, u_long n, u_long shift, GsOT *otp)
{
	(void)nortop;
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v12 = RU32(pr, 0x14);
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x12)));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		tmd_link(pk, otp, shift, 7);
		pk[1] = 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24);
		pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = SXY1W; pk[5] = RU32(pr, 8);
		pk[6] = SXY2W; pk[7] = RU32(pr, 0xC);
		pk += 8;
	next:
		pr += 0x18;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTF3L */
u_long *GsTMDfastTF3L(TMD_P_TF3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		      POLY_FT3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t nv0 = RU32(pr, 0x10), v12 = RU32(pr, 0x14);
		ld_vn(0, tmd_v(vertop, nv0 >> 16));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		ld_vn(0, tmd_v(nortop, nv0 & 0xffff));
		gte_cmd(C_NCCS);
		tmd_link(pk, otp, shift, 7);
		pk[1] = gte_ldr(22);
		pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = SXY1W; pk[5] = RU32(pr, 8);
		pk[6] = SXY2W; pk[7] = RU32(pr, 0xC);
		pk += 8;
	next:
		pr += 0x18;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTNF4 - TMD_P_TNF4: {hdr,uv x4,rgb,v0v1,v2v3} */
u_long *GsTMDfastTNF4(TMD_P_TNF4 *primtop, SVECTOR *vertop,
		      POLY_FT4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v01 = RU32(pr, 0x18), v23 = RU32(pr, 0x1C);
		ld_vn(0, tmd_v(vertop, v01 & 0xffff));
		ld_vn(1, tmd_v(vertop, v01 >> 16));
		ld_vn(2, tmd_v(vertop, v23 & 0xffff));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[4] = SXY1W; pk[6] = SXY2W;
		ld_vn(0, tmd_v(vertop, v23 >> 16));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[8] = SXY2W;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 9);
		pk[1] = (RU32(pr, 0x14) & 0xffffff) |
			((uint32_t)((RU8(pr, 3) | (GsTON << 1)) & 0xfe) << 24);
		pk[3] = RU32(pr, 4); pk[5] = RU32(pr, 8);
		pk[7] = RU32(pr, 0xC); pk[9] = RU32(pr, 0x10);
		pk += 10;
	next:
		pr += 0x20;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTF4NL */
u_long *GsTMDfastTF4NL(TMD_P_TF4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		       POLY_FT4 *s, u_long n, u_long shift, GsOT *otp)
{
	(void)nortop;
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v12 = RU32(pr, 0x18);
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x16)));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[4] = SXY1W; pk[6] = SXY2W;
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x1C)));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[8] = SXY2W;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 9);
		pk[1] = 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24);
		pk[3] = RU32(pr, 4); pk[5] = RU32(pr, 8);
		pk[7] = RU32(pr, 0xC); pk[9] = RU32(pr, 0x10);
		pk += 10;
	next:
		pr += 0x20;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTF4L */
u_long *GsTMDfastTF4L(TMD_P_TF4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		      POLY_FT4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t nv0 = RU32(pr, 0x14), v12 = RU32(pr, 0x18);
		ld_vn(0, tmd_v(vertop, nv0 >> 16));
		ld_vn(1, tmd_v(vertop, v12 & 0xffff));
		ld_vn(2, tmd_v(vertop, v12 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[4] = SXY1W; pk[6] = SXY2W;
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x1C)));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[8] = SXY2W;
		gte_cmd(C_AVZ4);
		ld_vn(0, tmd_v(nortop, nv0 & 0xffff));
		gte_cmd(C_NCCS);
		tmd_link(pk, otp, shift, 9);
		pk[1] = gte_ldr(22);
		pk[3] = RU32(pr, 4); pk[5] = RU32(pr, 8);
		pk[7] = RU32(pr, 0xC); pk[9] = RU32(pr, 0x10);
		pk += 10;
	next:
		pr += 0x20;
	}
	return (u_long *)pk;
}

/* ---- gouraud textured ---- */

/* glabel GsTMDfastTNG3 - TMD_P_TNG3: rgb0..2 then v01,v2 */
u_long *GsTMDfastTNG3(TMD_P_TNG3 *primtop, SVECTOR *vertop,
		      POLY_GT3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v01 = RU32(pr, 0x1C);
		uint32_t code = ((uint32_t)((RU8(pr, 3) | (GsTON << 1)) & 0xfe))
				<< 24;
		ld_vn(0, tmd_v(vertop, v01 & 0xffff));
		ld_vn(1, tmd_v(vertop, v01 >> 16));
		ld_vn(2, tmd_v(vertop, RU16(pr, 0x20)));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		tmd_link(pk, otp, shift, 9);
		pk[1] = (RU32(pr, 0x10) & 0xffffff) | code;
		pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = RU32(pr, 0x14); pk[5] = SXY1W; pk[6] = RU32(pr, 8);
		pk[7] = RU32(pr, 0x18); pk[8] = SXY2W; pk[9] = RU32(pr, 0xC);
		pk += 10;
	next:
		pr += 0x24;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTG3NL - emits FT3 packet (lighting off) */
u_long *GsTMDfastTG3NL(TMD_P_TG3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		       POLY_FT3 *s, u_long n, u_long shift, GsOT *otp)
{
	(void)nortop;
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x12)));
		ld_vn(1, tmd_v(vertop, RU16(pr, 0x16)));
		ld_vn(2, tmd_v(vertop, RU16(pr, 0x1A)));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		tmd_link(pk, otp, shift, 7);
		pk[1] = 0x808080u | ((0x25u |
			(uint32_t)((RU8(pr, 3) & 2) | (GsTON << 1))) << 24);
		pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = SXY1W; pk[5] = RU32(pr, 8);
		pk[6] = SXY2W; pk[7] = RU32(pr, 0xC);
		pk += 8;
	next:
		pr += 0x1C;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTG3L - GT3 packet, NCCT on 3 normals */
u_long *GsTMDfastTG3L(TMD_P_TG3 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		      POLY_GT3 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t n0v0 = RU32(pr, 0x10), n1v1 = RU32(pr, 0x14),
			n2v2 = RU32(pr, 0x18);
		ld_vn(0, tmd_v(vertop, n0v0 >> 16));
		ld_vn(1, tmd_v(vertop, n1v1 >> 16));
		ld_vn(2, tmd_v(vertop, n2v2 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		gte_cmd(C_AVZ3);
		ld_vn(0, tmd_v(nortop, n0v0 & 0xffff));
		ld_vn(1, tmd_v(nortop, n1v1 & 0xffff));
		ld_vn(2, tmd_v(nortop, n2v2 & 0xffff));
		gte_cmd(C_NCCT);
		tmd_link(pk, otp, shift, 9);
		pk[1] = gte_ldr(20); pk[2] = SXY0W; pk[3] = RU32(pr, 4);
		pk[4] = gte_ldr(21); pk[5] = SXY1W; pk[6] = RU32(pr, 8);
		pk[7] = gte_ldr(22); pk[8] = SXY2W; pk[9] = RU32(pr, 0xC);
		pk += 10;
	next:
		pr += 0x1C;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTNG4 - TMD_P_TNG4: rgb0..3 then v01,v23 */
u_long *GsTMDfastTNG4(TMD_P_TNG4 *primtop, SVECTOR *vertop,
		      POLY_GT4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t v01 = RU32(pr, 0x24), v23 = RU32(pr, 0x28);
		uint32_t code = ((uint32_t)((RU8(pr, 3) | (GsTON << 1)) & 0xfe))
				<< 24;
		ld_vn(0, tmd_v(vertop, v01 & 0xffff));
		ld_vn(1, tmd_v(vertop, v01 >> 16));
		ld_vn(2, tmd_v(vertop, v23 & 0xffff));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[5] = SXY1W; pk[8] = SXY2W;
		ld_vn(0, tmd_v(vertop, v23 >> 16));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[11] = SXY2W;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 0xC);
		pk[1] = (RU32(pr, 0x14) & 0xffffff) | code;
		pk[3] = RU32(pr, 4);
		pk[4] = RU32(pr, 0x18);
		pk[6] = RU32(pr, 8);
		pk[7] = RU32(pr, 0x1C);
		pk[9] = RU32(pr, 0xC);
		pk[10] = RU32(pr, 0x20);
		pk[12] = RU32(pr, 0x10);
		pk += 13;
	next:
		pr += 0x2C;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTG4NL - FT4 packet */
u_long *GsTMDfastTG4NL(TMD_P_TG4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		       POLY_FT4 *s, u_long n, u_long shift, GsOT *otp)
{
	(void)nortop;
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x16)));
		ld_vn(1, tmd_v(vertop, RU16(pr, 0x1A)));
		ld_vn(2, tmd_v(vertop, RU16(pr, 0x1E)));
		gte_cmd(C_RTPT);
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[4] = SXY1W; pk[6] = SXY2W;
		ld_vn(0, tmd_v(vertop, RU16(pr, 0x22)));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[8] = SXY2W;
		gte_cmd(C_AVZ4);
		tmd_link(pk, otp, shift, 9);
		pk[1] = 0x808080u | ((0x2Du |
			(uint32_t)((RU8(pr, 3) & 2) | (GsTON << 1))) << 24);
		pk[3] = RU32(pr, 4); pk[5] = RU32(pr, 8);
		pk[7] = RU32(pr, 0xC); pk[9] = RU32(pr, 0x10);
		pk += 10;
	next:
		pr += 0x24;
	}
	return (u_long *)pk;
}

/* glabel GsTMDfastTG4L - GT4, NCCS(n3) + NCCT(n0..2) */
u_long *GsTMDfastTG4L(TMD_P_TG4 *primtop, SVECTOR *vertop, SVECTOR *nortop,
		      POLY_GT4 *s, u_long n, u_long shift, GsOT *otp)
{
	uint32_t *pk = (uint32_t *)s;
	const uint8_t *pr = (const uint8_t *)primtop;
	while (n--) {
		uint32_t n0v0 = RU32(pr, 0x14), n1v1 = RU32(pr, 0x18),
			n2v2 = RU32(pr, 0x1C), n3v3 = RU32(pr, 0x20);
		ld_vn(0, tmd_v(vertop, n0v0 >> 16));
		ld_vn(1, tmd_v(vertop, n1v1 >> 16));
		ld_vn(2, tmd_v(vertop, n2v2 >> 16));
		gte_cmd(C_RTPT);
		gte_str(6, 0x808080u |
			((uint32_t)(RU8(pr, 3) | (GsTON << 1)) << 24));
		if (flag31())
			goto next;
		gte_cmd(C_NCLIP);
		if ((int32_t)gte_ldr(24) <= 0)
			goto next;
		pk[2] = SXY0W; pk[5] = SXY1W; pk[8] = SXY2W;
		ld_vn(0, tmd_v(vertop, n3v3 >> 16));
		gte_cmd(C_RTPS);
		if (flag31())
			goto next;
		pk[11] = SXY2W;
		gte_cmd(C_AVZ4);
		ld_vn(0, tmd_v(nortop, n3v3 & 0xffff));
		gte_cmd(C_NCCS);
		{
			uint32_t c3 = gte_ldr(22);
			ld_vn(0, tmd_v(nortop, n0v0 & 0xffff));
			ld_vn(1, tmd_v(nortop, n1v1 & 0xffff));
			ld_vn(2, tmd_v(nortop, n2v2 & 0xffff));
			gte_cmd(C_NCCT);
			tmd_link(pk, otp, shift, 0xC);
			pk[1] = gte_ldr(20);
			pk[3] = RU32(pr, 4);
			pk[4] = gte_ldr(21);
			pk[6] = RU32(pr, 8);
			pk[7] = gte_ldr(22);
			pk[9] = RU32(pr, 0xC);
			pk[10] = c3;
			pk[12] = RU32(pr, 0x10);
		}
		pk += 13;
	next:
		pr += 0x24;
	}
	return (u_long *)pk;
}

/* ---- subdivided variants ----
 * GsTMDdiv* subdivide large near-clip-area primitives into multiple OT
 * entries. We route them through the fast handler bodies - one packet
 * per prim, correct geometry (documented deviation: no subdivision). */

u_long *GsTMDdivTF3NL(TMD_P_TF3 *p, SVECTOR *v, SVECTOR *n, POLY_FT3 *s,
		      u_long cnt, u_long sh, GsOT *o)
{ return GsTMDfastTF3NL(p, v, n, s, cnt, sh, o); }
u_long *GsTMDdivTNF3(TMD_P_TNF3 *p, SVECTOR *v, POLY_FT3 *s, u_long cnt,
		     u_long sh, GsOT *o)
{ return GsTMDfastTNF3(p, v, s, cnt, sh, o); }
u_long *GsTMDdivTG3NL(TMD_P_TG3 *p, SVECTOR *v, SVECTOR *n, POLY_FT3 *s,
		      u_long cnt, u_long sh, GsOT *o)
{ return GsTMDfastTG3NL(p, v, n, s, cnt, sh, o); }
u_long *GsTMDdivTNG3(TMD_P_TNG3 *p, SVECTOR *v, POLY_GT3 *s, u_long cnt,
		     u_long sh, GsOT *o)
{ return GsTMDfastTNG3(p, v, s, cnt, sh, o); }
u_long *GsTMDdivTF4L(TMD_P_TF4 *p, SVECTOR *v, SVECTOR *n, POLY_FT4 *s,
		     u_long cnt, u_long sh, GsOT *o)
{ return GsTMDfastTF4L(p, v, n, s, cnt, sh, o); }
u_long *GsTMDdivTF4NL(TMD_P_TF4 *p, SVECTOR *v, SVECTOR *n, POLY_FT4 *s,
		      u_long cnt, u_long sh, GsOT *o)
{ return GsTMDfastTF4NL(p, v, n, s, cnt, sh, o); }
u_long *GsTMDdivTNF4(TMD_P_TNF4 *p, SVECTOR *v, POLY_FT4 *s, u_long cnt,
		     u_long sh, GsOT *o)
{ return GsTMDfastTNF4(p, v, s, cnt, sh, o); }
u_long *GsTMDdivTG4NL(TMD_P_TG4 *p, SVECTOR *v, SVECTOR *n, POLY_FT4 *s,
		      u_long cnt, u_long sh, GsOT *o)
{ return GsTMDfastTG4NL(p, v, n, s, cnt, sh, o); }
u_long *GsTMDdivTNG4(TMD_P_TNG4 *p, SVECTOR *v, POLY_GT4 *s, u_long cnt,
		     u_long sh, GsOT *o)
{ return GsTMDfastTNG4(p, v, s, cnt, sh, o); }
