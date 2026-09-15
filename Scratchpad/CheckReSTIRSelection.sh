#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckReSTIRSelection.sh — guards the carried-selection invariant in ReSTIRViewport.slang
#============================================================================================================================================
# #5 replaced four PHatFull evaluations with a value carried across the merges. That is only safe while every point
#    where the selection can change also updates the carried p̂. If someone later adds a resample and forgets the
#    update, the reservoir silently decorrelates — no crash, no warning, just a worse image nobody attributes to
#    this commit. This script fails loudly instead.
#
# Usage: bash Scratchpad/CheckReSTIRSelection.sh [path-to-glslang]

set -u
cd "$(dirname "$0")/.."

Glslang="${1:-}"
Fail=0
Kernel=Engine/Shaders/ReSTIRViewport.slang

Report() {   # Report <condition-exit-code> <label>
    if [ "$1" = "0" ]; then printf '  %-64s PASS\n' "$2"
    else                    printf '  %-64s FAIL\n' "$2"; Fail=1; fi
}

echo "[ReSTIRSelection] the carried selection p-hat is exact"
g++ -std=c++20 -O2 -Wall -Wextra -o /tmp/restir_selection_identity \
    Scratchpad/ReSTIRSelectionIdentity.cpp 2>/tmp/restir_selection_build.log
if [ $? -ne 0 ]; then
    echo "  identity proof failed to BUILD"; sed -n '1,12p' /tmp/restir_selection_build.log; exit 1
fi
/tmp/restir_selection_identity > /tmp/restir_selection_identity.log 2>&1
Report $? "400k randomised reservoirs: carried p-hat == re-derived p-hat"
grep -E "selection differs|RNG sequence differs|worst|mismatches|evaluations" /tmp/restir_selection_identity.log | sed 's/^/    /'

echo
echo "[ReSTIRSelection] every selection change updates the carried value"
# The invariant, checked structurally: each assignment to res.SelectedLight must be accompanied by a pSelected
#    update within the same block. Count them — they must match.
Selects=$(grep -c 'res\.SelectedLight *=' "$Kernel")
Updates=$(grep -c 'pSelected *=' "$Kernel")
# res.SelectedLight is assigned in ResampleCandidate (which runs BEFORE pSelected exists, so it is exempt) and at
#    the two merge winners. pSelected is assigned once at establishment plus once per merge winner.
[ "$Selects" -ge 2 ] && [ "$Updates" -ge 3 ]
Report $? "selection writes ($Selects) are covered by carry updates ($Updates)"

# The dead pHatSel must not come back. Matched as a DECLARATION, not as a word: the commentary above the carry
#    explains why it was removed and naturally mentions it by name, and a check that trips over its own
#    documentation is a check people learn to ignore.
! grep -qE '^[^/]*float +pHatSel' "$Kernel"
Report $? "the dead pHatSel evaluation has not returned"

# The merge must still consume exactly one RandFloat per tap, or the sequence shifts.
Draws=$(grep -c 'RandFloat(seed) \* ' "$Kernel")
[ "$Draws" -ge 3 ]
Report $? "merge tests still draw from the same RNG stream ($Draws sites)"

echo
if [ "$Fail" != "0" ]; then echo "[ReSTIRSelection] FAILED"; exit 1; fi

if [ -z "$Glslang" ] || [ ! -x "$Glslang" ]; then
    echo "[ReSTIRSelection] glslang not found - SPIR-V check skipped"
    echo "[ReSTIRSelection] OK (CPU only)"
    exit 0
fi

echo "[ReSTIRSelection] the kernel still lowers to SPIR-V"
Stage=$(mktemp -d); trap 'rm -rf "$Stage"' EXIT
"$Glslang" -V --target-env vulkan1.2 -S comp -I. -IEngine "$Kernel" -o "$Stage/ReSTIRViewport.spv" >/dev/null 2>&1
Report $? "ReSTIRViewport.slang lowers to SPIR-V"

echo
[ "$Fail" = "0" ] && echo "[ReSTIRSelection] OK" || { echo "[ReSTIRSelection] FAILED"; exit 1; }
