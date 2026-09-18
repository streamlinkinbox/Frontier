#!/usr/bin/env bash
# -Pack: fold a project's sibling payloads into the project file, so the project loads with its content deleted.
#
#    usage: bash Tools/Scripts/PackProject.sh <file.projectspace> [more files…]
#           bash Tools/Scripts/PackProject.sh --build-only        ← build the tool and stop (ExplodeProject.sh uses it)
#
# The plan (§7) names this script and `ExplodeProject.sh` as the two tools the pipeline exposes. Both are thin wrappers
#    over SpaceTool on purpose: a shell script that reimplements packing is a SECOND implementation of the format, and the
#    only thing two implementations of a container format reliably do is disagree about a corner of it.
#
# Exit codes: 0 = every input packed; 1 = one refused (the tool says which reference and where it looked); 2 = no input.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

if [ "$#" -eq 0 ]; then
    echo "usage: bash Tools/Scripts/PackProject.sh <file.projectspace> [more files…]"
    echo "       bash Tools/Scripts/PackProject.sh --build-only"
    exit 2
fi
BuildOnly=0
[ "${1:-}" = "--build-only" ] && { BuildOnly=1; set --; }

# ─── the tool, built if it is not there (Build/ is regenerable and gitignored) ────────────────────────────────────────
Tool="Build/SpaceTool"
if [ ! -x "$Tool" ] || [ -n "$(find Exhibits/Workbench/ProjectFormat Engine/ContentInterchange Projects/Project-Zero/Source -newer "$Tool" -name '*.cpp' -o -newer "$Tool" -name '*.h' 2>/dev/null | head -1)" ]; then
    Vk=""
    for candidate in "$PWD/ExternalPackages" "${MATERIAL_SCENES_EXT:-}" "$HOME/.cache/m7"; do
        [ -n "$candidate" ] && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Vk="$candidate" && break
    done
    [ -z "$Vk" ] && { echo "[PackProject] Vulkan-Headers not found (see CheckSpaceFamily.sh)"; exit 1; }
    mkdir -p Build
    echo "[PackProject] building $Tool"
    # -ffunction-sections/-fdata-sections/--gc-sections drop MaterialSwatchStructure::Export and the glTF writer behind it:
    #   the tool reads the level's records, it never writes glTF.
    g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -I Engine/GeometricRaster -I Engine/DeviceExchange -I Engine/ContentInterchange -I Engine/DisplayPresentation \
        -I Projects/Project-Zero/Source -I "$Vk/Vulkan-Headers/include" \
        Exhibits/Workbench/ProjectFormat/SpaceTool.cpp \
        Engine/ContentInterchange/SpaceCodec.cpp Engine/ContentInterchange/SpaceExport.cpp \
        Engine/ContentInterchange/MaterialSwatchStructure.cpp Engine/ContentInterchange/MaterialIndex.cpp \
        Engine/DeviceExchange/OrientationClassifier.cpp Projects/Project-Zero/Source/CommandLine.cpp \
        -o "$Tool" || exit 1
fi

[ "$BuildOnly" -eq 1 ] && exit 0

Status=0
for File in "$@"; do
    [ -f "$File" ] || { echo "[PackProject] no such file: $File"; Status=1; continue; }
    Directory="$(dirname "$File")"
    if "$Tool" -Pack="$File" -Out="$Directory"; then
        echo "[PackProject] $File → $File.packed"
    else
        echo "[PackProject] REFUSED: $File"
        Status=1
    fi
done
exit "$Status"
