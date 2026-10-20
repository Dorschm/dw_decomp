/* liblibspu stubs for the web build - real PsyQ signatures. */

#include <libspu.h>

#include <web/host/log.h>

long SpuClearReverbWorkArea(long mode)
{
	web_unimplemented("SpuClearReverbWorkArea");
	(void)mode;
	return (long)0;
}

long SpuGetKeyStatus(unsigned long voice_bit)
{
	web_unimplemented("SpuGetKeyStatus");
	(void)voice_bit;
	return (long)0;
}

long SpuSetReverb(long on_off)
{
	web_unimplemented("SpuSetReverb");
	(void)on_off;
	return (long)0;
}

long SpuSetReverbDepth(SpuReverbAttr *attr)
{
	web_unimplemented("SpuSetReverbDepth");
	(void)attr;
	return (long)0;
}

long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
	web_unimplemented("SpuSetReverbModeParam");
	(void)attr;
	return (long)0;
}

unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit)
{
	web_unimplemented("SpuSetReverbVoice");
	(void)on_off;
	(void)voice_bit;
	return (unsigned long)0;
}
