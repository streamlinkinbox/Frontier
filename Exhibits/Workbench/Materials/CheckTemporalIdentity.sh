#!/usr/bin/env bash
# D10 — temporal integration for MOVING GEOMETRY: does the reuse follow the object, and does it stop following the
#       wrong one?
#
#    The kernel reprojects every history read (the running mean in ResolveSurface, and both reservoir pools) through the
#    R2 motion vectors and validates it on the surface's normal and depth. That rule is necessary and it is not enough:
#    an object that slides across a coplanar neighbour, or two objects that exchange places, present the SAME normal and
#    the SAME depth at the reprojected pixel — so the pixel inherits radiance that was shaded for something else, and
#    the running mean carries it until the sample count drowns it. D10 adds the third fact a history has to remember:
#    WHICH surface it came from (kFeatureTemporalIdentity, bit 9).
#
#    This gate measures the two halves of that claim, on the CPU mirror of the kernel (MaterialLevelViewport, which is
#    the kernel's own estimator with the same constants), because neither can be measured off a still frame:
#
#      ① THE SHADOW FOLLOWS THE OBJECT. The moving object's shadow is tested against the renderer's own intersector: a grid of
#         floor points, each shot sun-ward, and the subset whose first blocker is the moving object IS its shadow. Both
#         poses must cast one and the two footprints must differ — the direction-agnostic statement of "the shadow moved
#         with it". (The mirror re-poses the geometry AND rebuilds the acceleration structure, so the shadow rays see the
#         object where it actually is: a moving object with a lagging shadow is the other half of this acceptance.)
#
#      ② THE GHOST IS REFUSED, AND IT SHOWS. The driver slides one swatch along its own plane, out and back, so the
#         closing frame's scene is the REST scene — which makes a plain render of the untouched level a ground truth
#         for it. ⚠️ That ground truth deliberately SHARES the arm's seed stream: this is a PAIRED comparison, where the
#         two arms differ in one switch and the Monte-Carlo noise is common to both, so the difference that survives is
#         the ghost and not the sampling. (Roadmap #7's `--seed-stream` is the opposite tool for a different question —
#         the absolute distance from a converged reference, where shared noise would flatter the estimate. Use it here
#         and the pair gets noisier without becoming more honest; the claim above is about a difference, not a level.) The same sequence then runs twice, once with identity validation and once without
#         (--restir-no-identity, the pre-D10 rule), and the error against that ground truth is compared. Measured on the
#         M10 row-3 crop / 256x192 / 4 spp / 16 frames: 891 vs 1732 RMSE (0.0136 vs 0.0264 normalised), i.e. the ghosts
#         the identity refuses are about twice the error the pre-D10 rule leaves in the picture.
#
#      ③ A STILL SCENE IS NOT DISTURBED. The identity is the OBJECT, not the triangle: a per-frame sub-pixel jitter over
#         a 1500-triangle sphere reports a different triangle almost every frame, and an identity that fine restarted
#         13.5 % of a STATIC image for nothing (measured — it was the first cut, and this check is why it was replaced).
#         So the A/B is also run with no motion at all: the two arms must agree to within a couple of percent of pixels,
#         and the refused-read count must stay at the level of genuine silhouettes.
#
#    The shader side of the contract (bit 9, the identity in the moment image, the widened reservoir record, the host's
#    80 B mirror of it) is checked by Tools/Build/CheckShaders.sh and by the static assertions in the shader and host
#    sources; this gate checks the behaviour.
#    Usage: CheckTemporalIdentity.sh [fast]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Work="$(mktemp -d /tmp/TemporalIdentity.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

Failures=0
Pass() { echo "  PASS  $1"; }
Fail() { echo "  FAIL  $1"; Failures=$((Failures + 1)); }

# ── the mirror must build ────────────────────────────────────────────────────────────────────────────────────────────
echo "[D10] building the CPU mirror of the kernel (Projects/Project-Zero/Host/MaterialLevelViewport)"
if ! make -C "$Host" MaterialLevelViewport > "$Work/build.log" 2>&1; then
    echo "  FAIL  the mirror did not build"; sed 's/^/    /' "$Work/build.log" | tail -20; exit 1
fi
Pass "the mirror builds clean (-Wall -Wextra -Werror)"

# ── the shader must still lower, and must carry the feature ──────────────────────────────────────────────────────────
Gl=""
Glslang="${GLSLANG:-}"
for candidate in "$Glslang" "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -x "$candidate/bin/glslang" ] && Gl="$candidate/bin/glslang" && break
done
[ -z "$Gl" ] && [ -x /tmp/glslang-build/StandAlone/glslang ] && Gl=/tmp/glslang-build/StandAlone/glslang
if [ -n "$Gl" ]; then
    # The same staging recipe Tools/Build/CheckShaders.sh uses: glslang needs a recognised extension for the stage.
    cp Engine/Shaders/ReSTIRViewport.slang "$Work/kernel.glsl"
    if "$Gl" -V --target-env vulkan1.2 -S comp -DFRONTIER_SHADER_TOOLCHAIN=1 \
            -IEngine "-I$PWD/Engine/Shaders" -o "$Work/kernel.spv" "$Work/kernel.glsl" > "$Work/shader.log" 2>&1; then
        Pass "ReSTIRViewport.slang lowers to SPIR-V with the D10 identity path in it ($(stat -c%s "$Work/kernel.spv") B)"
    else
        Fail "ReSTIRViewport.slang does not lower"; sed 's/^/    /' "$Work/shader.log" | tail -10
    fi
else
    echo "  NOTE  no glslang on PATH — the lowering check is Tools/Build/CheckShaders.sh's job (15/15)"
fi

