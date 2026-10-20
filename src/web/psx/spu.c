/* Software SPU emulation per nocash psx-spx SPU docs.
 * Register file 0x1F801C00-0x1F801DFF, 512 KiB SPU RAM, ADPCM voice
 * engine with ADSR + Gaussian interpolation, noise, reverb. */

#include <string.h>

#include <emscripten.h>

#include "spu.h"

#include <emscripten/console.h>

uint8_t spu_ram[SPU_RAM_SIZE];
unsigned long spu_kon_events;
unsigned long spu_samples_rendered;

/* ---- register file ---- */

static uint16_t regs[0x100]; /* word-indexed 0x1F801C00..0x1F801DFF */

enum {
	/* voice regs: base v*8 (16-bit units) */
	R_VOLL = 0x0, R_VOLR, R_PITCH, R_START, R_ADSR1, R_ADSR2,
	R_ENVX, R_REPEAT,
	R_MVOLL = 0xC0, R_MVOLR, R_RVOLL, R_RVOLR,
	R_KONL = 0xC4, R_KONH, R_KOFFL, R_KOFFH,
	R_PMONL = 0xC8, R_PMONH, R_NONL, R_NONH,
	R_EONL = 0xCC, R_EONH, R_ENDXL = 0xCE, R_ENDXH,
	R_REVB_A = 0xD1, /* 0x1A2 */
	R_IRQA = 0xD2, R_TSA = 0xD3, R_FIFO = 0xD4, R_SPUCNT = 0xD5,
	R_TRANSCTL = 0xD6, R_SPUSTAT = 0xD7,
	R_CDVOLL = 0xD8, R_CDVOLR, R_EXTVOLL, R_EXTVOLR,
	R_MVXL = 0xDC, R_MVXR,
	R_REVBASE = 0xE0, /* 0x1C0..0x1FF */
};

/* ---- voices ---- */

enum { ADSR_OFF, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE };

typedef struct {
	/* decoded sample state */
	uint32_t counter;      /* 12.4? psx pitch counter (bit12=sample) */
	uint32_t addr;         /* current SPU byte address (block base) */
	uint32_t repeat;       /* loop addr (bytes) */
	int16_t  buf[28 + 4];  /* current block + 4-sample history */
	int      bufidx;       /* position of 'new' sample in buf */
	int      s_1, s_2;     /* adpcm history */
	int16_t  sample;       /* current interpolated sample */
	/* env */
	int      state;
	int32_t  env;          /* 15-bit level */
	uint32_t envctr;
	int      adsr_stall;   /* all-ones rate -> never step */
	/* misc */
	int      on;
	uint32_t pmon_last;    /* previous output for PMON */
	int32_t  vol_l, vol_r; /* current voice volume (sweep state) */
} Voice;

static Voice voice[24];
static uint32_t kon_pending, koff_pending;
static uint16_t noise_lfsr = 1;
static uint32_t rev_addr;          /* current reverb buffer byte addr */

/* ---- ADPCM ---- */

static const int adpcm_filt[5][2] = {
	{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}
};

/* decode one 16-byte block at 'addr' into v->buf (keeps 4 history
 * samples at buf[0..3], new data at buf[4..31]). Returns flag byte. */
static int decode_block(Voice *v)
{
	const uint8_t *b = spu_ram + (v->addr & 0x7FFF8);
	int shift = b[0] & 0x0F;
	int filt = (b[0] >> 4) & 7;
	int flags = b[1];
	int f0 = adpcm_filt[filt & 7][0];
	int f1 = adpcm_filt[filt & 7][1];
	int i;

	memmove((unsigned char *)v->buf, (unsigned char *)(v->buf + 28),
		4 * sizeof(int16_t));
	for (i = 0; i < 14; i++) {
		int d = b[2 + i];
		int j;
		for (j = 0; j < 2; j++) {
			int s = (j ? (d >> 4) : d) & 0x0F;
			int val;
			if (s >= 8)
				s -= 16;
			val = (s << 12) >> shift;
			val += (f0 * v->s_1 + f1 * v->s_2 + 32) >> 6;
			if (val > 32767)
				val = 32767;
			else if (val < -32768)
				val = -32768;
			v->s_2 = v->s_1;
			v->s_1 = val;
			v->buf[4 + i * 2 + j] = (int16_t)val;
		}
	}
	if (flags & 4)
		v->repeat = v->addr;
	if (flags & 1) {
		regs[R_ENDXL + (v - voice >= 16)] |=
			1u << ((v - voice) & 15);
		v->addr = v->repeat;
		if (!(flags & 2)) {   /* End+Mute */
			v->state = ADSR_RELEASE;
			v->env = 0;
		}
	} else
		v->addr += 16;
	v->addr &= 0x7FFFF;
	return flags;
}

