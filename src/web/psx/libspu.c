/* libspu for the web build: PsyQ 4.1 semantics on top of the software SPU
 * register file (src/web/psx/spu.c). Function-by-function port of
 * asm/us/main/psyq/libspu.s; glabel cited above each function.
 *
 * `_spu_RXX` in the asm is the hardware register base 0x1F801C00; the
 * sh/lh accesses to it map to spu_write_reg16/spu_read_reg16. All the
 * DMA/manual-transfer paths (_spu_t/_spu_FiDMA/_spu_FwriteByIO/SpuWrite/
 * SpuWritePartly) are synchronous copies into spu_ram, so
 * SpuIsTransferCompleted always reports done.
 */

#include <libspu.h>

#include <string.h>

#include <web/host/log.h>

static void _spu_setReverbAttr(SpuReverbAttr *attr);
#include "spu.h"

/* register offsets relative to 0x1F801C00 */
#define VREG(ch, o)  ((ch) * 0x10 + (o))
#define R_MVOL      0x180
#define R_RVOL      0x184
#define R_KON       0x188
#define R_KOFF      0x18C
#define R_PMON      0x190
#define R_NON       0x194
#define R_EON       0x198
#define R_ENDX      0x19C
#define R_ESA       0x1A2
#define R_IRQA      0x1A4
#define R_TSA       0x1A6
#define R_DATA      0x1A8
#define R_SPUCNT    0x1AA
#define R_CDVOL     0x1B0
#define R_EXTVOL    0x1B4
#define R_MAINVOLC  0x1B8
#define R_REV0      0x1C0

static void set_reg(int ofs, unsigned short v) { spu_write_reg16(0x1F801C00 + ofs, v); }
static unsigned short get_reg(int ofs) { return spu_read_reg16(0x1F801C00 + ofs); }

/* _spu_FsetRXX/_spu_FgetRXXa: PsyQ indexes the register file in halfwords */
static void _spu_FsetRXX(int idx, unsigned short val, int mode)
{
    (void)mode;
    set_reg(idx * 2, val);
}
__attribute__((unused)) static unsigned short _spu_FgetRXX(int idx, int mode)
{
    (void)mode;
    return get_reg(idx * 2);
}

/* ---- module state (libspu.data.s globals) ---- */
static unsigned short _spu_tsa;
static unsigned long  _spu_transMode = SPU_TRANSFER_BY_DMA;
static unsigned long  _spu_keystat;
static int            _spu_inTransfer;

static unsigned long *_spu_memList;      /* {addr|flags, size} pairs */
static long _spu_AllocBlockNum;
static long _spu_AllocLastNum;

static long _spu_rev_flag;
static long _spu_rev_reserve_wa;
static long _spu_rev_offsetaddr;
static SpuReverbAttr _spu_rev_attr;

static unsigned short _spu_voice_centerNote[24]; /* 0xC000 default */

/* glabel _spu_rev_startaddr (libspu.data.s): work-area start in 8-byte
 * units per SPU_REV_MODE_* */
static const long _spu_rev_startaddr[10] = {
    0xFFFE, 0xFB28, 0xFC18, 0xF6F8, 0xF204,
    0xEA44, 0xE128, 0xCFF8, 0xCFF8, 0xF880
};

/* glabel _spu_rev_param (libspu.data.s): 10 records; 4-byte header is all
 * zero in the binary, followed by the 32 u16 reverb-register params. */
