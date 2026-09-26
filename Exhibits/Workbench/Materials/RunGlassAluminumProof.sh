#!/usr/bin/env bash
# Glass-on-aluminum CPU proof render.
# Usage: RunGlassAluminumProof.sh [size=384] [spp=128]
#
# This is a reference render, not the GPU ReSTIR viewport: it compiles the shipped
# MaterialEvaluation.slang as C++ and renders a red solid-glass sphere in front of
# an aluminum reflector, plus an IOR comparison sheet.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

Size="${1:-384}"
Spp="${2:-128}"
Gallery="Exhibits/Gallery/Materials"
Bin="$(mktemp -u /tmp/GlassAluminumProof.XXXXXX)"
Work="$(mktemp -d /tmp/GlassAluminumProof.XXXXXX)"
trap 'rm -rf "$Work" "$Bin"' EXIT

printf '[GlassAluminumProof] building CPU reference\n'
g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT \
    -I Exhibits/Workbench/Materials \
    -I Engine/DisplayPresentation \
    -I Engine/Shaders \
    -I Exhibits/Workbench/Editor \
    Exhibits/Workbench/Materials/GlassAluminumProof.cpp \
    Engine/DisplayPresentation/ShadingTableCodec.cpp \
    -o "$Bin"

"$Bin" "$Size" "$Spp" "$Gallery"

# The raw comparison has three panels separated by 4 pixels. Add a caption bar so
# the IOR/Fresnel variants remain identifiable when viewed without the README.
if command -v convert >/dev/null 2>&1; then
    Height=$(( (Size * 2 + 1) / 3 ))
    PointSize=$(( Size / 21 )); [ "$PointSize" -lt 12 ] && PointSize=12
    for I in 1 2 3; do
        Offset=$(( (I - 1) * (Size + 4) ))
        convert "$Gallery/GlassOnAluminum_FresnelIOR_Comparison.png" \
            -crop "${Size}x${Height}+${Offset}+0" +repage "$Work/p${I}.png"
        case "$I" in
            1) Label='IOR 1.10  |  low Fresnel' ;;
            2) Label='IOR 1.50  |  nominal glass' ;;
            3) Label='IOR 2.40  |  stronger Fresnel' ;;
        esac
        convert -size "${Size}x34" xc:'#161d26' \
            -font DejaVu-Sans-Bold -fill white -gravity center -pointsize "$PointSize" \
            -annotate +0+0 "$Label" "$Work/l${I}.png"
        convert "$Work/p${I}.png" "$Work/l${I}.png" -append "$Work/q${I}.png"
    done
    convert +append "$Work/q1.png" "$Work/q2.png" "$Work/q3.png" \
        "$Gallery/GlassOnAluminum_FresnelIOR_Comparison.png"
fi

sha256sum "$Gallery/GlassOnAluminum_IOR1p50_CPUReference.png" \
          "$Gallery/GlassOnAluminum_FresnelIOR_Comparison.png"
printf '[GlassAluminumProof] GREEN — reference renders written to %s\n' "$Gallery"
