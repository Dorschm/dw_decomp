/*
 * libmcrd for the web build.
 *
 * Decompilation notes (asm/us/main/psyq/libmcrd.s):
 *  - MemCardExist/MemCardAccept record a function id in libmcrd_cards[0]
 *    (1 resp. 2) and issue through UserFuncOpen -> _card_exist_back /
 *    _card_accept_back; completion is delivered later by MemCardSync
 *    (or the registered callback). Return 1 when issued, 0 when busy.
 *  - MemCardCreateFile does a synchronous create in the issue call and
 *    returns a result code (0 ok / 6 already-exists / 7 block-full /
 *    -1 busy).
 *  - MemCardReadFile/WriteFile issue the transfer (return 1) and the
 *    data copy completes with the MemCardSync result.
 *  - MemCardFormat/MemCardUnformat/MemCardDeleteFile/MemCardGetDirentry
 *    likewise return result codes from the issue call.
 *  - MemCardSync(mode) returns -1 with nothing completed, 0 while a
 *    command is still running, 1 with the completed cmd+result. We have
 *    no real latency to emulate so commands complete immediately at the
 *    issue call; MemCardSync then delivers the stored result once.
 *
 * Card images: two emulated 128 KiB raw .mcr images (64 frames of 128
 * bytes; frame 0 = "MC" header, frames 1..15 = directory, 16..31 =
 * broken-sector list, blocks 1..15 = 8 KiB data). Slot 1 (chan 0x00)
 * present, slot 2 (chan 0x10) absent by default. Persistence lives in
 * src/web/host/memcard.c (/memcard/cardN.mcr, IDBFS in browser).
 */

/* Values and struct copied from external/psyq_headers/.../libmcrd.h and
 * kernel.h (the headers' void-returning decls for MemCardInit/Start
 * conflict with the game TUs' long-returning declarations, which the
 * wasm signature check requires us to match). */
#define McFuncExist		(1)
#define McFuncAccept		(2)
#define McFuncReadFile		(3)
#define McFuncWriteFile		(4)
#define McFuncReadData		(5)
#define McFuncWriteData		(6)

#define McErrNone		(0)
#define McErrCardNotExist	(1)
#define McErrCardInvalid	(2)
#define McErrNewCard		(3)
#define McErrNotFormat		(4)
#define McErrFileNotExist	(5)
#define McErrAlreadyExist	(6)
#define McErrBlockFull		(7)

struct DIRENTRY {
	char name[20];
	long attr;
	long size;
	struct DIRENTRY *next;
	long head;
	char system[4];
};

typedef void (*MemCB)(unsigned long cmds, unsigned long rslt);

#include <string.h>
#include <stdint.h>

#define MC_IMG   0x20000
#define MC_FRAME 0x80
#define MC_DIR1  1			/* first directory frame */
#define MC_BSL   16			/* broken-sector list frames */
#define MC_NDIR  15			/* 15 usable blocks */
#define MC_BLOCK(b) ((b) * 64 * MC_FRAME)	/* byte offset of block */

extern long web_mc_load(int slot, void *dst, long cap);
extern void web_mc_mark_dirty(int slot);

static uint8_t mc_img[MC_IMG];
static uint8_t mc_img2[MC_IMG];			/* slot 2 image, unused while absent */
static int mc_present[2] = {1, 0};
static int mc_loaded[2];
static int mc_new[2];				/* first-exist reports NewCard */

static uint8_t *mc_card(int slot) { return slot ? mc_img2 : mc_img; }

static int mc_xor(const uint8_t *frame)
{
	uint8_t x = 0;
	int i;
	for (i = 0; i < MC_FRAME - 1; i++)
		x ^= frame[i];
	return x;
}

static void mc_format_image(uint8_t *c)
{
	int i;
	memset(c, 0, MC_IMG);
	c[0] = 'M';
	c[1] = 'C';
	c[MC_FRAME - 1] = (uint8_t)mc_xor(c);
	for (i = 0; i < MC_NDIR; i++) {
		uint8_t *f = c + (MC_DIR1 + i) * MC_FRAME;
		f[0] = 0xA0;			/* free */
		f[MC_FRAME - 1] = (uint8_t)mc_xor(f);
	}
	for (i = 0; i < 16; i++) {
		uint8_t *f = c + (MC_BSL + i) * MC_FRAME;
		memset(f, 0xFF, MC_FRAME - 1);
		f[MC_FRAME - 1] = 0xFF;
	}
}

static void mc_load(int slot)
{
	uint8_t *c = mc_card(slot);
	if (mc_loaded[slot] || !mc_present[slot])
		return;
	mc_loaded[slot] = 1;
	if (web_mc_load(slot, c, MC_IMG) != MC_IMG ||
	    c[0] != 'M' || c[1] != 'C')
		mc_format_image(c);
	mc_new[slot] = 1;
}

