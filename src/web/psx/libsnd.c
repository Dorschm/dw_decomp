/* libsnd for the web build, ported from asm/us/main/psyq/libsnd.s.
 *
 * The asm's per-track sequencer record is 0xB0 (176) bytes; the offsets
 * used by the asm are reproduced in SeqTrack below with the same field
 * roles (+0x90 delta countdown in delta*10 units, +0x52 sub-counter /
 * -1 flag, +0x54 rate-or-period, +0x88 tick position, +0x98 flags,
 * +0x16 running status, +0x17 channel, +0x14 replay, +0x20/+0x21 eos
 * state, +0x22/+0x23 sep link, +0x24/+0x25 loop marks, +0x26 vab,
 * +0x27..+0x36 per-channel pan(+0x40 default), +0x37..+0x46 channel
 * map, +0x50 PPQN, +0x58/+0x5A seq vol, +0x60..+0x7F channel vols,
 * +0x80 mute bits, +0x84 delta, +0x8C tempo ticks, +0x94 tempo2,
 * +0xA8/+0xAC tempo fade).  SsFCALL = the event-handler table the asm
 * fills in SsSeqOpen.
 *
 * Voice bookkeeping uses _svm_voice[] (54-byte entries in the asm); the
 * field roles are mirrored in SvmVoice.
 */

#include <libsnd.h>
#include <libspu.h>

#include <string.h>

#include <emscripten/console.h>

#include <web/host/log.h>
#include "spu.h"

extern int _spu_setInTransfer(int);
extern int _spu_getInTransfer(void);

#define SPU_VREG(ch, o)  (0x1F801C00 + (ch) * 0x10 + (o))

static void svreg(int ofs, unsigned short v) { spu_write_reg16(0x1F801C00 + ofs, v); }

/* ---------------- module globals (libsnd.data.s equivalents) -------- */

#define MAX_VAB  16
#define MAX_PROG 128
#define MAX_SEQ  32
#define MAX_TRK  16

static VabHdr    *_svm_vab_vh[MAX_VAB];   /* glabel _svm_vab_vh */
static ProgAtr   *_svm_pg;                /* current vab prog table */
static VagAtr    *_svm_tn;                /* current vab tone table */
static VabHdr    *_svm_vh;                /* current vab header */
static ProgAtr   *_svm_vab_pg[MAX_VAB];   /* glabel _svm_vab_pg */
static VagAtr    *_svm_vab_tn[MAX_VAB];   /* glabel _svm_vab_tn */
static unsigned long _svm_vab_start[MAX_VAB];
static unsigned long _svm_vab_total[MAX_VAB];
static unsigned char _svm_vab_used[MAX_VAB];
static short _svm_vab_count;
static short kMaxPrograms = 0x40;
static unsigned long _snd_vmask;           /* voices the host may use */
static signed char _SsVmMaxVoice = 24;

typedef struct {
    short vabId;      /* +0x10 */
    short note;       /* +0x12 */
    short tone;       /* +0x14 */
    short prog;       /* +0x16 */
    short seq;        /* +0x1A (seq/track id for SsVoKeyOff) */
    unsigned short vag;      /* +0x00 */
    unsigned short pitch;    /* +0x02 */
    unsigned short flags;    /* +0x06 nonzero => in use */
    unsigned char used;      /* +0x1D */
    unsigned char pad;
    short voll, volr;
    unsigned char vabB;
    unsigned char chan;
    unsigned char pan;
    unsigned char pad2;
    short pbend;      /* pitch-bend state */
    short pbmin, pbmax;
    unsigned short adsr1, adsr2;
    unsigned long addr;
} SvmVoice;

static SvmVoice _svm_voice[24];
static unsigned long _spu_keystat_ext;

/* _svm_cur: current-tone context filled by VSetUp/SelectToneAndVag */
static struct {
    short voice;            /* +0x1A */
    short id;               /* +0x16 (seq/track packed id) */
    unsigned char note;     /* +0x02 */
    unsigned char fine;     /* +0x03 */
    unsigned char voll, volr;
    unsigned char vabId;
    short prog;
    unsigned char pan;
    unsigned char prior;
    VagAtr tone;
    unsigned short vag;
    unsigned long addr;
    unsigned short adsr1, adsr2;
    unsigned char center, shift;
    short pbmin, pbmax;
} _svm_cur;

/* ---------------- sequencer state ---------------- */

typedef struct {
    unsigned char *ptr;      /* +0x00 read pos */
    unsigned char *start;    /* +0x04 (also +0x08 loop start, +0x0C loop2) */
    unsigned char *loop_a;   /* +0x08 */
    unsigned char *loop_b;   /* +0x0C */
    unsigned char *end;      /* +0x10 sequence end (NULL in asm = 0) */
    unsigned char replay;    /* +0x14 */
    unsigned char u15;       /* +0x15 */
    unsigned char runstat;   /* +0x16 running status */
    unsigned char chan;      /* +0x17 status channel */
    unsigned char e18, e19, e1a, e1b, e1c, e1d, e1e, e1f; /* +0x18..+0x1F */
    unsigned char loop_flag; /* +0x20 */
    unsigned char loop_cnt;  /* +0x21 */
    signed char sep_a;       /* +0x22 */
    signed char sep_b;       /* +0x23 */
    unsigned char u24, u25;  /* +0x24/+0x25 */
    signed char vabId;       /* +0x26 */
    unsigned char pan[16];   /* +0x27..+0x36 (default 0x40 center) */
    unsigned char prog[16];  /* +0x37..+0x46 (program per channel;
                              * _SsSetProgramChange writes +0x37+chan) */
    unsigned short ppqn;     /* +0x50 */
    short counter;           /* +0x52 sub-counter, -1 = rate mode */
    unsigned short period;   /* +0x54 ticks-per-d10 or rate */
    unsigned short period0;  /* +0x56 */
    unsigned short vol_l;    /* +0x58 */
    unsigned short vol_r;    /* +0x5A */
    short cresc_vol;         /* fade target/state */
    unsigned short chvol[16];/* +0x60..+0x7F */
    unsigned short mute;     /* +0x80 */
    unsigned short u82;
    unsigned long delta;     /* +0x84 */
    unsigned long tickpos;   /* +0x88 */
    unsigned long tempo_ticks;  /* +0x8C (60M/us-quarter) */
    unsigned long rem;       /* +0x90 delta*10 countdown */
    unsigned long tempo2;    /* +0x94 */
    unsigned long flags;     /* +0x98 */
    long A8, AC;             /* +0xA8/+0xAC tempo fade */
    unsigned long pb[16];    /* pitch bend state */
    short pbmin[16], pbmax[16];
    unsigned short rpn[16];  /* last rpn */
} SeqTrack;

