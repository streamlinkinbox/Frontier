#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckVolumetricMedia.sh — one march, a cloud ceiling, and markers for bodiless volumes
#============================================================================================================================================
# Celestial step 5. Four classes of regression are guarded here.
#
#  ① THE UNIFIED MARCH. The source branch consolidated the volumes into one loop at 73737b6 — shared extinction,
#    one sun-shadow march, one light loop — so fog shadows cloud for free. Splitting them apart looks almost
#    identical in a still frame and costs double, so the structure is asserted rather than reviewed.
#
#  ② THE CEILING. Clouds are tropospheric. Without a clamp a Base of 200 km renders a cloud shell OUTSIDE a 60 km
#    atmosphere, visible from orbit as a band floating in vacuum. The failure is silent: the render succeeds.
#
#  ③ THE PROHIBITED OPTIMISATIONS, each measured and reverted upstream: clear-air striding (73b71d6, speckled
#    cloud), a low-res cloud FBO with temporal reprojection (a152901, slower and worse), and atmosphere LUTs
#    (2fe78ed, no speedup and uglier — References/Deferred/AtmosphereLuts.md).
#
#  ④ THE DRIFT. Advecting each altitude by its own wind over time-of-day shredded the slab into horizontal
#    streaks (measured: 76 km of shear offset across 1.1 km by 7am). The whole medium rides one reference
#    flow plus a frozen shear offset — WindField::AdvectDrift, which both densities must call.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[VolumetricMedia] the media behave"
Binary="$(mktemp -u /tmp/VolumetricMediaProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" Scratchpad/VolumetricMediaProof.cpp Engine/DisplayPresentation/FidelityClassifier.cpp \
     2>/tmp/VolumetricMedia.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/VolumetricMedia.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

Header=Engine/DisplayPresentation/VolumetricMedia.h

echo
echo "[VolumetricMedia] the march is shared, not duplicated"
# One March entry point. A second March is the regression 73737b6 removed (a duplicated entry point); the
#    per-medium loops inside the one entry are the structure — each medium marches its own span at its own
#    pace, and the sun-shadow march stays shared (one per occupied step, whichever loop it sits in).
Marches=$(grep -c 'static VolumetricSample March' "$Header")
[ "$Marches" = "1" ]
Report $? "exactly one March entry point ($Marches found)"
grep -q 'THREE marches, one per medium' "$Header"
Report $? "the per-medium march is documented as the contract"

echo
echo "[VolumetricMedia] the ceiling is enforced in code, not in a comment"
grep -q 'CeilingMetres' "$Header"
Report $? "the cloud layer carries an explicit ceiling"
# SlabExtent must clamp, and CloudDensity must consult it — either alone leaves the hole open.
grep -q 'Clamp(Cloud.Base, 0.0f, Ceiling)' "$Header"
Report $? "SlabExtent clamps the base to the ceiling"
grep -q 'if (!SlabExtent(Cloud, Base, Top)) return 0.0f;' "$Header"
Report $? "CloudDensity refuses to sample outside the slab"

echo
echo "[VolumetricMedia] the step size is bounded, not the step count"
# A fixed step COUNT means the step SIZE grows with the span, so a long interval silently coarsens the
#    sampling. Measured under the old union march: adding fog raised transmittance from 0.2954 to 0.2963 —
#    more medium, more light through, which is impossible; separate marches make that structural failure
#    impossible, and each medium still derives its count from a bounded step.
grep -q 'kReferenceSpan' "$Header"
Report $? "the march derives its count from a bounded step size"

echo
echo "[VolumetricMedia] advection is uniform plus frozen shear, never local flow times time"
# The streak note lives in WindField::AdvectDrift: the local flow times time-of-day piled 76 km of offset
#    across the slab by 7am and shredded the sampling grid into horizontal streaks. Both densities route
#    through the one helper; the old inline form (flow times the clock at the art factor) must not return.
DriftDefs=$(grep -c 'static void AdvectDrift' Engine/DisplayPresentation/WindField.h)
[ "$DriftDefs" = "1" ]
Report $? "exactly one AdvectDrift helper ($DriftDefs found)"
DriftCalls=$(grep -c 'AdvectDrift(' "$Header")
[ "$DriftCalls" = "2" ]
Report $? "both densities route through it ($DriftCalls call sites)"
grep -q '0.5f \* (Base + Top)' "$Header"
Report $? "the layer advects by the slab-mid flow"
grep -q 'Volume.Centre\[2\]' "$Header"
Report $? "the box advects by the flow at its centre"
! sed 's;//.*;;' "$Header" | grep -qE 'Time \* 0\.[86]f'
Report $? "no inline flow-times-time drift in either density"

echo
echo "[VolumetricMedia] bodiless volumes have a marker to grab"
Marker=Engine/SpatialInterface/VolumeMarker.h
[ -f "$Marker" ]
Report $? "VolumeMarker.h exists"
grep -q 'kMarkerRadiusPixels' "$Marker"
Report $? "the marker holds a constant screen size, not a world size"
grep -q 'GlyphPath' "$Marker"
Report $? "the marker carries SVG glyphs for its categories"

echo
echo "[VolumetricMedia] the prohibited optimisations have not returned"
# ⚠️ Comments are stripped first. The header necessarily NAMES these prohibitions to explain them, and the first
#    version of this check matched its own documentation and failed forever — the same trap CheckShadowTiers
#    records for the PCSS half-angle. Check the code, not the prose.
MediaCode="$(sed 's;//.*;;' "$Header")"
! printf '%s' "$MediaCode" | grep -qiE 'clear.?air strid|temporal reproject|reprojectionbuffer'
Report $? "no clear-air striding or cloud temporal reprojection in the code"

echo
echo "[VolumetricMedia] cloud shafts come from the medium shadowing itself"
# The scene-occlusion callback was removed - nothing in the engine called it and no such geometry exists yet.
#    What stays is the half that renders: ShadowMarch accumulates cloud density along the sun ray, which is what
#    lights a cloud at all. Deleting THAT would leave clouds flat, so it is guarded here.
printf '%s' "$MediaCode" | grep -q 'Cloud.Enabled ? CloudDensity(Cloud, Wind, Q, Time) : 0.0f'
Report $? "the sun-shadow march samples cloud density along the sun ray"
! printf '%s' "$MediaCode" | grep -q 'SunVisibilityAt'
Report $? "the unused scene-occlusion callback is gone"
! printf '%s' "$MediaCode" | grep -qiE 'radial.?blur|screenspace shaft'
Report $? "no screen-space radial blur"

echo
if [ "$Fail" != "0" ]; then echo "[VolumetricMedia] FAILED"; exit 1; fi
echo "[VolumetricMedia] OK"
