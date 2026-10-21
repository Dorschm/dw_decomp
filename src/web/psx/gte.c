/*
 * PS1 GTE (cop2) emulator - nocash psx-spx semantics.
 * Bit-exact FLAG/saturation behaviour; depth division via the
 * unsigned Newton-Raphson hardware table.
 */
#include "gte.h"

GteRegs gte_regs;

/* ---- helpers ---- */

static inline int32_t sx16(uint32_t v) { return (int16_t)(v & 0xffff); }
static inline int32_t sx16h(uint32_t v) { return (int16_t)(v >> 16); }

/* FLAG bits (c[31]): 31 err | 30 mac1+ 29 mac2+ 28 mac3+ 27 mac1- 26 mac2-
 * 25 mac3- | 24 ir1 23 ir2 22 ir3 | 21 rgb r 20 g 19 b | 18 sz3/otz |
 * 17 div over | 16 mac0+ 15 mac0- | 14 sxy2x+ 13 sxy2y+ 12 sx2- 11 sy2- |
 * 10 sx0 9 sy0 8 sx1 7 sy1 6 sx2 5 sy2 - only 14..11 plus 10..5 partially
 * documented; we implement the documented ones. */
static uint32_t flag;

static inline void flag_or(uint32_t b) { flag |= b; }

/* MAC1..3 44-bit result clamp to s32 with flag */
static int32_t set_mac(int n, int64_t v)
{
	if (v > 0x7FFFFFFFFFFLL) flag_or(1u << (31 - n));	    /* macN + */
	if (v < -0x80000000000LL) flag_or(1u << (27 - n));    /* macN - */
	return (int32_t)v;
}

/* IRn saturation: lm=0 -> -0x8000..0x7FFF, lm=1 -> 0..0x7FFF */
static int16_t set_ir(int n, int32_t v, int lm)
{
	int lo = lm ? 0 : -0x8000;
	if (v < lo) { flag_or(1u << (24 - n)); return (int16_t)lo; }
	if (v > 0x7FFF) { flag_or(1u << (24 - n)); return (int16_t)0x7FFF; }
	return (int16_t)v;
}

static uint8_t set_rgb(int c, int32_t v)	/* c: 0=r(bit21) 1=g(20) 2=b(19) */
{
	if (v < 0) { flag_or(1u << (21 - c)); return 0; }
	if (v > 0xFF) { flag_or(1u << (21 - c)); return 0xFF; }
	return (uint8_t)v;
}

static uint16_t set_sz3(int32_t v)
{
	if (v < 0) { flag_or(1u << 18); return 0; }
	if (v > 0xFFFF) { flag_or(1u << 18); return 0xFFFF; }
	return (uint16_t)v;
}

/* web: the grown screen (src/web/host/glgpu.c) extends past the PS1's
 * [-1024,1023] screen range; widen the saturation (and its error flag,
 * which libgs uses to cull polygons) to cover the margins */
/* weak: standalone gte_test links gte.c without the renderer */
__attribute__((weak)) int glgpu_ext;
__attribute__((weak)) int glgpu_exty;

static int16_t set_sxy(int c, int32_t v)	/* c=0:x(bits14/12) c=1:y(bits13/11) */
{
	int32_t m = c ? glgpu_exty : glgpu_ext;
	int32_t hi = 0x3FF, lo = -0x400;

	if (m > 0) {
		if (hi < (c ? 240 : 320) + m + 256) hi = (c ? 240 : 320) + m + 256;
		if (lo > -m - 256) lo = -m - 256;
		if (hi > 0x7FFF) hi = 0x7FFF;
		if (lo < -0x8000) lo = -0x8000;
	}
	if (v > hi) { flag_or(1u << (14 - c)); return (int16_t)hi; }
	if (v < lo) { flag_or(1u << (12 - c)); return (int16_t)lo; }
	return (int16_t)v;
}

/* MAC0 44-bit check */
static void set_mac0(int64_t v)
{
	if (v > 0x7FFFFFFFFFFLL) flag_or(1u << 16);
	if (v < -0x80000000000LL) flag_or(1u << 15);
	gte_regs.d[24] = (uint32_t)v;
}

