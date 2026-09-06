#!/usr/bin/env bash
# Exports Showroom.gltf through the REAL SceneCodec and re-imports it through the real loader, proving the level
#    survives the round trip the renderer actually performs. This is what GameExecution does on first run with
#    `--scene showroom`; running it here just does it without a GPU.
#
# ExternalPackages/{stb,ufbx} and cgltf are uninitialised submodules in this sandbox, so the script clones the
#    upstream headers to /tmp on first use. On a normal checkout with submodules initialised, point the three
#    include variables at ExternalPackages instead -- or just run the game, which exports the level itself.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
Vkh="${VKH:-/tmp/vkh/include}"
Cg="${CGLTF:-/tmp/cg}"; Ufbx="${UFBX:-/tmp/ufbxsrc}"; Stb="${STB:-/tmp/stbsrc}"
[ -f "$Cg/cgltf.h"   ] || git clone --depth 1 -q https://github.com/jkuhlmann/cgltf.git "$Cg"
[ -f "$Ufbx/ufbx.h"  ] || git clone --depth 1 -q https://github.com/ufbx/ufbx.git "$Ufbx"
[ -f "$Stb/stb_image.h" ] || git clone --depth 1 -q https://github.com/nothings/stb.git "$Stb"
Work="$(mktemp -d)"; trap 'rm -rf "$Work"' EXIT
Sources="$Root/Engine/ContentInterchange/SceneCodec.cpp $Root/Engine/ContentInterchange/MaterialCodec.cpp
         $Root/Engine/ContentInterchange/MaterialIndex.cpp $Root/Engine/ContentInterchange/TextureIndex.cpp
         $Root/Engine/GeometricRaster/SceneStructure.cpp $Root/Engine/GeometricRaster/GeometryStructure.cpp
         $Root/Engine/DeviceExchange/OrientationClassifier.cpp"
cd "$Root"
g++ -std=c++20 -O1 -I "$Vkh" -I "$Cg" -I "$Ufbx" -I "$Stb" -I "$Root" \
    "$Root/Scratchpad/ShowroomExportProof.cpp" "$Root/Projects/Project-Zero/Source/ShowroomStructure.cpp" \
    $Sources -o "$Work/export"
"$Work/export"
echo "[Showroom] export OK"
