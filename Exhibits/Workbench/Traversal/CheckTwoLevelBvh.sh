#!/usr/bin/env bash
# D6/D7/D8 gate — the two-level acceleration structure (object-space BLASes + an instance TLAS), measured against
#    today's single world-space tree on the M10 level's own triangles (MaterialSwatchStructure::Construct, the source
#    the shipped level and the CPU mirror both read). Gates ①–⑦ (D6/D7): blob identity under an identity transform; ray
#    agreement across chunked identity instances; transform agreement against the D5 triangle rewrite; the D7 per-frame
#    budget with the BLAS blobs hashed before/after; the instancing byte cost; the payload walk; and the shader
#    transcription/text pins. §⑧ (D8) is the deformation path: refit vs rebuild, an INDEPENDENT walker over the PACKED
#    blob (calibrated first against the UNTOUCHED structure and against the blob's own triangles, with a dedicated
#    axis-aligned ray census), leaf and parent-child containment, untouched-BLAS slice hashes, the layout measurement
#    that forces an in-place update, the update policy table, and the two refusals.
#    ⚠️ The walker in §⑧ is deliberately NOT tinybvh's host walker: see TraversalIndex.cpp's note on the NaN/FMax
#    mechanism behind "the CWBVH host walker misses head-on rays".
#    Headers: Vulkan-Headers (TriangleIndex reaches them through SwapchainExchange.h) and tinybvh (tiny_bvh.h).
#    Both seat from ExternalPackages/ when the submodules are initialised, from an env override, or from the ~/.cache
#    mirrors. Compiles TraversalIndex.cpp, the ONLY translation unit that defines TINYBVH_IMPLEMENTATION, together
#    with the new InstanceAcceleration.cpp, so the two SIMD layouts cannot diverge across TUs.
#    §⑨/⑩ (D9/D9b) also compile BlasBuildMirror.cpp (the CPU mirror), BlasDevicePayload.cpp (the host payload and the
#    dispatch plan) and BlasBuildPipeline.cpp (the Vulkan plumbing) — the last needs -Wno-missing-field-initializers
#    because Vulkan's structs are initialised sType-first, which -Wextra reports once per struct.
#    the CPU mirror the two GPU kernels are transcribed from and pinned to
#    it holds the ONE definition of the quantiser (InstanceAcceleration.cpp's EncodeNode forwards to it).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[TwoLevelBvh] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    echo "    git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers \$HOME/.cache/m7/Vulkan-Headers"
    exit 1
fi

TB=""
for candidate in "$PWD/ExternalPackages/tinybvh" "${TWO_LEVEL_BVH_EXT:-}" "$HOME/.cache/m7/tinybvh-mirror"; do
    [ -n "$candidate" ] && [ -f "$candidate/tiny_bvh.h" ] && TB="$candidate" && break
done
if [ -z "$TB" ]; then
    echo "[TwoLevelBvh] RED — tiny_bvh.h not found (tried ExternalPackages/tinybvh, \$TWO_LEVEL_BVH_EXT, ~/.cache/m7/tinybvh-mirror)"
    echo "    git clone --depth 1 https://github.com/jbikker/tinybvh \$HOME/.cache/m7/tinybvh-mirror"
    exit 1
fi
echo "[TwoLevelBvh] headers: $Vk + $TB"

Bin="$(mktemp -u /tmp/TwoLevelBvh.XXXXXX)"
# -Wno-uninitialized / -Wno-array-bounds / -Wno-maybe-uninitialized: tiny_bvh.h's SSE intrinsics read 4 lanes at a
#   time from 12-byte vectors, which GCC's bounds analysis flags inside the header itself. Third-party header noise is
#   silenced; our own TUs still compile under -Wall -Wextra -Werror.
# -ffunction-sections -fdata-sections -Wl,--gc-sections: drops MaterialSwatchStructure::Export (and the SceneCodec/cgltf
#   chain behind it) — this proof reads the level's triangles, it never writes glTF. -DFRONTIER_CPU_PORT, as the other
#   CPU-side proofs set.
if ! g++ -std=c++20 -O2 -Wall -Wextra -Werror -Wno-uninitialized -Wno-array-bounds -Wno-maybe-uninitialized \
     -Wno-missing-field-initializers -DFRONTIER_CPU_PORT \
     -ffunction-sections -fdata-sections -Wl,--gc-sections -mavx2 -mfma -msse4.2 \
     -I Engine/GeometricRaster -I Engine/DeviceExchange -I Engine/ContentInterchange \
     -I "$Vk/Vulkan-Headers/include" -I "$TB" \
     Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp \
     Engine/GeometricRaster/InstanceAcceleration.cpp \
     Engine/GeometricRaster/TraversalIndex.cpp \
     Engine/GeometricRaster/BlasBuildMirror.cpp \
     Engine/GeometricRaster/BlasDevicePayload.cpp \
     Engine/GeometricRaster/BlasBuildPipeline.cpp \
     Engine/ContentInterchange/MaterialSwatchStructure.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Bin" 2>/tmp/TwoLevelBvh.build; then
    echo "[TwoLevelBvh] COMPILE FAILED"; sed 's/^/    /' /tmp/TwoLevelBvh.build | head -40; exit 1
fi
if [ -s /tmp/TwoLevelBvh.build ]; then echo "[TwoLevelBvh] warnings:"; sed 's/^/    /' /tmp/TwoLevelBvh.build | head -20; fi

"$Bin" 2>&1 | tee /tmp/TwoLevelBvh.log
Status=${PIPESTATUS[0]}
rm -f "$Bin"
if [ "$Status" -ne 0 ]; then
    echo "[TwoLevelBvh] RED"; grep "FAIL" /tmp/TwoLevelBvh.log | sed 's/^/    /' | head -20; exit 1
fi
grep -c "PASS" /tmp/TwoLevelBvh.log | xargs echo "[TwoLevelBvh] GREEN — gates passed:"