static void push_sxy(int32_t x, int32_t y)
{
	gte_regs.d[12] = gte_regs.d[13];
	gte_regs.d[13] = gte_regs.d[14];
	gte_regs.d[14] = ((uint16_t)set_sxy(1, y) << 16) | (uint16_t)set_sxy(0, x);
}

static void push_sz(int32_t v)
{
	gte_regs.d[16] = gte_regs.d[17];
	gte_regs.d[17] = gte_regs.d[18];
	gte_regs.d[18] = gte_regs.d[19];
	gte_regs.d[19] = set_sz3(v);	/* SZ3 saturation sets flag 18 */
}

static void push_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	uint32_t code = gte_regs.d[6] & 0xFF000000;
	gte_regs.d[20] = gte_regs.d[21];
	gte_regs.d[21] = gte_regs.d[22];
	gte_regs.d[22] = code | r | (g << 8) | ((uint32_t)b << 16);
}

/* ---- register access side effects ---- */

uint32_t gte_ldr(int idx)
{
	switch (idx) {
	case 28: case 29: {			    /* IRGB / ORGB read */
		int r = gte_regs.d[9], g = gte_regs.d[10], b = gte_regs.d[11];
		if (r < 0) r = 0; if (r > 0x1F) r = 0x1F;
		if (g < 0) g = 0; if (g > 0x1F) g = 0x1F;
		if (b < 0) b = 0; if (b > 0x1F) b = 0x1F;
		return (uint16_t)(r | (g << 5) | (b << 10));
	}
	default:
		return gte_regs.d[idx];
	}
}

void gte_str(int idx, uint32_t v)
{
	switch (idx) {
	case 15:	/* SXYP write pushes fifo */
		push_sxy((int16_t)(v & 0xffff), (int16_t)(v >> 16));
		break;
	case 28: case 29:	/* IRGB/ORGB write -> IR1..3 */
		gte_regs.d[9]  = v & 0x1F;
		gte_regs.d[10] = (v >> 5) & 0x1F;
		gte_regs.d[11] = (v >> 10) & 0x1F;
		break;
	case 30:	/* LZCS write -> LZCR count */
		gte_regs.d[30] = v;
		{
			int n = 0;
			uint32_t x = v;
			if ((int32_t)x < 0) x = ~x;
			if (x == 0xFFFFFFFF && (int32_t)v >= 0) x = 0;
			/* count leading bits equal to sign */
			{
				uint32_t m = ((int32_t)v < 0) ? 0x80000000u : 0;
				uint32_t t = v;
				while (n < 32 && ((t ^ m) & 0x80000000u) == 0) { n++; t <<= 1; }
			}
			gte_regs.d[31] = n;
		}
		break;
	case 31:	/* LZCR read-only */
		break;
	default:
		gte_regs.d[idx] = v;
		break;
	}
}

uint32_t gte_lcr(int idx)
{
	return (idx == 31) ? flag : gte_regs.c[idx];
}

void gte_scr(int idx, uint32_t v)
{
	if (idx == 31) flag = v & 0x7FFFFF00;	/* bits 0..11? kept as-is */
	else gte_regs.c[idx] = v;
}

/* matrix register helpers: ctrl regs store rows packed:
 * c[0] = RT11|RT12<<16, c[1] = RT13|RT21<<16, c[2] = RT22|RT23<<16,
 * c[3] = RT31|RT32<<16, c[4] = RT33 */
static void get_mat(int base, int16_t m[3][3])
{
	m[0][0] = (int16_t)(gte_regs.c[base] & 0xffff);
	m[0][1] = (int16_t)(gte_regs.c[base] >> 16);
	m[0][2] = (int16_t)(gte_regs.c[base + 1] & 0xffff);
	m[1][0] = (int16_t)(gte_regs.c[base + 1] >> 16);
	m[1][1] = (int16_t)(gte_regs.c[base + 2] & 0xffff);
	m[1][2] = (int16_t)(gte_regs.c[base + 2] >> 16);
	m[2][0] = (int16_t)(gte_regs.c[base + 3] & 0xffff);
	m[2][1] = (int16_t)(gte_regs.c[base + 3] >> 16);
	m[2][2] = (int16_t)(gte_regs.c[base + 4] & 0xffff);
}

