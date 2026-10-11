#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Exhibits/Workbench/Materials/RunGlintSheet.sh — M6 glints: slate_glint_* read at last (roadmap #10)
#============================================================================================================================================
# The library's row 12 has carried slate_glint_density 1 → 8 × slate_glint_uv_scale 4 → 12 since R4a, stored in
#    every slab and read by NOTHING — the report's "stored-unread". This sheet proves the wiring: the kernel
#    (ReSTIRViewport.slang, after the normal map, before the coat frame) and the mirror both perturb the shading
#    normal through the SHARED AutomotiveApplyTriCoatFlakeNormal — flakes are microfacets under the coat, never
#    painted dots, never light sources (the automotive precedent's exact rule).
#
#    Two arms, one view (showcase row 12 close): glints ON (the shipped path) and --no-glints (the pre-M6
#    surface). The gates: the arms must DIFFER where the parameters say they should (the pair is read), and the
#    film means must agree within 2 % (a normal perturbation redistributes energy, it must not create any).
#
#    usage: bash Exhibits/Workbench/Materials/RunGlintSheet.sh [fast|full]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Gallery="Exhibits/Gallery/Materials"
Work="$(mktemp -d /tmp/GlintSheet.XXXXXX)"
trap 'rm -rf "$Work"' EXIT

W=640; H=360; Spp=48
if [ "$Mode" = "fast" ]; then W=320; H=180; Spp=12; fi

echo "[GlintSheet] building the Project-Zero CPU viewport"
if ! make -C "$Host" MaterialLevelViewport >/tmp/GlintSheet.build 2>&1; then
    echo "[GlintSheet] BUILD FAILED"; sed 's/^/    /' /tmp/GlintSheet.build | tail -25; exit 1
fi

Render() { # $1 = tag, $2 = extra args, $3 = label
    echo "[GlintSheet] $3"
    "$Bin" --level showcase --view glints --width "$W" --height "$H" --spp "$Spp" --frames 1 $2 \
        --out "$Work/$1.png" > "$Work/$1.log" 2>&1 || {
        echo "[GlintSheet] RED — $1 exited $?"; sed 's/^/    /' "$Work/$1.log" | tail -15; exit 1; }
    if ! grep -q ", 0 non-finite/out-of-range samples" "$Work/$1.log"; then
        echo "[GlintSheet] RED — $1 has non-finite or out-of-range samples"; exit 1
    fi
}

Render on  ""            "① glints ON — row 12's density 1 → 8 finally read"
Render off "--no-glints" "② --no-glints — the pre-M6 surface, byte-for-byte"

# Gate 1: the arms differ — the pair is READ. The normalised RMSE floor is deliberately modest (the glint row
#    is a strip of the frame); fast mode halves it with the resolution.
Rmse="$(compare -metric RMSE "$Work/on.png" "$Work/off.png" null: 2>&1 | grep -oE '\(([0-9.]+)\)' | tr -d '()')"
Floor="0.02"; [ "$Mode" = "fast" ] && Floor="0.01"
echo "[GlintSheet] arms differ by RMSE $Rmse (normalised; floor $Floor)"
if ! awk -v R="$Rmse" -v F="$Floor" 'BEGIN { exit (R > F) ? 0 : 1 }'; then
    echo "[GlintSheet] RED — the arms are near-identical: slate_glint_* is stored-unread again"; exit 1
fi

# Gate 2: energy sanity — the film means agree within 2 % (a perturbed normal moves light, never makes it).
MeanOn="$(grep -oE 'film: mean [0-9.]+' "$Work/on.log"  | grep -oE '[0-9.]+$')"
MeanOff="$(grep -oE 'film: mean [0-9.]+' "$Work/off.log" | grep -oE '[0-9.]+$')"
echo "[GlintSheet] film means: on $MeanOn, off $MeanOff"
if ! awk -v A="$MeanOn" -v B="$MeanOff" 'BEGIN { D = A > B ? A - B : B - A; exit (D / B < 0.02) ? 0 : 1 }'; then
    echo "[GlintSheet] RED — the glint arm changed the image's energy, not just its distribution"; exit 1
fi

# The kept sheet: the two arms stacked, labelled.
montage -font EngineContent/FontArchives/Archivo/Archivo-Bold.ttf -pointsize 14 -fill white -background '#1b1d22' \
    -label "glints ON — slate_glint_density 1-8 x uv_scale 4-12 (row 12), flakes as microfacets under the coat" "$Work/on.png" \
    -label "glints OFF (--no-glints) — the pre-M6 surface: smooth highlights, the stored-unread era" "$Work/off.png" \
    -tile 1x2 -geometry +6+6 "$Gallery/GlintRowSheet.png"
echo "[GlintSheet] wrote $Gallery/GlintRowSheet.png"
echo "[GlintSheet] >>> the glint pair is read, and reads as flakes"
