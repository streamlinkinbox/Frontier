#!/usr/bin/env bash
# Standalone automotive-material preview. It does not modify or render Project-Zero.
# Usage: RunAutomotiveMaterialPreview.sh [width=768] [height=432] [spp=96]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

Width="${1:-768}"
Height="${2:-432}"
Spp="${3:-96}"
OutDir="Exhibits/Gallery/Automotive"
Bin="$(mktemp -u /tmp/AutomotiveMaterialPreview.XXXXXX)"
trap 'rm -f "$Bin"' EXIT
mkdir -p "$OutDir"

printf '[AutomotivePreview] building exact CPU mirror\n'
g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT \
    -I Exhibits/Workbench/Materials \
    -I Engine/DisplayPresentation \
    -I Engine/Shaders \
    -I Exhibits/Workbench/Editor \
    Exhibits/Workbench/Automotive/AutomotiveMaterialPreview.cpp \
    Engine/DisplayPresentation/ShadingTableCodec.cpp \
    -o "$Bin"

"$Bin" "$Width" "$Height" "$Spp" "$OutDir"
sha256sum "$OutDir/AutomotiveMaterialSuite.png" \
          "$OutDir/AutomotiveOpticsAndCoatings.png" \
          "$OutDir/AutomotiveUvSurfaceDetail.png" \
          "$OutDir/AutomotiveDispersionAndTir.png" \
          "$OutDir/AutomotiveLightingOptics.png" \
          "$OutDir/AutomotivePaintFlakeFlopComparison.png"
printf '[AutomotivePreview] GREEN — standalone automotive images written to %s\n' "$OutDir"