static void get_vec_cv(int cv, int32_t t[3])
{
	switch (cv) {
	case 0: t[0] = (int32_t)gte_regs.c[5];  t[1] = (int32_t)gte_regs.c[6];  t[2] = (int32_t)gte_regs.c[7];  break;
	case 1: t[0] = (int32_t)gte_regs.c[13]; t[1] = (int32_t)gte_regs.c[14]; t[2] = (int32_t)gte_regs.c[15]; break;
	case 2: t[0] = (int32_t)gte_regs.c[21]; t[1] = (int32_t)gte_regs.c[22]; t[2] = (int32_t)gte_regs.c[23]; break;
	default: t[0] = t[1] = t[2] = 0; break;   /* cv=3: buggy "none" on HW */
	}
}

static void get_vec_v(int v, int32_t out[3])
{
	switch (v) {
	case 0: case 1: case 2: {
		int b = v * 2;
		out[0] = sx16(gte_regs.d[b]); out[1] = sx16h(gte_regs.d[b]); out[2] = sx16(gte_regs.d[b + 1]);
		break;
	}
	case 3:
		out[0] = sx16(gte_regs.d[9]); out[1] = sx16(gte_regs.d[10]); out[2] = sx16(gte_regs.d[11]);
		break;
	}
}

/* The hardware UNR table (psx-spx "GTE Division Inaccuracy"): 257 entries,
 * unr_table[i] = max(0, (0x40000/(i+0x100)+1)/2 - 0x101) for i = 0..256.
 * First entries: FFh,FDh,FBh,F9h,F7h,F5h,F3h,F1h,EFh,... */
static uint16_t unr_table[257];
static int unr_init;
static void unr_table_init(void)
{
	int i;
	if (unr_init) return;
	unr_init = 1;
	for (i = 0; i < 257; i++) {
		int32_t v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
		unr_table[i] = (uint16_t)(v < 0 ? 0 : v);
	}
}

static uint32_t gte_divide(uint16_t h, uint16_t sz3)
{
	/* Hardware unsigned Newton-Raphson division (psx-spx GTE docs). */
	unr_table_init();
	uint32_t n, d, u;
	int z;
	if ((uint32_t)sz3 * 2 <= h) {
		flag_or(1u << 17);	/* division overflow */
		return 0x1FFFF;
	}
	z = 0;
	while (!((sz3 << z) & 0x8000)) z++;	/* leading zeros of sz3 */
	n = (uint32_t)h << z;
	d = (uint32_t)sz3 << z;
	u = (uint32_t)unr_table[(int32_t)(d - 0x7FC0) >> 7] + 0x101;
	d = (int32_t)(0x2000080u - d * u) >> 8;
	d = (int32_t)(0x0000080u + d * u) >> 8;
	n = (uint32_t)(((uint64_t)n * d + 0x8000) >> 16);
	if (n > 0x1FFFF) n = 0x1FFFF;
	return n;
}

/* ---- matrix x vector (RT on vector, 12.12 fixed) ---- */
static void mv_mat_vec(int base, const int32_t v[3], const int32_t tr[3],
		       int sf, int lm, int32_t out[3])
{
	int16_t m[3][3];
	int i, j;
	get_mat(base, m);
	for (i = 0; i < 3; i++) {
		int64_t mac = tr[i];
		mac <<= 12;
		for (j = 0; j < 3; j++)
			mac += (int64_t)m[i][j] * v[j];
		set_mac(i + 1, mac >> sf);
		out[i] = set_ir(i + 1, (int32_t)(mac >> sf), lm);
		gte_regs.d[25 + i] = (uint32_t)(int32_t)(mac >> sf);
		gte_regs.d[9 + i] = (uint16_t)out[i];
	}
}

/* ---- the ops ---- */

