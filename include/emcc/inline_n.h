/*
 * GTE Macro definitions - Emscripten/web version.
 * Every macro forwards to a C stub operating on gte_regs (see
 * src/web/psx/gte.h for the register map).
 */

#include <web/psx/gte.h>

#define gte_ldv0(r0) gte_ldv0_c(r0)
#define gte_ldv3(r0, r1, r2) gte_ldv3_c(r0, r1, r2)
#define gte_ldlzc(r0) gte_ldlzc_c(r0)
#define gte_stlzc(r0) gte_stlzc_c(r0)
#define gte_rtps() gte_rtps_c()
#define gte_rtpt() gte_rtpt_c()
#define gte_stsxy(r0) gte_stsxy_c(r0)
#define gte_stsxy3(r0, r1, r2) gte_stsxy3_c(r0, r1, r2)
#define gte_stszotz(r0) gte_stszotz_c(r0)
#define gte_SetRotMatrix(r0) gte_SetRotMatrix_c(r0)
#define gte_SetTransMatrix(r0) gte_SetTransMatrix_c(r0)
