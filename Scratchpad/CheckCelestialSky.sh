#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckCelestialSky.sh — the solver agrees with the almanac, and the sky reaches the GI-off raster
#============================================================================================================================================
# Celestial port, step 1. Two proofs, both headless — no Vulkan, no GLFW, no window:
#
#   ① CelestialSolverProof — the sun against independently computed NOAA values. Not a re-run of the same code:
#      the expected numbers were produced separately, which is the only arrangement that can catch a wrong model.
#      It also pins the seasons, because the reference demo's sun carries no date and reports the equinox
#      elevation on every day of the year (a 47 deg error at the solstice for this latitude).
#
#   ② CelestialSkyProof — the atmosphere through the REAL VisibilityRaster, four times of day, sheets committed
#      to Diagnostics/ and the numbers asserted.
#
# Dependency roots follow CheckShadowTiers so the two gates stay runnable in the same environment.
set -u
cd "$(dirname "$0")/.."
Fail=0
Cg="${CGLTF:-/tmp/cg}"; Ufbx="${UFBX:-/tmp/ufbxsrc}"; Stb="${STB:-/tmp/stbsrc}"
Bvh="${TINYBVH:-/tmp/tinybvh}"; Vkh="${VKH:-/tmp/vkh/include}"
[ -d "$Vkh" ] || Vkh="/tmp/sws/include"

echo "[CelestialSky] the solver agrees with the almanac"
Solver="$(mktemp -u /tmp/CelestialSolverProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Solver" \
     Scratchpad/CelestialSolverProof.cpp Engine/DisplayPresentation/CelestialSolver.cpp \
     2>/tmp/CelestialSolver.build; then
    echo "  SOLVER PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/CelestialSolver.build | head -20; exit 1
fi
"$Solver" || Fail=1
rm -f "$Solver"

echo "[CelestialSky] the sky reaches the GI-off raster"
Sky="$(mktemp -u /tmp/CelestialSkyProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -msse4.2 -I . -I Engine -I Scratchpad \
     -I "$Cg" -I "$Ufbx" -I "$Stb" -I "$Bvh" -I "$Vkh" -o "$Sky" \
     Scratchpad/CelestialSkyProof.cpp \
     Projects/Project-Zero/Source/CelestialSequence.cpp \
     Engine/GeometricRaster/VisibilityRaster.cpp \
     Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/GeometricRaster/TraversalIndex.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp \
     Engine/ContentInterchange/SceneCodec.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     2>/tmp/CelestialSky.build; then
    echo "  SKY PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/CelestialSky.build | head -20; exit 1
fi
"$Sky" || Fail=1
rm -f "$Sky"

echo "[CelestialSky] the sky stays off unless asked"
# Every existing proof in the tree was written against the flat fallback. The celestial path must be opt-in, or
#    this step silently rewrites the expected output of gates it has nothing to do with.
grep -q 'bool             Enabled          = false;' Engine/GeometricRaster/VisibilityRaster.h
if [ $? -eq 0 ]; then printf '  %-64s PASS\n' "CelestialSettings defaults to disabled"
else printf '  %-64s FAIL\n' "CelestialSettings defaults to disabled"; Fail=1; fi

# The model must live in the shared header, not be re-derived per call site.
Stray=$(grep -rln --include=*.cpp -e '5\.8e-6' -e '13\.5e-6' Engine/ | grep -v AtmosphereModel.h || true)
if [ -z "$Stray" ]; then printf '  %-64s PASS\n' "the Rayleigh coefficients live in one place"
else printf '  %-64s FAIL\n' "a second copy of the medium: $Stray"; Fail=1; fi

echo
if [ "$Fail" != "0" ]; then echo "[CelestialSky] FAILED"; exit 1; fi
echo "[CelestialSky] OK"