/* last issued command/result, delivered by the next MemCardSync */
static long mc_cmd = -1, mc_result = -1;

static int mc_valid_chan(long chan)
{
	return chan == 0 || chan == 0x10;
}

static int mc_find_dir(uint8_t *c, const char *name)
{
	int i;
	for (i = 0; i < MC_NDIR; i++) {
		uint8_t *f = c + (MC_DIR1 + i) * MC_FRAME;
		if ((f[0] & 0xF0) == 0x50 &&
		    strncmp((char *)f + 0x0A, (char *)name, 20) == 0)
			return i;
	}
	return -1;
}

static int mc_free_dir(uint8_t *c)
{
	int i;
	for (i = 0; i < MC_NDIR; i++) {
		uint8_t *f = c + (MC_DIR1 + i) * MC_FRAME;
		if ((f[0] & 0xF0) == 0xA0)
			return i;
	}
	return -1;
}

/* glob match: '*' tail matches anything, '?' matches any char */
static int mc_name_match(const char *dir_name, const char *pat)
{
	int i;
	for (i = 0; i < 20 && pat[i]; i++) {
		if (pat[i] == '*')
			return 1;
		if (pat[i] != '?' && dir_name[i] != pat[i])
			return 0;
	}
	return 1;
}

long MemCardInit(long flg)
{
	(void)flg;
	return 0;
}

long MemCardStart(void)
{
	return 0;
}

long MemCardStop(void)
{
	return 0;
}

long MemCardEnd(void)
{
	return 0;
}

long MemCardSync(long mode, unsigned long *cmds, unsigned long *rslt)
{
	(void)mode;
	if (mc_cmd < 0) {
		if (cmds)
			*cmds = 0;
		if (rslt)
			*rslt = McErrCardNotExist;
		return -1;
	}
	if (cmds)
		*cmds = (unsigned long)mc_cmd;
	if (rslt)
		*rslt = (unsigned long)mc_result;
	mc_cmd = -1;
	return 1;
}

MemCB MemCardCallback(MemCB func)
{
	(void)func;
	return NULL;
}

long MemCardExist(long chan)
{
	int slot = (chan >> 4) & 1;
	mc_cmd = McFuncExist;
	if (!mc_valid_chan(chan) || !mc_present[slot]) {
		mc_result = McErrCardNotExist;
		return 0;
	}
	mc_load(slot);
	mc_result = mc_new[slot] ? McErrNewCard : McErrNone;
	mc_new[slot] = 0;
	return 1;
}

long MemCardAccept(long chan)
{
	int slot = (chan >> 4) & 1;
	mc_cmd = McFuncAccept;
	if (!mc_valid_chan(chan) || !mc_present[slot]) {
		mc_result = McErrCardNotExist;
		return 0;
	}
	mc_load(slot);
	mc_result = McErrNone;
	return 1;
}

long MemCardFormat(long chan)
{
	int slot = (chan >> 4) & 1;
	mc_cmd = McFuncExist;
	if (!mc_valid_chan(chan) || !mc_present[slot])
		return McErrCardNotExist;
	mc_load(slot);
	mc_format_image(mc_card(slot));
	web_mc_mark_dirty(slot);
	return McErrNone;
}

long MemCardUnformat(long chan)
{
	(void)chan;
	return 0;
}

long MemCardCreateFile(long chan, char *fnam, long blocks)
{
	int slot = (chan >> 4) & 1;
	uint8_t *c;
	int dir, i;
	(void)blocks;

	if (!mc_valid_chan(chan) || !mc_present[slot])
		return -1;
	mc_load(slot);
	c = mc_card(slot);
	if (mc_find_dir(c, fnam) >= 0)
		return McErrAlreadyExist;
	dir = mc_free_dir(c);
	if (dir < 0)
		return McErrBlockFull;
	for (i = dir + 1; i < MC_NDIR && blocks > 1; i++)
		blocks--;				/* single-block saves here */
	{
		uint8_t *f = c + (MC_DIR1 + dir) * MC_FRAME;
		memset(f, 0, MC_FRAME - 1);
		f[0] = 0x51;				/* in use, first block */
		f[4] = 0x00; f[5] = 0x20;		/* size 0x2000 (LE) */
		f[8] = 0xFF; f[9] = 0xFF;		/* next = end */
		memset(f + 0x0A, 0, 20);
		strncpy((char *)f + 0x0A, fnam, 20);
		f[MC_FRAME - 1] = (uint8_t)mc_xor(f);
	}
	memset(c + MC_BLOCK(1 + dir), 0, 64 * MC_FRAME);
	web_mc_mark_dirty(slot);
	return McErrNone;
}