static SeqTrack *_ss_score_ptr[MAX_SEQ];  /* glabel _ss_score */
static long _snd_openflag;
static long _snd_ev_flag;
static short _snd_seq_t_max, _snd_seq_s_max;
static unsigned long VBLANK_MINUS = 60;
static long _snd_seq_tick_env;
static long seq_tick_count;
static int seq_ev_log;
static char *_snd_seq_table;              /* SS_SEQ_TABSIZ caller buf */

/* ---------------- helpers ---------------- */

/* glabel _SsVmVSetUp: select the vab tables; -1 if not open */
static short _SsVmVSetUp(short vabId, short prog)
{
    VabHdr *vh;
    if ((unsigned short)vabId >= MAX_VAB) return -1;
    if (_svm_vab_used[vabId] != 1) return -1;
    vh = _svm_vab_vh[vabId];
    if (vh == 0) return -1;
    if (prog >= (short)(vh->ps)) return -1;
    _svm_vh = vh;
    _svm_pg = _svm_vab_pg[vabId];
    _svm_tn = _svm_vab_tn[vabId];
    _svm_cur.vabId = (unsigned char)vabId;
    _svm_cur.prog = prog;
    _svm_cur.pan = _svm_pg[prog].mpan;
    _svm_cur.prior = _svm_pg[prog].prior;
    return 0;
}

/* glabel _SsVmSelectToneAndVag: tone index = pg.reserved1*16+tone;
 * fills _svm_cur.tone and vag id; returns tone count byte. */
static unsigned char _SsVmSelectToneAndVag(short prog, unsigned char note,
                                           unsigned char *sel,
                                           VagAtr *out)
{
    ProgAtr *pa = &_svm_pg[prog];
    VagAtr *va;
    unsigned char n = 0;
    int t;

    /* reserved1 holds this program's 16-tone row (set at open);
     * a tone is selected when min <= note <= max (asm iterates the
     * tone row and marks the +0x90 selection bitmap). */
    va = _svm_tn + (pa->reserved1 & 0xFFFF) * 16;
    for (t = 0; t < 16; t++) {
        if (va[t].min <= note && note <= va[t].max) {
            if (sel) sel[n] = (unsigned char)t;
            if (out && n == 0) *out = va[t];
            n++;
        }
    }
    return n;
}

/* vag i start addr was packed into ProgAtr[i/2].reserved2 u16[i&1] at
 * open (see SsVabOpenHeadWithMode). */
static unsigned long vag_addr_of(short vabId, short vag)
{
    unsigned short a =
        ((unsigned short *)&_svm_vab_pg[vabId][vag >> 1].reserved2)[vag & 1];
    return (unsigned long)a << 3;   /* stored absolute >>3 by openHead */
}

/* glabel note2pitch2 (libsnd): pitch for (note,fine) vs the tone's
 * center/shift, using the same 2^(x/1536) construction as libspu's
 * _spu_note2pitch. */
static unsigned short note2pitch2(unsigned char note, unsigned char fine)
{
    long d = (((long)_svm_cur.center << 7) + _svm_cur.shift)
           - (((long)note << 7) + fine);
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

/* glabel note2pitch (libsnd): public note->pitch for a given tone */
unsigned short note2pitch(unsigned short tone, unsigned short note,
                          unsigned char center, unsigned char shift)
{
    (void)tone; (void)note;
    _svm_cur.center = center;
    _svm_cur.shift = shift;
    return note2pitch2((unsigned char)(center), shift);
}

/* glabel SsPitchFromNote */
unsigned short SsPitchFromNote(short note, short fine,
                               unsigned char center, unsigned char s_shift)
{
    _svm_cur.center = center;
    _svm_cur.shift = s_shift;
    return note2pitch2((unsigned char)note, (unsigned char)fine);
}

/* glabel vmNoiseOn / vmNoiseOff */
static void vmNoiseOn(short voice)
{
    SpuSetNoiseVoice(SPU_ON, 1 << voice);
    SpuSetKey(SPU_ON, 1 << voice);
}

/* glabel _SsVmKeyOnNow: write regs + KON for _svm_cur.voice. */
static void _SsVmKeyOnNow(short on_off, unsigned short pitch)
{
    int v = _svm_cur.voice;
    (void)on_off;
    SvmVoice *sv = &_svm_voice[v];

    svreg(v * 0x10 + 0, (unsigned short)(sv->voll << 7));
    svreg(v * 0x10 + 2, (unsigned short)(sv->volr << 7));
    svreg(v * 0x10 + 4, pitch);
    svreg(v * 0x10 + 6, (unsigned short)(_svm_cur.addr >> 3));
    svreg(v * 0x10 + 8, _svm_cur.adsr1);
    svreg(v * 0x10 + 0xA, _svm_cur.adsr2);
    svreg(v * 0x10 + 0xE, (unsigned short)(_svm_cur.addr >> 3));
    SpuSetKey(SPU_ON, 1 << v);
}

/* glabel _SsVmKeyOffNow */
static void _SsVmKeyOffNow(short mode)
{
    int v = _svm_cur.voice;
    SpuSetKey(SPU_OFF, 1 << v);
    if (mode != 0) {
        _svm_voice[v].flags = 0;
        _svm_voice[v].used = 0;
        _svm_voice[v].vabId = 0xFF;
    }
}

/* glabel _SsVmAlloc: pick a voice. Prefers an unused voice; otherwise
 * the lowest-priority/oldest. Returns voice or -1. */
static int _SsVmAlloc(unsigned char prior)
{
    int worst = -1;
    int i;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_snd_vmask & (1 << i)) continue;
        if (!_svm_voice[i].used && _svm_voice[i].flags == 0)
            return i;
    }
    /* fallback: steal a used voice */
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_snd_vmask & (1 << i)) continue;
        if (_svm_voice[i].used) { worst = i; break; }
    }
    (void)prior;
    return worst;
}

/* glabel _SsVmDoAllocate — allocate + claim (called by KeyOn path) */
static short _SsVmDoAllocate(unsigned char prior)
{
    int v = _SsVmAlloc(prior);
    if (v >= 0) {
        _svm_cur.voice = (short)v;
        _svm_voice[v].used = 1;
    }
    return (short)v;
}

/* glabel _SsVmKeyOn (packed seq/track id, vabId, prog, note, vel —
 * the sequencer's entry point; keys every tone covering the note). */
