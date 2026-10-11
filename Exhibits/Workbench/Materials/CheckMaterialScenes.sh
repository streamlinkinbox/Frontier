#!/usr/bin/env bash
# M8 gate — full-scene validation + the Tier B decision data. Compiles MaterialSceneProof.cpp against the real
#    engine TUs (ContentCodec + SceneCodec + MaterialCodec + ObjCodec + FbxCodec + UfbxTranslation + SceneStructure +
#    GeometryStructure + OrientationClassifier + TextureIndex + MaterialIndex + ShaderBallStructure + the CPU
#    shaderball exhibit as a SHADERBALL_PREVIEW_LIB TU): CornellBox, GlassProof, and the generated R4b shaderball
#    level decode → Finalise at slab_limit 1/2/8 → census + fold review; synthetic multi-slab probes pin the fold;
#    every material shades through the preview entry; the 128-cell consumption matrix re-pins M3. Sponza validates
#    when present (fetch script), skipped otherwise.
#    Headers: Vulkan-Headers ($REPO/ExternalPackages, $MATERIAL_SCENES_EXT, or ~/.cache/m7) + the interchange
#    headers (same candidates + $MATERIAL_CODEC_EXT + ~/.cache/m6: cgltf/ufbx/fast_obj, MANDATORY) + stb_image.h
#    (ExternalPackages/stb, $MATERIAL_SCENES_EXT/stb, ~/.cache/m8/stb, or ~/.cache/sweep/stb — TextureIndex's only
#    third-party include). No imgui, no toml, no GPU, no window.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Vk=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
done
if [ -z "$Vk" ]; then
    echo "[MaterialScenes] RED — Vulkan-Headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, ~/.cache/m7)"
    exit 1
fi
M6=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "${MATERIAL_CODEC_EXT:-}" "$HOME/.cache/m6"; do
    [ -n "$candidate" ] && [ -f "$candidate/cgltf/cgltf.h" ] && [ -f "$candidate/ufbx/ufbx.h" ] \
        && [ -f "$candidate/fast_obj/fast_obj.h" ] && M6="$candidate" && break
done
if [ -z "$M6" ]; then
    echo "[MaterialScenes] RED — interchange headers not found (tried ExternalPackages/, \$MATERIAL_SCENES_EXT, \$MATERIAL_CODEC_EXT, ~/.cache/m6)"
    exit 1
fi
Stb=""
for candidate in "$PWD/ExternalPackages/stb" "${MATERIAL_SCENES_EXT:+$MATERIAL_SCENES_EXT/stb}" "$HOME/.cache/m8/stb" "$HOME/.cache/sweep/stb"; do
    [ -n "$candidate" ] && [ -f "$candidate/stb_image.h" ] && Stb="$candidate" && break
done
if [ -z "$Stb" ]; then
    echo "[MaterialScenes] RED — stb_image.h not found (tried ExternalPackages/stb, \$MATERIAL_SCENES_EXT/stb, ~/.cache/m8/stb, ~/.cache/sweep/stb)"
    exit 1
fi
echo "[MaterialScenes] headers: $Vk + $M6 + $Stb"
if [ -f Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf ]; then
    echo "[MaterialScenes] Sponza present — will validate"
else
    echo "[MaterialScenes] Sponza absent — will skip (fetch: Projects/Project-Zero/Build/FetchSponza.ps1)"
fi

Bin="$(mktemp -u /tmp/MaterialScenes.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections -DFRONTIER_CPU_PORT -DSHADERBALL_PREVIEW_LIB \
     -I Exhibits/Workbench/Materials -I Engine/DisplayPresentation -I Engine/ContentInterchange -I Engine/DeviceExchange \
     -I Engine/GeometricRaster -I Engine/Shaders -I Exhibits/Workbench/Editor \
     -I "$Vk/Vulkan-Headers/include" -I "$M6/cgltf" -I "$M6/ufbx" -I "$M6/fast_obj" -I "$Stb" \
     Exhibits/Workbench/Materials/MaterialSceneProof.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     Engine/ContentInterchange/SceneCodec.cpp \
     Engine/ContentInterchange/ContentCodec.cpp \
     Engine/ContentInterchange/ObjCodec.cpp \
     Engine/ContentInterchange/FbxCodec.cpp \
     Engine/ContentInterchange/UfbxTranslation.cpp \
     Engine/ContentInterchange/ShaderBallStructure.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     Engine/DisplayPresentation/ShadingTableCodec.cpp \
     Engine/ContentInterchange/ShaderballPreview.cpp \
     -o "$Bin" 2>/tmp/MaterialScenes.build; then
    echo "[MaterialScenes] COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialScenes.build | head -40; exit 1
fi
if [ -s /tmp/MaterialScenes.build ]; then echo "[MaterialScenes] warnings:"; sed 's/^/    /' /tmp/MaterialScenes.build | head -20; fi
if ! "$Bin" 2>&1 | tee /tmp/MaterialScenes.log | grep -q "MATERIAL SCENES: PASS"; then
    echo "[MaterialScenes] RED"; grep "FAIL" /tmp/MaterialScenes.log | sed 's/^/    /' | head -30; rm -f "$Bin"; exit 1
fi
grep -c "^ok " /tmp/MaterialScenes.log | xargs echo "[MaterialScenes] GREEN — checks passed:"
rm -f "$Bin"
