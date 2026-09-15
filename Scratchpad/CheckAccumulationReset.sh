#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckAccumulationReset.sh — a reset reaches the next dispatch, not the next increment
#============================================================================================================================================
# The frame loop reads the accumulation index for the dispatch, the §8 record comparisons reset it when the sky
#    changes, and the loop unconditionally increments it after presenting. Without the pending flag the reset was
#    incremented back to one before any frame read it: every slider reset dispatched with FrameIndex >= 1, the
#    kernel kept blending the old frame at 1/n, and panel edits only became visible when a camera move failed
#    reprojection geometrically. This gate pins the flag on both methods and the loop order that demands it.
set -u
cd "$(dirname "$0")/.."
Fail=0

Report()
{
    if [ "$1" -eq 0 ]; then printf '  %-64s PASS\n' "$2"; else printf '  %-64s FAIL\n' "$2"; Fail=1; fi
}

Integrator=Engine/DisplayPresentation/ReSTIRIntegrator.h
Loop=Projects/Project-Zero/Source/GameExecution.cpp

echo "[AccumulationReset] the reset survives the end-of-frame increment"
grep -q 'void ResetAccumulation() noexcept { AccumulationIndex = 0u; ResetPending = true; }' "$Integrator"
Report $? "ResetAccumulation raises the pending flag"
grep -q 'if (ResetPending) ResetPending = false; else AccumulationIndex++;' "$Integrator"
Report $? "the increment spends the flag instead of swallowing the reset"
grep -q 'bool.*ResetPending = false;' "$Integrator"
Report $? "the flag exists and defaults to clear"
grep -q 'ResetAccumulation();' Engine/DisplayPresentation/ReSTIRIntegrator.cpp
Report $? "camera moves route through the same reset"

echo "[AccumulationReset] the loop order that demands the flag"
ReadLine=$(grep -n 'Frame.FrameIndex       = Integrator.QueryAccumulationIndex();' "$Loop" | head -1 | cut -d: -f1); ReadLine=${ReadLine:-0}
PresentLine=$(grep -n 'Surface.RecordAndPresent(Dispatch);' "$Loop" | head -1 | cut -d: -f1); PresentLine=${PresentLine:-0}
IncrLine=$(grep -n 'Integrator.IncrementAccumulationIndex();' "$Loop" | head -1 | cut -d: -f1); IncrLine=${IncrLine:-0}
# The F3 reset sits before the dispatch read (same-frame fresh); the three §8 resets sit after it, which is the
#    order that needs the flag — count the resets past the read, not the first reset in the file.
ResetsAfter=0
for L in $(grep -n 'Integrator.ResetAccumulation();' "$Loop" | cut -d: -f1); do
    if [ "$L" -gt "$ReadLine" ]; then ResetsAfter=$((ResetsAfter + 1)); LastReset=$L; fi
done
LastReset=${LastReset:-0}
[ "$ReadLine" -gt 0 ] && [ "$ResetsAfter" -ge 3 ] && [ "$LastReset" -lt "$PresentLine" ] && [ "$IncrLine" -gt "$PresentLine" ]
Report $? "read index, then §8 resets ($ResetsAfter), then present, then increment"
Resets=$(grep -c 'Integrator.ResetAccumulation();' "$Loop")
[ "$Resets" -ge 4 ]
Report $? "all reset sites present (F3 plus sky/moon/post, $Resets found)"

if [ "$Fail" -eq 0 ]; then echo; echo "[AccumulationReset] OK"; else echo; echo "[AccumulationReset] FAILED"; fi
exit "$Fail"
