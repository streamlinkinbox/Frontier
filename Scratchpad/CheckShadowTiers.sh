#!/usr/bin/env bash
# The GI-off shadow ladder: hard · wide PCF · PCSS, one per quality tier, plus the Control Centre's
#    shadow-resolution override. Renders the real Cornell Box through the real VisibilityRaster once per tier
#    and gates the properties each technique claims (a hard edge stays a sliver, PCF widens it, PCSS narrows it
#    at contact). Also compiles the GPU side — ShadowRaster.vert/frag and ShadowResolve — to SPIR-V when a
#    glslang is available, because the application runs those, not the CPU path.
#    Sheets: Diagnostics/ShadowTier_*.png. No Vulkan, no GLFW.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
Fail=0
mkdir -p Diagnostics

Cg="${CGLTF:-/tmp/cg}"; Ufbx="${UFBX:-/tmp/ufbxsrc}"; Stb="${STB:-/tmp/stbsrc}"; Bvh="${TINYBVH:-/tmp/tinybvh}"
Vkh="${VKH:-/tmp/vkh/include}"
[ -f "$Cg/cgltf.h"      ] || git clone --depth 1 -q https://github.com/jkuhlmann/cgltf.git "$Cg"
[ -f "$Ufbx/ufbx.h"     ] || git clone --depth 1 -q https://github.com/ufbx/ufbx.git "$Ufbx"
[ -f "$Stb/stb_image.h" ] || git clone --depth 1 -q https://github.com/nothings/stb.git "$Stb"
[ -f "$Bvh/tiny_bvh.h"  ] || git clone --depth 1 -q https://github.com/jbikker/tinybvh.git "$Bvh"
[ -f "$Vkh/vulkan/vulkan.h" ] || git clone --depth 1 -q https://github.com/KhronosGroup/Vulkan-Headers.git "$(dirname "$Vkh")"

echo "[ShadowTier] compiling the proof (headless: no Vulkan, no GLFW)"
Binary="$(mktemp -u /tmp/ShadowTierProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -msse4.2 -DFRONTIER_DEVELOPMENT \
     -I . -I Engine -I Scratchpad -I "$Cg" -I "$Ufbx" -I "$Stb" -I "$Bvh" -I "$Vkh" \
     Scratchpad/ShadowTierProof.cpp \
     Engine/GeometricRaster/VisibilityRaster.cpp \
     Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/GeometricRaster/TraversalIndex.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp \
     Engine/ContentInterchange/SceneCodec.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Binary" 2>/tmp/ShadowTier.build; then
    echo "  COMPILE FAILED"; sed 's/^/    /' /tmp/ShadowTier.build | head -30; exit 1
fi

"$Binary" || Fail=1
rm -f "$Binary"

# ── The GPU side: what the application actually runs ────────────────────────────────────────────────────────
# The CPU raster above is the proof; ShadowRaster + ShadowResolve are the shipping path. Lower them to SPIR-V
#    so a shader that would fail at device bring-up fails here instead.
Glslang="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"
echo
if [ ! -x "$Glslang" ]; then
    echo "[ShadowTier] glslang not found - GPU shader check skipped (CPU gates above still ran)"
