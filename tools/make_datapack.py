#!/usr/bin/env python3
"""
Turn a Digimon World disc image into the data pack the web build boots
from, so the page needs no .bin at all:

    python tools/make_datapack.py "../Digimon World.bin" web/data

The pack is a manifest plus gzip chunks of CHUNK sectors each. A sector is
stored as its bytes 16..2348 (subheader + 2324 data bytes, 2332 bytes);
sync, header and EDC are rebuilt by web/index.html. Form 1 sectors have
their EDC/ECC zeroed first, which the engine never reads, so they
compress well. All-zero chunks are not written.

The output is the player's own disc data: it is gitignored and must not
be published.
"""

import gzip
import json
import os
import sys

RAW = 2352
REC = 2332  # bytes 16..2348 of each raw sector
CHUNK = 256


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    nsec = os.path.getsize(src) // RAW
    os.makedirs(out, exist_ok=True)
    zero = bytes(REC * CHUNK)
    empty = []
    total = 0
    with open(src, "rb") as f:
        for c in range((nsec + CHUNK - 1) // CHUNK):
            raw = f.read(RAW * CHUNK)
            n = len(raw) // RAW
            buf = bytearray(REC * n)
            for i in range(n):
                sec = raw[i * RAW:(i + 1) * RAW]
                rec = bytearray(sec[16:2348])
                if not sec[18] & 0x20:  # Form 1: drop EDC/ECC (2072..2348)
                    rec[2056:] = bytes(REC - 2056)
                buf[i * REC:(i + 1) * REC] = rec
            if bytes(buf) == zero[:len(buf)]:
                empty.append(c)
                continue
            data = gzip.compress(bytes(buf), 9, mtime=0)
            with open(os.path.join(out, "%04d.gz" % c), "wb") as g:
                g.write(data)
            total += len(data)
            if c % 64 == 0:
                print("chunk %d/%d  %.1f MB" % (c, nsec // CHUNK, total / 1e6))
    with open(os.path.join(out, "manifest.json"), "w") as g:
        json.dump({"version": 1, "sectors": nsec, "chunk": CHUNK,
                   "record": REC, "empty": empty}, g)
    print("%d sectors, %.1f MB, %d empty chunks -> %s"
          % (nsec, total / 1e6, len(empty), out))


if __name__ == "__main__":
    main()
