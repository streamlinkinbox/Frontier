#!/usr/bin/env bash
# Shaderball exhibit driver — builds the kept harness and renders the sheets. NOT part of
# CheckMaterialsProof.sh (the full sheets are minutes, not seconds); run on demand or before material milestones.
# Usage: RunShaderballExhibit.sh [Size=512] [Spp=256] [sheet=triptych|solid|sss|both]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1
Size="${1:-512}" Spp="${2:-256}" Sheet="${3:-triptych}"
Bin="$(mktemp -u /tmp/ShaderballExhibit.XXXXXX)"
echo "[ShaderballExhibit] building"
if ! g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT -I Exhibits/Workbench/Materials -I Engine/DisplayPresentation \
     -I Engine/Shaders -I Exhibits/Workbench/Editor Exhibits/Workbench/Materials/ShaderballExhibit.cpp \
     Engine/DisplayPresentation/ShadingTableCodec.cpp -o "$Bin" 2>/tmp/ShaderballExhibit.build; then
    echo "  BUILD FAILED"; head -30 /tmp/ShaderballExhibit.build; exit 1
fi
Smoke() { # $1 = extra args, $2 = tag
    echo "[ShaderballExhibit] smoke $2 (128px, 8spp)"
    if ! "$Bin" $1 --out /tmp/ShaderballExhibit.smoke.png --size 128 --spp 8 2>&1 | tee /tmp/ShaderballExhibit.smoke.log | grep -q "wrote"; then
        echo "  SMOKE FAILED"; tail -5 /tmp/ShaderballExhibit.smoke.log; rm -f "$Bin"; exit 1
    fi
    if grep -q "bad=[1-9]" /tmp/ShaderballExhibit.smoke.log; then echo "  SMOKE: non-finite pixels"; rm -f "$Bin"; exit 1; fi
}
Render() { # $1 = extra args, $2 = dest file
    echo "[ShaderballExhibit] full sheet $2 (${Size}px, ${Spp}spp)"
    "$Bin" $1 --out /tmp/ShaderballExhibit.raw.png --size "$Size" --spp "$Spp" 2>&1 | tail -5
    if command -v convert >/dev/null 2>&1; then convert /tmp/ShaderballExhibit.raw.png -strip -define png:compression-level=9 "Exhibits/Gallery/Materials/$2"; else cp /tmp/ShaderballExhibit.raw.png "Exhibits/Gallery/Materials/$2"; fi
    rm -f /tmp/ShaderballExhibit.raw.png
    sha256sum "Exhibits/Gallery/Materials/$2"
}
Smoke "" "triptych"
Smoke "--solid" "solid"
Smoke "--sss" "sss"
case "$Sheet" in
    triptych) Render "" "ShaderballSheet_GlassClothCoat.png" ;;
    solid)    Render "--solid" "ShaderballSheet_SolidGlass.png" ;;
    sss)      Render "--sss" "ShaderballSheet_Subsurface.png" ;;
    both)     Render "" "ShaderballSheet_GlassClothCoat.png"; Render "--solid" "ShaderballSheet_SolidGlass.png" ;;
    *)        echo "unknown sheet '$Sheet' (triptych|solid|both)"; rm -f "$Bin"; exit 1 ;;
esac
rm -f "$Bin"