static short _SsVmKeyOn(short packed, short vabId, short prog,
                        unsigned char note, unsigned char vel)
{
    VagAtr va;
    unsigned char sel[16];
    unsigned char n;
    ProgAtr *pa;
    short vab = (short)(vabId & 0xFF);
    int i, voice;
    unsigned long vag_addr;

    if (_SsVmVSetUp(vab, prog) != 0) return -1;
    pa = &_svm_pg[prog];
    _svm_cur.note = note;
    _svm_cur.prior = pa->prior;
    _svm_cur.pan = pa->mpan;
    _svm_cur.id = packed;

    n = _SsVmSelectToneAndVag(prog, note, sel, &va);
    if (n == 0) return -1;
    for (i = 0; i < n; i++) {
        va = _svm_tn[(pa->reserved1 & 0xFFFF) * 16 + sel[i]];
        _svm_cur.tone = va;
        _svm_cur.center = va.center;
        _svm_cur.shift = va.shift;
        _svm_cur.adsr1 = va.adsr1;
        _svm_cur.adsr2 = va.adsr2;
        _svm_cur.vag = (unsigned short)va.vag;
        vag_addr = vag_addr_of(vab, va.vag);
        _svm_cur.addr = vag_addr;
        if (vel == 0) continue;
        voice = _SsVmDoAllocate(_svm_cur.prior);
        if (voice < 0 || voice >= _SsVmMaxVoice) continue;
        {
            SvmVoice *sv = &_svm_voice[voice];
            short vl, vr;
            /* pan: 0x40 = center (asm uses the volume table) */
            short pv = va.pan - 0x40;
            if (_svm_cur.pan != 0x40) pv += (_svm_cur.pan - 0x40);
            if (pv > 0x3F) pv = 0x3F; if (pv < -0x40) pv = -0x40;
            vl = vr = vel;
            if (pv > 0) vr = (short)(vr * (0x40 - pv) >> 6);
            if (pv < 0) vl = (short)(vl * (0x40 + pv) >> 6);
            vl = (short)(vl * va.vol / 0x7F);
            vr = (short)(vr * va.vol / 0x7F);
            vl = (short)(vl * pa->mvol / 0x7F);
            vr = (short)(vr * pa->mvol / 0x7F);
            sv->vabId = vab;
            sv->note = note;
            sv->tone = (short)sel[i];
            sv->prog = prog;
            sv->seq = packed;
            sv->vag = _svm_cur.vag;
            sv->addr = vag_addr;
            sv->adsr1 = va.adsr1;
            sv->adsr2 = va.adsr2;
            sv->voll = vl;
            sv->volr = vr;
            sv->pbend = 0;
            sv->pbmin = (short)-(long)(_svm_cur.pbmin);
            sv->pbmax = _svm_cur.pbmax;
            sv->flags = 1;
            sv->pan = _svm_cur.pan;
            _svm_cur.voice = (short)voice;
            {
                unsigned short p = note2pitch2(note, 0);
                if (seq_ev_log < 40) {
                    seq_ev_log++;
                    emscripten_console_logf(
                        "seq: t=%ld noteon id=%04x v%d prog=%d tone=%d "
                        "note=%d vel=%d pitch=%04x addr=%x\n",
                        seq_tick_count, packed, voice, prog, sel[i],
                        note, vel, p, (unsigned)vag_addr);
                }
                _SsVmKeyOnNow(1, p);
            }
        }
    }
    return 0;
}

/* glabel _SsVmKeyOff */
static short _SsVmKeyOff(short packed, short vabId, short prog,
                         unsigned char note)
{
    int i;
    (void)vabId;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].used && _svm_voice[i].seq == packed &&
            _svm_voice[i].prog == prog && _svm_voice[i].note == note) {
            _svm_cur.voice = (short)i;
            _SsVmKeyOffNow(1);
        }
    }
    return 0;
}

/* glabel _SsVmSeqKeyOff */
static void _SsVmSeqKeyOff(short packed)
{
    int i;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].used && _svm_voice[i].seq == packed) {
            _svm_cur.voice = (short)i;
            _SsVmKeyOffNow(1);
        }
    }
}

/* glabel _SsVmFlush — nothing deferred on web */
static void _SsVmFlush(void) {}

/* glabel _SsVmInit */
static void _SsVmInit(void)
{
    int i;
    memset((unsigned char *)_svm_voice, 0, sizeof(_svm_voice));
    for (i = 0; i < 24; i++) _svm_voice[i].vabId = 0xFF;
    _snd_vmask = 0;
}

/* ---------------- SEQ event handlers ---------------- */

static SeqTrack *trk(short seq, short tr)
{
    if (_ss_score_ptr[seq] == 0) return 0;
    return (SeqTrack *)((char *)_ss_score_ptr[seq] + tr * 0xB0);
}

/* glabel _SsReadDeltaValue: variable-length delta; returns delta*10
 * and adds delta*10 to the track's +0x88 tick position (asm computes
 * v = raw*5 then *2 into both the return value and +0x88). */
static long read_delta(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    unsigned char b = *t->ptr++;
    long a0;
    if (b == 0) return 0;
    if (b & 0x80) {
        a0 = b & 0x7F;
        do {
            b = *t->ptr++;
            a0 = (a0 << 7) + (b & 0x7F);
        } while (b & 0x80);
    } else {
        a0 = b;
    }
    {
        long d10 = a0 * 10;
        t->tickpos += d10;
        return d10;
    }
}

/* glabel _SsSeqGetEof */
static short _SsSeqGetEof(short seq, short tr, unsigned char meta)
{
    SeqTrack *t = trk(seq, tr);
    (void)meta;
    if (t->loop_flag == 0) {
        /* first EOS: marks end but keeps looping if loop count set */
        t->loop_flag = 1;
        t->rem = 0;
        t->tickpos = 0;
        t->e1c = 0;
        if (!(t->flags & 0x400))
            t->ptr = t->start;
        else
            t->ptr = t->loop_b;
        return 0;
    }
    t->loop_cnt++;
    if ((signed char)t->loop_cnt < t->loop_flag) {
        /* endless? kept: replay count handling */
    }
    if (t->sep_a != -1) {
        /* chain to next sep track */
        extern void _SsSndNextSep(short, short);
        _SsSndNextSep(seq, tr);
    }
    _SsVmSeqKeyOff((short)((tr << 8) | (seq & 0xFF)));
    t->rem = t->period;      /* asm: sh +0x54 -> +0x90 */
    t->flags |= 0x204;       /* stopped + eof */
    t->flags &= ~0x8;
    return 1;
}

/* glabel _SsVmSetSeqVol */
static void _SsVmSetSeqVol(short packed, unsigned short l, unsigned short r,
                           short now)
{
    int i;
    (void)now;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].used && _svm_voice[i].seq == packed) {
            svreg(i * 0x10 + 0, (unsigned short)(l << 7));
            svreg(i * 0x10 + 2, (unsigned short)(r << 7));
            _svm_voice[i].voll = l;
            _svm_voice[i].volr = r;
        }
    }
}

/* event handlers — SsFCALL slots (glabels cited) */
void _SsNoteOn(short seq, short tr, unsigned char note,
                      unsigned char vel);        /* fwd */
