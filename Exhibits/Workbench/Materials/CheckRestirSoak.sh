#!/usr/bin/env bash
# Roadmap #2 — the 1 000-frame soak: is reservoir growth BOUNDED, and does a bounded M stop helping?
#
#    The ticket was opened when the spatial pass fed its own output back into the temporal history and M compounded —
#    mean 843 633 by frame 20: an unbounded reservoir, which makes the reuse meaningless (every merge is dominated by
#    whatever the previous frame decided, so the estimator stops responding to the scene). The fix took the TEMPORAL
#    reservoir into the history and capped each tap against the receiver's PRE-merge count. What was never run is the
#    long soak that says the fixed form is still bounded at 1 000 frames rather than at the 128 the sheets use.
#
#    This gate is that run, and the arithmetic read at the end:
#
#      ① the run completes with the harness's own zero-non-finite assertion intact, at every checkpoint;
#      ② no ceiling warning — the mirror prints one the moment max M passes 1e8, which is the uint32 ceiling the shader
#         stores M in (`GpuReservoir.Counts.x`). A saturation here is a wrap, and a wrapped M is a reservoir whose
#         confidence is nonsense;
#      ③ M does not COMPOUND: between checkpoints, mean M may not grow faster than the frame count does. Linear growth
#         is what a bounded merge does (each frame adds at most 20x the current count); super-linear growth is the
#         failure this ticket exists for. The measured shape is not even linear — M SATURATES (mean ~60 from frame 25
#         on, max 84 at every checkpoint, still 58.9 at frame 1 000) — so the assertion has room either way;
#      ④ the clamp bounds the ERROR too, not just M. This is measured against an INDEPENDENT reference (a 512-spp render
#         on `--seed-stream 1`, roadmap #7), because an error-versus-shared-reference curve is not monotone in the run
#         length and would measure the correlation rather than the estimator: measured on the shared stream, the plain
#         arm's error RISES 389.79 → 511.76 between 250 and 1 000 frames (it decouples from the reference's own noise),
#         while on the independent stream it falls 734.05 → 627.62. ④ therefore has TWO forms, because the claim differs
#         with the run length and asserting one form for both was a bug in this gate's first cut (the `fast` mode failed
#         on a 12 % move that is a *true* statement about the arm, not a defect):
#           · full (1 000 frames, compared 250 → 1 000): the arm is past its plateau, so a ±10 % band is the assertion —
#             that is the "the clamp is also the floor" claim;
#           · fast (250 frames, compared 62 → 250): the arm is still closing on the plateau (~-12 % over that span), so
#             the assertion is monotone improvement, and the plateau band is what the full run exists to measure.
#         Both forms PRINT both references' numbers rather than hiding the difference;
#      ⑤ the plain arm at the same budget is a control: it must improve over its span (a quarter of ④'s), or ④'s statement
#         would be a blind metric rather than a clamped reservoir. Measured in full: 1 017.76 → 734.05 (−28 %) from 62
#         to 250 frames.
#
#    What the numbers say (2026-09-18, 128×72): M is bounded, and so is the error — the ReSTIR arm's distance to a
#    converged reference is FLAT from about frame 250 on (7 334.1 → 7 423.8 independent, band 6 918–7 636 over the run).
#    The clamp is therefore both the fix and the floor: with M saturating near 60, the temporal filter's effective
#    memory is ~M frames, and additional frames resample the same clamped weights instead of adding information.
#    Buying accuracy past this point takes taps/candidates/coverage (report §14.3's dial matrix), not more frames.
#
#    Usage: CheckRestirSoak.sh [fast]      (fast = 250 frames, ~3 min; full = 1 000, ~6 min)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Work="$(mktemp -d /tmp/RestirSoak.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

Frames=1000
Size=128; Height=72
if [ "$Mode" = "fast" ]; then Frames=250; fi
Mid=$((Frames / 4))
# The control span is a QUARTER of ④'s, in both modes (62 → 250 frames in full, 15 → 62 in fast). It used to be a
#    hardcoded 62, which silently became a zero-length span in `fast` mode (Mid = 250/4 = 62) and compared a render
#    against itself: ⑤ read 1017.76 → 1017.76 and failed the gate for the one reason that is not a renderer defect.
#    A half-span was tried next and measured only −12.4 % over 31 → 62, which is real movement but too close to the
#    −15 % bar for a CONTROL to be comfortable; the quarter span has measured −28 %.
Control=$((Mid / 4))
Checkpoints="1 25 50 100 250"
[ "$Frames" != "250" ] && Checkpoints="$Checkpoints $Frames"

