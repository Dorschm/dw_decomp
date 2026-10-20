/* liblibpress stubs for the web build - real PsyQ signatures. */

#include <libpress.h>

#include <web/host/log.h>

void DecDCTReset(int mode)
{
	web_unimplemented("DecDCTReset");
	(void)mode;
}

void DecDCTin(u_long *buf, int mode)
{
	web_unimplemented("DecDCTin");
	(void)buf;
	(void)mode;
}

void DecDCTout(u_long *buf, int size)
{
	web_unimplemented("DecDCTout");
	(void)buf;
	(void)size;
}

int DecDCToutCallback(void (*func)())
{
	web_unimplemented("DecDCToutCallback");
	(void)func;
	return (int)0;
}

int DecDCTvlc2(u_long *bs, u_long *buf, DECDCTTAB table)
{
	web_unimplemented("DecDCTvlc2");
	(void)bs;
	(void)buf;
	(void)table;
	return (int)0;
}

void DecDCTvlcBuild(u_short *table)
{
	web_unimplemented("DecDCTvlcBuild");
	(void)table;
}
