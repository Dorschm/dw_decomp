/*
 * GTE (cop2) stub implementation for the web build.
 * Loads store argument bytes into gte_regs, stores read it back, and the
 * transform ops are no-ops until a real GTE interpreter lands.
 */

#include <string.h>

#include <web/psx/gte.h>

GteRegs gte_regs;

static uint32_t load_vxy(const void *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

static uint32_t load_vz(const void *p)
{
	int16_t z;
	memcpy(&z, (const char *)p + 4, 2);
	return (uint16_t)z;
}

static void load_vector(int base, const void *p)
{
	gte_regs.d[base] = load_vxy(p);
	gte_regs.d[base + 1] = load_vz(p);
}

void gte_ldv0_c(const void *r0)
{
	load_vector(0, r0);
}

void gte_ldv3_c(const void *r0, const void *r1, const void *r2)
{
	load_vector(0, r0);
	load_vector(2, r1);
	load_vector(4, r2);
}

void gte_ldlzc_c(int32_t r0)
{
	gte_regs.d[30] = (uint32_t)r0;
	gte_regs.d[31] = (r0 == 0) ? 32 : (uint32_t)__builtin_clz((uint32_t)r0 ^ ((uint32_t)r0 >> 31));
}

void gte_stlzc_c(void *r0)
{
	uint32_t v = gte_regs.d[31];
	memcpy(r0, &v, 4);
}

void gte_rtps_c(void)
{
}

void gte_rtpt_c(void)
{
}

void gte_stsxy_c(void *r0)
{
	uint32_t v = gte_regs.d[14];
	memcpy(r0, &v, 4);
}

void gte_stsxy3_c(void *r0, void *r1, void *r2)
{
	memcpy(r0, &gte_regs.d[12], 4);
	memcpy(r1, &gte_regs.d[13], 4);
	memcpy(r2, &gte_regs.d[14], 4);
}

void gte_stszotz_c(void *r0)
{
	uint32_t v = gte_regs.d[19] >> 2;
	memcpy(r0, &v, 4);
}

void gte_SetRotMatrix_c(const void *r0)
{
	memcpy(&gte_regs.c[0], r0, 20);
}

void gte_SetTransMatrix_c(const void *r0)
{
	const uint32_t *p = (const uint32_t *)r0;
	gte_regs.c[5] = p[5];
	gte_regs.c[6] = p[6];
	gte_regs.c[7] = p[7];
}
