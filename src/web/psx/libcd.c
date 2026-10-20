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
	return 1;
}

int CdReset(int mode)
{
	(void)mode;
	return 0;
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
	(void)mode;
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
	(void)vol;
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
		if (n == 0 && c == 0)
			return 1;
		if (n == 0 || c == 0)
			return 0;
		if (to_lower(c) != to_lower(n))
			return 0;
	}
	return name[ilen] == 0;
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
				 &isdir, fp->name))
			return NULL;
		if (slash == NULL)
			break;
		if (!isdir)
			return NULL;
		p = slash + 1;
	}

	CdIntToPos((int)extent, &fp->pos);
	fp->size = size;
	return fp;
}

/* ---------------------------------------------------------------- */
/* streaming (movies are a later segment - stubs that can't hang)    */

void StSetRing(u_long *ring_addr, u_long ring_size)
{
	(void)ring_addr;
	(void)ring_size;
}

void StClearRing(void)
{
}

void StUnSetRing(void)
{
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

/*
 * A fabricated stream frame: StGetNext returns a header whose
 * frameCount is past the end of every movie's frame table, so
 * MOV_readFrame sets MOVIE_COMPLETED and the play loop exits instead
 * of spinning on the stub forever.
 */
u_long StGetNext(u_long **addr, u_long **header)
{
	static u_long fake_bs[512];
	static StHEADER fake_header = {
		.id = 0x0160,
		.type = 0x8001,
		.secCount = 1,
		.nSectors = 1,
		.frameCount = 0x7FFFFFFF,
		.frameSize = 0x800,
		.width = 320,
		.height = 240,
	};

	*addr = fake_bs;
	*header = (u_long *)&fake_header;
	return 0;
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
}

int StGetBackloc(CdlLOC *loc)
{
	(void)loc;
	return 0;
}

int StSetChannel(u_long channel)
{
	(void)channel;
	return 0;
}

u_long StFreeRing(u_long *base)
{
	(void)base;
	return 0;
}
