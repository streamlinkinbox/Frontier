#!/usr/bin/env bash
# Compare the inherited and revised exhibit at identical geometry, compiler flags and cameras.
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD=_ClaudeScratch/vehiclephysics
JOLT=_ClaudeScratch/jolt
mkdir -p "$BUILD/before" "$BUILD/after" Exhibits/VehiclePhysics/Results
if [ ! -f "$JOLT/libJolt.a" ]; then
    echo 'Run RunVehiclePhysics.sh first to build the pinned Jolt dependency.' >&2
    exit 1
fi
# Compare with the restored pre-ground-change implementation (the renderer is a test tool, not a visual oracle).
git show 29c5ab194276552e56eb204b77c7fc1567b1219a:Exhibits/VehiclePhysics/Code/SceneRender.cpp > "$BUILD/SceneRenderBefore.cpp"
for VARIANT in before after; do
    DEFINES=()
    if [ "$VARIANT" = before ]; then
        DEFINES=(-DRASTER_BASELINE "-DRASTER_SOURCE=\"$PWD/$BUILD/SceneRenderBefore.cpp\"")
    fi
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -DNDEBUG -I Frontier -I "$JOLT/JoltPhysics-5.2.0" \
        "${DEFINES[@]}" Exhibits/VehiclePhysics/Code/RasterComparison.cpp \
        "$BUILD/VehicleSolver.o" "$BUILD/TyreSlipSolver.o" "$BUILD/TyreThermalSolver.o" \
        "$BUILD/TyreCarcassLattice.o" "$BUILD/TyreContinuum.o" "$JOLT/libJolt.a" -pthread -o "$BUILD/Raster-$VARIANT"
done
# Alternate ordering to expose scheduling/thermal bias. Do not run other workloads concurrently.
for VARIANT in before after after before; do
    echo "== $VARIANT =="
    "$BUILD/Raster-$VARIANT" Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Archive "$BUILD/$VARIANT"
done | tee Exhibits/VehiclePhysics/Results/RasterTiming.txt
