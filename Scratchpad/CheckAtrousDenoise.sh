#!/usr/bin/env bash
# R7 gate: the edge-avoiding à-trous denoiser removes noise without removing detail.
#
# Three checks:
#   1. CPU harness  — a verbatim port of the shader arithmetic, asserting the behaviours a plain blur would FAIL:
#                     silhouettes and shadow boundaries survive, background does not bleed, energy is preserved,
#                     and the filter is an exact identity when disabled.
#   2. SPIR-V       — AtrousDenoise.slang compiles for Vulkan 1.2.
#   3. Agreement    — the constants the harness hard-codes still match the ones in the shader. Without this the
#                     port could silently drift and keep passing while the shader misbehaved.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Glslang="${1:-$(command -v glslang || command -v glslangValidator || echo /tmp/gl/build/StandAlone/glslang)}"

echo "[AtrousDenoise] CPU filter harness"
g++ -std=c++20 -O2 -Wall -Wextra Scratchpad/AtrousDenoiseTest.cpp -o /tmp/AtrousDenoiseTest || exit 1
/tmp/AtrousDenoiseTest || exit 1

echo
echo "[AtrousDenoise] shader / harness agreement"
Fail=0

# The B3 spline kernel and the luminance vector must be identical on both sides.
grep -q '0.0625, 0.25, 0.375, 0.25, 0.0625' Engine/Shaders/AtrousDenoise.slang \
    || { echo "  shader lost the B3 spline kernel"; Fail=1; }
grep -q '0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f' Scratchpad/AtrousDenoiseTest.cpp \
    || { echo "  harness lost the B3 spline kernel"; Fail=1; }
grep -q '0.2126, 0.7152, 0.0722'   Engine/Shaders/AtrousDenoise.slang  || { echo "  shader luminance vector changed";  Fail=1; }
grep -q '0.2126f \* R'             Scratchpad/AtrousDenoiseTest.cpp    || { echo "  harness luminance vector changed"; Fail=1; }

# The three weights must all still be present in the shader — dropping one silently turns this into a blur.
for Term in NormalWeight DepthWeight LuminanceWeight; do
    grep -q "float $Term" Engine/Shaders/AtrousDenoise.slang || { echo "  shader is missing $Term"; Fail=1; }
done

# Variance must propagate as the square of the weights, or the filter's own strength estimate is wrong.
grep -q 'Weight \* Weight' Engine/Shaders/AtrousDenoise.slang || { echo "  shader lost the squared-weight variance"; Fail=1; }

# Background rejection appears twice: the centre early-out and the per-tap skip.
[ "$(grep -c 'w <= 0.0' Engine/Shaders/AtrousDenoise.slang)" -ge 2 ] \
    || { echo "  shader is missing a background (depth <= 0) rejection"; Fail=1; }

# ── Wiring ───────────────────────────────────────────────────────────────────────────────────────────────────────
# The kernel and the filter must agree on the luminance definition: the kernel measures the VARIANCE of this
# quantity and the filter compares neighbours by it. A mismatch makes the noise estimate describe a different
# signal from the one being filtered.
grep -q '0.2126, 0.7152, 0.0722' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  kernel luminance does not match the filter's"; Fail=1; }

# The kernel must skip its own tone map when the denoiser owns it, or the image is graded twice.
grep -q 'FeatureFlags & kFeatureDenoise' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  kernel does not defer the tone map to the filter"; Fail=1; }

# Both tone maps must be the same curve, or toggling the denoiser would change the grade.
for File in Engine/Shaders/ReSTIRViewport.slang Engine/Shaders/AtrousDenoise.slang; do
    grep -q 'a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14' "$File" \
        || { echo "  $File has a different ACES curve"; Fail=1; }
done

# Feature bit 7 must agree between the shader and the C++ mirror.
ShaderBit=$(grep -oP 'kFeatureDenoise\s*=\s*\K[0-9]+' Engine/Shaders/ReSTIRViewport.slang)
[ "$ShaderBit" = "128" ] || { echo "  shader denoise bit is $ShaderBit, expected 128 (1 << 7)"; Fail=1; }
grep -q 'DispatchFeatureDenoise\s*=\s*1u << 7' Engine/DeviceExchange/SwapchainExchange.h \
    || { echo "  C++ denoise bit mirror missing or not 1u << 7"; Fail=1; }

# The filter must actually be dispatched, once per level, and registered with both build systems.
grep -q 'Level < kDenoiseLevelCount' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  no per-level dispatch loop"; Fail=1; }
grep -q 'AtrousDenoise.spv' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the denoise pipeline never loads its SPIR-V"; Fail=1; }
grep -q 'AtrousDenoise' CMakeLists.txt \
    || { echo "  AtrousDenoise.slang is not in the CMake shader table"; Fail=1; }
grep -q 'AtrousDenoise' Projects/Project-Zero/Build/ToolchainSequence.ps1 \
    || { echo "  AtrousDenoise.slang is not in the Windows shader table"; Fail=1; }

