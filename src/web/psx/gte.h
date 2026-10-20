#ifndef WEB_PSX_GTE_H
#define WEB_PSX_GTE_H

#include <stdint.h>

/*
 * PS1 GTE (cop2) register file - canonical layout.
 *
 * Data registers d[0..31]:
 *   0  VXY0   (vx0 | vy0<<16)      16 SZ0
 *   1  VZ0                         17 SZ1
 *   2  VXY1                        18 SZ2
 *   3  VZ1                         19 SZ3
 *   4  VXY2                        20 RGB0
 *   5  VZ2                         21 RGB1
 *   6  RGB    (r | g<<8 | b<<16    22 RGB2
 *              | code<<24)        23 RES1 (reserved)
 *   7  OTZ                         24 MAC0
 *   8  IR0                         25 MAC1
 *   9  IR1                         26 MAC2
 *   10 IR2                         27 MAC3
 *   11 IR3                         28 IRGB (read: packed 5-bit rgb)
 *   12 SXY0                        29 ORGB
 *   13 SXY1                        30 LZCS
 *   14 SXY2                        31 LZCR
 *   15 SXYP   (write pushes SXY fifo)
 *
 * Control registers c[0..31]:
 *   0-4   RT11..RT33  rotation matrix (2 regs per row packed, 5 words)
 *   5-7   TRX, TRY, TRZ  translation
 *   8-12  L11..L33   light matrix
 *   13-15 RBK, GBK, BBK  background color
 *   16-20 LR1..LB33  light color matrix
 *   21-23 RFC, GFC, BFC  far color
 *   24    OFX    25 OFY   26 H   27 DQA   28 DQB   29 ZSF3   30 ZSF4   31 FLAG
 */
typedef struct GteRegs {
	uint32_t d[32];
	uint32_t c[32];
} GteRegs;

extern GteRegs gte_regs;

/*
 * C backing functions for the gte_* macros in include/emcc/inline_n.h.
 * For this segment they are stubs: loads record argument bytes into the
 * register file, stores read it back, math ops (rtps/rtpt) are no-ops.
 * A real GTE interpreter replaces them in a later segment.
 */
void gte_ldv0_c(const void *r0);
void gte_ldv3_c(const void *r0, const void *r1, const void *r2);
void gte_ldlzc_c(int32_t r0);
void gte_stlzc_c(void *r0);
void gte_rtps_c(void);
void gte_rtpt_c(void);
void gte_stsxy_c(void *r0);
void gte_stsxy3_c(void *r0, void *r1, void *r2);
void gte_stszotz_c(void *r0);
void gte_SetRotMatrix_c(const void *r0);
void gte_SetTransMatrix_c(const void *r0);

#endif