static void do_rtps(int idx, int sf, int lm)
{
	int32_t v[3], tr[3], out[3];
	get_vec_v(idx, v);
	get_vec_cv(0, tr);
	mv_mat_vec(0, v, tr, sf, lm, out);	/* MAC1-3 = TR + RT*v, IR1-3 */
	{
		uint16_t h = (uint16_t)gte_regs.c[26];
		uint32_t h_div;
		int64_t m0;
		/* SZ3 = MAC3 SAR ((1-sf)*12) */
		push_sz((int32_t)gte_regs.d[27] >> (12 - sf));
		h_div = gte_divide(h, (uint16_t)gte_regs.d[19]);
		/* SX2: MAC0 = h_div*IR1 + OFX; sx = MAC0>>16 */
		m0 = (int64_t)h_div * sx16(gte_regs.d[9]) +
		     (int32_t)gte_regs.c[24];
		set_mac0(m0);
		{
			int32_t sxx = (int32_t)(m0 >> 16);
			/* SY2: MAC0 = h_div*IR2 + OFY; sy = MAC0>>16 */
			m0 = (int64_t)h_div * sx16(gte_regs.d[10]) +
			     (int32_t)gte_regs.c[25];
			set_mac0(m0);
			push_sxy(sxx, (int32_t)(m0 >> 16));
		}
		/* IR0: MAC0 = h_div*DQA + DQB; IR0 = MAC0>>12 sat 0..1000h */
		m0 = (int64_t)h_div * (int16_t)gte_regs.c[27] +
		     (int32_t)gte_regs.c[28];
		set_mac0(m0);
		{
			int32_t ir0 = (int32_t)(m0 >> 12);
			if (ir0 < 0) ir0 = 0;
			if (ir0 > 0x1000) ir0 = 0x1000;
			gte_regs.d[8] = (uint16_t)ir0;
		}
	}
}

static void do_nclip(void)
{
	int64_t m = ((int64_t)gte_sx(0) * gte_sy(1) +
		     (int64_t)gte_sx(1) * gte_sy(2) +
		     (int64_t)gte_sx(2) * gte_sy(0)) -
		    ((int64_t)gte_sx(0) * gte_sy(2) +
		     (int64_t)gte_sx(1) * gte_sy(0) +
		     (int64_t)gte_sx(2) * gte_sy(1));
	set_mac0(m);
}

static void do_op(int sf, int lm)
{
	int64_t d1 = sx16(gte_regs.c[0]), d2 = sx16(gte_regs.c[2]),
		d3 = sx16(gte_regs.c[4]);
	int64_t ir1 = sx16(gte_regs.d[9]), ir2 = sx16(gte_regs.d[10]),
		ir3 = sx16(gte_regs.d[11]);
	int64_t m1 = (d2 * ir3 - d3 * ir2) >> sf;
	int64_t m2 = (d3 * ir1 - d1 * ir3) >> sf;
	int64_t m3 = (d1 * ir2 - d2 * ir1) >> sf;
	gte_regs.d[25] = (uint32_t)set_mac(1, m1);
	gte_regs.d[26] = (uint32_t)set_mac(2, m2);
	gte_regs.d[27] = (uint32_t)set_mac(3, m3);
	gte_regs.d[9]  = set_ir(1, (int32_t)m1, lm);
	gte_regs.d[10] = set_ir(2, (int32_t)m2, lm);
	gte_regs.d[11] = set_ir(3, (int32_t)m3, lm);
}

static void do_avsz(int n)
{
	int64_t m;
	int32_t zsf = sx16(gte_regs.c[n == 3 ? 29 : 30]);	/* ZSFn are signed */
	if (n == 3)
		m = (int64_t)zsf * ((uint16_t)gte_regs.d[17] + (uint16_t)gte_regs.d[18] + (uint16_t)gte_regs.d[19]);
	else
		m = (int64_t)zsf * ((uint16_t)gte_regs.d[16] + (uint16_t)gte_regs.d[17] + (uint16_t)gte_regs.d[18] + (uint16_t)gte_regs.d[19]);
	set_mac0(m);
	gte_regs.d[7] = set_sz3((int32_t)(m >> 12));
}

static void do_sqr(int sf, int lm)
{
	int i;
	for (i = 0; i < 3; i++) {
		int64_t ir = sx16(gte_regs.d[9 + i]);
		int64_t m = (ir * ir) >> sf;
		gte_regs.d[25 + i] = (uint32_t)set_mac(i + 1, m);
		gte_regs.d[9 + i] = set_ir(i + 1, (int32_t)m, lm);
	}
}