void _SsSetProgramChange(short seq, short tr, unsigned char prog);
void _SsSetPitchBend(short seq, short tr);
void _SsGetMetaEvent(short seq, short tr, unsigned char type);
void _SsSetControlChange(short seq, short tr, unsigned char cc);
void _SsContBankChange(short, short);
void _SsContMainVol(short, short, unsigned char);
void _SsContPanpot(short, short, unsigned char);
void _SsContExpression(short, short, unsigned char);
void _SsContDamper(short, short, unsigned char);
void _SsContExternal(short, short, unsigned char);
void _SsContNrpn1(short, short, unsigned char);
void _SsContNrpn2(short, short, unsigned char);
void _SsContRpn1(short, short, unsigned char);
void _SsContRpn2(short, short, unsigned char);
void _SsContResetAll(short, short);
void _SsContDataEntry(short, short, unsigned char);

/* glabel _SsNoteOn */
void _SsNoteOn(short seq, short tr, unsigned char note,
                      unsigned char vel)
{
    SeqTrack *t = trk(seq, tr);
    int chan = t->chan;
    short packed = (short)((tr << 8) | (seq & 0xFF));
    short prog = t->prog[chan];

    if (vel == 0) {
        if (seq_ev_log < 40) {
            seq_ev_log++;
            emscripten_console_logf("seq: t=%ld noteoff id=%04x ch=%d "
                "note=%d\n", seq_tick_count, packed, chan, note);
        }
        _SsVmKeyOff(packed, t->vabId, prog, note);
    } else {
        if (t->mute & (1 << chan)) return;
        _SsVmKeyOn(packed, t->vabId, prog, note, vel);
    }
}

/* glabel _SsSetProgramChange */
void _SsSetProgramChange(short seq, short tr, unsigned char prog)
{
    SeqTrack *t = trk(seq, tr);
    t->prog[t->chan] = prog;
}

/* glabel _SsSetPitchBend */
void _SsSetPitchBend(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    int chan = t->chan;
    unsigned char lo = *t->ptr++;
    unsigned char hi = *t->ptr++;
    short bend = (short)(((hi << 7) | lo) - 0x2000);
    short packed = (short)((tr << 8) | (seq & 0xFF));
    int i;
    t->pb[chan] = bend;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].used && _svm_voice[i].seq == packed) {
            /* recompute pitch with bend (+-pbmin/pbmax semitones) */
            long semi = bend > 0 ? _svm_voice[i].pbmax : _svm_voice[i].pbmin;
            long cents = (bend * semi) >> 13; /* semitones*128 units */
            long nf = ((long)_svm_voice[i].note << 7) + cents;
            unsigned short p;
            _svm_cur.center = _svm_voice[i].vabB; /* placeholder */
            p = note2pitch2(_svm_voice[i].note, (unsigned char)(nf & 0x7F));
            svreg(i * 0x10 + 4, p);
        }
    }
}

/* glabel _SsGetMetaEvent: tempo meta etc. 3-byte tempo -> +0x8C, then
 * recompute +0x52/+0x54 exactly like _SsInitSoundSeq. */
void _SsGetMetaEvent(short seq, short tr, unsigned char type)
{
    SeqTrack *t = trk(seq, tr);
    if (type == 0x51) {
        unsigned long tempo = (t->ptr[0] << 16) | (t->ptr[1] << 8) | t->ptr[2];
        unsigned long tk, prod, a1;
        t->ptr += 3;
        tk = 60000000 / tempo;
        t->tempo_ticks = tk;
        t->tempo2 = tk;
        prod = t->ppqn * tk;
        a1 = VBLANK_MINUS * 60;
        if (prod * 10 < a1) {
            t->period = (unsigned short)((VBLANK_MINUS * 600) / prod);
            t->counter = (short)t->period;
        } else {
            unsigned long q = prod * 10 / a1;
            t->counter = -1;
            t->period = (unsigned short)
                (((prod * 10 % a1) * 2 > a1) ? q + 1 : q);
        }
    } else {
        /* other metas: skip 3-byte payload */
        t->ptr += 3;
    }
    t->rem = read_delta(seq, tr);
}

/* glabel _SsSeqGetEof is above. */
/* glabel _SsGetSeqData: parse one event, then read the following
 * delta into +0x90 (the asm reads the delta right after each event). */
static long _SsGetSeqData(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    unsigned char s;
    long ret = 0;

    s = *t->ptr++;
    if (t->ptr == t->end && t->end != 0) {
        _SsSeqGetEof(seq, tr, 0);
        return -1;
    }
    if (s & 0x80) {
        t->chan = s & 0xF;
        switch (s & 0xF0) {
        case 0x90: {
            unsigned char note = *t->ptr++;
            unsigned char vel = *t->ptr++;
            t->runstat = s;
            t->rem = read_delta(seq, tr);
            _SsNoteOn(seq, tr, note, vel);
            return ret;
        }
        case 0xB0:
            t->runstat = s;
            _SsSetControlChange(seq, tr, *t->ptr++);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xC0:
            t->runstat = s;
            _SsSetProgramChange(seq, tr, *t->ptr++);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xE0:
            t->runstat = s;
            _SsSetPitchBend(seq, tr);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xF0: {
            unsigned char meta = *t->ptr++;
            t->runstat = 0xFF;
            if (meta == 0x2F) {
                ret = 1;
                _SsSeqGetEof(seq, tr, meta);
                return ret;
            }
            _SsGetMetaEvent(seq, tr, meta);
            return ret;
        }
        default:
            t->rem = read_delta(seq, tr);
            return ret;
        }
    } else {
        /* running status */
        switch (t->runstat & 0xF0) {
        case 0x90: {
            unsigned char vel = *t->ptr++;
            t->rem = read_delta(seq, tr);
            _SsNoteOn(seq, tr, s /*note*/, vel);
            return ret;
        }
        case 0xB0:
            _SsSetControlChange(seq, tr, s);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xC0:
            _SsSetProgramChange(seq, tr, s);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xE0:
            t->ptr--;         /* pitch bend reads 2 bytes itself */
            _SsSetPitchBend(seq, tr);
            t->rem = read_delta(seq, tr);
            return ret;
        case 0xF0:
            if (s == 0x2F) { ret = 1; _SsSeqGetEof(seq, tr, s); return ret; }
            _SsGetMetaEvent(seq, tr, s);
            return ret;
        default:
            t->rem = read_delta(seq, tr);
            return ret;
        }
    }
}

/* glabel _SsSeqPlay: one 240Hz tick of a track (the +0x90/+0x52/+0x54
 * countdown exactly as the asm). */
static void _SsSeqPlay(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    long a2 = t->rem - (long)(short)t->period;

    if (a2 > 0) {
        if (t->counter > 0) {
            t->counter--;
        } else if (t->counter == 0) {
            t->counter = (short)t->period;
            t->rem -= 1;
        } else {
            t->rem = a2;
        }
    } else {
        long s0 = t->rem;
        /* asm: drain zero-delta events, accumulate until s0 >= period */
        for (;;) {
            do {
                if (_SsGetSeqData(seq, tr) != 0) {
                    /* EOF handled inside; rem set to period */
                    break;
                }
            } while (t->rem == 0);
            s0 += t->rem;
            if (s0 >= (short)t->period) break;
            if (t->flags & 0x200) break;
        }
        t->rem = s0 - (short)t->period;
    }
}

