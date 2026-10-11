#!/usr/bin/env bash
# M9 gate — the re-enable milestone. Three things, none of them needing a GPU:
#    ① the live configuration (Engine/DisplayPresentation/ReSTIRIntegrator.cpp compiled as a real TU): the denoiser and
#       the motion-vector reprojection default ON, the tier ladder never turns them off, BuildDispatch sets the two
#       feature bits the shader reads, and the two toggles keep their documented accumulation semantics;
#    ② the shipped shaders, compiled 1:1 as C++: Engine/Shaders/AtrousDenoise.slang through DenoiseCpuShim.h (three
#       mechanical substitutions, re-derived and verified by the proof) — identity switch, mean preservation, edge
#       stopping, variance propagation, early-out equivalence, and the presentation A/B (identical at convergence,
#       different before it);
#    ③ the text audits for what cannot be compiled here (ReSTIRViewport.slang's accumulation + reprojection, the
#       dispatcher's level chain, and the reprojection rule mirror).
#    Headers: Vulkan-Headers ($PWD/ExternalPackages, $MATERIAL_SCENES_EXT, or ~/.cache/m7) for SwapchainExchange.h's
#    DispatchConfiguration. No imgui, no toml, no GPU, no window.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[MaterialDenoise] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    exit 1
fi
echo "[MaterialDenoise] headers: $Vk"

#──────────────────────────────────────────────────────────────────────────────────────────────────────────────
# ① the transformed shader: the four mechanical substitutions (StageAtrousDenoise.py), each asserted there, then
#    re-derived from the shader text by the proof (§C0). The exhibit driver calls the same script.
#──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Stage="$(mktemp -d /tmp/DenoiseMirror.XXXXXX)"
if ! DO_STAGE="$Stage" python3 Exhibits/Workbench/Materials/StageAtrousDenoise.py; then
    echo "[MaterialDenoise] RED — the shader did not match the transform's expectations"; rm -rf "$Stage"; exit 1
fi
sed 's/^/    /' "$Stage/transform.manifest"

#──────────────────────────────────────────────────────────────────────────────────────────────────────────────
# ② compile the gate: the mirror TU (shader as C++) + the real engine TUs + the proof.
#──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Bin="$(mktemp -u /tmp/MaterialDenoise.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections -DFRONTIER_CPU_PORT \
     -I Exhibits/Workbench/Materials -I Engine/DisplayPresentation -I Engine/ContentInterchange -I Engine/DeviceExchange \
     -I Engine/GeometricRaster -I Engine/Shaders -I Projects/Project-Zero/Source -I "$Stage" -I "$Vk/Vulkan-Headers/include" \
     Exhibits/Workbench/Materials/DenoiseReprojectionProof.cpp \
     Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp \
     Engine/DisplayPresentation/ReSTIRIntegrator.cpp \
     Engine/DisplayPresentation/ExposureIntegrator.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp \
     Projects/Project-Zero/Source/FlyThroughSolver.cpp \
     Engine/GeometricRaster/CameraProjection.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Bin" 2>/tmp/MaterialDenoise.build; then
    echo "[MaterialDenoise] COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialDenoise.build | head -40; rm -rf "$Stage"; exit 1
fi
if [ -s /tmp/MaterialDenoise.build ]; then echo "[MaterialDenoise] warnings:"; sed 's/^/    /' /tmp/MaterialDenoise.build | head -20; fi

DO_STAGE="$Stage" "$Bin" 2>&1 | tee /tmp/MaterialDenoise.log
if ! grep -q "MATERIAL DENOISE: PASS" /tmp/MaterialDenoise.log; then
    echo "[MaterialDenoise] RED"; grep "FAIL" /tmp/MaterialDenoise.log | sed 's/^/    /' | head -30; rm -f "$Bin"; rm -rf "$Stage"; exit 1
fi
grep -c "^ok " /tmp/MaterialDenoise.log | xargs echo "[MaterialDenoise] GREEN — checks passed:"
rm -f "$Bin"; rm -rf "$Stage"
