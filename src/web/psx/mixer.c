/* Native audio mixer for the web build — see mixer.h.
 *
 * No SPU register file or per-sample ADPCM decode: VAG data written via
 * SpuWrite lands in a plain byte store, and the first key-on (or
 * SsVabTransBody's prepare pass) decodes the sample at that address to
 * int16 PCM once, cached until the bytes under it are overwritten.
 * Voices resample those buffers (PS1-style 4-tap Gaussian, so the
 * timbre matches), apply an ADSR whose rates are the closed-form
 * per-sample averages of the hardware envelope generator, and sum into
 * dry + reverb-send buses. Reverb runs the libspu preset network on a
 * private 22.05 kHz delay line; XA/CD audio is mixed after the master
 * volume. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/console.h>

#include "mixer.h"

/* ---- sample memory + decoded-PCM cache ---- */

static uint8_t mem[MIX_MEM_SIZE];

typedef struct Sample {
	uint32_t addr, end;   /* byte range of the ADPCM source */
	int16_t *pcm;
	int len;              /* samples */
	int loop;             /* loop-start sample, -1 = one-shot */
	struct Sample *next;
} Sample;

static Sample *cache[MIX_MEM_SIZE >> 3]; /* by addr>>3 */
static Sample *cache_list;
static unsigned long cache_bytes;

static const int adpcm_filt[5][2] = {
	{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}
};

static void cache_drop(Sample *s)
{
	Sample **pp;
	for (pp = &cache_list; *pp; pp = &(*pp)->next)
		if (*pp == s) {
			*pp = s->next;
			break;
		}
	cache[s->addr >> 3] = NULL;
	cache_bytes -= (unsigned long)s->len * 2;
	free(s->pcm);
	free(s);
}

static void cache_invalidate(uint32_t lo, uint32_t hi);

/* Decode the VAG block chain at addr until its end flag: flag bit2
 * marks the loop start, bit0 ends the chain, bit1 with bit0 loops back
 * (otherwise the voice is muted, i.e. a one-shot). */
static Sample *decode(uint32_t addr)
{
	Sample *s;
	uint32_t a = addr & (MIX_MEM_SIZE - 8);
	int nblk = 0, i, s1 = 0, s2 = 0, loop_blk = 0, looped = 0, cap;
	int16_t *out;

	for (;;) {
		uint8_t f = mem[a + nblk * 16 + 1];
		nblk++;
		if (f & 1) {
			looped = (f & 2) != 0;
			break;
		}
		if (a + nblk * 16 >= MIX_MEM_SIZE)
			break;
	}
	cap = nblk * 28;
	out = malloc((size_t)cap * sizeof(int16_t));
	s = calloc(1, sizeof *s);
	if (!out || !s) {
		free(out);
		free(s);
		return NULL;
	}
	for (i = 0; i < nblk; i++) {
		const uint8_t *b = mem + a + i * 16;
		int shift = b[0] & 0x0F, filt = (b[0] >> 4) & 7, j;
		int f0, f1;
		if (filt > 4)
			filt = 4;
		if (shift > 12)
			shift = 9;
		f0 = adpcm_filt[filt][0];
		f1 = adpcm_filt[filt][1];
		if (b[1] & 4)
			loop_blk = i;
		for (j = 0; j < 28; j++) {
			int nib = (b[2 + (j >> 1)] >> ((j & 1) * 4)) & 0xF;
			int v = ((int16_t)(nib << 12)) >> shift;
			v += (f0 * s1 + f1 * s2 + 32) >> 6;
			if (v > 32767)
				v = 32767;
			else if (v < -32768)
				v = -32768;
			s2 = s1;
			s1 = v;
			out[i * 28 + j] = (int16_t)v;
		}
	}
	s->addr = a;
	s->end = a + nblk * 16;
	s->pcm = out;
	s->len = cap;
	s->loop = looped ? loop_blk * 28 : -1;
	s->next = cache_list;
	cache_list = s;
	cache[a >> 3] = s;
	cache_bytes += (unsigned long)cap * 2;
	return s;
}

