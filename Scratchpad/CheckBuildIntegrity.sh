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
if bad: print(">>> BUILD INTEGRITY FAILURES ABOVE")
sys.exit(bad)
PY
Fail=$?

# ── The project's own translation unit must actually PARSE ───────────────────────────────────────────────────────
# 🔴 Everything above checks that the files a build system names exist. None of it compiles anything, and a
# proof suite made entirely of small harnesses never touches GameExecution.cpp at all — so a symbol that is out
# of scope there passes thirty green suites and fails on the developer's machine. That happened: a readout added
# to the Moon panel referenced a render height declared two hundred lines further down, and it was committed.
#
# -fsyntax-only, so this is a parse rather than a build: about two seconds, no linking, no Vulkan runtime. The
# headers come from Khronos rather than the LunarG SDK, which is not installable here.
Vkh="${VKH:-/tmp/vkh/include}"
[ -f "$Vkh/vulkan/vulkan.h" ] || git clone --depth 1 -q https://github.com/KhronosGroup/Vulkan-Headers.git "$(dirname "$Vkh")" 2>/dev/null
if [ ! -f "$Vkh/vulkan/vulkan.h" ]; then
    echo "  Vulkan headers unavailable — the GameExecution parse was SKIPPED"
else
    Includes="-I . -I $Vkh -I Projects/Project-Zero/Source -I Projects/Project-Dyno/Source -I Engine"
    for Package in glfw/include imgui imgui/backends thorvg/inc jolt miniaudio stb tomlpp/include \
                   cgltf tinybvh fast_obj ufbx earcut/include clipper2/CPP/Clipper2Lib/include; do
        Includes="$Includes -I ExternalPackages/$Package"
    done
    if g++ -std=c++20 -fsyntax-only $Includes Projects/Project-Zero/Source/GameExecution.cpp 2>/tmp/BuildIntegrity.parse; then
        echo "  Projects/Project-Zero/Source/GameExecution.cpp                     parses"
    else
        echo "  GameExecution.cpp DOES NOT PARSE:"; sed 's/^/       /' /tmp/BuildIntegrity.parse | head -20; Fail=1
    fi
fi

echo
if [ "$Fail" = "0" ]; then echo ">>> BUILD INTEGRITY OK"; else echo ">>> BUILD INTEGRITY FAILURES ABOVE"; fi
exit "$Fail"
