#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckVolumetricMedia.sh — one march, a cloud ceiling, and markers for bodiless volumes
#============================================================================================================================================
# Celestial step 5. Three classes of regression are guarded here.
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
# One March entry point. A second per-medium march is the regression 73737b6 removed.
Marches=$(grep -c 'static VolumetricSample March' "$Header")
[ "$Marches" = "1" ]
Report $? "exactly one March entry point ($Marches found)"
grep -q 'ONE loop over the union' "$Header"
Report $? "the union march is documented as the contract"

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
# A fixed step COUNT means the step SIZE grows with the union interval, so enabling a distant volume silently
#    coarsens a near one. Measured: adding fog raised transmittance from 0.2954 to 0.2963 — more medium, more
#    light through, which is impossible.
grep -q 'kReferenceSpan' "$Header"
Report $? "the march derives its count from a bounded step size"

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
echo
echo "[VolumetricMedia] god rays ride the march, not a screen-space pass"
# A crepuscular shaft is the sun-visibility term the march already computes, evaluated against scene occlusion.
#    A separate full-screen radial blur would be cheaper and would fail with the sun off-screen, which is exactly
#    the shot shafts are wanted for.
printf '%s' "$MediaCode" | grep -q 'SunVisibilityAt SceneVisibility'
Report $? "the march takes a scene-occlusion source"
printf '%s' "$MediaCode" | grep -q 'SunTransmittance \*= Visible'
Report $? "occlusion multiplies the existing sun term rather than adding a pass"
# The budget must subdivide the shaft term, not merely switch it on: a shadow edge is far sharper than the
#    medium that carries it, and one sample per march step turns every beam edge into a step boundary.
printf '%s' "$MediaCode" | grep -q 'Budget.GodRaySamples > 8u ? 8u : Budget.GodRaySamples'
Report $? "GodRaySamples sets how finely the shaft is sampled across a step"
! printf '%s' "$MediaCode" | grep -qiE 'radial.?blur|screenspace shaft'
Report $? "no screen-space radial blur"

! printf '%s' "$MediaCode" | grep -qiE 'clear.?air strid|temporal reproject|reprojectionbuffer'
Report $? "no clear-air striding or cloud temporal reprojection in the code"

echo
if [ "$Fail" != "0" ]; then echo "[VolumetricMedia] FAILED"; exit 1; fi
echo "[VolumetricMedia] OK"
