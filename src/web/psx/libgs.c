/* libgs stubs for the web build - real PsyQ signatures. */

#include <libgs.h>

#include <web/host/log.h>

MATRIX GsWSMATRIX;
MATRIX GsIDMATRIX = { { {0x1000,0,0}, {0,0x1000,0}, {0,0,0x1000} }, {0,0,0} };
MATRIX GsLIGHTWSMATRIX;
_GsFCALL GsFCALL4;

void GsSetNearClip(long clip)
{
	(void)clip;
	web_unimplemented("GsSetNearClip");
}

PACKET *GsTMDdivTF3NL(void)
{
	web_unimplemented("GsTMDdivTF3NL");
	return (PACKET *)0;
}

PACKET *GsTMDdivTF4L(void)
{
	web_unimplemented("GsTMDdivTF4L");
	return (PACKET *)0;
}

PACKET *GsTMDdivTF4NL(void)
{
	web_unimplemented("GsTMDdivTF4NL");
	return (PACKET *)0;
}

PACKET *GsTMDdivTG3NL(void)
{
	web_unimplemented("GsTMDdivTG3NL");
	return (PACKET *)0;
}

PACKET *GsTMDdivTG4NL(void)
{
	web_unimplemented("GsTMDdivTG4NL");
	return (PACKET *)0;
}

PACKET *GsTMDdivTNF3(void)
{
	web_unimplemented("GsTMDdivTNF3");
	return (PACKET *)0;
}

PACKET *GsTMDdivTNF4(void)
{
	web_unimplemented("GsTMDdivTNF4");
	return (PACKET *)0;
}

PACKET *GsTMDdivTNG3(void)
{
	web_unimplemented("GsTMDdivTNG3");
	return (PACKET *)0;
}

PACKET *GsTMDdivTNG4(void)
{
	web_unimplemented("GsTMDdivTNG4");
	return (PACKET *)0;
}

PACKET *GsTMDfastF3L(void)
{
	web_unimplemented("GsTMDfastF3L");
	return (PACKET *)0;
}

PACKET *GsTMDfastF4L(void)
{
	web_unimplemented("GsTMDfastF4L");
	return (PACKET *)0;
}

PACKET *GsTMDfastF4NL(void)
{
	web_unimplemented("GsTMDfastF4NL");
	return (PACKET *)0;
}

PACKET *GsTMDfastG3L(void)
{
	web_unimplemented("GsTMDfastG3L");
	return (PACKET *)0;
}

PACKET *GsTMDfastG4L(void)
{
	web_unimplemented("GsTMDfastG4L");
	return (PACKET *)0;
}

PACKET *GsTMDfastNF4(void)
{
	web_unimplemented("GsTMDfastNF4");
	return (PACKET *)0;
}

PACKET *GsTMDfastTF3L(void)
{
	web_unimplemented("GsTMDfastTF3L");
	return (PACKET *)0;
}

PACKET *GsTMDfastTF3NL(void)
{
	web_unimplemented("GsTMDfastTF3NL");
	return (PACKET *)0;
}

PACKET *GsTMDfastTF4L(void)
{
	web_unimplemented("GsTMDfastTF4L");
	return (PACKET *)0;
}

PACKET *GsTMDfastTF4NL(void)
{
	web_unimplemented("GsTMDfastTF4NL");
	return (PACKET *)0;
}

PACKET *GsTMDfastTG3L(void)
{
	web_unimplemented("GsTMDfastTG3L");
	return (PACKET *)0;
}

PACKET *GsTMDfastTG3NL(void)
{
	web_unimplemented("GsTMDfastTG3NL");
	return (PACKET *)0;
}

PACKET *GsTMDfastTG4L(void)
{
	web_unimplemented("GsTMDfastTG4L");
	return (PACKET *)0;
}

PACKET *GsTMDfastTG4NL(void)
{
	web_unimplemented("GsTMDfastTG4NL");
	return (PACKET *)0;
}

PACKET *GsTMDfastTNF3(void)
{
	web_unimplemented("GsTMDfastTNF3");
	return (PACKET *)0;
}

PACKET *GsTMDfastTNF4(void)
{
	web_unimplemented("GsTMDfastTNF4");
	return (PACKET *)0;
}

PACKET *GsTMDfastTNG3(void)
{
	web_unimplemented("GsTMDfastTNG3");
	return (PACKET *)0;
}

PACKET *GsTMDfastTNG4(void)
{
	web_unimplemented("GsTMDfastTNG4");
	return (PACKET *)0;
}

static PACKET *gs_work_base;

void GsSetWorkBase(PACKET *outpacketp)
{
	gs_work_base = outpacketp;
}

PACKET *GsGetWorkBase(void)
{
	return gs_work_base;
}

void GsClearOt(unsigned short offset, unsigned short point, GsOT * otp)
{
	web_unimplemented("GsClearOt");
	(void)offset;
	(void)point;
	(void)otp;
}

