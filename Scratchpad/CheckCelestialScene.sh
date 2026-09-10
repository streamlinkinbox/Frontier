#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckCelestialScene.sh — the Celestial port is wired into Project Zero, not merely present
#============================================================================================================================================
# Each celestial system was proved on its own, and every one of those proofs would still pass if nothing called
#    them. This guards the assembly: one world, in the outliner, editable, ticking, and reaching the renderer.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }
Vk="${VKH:-/tmp/vkhsrc/include}"; [ -d "$Vk" ] || Vk="/tmp/sws/include"

echo "[CelestialScene] the sky is in the scene"
Binary="$(mktemp -u /tmp/CelestialSceneProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -I "$Vk" -o "$Binary" \
     Scratchpad/CelestialSceneProof.cpp \
     Projects/Project-Zero/Source/CelestialSequence.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp \
     Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp 2>/tmp/CelestialScene.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/CelestialScene.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

Game=Projects/Project-Zero/Source/GameExecution.cpp
Code="$(sed 's;//.*;;' "$Game")"

echo
echo "[CelestialScene] the project actually calls it"
# Comments stripped first: the file explains this wiring at length, and a check that matches its own prose
#    passes forever. The pattern is the CALL.
for Call in "Celestial.Prepare()" "Celestial.Tick(" "Celestial.AppendRoster(" "Celestial.BuildSheet(" "Celestial.ApplySheet("; do
    printf '%s' "$Code" | grep -qF "$Call"
    Report $? "GameExecution calls $Call"
done

echo
echo "[CelestialScene] the tier reaches it through the one translation"
printf '%s' "$Code" | grep -q 'CelestialTier::BudgetFor(Criteria)'
Report $? "the budget comes from CelestialTier, not a hand-built struct"
# The budget must be consumed, not assigned and forgotten - that is how a wire looks connected and is not.
printf '%s' "$Code" | grep -q 'Celestial.Budget = '
Report $? "and lands on the sequence where the sheets can report it"

echo
echo "[CelestialScene] the outliner's eye toggles do something"
printf '%s' "$Code" | grep -q 'Celestial.Shown\[E\] = SceneInstances\[Row\].Visible'
Report $? "row visibility carries back into the world"

echo
echo "[CelestialScene] stars know it is daytime"
# A star is only visible when it outshines the sky behind it. Without this the field draws over a noon sky.
grep -q 'kStarVisibilityCeiling' Engine/GeometricRaster/VisibilityRaster.cpp
Report $? "the star pass is gated on sky luminance"

echo
if [ "$Fail" != "0" ]; then echo "[CelestialScene] FAILED"; exit 1; fi
echo "[CelestialScene] OK"