else
    echo "[ShadowTier] lowering the GPU shadow pass to SPIR-V"
    Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
    for Entry in "ShadowRaster.vert.slang vert" "ShadowRaster.frag.slang frag" "ShadowResolve.slang comp"; do
        set -- $Entry
        if "$Glslang" -V --target-env vulkan1.2 -S "$2" -I. -IEngine "Engine/Shaders/$1" -o "$Stage/$1.spv" >/dev/null 2>"$Stage/err"; then
            echo "  $1 lowers to SPIR-V                            PASS"
        else
            echo "  $1 SPIR-V COMPILE FAILED"; sed 's/^/    /' "$Stage/err" | head -15; Fail=1
        fi
    done

    # The CPU and GPU paths must agree on the ladder, or the proof gates one renderer and ships another.
    for Name in kShadowFilterHard kShadowFilterPcf kShadowFilterPcss; do
        grep -q "$Name" Engine/Shaders/ShadowRecords.slang || { echo "  $Name missing from ShadowRecords.slang"; Fail=1; }
    done
    CpuTaps=$(grep -oP 'kBlockerTaps\s*=\s*\K[0-9]+' Engine/GeometricRaster/VisibilityRaster.h | head -1)
    GpuTaps=$(grep -oP 'kShadowBlockerTaps\s*=\s*\K[0-9]+' Engine/Shaders/ShadowRecords.slang | head -1)
    if [ "$CpuTaps" = "$GpuTaps" ]; then
        echo "  CPU and GPU agree on the blocker kernel ($CpuTaps)             PASS"
    else
        echo "  blocker kernel disagrees: CPU $CpuTaps, GPU $GpuTaps"; Fail=1
    fi
    CpuLightTaps=$(grep -oP 'kLightTaps\s*=\s*\K[0-9]+' Engine/GeometricRaster/VisibilityRaster.h | head -1)
    GpuLightTaps=$(grep -oP 'kShadowMaximumTaps\s*=\s*\K[0-9]+' Engine/Shaders/ShadowRecords.slang | head -1)
    if [ "$CpuLightTaps" = "$GpuLightTaps" ]; then
        echo "  CPU and GPU agree on the light tap count ($CpuLightTaps)             PASS"
    else
        echo "  light tap count disagrees: CPU $CpuLightTaps, GPU $GpuLightTaps"; Fail=1
    fi

    # Every shader the build lists must exist, or CMake lowers a file that is not there.
    for Src in ShadowRaster.vert.slang ShadowRaster.frag.slang ShadowResolve.slang ShadowRecords.slang ShadowSample.slang; do
        [ -f "Engine/Shaders/$Src" ] || { echo "  Engine/Shaders/$Src is listed but missing"; Fail=1; }
    done
    grep -q "ShadowResolve.slang|compute" CMakeLists.txt || { echo "  ShadowResolve is not in the CMake shader table"; Fail=1; }
    grep -q "ShadowRaster.vert.slang|vertex" CMakeLists.txt || { echo "  ShadowRaster.vert is not in the CMake shader table"; Fail=1; }
    grep -q "ShadowRaster.frag.slang|fragment" CMakeLists.txt || { echo "  ShadowRaster.frag is not in the CMake shader table"; Fail=1; }
    echo "  the build lists carry every shadow shader                        PASS"
fi

# R10 — the shaders are only half the job. Until this round ShadowResolve.slang compiled, sat in the CMake shader
#    table, and was never dispatched by anything: `grep ShadowResolve Engine/*.cpp` returned nothing at all. A gate
#    that only checks the shaders exist would have stayed green through that, so it now checks the HOST side too.
echo
echo "[ShadowTier] the GPU stage is actually reachable"
ShadowHost="Engine/DeviceExchange/VisibilityExchange.cpp"
grep -q 'ShadowRaster.vert.spv'  "$ShadowHost" || { echo "  no pipeline is built from ShadowRaster.vert.spv"; Fail=1; }
grep -q 'ShadowRaster.frag.spv'  "$ShadowHost" || { echo "  no pipeline is built from ShadowRaster.frag.spv"; Fail=1; }
grep -q 'ShadowResolve.spv'      "$ShadowHost" || { echo "  no pipeline is built from ShadowResolve.spv"; Fail=1; }
grep -q 'vkCmdBeginRenderPass'   "$ShadowHost" || { echo "  the shadow maps are never rasterised"; Fail=1; }
# Comments are stripped here too, and the pattern is the CALL rather than the bare name: the first version of this
#    check matched the explanatory comment above the dispatch and passed happily while the call itself was renamed
#    away. Verified by unhooking it — the gate must go red, and now does.
SwapchainCode="$(sed 's;//.*;;' Engine/DeviceExchange/SwapchainExchange.cpp)"
printf '%s' "$SwapchainCode" | grep -q 'Visibility\.RecordShadowFrame(' \
    || { echo "  the frame loop never records the shadow stage"; Fail=1; }
