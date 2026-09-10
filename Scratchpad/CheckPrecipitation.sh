#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckPrecipitation.sh — rain falls from clouds, not from the camera, and not in space
#============================================================================================================================================
# Celestial step 6. The reference demo has three behaviours that are defects rather than simplifications, all
#    reported from use, and each is guarded here as a property rather than a comment:
#
#    ① it spawns ahead of the camera, so turning around leaves the sky behind you empty,
#    ② its only altitude term is a fade, so it rains at 400 km,
#    ③ it treats cloud cover as a probability, so a clear sky still rains at 25%.
#
# And a fourth property that is ours rather than a fix: settled snow is retired into a fixed-size height field the
#    moment it lands, so accumulation costs no memory and draws as one surface instead of a million quads.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

echo "[Precipitation] the weather behaves"
Binary="$(mktemp -u /tmp/PrecipitationProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I . -I Engine -o "$Binary" Scratchpad/PrecipitationProof.cpp \
     2>/tmp/Precipitation.build; then
    echo "  PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/Precipitation.build | head -20; exit 1
fi
"$Binary" || Fail=1
rm -f "$Binary"

Header=Engine/DisplayPresentation/Precipitation.h
Code="$(sed 's;//.*;;' "$Header")"

echo
echo "[Precipitation] the emitter is world-space"
# Step must take a camera POSITION and never a view direction. A direction in this signature is defect ①
# returning, and it would not change any picture in a still frame.
printf '%s' "$Code" | grep -q 'void Step(const PrecipitationSettings& Settings, const CloudLayerSettings& Cloud,'
Report $? "Step's signature is unchanged"
! printf '%s' "$Code" | grep -qE 'Forward\[|ViewDirection|CameraYaw'
Report $? "no view direction reaches the emitter"

echo
echo "[Precipitation] the altitude gate is hard, not a fade"
printf '%s' "$Code" | grep -q 'if (Camera\[2\] > Cloud.CeilingMetres)'
Report $? "above the cloud ceiling the system returns early"
printf '%s' "$Code" | grep -q 'AboveWeather'
Report $? "and reports that state rather than silently emitting nothing"

echo
echo "[Precipitation] cloud cover is a gate, not a probability"
printf '%s' "$Code" | grep -q 'ColumnCover(Cloud, Wind, X, Y, SlabBase, SlabTop, Time) < Settings.MinimumCloudCover'
Report $? "a column below the cover threshold produces nothing"
# The demo's form was `random() > min(1, .25 + cover*1.2)`. Any random() in the spawn gate is that bug returning.
! printf '%s' "$Code" | grep -qE 'Random\(\) *[<>] *[^;]*Cover'
Report $? "cover is not compared against a random draw"

echo
echo "[Precipitation] settled snow leaves the particle pool"
printf '%s' "$Code" | grep -q 'Snow.Settle('
Report $? "a landed flake is retired into the field"
printf '%s' "$Code" | grep -q 'Particles.pop_back();'
Report $? "and its slot is freed the same tick"
grep -q 'kResolution' "$Header"
Report $? "the field is a fixed grid, so its cost does not grow"

echo
if [ "$Fail" != "0" ]; then echo "[Precipitation] FAILED"; exit 1; fi
echo "[Precipitation] OK"
