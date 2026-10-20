#ifndef WEB_PSX_GTE_H
#define WEB_PSX_GTE_H

#include <stdint.h>
/* NOTE: do not include <string.h> here - under the PsyQ header path it
 * resolves to the SDK header whose K&R memcpy/memset decls conflict with
 * game TUs that redeclare them with different signatures. */

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

/* Register access with the real read/write side effects. */
uint32_t gte_ldr(int idx);			/* read data reg */
void gte_str(int idx, uint32_t v);		/* write data reg */
uint32_t gte_lcr(int idx);			/* read control reg */
void gte_scr(int idx, uint32_t v);		/* write control reg */

/* Execute a cop2 command word (opcode + sf/lm/mx/v/cv bits, exactly as
 * they appear in the .s comments, e.g. rtpt = 0x4A280030 >> 24... pass
 * the 25-bit body without the cop2 tag: 0x02800030). */
void gte_cmd(uint32_t word);

/* Named opcode words (bit 25-31 tag stripped) as used by the game's asm. */
#define GTE_RTPS   0x00180001
#define GTE_RTPT   0x00280030
#define GTE_NCLIP  0x01400006
#define GTE_OP     0x0170000C		/* sf=1 lm=1? per site; callers pass raw */
#define GTE_DPCS   0x01400010
#define GTE_INTPL  0x01400011
#define GTE_NCDS   0x01480413
#define GTE_CDP    0x01480414
#define GTE_NCDT   0x01680416
#define GTE_NCCS   0x0148041B
#define GTE_CC     0x0148041C
#define GTE_NCS    0x0148041E
#define GTE_NCT    0x01680420
#define GTE_SQR    0x01480428
#define GTE_DCPL   0x01400029
#define GTE_DPCT   0x0140002A
#define GTE_AVSZ3  0x0158002D
#define GTE_AVSZ4  0x0168002E
#define GTE_GPF    0x0140003D
#define GTE_GPL    0x0140003E
#define GTE_NCCT   0x0168043F

/* Convenience helpers for the decompiled libgte/libgs code. */
static inline void gte_load_sxy(int idx, uint32_t v) { gte_str(12 + idx, v); }
static inline uint32_t gte_sxy(int idx) { return gte_ldr(12 + idx); }
static inline int16_t gte_sx(int idx) { return (int16_t)(gte_ldr(12 + idx) & 0xffff); }
static inline int16_t gte_sy(int idx) { return (int16_t)(gte_ldr(12 + idx) >> 16); }

/* load an SVECTOR (two s16 x,y then s16 z) into VXYn/VZn */
static inline void gte_ld_svector(int n, const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	uint32_t xy = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
		      ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	uint32_t z = (uint32_t)b[4] | ((uint32_t)b[5] << 8);
	gte_str(n * 2, xy);
	gte_str(n * 2 + 1, z);
}

/* C backing functions used by the gte_* macros in include/emcc/inline_n.h. */
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
