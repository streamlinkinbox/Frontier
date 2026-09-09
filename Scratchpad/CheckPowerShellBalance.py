#!/usr/bin/env python3
"""Structural sanity for the Windows build scripts: brace/paren/quote balance.

There is no PowerShell in the Linux sandbox, so this is the closest thing to a parse check. It will not catch
semantic errors, but it does catch the class of damage a bad edit causes -- an unclosed @( ... ) or a stray
quote -- which otherwise only surfaces on a Windows machine.
"""
import sys

def check(path):
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    par = cur = 0
    issues = []
    inblock = False
    for n, line in enumerate(lines, 1):
        if '<#' in line: inblock = True
        if inblock:
            if '#>' in line: inblock = False
            continue
        out, q = [], None
        for ch in line:
            if q:
                out.append(ch)
                if ch == q: q = None
                continue
            if ch in '"\'':
                q = ch; out.append(ch); continue
            if ch == '#': break
            out.append(ch)
        if q is not None: issues.append((n, 'unterminated quote'))
        t = ''.join(out)
        par += t.count('(') - t.count(')')
        cur += t.count('{') - t.count('}')
        if par < 0: issues.append((n, 'unbalanced )')); par = 0
        if cur < 0: issues.append((n, 'unbalanced }')); cur = 0
    if par: issues.append(('EOF', f'{par} unclosed ('))
    if cur: issues.append(('EOF', f'{cur} unclosed {{'))
    return issues

bad = 0
for p in sys.argv[1:]:
    iss = check(p)
    print(('  OK   ' if not iss else '  BAD  ') + p)
    for n, m in iss:
        print(f'         line {n}: {m}'); bad = 1
sys.exit(bad)