static Sample *sample_get(uint32_t addr)
{
	Sample *s = cache[(addr & (MIX_MEM_SIZE - 8)) >> 3];
	return s ? s : decode(addr);
}

void mix_sample_prepare(uint32_t addr)
{
	sample_get(addr);
}

/* ---- voices ---- */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
	Sample *smp;
	uint32_t addr;
	uint32_t pos;          /* sample index */
	uint32_t frac;         /* 12-bit fraction */
	int16_t hist[4];       /* last 4 source samples for interpolation */
	int16_t vol_l, vol_r;
	uint16_t pitch;
	uint16_t adsr1, adsr2;
	int stage;
	float env;             /* 0..32767 */
	int active;
	int ended;
} Voice;

static Voice voice[MIX_VOICES];
static uint32_t noise_mask, reverb_mask;
static int noise_clk;
static uint32_t noise_lfsr = 1, noise_timer;
static int16_t master_l, master_r, cd_vol_l, cd_vol_r;

void mix_voice_volume(int v, int16_t l, int16_t r)
{
	voice[v].vol_l = l;
	voice[v].vol_r = r;
}
void mix_voice_pitch(int v, uint16_t p) { voice[v].pitch = p; }
void mix_voice_sample(int v, uint32_t a) { voice[v].addr = a; }
void mix_voice_adsr(int v, uint16_t a1, uint16_t a2)
{
	voice[v].adsr1 = a1;
	voice[v].adsr2 = a2;
}
uint16_t mix_voice_get_adsr1(int v) { return voice[v].adsr1; }
uint16_t mix_voice_get_adsr2(int v) { return voice[v].adsr2; }
int16_t mix_voice_env(int v) { return (int16_t)voice[v].env; }

void mix_key_on(uint32_t mask)
{
	int i;
	for (i = 0; i < MIX_VOICES; i++) {
		Voice *v = &voice[i];
		if (!(mask & (1u << i)))
			continue;
		v->smp = sample_get(v->addr);
		v->pos = 0;
		v->frac = 0;
		memset((unsigned char *)v->hist, 0, sizeof v->hist);
		v->env = 0;
		v->stage = ENV_ATTACK;
		v->active = 1;
		v->ended = 0;
	}
}

void mix_key_off(uint32_t mask)
{
	int i;
	for (i = 0; i < MIX_VOICES; i++)
		if ((mask & (1u << i)) && voice[i].stage != ENV_OFF)
			voice[i].stage = ENV_RELEASE;
}

uint32_t mix_end_mask(void)
{
	uint32_t m = 0;
	int i;
	for (i = 0; i < MIX_VOICES; i++)
		if (voice[i].ended)
			m |= 1u << i;
	return m;
}

void mix_noise_voices(uint32_t mask) { noise_mask = mask; }
void mix_noise_clock(int clk) { noise_clk = clk & 0x3F; }

/* ---- envelope ----
 * Hardware: every (1 << max(0, shift-11)) samples the level moves by
 * step << max(0, 11-shift), step = 7-s (inc) or -(8-s) (dec). The
 * per-sample mean is step * 2^(11-shift); exponential decay scales it
 * by level/0x8000, exponential attack is quartered above 0x6000. */
static float env_rate(int shift, int sv, int dec)
{
	float step = dec ? -(float)(8 - sv) : (float)(7 - sv);
	return step * ldexpf(1.0f, 11 - shift);
}

