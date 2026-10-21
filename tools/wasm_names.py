#!/usr/bin/env python3
"""Resolve wasm function indices (as printed in browser stack traces:
`wasm-function[1686]`) to names, from the wasm 'name' section.

Needs a build with function names:
  make PLATFORM=web TOOLCHAIN=emcc WEB_NAMES=1 web

Usage: wasm_names.py web/dw.wasm 1686 779 2893 ..."""
import sys


def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7f) << s
        s += 7
        if not x & 0x80:
            return r, i


def main():
    b = open(sys.argv[1], 'rb').read()
    want = [int(a) for a in sys.argv[2:]]
    i = 8
    names = {}
    while i < len(b):
        sid = b[i]
        i += 1
        size, i = leb(b, i)
        end = i + size
        if sid == 0:
            n, j = leb(b, i)
            if b[j:j + n] == b'name':
                j += n
                while j < end:
                    sub = b[j]
                    j += 1
                    ssz, j = leb(b, j)
                    if sub == 1:
                        cnt, k = leb(b, j)
                        for _ in range(cnt):
                            idx, k = leb(b, k)
                            ln, k = leb(b, k)
                            names[idx] = b[k:k + ln].decode('utf-8', 'replace')
                            k += ln
                    j += ssz
        i = end
    if not names:
        print('no function names in this wasm (build with WEB_NAMES=1)')
        return
    for w in want:
        print(w, names.get(w, '?'))


main()
