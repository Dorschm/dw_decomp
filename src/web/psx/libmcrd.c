/* liblibmcrd stubs for the web build - real PsyQ signatures. */

struct DIRENTRY;
#include <libmcrd.h>

#include <web/host/log.h>

long MemCardAccept(long chan)
{
	web_unimplemented("MemCardAccept");
	(void)chan;
	return (long)0;
}

long MemCardCreateFile(long chan, char* fnam, long blocks)
{
	web_unimplemented("MemCardCreateFile");
	(void)chan;
	(void)fnam;
	(void)blocks;
	return (long)0;
}

long MemCardDeleteFile(long chan, char* fnam)
{
	web_unimplemented("MemCardDeleteFile");
	(void)chan;
	(void)fnam;
	return (long)0;
}

long MemCardExist(long chan)
{
	web_unimplemented("MemCardExist");
	(void)chan;
	return (long)0;
}

long MemCardFormat(long chan)
{
	web_unimplemented("MemCardFormat");
	(void)chan;
	return (long)0;
}

long MemCardGetDirentry(long chan, char* name, struct DIRENTRY* pdir, long* files, long ofs, long max)
{
	web_unimplemented("MemCardGetDirentry");
	(void)chan;
	(void)name;
	(void)pdir;
	(void)files;
	(void)ofs;
	(void)max;
	return (long)0;
}

long MemCardReadFile(long chan, char* fnam, long* adrs, long ofs, long bytes)
{
	web_unimplemented("MemCardReadFile");
	(void)chan;
	(void)fnam;
	(void)adrs;
	(void)ofs;
	(void)bytes;
	return (long)0;
}

long MemCardSync(long mode, unsigned long* cmds, unsigned long* rslt)
{
	web_unimplemented("MemCardSync");
	(void)mode;
	(void)cmds;
	(void)rslt;
	return (long)0;
}

long MemCardWriteFile(long chan, char* fnam, long* adrs, long ofs, long bytes)
{
	web_unimplemented("MemCardWriteFile");
	(void)chan;
	(void)fnam;
	(void)adrs;
	(void)ofs;
	(void)bytes;
	return (long)0;
}
