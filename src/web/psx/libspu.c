/* libspu for the web build: PsyQ 4.1 semantics on top of the native
 * mixer (src/web/psx/mixer.c). Function-by-function port of
 * asm/us/main/psyq/libspu.s; glabel cited above each function.
 *
 * Where the asm stores to the `_spu_RXX` register block the port calls
 * the matching mixer setter instead. All the DMA/manual-transfer paths
 * (_spu_t/_spu_FiDMA/_spu_FwriteByIO/SpuWrite/SpuWritePartly) are
 * synchronous copies into the mixer's sample memory, so
 * SpuIsTransferCompleted always reports done.
 */

#include <libspu.h>

#include <string.h>

#include <web/host/log.h>

static void _spu_setReverbAttr(SpuReverbAttr *attr);
#include "mixer.h"

/* Hardware SPUCNT bits libspu tracks (the mixer has no register
 * file): 0x80 reverb master enable, 0x40 IRQ, 0x3F00 noise clock,
 * 0x1/0x2/0x4/0x8 CD/ext mix+reverb. */
static unsigned short _spu_cnt;
static unsigned long _spu_non, _spu_eon, _spu_pmon;
static short _spu_voll[24], _spu_volr[24];

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

/* _spu_rev_param (the 32 per-preset reverb parameters) lives in
 * mixer.c, which runs the reverb network. */

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
    mix_reset();
    for (i = 0; i < 24; i++) {
        _spu_voice_centerNote[i] = 0xC000;
        _spu_voll[i] = _spu_volr[i] = 0;
    }
    _spu_non = _spu_eon = _spu_pmon = 0;
    mix_master_volume(0, 0);
    _spu_cnt = 0xC000;
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
    mix_reverb_mode(0);
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
    mix_mem_write((unsigned long)_spu_tsa << 3, addr, size);
    _spu_inTransfer = 0;
    return size;
}

/* glabel SpuRead */
unsigned long SpuRead(unsigned char *addr, unsigned long size)
{
    mix_mem_read((unsigned long)_spu_tsa << 3, addr, size);
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
    /* the mixer raises no SPU IRQs; keep the enable bit only */
    if (on_off == SPU_ON) _spu_cnt |= 0x40;
    else if (on_off == SPU_OFF) _spu_cnt &= ~0x40;
    return on_off;
}
unsigned long SpuSetIRQAddr(unsigned long addr)
{
    return addr;
}

/* glabel SpuSetNoiseClock */
long SpuSetNoiseClock(long n_clock)
{
    if (n_clock < 0) n_clock = 0;
    if (n_clock >= 0x40) n_clock = 0x3F;
    _spu_cnt = (_spu_cnt & 0xC0FF) | ((n_clock & 0x3F) << 8);
    mix_noise_clock((int)n_clock);
    return n_clock;
}

/* glabel _SpuSetAnyVoice: on_off=1 sets bits, 0 clears, in a 24-voice
 * mask (the asm's NON/EON/PMON register pair). */
static void _SpuSetAnyVoice(long on_off, unsigned long voice_bit,
                            unsigned long *mask)
{
    if (on_off == 1) *mask |= voice_bit;
    else if (on_off == 0) *mask &= ~voice_bit;
}

/* glabel SpuSetNoiseVoice */
unsigned long SpuSetNoiseVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, &_spu_non);
    mix_noise_voices(_spu_non);
    return voice_bit;
}

/* glabel SpuSetReverbVoice */
unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, &_spu_eon);
    mix_reverb_voices(_spu_eon);
    return voice_bit;
}

/* glabel SpuSetPitchLFOVoice: the mixer has no pitch modulation (the
 * game never keys an LFO voice pair); the mask is kept as state */
unsigned long SpuSetPitchLFOVoice(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    _SpuSetAnyVoice(on_off, voice_bit, &_spu_pmon);
    return voice_bit;
}

/* glabel SpuSetReverb */
long SpuSetReverb(long on_off)
{
    switch (on_off) {
    case SPU_ON:
        if (_spu_rev_flag && _spu_rev_reserve_wa == 0) {
            _spu_cnt |= 0x80;
            mix_reverb_enable(1);
        }
        break;
    case SPU_OFF:
        _spu_cnt &= ~0x80;
        mix_reverb_enable(0);
        break;
    }
    return on_off;
}

/* glabel _spu_setReverbAttr: mask-driven parameter writes. The preset
 * parameter block goes to the mixer as a mode number; delay/feedback
 * only retune the ECHO/DELAY presets' tap registers and are kept as
 * attribute state. */