static void env_step(Voice *v)
{
	uint16_t a1 = v->adsr1, a2 = v->adsr2;
	int shift, sv;
	float d;

	switch (v->stage) {
	case ENV_ATTACK:
		shift = (a1 >> 10) & 0x1F;
		sv = (a1 >> 8) & 3;
		if (((a1 >> 8) & 0x7F) == 0x7F)
			return;
		d = env_rate(shift, sv, 0);
		if ((a1 & 0x8000) && v->env > 0x6000)
			d *= 0.25f;
		v->env += d;
		if (v->env >= 32767.0f) {
			v->env = 32767.0f;
			v->stage = ENV_DECAY;
		}
		break;
	case ENV_DECAY:
		shift = (a1 >> 4) & 0x0F;
		v->env += env_rate(shift, 0, 1) * v->env / 32768.0f;
		if (v->env <= (float)(((a1 & 0x0F) + 1) << 11))
			v->stage = ENV_SUSTAIN;
		break;
	case ENV_SUSTAIN:
		shift = (a2 >> 8) & 0x1F;
		sv = (a2 >> 6) & 3;
		if (((a2 >> 6) & 0x7F) == 0x7F)
			return;
		d = env_rate(shift, sv, a2 & 0x4000 ? 1 : 0);
		if (a2 & 0x8000) { /* exponential */
			if (a2 & 0x4000)
				d = d * v->env / 32768.0f;
			else if (v->env > 0x6000)
				d *= 0.25f;
		}
		v->env += d;
		if (v->env > 32767.0f)
			v->env = 32767.0f;
		if (v->env < 0.0f)
			v->env = 0.0f;
		break;
	case ENV_RELEASE:
		shift = a2 & 0x1F;
		d = env_rate(shift, 0, 1);
		if (a2 & 0x20)
			d = d * v->env / 32768.0f;
		v->env += d;
		if (v->env <= 0.5f) {
			v->env = 0.0f;
			v->stage = ENV_OFF;
			v->active = 0;
		}
		break;
	default:
		break;
	}
}

/* ---- PS1 Gaussian interpolation table (psx-spx) ---- */

static const int16_t gauss[512] = {
#include "mixer_gauss.inc"
};

static int16_t next_source(Voice *v)
{
	Sample *s = v->smp;
	int16_t x;
	if (!s || v->ended == 2)
		return 0;
	x = s->pcm[v->pos++];
	if ((int)v->pos >= s->len) {
		v->ended = 1;
		if (s->loop >= 0) {
			v->pos = (uint32_t)s->loop;
		} else {
			/* end without repeat: hardware mutes the voice */
			v->ended = 2;
			v->stage = ENV_OFF;
			v->env = 0;
			v->active = 0;
		}
	}
	return x;
}

static int16_t noise_step(void)
{
	/* timer reload 0x20000 >> shift, decremented by 4..7 per sample */
	int shift = (noise_clk >> 2) & 0xF;
	int step = (noise_clk & 3) + 4;
	noise_timer += (uint32_t)step << shift;
	while (noise_timer >= 0x20000) {
		uint32_t p = ((noise_lfsr >> 15) ^ (noise_lfsr >> 12) ^
			      (noise_lfsr >> 11) ^ (noise_lfsr >> 10) ^ 1) & 1;
		noise_lfsr = ((noise_lfsr << 1) | p) & 0xFFFF;
		noise_timer -= 0x20000;
	}
	return (int16_t)noise_lfsr;
}

/* ---- reverb: libspu presets on a private work buffer ----
 * Parameter records and work-area start addresses are the PsyQ tables
 * from asm/us/main/data/psyq/libspu.data.s (_spu_rev_param, skipping
 * each record's 4-byte mask header, and _spu_rev_startaddr). The
 * network is the psx-spx reverb formula; addresses are in bytes
 * relative to a cursor that advances one halfword per 22.05 kHz tick
 * and wraps inside the preset's work-area size. */