/* glabel _SsSndPlay wrapper */
static void _SsSndPlay(short seq, short tr) { _SsSeqPlay(seq, tr); }

/* glabel _SsSndNextSep */
void _SsSndNextSep(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    t->loop_flag = 1;
    t->loop_cnt = 0;
    t->flags &= ~0x101;
    t->flags &= ~0x8;
    t->flags &= ~0x2;
    t->flags &= ~0x4;
    t->flags &= ~0x200;
    t->ptr = t->start;
    t->replay = 1;
    t->flags |= 0x1;
}

/* glabel _SsSndPause */
static void _SsSndPause(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    (void)t;
}

/* glabel _SsSndCrescendo / _SsSndDecrescendo */
static void _SsSndCrescendo(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    if (t->cresc_vol < 0x7F) {
        t->cresc_vol++;
        _SsVmSetSeqVol((short)((tr << 8) | (seq & 0xFF)),
                       t->cresc_vol, t->cresc_vol, 1);
    } else t->flags &= ~0x10;
}
static void _SsSndDecrescendo(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    if (t->cresc_vol > 0) {
        t->cresc_vol--;
        _SsVmSetSeqVol((short)((tr << 8) | (seq & 0xFF)),
                       t->cresc_vol, t->cresc_vol, 1);
    } else t->flags &= ~0x20;
}

/* glabel _SsSndTempo: tempo fade — approximate per asm (kept minimal) */
static void _SsSndTempo(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    if (--t->A8 < 0) {
        t->flags &= ~0x40;
        t->flags &= ~0x80;
    }
}

/* glabel SsSeqCalledTbyT: one sequencer tick over all seqs/tracks */
void SsSeqCalledTbyT(void)
{
    short seq, tr;
    if (_snd_ev_flag == 1) return;
    _snd_ev_flag = 1;
    seq_tick_count++;
    _SsVmFlush();
    for (seq = 0; seq < _snd_seq_s_max; seq++) {
        if (!(_snd_openflag & (1 << seq))) continue;
        for (tr = 0; tr < _snd_seq_t_max; tr++) {
            SeqTrack *t = trk(seq, tr);
            if (!(t->flags & 0x1)) continue;
            if (t->flags & 0x1) _SsSndPlay(seq, tr);
            if (t->flags & 0x10) _SsSndCrescendo(seq, tr);
            if (t->flags & 0x20) _SsSndDecrescendo(seq, tr);
            if (t->flags & 0x40) _SsSndTempo(seq, tr);
            if (t->flags & 0x80) _SsSndTempo(seq, tr);
            if (t->flags & 0x2) _SsSndPause(seq, tr);
            if (t->flags & 0x8) _SsVmSeqKeyOff((short)((tr << 8) | (seq & 0xFF)));
        }
    }
    _snd_ev_flag = 0;
}

/* glabel _SsInitSoundSeq: fill a track record; returns -1 on bad hdr */
static short _SsInitSoundSeq(short seq, short vabId, unsigned char *addr)
{
    SeqTrack *t = (SeqTrack *)((char *)_snd_seq_table + 0); /* base set by open */
    int i;
    unsigned char *p = addr;
    unsigned long tempo, tk, prod, a1;

    t = _ss_score_ptr[seq];
    if (t == 0) return -1;
    memset((unsigned char *)t, 0, sizeof(SeqTrack));
    t->vabId = (signed char)vabId;
    t->ppqn = 0;
    for (i = 0; i < 16; i++) {
        t->prog[i] = (unsigned char)i;      /* +0x37 init: prog = chan */
        t->pan[i] = 0x40;
        t->chvol[i] = 0x7F;
    }
    t->counter = 1;
    t->ptr = p;

    if (p[0] == 0x53 || p[0] == 0x70) {          /* 'S' or 'p' */
        if (p[7] != 1) {
            emscripten_console_logf("SEQ: version %d\n", p[7]);
            return -1;
        }
        p += 8;
        t->ppqn = (unsigned short)((p[0] << 8) | p[1]);
        t->ptr = p + 2;
        tempo = (t->ptr[0] << 16) | (t->ptr[1] << 8) | t->ptr[2];
        t->ptr += 3;
        tk = 60000000 / tempo;
        t->tempo_ticks = tk;
        t->tempo2 = tk;
        prod = t->ppqn * tk;
        a1 = VBLANK_MINUS * 60;
        if (prod * 10 < a1) {
            t->period = (unsigned short)((VBLANK_MINUS * 600) / prod);
            t->counter = (short)t->period;
        } else {
            unsigned long q = prod * 10 / a1;
            t->counter = -1;
            t->period = (unsigned short)
                (((prod * 10 % a1) * 2 > a1) ? q + 1 : q);
        }
        t->period0 = t->period;
        t->u24 = *t->ptr++;   /* stored at +0x24/+0x25 in asm */
        t->u25 = *t->ptr++;
        t->rem = read_delta(seq, (short)0);
        t->delta = t->rem;
        t->start = t->ptr;
        t->loop_a = t->ptr;
        t->loop_b = t->ptr;
        t->end = 0;
        t->flags = 0;
        t->loop_flag = 1;
        t->sep_a = -1; t->sep_b = -1;
        return 0;
    }
    emscripten_console_logf("SEQ: bad magic %02x\n", p[0]);
    return 0;
}

/* glabel SsSeqOpen */
short SsSeqOpen(unsigned long *addr, short vabId)
{
    int i, seq = -1;
    if (_snd_openflag == -1) {
        emscripten_console_logf("libsnd: SsSeqOpen before SsInit\n");
        return -1;
    }
    for (i = 0; i < MAX_SEQ; i++)
        if (!(_snd_openflag & (1 << i))) { seq = i; break; }
    if (seq < 0) return -1;
    _snd_openflag |= 1 << seq;
    _ss_score_ptr[seq] =
        (SeqTrack *)(_snd_seq_table + seq * (long)_snd_seq_t_max * 0xB0);
    if (_SsInitSoundSeq((short)seq, vabId, (unsigned char *)addr) < 0) {
        _snd_openflag &= ~(1 << seq);
        return -1;
    }
    if (seq >= _snd_seq_s_max) _snd_seq_s_max = (short)(seq + 1);
    return (short)seq;
}