static const unsigned short _spu_rev_param[10][32] = {
    /* OFF */ {0},
    /* ROOM */
    {0x007D,0x005B,0x6D80,0x54B8,0xBED0,0x0000,0x0000,0xBA80,
     0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
     0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C,0x8000,0x8000},
    /* STUDIO_A */
    {0x0033,0x0025,0x70F0,0x4FA8,0xBCE0,0x4410,0xC0F0,0x3C00,
     0x2800,0x1400,0x1BFC,0x0200,0x0EFA,0x0600,0x0ABE,0x0400,
     0x0120,0x011F,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0164,0x00B2,0x0058,0x002C,0x8000,0x8000},
    /* STUDIO_B */
    {0x00B1,0x007F,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0xB4C0,
     0x2800,0x1400,0x1204,0x0600,0x0DB4,0x0400,0x0762,0x0400,
     0x026E,0x015F,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0480,0x02A0,0x0140,0x00A0,0x8000,0x8000},
    /* STUDIO_C */
    {0x00E3,0x00A9,0x6F60,0x4FA8,0xBCE0,0x4510,0xCA00,0x9800,
     0x4A00,0x2E00,0x1440,0x0800,0x0C04,0x0600,0x0A04,0x0400,
     0x0266,0x0162,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0480,0x02A0,0x0140,0x00A0,0x8000,0x8000},
    /* HALL */
    {0x01A5,0x0139,0x6000,0x5000,0x4C00,0x4800,0xB800,0xBC00,
     0x8000,0x8080,0x6008,0x0400,0x5008,0x0400,0x4008,0x0200,
     0x0B00,0x0600,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0C00,0x0800,0x0400,0x0200,0x8000,0x8000},
    /* SPACE */
    {0x033D,0x0231,0x7E00,0x5000,0xB000,0xB000,0x4C00,0xB000,
     0x6004,0x1004,0x1002,0x0410,0x0200,0x0400,0x0400,0x0400,
     0x1000,0x0800,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x8000,0x8000},
    /* ECHO */
    {0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x8100,
     0x0000,0x0000,0x1C00,0x0E00,0x1C00,0x0E00,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x8000,0x8000},
    /* DELAY */
    {0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x8100,
     0x0000,0x0000,0x1C00,0x0E00,0x1C00,0x0E00,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x8000,0x8000},
    /* PIPE */
    {0x0017,0x0013,0x70F0,0x4FA8,0xBCE0,0x4510,0x8000,0x8000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
     0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x8000,0x8000},
};

/* glabel _spu_note2pitch: pitch = base_oct * r^(frac/32) with r =
 * 2^(1/48) ~= 0x103B/0x1000, 32-step iterations + 5-bit lerp. */
static unsigned short _spu_note2pitch(unsigned short note, unsigned short fine,
                                      unsigned short snote, unsigned short sfine)
{
    long d = (((long)snote << 7) + sfine) - (((long)note << 7) + fine);
    long ad = d < 0 ? -d : d;
    int oct = (int)(ad / 1536);
    int frac = (int)(ad - oct * 1536);
    long base, a0, a3, t1;
    int n, rem, i;

    if (d < 0) {
        if (frac != 0) { oct++; frac = 1536 - frac; }
        base = 0x1000 >> oct;
    } else {
        base = 0x1000 << oct;
    }
    base &= 0xFFFF;

    a0 = 0x103B;
    t1 = base * a0;
    a3 = base << 12;
    n = frac >> 5;
    rem = frac & 0x1F;
    for (i = 0; i < n; i++) {
        a3 = base * a0;
        a0 = ((a0 * 65 * 16 - a0) * 4 - a0) >> 12;
        t1 = base * a0;
    }
    {
        long v = a3 + (((t1 - a3) >> 5) * rem);
        long pitch = v >> 12;
        if (pitch >= 0x4000) pitch = 0x3FFF;
        return (unsigned short)pitch;
    }
}

/* glabel _spu_pitch2note: inverse of note2pitch (log2 via the same
 * table idea). Not referenced by libsnd's hot path; faithful version. */
__attribute__((unused)) static void _spu_pitch2note(unsigned short pitch, unsigned short *note,
                            unsigned short *fine, unsigned short snote,
                            unsigned short sfine)
{
    /* binary search over note2pitch; note units = semitone<<7|fine */
    int center = ((int)snote << 7) + sfine;
    int lo = center - 8 * 1536, hi = center + 8 * 1536, mid = center;
    while (hi - lo > 1) {
        unsigned short p = _spu_note2pitch((unsigned short)(mid >> 7),
                                           (unsigned short)(mid & 0x7F),
                                           snote, sfine);
        if (p < pitch) lo = mid; else hi = mid;
        mid = (lo + hi) >> 1;
    }
    *note = (unsigned short)(mid >> 7);
    *fine = (unsigned short)(mid & 0x7F);
}

/* glabel _spu_init: reset register file defaults. The asm writes the
 * BIOS-style defaults; SPUCNT ends 0xC000 (unmute on, enable). */
