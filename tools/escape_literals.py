#!/usr/bin/env python3
"""Transcode UTF-8 string literals in a C source file to escaped CP932
octal bytes, matching what `gcc -finput-charset=UTF-8 -fexec-charset=CP932`
produces.  clang/emcc cannot do the exec-charset conversion itself, so the
web build compiles the transformed copies this script emits.

Usage: escape_literals.py <in.c> <out.c>
Comments and code are copied verbatim (non-ASCII bytes outside literals are
passed through unchanged); inside "..." literals every character that does
not survive ASCII is encoded as CP932 octal escapes.
"""
import re
import sys


def xform_string(body):
    """body is the text between the quotes (UTF-8 decoded source)."""
    out = ''
    i = 0
    while i < len(body):
        c = body[i]
        if c == '\\':
            out += body[i:i + 2]
            i += 2
            continue
        if ord(c) < 0x80:
            out += c
        else:
            try:
                b = c.encode('cp932')
            except UnicodeEncodeError:
                sys.exit('no Shift-JIS character for {!r}'.format(c))
            out += ''.join('\\{:03o}'.format(x) for x in b)
        i += 1
    return out


def xform(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        # line comment
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.index('\n', i)
            out.append(text[i:j])
            i = j
            continue
        # block comment
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.index('*/', i + 2) + 2
            out.append(text[i:j])
            i = j
            continue
        # string literal
        if c == '"':
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                elif text[j] == '"':
                    break
                else:
                    j += 1
            out.append('"' + xform_string(text[i + 1:j]) + '"')
            i = j + 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding='utf-8').read()
    with open(dst, 'w', encoding='utf-8', newline='') as f:
        f.write(xform(text))


if __name__ == '__main__':
    main()
