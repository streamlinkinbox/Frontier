#!/usr/bin/env bash
# M9 visual exhibit driver — the shipped à-trous filter + the shipped reprojection rule, over a CPU path-traced scene.
# NOT part of CheckMaterialDenoise.sh (the sheets are a render, not a check); run on demand or before showing the work.
# Usage: RunDenoiseExhibit.sh [Size=192] [RefSpp=2048] [sheet=all|ab|identity|fade|levels|edges|reproject|streams]
# Writes Exhibits/Gallery/Materials/DenoiseSheet_*.png and prints their sha256.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1
Size="${1:-192}" Ref="${2:-2048}" Sheet="${3:-all}"

Stage="$(mktemp -d /tmp/DenoiseExhibit.XXXXXX)"
if ! DO_STAGE="$Stage" python3 Exhibits/Workbench/Materials/StageAtrousDenoise.py; then
    echo "[DenoiseExhibit] RED — the shader did not match the transform's expectations"; rm -rf "$Stage"; exit 1
fi
sed 's/^/    /' "$Stage/transform.manifest"

Bin="$(mktemp -u /tmp/DenoiseExhibit.XXXXXX)"
echo "[DenoiseExhibit] building"
if ! g++ -std=c++20 -O2 -Wall -Wextra -DFRONTIER_CPU_PORT \
     -I Exhibits/Workbench/Materials -I Exhibits/Workbench/Editor -I "$Stage" \
     Exhibits/Workbench/Materials/DenoiseExhibit.cpp \
     Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp \
     -o "$Bin" 2>/tmp/DenoiseExhibit.build; then
    echo "  BUILD FAILED"; head -40 /tmp/DenoiseExhibit.build; rm -rf "$Stage"; exit 1
fi
if [ -s /tmp/DenoiseExhibit.build ]; then echo "[DenoiseExhibit] warnings:"; sed 's/^/    /' /tmp/DenoiseExhibit.build | head -20; fi

Raw=/tmp/DenoiseExhibit.raw
rm -rf "$Raw"; mkdir -p "$Raw"
echo "[DenoiseExhibit] smoke (64px, 16 spp)"
if ! "$Bin" --sheet "$Sheet" --size 64 --ref 16 --outdir "$Raw" > /tmp/DenoiseExhibit.smoke.log 2>&1; then
    echo "  SMOKE FAILED"; tail -20 /tmp/DenoiseExhibit.smoke.log; rm -f "$Bin"; rm -rf "$Stage"; exit 1
fi
tail -3 /tmp/DenoiseExhibit.smoke.log
rm -rf "$Raw"; mkdir -p "$Raw"

echo "[DenoiseExhibit] full sheets (${Size}px, ${Ref}spp reference)"
"$Bin" --sheet "$Sheet" --size "$Size" --ref "$Ref" --outdir "$Raw" 2>&1 | tee /tmp/DenoiseExhibit.log | grep -v "^\[exhibit\] denoise-" | tail -12
for Pair in "denoise-ab.png:DenoiseSheet_NoiseAndEdges.png" \
            "denoise-identity.png:DenoiseSheet_IdentityAtConvergence.png" \
            "denoise-fade.png:DenoiseSheet_FadeOut.png" \
            "denoise-levels.png:DenoiseSheet_AtrasLevels.png" \
            "denoise-edges.png:DenoiseSheet_EdgeStops.png" \
            "denoise-reproject.png:DenoiseSheet_Reprojection.png" \
            "denoise-streams.png:DenoiseSheet_StreamAB.png"; do
    Source="${Pair%%:*}"; Dest="Exhibits/Gallery/Materials/${Pair##*:}"
    [ -f "$Raw/$Source" ] || continue
    if command -v convert >/dev/null 2>&1; then convert "$Raw/$Source" -strip -define png:compression-level=9 "$Dest"; else cp "$Raw/$Source" "$Dest"; fi
    sha256sum "$Dest"
done
rm -f "$Bin"; rm -rf "$Stage" "$Raw"