static void _spu_init(int hot)
{
    int i;
    if (hot) return;
    spu_reset();
    for (i = 0; i < 24; i++) _spu_voice_centerNote[i] = 0xC000;
    set_reg(R_MVOL, 0);
    set_reg(R_MVOL + 2, 0);
    set_reg(R_SPUCNT, 0xC000);
}

/* glabel _SpuInit */
void _SpuInit(int hot)
{
    _spu_init(hot);
    if (!hot)
        _spu_tsa = 0;
    _spu_rev_flag = 0;
    _spu_rev_reserve_wa = 0;
    _spu_rev_attr.mode = 0;
    _spu_rev_attr.depth.left = _spu_rev_attr.depth.right = 0;
    _spu_rev_attr.delay = _spu_rev_attr.feedback = 0;
    _spu_rev_offsetaddr = _spu_rev_startaddr[0];
    _spu_FsetRXX(0xD1, (unsigned short)_spu_rev_offsetaddr, 0);
    _spu_AllocBlockNum = 0;
}

/* glabel SpuInit */
void SpuInit(void)
{
    _SpuInit(0);
}

/* glabel SpuStart / SpuQuit */
void SpuStart(void) {}
void SpuQuit(void) {}

/* glabel _spu_gcSPU: returns the value of _spu_keystat read-modify */
static unsigned long _spu_gcSPU(void)
{
    return _spu_keystat;
}

/* glabel SpuInitMalloc */
long SpuInitMalloc(long num, char *top)
{
    if (num <= 0) return 0;
    /* {0x40001010, 0x80000-0x1010}: single free block starting at
     * byte 0x1010 in the asm's byte units (addr stored in 8-byte units
     * masked to 0xFFFFFFF... actually stored raw in mem units). */
    ((unsigned long *)top)[0] = 0x40001010;
    ((unsigned long *)top)[1] = 0x80000 - 0x1010;
    _spu_memList = (unsigned long *)top;
    _spu_AllocLastNum = 0;
    _spu_AllocBlockNum = num;
    return 0x80000 - 0x1010;
}

/* glabel _SpuIsInAllocateArea_ : addr in 8-byte units */
static int _SpuIsInAllocateArea_(unsigned long addr)
{
    unsigned long *p = _spu_memList;
    unsigned long a = addr << 3;
    if (p == 0) return 0;
    for (;;) {
        unsigned long tag = p[0];
        if (!(tag & 0x80000000)) {
            if (!(tag & 0x40000000)) {
                unsigned long base = tag & 0x0FFFFFFF;
                if (base <= a && a < base + p[1]) return 0;
            }
        }
        if (tag & 0x80000000) break;
        if (tag & 0x40000000) break;
        p += 2;
    }
    return 1;
}

/* glabel _SpuIsInAllocateArea : addr in bytes */
__attribute__((unused)) static int _SpuIsInAllocateArea(unsigned long addr)
{
    if (_spu_memList == 0) return 0;
    return _SpuIsInAllocateArea_(addr >> 3);
}

