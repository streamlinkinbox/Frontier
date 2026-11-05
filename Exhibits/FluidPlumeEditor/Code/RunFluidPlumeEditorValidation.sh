#!/bin/bash
# 🧩 The Smoke & fire editor checked as a user meets it: builds nothing, runs FluidPlumeEditorValidation from a configured build folder and copies the
#    evidence into this exhibit.
#
#      Exhibits/FluidPlumeEditor/Code/RunFluidPlumeEditorValidation.sh <build folder>
#
#    The build folder holds FluidPlumeEditorValidation (ninja FluidPlumeEditorValidation; the whole UI library and Jolt build with it, which takes a while).
#    The validation runs from the repository root, as its legacy fixture is Exhibits/EverestInterface/Content/Simulation.levelspace, and needs no GPU:
#    the window is drawn by Dear ImGui on the CPU raster of Exhibits/ControlCentre/Code/CpuRasterization.h.
set -euo pipefail
BUILD="$(cd "${1:?usage: RunFluidPlumeEditorValidation.sh <build folder>}" && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
EXHIBIT="$ROOT/Exhibits/FluidPlumeEditor"
EVIDENCE="$BUILD/fluid-plume-editor-evidence"
cd "$ROOT"

rm -rf "$EVIDENCE"
mkdir -p "$EVIDENCE" "$EXHIBIT/Images" "$EXHIBIT/Results"
"$BUILD/FluidPlumeEditorValidation" EngineContent Projects/Everest "$EVIDENCE" | tail -2

cp "$EVIDENCE"/*.png "$EXHIBIT/Images/"
cp "$EVIDENCE/Validation.tsv" "$EXHIBIT/Results/Validation.tsv"
echo "Copied $(ls "$EVIDENCE"/*.png | wc -l) images and Validation.tsv to $EXHIBIT"
