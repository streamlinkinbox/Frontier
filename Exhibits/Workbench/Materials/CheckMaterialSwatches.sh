#!/usr/bin/env bash
# Material library gate — the `--scene materials` level (MaterialSwatchStructure) pinned as executable code. Compiles
#    MaterialSwatchProof.cpp against the real engine TUs (ContentCodec + SceneCodec + MaterialCodec + ObjCodec +
#    FbxCodec + UfbxTranslation + SceneStructure + GeometryStructure + OrientationClassifier + TextureIndex +
#    MaterialIndex + MaterialSwatchStructure): the level builds, exports through the real SceneCodec, decodes through
#    the real codec stack, and every one of the 42 swatches is compared field by field against what was authored —
#    plus the grid geometry, the 20-channel coverage census (16 carried, 4 acknowledged) and the eight-selection tally.
#    Headers: Vulkan-Headers ($PWD/ExternalPackages, $MATERIAL_SCENES_EXT, or ~/.cache/m7) + the interchange headers
#    (same candidates + $MATERIAL_CODEC_EXT + ~/.cache/m6: cgltf/ufbx/fast_obj, MANDATORY) + stb_image.h
#    ($PWD/ExternalPackages/stb, $MATERIAL_SCENES_EXT/stb, ~/.cache/m8/stb, or ~/.cache/sweep/stb — TextureIndex's only
#    third-party include). No imgui, no toml, no GPU, no window.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[MaterialSwatches] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    exit 1
fi
M6=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "${MATERIAL_CODEC_EXT:-}" "$HOME/.cache/m6"; do
    [ -n "$candidate" ] && [ -f "$candidate/cgltf/cgltf.h" ] && [ -f "$candidate/ufbx/ufbx.h" ] \
        && [ -f "$candidate/fast_obj/fast_obj.h" ] && M6="$candidate" && break
done
if [ -z "$M6" ]; then
    echo "[MaterialSwatches] RED — interchange headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, \$MATERIAL_CODEC_EXT, ~/.cache/m6)"
    exit 1
fi
Stb=""
for candidate in "$PWD/ExternalPackages/stb" "${MATERIAL_SCENES_EXT:+$MATERIAL_SCENES_EXT/stb}" "$HOME/.cache/m8/stb" "$HOME/.cache/sweep/stb"; do
    [ -n "$candidate" ] && [ -f "$candidate/stb_image.h" ] && Stb="$candidate" && break
done
if [ -z "$Stb" ]; then
    echo "[MaterialSwatches] RED — stb_image.h not found (tried ExternalPackages/stb, \$MATERIAL_SCENES_EXT/stb, ~/.cache/m8/stb, ~/.cache/sweep/stb)"
    exit 1
fi
echo "[MaterialSwatches] headers: $Vk + $M6 + $Stb"

Bin="$(mktemp -u /tmp/MaterialSwatches.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections -DFRONTIER_CPU_PORT \
     -I Exhibits/Workbench/Materials -I Engine/DisplayPresentation -I Engine/ContentInterchange -I Engine/DeviceExchange \
     -I Engine/GeometricRaster -I Engine/Shaders -I Exhibits/Workbench/Editor \
     -I "$Vk/Vulkan-Headers/include" -I "$M6/cgltf" -I "$M6/ufbx" -I "$M6/fast_obj" -I "$Stb" \
     Exhibits/Workbench/Materials/MaterialSwatchProof.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     Engine/ContentInterchange/SceneCodec.cpp \
     Engine/ContentInterchange/ContentCodec.cpp \
     Engine/ContentInterchange/ObjCodec.cpp \
     Engine/ContentInterchange/FbxCodec.cpp \
     Engine/ContentInterchange/UfbxTranslation.cpp \
     Engine/ContentInterchange/MaterialSwatchStructure.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     -o "$Bin" 2>/tmp/MaterialSwatches.build; then
    echo "[MaterialSwatches] COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialSwatches.build | head -40; exit 1
fi
if [ -s /tmp/MaterialSwatches.build ]; then echo "[MaterialSwatches] warnings:"; sed 's/^/    /' /tmp/MaterialSwatches.build | head -20; fi
if ! "$Bin" 2>&1 | tee /tmp/MaterialSwatches.log | grep -q "MATERIAL SWATCHES: PASS"; then
    echo "[MaterialSwatches] RED"; grep "FAIL" /tmp/MaterialSwatches.log | sed 's/^/    /' | head -30; rm -f "$Bin"; exit 1
fi
grep -c "^ok " /tmp/MaterialSwatches.log | xargs echo "[MaterialSwatches] GREEN — checks passed:"
rm -f "$Bin"