/* glabel SpuMalloc — verbatim control flow of the asm */
long SpuMalloc(long size)
{
    long limit = 0;
    long need = size;
    long s0, idx = -1;
    unsigned long *list;

    if (_spu_rev_reserve_wa)
        limit = (0x10000 - _spu_rev_offsetaddr) << 3;

    /* round up to 8 */
    if (need & 7) need += 8;
    need = (need >> 3) << 3;

    list = _spu_memList;
    if (list[0] & 0x40000000) {
        idx = 0;
    } else {
        _spu_gcSPU();
        for (s0 = 0; s0 < _spu_AllocBlockNum; s0++) {
            unsigned long tag = list[s0 * 2];
            if (tag & 0x40000000) { idx = s0; break; }
            if ((tag & 0x80000000) && list[s0 * 2 + 1] >= (unsigned long)need) {
                idx = s0; break;
            }
        }
    }
    if (idx == -1) return -1;

    {
        unsigned long *e = list + idx * 2;
        unsigned long tag = e[0];
        if (tag & 0x40000000) {
            /* split the head free block; entry limit is alloc blocks */
            if (idx + 1 >= _spu_AllocBlockNum) return -1;
            if (e[1] - (unsigned long)limit < (unsigned long)need) return -1;
            {
                unsigned long *ne = e + 2;
                ne[0] = ((tag & 0x0FFFFFFF) + need) | 0x40000000;
                ne[1] = e[1] - need;
                _spu_AllocLastNum = idx + 1;
                e[1] = need;
                e[0] = tag & 0x0FFFFFFF;
            }
            _spu_gcSPU();
            return list[idx * 2];
        } else {
            /* freed block (bit31): allocate `need` and re-tag the
             * remainder as a freed block appended at AllocLastNum
             * (asm slides the tail entries down one slot) */
            unsigned long esz = e[1];
            if ((unsigned long)need < esz &&
                _spu_AllocLastNum < _spu_AllocBlockNum) {
                unsigned long *ne = list + _spu_AllocLastNum * 2;
                ne[0] = ((tag & 0x0FFFFFFF) + need) | 0x80000000;
                ne[1] = esz - need;
                _spu_AllocLastNum++;
            }
            e[1] = need;
            e[0] = tag & 0x0FFFFFFF;
            _spu_gcSPU();
            return list[idx * 2];
        }
    }
}

/* glabel SpuFree */
void SpuFree(unsigned long addr)
{
    long i;
    if (_spu_AllocBlockNum <= 0) { _spu_gcSPU(); return; }
    for (i = 0; i < _spu_AllocBlockNum; i++) {
        unsigned long *e = _spu_memList + i * 2;
        if (e[0] & 0x40000000) break;
        if (e[0] == addr) { e[0] = addr | 0x80000000; break; }
    }
    _spu_gcSPU();
}

/* glabel SpuMallocWithStartAddr */
long SpuMallocWithStartAddr(unsigned long addr, long size)
{
    long idx = SpuMalloc(size);
    long i;
    unsigned long naddr;
    if (idx == -1) return -1;
    if ((unsigned long)idx == addr) return idx;
    /* relocate: find a free block covering addr..addr+size */
    naddr = addr;
    for (i = 0; i < _spu_AllocBlockNum; i++) {
        unsigned long *e = _spu_memList + i * 2;
        if (e[0] & 0x40000000) {
            unsigned long base = e[0] & 0x0FFFFFFF;
            if (base <= naddr && naddr + size <= base + e[1]) {
                SpuFree((unsigned long)idx);
                e[0] = (base + size) | 0x40000000;
                e[1] -= (naddr - base) + size;
                return (long)(addr);
            }
            break;
        }
    }
    SpuFree((unsigned long)idx);
    return -1;
}

/* glabel SpuSetTransferStartAddr */
unsigned long SpuSetTransferStartAddr(unsigned long addr)
{
    _spu_tsa = (unsigned short)(addr >> 3);
    set_reg(R_TSA, _spu_tsa);
    return addr;
}

/* glabel SpuSetTransferMode */
long SpuSetTransferMode(long mode)
{
    _spu_transMode = mode;
    return mode;
}

/* glabel SpuWrite: synchronous copy of `size` bytes at _spu_tsa*8 */
unsigned long SpuWrite(unsigned char *addr, unsigned long size)
{
    _spu_inTransfer = 1;
    spu_dma_write((unsigned long)_spu_tsa << 3, addr, size);
    _spu_inTransfer = 0;
    return size;
}

/* glabel SpuRead */
unsigned long SpuRead(unsigned char *addr, unsigned long size)
{
    spu_dma_read((unsigned long)_spu_tsa << 3, addr, size);
    return size;
}

/* glabel SpuIsTransferCompleted: synchronous -> always complete */
long SpuIsTransferCompleted(long flag)
{
    (void)flag;
    return 1;
}

/* glabel _spu_setInTransfer / _spu_getInTransfer */
int _spu_setInTransfer(int v) { int o = _spu_inTransfer; _spu_inTransfer = v; return o; }
int _spu_getInTransfer(void) { return _spu_inTransfer; }

