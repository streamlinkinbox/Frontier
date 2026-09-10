#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckSkyKernel.sh — the sky reaches the ray-tracing kernel, and says the same thing there
#============================================================================================================================================
# ReSTIRViewport had two branches reading "there is no environment light": a missed primary ray and an escaped
#    bounce ray. The first is the sky BEHIND the geometry; the second is the sky AS A LIGHT, and in an outdoor
#    scene it is the largest emitter present. Both are now wired to Shaders/SkyRecords.slang at binding 21.
#
# Three ways this rots, all guarded:
#    ① the shader drifts from AtmosphereModel, so the GI-on and GI-off skies stop matching,
#    ② the std140 block and its C++ mirror disagree, which is a wrong picture rather than a compile error,
#    ③ a miss branch quietly goes back to contributing nothing.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[SkyKernel] the shader computes the same sky as the model"
Binary="$(mktemp -u /tmp/SkyKernelParity.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" Scratchpad/SkyKernelParityProof.cpp \
     2>/tmp/SkyKernel.build; then
    echo "  PARITY PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/SkyKernel.build | head -16; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

Kernel=Engine/Shaders/ReSTIRViewport.slang
Sky=Engine/Shaders/SkyRecords.slang
Code="$(sed 's;//.*;;' "$Kernel")"
SkyCode="$(sed 's;//.*;;' "$Sky")"

echo
echo "[SkyKernel] both miss branches collect the sky"
# ⚠️ Comments are stripped first. Both files explain these branches at length and a bare word match would find
#    the prose rather than the code — the trap CheckShadowTiers records for the PCSS half-angle.
printf '%s' "$Code" | grep -q 'Resolve(pixel, SkyAlong(direction));'
Report $? "a missed primary ray resolves to the sky"
printf '%s' "$Code" | grep -q 'accumulatedRadiance += throughput \* SkyAlong(bounceDir);'
Report $? "an escaped bounce ray adds the sky as a light"
# The old text is the regression: if either branch says this again, the sky has been unwired.
! printf '%s' "$Code" | grep -q 'Resolve(pixel, vec3(0.0));'
Report $? "no miss branch resolves to black any more"

echo
echo "[SkyKernel] one entry point, so the two branches cannot diverge"
Entries=$(printf '%s' "$SkyCode" | grep -c 'vec3 SkyAlong(vec3 Direction)')
[ "$Entries" = "1" ]
Report $? "SkyRecords exposes exactly one SkyAlong ($Entries found)"

echo
echo "[SkyKernel] the block is declared where it was reserved"
printf '%s' "$SkyCode" | grep -q 'layout(std140, binding = 21) uniform SkyConstants'
Report $? "the sky uniform sits at binding 21"
# 22-24 must stay undeclared: a declared-but-unwritten descriptor is a validation error, not free space.
! printf '%s' "$Code$SkyCode" | grep -qE 'binding = 2[234]\)'
Report $? "bindings 22-24 stay reserved rather than declared"
printf '%s' "$Code" | grep -q 'binding = 25) uniform sampler2D Textures'
Report $? "the bindless table is still last, as a variable-count binding must be"

echo
echo "[SkyKernel] the coefficients are not copied into the shader"
# A literal Rayleigh triple in the shader is the GI-on and GI-off skies starting to drift.
! printf '%s' "$SkyCode" | grep -qE '5\.8e-6|13\.5e-6|33\.1e-6'
Report $? "the medium arrives in the block rather than being restated"

echo
echo "[SkyKernel] the C++ mirror matches the shader's std140 layout"
grep -q 'static_assert(sizeof(SkyConstantRecord) == 128u' Engine/DisplayPresentation/SkyConstantRecord.h
Report $? "the record is pinned at 128 bytes"
Offsets=$(grep -c 'static_assert(offsetof(SkyConstantRecord' Engine/DisplayPresentation/SkyConstantRecord.h)
[ "$Offsets" -ge 7 ]
Report $? "every member's offset is asserted ($Offsets of them)"

echo
if [ "$Fail" != "0" ]; then echo "[SkyKernel] FAILED"; exit 1; fi
echo "[SkyKernel] OK"
