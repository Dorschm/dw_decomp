/* liblibcd stubs for the web build - real PsyQ signatures. */

#include <libcd.h>

#include <web/host/log.h>

/* stream interrupt flag, checked by src/mov/mov.c */
u_long StCdIntrFlag[4];

int CdControlB(u_char com, u_char *param, u_char *result)
{
	web_unimplemented("CdControlB");
	(void)com;
	(void)param;
	(void)result;
	return (int)0;
}

int CdInit(void)
{
	web_unimplemented("CdInit");
	return (int)0;
}

CdlLOC * CdIntToPos(int i, CdlLOC *p)
{
	web_unimplemented("CdIntToPos");
	(void)i;
	(void)p;
	return (CdlLOC *)0;
}

int CdPosToInt(CdlLOC *p)
{
	web_unimplemented("CdPosToInt");
	(void)p;
	return (int)0;
}

int CdRead(int sectors, u_long *buf, int mode)
{
	web_unimplemented("CdRead");
	(void)sectors;
	(void)buf;
	(void)mode;
	return (int)0;
}

int CdRead2(long mode)
{
	web_unimplemented("CdRead2");
	(void)mode;
	return (int)0;
}

int CdReadSync(int mode, u_char *result)
{
	web_unimplemented("CdReadSync");
	(void)mode;
	(void)result;
	return (int)0;
}

CdlFILE * CdSearchFile(CdlFILE *fp, char *name)
{
	web_unimplemented("CdSearchFile");
	(void)fp;
	(void)name;
	return (CdlFILE *)0;
}

void StCdInterrupt(void)
{
	web_unimplemented("StCdInterrupt");
}

u_long StFreeRing(u_long *base)
{
	web_unimplemented("StFreeRing");
	(void)base;
	return (u_long)0;
}

int StGetBackloc(CdlLOC *loc)
{
	web_unimplemented("StGetBackloc");
	(void)loc;
	return (int)0;
}

u_long StGetNext(u_long **addr, u_long **header)
{
	web_unimplemented("StGetNext");
	(void)addr;
	(void)header;
	return (u_long)0;
}

void StSetRing(u_long *ring_addr, u_long ring_size)
{
	web_unimplemented("StSetRing");
	(void)ring_addr;
	(void)ring_size;
}

void StSetStream(u_long mode, u_long start_frame, u_long end_frame, void (*func1)(), void (*func2)())
{
	web_unimplemented("StSetStream");
	(void)mode;
	(void)start_frame;
	(void)end_frame;
	(void)func1;
	(void)func2;
}

void StUnSetRing(void)
{
	web_unimplemented("StUnSetRing");
}

int CdControl(u_char com, u_char *param, u_char *result)
{
	(void)com; (void)param; (void)result;
	web_unimplemented("CdControl");
	return 0;
}