static void _spu_setReverbAttr(SpuReverbAttr *attr)
{
    long mask = attr->mask;
    if (mask < 1 || (mask & SPU_REV_MODE))
        mix_reverb_mode((int)attr->mode);
    if (mask < 1 || (mask & (SPU_REV_DEPTHL | SPU_REV_DEPTHR)))
        mix_reverb_depth(attr->depth.left, attr->depth.right);
}

/* glabel SpuSetReverbModeParam: the asm strips SPU_REV_MODE_CLEAR_WA
 * (0x100) from mode before range-checking it and clears the work area
 * afterwards when it was set. */
long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
    long mask, mode;
    int clear_wa = 0;
    SpuReverbAttr work;

    if (attr == 0) return -1;
    mask = attr->mask;
    mode = attr->mode;
    if (mode == SPU_REV_MODE_CHECK) return _spu_rev_attr.mode;
    if (mask < 1 || (mask & SPU_REV_MODE)) {
        if (mode & SPU_REV_MODE_CLEAR_WA) {
            mode &= ~SPU_REV_MODE_CLEAR_WA;
            clear_wa = 1;
        }
        if (mode < 0 || mode >= SPU_REV_MODE_MAX) return -1;
        if (_SpuIsInAllocateArea_(_spu_rev_startaddr[mode])) return -1;
        _spu_rev_attr.mode = mode;
        _spu_rev_offsetaddr = _spu_rev_startaddr[mode];
        _spu_rev_flag = 1;
    }
    if (mask < 1 || (mask & SPU_REV_DEPTHL))
        _spu_rev_attr.depth.left = attr->depth.left;
    if (mask < 1 || (mask & SPU_REV_DEPTHR))
        _spu_rev_attr.depth.right = attr->depth.right;
    if (mask < 1 || (mask & SPU_REV_DELAYTIME))
        _spu_rev_attr.delay = attr->delay;
    if (mask < 1 || (mask & SPU_REV_FEEDBACK))
        _spu_rev_attr.feedback = attr->feedback;

    work = _spu_rev_attr;
    work.mask = mask;
    _spu_setReverbAttr(&work);
    if (clear_wa)
        mix_reverb_clear();
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
    if (mask < 1 || (mask & SPU_REV_DEPTHL))
        _spu_rev_attr.depth.left = attr->depth.left;
    if (mask < 1 || (mask & SPU_REV_DEPTHR))
        _spu_rev_attr.depth.right = attr->depth.right;
    mix_reverb_depth(_spu_rev_attr.depth.left, _spu_rev_attr.depth.right);
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
    if ((unsigned long)mode >= 10) return -1;
    if (_SpuIsInAllocateArea_(_spu_rev_startaddr[mode])) return -1;
    /* the work area is the mixer's private delay line */
    mix_reverb_clear();
    return 0;
}

/* glabel SpuSetKey */
void SpuSetKey(long on_off, unsigned long voice_bit)
{
    voice_bit &= 0xFFFFFF;
    if (on_off == SPU_ON) {
        _spu_keystat |= voice_bit;
        mix_key_on(voice_bit);
    } else {
        mix_key_off(voice_bit);
        _spu_keystat &= ~voice_bit;
    }
}

/* glabel SpuGetKeyStatus */
long SpuGetKeyStatus(unsigned long voice_bit)
{
    int v = 0;
    unsigned long endx = mix_end_mask();
    for (; v < 24; v++) {
        if (voice_bit & (1 << v)) {
            int on = (_spu_keystat >> v) & 1;
            int end = (endx >> v) & 1;
            if (on) return end ? SPU_ON_ENV_OFF : SPU_ON;
            return end ? SPU_OFF_ENV_ON : SPU_OFF;
        }
    }
    return -1;
}

