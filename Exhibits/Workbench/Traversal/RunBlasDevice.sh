#!/usr/bin/env bash
# D9b — THE GPU RUN. Builds `BlasDeviceRun` and runs D9's two kernels on a Vulkan device, comparing their output byte for
#    byte with the CPU mirror (BlasBuildMirror). It is the one part of D9 that cannot happen in a sandbox with no GPU:
#    everything else — the kernels' lowering, the mirror, the payload, the plan, the pipelines' compilation — is gated
#    by tools that need no device at all.
#
# What it does:
#   1. locates Vulkan-Headers and tinybvh (ExternalPackages/, an env override, or the ~/.cache mirrors);
#   2. lowers the two kernels if their .spv files are missing (Tools/Build/CheckShaders.sh);
#   3. compiles and links the runner with the Vulkan-Headers only — the loader is opened at RUNTIME with dlopen, so no
#      -lvulkan is needed to build this;
#   4. runs it. Exit codes: 0 = the device matches the mirror · 1 = a mismatch (RED) · 2 = no Vulkan device (SKIP).
#
# ⚠️ SKIP is not a pass: it means this machine could not answer the question. The script says so and exits 0 so a
#    no-GPU CI leg does not look like a failure. On a machine with a GPU the verdict is the runner's.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[BlasDeviceRun] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    exit 1
fi

TB=""
for candidate in "$PWD/ExternalPackages/tinybvh" "${TWO_LEVEL_BVH_EXT:-}" "$HOME/.cache/m7/tinybvh-mirror"; do
    [ -n "$candidate" ] && [ -f "$candidate/tiny_bvh.h" ] && TB="$candidate" && break
done
if [ -z "$TB" ]; then
    echo "[BlasDeviceRun] RED — tiny_bvh.h not found (tried ExternalPackages/tinybvh, \$TWO_LEVEL_BVH_EXT, ~/.cache/m7/tinybvh-mirror)"
    exit 1
fi
echo "[BlasDeviceRun] headers: $Vk + $TB"

# ② the SPIR-V the pipelines are created from. Lowered here if absent — through the SAME recipe the compile gate uses
#    (SHADER_OUT tells CheckShaders.sh to keep its output), because a machine with a GPU is exactly the machine that
#    should not have to know how to lower a shader first. Nothing lands in the repository: the artifacts go to
#    Build/Spriv/, the build tree's business.
Spirv="$PWD/Build/Spriv"
if [ ! -f "$Spirv/BlasBuild.spv" ] || [ ! -f "$Spirv/BlasRefit.spv" ]; then
    echo "[BlasDeviceRun] lowering the kernels into Build/Spriv …"
    SHADER_OUT="$Spirv" bash Tools/Build/CheckShaders.sh | tail -4
fi
if [ ! -f "$Spirv/BlasBuild.spv" ] || [ ! -f "$Spirv/BlasRefit.spv" ]; then
    echo "[BlasDeviceRun] RED — no .spv in Build/Spriv; lower them with SHADER_OUT=Build/Spriv bash Tools/Build/CheckShaders.sh"
    exit 1
fi

Bin="$(mktemp -u /tmp/BlasDeviceRun.XXXXXX)"
# -Wno-missing-field-initializers: Vulkan structs are initialised sType-first, which -Wextra reports once per struct.
# -ffunction-sections -fdata-sections -Wl,--gc-sections: drops the material exporter chain this runner never touches.
if ! g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT -Wall -Wextra -Wno-missing-field-initializers \
     -Wno-uninitialized -Wno-array-bounds -Wno-maybe-uninitialized \
     -ffunction-sections -fdata-sections -Wl,--gc-sections -mavx2 -mfma -msse4.2 \
     -I Engine -I Engine/GeometricRaster -I Engine/DeviceExchange -I Engine/ContentInterchange \
     -I "$Vk/Vulkan-Headers/include" -I "$TB" \
     Exhibits/Workbench/Traversal/BlasDeviceRun.cpp \
     Engine/GeometricRaster/BlasBuildMirror.cpp \
     Engine/GeometricRaster/BlasDevicePayload.cpp \
     Engine/GeometricRaster/BlasBuildPipeline.cpp \
     Engine/GeometricRaster/InstanceAcceleration.cpp \
     Engine/GeometricRaster/TraversalIndex.cpp \
     Engine/ContentInterchange/MaterialSwatchStructure.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Bin" -ldl 2>/tmp/BlasDeviceRun.build; then
    echo "[BlasDeviceRun] COMPILE FAILED"; sed 's/^/    /' /tmp/BlasDeviceRun.build | head -40; exit 1
fi
if [ -s /tmp/BlasDeviceRun.build ]; then echo "[BlasDeviceRun] warnings:"; sed 's/^/    /' /tmp/BlasDeviceRun.build | head -20; fi

"$Bin" --shaders "$Spirv" "$@"
Status=$?
rm -f "$Bin"
if [ "$Status" -eq 2 ]; then
    echo "[BlasDeviceRun] SKIPPED — no Vulkan device on this machine. The kernels have been compiled, pinned and their CPU"
    echo "               mirror has been measured; running them is the item this script exists to finish elsewhere."
    exit 0
fi
if [ "$Status" -ne 0 ]; then echo "[BlasDeviceRun] RED — the device disagreed with the mirror"; exit 1; fi
echo "[BlasDeviceRun] GREEN"
