#!/usr/bin/env bash
# Roadmap #5 — WHERE the indirect pool's missing coverage goes, and why its M stays low.
#
#    Not a gate: a report. #5 asks for indirect coverage 16 % → 100 %, and "100 %" hides four different problems of very
#    different size. This script runs the mirror on the shipped ReSTIR config and prints the decomposition the mirror
#    itself counts — the reasons a pixel gets no pool (escape / glass / emitter / degenerate / unlit), the pool's coverage
#    and M against the DIRECT pool's, and the temporal half's attempts and successes, which is the number that says
#    whether a low M means "broken merge" or "never asked". Report §14.5 is this table, with the numbers measured
#    2026-09-18; re-running this reproduces them.
#
#    Usage: ReportGiCoverage.sh [seconds-per-config]      (default 32 frames at two sizes, ~1 min)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Frames="${1:-32}"
Work="$(mktemp -d /tmp/GiCoverage.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

make -C "$Host" MaterialLevelViewport > "$Work/build.log" 2>&1 || {
    echo "[GiCoverage] the mirror did not build"; tail -15 "$Work/build.log" | sed 's/^/    /'; exit 1; }

echo "[GiCoverage] indirect-pool coverage, ${Frames} frames per config, 4 candidates, 2 taps"
for Size in "240 135" "128 72"; do
    set -- $Size
    W=$1; H=$2
    "$Bin" --width "$W" --height "$H" --spp 4 --frames "$Frames" --restir --taps 2 --out "$Work/$W.png" \
        > "$Work/$W.log" 2>&1 || { echo "[GiCoverage] the $W run failed"; tail -5 "$Work/$W.log"; exit 1; }

    Surface=$(sed -n "s/^\[restir\] frame *$Frames: \([0-9]*\) surface pixels.*/\1/p" "$Work/$W.log" | tail -1)
    Direct=$(sed -n "s/^\[restir\] frame *$Frames:.*mean M \([0-9.]*\) (max M [0-9]*), shaded M \([0-9.]*\).*/\1 \2/p" "$Work/$W.log" | tail -1)
    Gi=$(sed -n "s/^\[restir gi\] frame *$Frames: indirect pool on \([0-9.]*\)% of surface pixels, mean M \([0-9.]*\), shaded M \([0-9.]*\).*/\1 \2 \3/p" "$Work/$W.log" | tail -1)
    Reasons=$(sed -n "s/^\[restir gi\] reasons: bad \([0-9]*\) escape \([0-9]*\) emitter \([0-9]*\) unlit \([0-9]*\) unusable \([0-9]*\) vertex \([0-9]*\).*/\1 \2 \3 \4 \5 \6/p" "$Work/$W.log" | tail -1)
    Temporal=$(sed -n "s/^\[restir gi\] temporal: tried \([0-9]*\), refused for a MISSING previous vertex record \([0-9]*\), merged \([0-9]*\).*/\1 \2 \3/p" "$Work/$W.log" | tail -1)

    if [ -z "$Surface" ] || [ -z "$Reasons" ]; then
        echo "[GiCoverage] could not parse the ${W}x${H} transcript — the counters or the print format moved"
        exit 1
    fi

    echo
    echo "  === ${W}x${H}, ${Surface} surface pixels/frame, ${Frames} frames ==="
    echo "  pool coverage: ${Gi%% *}% of surface pixels"
    echo "  indirect pool  mean M $(echo "$Gi" | cut -d' ' -f2), shaded M $(echo "$Gi" | cut -d' ' -f3)"
    echo "  direct pool    mean M $(echo "$Direct" | cut -d' ' -f1), shaded M $(echo "$Direct" | cut -d' ' -f2)"
    echo
    printf "  %-42s %9s %9s\n" "reason (per frame)" "pixels" "share"
    set -- $Reasons
    for Pair in "bad:$1" "escape:$2" "emitter:$3" "unlit:$4" "unusable:$5" "vertex (COVERED):$6"; do
        Name="${Pair%%:*}"; Total="${Pair##*:}"
        Per=$(awk "BEGIN{printf \"%.0f\", $Total / $Frames}")
        Share=$(awk "BEGIN{printf \"%.1f%%\", 100.0 * ($Total / $Frames) / $Surface}")
        printf "  %-42s %9s %9s\n" "    $Name" "$Per" "$Share"
    done
    echo
    set -- $Temporal
    Tried=$1; Missing=$2; Merged=$3
    awk "BEGIN{printf \"  temporal half (per frame): tried %.0f  merged %.0f (%.1f%% of tries)  missing-record %.0f\n\",
         $Tried / $Frames, $Merged / $Frames, 100.0 * $Merged / ($Tried + 1e-9), $Missing / $Frames}"
done

echo
echo "[GiCoverage] The gap is the escape case: a primary BSDF sample that sees the sky makes no first-bounce vertex, and a"
echo "             light-sample pool needs one to reconnect from. Fixing it is #5's replay + shift mapping, not a looser"
echo "             validation test — the reservoir must carry the vertex payload so the neighbour's vertex can be moved"
echo "             onto the receiver. Report §14.5."
