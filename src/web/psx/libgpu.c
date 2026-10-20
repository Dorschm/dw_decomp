/* liblibgpu stubs for the web build - real PsyQ signatures. */

#include <libgpu.h>

#include <web/host/log.h>

void AddPrim(void *ot, void *p)
{
	web_unimplemented("AddPrim");
	(void)ot;
	(void)p;
}

int ClearImage(RECT *rect, u_char r, u_char g, u_char b)
{
	web_unimplemented("ClearImage");
	(void)rect;
	(void)r;
	(void)g;
	(void)b;
	return (int)0;
}

int DrawSync(int mode)
{
	web_unimplemented("DrawSync");
	(void)mode;
	return (int)0;
}

u_short GetClut(int x, int y)
{
	web_unimplemented("GetClut");
	(void)x;
	(void)y;
	return (u_short)0;
}

u_short GetTPage(int tp, int abr, int x, int y)
{
	web_unimplemented("GetTPage");
	(void)tp;
	(void)abr;
	(void)x;
	(void)y;
	return (u_short)0;
}

u_short LoadClut(u_long *clut, int x, int y)
{
	web_unimplemented("LoadClut");
	(void)clut;
	(void)x;
	(void)y;
	return (u_short)0;
}

int LoadImage(RECT *rect, u_long *p)
{
	web_unimplemented("LoadImage");
	(void)rect;
	(void)p;
	return (int)0;
}

int MoveImage(RECT *rect, int x, int y)
{
	web_unimplemented("MoveImage");
	(void)rect;
	(void)x;
	(void)y;
	return (int)0;
}

int OpenTIM(u_long *addr)
{
	web_unimplemented("OpenTIM");
	(void)addr;
	return (int)0;
}

DISPENV * PutDispEnv(DISPENV *env)
{
	web_unimplemented("PutDispEnv");
	(void)env;
	return (DISPENV *)0;
}

DRAWENV * PutDrawEnv(DRAWENV *env)
{
	web_unimplemented("PutDrawEnv");
	(void)env;
	return (DRAWENV *)0;
}

TIM_IMAGE * ReadTIM(TIM_IMAGE *timimg)
{
	web_unimplemented("ReadTIM");
	(void)timimg;
	return (TIM_IMAGE *)0;
}

int ResetGraph(int mode)
{
	web_unimplemented("ResetGraph");
	(void)mode;
	return (int)0;
}

DISPENV * SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
	web_unimplemented("SetDefDispEnv");
	(void)env;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	return (DISPENV *)0;
}

void SetDispMask(int mask)
{
	web_unimplemented("SetDispMask");
	(void)mask;
}

void SetDrawLoad(DR_LOAD *p, RECT *rect)
{
	web_unimplemented("SetDrawLoad");
	(void)p;
	(void)rect;
}

void SetDrawOffset(DR_OFFSET *p, u_short *ofs)
{
	web_unimplemented("SetDrawOffset");
	(void)p;
	(void)ofs;
}

int SetGraphDebug(int level)
{
	web_unimplemented("SetGraphDebug");
	(void)level;
	return (int)0;
}

void SetLineF2(LINE_F2 *p)
{
	web_unimplemented("SetLineF2");
	(void)p;
}

void SetLineF3(LINE_F3 *p)
{
	web_unimplemented("SetLineF3");
	(void)p;
}

void SetLineF4(LINE_F4 *p)
{
	web_unimplemented("SetLineF4");
	(void)p;
}

void SetPolyF4(POLY_F4 *p)
{
	web_unimplemented("SetPolyF4");
	(void)p;
}

void SetPolyFT3(POLY_FT3 *p)
{
	web_unimplemented("SetPolyFT3");
	(void)p;
}

void SetPolyFT4(POLY_FT4 *p)
{
	web_unimplemented("SetPolyFT4");
	(void)p;
}

void SetPolyG4(POLY_G4 *p)
{
	web_unimplemented("SetPolyG4");
	(void)p;
}

void SetPolyGT4(POLY_GT4 *p)
{
	web_unimplemented("SetPolyGT4");
	(void)p;
}

void SetSemiTrans(void *p, int abe)
{
	web_unimplemented("SetSemiTrans");
	(void)p;
	(void)abe;
}

int StoreImage(RECT *rect, u_long *p)
{
	web_unimplemented("StoreImage");
	(void)rect;
	(void)p;
	return (int)0;
}