/* glabel SsSeqPlay */
void SsSeqPlay(short seq, char mode, short playcount)
{
    SeqTrack *t = trk(seq, 0);
    if (t == 0) return;
    t->flags = 0x1;                        /* play */
    if (playcount == SSPLAY_INFINITY)
        t->loop_flag = 0;                  /* asm: replay forever */
    else
        t->loop_flag = (unsigned char)playcount;
    t->loop_cnt = 0;
    t->replay = (mode == SSPLAY_PLAY) ? 1 : 0;
}

/* glabel SsSeqStop */
void SsSeqStop(short seq)
{
    short tr;
    if (!_snd_openflag) return;
    for (tr = 0; tr < _snd_seq_t_max; tr++) {
        SeqTrack *t = trk(seq, tr);
        t->flags = 0x8;                    /* flag: key off pending */
    }
    _SsVmSeqKeyOff((short)(seq & 0xFF));
}

/* glabel SsSeqSetVol */
void SsSeqSetVol(short seq, short voll, short volr)
{
    SeqTrack *t = trk(seq, 0);
    if (t == 0) return;
    if (t->flags == 1) {
        _SsVmSetSeqVol((short)(seq & 0xFF), (unsigned short)voll,
                       (unsigned short)volr, 1);
    }
    t->vol_l = (unsigned short)voll;
    t->vol_r = (unsigned short)volr;
}

/* glabel _SsClose / SsSeqClose */
void SsSeqClose(short seq)
{
    SsSeqStop(seq);
    _snd_openflag &= ~(1 << seq);
    _ss_score_ptr[seq] = 0;
}

/* glabel SsIsEos */
short SsIsEos(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    return (t->flags & 0x200) ? 1 : 0;
}

/* ---------------- control change handlers ---------------- */
/* glabel _SsSetControlChange: CC dispatch table (the asm jumps through
 * _SsFCALL slots after reading the value byte). */
void _SsSetControlChange(short seq, short tr, unsigned char cc)
{
    SeqTrack *t = trk(seq, tr);
    unsigned char val;
    if (cc == 0x00) { _SsContBankChange(seq, tr); return; }
    if (cc == 0x61) { _SsContResetAll(seq, tr); return; }
    val = *t->ptr++;
    switch (cc) {
    case 0x06: _SsContDataEntry(seq, tr, val); break;
    case 0x07: _SsContMainVol(seq, tr, val); break;
    case 0x0A: _SsContPanpot(seq, tr, val); break;
    case 0x0B: _SsContExpression(seq, tr, val); break;
    case 0x40: _SsContDamper(seq, tr, val); break;
    case 0x3F: _SsContExternal(seq, tr, val); break;
    case 0x62: _SsContNrpn2(seq, tr, val); break;
    case 0x63: _SsContNrpn1(seq, tr, val); break;
    case 0x64: _SsContRpn2(seq, tr, val); break;
    case 0x65: _SsContRpn1(seq, tr, val); break;
    default: break;
    }
}

/* glabel _SsContBankChange: selects the vab id for the channel */
void _SsContBankChange(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    t->vabId = (signed char)*t->ptr++;
}
/* glabel _SsContMainVol */
void _SsContMainVol(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->chvol[t->chan] = val;
}
/* glabel _SsContPanpot */
void _SsContPanpot(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->pan[t->chan] = val;
}
/* glabel _SsContExpression */
void _SsContExpression(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->chvol[t->chan] = val;
}
/* glabel _SsContDamper */
void _SsContDamper(short seq, short tr, unsigned char val)
{
    (void)seq; (void)tr; (void)val;
}
/* glabel _SsContExternal */
void _SsContExternal(short seq, short tr, unsigned char val)
{
    (void)seq; (void)tr; (void)val;
}
/* glabel _SsContNrpn1/2, _SsContRpn1/2 — NRPN/RPN select */
void _SsContNrpn1(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->rpn[t->chan] = (unsigned short)((val << 8) | 0x80);
}
void _SsContNrpn2(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->rpn[t->chan] = (unsigned short)((t->rpn[t->chan] & 0xFF00) | val | 0x80);
}
void _SsContRpn1(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->rpn[t->chan] = (unsigned short)(val << 8);
}
void _SsContRpn2(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    t->rpn[t->chan] = (unsigned short)((t->rpn[t->chan] & 0xFF00) | val);
}
/* glabel _SsContResetAll */
void _SsContResetAll(short seq, short tr)
{
    SeqTrack *t = trk(seq, tr);
    (void)*t->ptr++;
}
/* glabel _SsContDataEntry: data-entry on the selected RPN/NRPN —
 * pitch-bend range is what the game's sequences use */
void _SsContDataEntry(short seq, short tr, unsigned char val)
{
    SeqTrack *t = trk(seq, tr);
    int i;
    /* RPN 0 = pitch bend sensitivity */
    if ((t->rpn[t->chan] & 0x807F) == 0) {
        for (i = 0; i < _SsVmMaxVoice; i++)
            _svm_voice[i].pbmax = _svm_voice[i].pbmin = (short)val;
    }
}

/* ---------------- public API ---------------- */

/* glabel _SsInit */
void _SsInit(void)
{
    int i;
    memset((unsigned char *)_svm_vab_used, 0, sizeof(_svm_vab_used));
    memset((unsigned char *)_svm_vab_vh, 0, sizeof(_svm_vab_vh));
    memset((unsigned char *)_svm_vab_pg, 0, sizeof(_svm_vab_pg));
    memset((unsigned char *)_svm_vab_tn, 0, sizeof(_svm_vab_tn));
    memset((unsigned char *)_svm_vab_start, 0, sizeof(_svm_vab_start));
    memset((unsigned char *)_svm_vab_total, 0, sizeof(_svm_vab_total));
    memset((unsigned char *)_ss_score_ptr, 0, sizeof(_ss_score_ptr));
    _svm_vab_count = 0;
    _snd_seq_t_max = _snd_seq_s_max = 0;
    _snd_seq_table = 0;
    _SsVmInit();
    VBLANK_MINUS = 60;
    _snd_openflag = 0;
    _snd_ev_flag = 0;
    for (i = 0; i < 32; i++) { /* the asm zeroes 32 seq-slot words */ }
    _spu_keystat_ext = 0;
}

/* glabel SsInit */
void SsInit(void)
{
    SpuInit();
    _SsInit();
}

/* glabel SsSetTableSize */
void SsSetTableSize(char *table, short s_max, short t_max)
{
    _snd_seq_table = table;
    _snd_seq_s_max = s_max;
    _snd_seq_t_max = t_max;
}

/* glabel SsSetTickMode */
void SsSetTickMode(long mode)
{
    /* jtbl in SsSetTickMode: {60, 60, 240, 120, 50, 60} */
    static const unsigned long vb[6] = { 60, 60, 240, 120, 50, 60 };
    if (mode >= 0 && mode < 6) VBLANK_MINUS = vb[mode];
    _snd_seq_tick_env = mode;
}

/* glabel _SsStart: registers the vsync/RCnt tick source; on web the
 * audio pump calls SsSeqCalledTbyT at VBLANK_MINUS Hz. */
