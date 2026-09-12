#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckSunDirect.sh — the sun lights the kernel, and the estimator still converges
#============================================================================================================================================
# The sun joined the direct-light set as a first-class reservoir candidate, and the estimator was corrected
#    around it (area pdfs, emitter cosine, the bounce's inverted slot count). Five ways this rots, all guarded:
#    ① the estimator drifts from the math the Monte Carlo proof pins (a weight loses its pdf),
#    ② a new re-evaluation site calls PHatFull raw and forgets the sun branch,
#    ③ the sentinel leaks somewhere only mesh indices may go (a LightEmission on 0xFFFFFFFF reads garbage),
#    ④ the shared sun body forks (the disc and the NEE cone disagree on the radius),
#    ⑤ the panel gain or the Direct slider stops reaching the record.
#
# SPIR-V is deliberately NOT recompiled here: CheckShaderCompile proves the file lowers. This gate pins the math.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[SunDirect] the estimator converges to quadrature, twice seeded"
Proof="$(mktemp -u /tmp/SunDirectProof.XXXXXX)"
if ! g++ -std=c++17 -O2 -Wall -Wextra -o "$Proof" Scratchpad/SunDirectProof.cpp 2>/tmp/SunDirect.build; then
    echo "  ESTIMATOR PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/SunDirect.build | head -12; exit 1
fi
"$Proof" > /tmp/SunDirect.run 2>&1
ProofExit=$?
grep -c ' PASS ' /tmp/SunDirect.run | sed 's/^/    estimator checks passed: /'
[ "$ProofExit" = "0" ]
Report $? "all estimator checks pass (samplers, quadrature, reservoirs, bounce)"
rm -f "$Proof"

Kernel=Engine/Shaders/ReSTIRViewport.slang
Sky=Engine/Shaders/SkyRecords.slang
Code="$(sed 's;//.*;;' "$Kernel")"
SkyCode="$(sed 's;//.*;;' "$Sky")"

echo
echo "[SunDirect] the kernel divides by the pdfs the proof pins"
printf '%s' "$Code" | grep -q 'weight = pHat \* SunSolidAngle() / pPick;'
Report $? "a sun candidate pays p̂·Ω/p (solid-angle pdf included)"
printf '%s' "$Code" | grep -q 'weight = pHat \* Luminaires\[li\]\.Area / (pSource \* pPick);'
Report $? "a mesh candidate pays p̂·A/(p·coin) (area pdf included)"
! printf '%s' "$Code" | grep -q 'float w    = pHat / pSource;'
Report $? "the old pick-only weight is gone"
printf '%s' "$Code" | grep -q 'cosT \* cosL / (dist2 + 0.001)'
Report $? "the mesh target carries the emitter cosine"
printf '%s' "$Code" | grep -q 'return max(dot(EvaluateBsdf(m, L, wo, wi), sunEmit), 0.0) \* cosT \* CloudSunTransmittance(hitPos, sunDir);'
Report $? "the sun target divides by no d² (infinity has none) and carries cloud visibility"
printf '%s' "$Code" | grep -q 'SunEmission() \* bCos \* SunSolidAngle()'
Report $? "the bounce sun sample pays p̂·Ω/p"
printf '%s' "$Code" | grep -q 'CloudSunTransmittance(bHitPos, bSunDir) / pPick;'
Report $? "the bounced sun sample carries cloud visibility"
printf '%s' "$Code" | grep -q 'Luminaires\[bLi\]\.Area \* float(bSlots) / ((bDist2 + 0.01) \* pMesh)'
Report $? "the bounce mesh sample multiplies by slots·area (pdf, not count)"
! printf '%s' "$Code" | grep -q '(bDist2 + 0.01) \* float(bSlots))'
Report $? "the old divide-by-slots bounce line is gone"
printf '%s' "$Code" | grep -q 'return SkySunDirect.xyz / SunSolidAngle();'
Report $? "the effective disc radiance is Q/Ω, one definition"
printf '%s' "$Code" | grep -q 'float s = sin(kSunAngularRadius \* 0.5);'
Report $? "Ω uses the half-angle form (no 1−cos cancellation)"