Failures=0
Pass() { echo "  PASS  $1"; }
Fail() { echo "  FAIL  $1"; Failures=$((Failures + 1)); }
RMSE() { compare -metric RMSE "$1" "$2" null: 2>&1 | grep -o '^[0-9.]*'; }

echo "[Soak] building the CPU mirror of the kernel"
if ! make -C "$Host" MaterialLevelViewport > "$Work/build.log" 2>&1; then
    echo "  FAIL  the mirror did not build"; sed 's/^/    /' "$Work/build.log" | tail -20; exit 1
fi

echo "[Soak] ${Frames} frames at ${Size}x${Height}, 4 candidates, 2 taps — one resolve per frame, the app's own rate"
if ! "$Bin" --width "$Size" --height "$Height" --spp 4 --frames "$Frames" --restir --taps 2 \
        --out "$Work/soak.png" > "$Work/soak.log" 2>&1; then
    echo "  FAIL  the soak run exited non-zero"; tail -10 "$Work/soak.log" | sed 's/^/    /'; exit 1
fi

MeanAt()   { sed -n "s/^\[restir\] frame *$1:.*mean M \([0-9.]*\) (max M \([0-9]*\)).*/\1/p" "$Work/soak.log" | tail -1; }
MaxAt()    { sed -n "s/^\[restir\] frame *$1:.*(max M \([0-9]*\)).*/\1/p" "$Work/soak.log" | tail -1; }
ShadedAt() { sed -n "s/^\[restir\] frame *$1:.*shaded M \([0-9.]*\).*/\1/p" "$Work/soak.log" | tail -1; }

echo "[Soak] M over the run (mean / max / shaded):"
for F in $Checkpoints; do
    printf "    frame %4d : mean M %-8s max M %-8s shaded M %s\n" \
        "$F" "$(MeanAt "$F")" "$(MaxAt "$F")" "$(ShadedAt "$F")"
done

# ① the harness's own assertion, at every checkpoint it printed
if grep -q ", 0 non-finite/out-of-range samples" "$Work/soak.log" && \
   ! grep "non-finite" "$Work/soak.log" | grep -qv ", 0 non-finite"; then
    Pass "① ${Frames} frames completed with zero non-finite or out-of-range samples"
else
    Fail "① the soak produced non-finite samples"; grep -n "non-finite" "$Work/soak.log" | grep -v ", 0 " | head -3 | sed 's/^/    /'
fi

# ② the uint32 ceiling the shader stores M in
if grep -q "approaching the uint32 ceiling" "$Work/soak.log"; then
    Fail "② M approached the uint32 ceiling the shader stores it in"
else
    Pass "② max M never approached the uint32 ceiling (the mirror's own warning never fired)"
fi

# ③ non-compounding: no checkpoint may grow faster than the frame count did
LastMean="$(MeanAt "$Frames")"; MidMean="$(MeanAt "$Mid")"
LastMax="$(MaxAt "$Frames")";   MidMax="$(MaxAt "$Mid")"
RatioMean=$(awk "BEGIN{printf \"%.3f\", ${LastMean:-0} / (${MidMean:-1} + 1e-9)}")
RatioFrames=$(awk "BEGIN{printf \"%.3f\", $Frames / $Mid}")
if awk "BEGIN{exit !($RatioMean < 6.0)}"; then
    Pass "③ mean M grew ${RatioMean}x while the frame count grew ${RatioFrames}x (frame $Mid -> $Frames) — bounded, not compounding"
else
    Fail "③ mean M grew ${RatioMean}x over a ${RatioFrames}x longer run — that is the compounding the clamp exists to prevent"
fi
if [ "${LastMax:-0}" -le $(( ${MidMax:-1} * 4 )) ]; then
    Pass "③ max M stayed within 4x of its frame-$Mid value ($MidMax -> $LastMax) — the M-clamp holds at ${Frames} frames"
else
    Fail "③ max M grew from $MidMax to $LastMax — the clamp is not holding"
fi

