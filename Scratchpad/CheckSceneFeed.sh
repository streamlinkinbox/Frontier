#!/usr/bin/env bash
# Pins the editor feed (EditorFeedSequence) against the FRESH CornellBox.gltf: the roster rows, the sheet
#    figures and the animated run GameExecution's development tick walks. Headless: everything here is CPU.
#
# ExternalPackages/{stb,ufbx} and cgltf are uninitialised submodules in this sandbox, so the script clones the
#    upstream headers to /tmp on first use. On a normal checkout with submodules initialised, point the three
#    include variables at ExternalPackages instead.
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
         $Root/Engine/DeviceExchange/OrientationClassifier.cpp $Root/Engine/DeviceExchange/InputExchange.cpp
         $Root/Engine/GeometricRaster/CameraProjection.cpp"
cd "$Root"
g++ -std=c++20 -O1 -I "$Vkh" -I "$Cg" -I "$Ufbx" -I "$Stb" -I "$Root" \
    "$Root/Scratchpad/SceneFeedProof.cpp" "$Root/Projects/Project-Zero/Source/EditorFeedSequence.cpp" "$Root/Projects/Project-Zero/Source/FlyThroughSolver.cpp" "$Root/Projects/Project-Zero/Source/ShowroomStructure.cpp" "$Root/Engine/DisplayPresentation/ReSTIRIntegrator.cpp" "$Root/Engine/DisplayPresentation/ExposureIntegrator.cpp" \
    $Sources -o "$Work/feed"
"$Work/feed"
echo "[Feed] feed OK"