static const uint16_t rev_param[10][32] = {
	{0},
	{0x007D,0x005B,0x6D80,0x54B8,0xBED0,0x0000,0x0000,0xBA80,
	 0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
	 0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	 0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C,0x8000,0x8000},
	{0x0033,0x0025,0x70F0,0x4FA8,0xBCE0,0x4410,0xC0F0,0x9C00,
	 0x5280,0x4EC0,0x03E4,0x031B,0x03A4,0x02AF,0x0372,0x0266,
	 0x031C,0x025D,0x025C,0x018E,0x022F,0x0135,0x01D2,0x00B7,
	 0x018F,0x00B5,0x00B4,0x0080,0x004C,0x0026,0x8000,0x8000},
	{0x00B1,0x007F,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0xB4C0,
	 0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
	 0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7,
	 0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080,0x8000,0x8000},
	{0x00E3,0x00A9,0x6F60,0x4FA8,0xBCE0,0x4510,0xBEF0,0xA680,
	 0x5680,0x52C0,0x0DFB,0x0B58,0x0D09,0x0A3C,0x0BD9,0x0973,
	 0x0B59,0x08DA,0x08D9,0x05E9,0x07EC,0x04B0,0x06EF,0x03D2,
	 0x05EA,0x031D,0x031C,0x0238,0x0154,0x00AA,0x8000,0x8000},
	{0x01A5,0x0139,0x6000,0x5000,0x4C00,0xB800,0xBC00,0xC000,
	 0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
	 0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD,
	 0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A,0x8000,0x8000},
	{0x033D,0x0231,0x7E00,0x5000,0xB400,0xB000,0x4C00,0xB000,
	 0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC2,0x16B2,
	 0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D,
	 0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232,0x8000,0x8000},
	{0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x8100,
	 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
	 0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000},
	{0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x0000,
	 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
	 0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000},
	{0x0017,0x0013,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0x8500,
	 0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
	 0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059,
	 0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014,0x8000,0x8000},
};
static const uint16_t rev_start[10] = {
	0xFFFE, 0xFB28, 0xFC18, 0xF6F8, 0xF204,
	0xEA44, 0xE128, 0xCFF8, 0xCFF8, 0xF880
};

enum {
	dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL,
	vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2,
	dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4,
	dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN
};

#define REV_MAX_BYTES ((0x10000 - 0xCFF8) * 8)
static int16_t rev_buf[REV_MAX_BYTES / 2];
static uint32_t rev_size = 16;  /* bytes */
static uint32_t rev_cur;
static int rev_mode, rev_on;
static int16_t rev_depth_l, rev_depth_r;

#define RP(i) ((int32_t)(int16_t)rev_param[rev_mode][i])
#define RA(i) ((int32_t)rev_param[rev_mode][i] * 8)

static uint32_t rv_addr(int32_t rel)
{
	int32_t a = (int32_t)(((int64_t)rev_cur + rel) % (int64_t)rev_size);
	if (a < 0)
		a += (int32_t)rev_size;
	return (uint32_t)a >> 1;
}

static int32_t rv_get(int32_t rel)
{
	return rev_buf[rv_addr(rel)];
}

static void rv_put(int32_t rel, int32_t v)
{
	uint32_t a = rv_addr(rel);
	if (v > 32767)
		v = 32767;
	else if (v < -32768)
		v = -32768;
	rev_buf[a] = (int16_t)v;
}