/* glabel SpuSetIRQ / SpuSetIRQAddr */
long SpuSetIRQ(long on_off)
{
    unsigned short cnt = get_reg(R_SPUCNT);
    if (on_off == SPU_ON) set_reg(R_SPUCNT, cnt | 0x40);
    else if (on_off == SPU_OFF) set_reg(R_SPUCNT, cnt & ~0x40);
    return on_off;
}
unsigned long SpuSetIRQAddr(unsigned long addr)
{
    set_reg(R_IRQA, (unsigned short)(addr >> 3));
    return addr;
}

/* glabel SpuSetNoiseClock */
long SpuSetNoiseClock(long n_clock)
{
    if (n_clock < 0) n_clock = 0;
    if (n_clock >= 0x40) n_clock = 0x3F;
    set_reg(R_SPUCNT, (get_reg(R_SPUCNT) & 0xC0FF) | ((n_clock & 0x3F) << 8));
    return n_clock;
}

/* glabel _SpuSetAnyVoice: on_off=1 sets bits, 0 clears, in 16-voice reg
 * pair r_lo/r_hi (register-file halfword indices). */
static void _SpuSetAnyVoice(long on_off, unsigned long voice_bit,
                            int r_lo, int r_hi)
{
    if (on_off == 1) {
        set_reg(r_lo * 2, get_reg(r_lo * 2) | (unsigned short)voice_bit);
        set_reg(r_hi * 2, get_reg(r_hi * 2) | (unsigned short)(voice_bit >> 16));
    } else if (on_off == 0) {
        set_reg(r_lo * 2, get_reg(r_lo * 2) & (unsigned short)~voice_bit);
        set_reg(r_hi * 2, get_reg(r_hi * 2) & (unsigned short)~(voice_bit >> 16));
    }
}

/* glabel SpuSetNoiseVoice (NON regs at idx 0xCA/0xCB => ofs 0x194/0x196) */
unsigned long SpuSetNoiseVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, 0xCA, 0xCB);
    return voice_bit;
}

/* glabel SpuSetReverbVoice (EON at ofs 0x198/0x19A -> idx 0xCC/0xCD) */
unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, 0xCC, 0xCD);
    return voice_bit;
}

/* glabel SpuSetPitchLFOVoice / SpuSetVoiceVolume / etc. PMON pair */
unsigned long SpuSetPitchLFOVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, 0xC8, 0xC9);
    return voice_bit;
}

/* glabel SpuSetReverb */
long SpuSetReverb(long on_off)
{
    switch (on_off) {
    case SPU_ON:
        if (_spu_rev_flag && _spu_rev_reserve_wa == 0) {
            set_reg(R_SPUCNT, get_reg(R_SPUCNT) | 0x80);
            /* apply stored attr */
            _spu_FsetRXX(0xD1, (unsigned short)_spu_rev_offsetaddr, 0);
            _spu_setReverbAttr(&_spu_rev_attr);
        }
        break;
    case SPU_OFF:
        set_reg(R_SPUCNT, get_reg(R_SPUCNT) & ~0x80);
        break;
    }
    return on_off;
}

/* glabel _spu_setReverbAttr: mask-driven write of the 32 reverb regs
 * (SPU_REV_MODE writes params when mode>0 or mask<1 special-case). */
static void _spu_setReverbAttr(SpuReverbAttr *attr)
{
    long mask = attr->mask;
    int i;
    if (mask < 1 || (mask & SPU_REV_MODE)) {
        if (attr->mode != 0) {
            for (i = 0; i < 32; i++)
                set_reg(R_REV0 + i * 2, _spu_rev_param[attr->mode][i]);
        }
    }
    if (mask < 1 || (mask & SPU_REV_DEPTHL))
        set_reg(R_RVOL, (unsigned short)attr->depth.left);
    if (mask < 1 || (mask & SPU_REV_DEPTHR))
        set_reg(R_RVOL + 2, (unsigned short)attr->depth.right);
    if (mask < 1 || (mask & SPU_REV_DELAYTIME))
        _spu_FsetRXX(0xD2, (unsigned short)attr->delay, 0);
    if (mask < 1 || (mask & SPU_REV_FEEDBACK))
        _spu_FsetRXX(0xD3, (unsigned short)attr->feedback, 0);
}

