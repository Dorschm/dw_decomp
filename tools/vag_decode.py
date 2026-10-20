#!/usr/bin/env python3
"""VAG extractor + reference decoder (nocash psx-spx ADPCM spec).

Usage: vag_decode.py <file.VHB> <vag_index> <out_prefix>
Writes <out_prefix>.vag (raw bytes) and <out_prefix>.h (C arrays:
vag_test_bytes[], vag_test_pcm[]) for src/web/test/spu_test.c.
"""
import struct, sys

FILT = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]

def decode_vag(data):
    """Decode whole VAG. Returns list of s16 samples. Stops at
    End without Repeat; loops once on Repeat then continues
    (looping vags are truncated at first wrap for testing)."""
    out = []
    s1 = s2 = 0
    loop_start = None
    wrapped = 0
    i = 0
    while i + 16 <= len(data):
        shift = data[i] & 0x0F
        filt = (data[i] >> 4) & 7
        flags = data[i + 1]
        f0, f1 = FILT[filt if filt < 5 else 4]
        for j in range(14):
            d = data[i + 2 + j]
            for k in range(2):
                s = (d >> 4) if k else (d & 0x0F)
                if s >= 8:
                    s -= 16
                v = (s << 12) >> shift
                v += (f0 * s1 + f1 * s2 + 32) >> 6
                v = max(-32768, min(32767, v))
                s2, s1 = s1, v
                out.append(v)
        if flags & 4:
            loop_start = i
        if flags & 1:
            if flags & 2 and loop_start is not None and not wrapped:
                i = loop_start
                wrapped = 1
                continue
            break
        i += 16
    return out

def parse_vhb(path, vag):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[0:4])[0]      # VhbFileHeader.vhOffset
    vbo = struct.unpack('<I', d[4:8])[0]      # VhbFileHeader.vbOffset
    vh = off                                # VabHdr offset
    assert d[vh:vh+4] == b'pBAV', d[vh:vh+4]
    ver = struct.unpack('<I', d[vh+4:vh+8])[0]
    ps, ts, vs = struct.unpack('<HHH', d[vh+18:vh+24])
    kmax = 0x80 if ((d[vh] & 0xFF) == 0x70 and ver >= 5) else 0x40
    tn = vh + 0x20 + kmax * 16
    sz = tn + ps * 512
    sizes = []
    for i in range(0x100):
        if i <= (vs & 0xFF):
            v = struct.unpack('<H', d[sz + i*2:sz + i*2 + 2])[0]
            sizes.append(v << 3 if ver >= 5 else v << 2)
        else:
            sizes.append(0)
    vb = vbo
    body = d[vb:]
    start = sum(sizes[:vag])
    return body[start:start + sizes[vag]], ver, start, vb

if __name__ == '__main__':
    path, vag, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    data, ver, voff, vboff = parse_vhb(path, vag)
    open(out + '.vag', 'wb').write(data)
    pcm = decode_vag(data)
    n = min(len(pcm), 60000)
    with open(out + '.h', 'w') as f:
        f.write('static const unsigned char vag_test_bytes[] = {\n')
        for i in range(0, len(data), 16):
            f.write(','.join(str(b) for b in data[i:i+16]) + ',\n')
        f.write('};\nstatic const short vag_test_pcm[%d] = {\n' % n)
        for i in range(0, n, 16):
            f.write(','.join(str(s) for s in pcm[i:i+16]) + ',\n')
        f.write('};\n')
    print('ver=%d vb@0x%x vag off=0x%x len=%d pcm=%d first blk %s'
          % (ver, vboff, voff, len(data), len(pcm), data[:8].hex()))