echo
echo "[SunDirect] every re-evaluation answers for both light kinds"
Draws=$(printf '%s' "$Code" | grep -c 'DrawDirectCandidate(')
[ "$Draws" = "3" ]
Report $? "one candidate drawer, two call sites (definition + initial + extras)"
Selected=$(printf '%s' "$Code" | grep -c 'PHatSelected(')
[ "$Selected" = "4" ]
Report $? "one stored-sample target, three merges (definition + shade-W + temporal + spatial)"
RawFull=$(printf '%s' "$Code" | grep -c 'PHatFull(')
[ "$RawFull" = "3" ]
Report $? "raw PHatFull survives only where the kind is known mesh ($RawFull sites)"
Emits=$(printf '%s' "$Code" | grep -c 'LightEmission(')
[ "$Emits" = "5" ]
Report $? "LightEmission is read only behind mesh-proven branches ($Emits sites)"
printf '%s' "$Code" | grep -q 'const uint  kSunLightIndex      = 0xFFFFFFFFu;'
Report $? "the sun sentinel is the uninhabitable index"
SentinelReads=$(printf '%s' "$Code" | grep -c '== kSunLightIndex')
[ "$SentinelReads" = "2" ]
Report $? "the sentinel is tested exactly where kinds diverge (target + shade)"

echo
echo "[SunDirect] the sentinel cannot leak into a mesh read"
! grep -q 'ReservoirBufferRecord' Engine/DeviceExchange/SwapchainExchange.h
Report $? "the reservoir row is invisible outside the exchange (.h)"
grep -q 'struct ReservoirBufferRecord' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the reservoir row is declared where it is allocated (.cpp)"
! grep -q 'Reservoir.*\.\(Counts\|Sample\|UvDepth\|Normal\)' Engine/DeviceExchange/SwapchainExchange.cpp
Report $? "the host sizes the reservoirs but never reads a field"

echo
echo "[SunDirect] the disc and the cone are one body"
RadiusDefs=$(printf '%s' "$SkyCode" | grep -c 'kSunAngularRadius =')
[ "$RadiusDefs" = "1" ]
Report $? "the angular radius is defined once ($RadiusDefs definition)"
printf '%s' "$SkyCode" | grep -q 'kSunRadius = kSunAngularRadius;'
Report $? "the disc draws the shared body"
printf '%s' "$Code" | grep -q 'cos(kSunAngularRadius)'
Report $? "the cone sampler draws the shared body"
printf '%s' "$SkyCode" | grep -q 'vec4 SkySunDirect;'
Report $? "the direct row rides the sky block"

echo
echo "[SunDirect] the panel's gain and the slider reach the record"
grep -q 'kPanelDirectSunGain = 0.11f' Engine/DisplayPresentation/SkyConstantRecord.h
Report $? "the packer carries the panel's 0.11 direct-sun gain"
grep -q 'CelestialPanel.html:1162' Engine/DisplayPresentation/SkyConstantRecord.h
Report $? "the gain cites the panel line it was transcribed from"
grep -q 'float SunDirectGain = 1.0f' Engine/DisplayPresentation/SkyConstantRecord.h
Report $? "the gain slider defaults to one (old call sites unchanged)"
grep -q 'float SunDirect ' Projects/Project-Zero/Source/CelestialSequence.h
Report $? "the sequence owns the Direct gain"
grep -q 'MakeSlider("Direct", 0.0f, 5.0f, SunDirect' Projects/Project-Zero/Source/CelestialSequence.cpp
Report $? "the Sun row offers the Direct slider"
grep -q 'ReadSlider(Sheet, "Direct", SunDirect)' Projects/Project-Zero/Source/CelestialSequence.cpp
Report $? "the sheet writes the Direct slider back"
grep -q 'LightSamples, Enabled, SunDirect)' Projects/Project-Zero/Source/CelestialSequence.cpp
Report $? "the pack hands the gain to the record"

echo
echo "[SunDirect] the sun switch is the record, not a flag"
SunGates=$(printf '%s' "$Code" | grep -c 'sunUp || LightTriangleCount > 0u')
[ "$SunGates" = "2" ]
Report $? "both DI gates run on either light kind (reservoir + bounce)"
printf '%s' "$Code" | grep -q 'bool sunUp = dot(SkySunDirect.xyz, vec3(1.0)) > 0.0;'
Report $? "night, hidden sun and disabled sky take the lamps-only path"
printf '%s' "$Code" | grep -q 'const float kSunPickProbability = 0.5;'
Report $? "the sun/lamp coin is a pinned one half"

echo
if [ "$Fail" != "0" ]; then echo "[SunDirect] FAILED"; exit 1; fi
echo "[SunDirect] OK"