void _SsStart(void)
{
    SpuStart();
}

/* glabel SsStart */
void SsStart(void)
{
    _SsStart();
}

/* glabel SsEnd */
void SsEnd(void)
{
    /* stop the tick; all voices keyed off */
    _SsVmInit();
}

/* glabel SsQuit */
void SsQuit(void)
{
    SsEnd();
}

/* glabel SsSetMVol */
void SsSetMVol(short left, short right)
{
    svreg(0x180, (unsigned short)(left * 129));
    svreg(0x182, (unsigned short)(right * 129));
}

/* ---------------- VAB ---------------- */

/* glabel SsVabOpenHeadWithMode — see notes at top for field map. */
static short SsVabOpenHeadWithMode(unsigned char *vh_addr, short vabId,
                                   short sticky, unsigned long sb_addr)
{
    VabHdr *vh = (VabHdr *)vh_addr;
    short id;
    ProgAtr *pg;
    VagAtr *tn;
    unsigned short *sz;
    int i, nprog;
    unsigned long sizes[256], total = 0;
    unsigned long body_addr;

    if (_spu_getInTransfer() == 1) return -1;
    _spu_setInTransfer(1);

    if (vabId >= 0x10) { _spu_setInTransfer(0); return -1; }
    if (vabId == -1) {
        for (i = 0; i < 0x10; i++)
            if (!_svm_vab_used[i]) { _svm_vab_used[i] = 1; id = (short)i; break; }
        if (i >= 0x10) { _spu_setInTransfer(0); return -1; }
    } else {
        if (_svm_vab_used[vabId]) { _spu_setInTransfer(0); return -1; }
        _svm_vab_used[vabId] = 1;
        id = vabId;
    }
    _svm_vab_count++;

    _svm_vab_vh[id] = vh;
    /* 'VABp'/'VABh' check: asm compares (form>>8)=='VAB' and low byte */
    if ((vh->form >> 8) != 0x564142) {          /* 'VAB' */
        _svm_vab_used[id] = 0;
        _svm_vab_count--;
        _spu_setInTransfer(0);
        return -1;
    }
    /* asm: (form&0xFF)==0x70 && ver>=5 -> 0x80 progs, else 0x40 */
    kMaxPrograms = ((vh->form & 0xFF) == 0x70 && vh->ver >= 5) ? 0x80 : 0x40;

    if ((short)vh->ps > kMaxPrograms) {
        _svm_vab_used[id] = 0; _svm_vab_count--;
        _spu_setInTransfer(0);
        return -1;
    }
    pg = (ProgAtr *)(vh_addr + 0x20);
    _svm_vab_pg[id] = pg;

    /* asm: ProgAtr.reserved1 = compacted tone-row index, assigned to
     * every program with tones != 0 (running counter s0). */
    {
        int row = 0;
        for (i = 0; i < kMaxPrograms; i++) {
            if (pg[i].tones) pg[i].reserved1 = (unsigned long)row++;
        }
    }
    tn = (VagAtr *)((unsigned char *)pg + kMaxPrograms * 16);
    _svm_vab_tn[id] = tn;

    /* vag size table: after ps*16 VagAtr entries, u16 per vag;
     * asm iterates vs+1 entries (a1 <= vs), size = sz << (ver<5 ? 3:2) */
    sz = (unsigned short *)((unsigned char *)tn + vh->ps * 512);
    nprog = (vh->vs & 0xFF) + 1;
    for (i = 0; i < 0x100; i++) {
        unsigned long s;
        if (i <= (vh->vs & 0xFF)) {
            s = (vh->ver < 5) ? ((unsigned long)sz[i] << 2)
                              : ((unsigned long)sz[i] << 3);
        } else s = 0;
        sizes[i] = s;
        total += s;
    }

    if (sticky == 0) {
        body_addr = (unsigned long)SpuMalloc((long)total);
        if (body_addr == (unsigned long)-1) {
            _svm_vab_used[id] = 0; _svm_vab_count--;
            _spu_setInTransfer(0);
            return -1;
        }
    } else {
        body_addr = sb_addr;
    }
    if (body_addr + total > 0x80000) {
        _svm_vab_used[id] = 0; _svm_vab_count--;
        _spu_setInTransfer(0);
        return -1;
    }

    _svm_vab_start[id] = body_addr;
    /* asm: vag i start addr (8-byte units) -> ProgAtr[i/2].reserved2
     * u16[i&1] (sh at 0xC/0xE of pg + (i>>1)*16). */
    {
        unsigned long acc = 0;
        for (i = 0; i < nprog; i++) {
            unsigned short a = (unsigned short)((body_addr + acc) >> 3);
            acc += sizes[i];
            ((unsigned short *)&pg[i >> 1].reserved2)[i & 1] = a;
        }
    }
    _svm_vab_total[id] = total;
    _svm_vab_used[id] = 2;
    _spu_setInTransfer(0);
    return id;
}

/* glabel SsVabOpenHeadSticky */
short SsVabOpenHeadSticky(unsigned char *vh_addr, short vabId,
                          unsigned long sb_addr)
{
    return SsVabOpenHeadWithMode(vh_addr, vabId, 1, sb_addr);
}

/* glabel SsVabOpenHead */
short SsVabOpenHead(unsigned char *vh_addr, short vabId)
{
    return SsVabOpenHeadWithMode(vh_addr, vabId, 0, 0);
}

/* glabel SsVabTransBody */
short SsVabTransBody(unsigned char *vag_addr, short vabId)
{
    if ((unsigned short)vabId >= 0x11) { _spu_setInTransfer(0); return -1; }
    if (_svm_vab_used[vabId] != 2) { _spu_setInTransfer(0); return -1; }
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(_svm_vab_start[vabId]);
    SpuWrite(vag_addr, _svm_vab_total[vabId]);
    _svm_vab_used[vabId] = 1;
    return vabId;
}

/* glabel SsVabTransCompleted */
short SsVabTransCompleted(short ab_auto)
{
    return (short)SpuIsTransferCompleted(ab_auto);
}

/* glabel SsVabClose */
void SsVabClose(short vabId)
{
    if ((unsigned short)vabId >= 0x10) return;
    if (_svm_vab_used[vabId] != 1) return;
    SpuFree(_svm_vab_start[vabId]);
    _svm_vab_used[vabId] = 0;
    _svm_vab_count--;
}

/* glabel SsUtGetVBaddrInSB */
unsigned long SsUtGetVBaddrInSB(short vabId)
{
    if ((unsigned short)vabId >= 0x11) return (unsigned long)-1;
    if (_svm_vab_used[vabId] != 1) return (unsigned long)-1;
    return _svm_vab_start[vabId];
}

