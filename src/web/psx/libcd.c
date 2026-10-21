/*
 * libcd for the web build: a real implementation over Module.disc
 * (src/web/host/disc.c). Reads are synchronous to the game (Asyncify
 * suspends inside CdReadSync), matching how the game's polling loops
 * use CdRead + CdReadSync(0|1).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcd.h>

/* declared manually: <emscripten.h> drags in system headers that clash
 * with the PsyQ headers on this file's include path. */
extern void emscripten_console_log(const char *str);
extern void emscripten_console_logf(const char *fmt, ...);

#include <web/host/disc.h>
#include <web/host/log.h>

static int to_lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* stream interrupt flag, checked by src/mov/mov.c */
u_long StCdIntrFlag[4];

static uint32_t cur_lba;
static uint8_t cur_mode;
static uint8_t cur_filter[2];

static int read_pending;
static uint32_t pend_lba, pend_count;
static void *pend_buf;

static int read_log_count;

extern int VSync(int mode);
extern unsigned int web_vsync_count;
#include "mixer.h"
static void st_stream_arm(void);

/* CD controller output attenuation matrix (ATV0..3): L = l*v0 + r*v1,
 * R = l*v2 + r*v3, 0x80 = normal. Power-on default per psx-spx. */
static uint8_t cd_atv[4] = { 0x80, 0x00, 0x80, 0x00 };

/* glabel CD_initvol (asm/us/main/psyq/libcd.s): if the SPU main volume
 * is still zero install 0x3FFF, always set CD input volume to 0x3FFF
 * (or the current main volume), SPUCNT = 0xC001 (SPU on, unmuted, CD
 * audio enable — the mixer always mixes CD), and the CD controller ATV
 * matrix to normal stereo. The asm reads the 0x1DB8/0x1DBA current
 * main-volume registers; the mixer's master volume is that value. */
static void cd_initvol(void)
{
	uint16_t v0;

	if (mix_get_master_l() != 0) {
		v0 = 0x3FFF;
	} else {
		v0 = (uint16_t)mix_get_master_r();
		if (v0 == 0) {
			v0 = 0x3FFF;
			mix_master_volume((int16_t)v0, (int16_t)v0);
		}
	}
	mix_cd_volume((int16_t)v0, (int16_t)v0);
	cd_atv[0] = 0x80;
	cd_atv[1] = 0x00;
	cd_atv[2] = 0x80;
	cd_atv[3] = 0x00;
}

/* ---------------------------------------------------------------- */
/* helpers                                                           */

static void fill_status(u_char *result)
{
	if (result == NULL)
		return;
	memset(result, 0, 8);
	result[0] = CdlStatStandby;
}

static int loc_to_lba(const CdlLOC *p)
{
	return (btoi(p->minute) * 60 + btoi(p->second)) * 75 +
	       btoi(p->sector) - 150;
}

/* ---------------------------------------------------------------- */
/* init / position                                                   */

int CdInit(void)
{
	static uint8_t pvd[2048];
	char volid[33];

	if (disc_wait_ready() != 0) {
		emscripten_console_log("cd: no disc attached");
		return 0;
	}
	if (disc_read_user(16, 1, pvd) != 0 ||
	    memcmp(pvd + 1, "CD001", 5) != 0) {
		emscripten_console_log("cd: PVD check failed");
		return 0;
	}
	memcpy(volid, pvd + 40, 32);
	volid[32] = 0;
	emscripten_console_logf("cd: disc attached, volume id '%s'", volid);
	/* glabel CdInit -> CdReset(1) -> CD_init + CD_initvol */
	cd_initvol();
	return 1;
}

int CdReset(int mode)
{
	/* asm: mode 2 re-inits interrupts only; mode 1 additionally
	 * runs CD_initvol after a successful CD_init. */
	if (mode == 1)
		cd_initvol();
	return 1;
}