void GsDefDispBuff(unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1)
{
	web_unimplemented("GsDefDispBuff");
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

void GsDrawOt(GsOT * ot)
{
	web_unimplemented("GsDrawOt");
	(void)ot;
}

int GsGetActiveBuff(void)
{
	web_unimplemented("GsGetActiveBuff");
	return (int)0;
}

void GsGetLs(GsCOORDINATE2 * m, MATRIX * out)
{
	web_unimplemented("GsGetLs");
	(void)m;
	(void)out;
}

void GsGetLw(GsCOORDINATE2 * m, MATRIX * out)
{
	web_unimplemented("GsGetLw");
	(void)m;
	(void)out;
}

void GsGetLws(GsCOORDINATE2 * m, MATRIX * outw, MATRIX * outs)
{
	web_unimplemented("GsGetLws");
	(void)m;
	(void)outw;
	(void)outs;
}

void GsGetTimInfo(unsigned long *im, GsIMAGE * tim)
{
	web_unimplemented("GsGetTimInfo");
	(void)*im;
	(void)tim;
}

void GsInit3D(void)
{
	web_unimplemented("GsInit3D");
}

void GsInitCoordinate2(GsCOORDINATE2 * super, GsCOORDINATE2 * base)
{
	web_unimplemented("GsInitCoordinate2");
	(void)super;
	(void)base;
}

void GsInitGraph(unsigned short x, unsigned short y, unsigned short intmode, unsigned short dith, unsigned short varmmode)
{
	web_unimplemented("GsInitGraph");
	(void)x;
	(void)y;
	(void)intmode;
	(void)dith;
	(void)varmmode;
}

void GsLinkObject4(unsigned long tmd_base, GsDOBJ2 * objp, int n)
{
	web_unimplemented("GsLinkObject4");
	(void)tmd_base;
	(void)objp;
	(void)n;
}

void GsMapModelingData(unsigned long *p)
{
	web_unimplemented("GsMapModelingData");
	(void)*p;
}

void GsMulCoord0(MATRIX * m1, MATRIX * m2, MATRIX * m3)
{
	web_unimplemented("GsMulCoord0");
	(void)m1;
	(void)m2;
	(void)m3;
}

void GsMulCoord3(MATRIX * m1, MATRIX * m2)
{
	web_unimplemented("GsMulCoord3");
	(void)m1;
	(void)m2;
}

void GsSetAmbient(long r, long g, long b)
{
	web_unimplemented("GsSetAmbient");
	(void)r;
	(void)g;
	(void)b;
}

int GsSetFlatLight(int id, GsF_LIGHT * lt)
{
	web_unimplemented("GsSetFlatLight");
	(void)id;
	(void)lt;
	return (int)0;
}

void GsSetLightMatrix(MATRIX * mp)
{
	web_unimplemented("GsSetLightMatrix");
	(void)mp;
}

void GsSetLightMode(int mode)
{
	web_unimplemented("GsSetLightMode");
	(void)mode;
}

void GsSetLsMatrix(MATRIX * mp)
{
	web_unimplemented("GsSetLsMatrix");
	(void)mp;
}

void GsSetOrign(long x, long y)
{
	web_unimplemented("GsSetOrign");
	(void)x;
	(void)y;
}

void GsSetProjection(long h)
{
	web_unimplemented("GsSetProjection");
	(void)h;
}

int GsSetRefView2(GsRVIEW2 * pv)
{
	web_unimplemented("GsSetRefView2");
	(void)pv;
	return (int)0;
}

int GsSetView2(GsVIEW2 * pv)
{
	web_unimplemented("GsSetView2");
	(void)pv;
	return (int)0;
}

void GsSortBoxFill(GsBOXF * bp, GsOT * ot, unsigned short pri)
{
	web_unimplemented("GsSortBoxFill");
	(void)bp;
	(void)ot;
	(void)pri;
}

void GsSortClear(unsigned char a0, unsigned char a1, unsigned char a2, GsOT * a3)
{
	web_unimplemented("GsSortClear");
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

void GsSortFastSprite(GsSPRITE * sp, GsOT * ot, unsigned short pri)
{
	web_unimplemented("GsSortFastSprite");
	(void)sp;
	(void)ot;
	(void)pri;
}

void GsSortObject4J(GsDOBJ2 *objp, GsOT *otp, int shift, u_long *scratch)
{
	web_unimplemented("GsSortObject4J");
	(void)*objp;
	(void)*otp;
	(void)shift;
	(void)*scratch;
}

GsOT * GsSortOt(GsOT * ot_src, GsOT * ot_dest)
{
	web_unimplemented("GsSortOt");
	(void)ot_src;
	(void)ot_dest;
	return (GsOT *)0;
}

void GsSortPoly(void *pp, GsOT * ot, unsigned short pri)
{
	web_unimplemented("GsSortPoly");
	(void)pp;
	(void)ot;
	(void)pri;
}

void GsSortSprite(GsSPRITE * sp, GsOT * ot, unsigned short pri)
{
	web_unimplemented("GsSortSprite");
	(void)sp;
	(void)ot;
	(void)pri;
}

void GsSwapDispBuff(void)
{
	web_unimplemented("GsSwapDispBuff");
}