/* glabel SsUtKeyOnV */
short SsUtKeyOnV(short voice, short vabId, short prog, short tone,
                 short note, short fine, short voll, short volr)
{
    VagAtr va;
    ProgAtr *pa;
    unsigned long addr;

    if (_snd_ev_flag == 1) return -1;
    _snd_ev_flag = 1;
    if ((unsigned short)voice >= 0x18 ||
        _SsVmVSetUp(vabId, prog) != 0) {
        _snd_ev_flag = 0;
        return -1;
    }
    _svm_cur.note = (unsigned char)note;
    _svm_cur.id = 0x21;
    pa = &_svm_pg[prog];
    va = _svm_tn[(pa->reserved1 & 0xFFFF) * 16 + tone];
    {
        static int ut_log;
        if (ut_log < 20) {
            ut_log++;
            emscripten_console_logf(
                "ut: vab=%d prog=%d tone=%d row=%ld vag=%d parent=%d "
                "vol=%d pan=%d c=%d s=%d min=%d max=%d a=%x\n",
                vabId, prog, tone, (long)(pa->reserved1 & 0xFFFF),
                va.vag, va.prog, va.vol, va.pan, va.center, va.shift,
                va.min, va.max, (unsigned)vag_addr_of(vabId, va.vag));
        }
    }
    _svm_cur.tone = va;
    _svm_cur.center = va.center;
    _svm_cur.shift = va.shift;
    _svm_cur.adsr1 = va.adsr1;
    _svm_cur.adsr2 = va.adsr2;
    _svm_cur.vag = (unsigned short)va.vag;
    addr = vag_addr_of(vabId, va.vag);
    _svm_cur.addr = addr;
    _svm_cur.voice = voice;
    {
        SvmVoice *sv = &_svm_voice[voice];
        sv->vabId = vabId;
        sv->note = note;
        sv->tone = tone;
        sv->prog = prog;
        sv->seq = 0x21;
        sv->vag = _svm_cur.vag;
        sv->addr = addr;
        sv->adsr1 = va.adsr1;
        sv->adsr2 = va.adsr2;
        sv->voll = voll;
        sv->volr = volr;
        sv->used = 1;
        sv->flags = 1;
        sv->pan = _svm_cur.pan;
    }
    if (va.prog == 0xFF) {
        vmNoiseOn(voice);
    } else {
        _SsVmKeyOnNow(1, note2pitch2((unsigned char)note, (unsigned char)fine));
    }
    _snd_ev_flag = 0;
    return voice;
}

/* glabel SsUtKeyOffV */
short SsUtKeyOffV(short voice)
{
    if (_snd_ev_flag == 1) return -1;
    _snd_ev_flag = 1;
    if ((unsigned short)voice >= 0x18) { _snd_ev_flag = 0; return -1; }
    _svm_cur.voice = voice;
    _SsVmKeyOffNow(0);
    _svm_voice[voice].used = 0;
    _svm_voice[voice].flags = 0;
    _svm_voice[voice].vabId = 0xFF;
    _snd_ev_flag = 0;
    return 0;
}

/* glabel SsUtAllKeyOff */
void SsUtAllKeyOff(short mode)
{
    int i;
    SpuVoiceAttr attr;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_snd_vmask & (1 << i)) continue;
        memset((unsigned char *)&attr, 0, sizeof(attr));
        attr.voice = 1 << i;
        attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR |
                    SPU_VOICE_PITCH | SPU_VOICE_WDSA |
                    SPU_VOICE_ADSR_ADSR1 | SPU_VOICE_ADSR_ADSR2;
        attr.volume.left = attr.volume.right = 0;
        attr.pitch = 0x1000;
        attr.adsr1 = 0x80FF;
        attr.adsr2 = 0x4000;
        SpuSetVoiceAttr(&attr);
        _svm_voice[i].flags = 0x18;
        _svm_voice[i].vabId = 0xFF;
        _svm_voice[i].seq = 0;
        _svm_voice[i].used = 0;
        _svm_cur.voice = (short)i;
        _SsVmKeyOffNow(1);
    }
    (void)mode;
}

/* glabel SsUtAutoVol ( -> SeAutoVol via _autovol in asm; immediate
 * set is faithful enough for the game's fade-to-zero use). */
static short _autovol;
short SsUtAutoVol(short vabId_voice, short voll, short volr, short delta)
{
    (void)delta;
    if ((unsigned short)vabId_voice >= 0x18) return -1;
    _autovol = 1;
    svreg(vabId_voice * 0x10 + 0, (unsigned short)(voll << 7));
    svreg(vabId_voice * 0x10 + 2, (unsigned short)(volr << 7));
    _svm_voice[vabId_voice].voll = voll;
    _svm_voice[vabId_voice].volr = volr;
    return 0;
}

/* used by the host audio pump: ticks per second the sequencer wants */
long web_seq_tick_rate(void)
{
    return (long)VBLANK_MINUS;
}

/* glabel SsUtReverbOn/Off/SetReverbType/GetReverbType */
void SsUtReverbOn(void) {}
void SsUtReverbOff(void) {}
short SsUtSetReverbType(short type) { return type; }
short SsUtGetReverbType(void) { return 0; }

/* glabel SsVoKeyOn: vab_progtone packed (tone<<24|prog<<16|vab),
 * pitch in SPU pitch units, direct volumes. */
long SsVoKeyOn(long vab_progtone, long pitch,
               unsigned short volL, unsigned short volR)
{
    short vab = (short)(vab_progtone & 0xFFFF);
    short prog = (short)((vab_progtone >> 16) & 0xFF);
    short tone = (short)((vab_progtone >> 24) & 0xFF);
    VagAtr va;
    unsigned long addr;
    int voice;

    if (_SsVmVSetUp(vab, prog) != 0) return -1;
    va = _svm_tn[(_svm_pg[prog].reserved1 & 0xFFFF) * 16 + tone];
    addr = vag_addr_of(vab, va.vag);
    voice = _SsVmDoAllocate(_svm_cur.prior);
    if (voice < 0) return -1;
    _svm_cur.voice = (short)voice;
    _svm_cur.addr = addr;
    _svm_cur.adsr1 = va.adsr1;
    _svm_cur.adsr2 = va.adsr2;
    _svm_voice[voice].voll = volL;
    _svm_voice[voice].volr = volR;
    _svm_voice[voice].used = 1;
    _SsVmKeyOnNow(1, (unsigned short)pitch);
    return 0;
}
long SsVoKeyOff(long vab_progtone, long voice)
{
    (void)vab_progtone;
    _svm_cur.voice = (short)voice;
    _SsVmKeyOffNow(1);
    return 0;
}

/* glabel SsSeqPause / SsSeqReplay */
void SsSeqPause(short seq) { trk(seq, 0)->flags |= 0x2; }
void SsSeqReplay(short seq)
{
    SeqTrack *t = trk(seq, 0);
    t->ptr = t->start;
    t->flags = 1;
}