static int32_t clamp16(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

static void reverb_tick(int32_t lin, int32_t rin, int32_t *lo, int32_t *ro)
{
	int32_t l, r;
	lin = lin * RP(vLIN) >> 15;
	rin = rin * RP(vRIN) >> 15;

	rv_put(RA(mLSAME), ((lin + (rv_get(RA(dLSAME)) * RP(vWALL) >> 15) -
		rv_get(RA(mLSAME) - 2)) * RP(vIIR) >> 15) + rv_get(RA(mLSAME) - 2));
	rv_put(RA(mRSAME), ((rin + (rv_get(RA(dRSAME)) * RP(vWALL) >> 15) -
		rv_get(RA(mRSAME) - 2)) * RP(vIIR) >> 15) + rv_get(RA(mRSAME) - 2));
	rv_put(RA(mLDIFF), ((lin + (rv_get(RA(dRDIFF)) * RP(vWALL) >> 15) -
		rv_get(RA(mLDIFF) - 2)) * RP(vIIR) >> 15) + rv_get(RA(mLDIFF) - 2));
	rv_put(RA(mRDIFF), ((rin + (rv_get(RA(dLDIFF)) * RP(vWALL) >> 15) -
		rv_get(RA(mRDIFF) - 2)) * RP(vIIR) >> 15) + rv_get(RA(mRDIFF) - 2));

	l = clamp16((RP(vCOMB1) * rv_get(RA(mLCOMB1)) + RP(vCOMB2) * rv_get(RA(mLCOMB2)) +
		     RP(vCOMB3) * rv_get(RA(mLCOMB3)) + RP(vCOMB4) * rv_get(RA(mLCOMB4))) >> 15);
	r = clamp16((RP(vCOMB1) * rv_get(RA(mRCOMB1)) + RP(vCOMB2) * rv_get(RA(mRCOMB2)) +
		     RP(vCOMB3) * rv_get(RA(mRCOMB3)) + RP(vCOMB4) * rv_get(RA(mRCOMB4))) >> 15);

	l = clamp16(l - (RP(vAPF1) * rv_get(RA(mLAPF1) - RA(dAPF1)) >> 15));
	rv_put(RA(mLAPF1), l);
	l = clamp16((l * RP(vAPF1) >> 15) + rv_get(RA(mLAPF1) - RA(dAPF1)));
	r = clamp16(r - (RP(vAPF1) * rv_get(RA(mRAPF1) - RA(dAPF1)) >> 15));
	rv_put(RA(mRAPF1), r);
	r = clamp16((r * RP(vAPF1) >> 15) + rv_get(RA(mRAPF1) - RA(dAPF1)));

	l = clamp16(l - (RP(vAPF2) * rv_get(RA(mLAPF2) - RA(dAPF2)) >> 15));
	rv_put(RA(mLAPF2), l);
	l = clamp16((l * RP(vAPF2) >> 15) + rv_get(RA(mLAPF2) - RA(dAPF2)));
	r = clamp16(r - (RP(vAPF2) * rv_get(RA(mRAPF2) - RA(dAPF2)) >> 15));
	rv_put(RA(mRAPF2), r);
	r = clamp16((r * RP(vAPF2) >> 15) + rv_get(RA(mRAPF2) - RA(dAPF2)));

	*lo = l * rev_depth_l >> 15;
	*ro = r * rev_depth_r >> 15;
	rev_cur = (rev_cur + 2) % rev_size;
}

void mix_reverb_voices(uint32_t mask) { reverb_mask = mask; }
void mix_reverb_depth(int16_t l, int16_t r)
{
	rev_depth_l = l;
	rev_depth_r = r;
}
void mix_reverb_enable(int on) { rev_on = on; }
void mix_reverb_clear(void)
{
	memset((unsigned char *)rev_buf, 0, sizeof rev_buf);
}
void mix_reverb_mode(int mode)
{
	if (mode < 0 || mode >= 10)
		return;
	rev_mode = mode;
	rev_size = (0x10000u - rev_start[mode]) * 8;
	if (rev_size < 16)
		rev_size = 16;
	rev_cur %= rev_size;
}

/* ---- master / CD ---- */

void mix_master_volume(int16_t l, int16_t r)
{
	master_l = l;
	master_r = r;
}
int16_t mix_get_master_l(void) { return master_l; }
int16_t mix_get_master_r(void) { return master_r; }
void mix_cd_volume(int16_t l, int16_t r)
{
	cd_vol_l = l;
	cd_vol_r = r;
}

#define CD_BUF_FRAMES (44100 * 4)
static int16_t cd_buf[CD_BUF_FRAMES * 2];
static int cd_w, cd_r;

void mix_cd_input(const int16_t *stereo, int frames)
{
	int i;
	for (i = 0; i < frames; i++) {
		int next = (cd_w + 1) % CD_BUF_FRAMES;
		if (next == cd_r)
			break; /* full: drop */
		cd_buf[cd_w * 2] = stereo[i * 2];
		cd_buf[cd_w * 2 + 1] = stereo[i * 2 + 1];
		cd_w = next;
	}
}

/* ---- sample memory ---- */

static void cache_invalidate(uint32_t lo, uint32_t hi)
{
	Sample *s = cache_list, *n;
	int i;
	for (; s; s = n) {
		n = s->next;
		if (s->addr < hi && lo < s->end) {
			for (i = 0; i < MIX_VOICES; i++)
				if (voice[i].smp == s) {
					voice[i].smp = NULL;
					voice[i].active = 0;
					voice[i].stage = ENV_OFF;
				}
			cache_drop(s);
		}
	}
}

void mix_mem_write(uint32_t addr, const void *src, uint32_t len)
{
	if (addr >= MIX_MEM_SIZE)
		return;
	if (len > MIX_MEM_SIZE - addr)
		len = MIX_MEM_SIZE - addr;
	memcpy(mem + addr, src, len);
	cache_invalidate(addr, addr + len);
}

void mix_mem_read(uint32_t addr, void *dst, uint32_t len)
{
	if (addr >= MIX_MEM_SIZE)
		return;
	if (len > MIX_MEM_SIZE - addr)
		len = MIX_MEM_SIZE - addr;
	memcpy(dst, mem + addr, len);
}

/* ---- render ---- */

void mix_reset(void)
{
	while (cache_list)
		cache_drop(cache_list);
	memset((unsigned char *)mem, 0, sizeof mem);
	memset((unsigned char *)voice, 0, sizeof voice);
	noise_mask = reverb_mask = 0;
	master_l = master_r = cd_vol_l = cd_vol_r = 0;
	rev_on = 0;
	rev_mode = 0;
	rev_size = 16;
	rev_cur = 0;
	mix_reverb_clear();
}

static int32_t vol15(int16_t v)
{
	/* bit15 = sweep mode; sweeps are not used by the game's libsnd
	 * paths, so hold the maximum like the old emulation did */
	return (v & 0x8000) ? 0x7FFF : v;
}

void mix_render(int16_t *out, int frames)
{
	static int rev_phase;
	static int32_t rsend_l, rsend_r, rout_l, rout_r;
	int f, i;

	for (f = 0; f < frames; f++) {
		int32_t mix_l = 0, mix_r = 0;
		int16_t nz = noise_mask ? noise_step() : 0;

		for (i = 0; i < MIX_VOICES; i++) {
			Voice *v = &voice[i];
			uint32_t step;
			int32_t s, dl, dr, k;

			if (!v->active)
				continue;
			env_step(v);
			step = v->pitch > 0x4000 ? 0x4000 : v->pitch;
			v->frac += step;
			while (v->frac >= 0x1000) {
				v->frac -= 0x1000;
				v->hist[0] = v->hist[1];
				v->hist[1] = v->hist[2];
				v->hist[2] = v->hist[3];
				v->hist[3] = next_source(v);
			}
			if (noise_mask & (1u << i)) {
				s = nz;
			} else {
				k = (v->frac >> 4) & 0xFF;
				s = ((int32_t)gauss[0xFF - k] * v->hist[0] +
				     (int32_t)gauss[0x1FF - k] * v->hist[1] +
				     (int32_t)gauss[0x100 + k] * v->hist[2] +
				     (int32_t)gauss[k] * v->hist[3]) >> 15;
			}
			s = s * (int32_t)v->env >> 15;
			dl = s * vol15(v->vol_l) >> 15;
			dr = s * vol15(v->vol_r) >> 15;
			mix_l += dl;
			mix_r += dr;
			if (reverb_mask & (1u << i)) {
				rsend_l += dl;
				rsend_r += dr;
			}
		}

		/* reverb at 22.05 kHz on the averaged send of two samples */
		if ((rev_phase ^= 1) == 0) {
			if (rev_on && rev_mode)
				reverb_tick(clamp16(rsend_l >> 1),
					    clamp16(rsend_r >> 1),
					    &rout_l, &rout_r);
			else
				rout_l = rout_r = 0;
			rsend_l = rsend_r = 0;
		}
		mix_l += rout_l;
		mix_r += rout_r;

		mix_l = mix_l * master_l >> 15;
		mix_r = mix_r * master_r >> 15;
		if (cd_r != cd_w) {
			mix_l += (int32_t)cd_buf[cd_r * 2] * cd_vol_l >> 15;
			mix_r += (int32_t)cd_buf[cd_r * 2 + 1] * cd_vol_r >> 15;
			cd_r = (cd_r + 1) % CD_BUF_FRAMES;
		}
		out[f * 2] = (int16_t)clamp16(mix_l);
		out[f * 2 + 1] = (int16_t)clamp16(mix_r);
	}
}

