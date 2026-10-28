#!/usr/bin/env bash
# 🧩 Validate the static indirect reservoir and audit the retained demo's spatial estimator.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CXX="${CXX:-g++}"
CMAKE="${CMAKE:-cmake}"
MODE="release"
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror)
CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE="sanitized"
    FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
                 '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
BUILD="$ROOT/_ClaudeScratch/build/static-indirect-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/StaticIndirect-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
[[ "$(git hash-object Experimental/VehicleHmiReference/Shader/ReservoirRecords.wgsl)" == a999b1cff7a48f6a34594dd611ce501bda424c7a ]]
[[ "$(git hash-object Experimental/VehicleHmiReference/Shader/ReservoirTemporal.wgsl)" == a1d4f4eb272f38690ebde1f79d90d59cece6ede9 ]]
[[ "$(git hash-object Experimental/VehicleHmiReference/Shader/ReservoirSpatial.wgsl)" == be6c589140c8fb9bc8ef699c5bb8abdd9284a3e6 ]]
"$CXX" "${FLAGS[@]}" Exhibits/LightCompute/Code/PathSpatialAudit.cpp -o "$BUILD/PathSpatialAudit"
"$CXX" "${FLAGS[@]}" -I Frontier -c Exhibits/LightCompute/Code/StaticIndirectValidation.cpp -o "$BUILD/StaticIndirectValidation-strict.o"
"$CXX" "${FLAGS[@]}" -I Frontier -c Exhibits/LightCompute/Code/DirectSpatialValidation.cpp -o "$BUILD/DirectSpatialValidation-strict.o"
"$CXX" "${FLAGS[@]}" -I Frontier -c Frontier/Engine/Rendering/ReferenceRenderer.cpp -o "$BUILD/ReferenceRenderer-strict.o"
"$CMAKE" -S Exhibits/VehicleReference -B "$BUILD/reference" -DCMAKE_CXX_COMPILER="$CXX" "${CMAKE_FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD/reference" --target StaticIndirectValidation DirectSpatialValidation -j2 >/dev/null
if [[ "${BASELINE:-0}" == 1 ]]; then
    # Characterize the pinned pre-correction renderer with the same harness and dependencies.
    git show 07ab116:Frontier/Engine/Rendering/ReferenceRenderer.cpp > "$BUILD/ReferenceRenderer-before.cpp"
    "$CXX" "${FLAGS[@]}" -I Frontier -I Frontier/Engine/Rendering -c "$BUILD/ReferenceRenderer-before.cpp" -o "$BUILD/ReferenceRenderer-before.o"
    mapfile -t OBJECTS < <(find "$BUILD/reference/CMakeFiles/DirectSpatialValidation.dir" -name '*.o' \
        ! -name 'ReferenceRenderer.cpp.o' ! -name 'DirectSpatialValidation.cpp.o' | sort)
    "$CXX" "${FLAGS[@]}" -I Frontier Exhibits/LightCompute/Code/DirectSpatialValidation.cpp \
        "$BUILD/ReferenceRenderer-before.o" "${OBJECTS[@]}" -pthread -o "$BUILD/DirectSpatial-before"
    BEFORE_HASH="$("$BUILD/DirectSpatial-before" --default-probe)"
    AFTER_HASH="$("$BUILD/reference/DirectSpatialValidation" --default-probe)"
    [[ "$BEFORE_HASH" == "$AFTER_HASH" ]] || { echo 'Default-off indirect probe changed' >&2; exit 1; }
    {
        echo 'Pre-correction CPU reference renderer source: 07ab116; same validation harness, --observe.'
        echo 'The historical renderer is characterized, not asserted to pass.'
        echo "PASS identical binary64-pixel hash with spatial reuse off and default-off indirect: $AFTER_HASH"
        "$BUILD/DirectSpatial-before" --observe
    } | tee "$ROOT/Exhibits/LightCompute/Results/DirectSpatial-before.txt"
fi
{
    echo "Static SDF-emitter temporal reservoir, $MODE; retained demo GI shader hashes verified."
    echo "Native base $(git rev-parse --short HEAD), plus working changes if any; see committed source for exact test."
    "$CXX" --version | head -1
    uname -srm
    "$BUILD/PathSpatialAudit"
    "$BUILD/reference/DirectSpatialValidation"
    "$BUILD/reference/StaticIndirectValidation"
    echo 'Scope: CPU static direct-light and one-bounce SDF-emitter subsets only; no WGSL/GPU execution or full ReSTIR GI parity.'
} | tee "$RESULTS"