int CdSetDebug(int level)
{
	(void)level;
	return 0;
}

int CdSync(int mode, u_char *result)
{
	(void)mode;
	fill_status(result);
	return 0;
}

int CdStatus(void)
{
	return CdlStatStandby;
}

CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
	i += 150;
	p->minute = (u_char)itob(i / 4500);
	p->second = (u_char)itob((i % 4500) / 75);
	p->sector = (u_char)itob(i % 75);
	p->track = 0;
	return p;
}

int CdPosToInt(CdlLOC *p)
{
	return loc_to_lba(p);
}

/* ---------------------------------------------------------------- */
/* control                                                           */

int CdControl(u_char com, u_char *param, u_char *result)
{
	switch (com) {
	case CdlSetloc:
		cur_lba = (uint32_t)loc_to_lba((CdlLOC *)param);
		break;
	case CdlSetmode:
		cur_mode = param[0];
		break;
	case CdlSetfilter:
		cur_filter[0] = param[0];
		cur_filter[1] = param[1];
		break;
	case CdlGetparam:
		if (result != NULL) {
			result[0] = CdlStatStandby;
			result[1] = 0;
			result[2] = cur_mode;
			result[3] = cur_filter[0];
			result[4] = cur_filter[1];
			return 1;
		}
		break;
	case CdlGetTN:
		if (result != NULL) {
			memset(result, 0, 8);
			result[0] = CdlStatStandby;
			result[1] = 1; /* first track  */
			result[2] = 1; /* last track   */
			return 1;
		}
		break;
	case CdlSeekL:
	case CdlSeekP:
	case CdlReadN:
	case CdlReadS:
	case CdlPause:
	case CdlStandby:
	case CdlStop:
	case CdlNop:
	case CdlMute:
	case CdlDemute:
	case CdlGetlocL:
	case CdlGetlocP:
	case CdlGetTD:
		break;
	default:
		emscripten_console_logf("cd: unimplemented CdControl cmd 0x%02x",
					com);
		break;
	}
	fill_status(result);
	return 1;
}

int CdControlB(u_char com, u_char *param, u_char *result)
{
	return CdControl(com, param, result);
}

int CdControlF(u_char com, u_char *param)
{
	return CdControl(com, param, NULL);
}

/* ---------------------------------------------------------------- */
/* reads                                                             */

long cd_read_calls;

int CdRead(int sectors, u_long *buf, int mode)
{
	(void)mode;
	cd_read_calls++;
	if (sectors <= 0 || buf == NULL)
		return 0;
	read_pending = 1;
	pend_lba = cur_lba;
	pend_count = (uint32_t)sectors;
	pend_buf = buf;
	return 1;
}

int CdReadSync(int mode, u_char *result)
{
	(void)mode;
	if (result != NULL)
		result[0] = CdlStatStandby;
	if (!read_pending)
		return 0;
	read_pending = 0;
	if (disc_read_user(pend_lba, pend_count, pend_buf) != 0) {
		emscripten_console_logf("cd: read error lba=%u n=%u", pend_lba,
					pend_count);
		return -1; /* CdlDiskError - the game retries the read */
	}
	if (read_log_count < 40) {
		uintptr_t p = (uintptr_t)pend_buf;
		extern unsigned char PSX_RAM[0x200000];
		uintptr_t ram = (uintptr_t)PSX_RAM;
		if (p >= ram && p < ram + 0x200000)
			emscripten_console_logf("cd: read lba=%u n=%u -> "
						"PSX_RAM+0x%X", pend_lba,
						pend_count,
						(unsigned)(p - ram));
		else
			emscripten_console_logf("cd: read lba=%u n=%u -> "
						"native %p", pend_lba,
						pend_count, pend_buf);
	}
	read_log_count++;
	cur_lba += pend_count;
	return 0;
}

int CdRead2(long mode)
{
	if (mode & CdlModeStream)
		st_stream_arm();
	return 1;
}

