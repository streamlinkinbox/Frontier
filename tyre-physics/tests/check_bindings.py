#!/usr/bin/env python3
"""Static check: the resource list each compute entry point binds (src/sim/tyreSim.js USES)
must equal the set of global bindings its WGSL body uses, including helper functions.
Run: python3 tests/check_bindings.py  (exit 1 on mismatch)."""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
wgsl = (root / 'src/shaders/tyre_sim.wgsl').read_text()
js = (root / 'src/sim/tyreSim.js').read_text()

# binding name -> WGSL global name, from "@binding(N) var<...> NAME"
globals_ = dict((m.group(2), int(m.group(1))) for m in re.finditer(r'@binding\((\d+)\)\s+var(?:<[^>]*>)?\s+(\w+)', wgsl))
# BIND map in JS: { P: 0, POS: 1, ... }
bind_js = dict((k, int(v)) for k, v in re.findall(r'\b([A-Z_]+):\s*(\d+)', js.split('const USES')[0]))
uses_block = js[js.index('const USES'):js.index('});', js.index('const USES'))]
uses = {m.group(1): [x.strip().strip("'") for x in m.group(2).split(',') if x.strip()]
        for m in re.finditer(r"(\w+): \[([^\]]*)\]", uses_block)}

# split the WGSL into top-level functions
funcs = {}
for m in re.finditer(r'(?:@compute[^\n]*\n)?fn (\w+)\s*\(', wgsl):
    start = m.start()
    depth, i = 0, wgsl.index('{', m.end())
    j = i
    while True:
        if wgsl[j] == '{': depth += 1
        elif wgsl[j] == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    funcs[m.group(1)] = wgsl[start:j + 1]

def names_in(body):
    code = re.sub(r'//[^\n]*', '', body)
    return {w for w in re.findall(r'\b[A-Za-z_]\w*\b', code)}

def calls(body):
    return {c for c in re.findall(r'\b(\w+)\s*\(', re.sub(r'//[^\n]*', '', body)) if c in funcs}

def used(fn, seen=None):
    seen = seen or set()
    if fn in seen: return set()
    seen.add(fn)
    names = names_in(funcs[fn])
    out = {n for n in names if n in globals_}
    for c in calls(funcs[fn]):
        out |= used(c, seen)
    return out

ok = True
for entry in uses:
    if entry not in funcs:
        print(f'MISSING entry point {entry}'); ok = False; continue
    u = used(entry)
    declared = set(uses[entry])
    # map binding-name keys to the WGSL global names (same names in this module)
    if u != declared:
        ok = False
        print(f'MISMATCH {entry}: wgsl uses {sorted(u)} ; USES lists {sorted(declared)}')
for name in funcs:
    if '@compute' in wgsl[max(0, wgsl.rfind('\n', 0, wgsl.index('fn ' + name + '(')) - 80):wgsl.index('fn ' + name + '(')] and name not in uses:
        print(f'NOT IN USES: {name}'); ok = False
print('bindings OK' if ok else 'bindings MISMATCH')
sys.exit(0 if ok else 1)