static void do_gpf_gpl(int is_gpl, int sf, int lm)
{
	int i;
	for (i = 0; i < 3; i++) {
		int64_t ir0 = sx16(gte_regs.d[8]);
		int64_t irn = sx16(gte_regs.d[9 + i]);
		int64_t m = ir0 * irn;
		if (is_gpl) m += (int64_t)(int32_t)gte_regs.d[25 + i] << sf;
		m >>= sf;
		gte_regs.d[25 + i] = (uint32_t)set_mac(i + 1, m);
		gte_regs.d[9 + i] = set_ir(i + 1, (int32_t)m, lm);
	}
	push_rgb(set_rgb(0, (int32_t)gte_regs.d[25] >> 4),
		 set_rgb(1, (int32_t)gte_regs.d[26] >> 4),
		 set_rgb(2, (int32_t)gte_regs.d[27] >> 4));
}

/* Depth cue step, per spec "MAC+(FC-MAC)*IR0":
 *   IRn' = ((FCn<<12) - MACn) SAR (sf*12)     (lm=0 saturation, flags set)
 *   MACn = IRn'*IR0 + MACn                     (44-bit check, no shift here)
 * Final: MACn SAR (sf*12), IRn = sat(MACn) per lm.
 * base(c) supplies the pre-cue MAC value for each channel. */
static void depth_cue(int sf, int lm, int64_t base[3])
{
	int c;
	for (c = 0; c < 3; c++) {
		int64_t fc = (int32_t)gte_regs.c[21 + c];
		int32_t t = (int32_t)(((fc << 12) - base[c]) >> sf);
		int64_t m;
		t = set_ir(c + 1, t, 0);
		m = (int64_t)t * sx16(gte_regs.d[8]) + base[c];
		gte_regs.d[25 + c] = (uint32_t)set_mac(c + 1, m);
	}
	for (c = 0; c < 3; c++) {
		int64_t m = (int32_t)gte_regs.d[25 + c] >> sf;
		gte_regs.d[25 + c] = (uint32_t)set_mac(c + 1, m);
		gte_regs.d[9 + c] = set_ir(c + 1, (int32_t)m, lm);
	}
	push_rgb(set_rgb(0, (int32_t)gte_regs.d[25] >> 4),
		 set_rgb(1, (int32_t)gte_regs.d[26] >> 4),
		 set_rgb(2, (int32_t)gte_regs.d[27] >> 4));
}

/* DPCS (single) / DPCT (triple, reads RGB0) / INTPL (IR<<12) */
static void do_dpcs_dctp(int mode, int single, int sf, int lm)
{
	int count = single ? 1 : 3;
	int i;
	for (i = 0; i < count; i++) {
		int64_t base[3];
		int c;
		for (c = 0; c < 3; c++) {
			if (mode == 1)			/* INTPL: IRn<<12 */
				base[c] = (int64_t)sx16(gte_regs.d[9 + c]) << 12;
			else {				/* DPCS/DPCT: Rn<<16 */
				uint32_t rgb = single ? gte_regs.d[6] : gte_regs.d[20];
				base[c] = (int64_t)((rgb >> (c * 8)) & 0xFF) << 16;
			}
		}
		depth_cue(sf, lm, base);
	}
}

/* Normal-color stages, per spec:
 *   nc_llm:    IR = MAC = (LLM*Vn) SAR (sf*12)
 *   nc_lcm:    IR = MAC = (BK*1000h + LCM*IR) SAR (sf*12)
 *   nc_colmul: MAC = [R*IR1,G*IR2,B*IR3] SHL 4 (unshifted base; caller SARs)
 * RGB FIFO push = MACn>>4 saturated 0..FFh, CODE taken from RGBC. */
static void nc_llm(int v, int sf)
{
	int16_t lm_m[3][3];
	int32_t norm[3];
	int i, j;
	get_mat(8, lm_m);
	get_vec_v(v, norm);
	for (i = 0; i < 3; i++) {
		int64_t m = 0;
		for (j = 0; j < 3; j++) m += (int64_t)lm_m[i][j] * norm[j];
		m >>= sf;
		gte_regs.d[25 + i] = (uint32_t)set_mac(i + 1, m);
		gte_regs.d[9 + i] = set_ir(i + 1, (int32_t)m, 1);
	}
}

