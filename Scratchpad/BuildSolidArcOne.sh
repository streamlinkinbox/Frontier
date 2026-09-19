#!/usr/bin/env bash
# One compile or link job for BuildSolidArcParallel.sh.  usage: BuildSolidArcOne.sh <obj|verif> <source>
#    env: PS (sketcher folder), OUT (output folder), OBJLIST (object list, link jobs only)
set -euo pipefail
MODE="$1"; S="$2"
cd "$PS"
FLAGS=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-unused-function)
INC=(-I. -IPresentation "-DSOLIDARC_PROOF_FOLDER=\"$PS/Proofs\"" -DFRONTIER_DEVELOPMENT)
if [ "$MODE" = obj ]; then
    O="$OUT/obj/$(echo "$S" | tr / _).o"
    if [ ! -f "$O" ] || [ "$S" -nt "$O" ] || [ -n "$(find Kernel Presentation Interaction Document Console -name '*.h' -newer "$O" | head -1)" ]; then
        echo "  cc $S"; g++ "${FLAGS[@]}" "${INC[@]}" -c "$S" -o "$O"
    fi
else
    NAME=$(basename "$S" .cpp)
    if [ ! -f "$OUT/$NAME" ] || [ "$S" -nt "$OUT/$NAME" ] || [ "$OUT/SolidArc" -nt "$OUT/$NAME" ] || [ Verification/VerificationPanel.h -nt "$OUT/$NAME" ]; then
        echo "  ld $NAME"; g++ "${FLAGS[@]}" "${INC[@]}" "-DSUITEVERIFICATION_SOURCE_ROOT=\"$PS\"" $OBJLIST "$S" -lpthread -o "$OUT/$NAME"
    fi
fi