int CdReadFile(char *file, u_long *addr, int nbyte)
{
	web_unimplemented("CdReadFile");
	(void)file;
	(void)addr;
	(void)nbyte;
	return 0;
}

int CdGetSector(void *madr, int size)
{
	web_unimplemented("CdGetSector");
	(void)madr;
	(void)size;
	return 0;
}

int CdDataSync(int mode)
{
	(void)mode;
	return 0;
}

void CdReadBreak(void)
{
}

int CdGetToc(CdlLOC *loc)
{
	(void)loc;
	return 0;
}

int CdPlay(int mode, int *track, int offset)
{
	web_unimplemented("CdPlay");
	(void)mode;
	(void)track;
	(void)offset;
	return 0;
}

int CdMix(CdlATV *vol)
{
	/* CD controller ATV registers: scale the DSP->SPU output. */
	cd_atv[0] = vol->val0;
	cd_atv[1] = vol->val1;
	cd_atv[2] = vol->val2;
	cd_atv[3] = vol->val3;
	return 0;
}

void CdFlush(void)
{
}

CdlLOC *CdLastPos(void)
{
	static CdlLOC p;

	CdIntToPos((int)cur_lba, &p);
	return &p;
}

CdlCB CdReadyCallback(CdlCB func)
{
	(void)func;
	return NULL;
}

CdlCB CdReadCallback(CdlCB func)
{
	(void)func;
	return NULL;
}

CdlCB CdSyncCallback(CdlCB func)
{
	(void)func;
	return NULL;
}

void (*CdDataCallback(void (*func)()))
{
	(void)func;
	return NULL;
}

/* ---------------------------------------------------------------- */
/* ISO9660 for CdSearchFile                                          */

static uint8_t *dir_buf;
static uint32_t dir_buf_size;

static uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* case-insensitive compare of a path component against a directory
 * record identifier with its ";1" version suffix stripped */
static int ident_match(const uint8_t *ident, int ilen, const char *name)
{
	int i;

	for (i = 0; i < ilen; ++i) {
		char c = (char)ident[i];
		char n = name[i];

		if (c == ';')
			c = 0;
		if (n == ';')
			n = 0;
		if (n == 0 && c == 0)
			return 1;
		if (n == 0 || c == 0)
			return 0;
		if (to_lower(c) != to_lower(n))
			return 0;
	}
	return name[ilen] == 0 || name[ilen] == ';';
}

static int find_in_dir(uint32_t extent, uint32_t size, const char *name,
		       uint32_t *out_extent, uint32_t *out_size,
		       int *out_isdir, char *out_name)
{
	uint32_t nsec, pos;
	const uint8_t *rec;

	if (size == 0)
		return 0;
	nsec = (size + 2047) / 2048;
	if (nsec * 2048 > dir_buf_size) {
		free(dir_buf);
		dir_buf_size = nsec * 2048;
		dir_buf = (uint8_t *)malloc(dir_buf_size);
		if (dir_buf == NULL) {
			dir_buf_size = 0;
			return 0;
		}
	}
	if (disc_read_user(extent, nsec, dir_buf) != 0)
		return 0;

	pos = 0;
	while (pos < size) {
		uint8_t rlen = dir_buf[pos];

		if (rlen == 0) {
			pos = (pos + 2047) & ~2047u;
			continue;
		}
		rec = dir_buf + pos;
		if (ident_match(rec + 33, rec[32], name)) {
			*out_extent = rd_u32(rec + 2);
			*out_size = rd_u32(rec + 10);
			*out_isdir = (rec[25] & 2) != 0;
			if (out_name != NULL) {
				int i;

				for (i = 0; i < rec[32] && i < 15; ++i) {
					if (rec[33 + i] == ';')
						break;
					out_name[i] = (char)rec[33 + i];
				}
				out_name[i] = 0;
			}
			return 1;
		}
		pos += rlen;
	}
	return 0;
}

