#!/usr/bin/env bash
#============================================================================================================================================
#                                                    CHECKCELESTIALCONTENT.SH
#============================================================================================================================================
# The moon albedos must decode to real images, and the star catalogue must not be empty.
#
#    Both degraded SILENTLY in the shipped build — 1x1 white placeholder moons and a starless sky — because the files
#    were opened relative to the working directory. That is a wrong picture, not an error, so it needs a gate.
#
#    usage: bash Tools/Build/CheckCelestialContent.sh

set -euo pipefail
cd "$(dirname "$0")/../.."

STB_ROOT=""
for Candidate in ExternalPackages/stb "${MATERIAL_SCENES_EXT:-}/stb" "$HOME/.cache/m7/stb"; do
    if [ -f "$Candidate/stb_image.h" ]; then STB_ROOT="$Candidate"; break; fi
done
if [ -z "$STB_ROOT" ]; then
    echo "[celestial-content] stb_image.h not found — tried ExternalPackages/, \$MATERIAL_SCENES_EXT and ~/.cache/m7." >&2
    echo "[celestial-content]   git clone --depth 1 https://github.com/nothings/stb ~/.cache/m7/stb" >&2
    exit 2
fi
echo "[celestial-content] stb: $STB_ROOT"

Stage="$(mktemp -d)"
trap 'rm -rf "$Stage"' EXIT

g++ -std=c++20 -O1 -g -I. -I"$STB_ROOT" \
    -o "$Stage/CelestialContentGate" \
    Tools/Build/Gates/CelestialContentGate.cpp \
    Engine/ContentInterchange/TextureIndex.cpp \
    Engine/ContentInterchange/AssetResolution.cpp \
    Engine/GeometricRaster/StarCatalogueIndex.cpp

# Deliberately run from the repository root: the gate asserts the CONTENT is good. The separate question of whether
#    paths resolve from another working directory is what AssetResolution handles, and the check below covers it.
"$Stage/CelestialContentGate"

# ...and again from somewhere else entirely, which is the exact condition that produced placeholder moons: the
#    mirrored Build\Project-Zero.exe runs with its own folder as the working directory, not the repository root.
#
#    The binary must be staged INSIDE the repository for this to mean anything. AssetResolution finds content by
#    walking the working directory first and then the EXECUTABLE's own parent chain, so a gate binary sitting in
#    /tmp has no repository above it and would fail for a reason that has nothing to do with the bug. Staging it
#    under Build/ is what the real executable does.
mkdir -p Build/.gate
cp "$Stage/CelestialContentGate" Build/.gate/CelestialContentGate
trap 'rm -rf "$Stage" Build/.gate' EXIT

echo
echo "[celestial-content] re-running from / with the binary staged in Build/, as the shipped .exe is"
if ( cd / && "$OLDPWD/Build/.gate/CelestialContentGate" >/dev/null 2>&1 ); then
    echo "[celestial-content] GREEN — content resolves without being launched from the repository root"
else
    echo "[celestial-content] FAIL — content only resolves when launched from the repository root." >&2
    echo "[celestial-content]   This is the AssetResolution regression: moons fall back to 1x1 placeholders" >&2
    echo "[celestial-content]   and the star catalogue loads empty. See Engine/ContentInterchange/AssetResolution.h" >&2
    exit 1
fi
