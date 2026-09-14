#!/usr/bin/env python3
"""Static dual-compile rules for the shipped .slang files.

The dual region of CelestialCore.slang / CelestialReSTIR.slang compiles as
C++17 (Host harness) AND as Slang (Vulkan engine) from the same text. These
rules reject constructs that are valid in only one language, so a violation
fails here instead of on the first GPU build. Exit code 0 iff all rules pass.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "Shaders"
FILES = [SHADERS / "CelestialCore.slang", SHADERS / "CelestialReSTIR.slang"]

FAILURES = []


def fail(rule, path, lineno, text):
    FAILURES.append("%s:%d: [%s] %s" % (path.name, lineno, rule, text))


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return "\n".join(line.split("//")[0] for line in src.split("\n"))


def check(path):
    raw = path.read_text()
    code = strip_comments(raw)
    lines = code.split("\n")
    dual = []
    in_shell = False
    for i, line in enumerate(lines, 1):
        if "#ifndef SLANG_COMPAT_CXX" in line:
            in_shell = True
        dual.append((i, line, in_shell))

    # R1: no `&` outside `&&` (no C++ references, no address-of).
    for i, line, _ in dual:
        if re.search(r"&(?!&)", line.replace("&&", "")):
            fail("R1-no-ref", path, i, line.strip()[:90])

    # R2: no whole-word `out` / `inout` (multi-value returns use structs).
    for i, line, _ in dual:
        if re.search(r"\b(inout|out)\b", line):
            fail("R2-no-out-param", path, i, line.strip()[:90])

    # R3: no `{...}` struct literals (declare + field-assign instead).
    allowed_brace = {"else", "struct", "cbuffer", "do"}
    for i, line, _ in dual:
        for m in re.finditer(r"(\w+)\s*\{", line):
            if m.group(1) not in allowed_brace:
                fail("R3-no-brace-init", path, i, line.strip()[:90])

    # R3b: a lone `{` opening a `= Type` / `return Type` literal across lines.
    prev = ""
    for i, line, _ in dual:
        if line.strip() == "{" and re.search(r"(=\s*|return\s+)(Cel\w*|float[234])\s*$", prev):
            fail("R3b-no-brace-init", path, i, (prev.strip() + " {")[:90])
        if line.strip():
            prev = line

    # R4: float literals need a leading zero (`.35f` is not portable Slang).
    for i, line, _ in dual:
        if re.search(r"(?<![0-9A-Za-z_.])\.[0-9]", line):
            fail("R4-leading-zero", path, i, line.strip()[:90])

    # R5: no fixed-width int aliases (uint/int are 32-bit in both languages).
    for i, line, _ in dual:
        if re.search(r"\b[ui]int(8|16|32|64)_t\b", line):
            fail("R5-no-fixed-int", path, i, line.strip()[:90])

    # R6: dual region must not reference shell globals (`gCapital`).
    for i, line, in_shell in dual:
        if not in_shell and re.search(r"\bg[A-Z]\w*", line):
            fail("R6-dual-pure", path, i, line.strip()[:90])

    # R7: float32 only (the panel is WebGL float; no double/long/short/half).
    for i, line, _ in dual:
        if re.search(r"\b(double|long|short|half)\b", line):
            fail("R7-float32", path, i, line.strip()[:90])

    # R8: compute-only (no textures, samplers, derivatives in shipped files).
    for i, line, _ in dual:
        if re.search(r"\b(texture|sampler|fwidth|ddx|ddy)\w*\b", line):
            fail("R8-compute-only", path, i, line.strip()[:90])

    # R9: brace/paren balance per file.
    for a, b, name in [("{", "}", "brace"), ("(", ")", "paren")]:
        if code.count(a) != code.count(b):
            fail("R9-balance", path, 0,
                 "%s imbalance: %d '%s' vs %d '%s'" % (name, code.count(a), a, code.count(b), b))

    # R10: struct-typed params carry `in` (input in Slang, empty in C++).
    for m in re.finditer(r"\b(\w+)\s+(\w+)\s*\(([^;{}]*)\)", code):
        _, _, params = m.groups()
        for pm in re.finditer(r"(?:^|,)\s*(in\s+)?(Cel\w+|Cel\w*)\s+\w+\s*(?=[,)])", "," + params):
            if not pm.group(1):
                lineno = code.count("\n", 0, m.start()) + 1
                fail("R10-in-param", path, lineno, ("...%s..." % params.strip()[:70]))
                break


def main():
    for path in FILES:
        if not path.exists():
            print("missing: %s" % path)
            return 1
        check(path)
    # R11: the ReSTIR shell exposes exactly the 8 documented entries.
    shell = (SHADERS / "CelestialReSTIR.slang").read_text()
    entries = set(re.findall(r"void\s+(diInitial|diTemporal|diSpatial|giInitial|giTemporal|giSpatial|shade|skyViewport)\s*\(", shell))
    expected = {"diInitial", "diTemporal", "diSpatial", "giInitial",
                "giTemporal", "giSpatial", "shade", "skyViewport"}
    if entries != expected:
        FAILURES.append("CelestialReSTIR.slang: [R11-entries] found %s" % sorted(entries))
    if FAILURES:
        print("CheckCelestialSlang: %d violation(s)" % len(FAILURES))
        for f in FAILURES:
            print("  " + f)
        return 1
    print("CheckCelestialSlang: all rules pass (%d files)" % len(FILES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
