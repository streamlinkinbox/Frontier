#!/usr/bin/env bash
# 📝 Verify the retained temporal equation and an independent area-measure counterexample.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MODE=release
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
BUILD="$ROOT/_ClaudeScratch/build/temporal-reconnection-$MODE"
mkdir -p "$BUILD" Exhibits/LightCompute/Results
TEMP=Experimental/VehicleHmiReference/Shader/ReservoirTemporal.wgsl
SPATIAL=Experimental/VehicleHmiReference/Shader/ReservoirSpatial.wgsl
grep -Fq 'MergeReservoir(&Current, Reused, Target * Prior.Weight * Tally, Tally' "$TEMP"
grep -Fq 'ResolveReconnectionJacobian' "$SPATIAL"
"${CXX:-g++}" "${FLAGS[@]}" Exhibits/LightCompute/Code/TemporalReconnectionAudit.cpp -o "$BUILD/audit"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/TemporalReconnection-$MODE.txt}"
{
    sha256sum "$TEMP" "$SPATIAL" Exhibits/LightCompute/Code/TemporalReconnectionAudit.cpp
    "$BUILD/audit"
} | tee "$RESULTS"
