#!/usr/bin/env bash
# 🧩 Build the imported Project Drive hook-based vehicle against its unchanged source checks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
BUILD="$ROOT/_ClaudeScratch/projectdrive"
REFERENCE="$ROOT/Experimental/ProjectDriveReference/Vehicle"
IMPORTED="$ROOT/Frontier/Engine/Physics/ProjectDrive"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results/ProjectDrive.txt}"
mkdir -p "$BUILD/source" "$BUILD/imported" "$BUILD/tests" "$(dirname "$RESULTS")"
while read -r HASH PATHNAME; do
    [[ "$(git hash-object "$ROOT/Experimental/ProjectDriveReference/$PATHNAME")" == "$HASH" ]] || {
        echo "Reference hash mismatch: $PATHNAME" >&2; exit 1; }
done < "$ROOT/Experimental/ProjectDriveReference/SourceManifest.txt"
FLAGS=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    FLAGS+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
else
    FLAGS+=(-O2)
fi
CXX="${CXX:-g++}"
CORE=(Aerodynamics Drivetrain PacejkaMagicFormula TyreSlipDynamics VehicleGeometry VehicleSolver)
# Besides the behaviour traces, verify all six original translation units (including their headers)
# remain identical after preprocessing. This permits the imported comment corrections, not code drift.
for UNIT in "${CORE[@]}"; do
    "$CXX" -std=c++17 -E -P -I "$REFERENCE" "$REFERENCE/$UNIT.cpp" > "$BUILD/source/$UNIT.i"
    "$CXX" -std=c++17 -E -P -I "$IMPORTED" "$IMPORTED/$UNIT.cpp" > "$BUILD/imported/$UNIT.i"
    cmp "$BUILD/source/$UNIT.i" "$BUILD/imported/$UNIT.i"
done
TESTS=(VehicleValidation VehicleDrivingChecks AbsSuperchargerChecks AerodynamicsValidation BrakingValidation SuperchargerValidation VehicleInputChecks ProjectDriveTrace)
for TEST in "${TESTS[@]}"; do
    if [[ "$TEST" == ProjectDriveTrace ]]; then cp Exhibits/VehiclePhysics/Code/ProjectDriveTrace.cpp "$BUILD/tests/$TEST.cpp"
    else cp "$REFERENCE/$TEST.cpp" "$BUILD/tests/$TEST.cpp"; fi
done
for VARIANT in source imported; do
    DIR="$REFERENCE"; [[ "$VARIANT" == imported ]] && DIR="$IMPORTED"
    OBJECTS=()
    for UNIT in "${CORE[@]}"; do
        "$CXX" "${FLAGS[@]}" -I "$DIR" -c "$DIR/$UNIT.cpp" -o "$BUILD/$VARIANT/$UNIT.o"
        OBJECTS+=("$BUILD/$VARIANT/$UNIT.o")
    done
    for TEST in "${TESTS[@]}"; do
        # Two old upstream test files contain trailing-backslash comments and/or misleading indentation.
        # Suppress only those test-source warnings; every imported/source core was compiled with -Werror.
        "$CXX" "${FLAGS[@]}" -Wno-comment -Wno-misleading-indentation -I "$DIR" \
            "$BUILD/tests/$TEST.cpp" "${OBJECTS[@]}" -o "$BUILD/$VARIANT/$TEST"
        "$BUILD/$VARIANT/$TEST" > "$BUILD/$VARIANT/$TEST.txt" 2> "$BUILD/$VARIANT/$TEST.stderr"
    done
done
# 📝 Integrate the imported solver with the separate VehicleHMI visual solvers under a mock chassis.
OBJECTS=(); for UNIT in "${CORE[@]}"; do OBJECTS+=("$BUILD/imported/$UNIT.o"); done
"$CXX" "${FLAGS[@]}" -I Frontier Exhibits/VehiclePhysics/Code/ProjectDriveVisualValidation.cpp \
    "$IMPORTED/TyreVisualBridge.cpp" Frontier/Engine/Physics/TyreContinuum.cpp "${OBJECTS[@]}" \
    -o "$BUILD/imported/ProjectDriveVisualValidation"
"$BUILD/imported/ProjectDriveVisualValidation" > "$BUILD/imported/ProjectDriveVisualValidation.txt"
{
    echo 'Project Drive source: SultanAladin/Frontier- commit 3f14808a6dcc909ae64070d62639a7019f5de794'
    echo 'All retained reference Git blob hashes verified; six imported core units preprocess identically to source.'
    echo 'Source and imported cores were built and run separately.'
    "$CXX" --version | head -1
    uname -srm
    for TEST in "${TESTS[@]}"; do
        cmp "$BUILD/source/$TEST.txt" "$BUILD/imported/$TEST.txt"
        echo "== $TEST: source and imported outputs byte-identical =="
        if [[ "$TEST" == ProjectDriveTrace ]]; then cat "$BUILD/imported/$TEST.stderr"
        else tail -4 "$BUILD/imported/$TEST.txt"; fi
    done
    cat "$BUILD/imported/ProjectDriveVisualValidation.txt"
    echo 'Scope: upstream equation/invariant tests and mock-chassis simulations, not measured tyre or Jolt validation.'
    echo 'Unchanged upstream VehicleSceneValidation is stale: its WheelMount initializer no longer compiles.'
} | tee "$RESULTS"
