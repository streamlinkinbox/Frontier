#!/usr/bin/env bash
# CPU ReSTIR — the kernel's direct-lighting block simulated off-GPU, on the M10 level, as a kept sheet.
#
#    The Vulkan build draws `--scene materials` with ReSTIRViewport.slang: RIS candidates, temporal and spatial
#    reservoir reuse, visibility re-traced at the shading pixel, the running mean, then the M9 à-trous chain. A CPU
#    render cannot dispatch that kernel (bindless tables, a BVH in buffers, storage images), so this driver renders
#    the same level with the same algorithm mirrored in MaterialLevelViewport.cpp — line for line against the shader
#    text, constants included (kTemporalMClamp, the 25° / 10 % validation, kSpatialRadiusMin/MaxPx, kSunPickProbability,
#    kSunShadowDistance, the shade form) — and writes the comparison the GPU cannot print:
#
#      ① the converged reference (brute-force accumulation, 192 spp)
#      ② brute force at the SAME sample budget as the ReSTIR panels (16 frames × 4 spp = the Standard tier's
#         CandidatesPerPixel)
#      ③ ReSTIR, Standard tier (4 candidates/pixel/frame, 2 spatial taps) — what the app's default tier runs
#      ④ ③ through the shipped à-trous chain (the product's actual pipeline: R7 filter over the R6 reuse)
#      ⑤ ReSTIR with a triangular camera excursion (out and back, so the closing frame is the base pose again)
#         — the R2/R7a reprojection doing its job: the running mean follows the surface out and back
#      ⑥ ⑤ with BOTH reads forced to the pixel's own address (the pre-R7a rule) — the A/B the rule exists for.
#         The excursion is big enough to separate them: at 0.10 m/frame the reprojected closing frame measures
#         ~19 % lower error against ① than the same-pixel read, and carries fewer spurious restarts
#      ⑦ D10 — the MOVING OBJECT: one swatch slides along its own plane, out and back (so ⑧'s closing frame is the rest
#         pose). Correct behaviour is threefold and all three are asserted by CheckTemporalIdentity.sh: the shadow
#         follows the object (the renderer's own intersector, a grid of floor points), a still scene is left alone, and
#         the ghost is refused. The panel is the picture of the last of those.
#      ⑧ ⑦ with identity validation off (--restir-no-identity, the pre-D10 rule) and — for the eye — the untouched level
#         at the same pose, which is the ground truth the closing frame is judged against: 891 vs 1 732 RMSE in
#         CheckTemporalIdentity.sh, i.e. the reads the identity refuses carry about twice the error.
#
#    Deterministic: every RNG is seeded from (pixel, sample index, frame); re-running reproduces each panel and the
#    montage bit-for-bit. NOT part of CheckMaterialDenoise.sh (a render, not a check).
#    Usage: RunRestirViewport.sh [fast|full]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Gallery="Exhibits/Gallery/Materials"
Work="$(mktemp -d /tmp/RestirSheet.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[RestirSheet] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    exit 1
fi

echo "[RestirSheet] building the Project-Zero CPU viewport (with the M9 filter staged)"
if ! make -C "$Host" MaterialLevelViewport >/tmp/RestirSheet.build 2>&1; then
    echo "[RestirSheet] BUILD FAILED"; sed 's/^/    /' /tmp/RestirSheet.build | tail -25; exit 1
fi

Size=320; Spp=4; Frames=16; Reference=192; Pan=0.10; Drift=0.06
if [ "$Mode" = "fast" ]; then Size=128; Spp=2; Frames=4; Reference=16; fi
Excursion=$(awk -v p="$Pan" -v f="$Frames" 'BEGIN { printf "%.2f", p * int((f - 1) / 2) }')

Render() { # $1 = tag, $2 = extra args, $3 = label
    echo "[RestirSheet] $3"
    "$Bin" --width "$Size" --height "$Size" --view default --out "$Work/$1.png" $2 > "$Work/$1.log" 2>&1 || {
        echo "[RestirSheet] RED — $1 exited $?"; sed 's/^/    /' "$Work/$1.log" | tail -20; exit 1; }
    grep -E "\[(restir|film)\] +frame +(1|$Frames):|reservoirs:|film:" "$Work/$1.log" | sed 's/^/    /'
    if ! grep -q ", 0 non-finite/out-of-range samples" "$Work/$1.log"; then
        echo "[RestirSheet] RED — $1 has non-finite or out-of-range samples"; exit 1
    fi
}

Render reference "--spp $Reference --frames 1" \
       "① reference — brute force, ${Reference} spp, 1 frame"
Render plain     "--spp $Spp --frames $Frames" \
       "② brute force at the same budget — ${Spp} spp × ${Frames} frames"
