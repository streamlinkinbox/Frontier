#!/usr/bin/env bash
# D2 gate: the (instance, primitive) hit identity is correct, consistent between call sites, and changes NOTHING
#    the GPU can observe about buffer layout.
#
# Three checks:
#   1. CPU harness  — the identity rule against an independent brute-force oracle, on real scene layouts.
#   2. SPIR-V       — ReSTIRViewport.slang still compiles.
#   3. Reflection   — every binding, offset and stride is byte-identical to the pre-D2 shader. This is the check
#                     that matters: a moved offset would corrupt reservoirs silently rather than fail loudly.
#                     glslang's own "index" field is internal declaration ordering and is normalised away.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Glslang="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"

echo "[HitIdentity] CPU identity harness"
g++ -std=c++20 -O2 -Wall -Wextra Scratchpad/HitIdentityTest.cpp -o /tmp/HitIdentityTest || exit 1
/tmp/HitIdentityTest || exit 1

if [ ! -x "$Glslang" ]; then
    echo "[HitIdentity] glslang not found - SPIR-V and reflection checks skipped"
    echo "[HitIdentity] OK (CPU only)"
    exit 0
fi

Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
mkdir -p "$Stage/Shaders"
cp Engine/Shaders/*.slang "$Stage/Shaders/"

echo "[HitIdentity] compiling ReSTIRViewport.slang to SPIR-V"
"$Glslang" -V --target-env vulkan1.2 -S comp -I"$Stage" "$Stage/Shaders/ReSTIRViewport.slang" \
    -o "$Stage/ReSTIRViewport.spv" >/dev/null || { echo "[HitIdentity] SPIR-V COMPILE FAILED"; exit 1; }

# Reflection against the pre-D2 revision of the shader, if git can produce one.
Baseline="$Stage/Shaders_base"
if git cat-file -e "HEAD:Engine/Shaders/ReSTIRViewport.slang" 2>/dev/null; then
    mkdir -p "$Baseline/Shaders"
    cp Engine/Shaders/*.slang "$Baseline/Shaders/"
    git show "HEAD:Engine/Shaders/ReSTIRViewport.slang" > "$Baseline/Shaders/ReSTIRViewport.slang"

    Normalise() { sed -E 's/, index [0-9-]+//' | grep -E 'offset|binding' | sed -E 's/^Instances\[0\]\./Instances./' | sort -u; }
    "$Glslang" -V --target-env vulkan1.2 -S comp -I"$Baseline" -q "$Baseline/Shaders/ReSTIRViewport.slang" 2>/dev/null | Normalise > "$Stage/base.txt"
    "$Glslang" -V --target-env vulkan1.2 -S comp -I"$Stage"    -q "$Stage/Shaders/ReSTIRViewport.slang"    2>/dev/null | Normalise > "$Stage/curr.txt"

    if diff -q "$Stage/base.txt" "$Stage/curr.txt" >/dev/null; then
        echo "[HitIdentity] reflection identical to HEAD ($(wc -l < "$Stage/base.txt") bindings/offsets/strides)"
    else
        echo "[HitIdentity] REFLECTION CHANGED vs HEAD:"
        diff "$Stage/base.txt" "$Stage/curr.txt" | head -20
        exit 1
    fi
fi

echo "[HitIdentity] OK"