/* ---- Gaussian interpolation (psx-spx table) ---- */

static const int16_t gauss[512] = {
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
	0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
	0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
	0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
	0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
	0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
	0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
	0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
	0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
	0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
	0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
	0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
	0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
	0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
	0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
	0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
	0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
	0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
	0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
	0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
	0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
	0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
	0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
	0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
	0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
	0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
	0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
	0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
	0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
	0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
	0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
	0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
	0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
	0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
	0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
	0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
	0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
	0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
	0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
	0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
	0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
	0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
	0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
	0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
	0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
	0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
	0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
	0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
	0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
	0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
	0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
	0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
	0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
	0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
	0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
	0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
	0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
	0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
	0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
	0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
	0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
	0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

/* ---- ADSR ---- */

/* one 44.1 kHz step of the envelope generator, per the spec formula */
static void adsr_step(Voice *v, int vi)
{
	uint16_t adsr1 = regs[vi * 8 + R_ADSR1];
	uint16_t adsr2 = regs[vi * 8 + R_ADSR2];
	int shift = 0, stepval = 0, exp = 0, dec = 0, step, ctr_inc;
	int32_t sus_level;

	switch (v->state) {
	case ADSR_ATTACK:
		shift = (adsr1 >> 10) & 0x1F;
		stepval = (adsr1 >> 8) & 3;
		exp = adsr1 & 0x8000;
		dec = 0;
		break;
	case ADSR_DECAY:
		shift = (adsr1 >> 4) & 0x0F;
		stepval = 0;
		exp = 1;
		dec = 1;
		break;
	case ADSR_SUSTAIN:
		shift = (adsr2 >> 8) & 0x1F;
		stepval = (adsr2 >> 6) & 3;
		exp = adsr2 & 0x40;
		dec = adsr2 & 0x20;
		break;
	case ADSR_RELEASE:
		shift = adsr2 & 0x1F;
		stepval = 0;
		exp = adsr2 & 0x20;
		dec = 1;
		break;
	default:
		return;
	}

	step = 7 - stepval;
	if (dec)
		step = ~step;          /* +7..+4 -> -8..-5 */
	step <<= (shift < 11) ? 11 - shift : 0;
	ctr_inc = 0x8000 >> (shift > 11 ? shift - 11 : 0);

	if (exp && !dec && v->env > 0x6000) {
		if (shift < 10)
			step >>= 2;
		else if (shift >= 11) {
			step >>= 1;
			ctr_inc >>= 1;
		} else
			ctr_inc >>= 2;
	} else if (exp && dec)
		step = (int32_t)step * v->env / 0x8000;

	if ((stepval | (shift << 2)) != 0x7F && ctr_inc == 0)
		ctr_inc = 1;
	if (ctr_inc == 0)
		return;

	v->envctr += ctr_inc;
	if (!(v->envctr & 0x8000))
		return;
	v->envctr = 0;

	v->env += step;
	if (!dec) {
		if (v->env > 0x7FFF)
			v->env = 0x7FFF;
		if (v->env < -0x8000)
			v->env = -0x8000;
	} else if (v->env < 0)
		v->env = 0;

	/* phase transitions */
	switch (v->state) {
	case ADSR_ATTACK:
		if (v->env >= 0x7FFF) {
			v->env = 0x7FFF;
			v->state = ADSR_DECAY;
		}
		break;
	case ADSR_DECAY:
		sus_level = ((adsr1 & 0x0F) + 1) << 11;
		if (v->env <= sus_level)
			v->state = ADSR_SUSTAIN;
		break;
	default:
		break;
	}
}

/* ---- noise ---- */

static int16_t noise_sample(void)
{
	/* SPU LFSR per spec: taps on bit4 of bit15.. */
	uint32_t parity = __builtin_popcount(noise_lfsr & 0x2417) & 1;
	noise_lfsr = (uint16_t)((noise_lfsr << 1) | (parity ^ 1));
	return (int16_t)noise_lfsr;
}

/* ---- reverb ---- */

static int16_t rv_read(int32_t rel)
{
	uint32_t a = (rev_addr + rel) & 0x7FFFF;
	if (a < (uint32_t)regs[R_REVB_A] * 8)
		a += 0x80000 - (uint32_t)regs[R_REVB_A] * 8;
	a &= ~1u;
	return (int16_t)(spu_ram[a] | (spu_ram[a + 1] << 8));
}

static void rv_write(int32_t rel, int32_t v)
{
	uint32_t a = (rev_addr + rel) & 0x7FFFF;
	if (a < (uint32_t)regs[R_REVB_A] * 8)
		a += 0x80000 - (uint32_t)regs[R_REVB_A] * 8;
	a &= ~1u;
	if (v > 32767)
		v = 32767;
	else if (v < -32768)
		v = -32768;
	if (regs[R_SPUCNT] & 0x80)   /* reverb master enable */
	{
		spu_ram[a] = (uint8_t)v;
		spu_ram[a + 1] = (uint8_t)(v >> 8);
	}
}

#define RREG(i) ((int16_t)regs[R_REVBASE + (i)])
enum {
	RV_dAPF1, RV_dAPF2, RV_vIIR, RV_vCOMB1, RV_vCOMB2, RV_vCOMB3,
	RV_vCOMB4, RV_vWALL, RV_vAPF1, RV_vAPF2, RV_mLSAME, RV_mRSAME,
	RV_mLCOMB1, RV_mRCOMB1, RV_mLCOMB2, RV_mRCOMB2, RV_dLSAME,
	RV_dRSAME, RV_mLDIFF, RV_mRDIFF, RV_mLCOMB3, RV_mRCOMB3,
	RV_mLCOMB4, RV_mRCOMB4, RV_dLDIFF, RV_dRDIFF, RV_mLAPF1,
	RV_mRAPF1, RV_mLAPF2, RV_mRAPF2, RV_vLIN, RV_vRIN
};

static void reverb_step(int32_t lin_in, int32_t rin_in,
			int32_t *lout, int32_t *rout)
{
	int32_t lin = lin_in * RREG(RV_vLIN) >> 15;
	int32_t rin = rin_in * RREG(RV_vRIN) >> 15;
	int32_t t, lo, ro;

	/* same-side reflections */
	t = lin + ((int32_t)rv_read(RREG(RV_dLSAME) * 8) * RREG(RV_vWALL) >> 15)
		- rv_read(RREG(RV_mLSAME) * 8 - 2);
	t = (t * RREG(RV_vIIR) >> 15) + rv_read(RREG(RV_mLSAME) * 8 - 2);
	rv_write(RREG(RV_mLSAME) * 8, t);
	int32_t l_same = t;
	t = rin + ((int32_t)rv_read(RREG(RV_dRSAME) * 8) * RREG(RV_vWALL) >> 15)
		- rv_read(RREG(RV_mRSAME) * 8 - 2);
	t = (t * RREG(RV_vIIR) >> 15) + rv_read(RREG(RV_mRSAME) * 8 - 2);
	rv_write(RREG(RV_mRSAME) * 8, t);
	int32_t r_same = t;
	/* different-side reflections */
	t = lin + ((int32_t)rv_read(RREG(RV_dRDIFF) * 8) * RREG(RV_vWALL) >> 15)
		- rv_read(RREG(RV_mLDIFF) * 8 - 2);
	t = (t * RREG(RV_vIIR) >> 15) + rv_read(RREG(RV_mLDIFF) * 8 - 2);
	rv_write(RREG(RV_mLDIFF) * 8, t);
	int32_t l_diff = t;
	t = rin + ((int32_t)rv_read(RREG(RV_dLDIFF) * 8) * RREG(RV_vWALL) >> 15)
		- rv_read(RREG(RV_mRDIFF) * 8 - 2);
	t = (t * RREG(RV_vIIR) >> 15) + rv_read(RREG(RV_mRDIFF) * 8 - 2);
	rv_write(RREG(RV_mRDIFF) * 8, t);
	int32_t r_diff = t;
	/* comb filter */
	lo = ((int32_t)RREG(RV_vCOMB1) * rv_read(RREG(RV_mLCOMB1) * 8) +
	      (int32_t)RREG(RV_vCOMB2) * rv_read(RREG(RV_mLCOMB2) * 8) +
	      (int32_t)RREG(RV_vCOMB3) * rv_read(RREG(RV_mLCOMB3) * 8) +
	      (int32_t)RREG(RV_vCOMB4) * rv_read(RREG(RV_mLCOMB4) * 8)) >> 15;
	ro = ((int32_t)RREG(RV_vCOMB1) * rv_read(RREG(RV_mRCOMB1) * 8) +
	      (int32_t)RREG(RV_vCOMB2) * rv_read(RREG(RV_mRCOMB2) * 8) +
	      (int32_t)RREG(RV_vCOMB3) * rv_read(RREG(RV_mRCOMB3) * 8) +
	      (int32_t)RREG(RV_vCOMB4) * rv_read(RREG(RV_mRCOMB4) * 8)) >> 15;
	/* comb buffers are fed from the four reflections */
	rv_write(RREG(RV_mLCOMB1) * 8, l_same);
	rv_write(RREG(RV_mRCOMB1) * 8, r_same);
	rv_write(RREG(RV_mLCOMB2) * 8, r_same);
	rv_write(RREG(RV_mRCOMB2) * 8, l_same);
	rv_write(RREG(RV_mLCOMB3) * 8, l_diff);
	rv_write(RREG(RV_mRCOMB3) * 8, r_diff);
	rv_write(RREG(RV_mLCOMB4) * 8, r_diff);
	rv_write(RREG(RV_mRCOMB4) * 8, l_diff);
	/* APF1 */
	lo -= (int32_t)RREG(RV_vAPF1) * rv_read(RREG(RV_mLAPF1) * 8 - RREG(RV_dAPF1) * 8) >> 15;
	rv_write(RREG(RV_mLAPF1) * 8, lo);
	lo = (lo * RREG(RV_vAPF1) >> 15) + rv_read(RREG(RV_mLAPF1) * 8 - RREG(RV_dAPF1) * 8);
	ro -= (int32_t)RREG(RV_vAPF1) * rv_read(RREG(RV_mRAPF1) * 8 - RREG(RV_dAPF1) * 8) >> 15;
	rv_write(RREG(RV_mRAPF1) * 8, ro);
	ro = (ro * RREG(RV_vAPF1) >> 15) + rv_read(RREG(RV_mRAPF1) * 8 - RREG(RV_dAPF1) * 8);
	/* APF2 */
	lo -= (int32_t)RREG(RV_vAPF2) * rv_read(RREG(RV_mLAPF2) * 8 - RREG(RV_dAPF2) * 8) >> 15;
	rv_write(RREG(RV_mLAPF2) * 8, lo);
	lo = (lo * RREG(RV_vAPF2) >> 15) + rv_read(RREG(RV_mLAPF2) * 8 - RREG(RV_dAPF2) * 8);
	ro -= (int32_t)RREG(RV_vAPF2) * rv_read(RREG(RV_mRAPF2) * 8 - RREG(RV_dAPF2) * 8) >> 15;
	rv_write(RREG(RV_mRAPF2) * 8, ro);
	ro = (ro * RREG(RV_vAPF2) >> 15) + rv_read(RREG(RV_mRAPF2) * 8 - RREG(RV_dAPF2) * 8);
	/* echo output volume */
	*lout = lo * (int16_t)regs[R_RVOLL] >> 15;
	*rout = ro * (int16_t)regs[R_RVOLR] >> 15;
}

/* ---- register interface ---- */

void spu_write_reg16(uint32_t addr, uint16_t val)
{
	uint32_t r = (addr - 0x1F801C00) >> 1;
	if (r >= 0x100)
		return;
	regs[r] = val;
	switch (r) {
	case R_KONL:
		kon_pending |= val & 0xFFFF;
		break;
	case R_KONH:
		kon_pending |= (uint32_t)val << 16;
		break;
	case R_KOFFL:
		koff_pending |= val & 0xFFFF;
		break;
	case R_KOFFH:
		koff_pending |= (uint32_t)val << 16;
		break;
	case R_MVXL:
	case R_MVXR:
		break;
	default:
		break;
	}
	/* mirror current main volume (RO on HW but we track writes) */
	regs[R_MVXL] = regs[R_MVOLL];
	regs[R_MVXR] = regs[R_MVOLR];
}

uint16_t spu_read_reg16(uint32_t addr)
{
	uint32_t r = (addr - 0x1F801C00) >> 1;
	int i;
	if (r >= 0x100)
		return 0;
	/* ENVX reads return live env level */
	for (i = 0; i < 24; i++)
		if (r == (uint32_t)(i * 8 + R_ENVX))
			return (uint16_t)voice[i].env;
	return regs[r];
}

void spu_dma_write(uint32_t spu_addr, const void *src, uint32_t len)
{
	if (spu_addr >= SPU_RAM_SIZE)
		return;
	if (len > SPU_RAM_SIZE - spu_addr)
		len = SPU_RAM_SIZE - spu_addr;
	memcpy(spu_ram + spu_addr, src, len);
}

void spu_dma_read(uint32_t spu_addr, void *dst, uint32_t len)
{
	if (spu_addr >= SPU_RAM_SIZE)
		return;
	if (len > SPU_RAM_SIZE - spu_addr)
		len = SPU_RAM_SIZE - spu_addr;
	memcpy(dst, spu_ram + spu_addr, len);
}

/* ---- render ---- */

void spu_reset(void)
{
	memset((unsigned char *)regs, 0, sizeof regs);
	memset((unsigned char *)voice, 0, sizeof voice);
	memset(spu_ram, 0, sizeof spu_ram);
	kon_pending = koff_pending = 0;
	rev_addr = 0;
	regs[R_SPUSTAT] = 0; /* "HAL?": keep simple */
}

void spu_render(int16_t *out, int frames)
{
	int f, vi;
	static int rev_phase;

	for (f = 0; f < frames; f++) {
		int32_t mix_l = 0, mix_r = 0, rev_l = 0, rev_r = 0;
		uint32_t pmon = (uint32_t)regs[R_PMONL] |
				((uint32_t)regs[R_PMONH] << 16);
		uint32_t non = (uint32_t)regs[R_NONL] |
			       ((uint32_t)regs[R_NONH] << 16);
		uint32_t eon = (uint32_t)regs[R_EONL] |
			       ((uint32_t)regs[R_EONH] << 16);

		/* apply pending KON/KOFF */
		if (kon_pending | koff_pending) {
			uint32_t m;
			for (vi = 0; vi < 24; vi++) {
				m = 1u << vi;
				if (kon_pending & m) {
					Voice *v = &voice[vi];
					v->addr = (uint32_t)
						regs[vi * 8 + R_START] * 8;
					v->repeat = v->addr;
					v->counter = 0;
					v->s_1 = v->s_2 = 0;
					v->env = 0;
					v->envctr = 0;
					v->state = ADSR_ATTACK;
					v->on = 1;
					v->bufidx = 4;
					memset((unsigned char *)v->buf, 0, sizeof v->buf);
					decode_block(v);
					regs[R_ENDXL + (vi >= 16)] &=
						~(1u << (vi & 15));
					spu_kon_events++;
					if (spu_kon_events <= 24) {
						emscripten_console_logf(
						"spu: KON v%d start=%x "
						"pitch=%x adsr=%04x/%04x\n",
						vi, v->addr,
						regs[vi * 8 + R_PITCH],
						regs[vi * 8 + R_ADSR1],
						regs[vi * 8 + R_ADSR2]);
						if (spu_kon_events <= 4) {
							emscripten_console_logf(
							"spu: blk %02x %02x %02x %02x "
							"%02x %02x %02x %02x dec %d %d "
							"%d %d %d %d\n",
							spu_ram[v->addr],
							spu_ram[v->addr+1],
							spu_ram[v->addr+2],
							spu_ram[v->addr+3],
							spu_ram[v->addr+4],
							spu_ram[v->addr+5],
							spu_ram[v->addr+6],
							spu_ram[v->addr+7],
							v->buf[4], v->buf[8],
							v->buf[12], v->buf[16],
							v->buf[20], v->buf[24]);
						}
					}
				}
				if (koff_pending & m) {
					voice[vi].state = ADSR_RELEASE;
					if (spu_kon_events <= 24)
						emscripten_console_logf(
						"spu: KOFF v%d\n", vi);
				}
			}
			kon_pending = koff_pending = 0;
		}

		for (vi = 0; vi < 24; vi++) {
			Voice *v = &voice[vi];
			uint32_t step;
			int i;
			int32_t s, vl, vr;

			if (!v->on)
				continue;
			adsr_step(v, vi);
			v->env &= 0xFFFF;
			step = regs[vi * 8 + R_PITCH];
			if (pmon & (2u << vi) && vi > 0)
				step = (uint16_t)((int32_t)step *
					(voice[vi - 1].pmon_last)) >> 15;
			if (step > 0x3FFF)
				step = 0x4000;
			v->counter += step;
			if (v->counter & 0x1000) { /* wrapped into next sample */
				v->counter &= 0xFFF;
				v->bufidx++;
				if (v->bufidx >= 32)
					decode_block(v), v->bufidx = 4;
			}
			/* gaussian interp */
			i = (v->counter >> 4) & 0xFF;
			{
				const int16_t *b = &v->buf[v->bufidx - 3];
				s = ((int32_t)gauss[0xFF - i] * b[0] +
				     (int32_t)gauss[0x1FF - i] * b[1] +
				     (int32_t)gauss[0x100 + i] * b[2] +
				     (int32_t)gauss[i] * b[3]) >> 15;
			}
			if (non & (1u << vi))
				s = noise_sample();
			v->sample = (int16_t)s;
			v->pmon_last = s;
			/* volume: fixed mode only (sweep TODO) */
			vl = (int16_t)regs[vi * 8 + R_VOLL];
			vr = (int16_t)regs[vi * 8 + R_VOLR];
			if (vl & 0x8000)
				vl = 0x7FFF; /* sweep: hold max TODO */
			if (vr & 0x8000)
				vr = 0x7FFF;
			{
				int32_t env = v->env;
				int32_t sl = s * env >> 15;
				int32_t dl = sl * vl >> 15;
				int32_t dr = sl * vr >> 15;
				mix_l += dl;
				mix_r += dr;
				if (eon & (1u << vi)) {
					rev_l += dl;
					rev_r += dr;
				}
			}
		}

		/* reverb runs at 22.05 kHz: one cycle per two samples,
		 * fed with the accumulated two-sample input */
		if ((rev_phase ^= 1) == 0) {
			int32_t lo, ro;
			reverb_step(rev_l, rev_r, &lo, &ro);
			mix_l += lo;
			mix_r += ro;
			rev_l = rev_r = 0;
		}

		mix_l = mix_l * (int16_t)regs[R_MVOLL] >> 15;
		mix_r = mix_r * (int16_t)regs[R_MVOLR] >> 15;
		if (mix_l > 32767)
			mix_l = 32767;
		else if (mix_l < -32768)
			mix_l = -32768;
		if (mix_r > 32767)
			mix_r = 32767;
		else if (mix_r < -32768)
			mix_r = -32768;
		out[f * 2] = (int16_t)mix_l;
		out[f * 2 + 1] = (int16_t)mix_r;
		/* advance reverb buffer pointer each 22050 cycle */
		if (f & 1) {
			uint32_t esa = (uint32_t)regs[R_REVB_A] * 8;
			rev_addr = (rev_addr + 2) & 0x7FFFE;
			if (rev_addr < esa)
				rev_addr = esa;
		}
	}
	spu_samples_rendered += frames;
}
