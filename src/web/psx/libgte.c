/*
 * libgte stubs for the web build.
 * rsin/rcos/ratan2/SquareRoot0 are implemented for real (fixed-point),
 * the rest log once via web_unimplemented and return 0.
 */

#include <libgte.h>

#include <math.h>
#include <stdlib.h>

#include <web/host/log.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * PsyQ rsin/rcos use a 4096-entry lookup table of sin at 2pi/4096 steps
 * scaled to 1<<12. TODO(port): replace with the exact PsyQ table so
 * results match the retail binary bit-for-bit.
 */
int rsin(int a)
{
	return (int)lrint(sin((double)a * (2.0 * M_PI / 4096.0)) * 4096.0);
}

int rcos(int a)
{
	return (int)lrint(cos((double)a * (2.0 * M_PI / 4096.0)) * 4096.0);
}

/* TODO(port): verify against the PsyQ asm (returns 12-bit fixed angle). */
long ratan2(long y, long x)
{
	return lrint(atan2((double)y, (double)x) * (4096.0 / (2.0 * M_PI)));
}

/* TODO(port): verify against the PsyQ asm. */
long SquareRoot0(long a)
{
	if (a <= 0) {
		return 0;
	}
	return (long)sqrt((double)a);
}

long SquareRoot12(long a)
{
	if (a <= 0) {
		return 0;
	}
	return (long)lrint(sqrt((double)a) * 4096.0);
}

void InvSquareRoot(long a, long *b, long *c)
{
	(void)a;
	web_unimplemented("InvSquareRoot");
	if (b) {
		*b = 0;
	}
	if (c) {
		*c = 0;
	}
}


VECTOR * ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{
	web_unimplemented("ApplyMatrix");
	(void)m;
	(void)v0;
	(void)v1;
	return (VECTOR *)0;
}

VECTOR * ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	web_unimplemented("ApplyMatrixLV");
	(void)m;
	(void)v0;
	(void)v1;
	return (VECTOR *)0;
}

SVECTOR * ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{
	web_unimplemented("ApplyMatrixSV");
	(void)m;
	(void)v0;
	(void)v1;
	return (SVECTOR *)0;
}

VECTOR * ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	web_unimplemented("ApplyTransposeMatrixLV");
	(void)m;
	(void)v0;
	(void)v1;
	return (VECTOR *)0;
}

MATRIX * CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	web_unimplemented("CompMatrix");
	(void)m0;
	(void)m1;
	(void)m2;
	return (MATRIX *)0;
}

MATRIX * MulMatrix(MATRIX *m0, MATRIX *m1)
{
	web_unimplemented("MulMatrix");
	(void)m0;
	(void)m1;
	return (MATRIX *)0;
}

MATRIX * MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	web_unimplemented("MulMatrix0");
	(void)m0;
	(void)m1;
	(void)m2;
	return (MATRIX *)0;
}

void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2)
{
	web_unimplemented("NormalColorCol");
	(void)v0;
	(void)v1;
	(void)v2;
}

void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, CVECTOR *v3, CVECTOR *v4, CVECTOR *v5, CVECTOR *v6)
{
	web_unimplemented("NormalColorCol3");
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;
	(void)v4;
	(void)v5;
	(void)v6;
}

void PopMatrix(void)
{
	web_unimplemented("PopMatrix");
}

void PushMatrix(void)
{
	web_unimplemented("PushMatrix");
}

MATRIX * RotMatrix(SVECTOR *r, MATRIX *m)
{
	web_unimplemented("RotMatrix");
	(void)r;
	(void)m;
	return (MATRIX *)0;
}

MATRIX * RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
	web_unimplemented("RotMatrixYXZ");
	(void)r;
	(void)m;
	return (MATRIX *)0;
}

MATRIX * RotMatrixZYX(SVECTOR *r, MATRIX *m)
{
	web_unimplemented("RotMatrixZYX");
	(void)r;
	(void)m;
	return (MATRIX *)0;
}

long RotNclip3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, long *sxy0, long *sxy1, long *sxy2, long *p, long *otz, long *flag)
{
	web_unimplemented("RotNclip3");
	(void)v0;
	(void)v1;
	(void)v2;
	(void)sxy0;
	(void)sxy1;
	(void)sxy2;
	(void)p;
	(void)otz;
	(void)flag;
	return (long)0;
}

long RotNclip4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, long *sxy0, long *sxy1, long *sxy2, long *sxy3, long *p, long *otz, long *flag)
{
	web_unimplemented("RotNclip4");
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;
	(void)sxy0;
	(void)sxy1;
	(void)sxy2;
	(void)sxy3;
	(void)p;
	(void)otz;
	(void)flag;
	return (long)0;
}

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag)
{
	web_unimplemented("RotTransPers");
	(void)v0;
	(void)sxy;
	(void)p;
	(void)flag;
	return (long)0;
}

long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, long *sxy0, long *sxy1, long *sxy2, long *p, long *flag)
{
	web_unimplemented("RotTransPers3");
	(void)v0;
	(void)v1;
	(void)v2;
	(void)sxy0;
	(void)sxy1;
	(void)sxy2;
	(void)p;
	(void)flag;
	return (long)0;
}

long RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, long *sxy0, long *sxy1, long *sxy2, long *sxy3, long *p, long *flag)
{
	web_unimplemented("RotTransPers4");
	(void)v0;
	(void)v1;
	(void)v2;
	(void)v3;
	(void)sxy0;
	(void)sxy1;
	(void)sxy2;
	(void)sxy3;
	(void)p;
	(void)flag;
	return (long)0;
}

MATRIX * ScaleMatrix(MATRIX *m, VECTOR *v)
{
	web_unimplemented("ScaleMatrix");
	(void)m;
	(void)v;
	return (MATRIX *)0;
}

void SetLightMatrix(MATRIX *m)
{
	web_unimplemented("SetLightMatrix");
	(void)m;
}

void SetRotMatrix(MATRIX *m)
{
	web_unimplemented("SetRotMatrix");
	(void)m;
}

void SetTransMatrix(MATRIX *m)
{
	web_unimplemented("SetTransMatrix");
	(void)m;
}

MATRIX * TransMatrix(MATRIX *m, VECTOR *v)
{
	web_unimplemented("TransMatrix");
	(void)m;
	(void)v;
	return (MATRIX *)0;
}

MATRIX * TransposeMatrix(MATRIX *m0, MATRIX *m1)
{
	web_unimplemented("TransposeMatrix");
	(void)m0;
	(void)m1;
	return (MATRIX *)0;
}