Render restir    "--spp $Spp --frames $Frames --restir --taps 2" \
       "③ ReSTIR, Standard tier — ${Spp} candidates × ${Frames} frames, 2 taps"
Render denoised  "--spp $Spp --frames $Frames --restir --taps 2 --denoise" \
       "④ ReSTIR + the shipped à-trous chain (5 levels)"
Render pan       "--spp $Spp --frames $Frames --restir --taps 2 --pan $Pan" \
       "⑤ ReSTIR with a ±${Excursion} m triangular camera excursion (R7a reprojection on)"
Render noreproj  "--spp $Spp --frames $Frames --restir --taps 2 --pan $Pan --no-reproject" \
       "⑥ … the same excursion, both history reads at the pixel's own address (pre-R7a)"
# The moving panels crop to the sphere row that carries the drifting swatch (--row 3): at whole-level scale the swatch
#    is ~26 px across in a 320 px panel and the ghost it drags is a few pixels — the crop is what makes the picture show
#    what the gate measures.
Render moving    "--spp $Spp --frames $Frames --restir --taps 2 --row 3 --drift $Drift" \
       "⑦ D10 — a moving object: one swatch slides along its own plane (${Drift} m/frame), out and back"
Render movingoff "--spp $Spp --frames $Frames --restir --taps 2 --row 3 --drift $Drift --restir-no-identity" \
       "⑧ … the same, identity validation off (the pre-D10 rule: ghosts inherited)"
# ⚠️ The ground truth is a render at the SAME budget, not ①'s converged reference — deliberately. At a few spp over a
#    few frames, a 192-spp reference measures Monte-Carlo noise rather than ghosts, and the numbers said exactly that:
#    the ON/OFF pair came out noise-dominated (3 459 vs 3 759 RMSE) while the gate, which compares against a MATCHED
#    budget where the noise is common to both arms, measures 1.94x. The excursion RETURNS to the rest pose, so the
#    matched-budget render of the untouched level IS what the closing frame should look like: the ghost is the
#    difference, and only the difference.
# ⚠️ The ground truth must be the SAME ESTIMATOR (--restir --taps 2), not ①'s brute force: two different estimators
#    differ by far more than a ghost does, and the first version of this panel measured exactly that mistake (3 803 vs
#    3 508, noise-dominated, with the ON arm apparently worse). Static ReSTIR at the same budget, same pose.
Render movingref "--spp $Spp --frames $Frames --restir --taps 2 --row 3" \
       "⑧ ground truth — the level untouched, same estimator, budget and pose"

echo "[RestirSheet] error against ① (display space, RMSE / normalised), and the R7a tell of the pan pair:"
# ⚠️ The D10 panels are CROPPED (--row 3) and are therefore not comparable with ①: they get their own block below.
for panel in plain restir denoised pan noreproj; do
    printf "    %-10s " "$panel"
    compare -metric RMSE "$Work/$panel.png" "$Work/reference.png" null: 2>&1; echo
done
printf "    %-10s " "⑤ vs ⑥"
compare -metric RMSE "$Work/pan.png" "$Work/noreproj.png" null: 2>&1; echo
echo "[RestirSheet] D10: the moving object's ghost, against the same budget and pose untouched (§7a of Docs/DynamicGeometry.md):"
for panel in moving movingoff; do
    printf "    %-10s " "$panel"
    compare -metric RMSE "$Work/$panel.png" "$Work/movingref.png" null: 2>&1; echo
done
printf "    %-10s " "⑦ vs ⑧"
compare -metric RMSE "$Work/moving.png" "$Work/movingoff.png" null: 2>&1; echo

Sheet="$Gallery/RestirSheet_StandardTier.png"
montage -label "① reference — brute force ${Reference} spp" "$Work/reference.png" \
        -label "② brute force — ${Spp} spp × ${Frames} frames" "$Work/plain.png" \
        -label "③ ReSTIR — ${Spp} cand. × ${Frames} frames, 2 taps" "$Work/restir.png" \
        -label '④ ReSTIR + à-trous (the product)'       "$Work/denoised.png" \
        -label "⑤ + a ±${Excursion} m camera excursion (R7a on)" "$Work/pan.png" \
        -label '⑥ the same, reads at the own address'   "$Work/noreproj.png" \
        -label "⑦ D10 — an object slides ${Drift} m/frame (identity on)" "$Work/moving.png" \
        -label '⑧ … identity validation off (pre-D10)'   "$Work/movingoff.png" \
        -tile 2x4 -geometry +6+6 -background '#141414' -fill '#e8e8e8' -font DejaVu-Sans -pointsize 15 \
        "$Sheet"
echo "[RestirSheet] wrote $Sheet"
sha256sum "$Sheet"