static void nc_lcm(int sf, int lm)
{
	int16_t lc_m[3][3];
	int32_t lv[3];
	int i, j;
	get_mat(16, lc_m);
	for (i = 0; i < 3; i++) lv[i] = sx16(gte_regs.d[9 + i]);
	for (i = 0; i < 3; i++) {
		int64_t m = ((int64_t)(int32_t)gte_regs.c[13 + i] << 12);
		for (j = 0; j < 3; j++) m += (int64_t)lc_m[i][j] * lv[j];
		m >>= sf;
		gte_regs.d[25 + i] = (uint32_t)set_mac(i + 1, m);
		gte_regs.d[9 + i] = set_ir(i + 1, (int32_t)m, lm);
	}
}

static void nc_colmul(uint32_t rgb, int sf, int lm)
{
	int c;
	for (c = 0; c < 3; c++) {
		int64_t col = (rgb >> (c * 8)) & 0xFF;
		int64_t m = ((col * sx16(gte_regs.d[9 + c])) << 4) >> sf;
		gte_regs.d[25 + c] = (uint32_t)set_mac(c + 1, m);
		gte_regs.d[9 + c] = set_ir(c + 1, (int32_t)m, lm);
	}
}

/* unshifted variant for the depth-cued ops (NCDx/CDP/DCPL) */
static void nc_colmul_raw(uint32_t rgb)
{
	int c;
	for (c = 0; c < 3; c++) {
		int64_t col = (rgb >> (c * 8)) & 0xFF;
		int64_t m = (col * sx16(gte_regs.d[9 + c])) << 4;
		gte_regs.d[25 + c] = (uint32_t)set_mac(c + 1, m);
	}
}

static void nc_push_rgb(void)
{
	push_rgb(set_rgb(0, (int32_t)gte_regs.d[25] >> 4),
		 set_rgb(1, (int32_t)gte_regs.d[26] >> 4),
		 set_rgb(2, (int32_t)gte_regs.d[27] >> 4));
}

/* NCDx/CDP/DCPL: MAC = MAC + (FC-MAC)*IR0 using current (unshifted) MACs */
static void do_ncd_depthcue(int sf, int lm)
{
	int64_t base[3];
	int c;
	for (c = 0; c < 3; c++) base[c] = (int32_t)gte_regs.d[25 + c];
	depth_cue(sf, lm, base);
}