long MemCardDeleteFile(long chan, char *fnam)
{
	int slot = (chan >> 4) & 1;
	uint8_t *c;
	int dir;

	mc_cmd = McFuncExist;
	if (!mc_valid_chan(chan) || !mc_present[slot])
		return -1;
	mc_load(slot);
	c = mc_card(slot);
	dir = mc_find_dir(c, fnam);
	if (dir < 0)
		return McErrFileNotExist;
	{
		uint8_t *f = c + (MC_DIR1 + dir) * MC_FRAME;
		f[0] = 0xA2;			/* deleted */
		f[MC_FRAME - 1] = (uint8_t)mc_xor(f);
	}
	web_mc_mark_dirty(slot);
	return McErrNone;
}

long MemCardGetDirentry(long chan, char *name, struct DIRENTRY *pdir,
			long *files, long ofs, long max)
{
	int slot = (chan >> 4) & 1;
	uint8_t *c;
	long count = 0;
	int i;

	mc_cmd = McFuncExist;
	if (!mc_valid_chan(chan) || !mc_present[slot])
		return -1;
	mc_load(slot);
	c = mc_card(slot);
	for (i = ofs; i < MC_NDIR; i++) {
		uint8_t *f = c + (MC_DIR1 + i) * MC_FRAME;
		if ((f[0] & 0xF0) != 0x50)
			continue;
		if (!mc_name_match((char *)f + 0x0A, name))
			continue;
		if (pdir && count < max) {
			struct DIRENTRY *d = pdir + count;
			memset((unsigned char *)d->name, 0, sizeof(d->name));
			strncpy(d->name, (char *)f + 0x0A, 20);
			d->size = f[1] | (f[2] << 8) | (f[3] << 16) |
				  (f[4] << 24);
		}
		count++;
	}
	if (files)
		*files = count;
	return 0;
}

long MemCardReadFile(long chan, char *fnam, long *adrs, long ofs, long bytes)
{
	int slot = (chan >> 4) & 1;
	uint8_t *c;
	int dir;

	mc_cmd = McFuncReadFile;
	if (!mc_valid_chan(chan) || !mc_present[slot]) {
		mc_result = McErrCardNotExist;
		return 0;
	}
	mc_load(slot);
	c = mc_card(slot);
	dir = mc_find_dir(c, fnam);
	if (dir < 0) {
		mc_result = McErrFileNotExist;
		return 0;
	}
	if (bytes > 0 && adrs) {
		long cap = MC_IMG - MC_BLOCK(1 + dir);
		if (bytes > cap - ofs)
			bytes = cap - ofs;
		if (bytes > 0)
			memcpy(adrs, c + MC_BLOCK(1 + dir) + ofs, bytes);
	}
	mc_result = McErrNone;
	return 1;
}

long MemCardWriteFile(long chan, char *fnam, long *adrs, long ofs,
		      long bytes)
{
	int slot = (chan >> 4) & 1;
	uint8_t *c;
	int dir;

	mc_cmd = McFuncWriteFile;
	if (!mc_valid_chan(chan) || !mc_present[slot]) {
		mc_result = McErrCardNotExist;
		return 0;
	}
	mc_load(slot);
	c = mc_card(slot);
	dir = mc_find_dir(c, fnam);
	if (dir < 0) {
		mc_result = McErrFileNotExist;
		return 0;
	}
	if (bytes > 0 && adrs) {
		long cap = MC_IMG - MC_BLOCK(1 + dir);
		if (bytes > cap - ofs)
			bytes = cap - ofs;
		if (bytes > 0)
			memcpy(c + MC_BLOCK(1 + dir) + ofs, adrs, bytes);
	}
	{
		uint8_t *f = c + (MC_DIR1 + dir) * MC_FRAME;
		long sz = f[4] | (f[5] << 8) | (f[6] << 16) | ((long)f[7] << 24);
		if (ofs + bytes > sz) {
			sz = ofs + bytes;
			f[4] = sz; f[5] = sz >> 8; f[6] = sz >> 16; f[7] = sz >> 24;
			f[MC_FRAME - 1] = (uint8_t)mc_xor(f);
		}
	}
	web_mc_mark_dirty(slot);
	mc_result = McErrNone;
	return 1;
}

/* used by host/memcard.c for browser save import */
const uint8_t *web_mc_image(int slot)
{
	return mc_present[slot] ? mc_card(slot) : NULL;
}

void web_mc_reload_image(int slot)
{
	if (slot < 0 || slot > 1 || !mc_present[slot])
		return;
	mc_loaded[slot] = 0;
	mc_load(slot);
}