CdlFILE *CdSearchFile(CdlFILE *fp, char *name)
{
	static uint8_t pvd[2048];
	static char component[64];
	char *p, *slash;
	uint32_t extent, size;
	int isdir;

	if (disc_read_user(16, 1, pvd) != 0 ||
	    memcmp(pvd + 1, "CD001", 5) != 0)
		return NULL;
	if (pvd[156] < 34)
		return NULL;
	extent = rd_u32(pvd + 156 + 2);
	size = rd_u32(pvd + 156 + 10);

	/* split "DIR\FILE;1" or "\DIR\FILE;1" into components */
	p = name;
	while (*p == '\\')
		++p;
	for (;;) {
		slash = strchr(p, '\\');
		if (slash != NULL && slash - p < (int)sizeof(component)) {
			memcpy(component, p, (size_t)(slash - p));
			component[slash - p] = 0;
		} else if (slash == NULL) {
			if ((size_t)strlen(p) >= sizeof(component))
				return NULL;
			strcpy(component, p);
		} else {
			return NULL;
		}
		if (component[0] == 0)
			return NULL;
		if (!find_in_dir(extent, size, component, &extent, &size,
				 &isdir, fp->name)) {
			emscripten_console_logf("cd: search '%s' miss '%s'", name, component);
			return NULL;
		}
		if (slash == NULL)
			break;
		if (!isdir)
			return NULL;
		p = slash + 1;
	}

	CdIntToPos((int)extent, &fp->pos);
	fp->size = size;
	emscripten_console_logf("cd: search '%s' -> lba=%u size=%u", name, extent, size);
	return fp;
}

/* ---------------------------------------------------------------- */
/* libds streaming                                                    */
/*                                                                    */
/* Faithful to the retail model (asm/us/main/psyq/libcd.s): the ring  */
/* is `ring_size` slots; each slot is a 32-byte STR header followed   */
/* by a 2016-byte chunk payload (headers occupy the first             */
/* ring_size*32 bytes, data starts at ring + ring_size*32).           */
/* A movie frame occupies nSectors consecutive slots; the frame's     */
/* first slot's header id is set to StCOMPLETE when the last sector   */
/* arrives. StGetNext locks it (StLOCK) and returns the contiguous    */
/* assembled bitstream.                                               */
/*                                                                    */
/* Sector delivery is clocked by the emulated 2x CD rate: 150        */
/* sectors/second -> 2.5 per 60 Hz VSync, plus an initial burst so    */
/* the ring pre-fills like the real drive's sector buffer.           */
/* XA audio sectors (submode bit2) decode into the SPU CD input.     */

static u_long *st_ring;
static u_long st_ringsize;
static int st_widx;		/* producer slot index (StRingIdx1) */
static int st_ridx;		/* consumer slot index (StRingIdx3) */
static int st_frame_start;	/* slot of the assembling frame */

static int st_streaming;	/* CdRead2 armed in stream mode */
static uint32_t st_lba;		/* next sector to deliver */
static uint32_t st_delivered;	/* sectors delivered since arm */
static uint32_t st_vsync0;	/* vsync count at arm */
static uint32_t st_next_frameid;

static CdlLOC st_prev_loc;

static uint8_t st_secbuf[2352 * 24];

static u_char *st_slot_hdr(int i) { return (u_char *)st_ring + i * 32; }
static u_char *st_slot_data(int i)
{
	return (u_char *)st_ring + st_ringsize * 32 + i * 2016;
}

/* XA ADPCM decode (37800/18900 Hz stereo or mono, 4/8-bit) -> stereo
 * 44100 Hz pairs pushed to the SPU CD input. */
static int16_t xa_prev[4];
static double xa_frac;