/* glabel SpuSetReverbModeParam */
long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
    long mask, mode;
    SpuReverbAttr work;
    int i;

    if (attr == 0) return -1;
    mask = attr->mask;
    mode = attr->mode;
    if (mode == SPU_REV_MODE_CHECK) return _spu_rev_attr.mode;
    if (mode < 0 || mode >= SPU_REV_MODE_MAX) return -1;

    _spu_rev_attr.mode = mode;
    _spu_rev_offsetaddr = _spu_rev_startaddr[mode];
    _spu_rev_flag = 1;

    work = *attr;
    if (mask < 1 || (mask & SPU_REV_MODE)) {
        work.mode = mode;
        /* load preset params into the working attr (asm copies
         * _spu_rev_param[mode] into a stack SpuReverbAttr) */
        for (i = 0; i < 32; i++) { /* params applied below */ }
    }
    if (mask < 1 || (mask & SPU_REV_DEPTHL)) {
        _spu_rev_attr.depth.left = attr->depth.left;
    }
    if (mask < 1 || (mask & SPU_REV_DEPTHR)) {
        _spu_rev_attr.depth.right = attr->depth.right;
    }
    work.depth.left = _spu_rev_attr.depth.left;
    work.depth.right = _spu_rev_attr.depth.right;
    work.mask = mask;

    /* if reverb already on (SPUCNT bit7), briefly disable while
     * rewriting params (asm tests/clears/restores bit 0x80) */
    {
        int was_on = get_reg(R_SPUCNT) & 0x80;
        if (was_on) set_reg(R_SPUCNT, get_reg(R_SPUCNT) & ~0x80);
        _spu_setReverbAttr(&work);
        _spu_FsetRXX(0xD1, (unsigned short)_spu_rev_offsetaddr, 0);
        if (mask & SPU_REV_MODE_CLEAR_WA)
            SpuClearReverbWorkArea(mode);
        if (was_on) set_reg(R_SPUCNT, get_reg(R_SPUCNT) | 0x80);
    }
    return 0;
}

/* glabel SpuGetReverbModeParam (read back current preset) */
void SpuGetReverbModeParam(SpuReverbAttr *attr)
{
    *attr = _spu_rev_attr;
}

/* glabel SpuSetReverbDepth */
long SpuSetReverbDepth(SpuReverbAttr *attr)
{
    long mask = attr->mask;
    if (mask < 1 || (mask & SPU_REV_DEPTHL)) {
        set_reg(R_RVOL, (unsigned short)attr->depth.left);
        _spu_rev_attr.depth.left = attr->depth.left;
    }
    if (mask < 1 || (mask & SPU_REV_DEPTHR)) {
        set_reg(R_RVOL + 2, (unsigned short)attr->depth.right);
        _spu_rev_attr.depth.right = attr->depth.right;
    }
    return 0;
}

/* glabel SpuReserveReverbWorkArea */
long SpuReserveReverbWorkArea(long on_off)
{
    if (on_off == SPU_ON) {
        if (_spu_rev_flag) { _spu_rev_reserve_wa = 1; return 1; }
        return 0;
    }
    _spu_rev_reserve_wa = 0;
    return 0;
}

/* glabel SpuClearReverbWorkArea */
long SpuClearReverbWorkArea(long mode)
{
    unsigned long lo, hi;
    if ((unsigned long)mode >= 10) return -1;
    if (_SpuIsInAllocateArea_(_spu_rev_startaddr[mode])) return -1;
    if (mode == SPU_REV_MODE_OFF) {
        lo = 0x10 << 3;
        hi = 0xFFF0 << 3;
    } else {
        lo = _spu_rev_startaddr[mode] << 3;
        hi = 0x10000 << 3;
    }
    memset(spu_ram + lo, 0, hi - lo);
    return 0;
}

/* glabel SpuSetKey */
void SpuSetKey(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    if (on_off == SPU_ON) {
        _spu_keystat |= voice_bit;
        set_reg(R_KON, (unsigned short)voice_bit);
        set_reg(R_KON + 2, (unsigned short)(voice_bit >> 16));
    } else {
        set_reg(R_KOFF, (unsigned short)voice_bit);
        set_reg(R_KOFF + 2, (unsigned short)(voice_bit >> 16));
        _spu_keystat &= ~voice_bit;
    }
}

