#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckAtmosphericOptics.sh — the rainbow is derived, not drawn
#============================================================================================================================================
# Celestial step 7. A hand-tuned arc at "about 42 degrees" gets the position roughly right and three other things
#    wrong: the secondary bow's offset, its REVERSED colour order, and the darker sky between the two. All three
#    fall out of the Descartes geometry for free, so the gate checks that the geometry is still what produces
#    them — a later "simplification" to a fixed angle and a gradient would pass a screenshot and fail this.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[AtmosphericOptics] the optics behave"
Binary="$(mktemp -u /tmp/AtmosphericOpticsProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" Scratchpad/AtmosphericOpticsProof.cpp \
     2>/tmp/AtmosphericOptics.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/AtmosphericOptics.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

Header=Engine/DisplayPresentation/AtmosphericOptics.h
Code="$(sed 's;//.*;;' "$Header")"

echo
echo "[AtmosphericOptics] the angle is computed, not tabulated"
# The dispersion relation and the deviation are what make the bow physical. A literal 42 in the code, with these
#    gone, is the hand-drawn arc coming back.
printf '%s' "$Code" | grep -q '1.3245f + 3000.0f'
Report $? "Cauchy dispersion is present"
printf '%s' "$Code" | grep -q 'Deviation = 2.0f \* I - 2.0f \* (K + 1.0f) \* R'
Report $? "the Descartes deviation is computed per wavelength"
! printf '%s' "$Code" | grep -qE '= *42\.[0-9]*f|Angle *= *0\.74'
Report $? "no hardcoded 42 degrees"

echo
echo "[AtmosphericOptics] aerial perspective shares the sky's medium"
printf '%s' "$Code" | grep -q 'const AtmosphereMedium& Medium'
Report $? "it takes the same AtmosphereMedium the sky uses"
# A second copy of the Rayleigh triple here would drift from the sky's haze.
! printf '%s' "$Code" | grep -qE '5\.8e-6|13\.5e-6'
Report $? "no second copy of the scattering coefficients"

echo
echo "[AtmosphericOptics] all four lens types exist and differ"
# Four optical assemblies, not one effect with an intensity slider: anamorphic turned up is not starburst.
for Type in Cinematic Anamorphic Starburst Halo; do
    printf '%s' "$Code" | grep -q "$Type"
    Report $? "the $Type lens type is defined"
done
printf '%s' "$Code" | grep -q 'static LensFlareMix MixFor'
Report $? "the component mix per type is a table, not scattered branches"
printf '%s' "$Code" | grep -q 'ApertureBlades'
Report $? "the starburst's spoke count comes from the aperture blades"

echo
echo "[AtmosphericOptics] the bow is in the world, the flare is in the lens"
# These two look alike and behave oppositely. The rainbow must take a distance so geometry can hide it; the
#    flare must not, because it is scattered on the sensor after the light entered and correctly lies over
#    everything. Guarded so a later tidy-up does not make them consistent with each other.
printf '%s' "$Code" | grep -q 'float RainVisibility, float RainDistanceMetres'
Report $? "the rainbow takes the ray's distance, so nearby geometry suppresses it"
printf '%s' "$Code" | grep -q 'float SunVisibility, float Aspect'
Report $? "the flare takes sun visibility, so an occluded sun kills it"
grep -q 'do not "fix" this to match the rainbow' "$Header"
Report $? "the difference between the two is recorded where it would be undone"

echo
if [ "$Fail" != "0" ]; then echo "[AtmosphericOptics] FAILED"; exit 1; fi
echo "[AtmosphericOptics] OK"