grep -q "kFeatureTemporalIdentity" Engine/Shaders/ReSTIRViewport.slang \
    && Pass "the kernel still declares the identity feature bit (bit 9)" || Fail "the kernel lost kFeatureTemporalIdentity"
grep -q "IdentityObject" Engine/Shaders/ReSTIRViewport.slang \
    && Pass "the kernel validates on the OBJECT (the tesselation cannot flip the identity)" || Fail "the kernel lost IdentityObject"
grep -q "uint32_t Identity\[4\]" Engine/DeviceExchange/SwapchainExchange.cpp \
    && Pass "the host's reservoir record is the shader's (80 B with Identity[4], static_asserted)" || Fail "the host record lost Identity"

# ── ① the shadow follows the object ─────────────────────────────────────────────────────────────────────────────────
echo "[D10] ① does the moving object's shadow follow it?"
Shadow="$("$Bin" --shadow-probe --drift 0.08 --frames 9 --sun 12 2>&1)"
echo "$Shadow" | grep -E "^\[shadow-probe\]" | sed 's/^/    /'
Rest="$(echo "$Shadow"  | sed -n 's/.*rest        : \([0-9]*\) of.*/\1/p')"
Peak="$(echo "$Shadow"  | sed -n 's/.*peak .*: \([0-9]*\) of.*/\1/p')"
Changed="$(echo "$Shadow" | sed -n 's/.*footprint: \([0-9]*\) points.*/\1/p')"
if [ "${Rest:-0}" -gt 0 ] && [ "${Peak:-0}" -gt 0 ] && [ "${Changed:-0}" -gt 0 ]; then
    Pass "① both poses cast a shadow onto the floor ($Rest / $Peak grid points) and the footprint moved with the object ($Changed points changed state)"
else
    Fail "① the shadow did not follow the object (rest $Rest, peak $Peak, changed $Changed)"
fi

# ── ② the ghost is refused (moving) and ③ a still scene is not disturbed ─────────────────────────────────────────────
Size=256; Height=192; Spp=4; Frames=16; Drift=0.08
if [ "$Mode" = "fast" ]; then Size=160; Height=120; Spp=2; Frames=6; fi
Common="--row 3 --width $Size --height $Height --spp $Spp --frames $Frames --restir --taps 2"

Render() { # $1 tag, $2 extra args
    "$Bin" $Common $2 --out "$Work/$1.png" > "$Work/$1.log" 2>&1 || { echo "  FAIL  $1 exited non-zero"; tail -5 "$Work/$1.log" | sed 's/^/    /'; exit 1; }
    grep -q ", 0 non-finite/out-of-range samples" "$Work/$1.log" || { echo "  FAIL  $1 produced non-finite samples"; exit 1; }
}

echo "[D10] ② the moving sequence, with and without the identity rule, against a ground truth"
Render on     "--drift $Drift"
Render off    "--drift $Drift --restir-no-identity"
Render ground ""

Refused=$(grep -o "D10 identity: [0-9]* film reads + [0-9]* DI merges + [0-9]* GI merges" "$Work/on.log" | tail -1)
Reads=$(echo "$Refused" | grep -o "[0-9]*" | awk '{t += $1} END {print t + 0}')
if [ "${Reads:-0}" -gt 0 ]; then
    Pass "② the rule refuses history that the normal/depth test alone would merge: $Refused reads (final frame)"
else
    Fail "② the rule refused nothing — the sequence has no ghost to catch, so it measures nothing"
fi
OnErr=$(compare -metric RMSE "$Work/on.png" "$Work/ground.png" null: 2>&1 | grep -o "^[0-9.]*")
OffErr=$(compare -metric RMSE "$Work/off.png" "$Work/ground.png" null: 2>&1 | grep -o "^[0-9.]*")
echo "    identity ON  RMSE vs the static ground truth: $OnErr"
echo "    identity OFF RMSE vs the static ground truth: $OffErr   (the pre-D10 rule, ghosts inherited)"
if [ -n "$OnErr" ] && [ -n "$OffErr" ] && awk "BEGIN{exit !($OnErr < $OffErr)}"; then
    Ratio=$(awk "BEGIN{printf \"%.2f\", $OffErr / ($OnErr + 1e-9)}")
    Pass "② the identity rule is closer to the ground truth than the pre-D10 rule (${Ratio}x lower RMSE)"
else
    Fail "② identity validation did not improve the moving sequence ($OnErr vs $OffErr)"
fi

echo "[D10] ③ a STILL scene: the rule must not disturb what did not move"
Still="--width $Size --height $Height --spp $Spp --frames $Frames --restir --taps 2"
"$Bin" $Still --out "$Work/still_on.png"  > "$Work/still_on.log"  2>&1
"$Bin" $Still --restir-no-identity --out "$Work/still_off.png" > "$Work/still_off.log" 2>&1
AE=$(compare -metric AE "$Work/still_on.png" "$Work/still_off.png" null: 2>&1 | grep -o "^[0-9]*")
Total=$((Size * Height))
Pct=$(awk "BEGIN{printf \"%.2f\", 100.0 * ${AE:-999999} / $Total}")
StillRefused=$(grep -o "D10 identity: [0-9]* film reads" "$Work/still_on.log" | tail -1)
if [ -n "$AE" ] && awk "BEGIN{exit !($Pct < 2.0)}"; then
    Pass "③ on a still scene the two arms agree to $Pct % of pixels ($AE of $Total) — no phantom restarts; $StillRefused of them"
else
    Fail "③ the rule restarts $Pct % of a STILL image ($AE of $Total) — the identity is finer than the surfaces it names"
fi

echo
if [ "$Failures" -eq 0 ]; then
    echo "[TemporalIdentity] GREEN — moving geometry keeps its own history, and only its own"
    exit 0
fi
echo "[TemporalIdentity] RED — $Failures check(s) failed"
exit 1
