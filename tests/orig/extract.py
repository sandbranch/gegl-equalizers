#!/usr/bin/env python3
# Takes the computing part out of an original plug-in's C file, for
# compiling it without GIMP 2 and GTK 2 (tests/orig-check.sh): drops the
# named top-level functions (the dialog, its callbacks, query and run),
# the MAIN () line, the PLUG_IN_INFO definition and the #include lines
# of libgimp and GTK, and puts #include "fake-gimp.h" first. Everything
# else stays as it is, in its order, byte for byte.
#
#   extract.py drop <in.c> <out.c> <function>...
#   extract.py keep <in.c> <out.c> <function>...   (only these functions,
#                                                   nothing else)
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import re
import sys


def chunks(src):
    """top-level pieces of C: (text, kind, name); kind is 'func' for a
    function definition, 'other' for everything else (declarations,
    preprocessor lines, comments, blank lines)"""
    out = []
    i, n = 0, len(src)
    start = 0
    depth = 0
    body_start = None
    paren = 0
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == '//':
            j = src.find('\n', i)
            i = n if j < 0 else j
            continue
        if two == '/*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            i = j + 1
            continue
        if c == '#' and depth == 0 and (i == 0 or src[i - 1] == '\n'):
            # a preprocessor line (with continuations) of its own
            j = i
            while True:
                k = src.find('\n', j)
                if k < 0:
                    k = n
                    break
                if src[k - 1] != '\\':
                    break
                j = k + 1
            if start < i:
                out.append((src[start:i], 'other', None))
            out.append((src[i:k + 1], 'other', None))
            i = start = k + 1
            continue
        if c == '(':
            paren += 1
        elif c == ')':
            paren -= 1
        elif c == '{':
            if depth == 0:
                header = src[start:i]
                code = re.sub(r'//[^\n]*|/\*.*?\*/', ' ', header, flags=re.S)
                is_func = ('=' not in code and
                           not re.search(r'\b(struct|union|enum|typedef)\b', code) and
                           re.search(r'\w+\s*\([^()]*(\([^()]*\)[^()]*)*\)\s*$', code))
                body_start = i if is_func else None
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0 and body_start is not None:
                header = src[start:body_start]
                code = re.sub(r'//[^\n]*|/\*.*?\*/', ' ', header, flags=re.S)
                name = re.search(r'(\w+)\s*\([^()]*(\([^()]*\)[^()]*)*\)\s*$', code).group(1)
                # the comments and blank lines before the function stay
                # with the text before it
                lead = len(header) - len(header.lstrip())
                out.append((src[start:start + lead], 'other', None))
                out.append((src[start + lead:i + 1], 'func', name))
                start = i + 1
                body_start = None
        elif c == ';' and depth == 0 and paren == 0:
            out.append((src[start:i + 1], 'other', None))
            start = i + 1
        i += 1
    out.append((src[start:], 'other', None))
    return out


def main():
    mode, path_in, path_out = sys.argv[1:4]
    names = set(sys.argv[4:])
    src = open(path_in, encoding='latin-1').read()
    # MAIN () is a macro call without a semicolon
    src = re.sub(r'(?m)^MAIN\(\)\s*$', '/* MAIN () dropped */', src)
    pieces = chunks(src)
    found = {name for _, kind, name in pieces if kind == 'func'}
    missing = names - found
    if missing:
        sys.exit('extract.py: not found in %s: %s' % (path_in, ', '.join(sorted(missing))))
    res = ['#include "fake-gimp.h"\n']
    for text, kind, name in pieces:
        if mode == 'keep':
            if kind == 'func' and name in names:
                res.append(text + '\n\n')
            continue
        if kind == 'func' and name in names:
            res.append('/* %s () dropped */' % name)
            continue
        if kind == 'other':
            if re.match(r'\s*#\s*include\s*<(libgimp|gtk)/', text):
                continue
            if 'PLUG_IN_INFO' in text:
                res.append('/* PLUG_IN_INFO dropped */')
                continue
        res.append(text)
    open(path_out, 'w', encoding='latin-1').write(''.join(res))


main()
