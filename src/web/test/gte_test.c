/* Standalone GTE sanity tests - runs under node via the emcc build.
 * Links only src/web/psx/gte.c; no PsyQ headers.
 *
 * Reference values hand-computed from the psx-spx GTE spec:
 *   RTPS:  MAC123 = TR<<12 + RT*v;  SZ3 = sat(MAC3);
 *          h_over_sz3 = UNR(H, SZ3) (unsigned Newton-Raphson);
 *          SXY2 = sat(OFX + IR1*h_over_sz3) >>16 / same for Y.
 *   NCLIP: MAC0 = sx0*sy1 + sx1*sy2 + sx2*sy0 - sx0*sy2 - sx1*sy0
 *                - sx2*sy1.
 *   AVSZ3: MAC0 = ZSF3*(SZ1+SZ2+SZ3); OTZ = sat(MAC0>>12).
 */
#include <stdio.h>
#include <stdint.h>
#include "../psx/gte.h"

static int fails;

#define CHECK(name, got, exp) do { \
	long _g = (long)(got), _e = (long)(exp); \
	if (_g == _e) \
		printf("PASS %-28s got=0x%lx\n", name, _g); \
	else { \
		printf("FAIL %-28s got=0x%lx want=0x%lx\n", name, _g, _e); \
		fails++; \
	} \
} while (0)

static void set_rt_identity(void)
{
	/* RT11RT12, RT13RT21, RT22RT23, RT31RT32, RT33 */
	gte_scr(0, 0x00001000);
	gte_scr(1, 0);
	gte_scr(2, 0x00001000);
	gte_scr(3, 0);
	gte_scr(4, 0x00001000);
}

static void set_tr(int32_t x, int32_t y, int32_t z)
{
	gte_scr(5, (uint32_t)x);
	gte_scr(6, (uint32_t)y);
	gte_scr(7, (uint32_t)z);
}

static void load_v0(int16_t x, int16_t y, int16_t z)
{
	gte_str(0, (uint16_t)x | ((uint32_t)(uint16_t)y << 16));
	gte_str(1, (uint16_t)z);
}

int main(void)
{
	set_rt_identity();
	set_tr(0, 0, 0);
	gte_scr(24, 0);		/* OFX */
	gte_scr(25, 0);		/* OFY */
	gte_scr(26, 0x1000);	/* H   */

	/* RTPS v=(0,0,0x1000): SZ3=0x1000, H/SZ3 = 0x20000 -> x,y stay 0 */
	load_v0(0, 0, 0x1000);
	gte_cmd(GTE_RTPS);
	CHECK("rtps.sz3", gte_ldr(19), 0x1000);
	CHECK("rtps.sx2", (int16_t)(gte_ldr(14) & 0xffff), 0);
	CHECK("rtps.sy2", (int16_t)(gte_ldr(14) >> 16), 0);

	/* RTPS v=(0x100,0x80,0x1000): H/SZ3 = 1.0 (16.16) ->
	 * sx2 = 0x100, sy2 = 0x80 */
	load_v0(0x100, 0x80, 0x1000);
	gte_cmd(GTE_RTPS);
	CHECK("rtps2.sz3", gte_ldr(19), 0x1000);
	CHECK("rtps2.sx2", (int16_t)(gte_ldr(14) & 0xffff), 0x100);
	CHECK("rtps2.sy2", (int16_t)(gte_ldr(14) >> 16), 0x80);

	/* RTPS v=(0x100,0,0x2000): UNR H=0x1000 SZ3=0x2000 -> quot 0x8000
	 * (0.5) -> sx2 = 0x80 */
	load_v0(0x100, 0, 0x2000);
	gte_cmd(GTE_RTPS);
	CHECK("rtps3.sz3", gte_ldr(19), 0x2000);
	CHECK("rtps3.sx2", (int16_t)(gte_ldr(14) & 0xffff), 0x80);

	/* NCLIP: (0,0),(1,0),(0,1) -> MAC0 = +1 */
	gte_str(12, 0);
	gte_str(13, 1);		/* sx=1, sy=0 */
	gte_str(14, 0x10000);	/* sx=0, sy=1 */
	gte_cmd(GTE_NCLIP);
	CHECK("nclip.pos", gte_ldr(24), 1);
	/* reversed winding -> -1 */
	gte_str(13, 0x10000);
	gte_str(14, 1);
	gte_cmd(GTE_NCLIP);
	CHECK("nclip.neg", (int32_t)gte_ldr(24), -1);

	/* MVMVA sf=1 mx=0(RT) v=0 cv=0(TR=0) lm=0, identity RT, v0=(1,2,3) */
	load_v0(1, 2, 3);
	gte_cmd(0x01000012 | (1 << 19));	/* mvmva sf1 mx0 v0 cv0 lm0 */
	CHECK("mvmva.ir1", (int16_t)gte_ldr(9), 1);
	CHECK("mvmva.ir2", (int16_t)gte_ldr(10), 2);
	CHECK("mvmva.ir3", (int16_t)gte_ldr(11), 3);
	CHECK("mvmva.mac1", (int32_t)gte_ldr(25), 1);

	/* AVSZ3: sz1=sz2=sz3=0x100, ZSF3=0x1000 -> MAC0=0x300<<12, OTZ=0x300 */
	gte_scr(29, 0x1000);
	gte_str(17, 0x100);
	gte_str(18, 0x100);
	gte_str(19, 0x100);
	gte_cmd(GTE_AVSZ3);
	CHECK("avsz3.mac0", gte_ldr(24), 0x300 << 12);
	CHECK("avsz3.otz", gte_ldr(7), 0x300);

	printf(fails ? "gte_test: %d FAILURES\n" : "gte_test: all pass\n", fails);
	return fails ? 1 : 0;
}
