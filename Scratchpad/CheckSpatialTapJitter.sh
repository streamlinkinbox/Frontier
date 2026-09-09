#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  CheckSpatialTapJitter.sh — ReSTIR spatial reuse must sample a JITTERED neighbourhood, never a fixed stencil
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  A fixed tap offset set partitions the image into residue classes that never share a light sample, producing a permanent
#  lattice that temporal accumulation sharpens rather than removes. This gate runs the numeric proof and then checks that the
#  shader still derives its taps the way the proof assumes.
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
set -u

Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$Root" || exit 1

Shader="Engine/Shaders/ReSTIRViewport.slang"
Harness="Scratchpad/SpatialTapJitterTest.cpp"
Fail=0

# ── CPU proof ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
echo "[SpatialTapJitter] numeric proof"
Binary="$(mktemp -u /tmp/SpatialTapJitter.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra "$Harness" -o "$Binary" 2>/tmp/SpatialTapJitter.build; then
    echo "  harness failed to build:"
    sed 's/^/    /' /tmp/SpatialTapJitter.build | head -20
    exit 1
fi
if ! "$Binary"; then
    Fail=1
fi
rm -f "$Binary"

# ── Shader agreement ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
echo
echo "[SpatialTapJitter] shader / harness agreement"

# The fixed-radius constant must be gone. Its presence means someone reverted the fix.
if grep -q 'kSpatialRadiusPx' "$Shader"; then
    echo "  the fixed kSpatialRadiusPx constant is back — the reuse lattice has returned"
    Fail=1
fi

# The band the harness asserts against must be the band the shader uses.
for Name in kSpatialRadiusMinPx kSpatialRadiusMaxPx; do
    ShaderValue=$(grep -oP "${Name}\s*=\s*\K[0-9.]+" "$Shader" | head -1)
    HarnessValue=$(grep -oP "${Name}\s*=\s*\K[0-9.]+" "$Harness" | head -1)
    if [ -z "$ShaderValue" ]; then
        echo "  $Name is missing from the shader"
        Fail=1
    elif [ "$ShaderValue" != "$HarnessValue" ]; then
        echo "  $Name is $ShaderValue in the shader but $HarnessValue in the harness"
        Fail=1
    fi
done

# The taps must actually be rotated and jittered, not merely renamed constants.
grep -q 'angle + float(tap)' "$Shader" \
    || { echo "  the tap cross is not rotated per frame"; Fail=1; }
grep -q 'mix(kSpatialRadiusMinPx, kSpatialRadiusMaxPx' "$Shader" \
    || { echo "  the tap radius is not jittered within the band"; Fail=1; }

# The jitter must draw from its OWN stream. Consuming the main `seed` would shift every subsequent resampling draw and
# silently invalidate the R6 identity proofs.
grep -q 'tapSeed' "$Shader" \
    || { echo "  the jitter does not use a separate RNG stream"; Fail=1; }
if grep -q 'RandFloat(seed) \* 6.28318531' "$Shader"; then
    echo "  the jitter is drawing from the main resampling seed — R6 identity proofs would shift"
    Fail=1
fi

# A rounded-to-zero tap would merge a pixel with itself and inflate its own sample count.
grep -q 'offset.x == 0 && offset.y == 0' "$Shader" \
    || { echo "  degenerate (0,0) taps are not rejected"; Fail=1; }

[ "$Fail" = "0" ] && echo "  tap derivation agrees with the proof                             PASS"

# ── SPIR-V ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
echo
Glslang=""
for Candidate in /tmp/gl/build/StandAlone/glslang "$(command -v glslang 2>/dev/null)" "$(command -v glslangValidator 2>/dev/null)"; do
    [ -n "$Candidate" ] && [ -x "$Candidate" ] && Glslang="$Candidate" && break
done

if [ -z "$Glslang" ]; then
    echo "[SpatialTapJitter] glslang not found — SPIR-V check skipped"
    [ "$Fail" = "0" ] && echo "[SpatialTapJitter] OK (CPU only)" && exit 0
    echo "[SpatialTapJitter] FAILED"; exit 1
fi

echo "[SpatialTapJitter] compiling $Shader to SPIR-V"
if "$Glslang" -V --target-env vulkan1.2 -S comp "$Shader" -o /dev/null >/tmp/SpatialTapJitter.spv.log 2>&1; then
    echo "  SPIR-V compiles                                                  PASS"
else
    sed 's/^/    /' /tmp/SpatialTapJitter.spv.log | head -20
    Fail=1
fi

echo
if [ "$Fail" = "0" ]; then echo "[SpatialTapJitter] OK"; exit 0; else echo "[SpatialTapJitter] FAILED"; exit 1; fi
