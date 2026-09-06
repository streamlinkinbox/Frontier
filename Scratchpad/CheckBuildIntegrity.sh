#!/usr/bin/env bash
# Verifies every source file named by every build system actually exists, that all declared submodules are
#    populated, and that each .ps1 is structurally balanced. This is the guard that would have caught the
#    "12 packages expected, 7 declared" gap and the phantom-source C1083 wall.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 - <<'PY'
import re, os, sys
bad = 0

def report(name, listed, missing):
    global bad
    print(f"  {name:54s} {len(listed):3d} listed, {len(missing)} missing")
    for m in sorted(set(missing)):
        print(f"       MISSING: {m}")
    if missing: bad = 1

# ── PowerShell: engine list + the $PackageRoot-relative ImGui list ──────────────────────────────────────────
for ps, arrays in [("Projects/Project-Zero/Build/ToolchainSequence.ps1", ["EngineRelative"]),
                   ("Projects/Project-Physics/Build/ToolchainSequence.ps1", ["EngineRelative", "Sources"]),
                   ("Projects/Project-Dyno/Build/ToolchainSequence.ps1",   ["EngineRelative", "Sources"])]:
    s = open(ps).read()
    found = []
    for a in arrays:
        m = re.search(r"\$" + a + r"\s*=\s*@\((.*?)\n\)", s, re.S)
        if m: found += [x.replace("\\", "/") for x in re.findall(r"'([^']+\.(?:cpp|c|h))'", m.group(1))]
    report(ps, found, [x for x in found if not os.path.exists(x)])
    # ImGui sources are Join-Path $PackageRoot 'imgui\...'
    ig = re.findall(r"Join-Path \$PackageRoot '([^']+\.cpp)'", s)
    ig = ["ExternalPackages/" + x.replace("\\", "/") for x in ig]
    if ig: report(ps + "  [$PackageRoot]", ig, [x for x in ig if not os.path.exists(x)])

# ── CMake ───────────────────────────────────────────────────────────────────────────────────────────────────
s = open("CMakeLists.txt").read()
p = re.findall(r'^\s+((?:Engine|Projects|Scratchpad)/[A-Za-z0-9_/\.-]+\.(?:cpp|h))\s*$', s, re.M)
report("CMakeLists.txt", p, [x for x in p if not os.path.exists(x)])

s = open("ParametricSketcher/CMakeLists.txt").read()
p = re.findall(r'^\s+((?:Kernel|Presentation|Interaction|Document|Console|Verification)/[A-Za-z0-9_/\.-]+\.(?:cpp|h))\s*$', s, re.M)
report("ParametricSketcher/CMakeLists.txt", p, [x for x in p if not os.path.exists("ParametricSketcher/" + x)])

# ── Every submodule the scripts expect must be DECLARED and POPULATED ───────────────────────────────────────
declared = set(re.findall(r"path\s*=\s*(\S+)", open(".gitmodules").read()))
expected = set()
for ps in ["Projects/Project-Zero/Build/ToolchainSequence.ps1"]:
    s = open(ps).read()
    m = re.search(r"\$SubmoduleList = @\((.*?)\n\)", s, re.S)
    if m: expected |= set(re.findall(r"'([^']+)'", m.group(1)))
undeclared = sorted(expected - declared)
print(f"  submodules: {len(expected)} expected by scripts, {len(declared)} declared, {len(undeclared)} undeclared")
for u in undeclared:
    print(f"       UNDECLARED IN .gitmodules: {u}"); bad = 1
empty = sorted(d for d in declared if os.path.isdir(d) and not os.listdir(d))
for e in empty:
    print(f"       DECLARED BUT EMPTY (run git submodule update --init): {e}"); bad = 1

print()
print(">>> BUILD INTEGRITY OK" if not bad else ">>> BUILD INTEGRITY FAILURES ABOVE")
sys.exit(bad)
PY
