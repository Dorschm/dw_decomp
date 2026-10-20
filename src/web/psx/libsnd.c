/* liblibsnd stubs for the web build - real PsyQ signatures. */

#include <libsnd.h>

#include <web/host/log.h>

void SsEnd(void)
{
	web_unimplemented("SsEnd");
}

void SsInit(void)
{
	web_unimplemented("SsInit");
}

void SsQuit(void)
{
	web_unimplemented("SsQuit");
}

void SsSeqClose(short a0)
{
	web_unimplemented("SsSeqClose");
	(void)a0;
}

short SsSeqOpen(unsigned long* a0, short a1)
{
	web_unimplemented("SsSeqOpen");
	(void)a0;
	(void)a1;
	return (short)0;
}

void SsSeqPlay(short a0, char a1, short a2)
{
	web_unimplemented("SsSeqPlay");
	(void)a0;
	(void)a1;
	(void)a2;
}

void SsSeqSetVol(short a0, short a1, short a2)
{
	web_unimplemented("SsSeqSetVol");
	(void)a0;
	(void)a1;
	(void)a2;
}

void SsSeqStop(short a0)
{
	web_unimplemented("SsSeqStop");
	(void)a0;
}

void SsSetMVol(short a0, short a1)
{
	web_unimplemented("SsSetMVol");
	(void)a0;
	(void)a1;
}

void SsSetTableSize(char* a0, short a1, short a2)
{
	web_unimplemented("SsSetTableSize");
	(void)a0;
	(void)a1;
	(void)a2;
}

void SsSetTickMode(long a0)
{
	web_unimplemented("SsSetTickMode");
	(void)a0;
}

void SsStart(void)
{
	web_unimplemented("SsStart");
}

void SsUtAllKeyOff(short a0)
{
	web_unimplemented("SsUtAllKeyOff");
	(void)a0;
}

short SsUtAutoVol(short a0, short a1, short a2, short a3)
{
	web_unimplemented("SsUtAutoVol");
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	return (short)0;
}

unsigned long SsUtGetVBaddrInSB(short a0)
{
	web_unimplemented("SsUtGetVBaddrInSB");
	(void)a0;
	return (unsigned long)0;
}

short SsUtKeyOffV(short voice)
{
	web_unimplemented("SsUtKeyOffV");
	(void)voice;
	return (short)0;
}

short SsUtKeyOnV(short voice, short vabId, short prog, short tone, short note, short fine, short voll, short volr)
{
	web_unimplemented("SsUtKeyOnV");
	(void)voice;
	(void)vabId;
	(void)prog;
	(void)tone;
	(void)note;
	(void)fine;
	(void)voll;
	(void)volr;
	return (short)0;
}

void SsVabClose(short a0)
{
	web_unimplemented("SsVabClose");
	(void)a0;
}

short SsVabOpenHeadSticky(unsigned char* a0, short a1, unsigned long a2)
{
	web_unimplemented("SsVabOpenHeadSticky");
	(void)a0;
	(void)a1;
	(void)a2;
	return (short)0;
}

short SsVabTransBody(unsigned char* a0, short a1)
{
	web_unimplemented("SsVabTransBody");
	(void)a0;
	(void)a1;
	return (short)0;
}

short SsVabTransCompleted(short a0)
{
	web_unimplemented("SsVabTransCompleted");
	(void)a0;
	return (short)0;
}