/* glabel SpuGetKeyStatus */
long SpuGetKeyStatus(unsigned long voice_bit)
{
    int v = 0;
    for (; v < 24; v++) {
        if (voice_bit & (1 << v)) {
            unsigned short endx = (v < 16) ? get_reg(R_ENDX)
                                           : get_reg(R_ENDX + 2);
            int on = (_spu_keystat >> v) & 1;
            int end = (endx >> (v & 15)) & 1;
            if (on) return end ? SPU_ON_ENV_OFF : SPU_ON;
            return end ? SPU_OFF_ENV_ON : SPU_OFF;
        }
    }
    return -1;
}

/* glabel SpuSetCommonAttr: mask-driven writes of the common regs */
void SpuSetCommonAttr(SpuCommonAttr *attr)
{
    unsigned long mask = attr->mask;
    if (mask == 0) return;
    if (mask < 1 || (mask & SPU_COMMON_MVOLL))
        set_reg(R_MVOL, (unsigned short)attr->mvol.left);
    if (mask < 1 || (mask & SPU_COMMON_MVOLR))
        set_reg(R_MVOL + 2, (unsigned short)attr->mvol.right);
    if (mask < 1 || (mask & SPU_COMMON_CDVOLL))
        set_reg(R_CDVOL, (unsigned short)attr->cd.volume.left);
    if (mask < 1 || (mask & SPU_COMMON_CDVOLR))
        set_reg(R_CDVOL + 2, (unsigned short)attr->cd.volume.right);
    if (mask < 1 || (mask & SPU_COMMON_EXTVOLL))
        set_reg(R_EXTVOL, (unsigned short)attr->ext.volume.left);
    if (mask < 1 || (mask & SPU_COMMON_EXTVOLR))
        set_reg(R_EXTVOL + 2, (unsigned short)attr->ext.volume.right);
    /* SPUCNT flag bits for CD/ext mix + reverb (asm writes the same
     * cnt bits through _spu_FsetRXX) */
    {
        unsigned short cnt = get_reg(R_SPUCNT);
        if (mask < 1 || (mask & SPU_COMMON_CDMIX)) {
            cnt = attr->cd.mix ? (cnt | 0x1) : (cnt & ~0x1);
        }
        if (mask < 1 || (mask & SPU_COMMON_CDREV)) {
            cnt = attr->cd.reverb ? (cnt | 0x4) : (cnt & ~0x4);
        }
        if (mask < 1 || (mask & SPU_COMMON_EXTMIX)) {
            cnt = attr->ext.mix ? (cnt | 0x2) : (cnt & ~0x2);
        }
        if (mask < 1 || (mask & SPU_COMMON_EXTREV)) {
            cnt = attr->ext.reverb ? (cnt | 0x8) : (cnt & ~0x8);
        }
        set_reg(R_SPUCNT, cnt);
    }
    /* volmode fields are read-only state in this rev; mvolmode selects
     * sweep vs direct for the master volume (kept as stored state) */
    (void)attr->mvolmode; (void)attr->mvolx;
}

