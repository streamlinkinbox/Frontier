#!/usr/bin/env bash
# Bootstrap a glslang standalone binary for Tools/Build/CheckShaders.sh on a machine with no cmake/ninja.
#
# The shader gate picks up slangc / glslc / glslangValidator / glslang from PATH (or $VULKAN_SDK/bin) and reports SKIPPED
#    when none is present — which means a sandbox with no compiler silently stops checking 15 shaders. This script closes
#    that hole: it clones glslang, generates the two headers CMake would have generated, compiles the library sources
#    directly with g++ (in parallel, one object per TU, so a re-run is a relink) and leaves a working `glslang` at
#    /tmp/glslang-build/StandAlone/glslang.
#
#    usage: bash Tools/Build/BuildGlslang.sh          → builds, then prints the PATH line to use
#           GLSLANG_SRC=... GLSLANG_OUT=... bash ...  → somewhere else
#
# ⚠️ Two flags are load-bearing and look wrong until the failure they prevent is seen:
#      -DENABLE_SPIRV=1  without it the binary answers "This configuration of glslang does not have SPIR-V support";
#      -DENABLE_OPT=0    without it StandAlone links against SPIRV-Tools, which is not here (and is not needed: this
#                        gate asks "does it lower", not "is it optimised").
#    SPIRV/disassemble.cpp and SPIRV/doc.cpp are the two files that only matter with SPIRV enabled — omitting them is a
#    link error (spv::Disassemble / spv::Parameterize), not a warning.
set -uo pipefail

Src="${GLSLANG_SRC:-/tmp/glslang-src}"
Out="${GLSLANG_OUT:-/tmp/glslang-build}"
Jobs="$(nproc 2>/dev/null || echo 4)"

if [ ! -d "$Src" ]; then
    git clone --depth 1 https://github.com/KhronosGroup/glslang "$Src" || exit 1
    git -C "$Src" submodule update --init --depth 1 --recursive || exit 1
fi

# ─── the two headers CMake generates (build_info.h from the template, glsl_intrinsic_header.h from the extension dir) ─
python3 - "$Src" <<'PY' || exit 1
import sys, pathlib
src = pathlib.Path(sys.argv[1])
template = (src / "build_info.h.tmpl").read_text()
(src / "glslang" / "build_info.h").write_text(
    template.replace("@major@", "16").replace("@minor@", "0").replace("@patch@", "0").replace("@flavor@", ""))
print("[glslang] build_info.h generated")
PY
python3 "$Src/gen_extension_headers.py" -i "$Src/glslang/ExtensionHeaders" -o "$Src/glslang/glsl_intrinsic_header.h" || exit 1

mkdir -p "$Out/obj" "$Out/StandAlone"
Flags=(-std=c++17 -O2 -w -DGLSLANG_OSINCLUDE_UNIX -DENABLE_OPT=0 -DENABLE_SPIRV=1
       -I"$Src" -I"$Src/glslang/Include" -I"$Src/glslang" -I"$Src/SPIRV")

cd "$Src" || exit 1
Sources=$(ls glslang/MachineIndependent/*.cpp glslang/MachineIndependent/preprocessor/*.cpp \
             glslang/GenericCodeGen/*.cpp glslang/OSDependent/Unix/ossource.cpp \
             glslang/ResourceLimits/ResourceLimits.cpp \
             SPIRV/GlslangToSpv.cpp SPIRV/InReadableOrder.cpp SPIRV/Logger.cpp SPIRV/SpvBuilder.cpp \
             SPIRV/SpvPostProcess.cpp SPIRV/disassemble.cpp SPIRV/doc.cpp SPIRV/CInterface/*.cpp \
             StandAlone/StandAlone.cpp)
echo "[glslang] compiling $(printf '%s\n' "$Sources" | wc -l) translation units with $Jobs jobs"
printf '%s\n' "$Sources" | xargs -P "$Jobs" -I{} bash -c \
    'o="'"$Out"'/obj/$(echo "{}" | tr "/" "_").o"; [ -f "$o" ] || g++ '"${Flags[*]@Q}"' -c "{}" -o "$o"'
g++ -o "$Out/StandAlone/glslang" "$Out"/obj/*.o -lpthread || exit 1

if [ -x "$Out/StandAlone/glslang" ]; then
    echo "[glslang] built: $Out/StandAlone/glslang"
    echo "[glslang] run the shader gate with:"
    echo "    PATH=$Out/StandAlone:\$PATH bash Tools/Build/CheckShaders.sh"
else
    echo "[glslang] RED — no binary produced"
    exit 1
fi
