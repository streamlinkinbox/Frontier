#!/usr/bin/env bash
# Run every SolidArc verification binary built by BuildSolidArcParallel.sh; one summary line per suite, logs in $LOG.
#    usage: bash Scratchpad/RunSolidArcVerification.sh [NamePattern]
HERE="$(cd "$(dirname "$0")" && pwd)"
export PS="$(cd "$HERE/../Editor/EditorTools/ParametricSketcher" && pwd)"
export OUT=${OUT:-/tmp/solidarc-build}
export LOG=${LOG:-/tmp/solidarc-verification}
PATTERN=${1:-}
mkdir -p "$LOG"
cd "$PS"
run_one() {
    B="$1"; N=$(basename "$B")
    START=$(date +%s)
    timeout 1200 "$B" > "$LOG/$N.log" 2>&1; RC=$?
    END=$(date +%s)
    SUMMARY=$(grep -E "checks, [0-9]+ failed|all checks passed" "$LOG/$N.log" | tail -1 | sed 's/^ *//')
    printf "%-58s rc=%-3s %4ss  %s\n" "$N" "$RC" "$((END - START))" "$SUMMARY"
}
export -f run_one; export LOG PS
ls "$OUT"/*Verification | grep -e "${PATTERN}" | xargs -P "${JOBS:-2}" -I{} bash -c 'run_one {}'