void gte_cmd(uint32_t word)
{
	int sf = (word >> 19) & 1 ? 12 : 0;
	int lm = (word >> 10) & 1;
	int mx = (word >> 17) & 3;
	int v  = (word >> 15) & 3;
	int cv = (word >> 13) & 3;
	int op = word & 0x3F;

	flag = 0;
	switch (op) {
	case 0x01: /* RTPS */
		do_rtps(v, sf, lm);
		break;
	case 0x30: /* RTPT */
		do_rtps(0, sf, lm);
		do_rtps(1, sf, lm);
		do_rtps(2, sf, lm);
		break;
	case 0x06: do_nclip(); break;
	case 0x0C: do_op(sf, lm); break;
	case 0x10: do_dpcs_dctp(1, 1, sf, lm); break;		/* DPCS */
	case 0x11: do_dpcs_dctp(0, 1, sf, lm); break;		/* INTPL */
	case 0x12: {						/* MVMVA */
		int base = mx == 0 ? 0 : mx == 1 ? 8 : mx == 2 ? 16 : -1;
		int32_t vv[3], tr[3], out[3];
		if (mx == 3) { flag_or(1u << 31); break; }
		get_vec_cv(cv, tr);
		if (v == 3) {
			/* mx x IR vector */
			vv[0] = sx16(gte_regs.d[9]); vv[1] = sx16(gte_regs.d[10]); vv[2] = sx16(gte_regs.d[11]);
		} else get_vec_v(v, vv);
		mv_mat_vec(base, vv, tr, sf, lm, out);
		break;
	}
	case 0x13:	/* NCDS */
		nc_llm(v, sf); nc_lcm(sf, lm);
		nc_colmul_raw(gte_regs.d[6]); do_ncd_depthcue(sf, lm);
		break;
	case 0x14:	/* CDP: BK+LCM*IR, col*IR<<4, depth cue */
		nc_lcm(sf, lm);
		nc_colmul_raw(gte_regs.d[6]); do_ncd_depthcue(sf, lm);
		break;
	case 0x16:	/* NCDT */
		{ int t; for (t = 0; t < 3; t++) {
			nc_llm(t, sf); nc_lcm(sf, lm);
			nc_colmul_raw(gte_regs.d[6]); do_ncd_depthcue(sf, lm);
		} }
		break;
	case 0x1B:	/* NCCS */
		nc_llm(v, sf); nc_lcm(sf, lm); nc_colmul(gte_regs.d[6], sf, lm);
		nc_push_rgb();
		break;
	case 0x1C:	/* CC: BK+LCM*IR then color */
		nc_lcm(sf, lm); nc_colmul(gte_regs.d[6], sf, lm);
		nc_push_rgb();
		break;
	case 0x1E:	/* NCS */
		nc_llm(v, sf); nc_lcm(sf, lm); nc_push_rgb();
		break;
	case 0x20:	/* NCT */
		{ int t; for (t = 0; t < 3; t++) { nc_llm(t, sf); nc_lcm(sf, lm); nc_push_rgb(); } }
		break;
	case 0x28: do_sqr(sf, lm); break;
	case 0x29:	/* DCPL: MAC=RGB*IR<<4, then depth cue */
		nc_colmul_raw(gte_regs.d[6]); do_ncd_depthcue(sf, lm);
		break;
	case 0x2A: do_dpcs_dctp(0, 0, sf, lm); break;	/* DPCT */
	case 0x2D: do_avsz(3); break;
	case 0x2E: do_avsz(4); break;
	case 0x3D: do_gpf_gpl(0, sf, lm); break;
	case 0x3E: do_gpf_gpl(1, sf, lm); break;
	case 0x3F:	/* NCCT */
		{ int t; for (t = 0; t < 3; t++) { nc_llm(t, sf); nc_lcm(sf, lm); nc_colmul(gte_regs.d[6], sf, lm); nc_push_rgb(); } }
		break;
	default:
		break;
	}
	/* FLAG bit31 = OR of all error bits */
	if (flag & 0x7F87E000) flag |= 0x80000000u;
	gte_regs.c[31] = flag;
}

/* ---- backing functions for inline_n.h macros ---- */

void gte_ldv0_c(const void *r0) { gte_ld_svector(0, r0); }
void gte_ldv3_c(const void *r0, const void *r1, const void *r2)
{
	gte_ld_svector(0, r0); gte_ld_svector(1, r1); gte_ld_svector(2, r2);
}
void gte_ldlzc_c(int32_t r0) { gte_str(30, (uint32_t)r0); }
void gte_stlzc_c(void *r0) { *(uint32_t *)r0 = gte_ldr(31); }
void gte_rtps_c(void) { gte_cmd(GTE_RTPS); }
void gte_rtpt_c(void) { gte_cmd(GTE_RTPT); }
void gte_stsxy_c(void *r0) { *(uint32_t *)r0 = gte_ldr(14); }
void gte_stsxy3_c(void *r0, void *r1, void *r2)
{
	*(uint32_t *)r0 = gte_ldr(12);
	*(uint32_t *)r1 = gte_ldr(13);
	*(uint32_t *)r2 = gte_ldr(14);
}
void gte_stszotz_c(void *r0) { *(uint32_t *)r0 = gte_ldr(19) >> 2; }
void gte_SetRotMatrix_c(const void *r0)
{
	const uint16_t *m = (const uint16_t *)r0;   /* MATRIX: 3x3 s16 + pad + t */
	int i;
	for (i = 0; i < 5; i++) {
		gte_regs.c[i] = (uint32_t)m[i * 2] | ((uint32_t)m[i * 2 + 1] << 16);
	}
}
void gte_SetTransMatrix_c(const void *r0)
{
	const int32_t *m = (const int32_t *)r0;
	gte_regs.c[5] = (uint32_t)m[5];
	gte_regs.c[6] = (uint32_t)m[6];
	gte_regs.c[7] = (uint32_t)m[7];
}
