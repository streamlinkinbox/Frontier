#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckColourTransfer.sh — one tone map, not four
#============================================================================================================================================
# Celestial port. The sky feeds BOTH render paths, so the two must turn identical radiance into identical pixels.
#    They did not: the ReSTIR kernel and the denoiser applied ACES with exposure and low-light desaturation, while
#    ShadowResolve and the CPU raster applied plain Reinhard with neither — up to 49/255 apart at mid-tones.
#
# This guards the three ways that comes back:
#    ① a path grows its own curve again,
#    ② the presentation format becomes _SRGB while the shader still applies gamma (the curve applied twice), and
#    ③ the low-light desaturation is recomputed per pixel instead of consumed per frame.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[ColourTransfer] the transfer has one definition"
[ -f Engine/DisplayPresentation/ColourTransfer.h ]
Report $? "ColourTransfer.h exists"

# The CPU raster must go through it rather than inlining a curve.
grep -q 'ColourPipeline::ApplyToByte' Engine/GeometricRaster/VisibilityRaster.cpp
Report $? "the CPU raster uses the shared pipeline"

# The literal Reinhard-plus-gamma that used to be inlined there must not return.
! grep -qE '1\.0f \+ Reinhard' Engine/GeometricRaster/VisibilityRaster.cpp
Report $? "the raster's private Reinhard is gone"

echo
echo "[ColourTransfer] the curves agree across the render paths"
# ReSTIR and the denoiser must keep using the same ACES fit; the constants are the fingerprint.
for File in Engine/Shaders/ReSTIRViewport.slang Engine/Shaders/AtrousDenoise.slang; do
    grep -q '2\.51' "$File" && grep -q '0\.59' "$File"
    Report $? "$(basename "$File") uses the ACES fit"
done
# ShadowResolve is the GI-off GPU path. It still carries Reinhard: that is a KNOWN remaining divergence, recorded
#    rather than hidden, because changing it needs the shader's own exposure plumbing (step 6's post work).
if grep -q 'Accumulated / (1.0 + Accumulated)' Engine/Shaders/ShadowResolve.slang; then
    printf '  %-66s NOTE\n' "ShadowResolve still uses Reinhard - tracked, see ColourTransfer.h"
fi

echo
echo "[ColourTransfer] the display encode is applied exactly once"
# UNORM plus in-shader gamma is correct. _SRGB plus in-shader gamma applies it twice and washes the image out.
grep -q 'VK_FORMAT_R8G8B8A8_UNORM' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the presentation image is UNORM, not _SRGB"
! grep -qE 'CreateStorageImage\([^)]*R8G8B8A8_SRGB' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "no _SRGB storage image would double-encode the gamma"

echo
echo "[ColourTransfer] low-light desaturation stays a per-frame value"
# The eye adapts to a scene, not to a pixel, and scene radiance is not cd/m2 until exposure has mapped it.
#    Recomputing the cone response per pixel desaturated the whole sky to grey once already.
! grep -qE 'ScotopicLimit|LowLightDesaturate' Engine/DisplayPresentation/ColourTransfer.h
Report $? "the transfer does not recompute the cone response per pixel"
grep -q 'QueryColourSaturation' Engine/DisplayPresentation/ExposureIntegrator.cpp
Report $? "ExposureIntegrator still owns the per-frame saturation"

echo
if [ "$Fail" != "0" ]; then echo "[ColourTransfer] FAILED"; exit 1; fi
echo "[ColourTransfer] OK"
