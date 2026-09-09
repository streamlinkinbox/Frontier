#!/usr/bin/env bash
#============================================================================================================================================
# Scratchpad/CompileInterfaceShaders.sh — compile and verify the spatial-interface shaders without the full toolchain
#============================================================================================================================================
# The Windows build lowers .slang with slangc (ToolchainSequence.ps1) and the CMake build uses glslc. This script is
# the sandbox/CI equivalent: it compiles both raster stages to SPIR-V with glslang, links them so the varyings are
# checked against each other, and prints the std430 reflection of the instance slot so the byte layout can be
# compared with the static_asserts in Engine/SpatialInterface/InterfaceLayoutCodec.h.
#
#   Usage:  bash Scratchpad/CompileInterfaceShaders.sh [path-to-glslang]
#
# Exits non-zero if either stage fails to compile or the two stages fail to link.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GLSLANG="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"

if [ ! -x "$GLSLANG" ]; then
    echo "[Interface] No glslang found. Pass its path as the first argument, or build it:"
    echo "    git clone --depth 1 https://github.com/KhronosGroup/glslang.git /tmp/gl"
    echo "    cmake -S /tmp/gl -B /tmp/gl/build -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=0 -DGLSLANG_TESTS=OFF"
    echo "    cmake --build /tmp/gl/build -j --target glslang-standalone"
    exit 2
fi

# glslang resolves #include "Shaders/..." against the include path, so stage the headers under that prefix.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Shaders"
cp "$ROOT/Engine/Shaders/InterfaceRecords.slang"        "$STAGE/Shaders/"
cp "$ROOT/Engine/Shaders/InterfaceSignedDistance.slang" "$STAGE/Shaders/"

OUT="$ROOT/Engine/Shaders"

echo "[Interface] glslang: $("$GLSLANG" --version | head -1)"

echo "[Interface] Compiling the vertex stage..."
"$GLSLANG" -V --target-env vulkan1.2 -I"$STAGE" -S vert -o "$OUT/InterfaceRaster.vert.spv" \
           "$ROOT/Engine/Shaders/InterfaceRaster.vert.slang"

echo "[Interface] Compiling the fragment stage..."
"$GLSLANG" -V --target-env vulkan1.2 -I"$STAGE" -S frag -o "$OUT/InterfaceRaster.frag.spv" \
           "$ROOT/Engine/Shaders/InterfaceRaster.frag.slang"

# Linking is the real interface check: a varying mismatch between the stages fails here, not on the GPU.
echo "[Interface] Linking both stages (varying interface check)..."
cp "$ROOT/Engine/Shaders/InterfaceRaster.vert.slang" "$STAGE/link.vert"
cp "$ROOT/Engine/Shaders/InterfaceRaster.frag.slang" "$STAGE/link.frag"
"$GLSLANG" -V --target-env vulkan1.2 -I"$STAGE" -l -o "$STAGE/linked.spv" "$STAGE/link.vert" "$STAGE/link.frag"

echo "[Interface] Instance slot layout (compare with the static_asserts in InterfaceLayoutCodec.h):"
"$GLSLANG" --target-env vulkan1.2 -I"$STAGE" -S frag -q "$ROOT/Engine/Shaders/InterfaceRaster.frag.slang" \
    | grep -E 'Figures\.|topLevelArrayStride' | sed 's/^/    /'

echo "[Interface] SPIR-V written:"
ls -l "$OUT/InterfaceRaster.vert.spv" "$OUT/InterfaceRaster.frag.spv" | sed 's/^/    /'
echo "[Interface] OK"
