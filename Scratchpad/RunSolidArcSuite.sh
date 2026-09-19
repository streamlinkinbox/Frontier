#!/usr/bin/env bash
# Sandbox audit: run every verification binary from the tool's own source root (the working directory the console
# scripts, proof output and the two source-scanning suites require) and print one table row per binary.
# Usage: bash Scratchpad/RunSolidArcSuite.sh [per-binary timeout seconds]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../Editor/EditorTools/ParametricSketcher" && pwd)"
OUT=${SOLIDARC_BUILD:-/tmp/solidarc-build}
TIMEOUT="${1:-300}"
REPORT="$OUT/audit-run.txt"

cd "$ROOT"
: > "$REPORT"
Total=0; Failed=0
for B in "$OUT"/*Verification; do
    Name=$(basename "$B")
    Start=$(date +%s)
    Output=$(timeout "$TIMEOUT" "$B" 2>&1)
    Code=$?
    Elapsed=$(( $(date +%s) - Start ))
    Summary=$(echo "$Output" | grep -E "checks, .* failed" | tail -1)
    [ -n "$Summary" ] || Summary="rc=$Code (no summary)"
    [ "$Code" -eq 0 ] || Failed=$((Failed + 1))
    Total=$((Total + 1))
    printf "%-52s rc=%-3s %4ss  %s\n" "$Name" "$Code" "$Elapsed" "$Summary" | tee -a "$REPORT"
done
echo
echo "── binaries: $Total   failing: $Failed"
echo "$Failed" > "$OUT/audit-failed-count"
