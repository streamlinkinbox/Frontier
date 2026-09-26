#!/usr/bin/env bash
# Roadmap #5, step 1 — is COVERAGE the binding constraint, or is the residual somewhere else?
#
#    §14.5 measured where the pool's missing 84 % go, and the answer was 69 % sky escape: a first-bounce vertex needs a
#    BSDF ray that reached geometry, and most of them see the sky. The obvious reading is "close the coverage gap, then"
#    — but coverage is a share of PIXELS, and the question that decides whether the replay + shift-mapping rewrite is
#    worth its bias risk is a share of ERROR. Those are different questions and this script is the second one: it masks
#    an RMSE against a converged reference per class, so the report can say which classes actually hold the residual.
#
#      · the reference is a 512-spp render on --seed-stream 1 (independent — roadmap #7's lesson);
#      · the ReSTIR arm is the shipped configuration (4 candidates, 2 taps, 128 frames);
#      · the plain arm is the SAME resolve rate (1 spp × 128 frames), which is §14.3's matched-rate comparison;
#      · the class map comes from the ReSTIR arm's last frame, so the masks describe the frame being measured.
#
#    The per-class numbers are produced by `GiClassError.cpp` (three raw dumps, no dependencies) and its TOTAL row is
#    checked against `compare -metric RMSE` on the same pair of PNGs — if the masking arithmetic drifts, this fails.
#
#    Usage: ReportGiClassError.sh [fast]     (fast = 64 frames; full = 128, ~2 min)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Work="$(mktemp -d /tmp/GiClassError.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

Frames=128; Width=240; Height=135
[ "$Mode" = "fast" ] && Frames=64

echo "[GiClass] building the mirror"
if ! make -C "$Host" MaterialLevelViewport > "$Work/build.log" 2>&1; then
    echo "  FAILED to build"; sed 's/^/    /' "$Work/build.log" | tail -15; exit 1
fi

echo "[GiClass] rendering at ${Width}x${Height}: reference (512 spp, stream 1), ReSTIR arm, plain arm at the same rate"
"$Bin" --width "$Width" --height "$Height" --spp 512 --frames 1 --seed-stream 1 --out "$Work/ref.png" > /dev/null 2>&1
"$Bin" --width "$Width" --height "$Height" --spp 4 --frames "$Frames" --restir --taps 2 \
       --class-map "$Work/class.png" --out "$Work/restir.png" > "$Work/restir.log" 2>&1
"$Bin" --width "$Width" --height "$Height" --spp 1 --frames "$Frames" --out "$Work/plain.png" > /dev/null 2>&1

grep -E "GI classes \(last frame\)|indirect pool on|\[restir\] frame +$Frames:" "$Work/restir.log" | sed 's/^/    /'

echo "[GiClass] decoding to raw dumps (ImageMagick) and masking"
for N in ref restir plain; do convert "$Work/$N.png" -depth 8 "rgb:$Work/$N.rgb"; done
convert "$Work/class.png" -depth 8 "gray:$Work/class.gray"

if ! g++ -std=c++20 -O2 -Wall -Wextra -Werror -I Exhibits/Workbench/Materials \
        Exhibits/Workbench/Materials/GiClassError.cpp -o "$Work/GiClassError" 2> "$Work/tool.build"; then
    echo "  FAILED to build the analysis tool"; sed 's/^/    /' "$Work/tool.build" | head -20; exit 1
fi

"$Work/GiClassError" --arm "$Work/restir.rgb" --ref "$Work/ref.rgb" --class "$Work/class.gray" \
                     --arm2 "$Work/plain.rgb" --width "$Width" --height "$Height"

echo "[GiClass] cross-check — the tool's TOTAL row vs ImageMagick on the same PNGs:"
ToolTotal=$("$Work/GiClassError" --arm "$Work/restir.rgb" --ref "$Work/ref.rgb" --class "$Work/class.gray" \
                --width "$Width" --height "$Height" | awk '/^TOTAL_RMSE/ {print $2}')
ImTotal=$(compare -metric RMSE "$Work/restir.png" "$Work/ref.png" null: 2>&1 | grep -o '^[0-9.]*')
printf "    tool %.2f · compare %.2f\n" "$ToolTotal" "$ImTotal"
if awk "BEGIN{d = $ToolTotal - $ImTotal; if (d < 0) d = -d; exit !(d < 0.5)}"; then
    echo "[GiClass] GREEN — the per-class masking reproduces ImageMagick's whole-image RMSE"
    exit 0
fi
echo "[GiClass] RED — the tool and ImageMagick disagree on the same images (masking arithmetic is wrong)"
exit 1