/* glabel SpuSetCommonAttr: mask-driven writes of the common attrs */
void SpuSetCommonAttr(SpuCommonAttr *attr)
{
    static short cdl, cdr;
    short mvl, mvr;
    unsigned long mask = attr->mask;
    if (mask == 0) return;
    mvl = mix_get_master_l();
    mvr = mix_get_master_r();
    if (mask < 1 || (mask & SPU_COMMON_MVOLL)) mvl = attr->mvol.left;
    if (mask < 1 || (mask & SPU_COMMON_MVOLR)) mvr = attr->mvol.right;
    mix_master_volume(mvl, mvr);
    if (mask < 1 || (mask & SPU_COMMON_CDVOLL)) cdl = attr->cd.volume.left;
    if (mask < 1 || (mask & SPU_COMMON_CDVOLR)) cdr = attr->cd.volume.right;
    if (mask < 1 || (mask & (SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR)))
        mix_cd_volume(cdl, cdr);
    /* there is no external input on web; the SPUCNT mix/reverb flag
     * bits are kept as state (CD audio is always mixed) */
    if (mask < 1 || (mask & SPU_COMMON_CDMIX))
        _spu_cnt = attr->cd.mix ? (_spu_cnt | 0x1) : (_spu_cnt & ~0x1);
    if (mask < 1 || (mask & SPU_COMMON_CDREV))
        _spu_cnt = attr->cd.reverb ? (_spu_cnt | 0x4) : (_spu_cnt & ~0x4);
    if (mask < 1 || (mask & SPU_COMMON_EXTMIX))
        _spu_cnt = attr->ext.mix ? (_spu_cnt | 0x2) : (_spu_cnt & ~0x2);
    if (mask < 1 || (mask & SPU_COMMON_EXTREV))
        _spu_cnt = attr->ext.reverb ? (_spu_cnt | 0x8) : (_spu_cnt & ~0x8);
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
            _spu_voll[ch] = arg->volume.left;
        if (mask < 1 || (mask & SPU_VOICE_VOLR))
            _spu_volr[ch] = arg->volume.right;
        if (mask < 1 || (mask & (SPU_VOICE_VOLL | SPU_VOICE_VOLR)))
            mix_voice_volume(ch, _spu_voll[ch], _spu_volr[ch]);
        if (mask < 1 || (mask & SPU_VOICE_PITCH))
            mix_voice_pitch(ch, arg->pitch);
        if (mask < 1 || (mask & SPU_VOICE_NOTE)) {
            unsigned short p = _spu_note2pitch(
                arg->note, 0, _spu_voice_centerNote[ch] >> 8,
                _spu_voice_centerNote[ch] & 0xFF);
            mix_voice_pitch(ch, p);
        }
        if (mask & SPU_VOICE_SAMPLE_NOTE)
            _spu_voice_centerNote[ch] = arg->sample_note;
        if (mask < 1 || (mask & SPU_VOICE_WDSA))
            mix_voice_sample(ch, arg->addr);
        /* SPU_VOICE_LSAX: the loop point comes from the sample's own
         * loop-start block flag (key-on resets the hardware repeat
         * address the same way), so an explicit loop_addr is moot */
        if (mask < 1 || (mask & SPU_VOICE_ADSR_ADSR1))
            mix_voice_adsr(ch, arg->adsr1, mix_voice_get_adsr2(ch));
        if (mask < 1 || (mask & SPU_VOICE_ADSR_ADSR2))
            mix_voice_adsr(ch, mix_voice_get_adsr1(ch), arg->adsr2);
        if (mask & (SPU_VOICE_ADSR_AMODE | SPU_VOICE_ADSR_AR |
                    SPU_VOICE_ADSR_DR | SPU_VOICE_ADSR_SL)) {
            unsigned short a1 = mix_voice_get_adsr1(ch);
            if (mask & SPU_VOICE_ADSR_AMODE) {
                a1 &= ~0x8000;
                if (arg->a_mode == SPU_VOICE_EXPIncN) a1 |= 0x8000;
            }
            if (mask & SPU_VOICE_ADSR_AR) { a1 = (a1 & 0x80FF) | ((arg->ar & 0x7F) << 8); }
            if (mask & SPU_VOICE_ADSR_DR) { a1 = (a1 & 0xFF0F) | ((arg->dr & 0xF) << 4); }
            if (mask & SPU_VOICE_ADSR_SL) { a1 = (a1 & 0xFFF0) | (arg->sl & 0xF); }
            mix_voice_adsr(ch, a1, mix_voice_get_adsr2(ch));
        }
        if (mask & (SPU_VOICE_ADSR_SMODE | SPU_VOICE_ADSR_SR |
                    SPU_VOICE_ADSR_RMODE | SPU_VOICE_ADSR_RR)) {
            unsigned short a2 = mix_voice_get_adsr2(ch);
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
            mix_voice_adsr(ch, mix_voice_get_adsr1(ch), a2);
        }
    }
}

/* glabel SpuGetVoiceEnvelope */
void SpuGetVoiceEnvelope(int vNum, short *envx)
{
    *envx = mix_voice_env(vNum);
}
