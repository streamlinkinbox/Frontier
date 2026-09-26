#!/usr/bin/env bash
# Material library level — visual proof driver. Renders the M10 level (`--scene materials`) the way Project-Zero's own
#    CPU stack sees it: the engine's level builder + material records + BSDF text + sky core + tone map, at the product's
#    own camera and exposure. The Vulkan build draws the same thing with ReSTIR + the M9 denoiser instead of brute-force
#    samples; these sheets are the converged reference that stack is estimating, and they are what the level's rows,
#    panels and luminaires look like.
#
# NOT part of CheckMaterialSwatches.sh (a render, not a check). Usage:
#    RunMaterialLibraryViewport.sh [fast|full]        fast = 256px smoke, full = the kept sheets
# Writes Exhibits/Gallery/Materials/MaterialLibrary_*.png and prints their sha256 + linear means.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Mode="${1:-full}"
Host="Projects/Project-Zero/Host"
Bin="$Host/MaterialLevelViewport"
Gallery="Exhibits/Gallery/Materials"

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[MaterialLevel] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    echo "                git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers ~/.cache/m7/Vulkan-Headers"
    exit 1
fi

echo "[MaterialLevel] building the Project-Zero CPU viewport"
if ! make -C "$Host" MaterialLevelViewport >/tmp/MaterialLevel.build 2>&1; then
    echo "[MaterialLevel] BUILD FAILED"; sed 's/^/    /' /tmp/MaterialLevel.build | tail -30; exit 1
fi

# One render + its own sanity gate: nothing non-finite, nothing out of range, a mean that is not black or blown. A
#    render that fails those is a broken renderer, not a dark scene, so it stops the sheet set rather than shipping it.
Render() { # $1 = extra args, $2 = out name, $3 = label
    echo "[MaterialLevel] $3"
    "$Bin" $1 --out "$Gallery/$2" > /tmp/MaterialLevel.render.log 2>&1 || { echo "[MaterialLevel] RED — $2 exited $?"; exit 1; }
    sed 's/^/    /' /tmp/MaterialLevel.render.log
    if ! grep -q ", 0 non-finite/out-of-range samples" /tmp/MaterialLevel.render.log; then
        echo "[MaterialLevel] RED — $2 has non-finite or out-of-range samples"; exit 1
    fi
    local Mean
    Mean="$(grep -o "film: mean [0-9.]*" /tmp/MaterialLevel.render.log | tail -1 | awk '{print $3}')"
    if [ -z "$Mean" ] || [ "${Mean%%.*}" != "0" ] && [ "${Mean%%.*}" -gt 8 ]; then
        echo "[MaterialLevel] RED — $2 mean '$Mean' outside a plausible range (0.01 … 8)"; exit 1
    fi
}

if [ "$Mode" = "fast" ]; then
    Render "--width 256 --height 144 --spp 24" "MaterialLibrary_View.png" "smoke: 256×144, 24 spp"
    echo "[MaterialLevel] smoke OK"
    exit 0
fi

# Sample budgets are honest, not maxed: the CPU tracer is 2-core here, and these are the counts that finish inside a
#    coffee break. (The exhibit sheets run 256 spp because a shaderball panel is 1/250th of this pixel count.)
# ① the product frame: GameExecution's own camera, 16:9 at the window's aspect, product exposure
Render "--width 960 --height 540 --spp 80" "MaterialLibrary_View.png" "① the level at the app's own framing (960×540, 80 spp)"
# ② the glass family (row 3) — M4 thin/thick/BTDF, Beer tints, thin-walled acrylic
Render "--width 480 --height 270 --spp 128 --view glass --row 3" "MaterialLibrary_GlassRow.png" "② glass row (480×270, 128 spp)"
# ③ the specials (row 5) — M3 cloth, M4c film, haziness, metals
Render "--width 480 --height 270 --spp 128 --view row5 --row 5" "MaterialLibrary_Specials.png" "③ specials row (480×270, 128 spp)"
# ④ the whole set: grid, backdrop, three sign panels, ceiling key, side fill
Render "--width 480 --height 270 --spp 80 --view wide" "MaterialLibrary_Wide.png" "④ whole set (480×270, 80 spp)"

echo "[MaterialLevel] kept sheets:"
sha256sum "$Gallery"/MaterialLibrary_*.png