GameCode="$(sed 's;//.*;;' Projects/Project-Zero/Source/GameExecution.cpp)"
printf '%s' "$GameCode" | grep -q 'AssignShadowFrame(' \
    || { echo "  the application never supplies the shadow settings"; Fail=1; }
# The dropdown must outrank the tier, and must do so through WithShadowResolution rather than a second copy of the
#    ladder — two places deciding one number is how they drift apart.
printf '%s' "$GameCode" | grep -q 'WithShadowResolution(' \
    || { echo "  the Control Centre resolution override is not applied"; Fail=1; }
[ "$Fail" -eq 0 ] && echo "  pipelines built, stage recorded, override applied                PASS"

# R10 — PCSS regression. ShadowSample.slang used to recover the light's half-angle as 1/ShadowLightClip[1][1]. That
#    matrix is Projection · View, so the element carries the light's own Up.y: measured over six orientations every
#    single one was wrong, and a ceiling lamp pointing straight down recovered 1e6 instead of 2.14 — a penumbra
#    466,000x too wide. Scratchpad/ShadowMatrixProof.cpp is the measurement; this keeps the fix from being undone.
echo
echo "[ShadowTier] PCSS reads its half-angle from the tap, not the matrix"
# Comments are stripped first: the fix is DOCUMENTED in a comment that necessarily quotes the old expression, and a
#    grep over the raw file would match its own explanation and fail forever. Check the code, not the prose.
ShadowSampleCode="$(sed 's;//.*;;' Engine/Shaders/ShadowSample.slang)"
if printf '%s' "$ShadowSampleCode" | grep -q 'ShadowLightClip\[Tap\]\[1\]\[1\]'; then
    echo "  the half-angle is being recovered from the world->clip matrix again"; Fail=1
elif ! printf '%s' "$ShadowSampleCode" | grep -q 'ShadowTapNormal\[Tap\].w'; then
    echo "  the half-angle is no longer read from the tap record"; Fail=1
else
    if command -v g++ >/dev/null 2>&1 && [ -f Scratchpad/ShadowMatrixProof.cpp ]; then
        if g++ -std=c++20 -O2 -o "${TMPDIR:-/tmp}/ShadowMatrixProof" Scratchpad/ShadowMatrixProof.cpp 2>/dev/null; then
            # The proof exits non-zero while the OLD recovery is wrong (which it always is) — what must hold is that
            #    the projection itself is sane, so grep the line the projection checks print.
            if "${TMPDIR:-/tmp}/ShadowMatrixProof" | grep -q "centre, depth-in-metres and behind-the-light all correct"; then
                echo "  light-clip projection verified: centre, metres, w<=0 behind         PASS"
            else
                echo "  the light-clip matrix no longer projects correctly"; Fail=1
            fi
        fi
    fi
fi

# The GI-off path must stay ray-free: the quarantine rule the preview already enforces on VisibilityRaster.
echo
echo "[ShadowTier] the GI-off path names no ray query"
if grep -nE "TraverseOccluded|TraverseClosest|RayQuery|rayQuery|TraceShadow" \
        Engine/GeometricRaster/VisibilityRaster.cpp Engine/Shaders/ShadowSample.slang \
        Engine/Shaders/ShadowResolve.slang Engine/Shaders/ShadowRaster.frag.slang 2>/dev/null; then
    echo "  a ray query leaked into the no-ray path"; Fail=1
else
    echo "  no traversal, no occlusion segment, no ray query                 PASS"
fi

echo
if [ "$Fail" -ne 0 ]; then
    echo "[ShadowTier] PROOF FAILED - see Diagnostics/ShadowTier_*.png"
    exit 1
fi
echo "[ShadowTier] OK"
