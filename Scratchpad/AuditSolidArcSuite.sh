#!/usr/bin/env bash
# Sandbox audit: compile every Verification/*.cpp against the current kernel and report OK/FAIL.
# Usage: bash Scratchpad/AuditSolidArcSuite.sh [--rebuild]
set -uo pipefail
cd "$(dirname "$0")/../Editor/EditorTools/ParametricSketcher"

OUT=/tmp/solidarc-build
LOG="$OUT/audit-compile.log"
INC="-I. -IPresentation -DSOLIDARC_PROOF_FOLDER=\"$PWD/Proofs\" -DFRONTIER_DEVELOPMENT"
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-unused-function"
OBJLIST=$(ls "$OUT"/obj/*.o | tr '\n' ' ')

mkdir -p "$OUT"
: > "$LOG"

compile_one() {
    local S="$1" NAME BIN
    NAME=$(basename "$S" .cpp)
    BIN="$OUT/$NAME"
    if [ "${REBUILD:-0}" != "1" ] && [ -x "$BIN" ] && [ "$BIN" -nt "$S" ]; then
        echo "SKIP $NAME (up to date)"; return 0
    fi
    if g++ $FLAGS $INC -DSUITEVERIFICATION_SOURCE_ROOT="\"$PWD\"" $OBJLIST "$S" -lpthread -o "$BIN" 2>>"$LOG"; then
        echo "OK   $NAME"
    else
        echo "FAIL $NAME"
    fi
}
export -f compile_one
export OUT INC FLAGS OBJLIST LOG
export REBUILD="${1:+1}"; [ "${1:-}" = "--rebuild" ] || REBUILD=0

ls Verification/*.cpp | xargs -P 2 -I{} bash -c 'compile_one {}' | sort | tee "$OUT/audit-compile.txt"
echo
echo "── failures: $(grep -c '^FAIL' "$OUT/audit-compile.txt") / $(ls Verification/*.cpp | wc -l)"
echo "── first errors:"
grep -E "error:" "$LOG" | sed 's|Verification/||' | sort -u | head -30