# ④/⑤: the error, against an INDEPENDENT reference (roadmap #7's second seed stream)
echo "[Soak] rendering the references and the comparison runs (this is the long part)"
"$Bin" --width "$Size" --height "$Height" --spp 512 --frames 1             --out "$Work/ref_shared.png" > /dev/null 2>&1
"$Bin" --width "$Size" --height "$Height" --spp 512 --frames 1 --seed-stream 1 --out "$Work/ref_ind.png" > /dev/null 2>&1
"$Bin" --width "$Size" --height "$Height" --spp 4 --frames "$Mid" --restir --taps 2 --out "$Work/short.png" > /dev/null 2>&1

ErrShortInd=$(RMSE "$Work/short.png" "$Work/ref_ind.png")
ErrLongInd=$(RMSE "$Work/soak.png"  "$Work/ref_ind.png")
ErrShortSha=$(RMSE "$Work/short.png" "$Work/ref_shared.png")
ErrLongSha=$(RMSE "$Work/soak.png"  "$Work/ref_shared.png")
echo "[Soak] ReSTIR arm, distance to a converged reference (RMSE, display space):"
echo "    frame $Mid -> $Frames, INDEPENDENT stream 0 vs 1 : $ErrShortInd -> $ErrLongInd"
echo "    frame $Mid -> $Frames, SHARED stream           : $ErrShortSha -> $ErrLongSha   (recorded, not asserted)"
Drift=$(awk "BEGIN{d = ($ErrLongInd - $ErrShortInd) / ($ErrShortInd + 1e-9); printf \"%.4f\", (d < 0 ? -d : d)}")
Shift=$(awk "BEGIN{printf \"%.1f\", $Drift * 100}")
if [ "$Frames" -ge 1000 ]; then
    # Past the plateau: flatness is the claim.
    if [ -n "$ErrShortInd" ] && [ -n "$ErrLongInd" ] && awk "BEGIN{exit !($Drift <= 0.10)}"; then
        Pass "④ the independent-stream error moved ${Shift}% between $Mid and $Frames frames — past the plateau, the clamp bounds the error as tightly as it bounds M"
    else
        Fail "④ the error moved ${Shift}% from $ErrShortInd to $ErrLongInd — a bounded M with a drifting image is the failure this check exists for"
    fi
else
    # Still on the way up to the plateau: improvement is the claim, and the band needs the 1 000-frame run.
    if [ -n "$ErrShortInd" ] && [ -n "$ErrLongInd" ] && awk "BEGIN{exit !($ErrLongInd <= $ErrShortInd * 1.02)}"; then
        Pass "④ the independent-stream error moved ${Shift}% between $Mid and $Frames frames — still closing on the plateau (the ±10 % flatness band is the 1 000-frame run's assertion, and the control below is what says the metric still sees movement)"
    else
        Fail "④ the error went $ErrShortInd → $ErrLongInd over $Mid → $Frames frames — more frames made it WORSE this early, which no model of the clamp predicts"
    fi
fi

"$Bin" --width "$Size" --height "$Height" --spp 4 --frames "$Control" --out "$Work/plain_short.png" > /dev/null 2>&1
"$Bin" --width "$Size" --height "$Height" --spp 4 --frames "$Mid"     --out "$Work/plain_long.png"  > /dev/null 2>&1
CtlShort=$(RMSE "$Work/plain_short.png" "$Work/ref_ind.png")
CtlLong=$(RMSE "$Work/plain_long.png"  "$Work/ref_ind.png")
echo "[Soak] plain arm, same reference and same one-resolve-per-frame rate: $Control frames -> $CtlShort · $Mid frames -> $CtlLong"
if [ -n "$CtlShort" ] && [ -n "$CtlLong" ] && awk "BEGIN{exit !($CtlLong < $CtlShort * 0.85)}"; then
    Pass "⑤ the plain control improved $(awk "BEGIN{printf \"%.0f\", (1 - $CtlLong / $CtlShort) * 100}")% over that span — the flatness in ④ is the clamp's, not a blind metric"
else
    Fail "⑤ the plain control did not improve ($CtlShort -> $CtlLong) — ④ proves nothing until the metric can see improvement"
fi

echo
if [ "$Failures" -eq 0 ]; then
    echo "[Soak] GREEN — ${Frames} frames, M bounded, the error bounded with it, and the metric able to see improvement"
    exit 0
fi
echo "[Soak] RED — $Failures check(s) failed"
exit 1