/* apply the CD controller ATV matrix then hand off to the SPU CD input */
static void xa_push(int16_t *stereo, int frames)
{
	for (int i = 0; i < frames; i++) {
		int l = stereo[i * 2], r = stereo[i * 2 + 1];
		int lo = (l * cd_atv[0] + r * cd_atv[1]) >> 7;
		int ro = (l * cd_atv[2] + r * cd_atv[3]) >> 7;
		if (lo > 32767)
			lo = 32767;
		else if (lo < -32768)
			lo = -32768;
		if (ro > 32767)
			ro = 32767;
		else if (ro < -32768)
			ro = -32768;
		stereo[i * 2] = (int16_t)lo;
		stereo[i * 2 + 1] = (int16_t)ro;
	}
	mix_cd_input(stereo, frames);
}

static void xa_decode_sector(const uint8_t *sec)
{
	static const int8_t filt_pos[16] = {
		0, 60, 115, 98, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	static const int8_t filt_neg[16] = {
		0, 0, -52, -55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	const uint8_t *chunk = sec + 24;
	int stereo = sec[19] & 1;
	int eight_bit = sec[19] & 0x10;
	static int16_t pcm[2352 * 2];
	int n = 0;
	(void)eight_bit;

	for (int grp = 0; grp < 18; grp++) {
		const uint8_t *hdr = chunk + 4;
		const uint8_t *wrd = chunk + 16;
		for (int blk = 0; blk < 8; blk++) {
			uint8_t param = hdr[blk];
			int shift = param & 0x0F;
			int filter = param >> 4;
			int32_t *prev = (int32_t *)&xa_prev[(blk & 1) * 2];
			if (shift > 12)
				shift = 9;
			for (int wd = 0; wd < 28; wd++) {
				uint32_t w;
				int nib, smp, out;
				memcpy(&w, wrd + wd * 4, 4);
				nib = (int)(w >> (blk * 4)) & 0x0F;
				smp = (int16_t)(nib << 12) >> shift;
				out = smp + ((prev[0] * filt_pos[filter]) >> 6) +
				      ((prev[1] * filt_neg[filter]) >> 6);
				if (out > 32767)
					out = 32767;
				else if (out < -32768)
					out = -32768;
				prev[1] = prev[0];
				prev[0] = out;
				if (stereo) {
					/* 8 blocks alternate L,R: 112
					 * stereo frames per 128B unit */
					int ch = blk & 1;
					int fr = (blk / 2) * 28 + wd;
					pcm[(grp * 112 + fr) * 2 + ch] =
						(int16_t)out;
				} else {
					pcm[grp * 224 + blk * 28 + wd] =
						(int16_t)out;
				}
			}
		}
		chunk += 128;
	}

	if (stereo) {
		/* 2016 stereo frames @37800 -> resample to 44100 (7:6) */
		static int16_t last_l, last_r;
		static double phase;
		double step = 37800.0 / 44100.0;
		int16_t outp[2400 * 2];
		int o = 0;
		(void)xa_frac;
		while (phase < 2016.0 && o < 2398) {
			int i0 = (int)phase;
			int i1 = i0 + 1 < 2016 ? i0 + 1 : i0;
			double f = phase - i0;
			int16_t l = (int16_t)((1.0 - f) *
						 pcm[i0 * 2] +
					     f * pcm[i1 * 2]);
			int16_t r = (int16_t)((1.0 - f) *
						 pcm[i0 * 2 + 1] +
					     f * pcm[i1 * 2 + 1]);
			outp[o * 2] = l;
			outp[o * 2 + 1] = r;
			o++;
			phase += step;
		}
		phase -= 2016.0;
		(void)last_l;
		(void)last_r;
		xa_push(outp, o);
		n = o;
	} else {
		/* 4032 mono samples @37800 -> 44100, duplicate to stereo */
		static double phase_m;
		double step = 37800.0 / 44100.0;
		int16_t outp[4800 * 2];
		int o = 0;
		while (phase_m < 4032.0 && o < 4798) {
			int i0 = (int)phase_m;
			int i1 = i0 + 1 < 4032 ? i0 + 1 : i0;
			double f = phase_m - i0;
			int16_t s = (int16_t)((1.0 - f) * pcm[i0] +
					     f * pcm[i1]);
			outp[o * 2] = s;
			outp[o * 2 + 1] = s;
			o++;
			phase_m += step;
		}
		phase_m -= 4032.0;
		xa_push(outp, o);
		n = o;
	}
	(void)n;
}

/* pump `budget` more sectors through the stream */
static void st_pump(uint32_t budget)
{
	uint32_t batch, got = 0;

	if (!st_streaming || st_ring == NULL)
		return;
	while (budget > 0) {
		batch = budget > 24 ? 24 : budget;
		if (disc_read_raw(st_lba, batch, st_secbuf) != 0) {
			st_streaming = 0;
			return;
		}
		for (uint32_t i = 0; i < batch; i++) {
			const uint8_t *sec = st_secbuf + i * 2352;
			uint8_t submode = sec[18];
			uint16_t id, sc, ns;

			st_lba++;
			st_delivered++;
			if (submode & 0x04) {
				xa_decode_sector(sec);
				continue;
			}
			id = *(const uint16_t *)(sec + 24);
			if (id != 0x0160)
				continue;
			sc = *(const uint16_t *)(sec + 28);
			ns = *(const uint16_t *)(sec + 30);

			if (sc == 0) {
				/* new frame: needs ns contiguous slots */
				uint32_t used = (uint32_t)(
					(st_widx - st_ridx + (int)st_ringsize) %
					(int)st_ringsize);
				if (used + ns + 1 >= st_ringsize)
					return; /* ring full - consumer will drain */
				if ((uint32_t)st_widx + ns > st_ringsize) {
					/* wrap: REWIND marker */
					*(uint16_t *)st_slot_hdr(st_widx) =
						StREWIND;
					st_widx = 0;
				}
				st_frame_start = st_widx;
				st_next_frameid =
					*(const uint32_t *)(sec + 32);
			}
			/* ring space for this slot? */
			{
				uint32_t used = (uint32_t)(
					(st_widx - st_ridx + (int)st_ringsize) %
					(int)st_ringsize);
				if (used + 1 >= st_ringsize)
					return;
			}
			memcpy(st_slot_hdr(st_widx), sec + 24, 32);
			memcpy(st_slot_data(st_widx), sec + 56, 2016);
			if (sc == ns - 1)
				*(uint16_t *)st_slot_hdr(st_frame_start) =
					StCOMPLETE;
			st_widx = (st_widx + 1) % (int)st_ringsize;
			st_prev_loc = *(CdlLOC *)(sec + 24 + 28);
			StCdIntrFlag[0] = 1;
		}
		got += batch;
		budget -= batch;
	}
	(void)got;
}

/* sector budget: 2.5 per vsync since arm, +ring_size initial burst */
static uint32_t st_budget(void)
{
	uint32_t elapsed = web_vsync_count - st_vsync0;
	uint32_t allowed = st_ringsize + (elapsed * 5) / 2;
	if (st_delivered >= allowed)
		return 0;
	return allowed - st_delivered;
}

static void st_stream_arm(void)
{
	st_lba = cur_lba;
	st_delivered = 0;
	st_vsync0 = web_vsync_count;
	st_widx = st_ridx = 0;
	st_frame_start = 0;
	xa_frac = 0;
	memset((unsigned char *)xa_prev, 0, sizeof xa_prev);
	st_streaming = 1;
	emscripten_console_logf("str: arm lba=%u vsync=%u", st_lba,
				web_vsync_count);
}

void StSetRing(u_long *ring_addr, u_long ring_size)
{
	st_ring = ring_addr;
	st_ringsize = ring_size;
	StClearRing();
}

void StClearRing(void)
{
	st_widx = st_ridx = 0;
	st_frame_start = 0;
	st_next_frameid = 0;
	if (st_ring != NULL)
		memset((unsigned char *)st_ring, 0, (size_t)st_ringsize * 32);
	StCdIntrFlag[0] = 0;
}

void StUnSetRing(void)
{
	emscripten_console_logf("str: unset lba=%u del=%u vsync=%u fid=%u",
				st_lba, st_delivered, web_vsync_count,
				st_next_frameid);
	st_streaming = 0;
	st_ring = NULL;
	StCdIntrFlag[0] = 0;
}

void StSetStream(u_long mode, u_long start_frame, u_long end_frame,
		 void (*func1)(), void (*func2)())
{
	(void)mode;
	(void)start_frame;
	(void)end_frame;
	(void)func1;
	(void)func2;
}

void StSetEmulate(u_long *addr, u_long mode, u_long start_frame,
		  u_long end_frame, void (*func1)(), void (*func2)())
{
	(void)addr;
	(void)mode;
	(void)start_frame;
	(void)end_frame;
	(void)func1;
	(void)func2;
}

u_long StGetNext(u_long **addr, u_long **header)
{
	int spins = 0;

	for (;;) {
		u_short id = *(u_short *)st_slot_hdr(st_ridx);
		if (id == StREWIND) {
			st_ridx = 0;
			continue;
		}
		if (id == StCOMPLETE) {
			*(u_short *)st_slot_hdr(st_ridx) = StLOCK;
			*addr = (u_long *)st_slot_data(st_ridx);
			*header = (u_long *)st_slot_hdr(st_ridx);
			return 0;
		}
		{
			uint32_t b = st_budget();
			if (b > 0) {
				st_pump(b);
				continue;
			}
			if (++spins > 2000) {
				emscripten_console_logf(
					"str: getnext starved lba=%u del=%u",
					st_lba, st_delivered);
				return 1;
			}
			VSync(0); /* wait for the next sector burst */
		}
	}
}

u_long StGetNextS(u_long **addr, u_long **header)
{
	return StGetNext(addr, header);
}

u_short StNextStatus(u_long **addr, u_long **header)
{
	(void)addr;
	(void)header;
	return StCOMPLETE;
}

void StRingStatus(short *free_sectors, short *over_sectors)
{
	if (free_sectors != NULL)
		*free_sectors = 0;
	if (over_sectors != NULL)
		*over_sectors = 0;
}

void StSetMask(u_long mask, u_long start, u_long end)
{
	(void)mask;
	(void)start;
	(void)end;
}

void StCdInterrupt(void)
{
	/* called from MOV_onSliceDecoded when StCdIntrFlag was set;
	 * top up the ring within the current budget */
	uint32_t b = st_budget();
	if (b > 0)
		st_pump(b > 6 ? 6 : b);
	StCdIntrFlag[0] = 0;
}

int StGetBackloc(CdlLOC *loc)
{
	if (!st_streaming)
		return -1;
	*loc = st_prev_loc;
	return (int)st_next_frameid;
}

int StSetChannel(u_long channel)
{
	(void)channel;
	return 0;
}

u_long StFreeRing(u_long *base)
{
	uint8_t *data_base = (u_char *)st_ring + st_ringsize * 32;
	long idx;
	uint16_t ns, i;

	if (st_ring == NULL)
		return 0;
	idx = ((u_char *)base - data_base) / 2016;
	if (idx < 0 || idx >= (long)st_ringsize)
		return 0;
	if (*(u_short *)st_slot_hdr((int)idx) != StLOCK)
		return 0;
	ns = *(const uint16_t *)(st_slot_hdr((int)idx) + 6);
	if ((int)ns > 0) {
		for (i = 0; i < ns; i++)
			*(u_short *)st_slot_hdr((int)idx + i * 1) = StFREE;
	}
	st_ridx = ((int)idx + ns) % (int)st_ringsize;
	return 0;
}
