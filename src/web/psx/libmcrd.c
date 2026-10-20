/* libmcrd for the web build - reports "no card" cleanly.
 * Every issue-style call records its function id; MemCardSync completes
 * it immediately with McErrCardNotExist, which is the no-card result the
 * game handles (CONNECTED_CARDS cleared, menu continues). */

struct DIRENTRY;
#include <libmcrd.h>

static long mc_last_func = McFuncExist;

long MemCardSync(long mode, unsigned long* cmds, unsigned long* rslt)
{
	(void)mode;
	if (cmds)
		*cmds = mc_last_func;
	if (rslt)
		*rslt = McErrCardNotExist;
	return 1;
}

long MemCardAccept(long chan)
{
	(void)chan;
	mc_last_func = McFuncAccept;
	return 0;
}

long MemCardCreateFile(long chan, char* fnam, long blocks)
{
	(void)chan;
	(void)fnam;
	(void)blocks;
	mc_last_func = McFuncWriteFile;
	return 0;
}

long MemCardDeleteFile(long chan, char* fnam)
{
	(void)chan;
	(void)fnam;
	mc_last_func = McFuncExist;
	return 0;
}

long MemCardExist(long chan)
{
	(void)chan;
	mc_last_func = McFuncExist;
	return 0;
}

long MemCardFormat(long chan)
{
	(void)chan;
	mc_last_func = McFuncAccept;
	return 0;
}

long MemCardGetDirentry(long chan, char* name, struct DIRENTRY* pdir, long* files, long ofs, long max)
{
	(void)chan;
	(void)name;
	(void)pdir;
	(void)ofs;
	(void)max;
	if (files)
		*files = 0;
	mc_last_func = McFuncExist;
	return 0;
}

long MemCardReadFile(long chan, char* fnam, long* adrs, long ofs, long bytes)
{
	(void)chan;
	(void)fnam;
	(void)adrs;
	(void)ofs;
	(void)bytes;
	mc_last_func = McFuncReadFile;
	return 0;
}

long MemCardWriteFile(long chan, char* fnam, long* adrs, long ofs, long bytes)
{
	(void)chan;
	(void)fnam;
	(void)adrs;
	(void)ofs;
	(void)bytes;
	mc_last_func = McFuncWriteFile;
	return 0;
}