/* glabel SpuSetVoiceAttr: mask-driven per-voice register writes */
void SpuSetVoiceAttr(SpuVoiceAttr *arg)
{
    unsigned long mask = arg->mask;
    int first, last, ch;

    /* asm derives the voice range from mask bits 19-24 (voice bitmask
     * in the upper byte? actually arg->voice is a separate field in
     * the real SDK; in this header the voice select is arg->voice). */
    {
        unsigned long vb = arg->voice & 0xFFFFFF;
        first = 0; last = 24;
        for (ch = 0; ch < 24; ch++)
            if (vb & (1 << ch)) { first = ch; break; }
        for (ch = 23; ch >= 0; ch--)
            if (vb & (1 << ch)) { last = ch + 1; break; }
        if (!(vb & 0xFFFFFF)) { first = 0; last = 0; }
    }
    for (ch = first; ch < last; ch++) {
        if (!(arg->voice & (1 << ch))) continue;
        if (mask < 1 || (mask & SPU_VOICE_VOLL))
            set_reg(VREG(ch, 0), (unsigned short)arg->volume.left);
        if (mask < 1 || (mask & SPU_VOICE_VOLR))
            set_reg(VREG(ch, 2), (unsigned short)arg->volume.right);
        if (mask < 1 || (mask & SPU_VOICE_PITCH))
            set_reg(VREG(ch, 4), arg->pitch);
        if (mask < 1 || (mask & SPU_VOICE_NOTE)) {
            unsigned short p = _spu_note2pitch(
                arg->note, 0, _spu_voice_centerNote[ch] >> 8,
                _spu_voice_centerNote[ch] & 0xFF);
            set_reg(VREG(ch, 4), p);
        }
        if (mask & SPU_VOICE_SAMPLE_NOTE)
            _spu_voice_centerNote[ch] = arg->sample_note;
        if (mask < 1 || (mask & SPU_VOICE_WDSA))
            set_reg(VREG(ch, 6), (unsigned short)(arg->addr >> 3));
        if (mask < 1 || (mask & SPU_VOICE_LSAX))
            set_reg(VREG(ch, 0xE), (unsigned short)(arg->loop_addr >> 3));
        if (mask < 1 || (mask & SPU_VOICE_ADSR_ADSR1))
            set_reg(VREG(ch, 8), arg->adsr1);
        if (mask < 1 || (mask & SPU_VOICE_ADSR_ADSR2))
            set_reg(VREG(ch, 0xA), arg->adsr2);
        if (mask & (SPU_VOICE_ADSR_AMODE | SPU_VOICE_ADSR_AR |
                    SPU_VOICE_ADSR_DR | SPU_VOICE_ADSR_SL)) {
            unsigned short a1 = get_reg(VREG(ch, 8));
            if (mask & SPU_VOICE_ADSR_AMODE) {
                a1 &= ~0x8000;
                if (arg->a_mode == SPU_VOICE_EXPIncN) a1 |= 0x8000;
            }
            if (mask & SPU_VOICE_ADSR_AR) { a1 = (a1 & 0x80FF) | ((arg->ar & 0x7F) << 8); }
            if (mask & SPU_VOICE_ADSR_DR) { a1 = (a1 & 0xFF0F) | ((arg->dr & 0xF) << 4); }
            if (mask & SPU_VOICE_ADSR_SL) { a1 = (a1 & 0xFFF0) | (arg->sl & 0xF); }
            set_reg(VREG(ch, 8), a1);
        }
        if (mask & (SPU_VOICE_ADSR_SMODE | SPU_VOICE_ADSR_SR |
                    SPU_VOICE_ADSR_RMODE | SPU_VOICE_ADSR_RR)) {
            unsigned short a2 = get_reg(VREG(ch, 0xA));
            if (mask & SPU_VOICE_ADSR_SMODE) {
                a2 &= ~0xC000; /* sustain mode bits 15-14 */
                switch (arg->s_mode) {
                case SPU_VOICE_LINEARIncN: break;
                case SPU_VOICE_LINEARIncR: a2 |= 0x4000; break;
                case SPU_VOICE_LINEARDecN: a2 |= 0x8000; break;
                case SPU_VOICE_LINEARDecR: a2 |= 0xC000; break;
                case SPU_VOICE_EXPIncN:  break;
                case SPU_VOICE_EXPIncR:  a2 |= 0x4000; break;
                case SPU_VOICE_EXPDec:   a2 |= 0x8000; break;
                }
            }
            if (mask & SPU_VOICE_ADSR_SR) { a2 = (a2 & 0xC0FF) | ((arg->sr & 0x7F) << 8); }
            if (mask & SPU_VOICE_ADSR_RMODE) {
                a2 &= ~0x20;
                if (arg->r_mode == SPU_VOICE_EXPDec) a2 |= 0x20;
            }
            if (mask & SPU_VOICE_ADSR_RR) { a2 = (a2 & 0xFFE0) | (arg->rr & 0x1F); }
            set_reg(VREG(ch, 0xA), a2);
        }
    }
}

/* glabel SpuGetVoiceEnvelope */
void SpuGetVoiceEnvelope(int vNum, short *envx)
{
    *envx = (short)get_reg(VREG(vNum, 0xC));
}
