#!/usr/bin/env python3
"""Give every 32-bit integer divide/remainder in a wasm module the MIPS
R3000 semantics the game was written for.

WebAssembly traps on x/0 and on INT_MIN/-1; a MIPS `div` never traps (and
neither did the PS1 compilers' output for this game -- e.g.
`angle += 512 / (i * 13 / 4)` in tickHealingParticles divides by zero at
i == 0 and carries on). R3000 results (psx-spx, "CPU Division"):

    signed   x / 0  -> x >= 0 ? -1 : 1     x % 0 -> x
    unsigned x / 0  -> 0xFFFFFFFF          x % 0 -> x
    INT_MIN / -1 -> INT_MIN                INT_MIN % -1 -> 0

The module is disassembled with Binaryen's wasm-dis, every
i32.{div,rem}_{s,u} is turned into a call to a helper implementing the
above, and it is re-assembled in place. Run it on the linked dw.wasm
(wired into mk/platform/web.mk).

Usage: wasm_safe_div.py module.wasm"""
import os
import re
import subprocess
import sys
import tempfile

BIN = '/emsdk/upstream/bin'

HELPERS = '''
 (func $psx_div_s (param $a i32) (param $b i32) (result i32)
  (if (i32.eqz (local.get $b))
   (then (return (select (i32.const 1) (i32.const -1)
                         (i32.lt_s (local.get $a) (i32.const 0))))))
  (if (i32.and (i32.eq (local.get $b) (i32.const -1))
               (i32.eq (local.get $a) (i32.const -2147483648)))
   (then (return (i32.const -2147483648))))
  (i32.div_s (local.get $a) (local.get $b)))
 (func $psx_div_u (param $a i32) (param $b i32) (result i32)
  (if (i32.eqz (local.get $b)) (then (return (i32.const -1))))
  (i32.div_u (local.get $a) (local.get $b)))
 (func $psx_rem_s (param $a i32) (param $b i32) (result i32)
  (if (i32.eqz (local.get $b)) (then (return (local.get $a))))
  (if (i32.eq (local.get $b) (i32.const -1)) (then (return (i32.const 0))))
  (i32.rem_s (local.get $a) (local.get $b)))
 (func $psx_rem_u (param $a i32) (param $b i32) (result i32)
  (if (i32.eqz (local.get $b)) (then (return (local.get $a))))
  (i32.rem_u (local.get $a) (local.get $b)))
'''


def tool(name):
    p = os.path.join(BIN, name)
    return p if os.path.exists(p) else name


def main():
    wasm = sys.argv[1]
    with tempfile.TemporaryDirectory() as d:
        wat = os.path.join(d, 'm.wat')
        subprocess.run([tool('wasm-dis'), '--all-features', wasm, '-o', wat],
                       check=True)
        text = open(wat, encoding='utf-8').read()
        counts = {}

        def sub(m):
            counts[m.group(1)] = counts.get(m.group(1), 0) + 1
            return '(call $psx_' + m.group(1)

        text = re.sub(r'\(i32\.((?:div|rem)_[su])(?=[\s)])', sub, text)
        # helpers go right after the module header line
        text = text.replace('(module', '(module' , 1)
        idx = text.rindex(')')
        text = text[:idx] + HELPERS + text[idx:]
        open(wat, 'w', encoding='utf-8').write(text)
        subprocess.run([tool('wasm-as'), '--all-features', wat, '-o', wasm,
                        '-g'], check=True)
        print('wasm_safe_div: rewrote', counts)


main()