# The kernel must report the variance OF THE MEAN (sample variance / n), not the raw sample variance: the image
# being filtered is a mean of n samples, and without the division the reported noise never falls, so the filter
# keeps blurring a converged image.
grep -q 'sampleVariance / count' Engine/Shaders/ReSTIRViewport.slang \
    || { echo "  kernel does not divide the sample variance by the sample count"; Fail=1; }

# The ping-pong barrier must cover BOTH slots and BOTH hazard directions. Level L reads one slot and overwrites
# the other — the slot the previous level was still reading. Ordering only write→read on the source leaves that
# write-after-read hazard unsynchronised, which surfaces as a per-workgroup tile pattern across the image.
grep -q 'VkImageMemoryBarrier Barriers\[2\]' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the denoise level barrier does not cover both ping-pong slots"; Fail=1; }
grep -q 'srcAccessMask               = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the denoise barrier does not order write-after-read"; Fail=1; }

# Every image the filter READS must be ordered against the kernel that WROTE it. The radiance/variance slots are
# covered by the per-level barrier, but HistorySurfaceImage is written by the ReSTIR kernel and read by every
# filter level for its normal, depth and background tests — it needs its own barrier before level 0. Missing it
# lets taps see unwritten depth (0), reject them as background, and filter with fewer taps than their neighbours.
grep -q 'KernelOutput.image                       = Vulkan->HistorySurfaceImage' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the surface image is not ordered against the kernel before the filter reads it"; Fail=1; }

# Every image sampled by the shader must appear in that ordering. If a new imageLoad target is added to the
# filter, this list must grow with it.
for Image in SourceImage SurfaceImage; do
    grep -q "imageLoad($Image" Engine/Shaders/AtrousDenoise.slang \
        || { echo "  expected the filter to read $Image"; Fail=1; }
done
ReadCount=$(grep -oP 'imageLoad\(\K[A-Za-z]+' Engine/Shaders/AtrousDenoise.slang | sort -u | wc -l)
[ "$ReadCount" = "2" ] \
    || { echo "  the filter now reads $ReadCount images - check each is barriered against its writer"; Fail=1; }

# ── Dispatch coverage ────────────────────────────────────────────────────────────────────────────────────────────
# The filter's workgroup size must match what the dispatch derives its group count from. Reusing the ReSTIR
# kernel's 16x16 count for an 8x8 shader covered exactly the top-left quarter of the image.
ShaderGroup=$(grep -oP 'local_size_x = \K[0-9]+' Engine/Shaders/AtrousDenoise.slang)
CppGroup=$(grep -oP 'kDenoiseGroupSize\s*=\s*\K[0-9]+' Engine/DeviceExchange/SwapchainExchange.cpp)
[ -n "$CppGroup" ] || { echo "  kDenoiseGroupSize is not defined"; Fail=1; }
[ "$ShaderGroup" = "$CppGroup" ] \
    || { echo "  filter workgroup is $ShaderGroup but the dispatch uses $CppGroup"; Fail=1; }
grep -q 'vkCmdDispatch(Command, DenoiseGroupX, DenoiseGroupY' Engine/DeviceExchange/SwapchainExchange.cpp \
    || { echo "  the denoise dispatch does not use its own group count"; Fail=1; }

# The denoiser's descriptor sets are populated by WriteDescriptorSet(), which BringDescriptorSet() calls at the end
# of its own stage — so the pipeline must be brought up FIRST or the sets are never written.
DenoiseLine=$(grep -n '"BringDenoisePipeline"' Engine/DeviceExchange/SwapchainExchange.cpp | head -1 | cut -d: -f1)
SetLine=$(grep -n '"BringDescriptorSet"'   Engine/DeviceExchange/SwapchainExchange.cpp | head -1 | cut -d: -f1)
if [ -n "$DenoiseLine" ] && [ -n "$SetLine" ] && [ "$DenoiseLine" -gt "$SetLine" ]; then
    echo "  BringDenoisePipeline runs after BringDescriptorSet - its sets would never be written"
    Fail=1
fi

[ "$Fail" = "0" ] && echo "  constants, weights, wiring and dispatch coverage agree           PASS"
[ "$Fail" = "0" ] || exit 1

if [ ! -x "$Glslang" ]; then
    echo
    echo "[AtrousDenoise] glslang not found - SPIR-V check skipped"
    echo "[AtrousDenoise] OK (CPU only)"
    exit 0
fi

echo
echo "[AtrousDenoise] compiling AtrousDenoise.slang to SPIR-V"
Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
"$Glslang" -V --target-env vulkan1.2 -S comp Engine/Shaders/AtrousDenoise.slang -o "$Stage/AtrousDenoise.spv" >/dev/null \
    || { echo "  SPIR-V COMPILE FAILED"; exit 1; }
echo "  SPIR-V compiles                                                  PASS"

echo
echo "[AtrousDenoise] OK"
