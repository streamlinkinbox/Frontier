#!/usr/bin/env bash
# Build gate for the standalone SolidArc C++ CAD/modelling tool.
#
# SolidArc intentionally lives under Editor/AuthoringTools/Modelling instead of the Project-Zero runtime source batch.
# This gate proves the C++ side still compiles without CMake or external packages: kernel, document, console,
# interaction, software-raster presentation, shared outliner adapter, console executable, and every C++ verification TU.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

Compiler="${CXX:-g++}"
if ! command -v "$Compiler" >/dev/null 2>&1; then
    echo "[SolidArc] SKIPPED — no C++ compiler ($Compiler) on PATH"
    exit 0
fi

Root="Editor/AuthoringTools/Modelling/SolidArc"
if [ ! -d "$Root" ]; then
    echo "[SolidArc] RED — $Root is missing"
    exit 1
fi

Work="$(mktemp -d /tmp/SolidArcGate.XXXXXX)"
trap 'rm -rf "$Work"' EXIT
mkdir -p "$Work/obj" "$Work/proofs"

Flags=(-std=c++20 -Wall -Wextra -Wpedantic -Wno-unused-function -I"$Root" -I"$Root/Presentation" -DSOLIDARC_PROOF_FOLDER="\"$Work/proofs\"")
Core=(
    Kernel/CurveSpecification.cpp
    Kernel/SurfaceSpecification.cpp
    Kernel/TopologySpecification.cpp
    Kernel/SkinSolver.cpp
    Kernel/IntersectionSolver.cpp
    Kernel/FairPatchSolver.cpp
    Kernel/ProfileSolver.cpp
    Kernel/ConstraintSolver.cpp
    Kernel/ConstraintGraph.cpp
    Kernel/MirrorSolver.cpp
    Kernel/BlendSolver.cpp
    Presentation/SoftwareRaster.cpp
    Presentation/ScenePresentation.cpp
    Interaction/CameraProjection.cpp
    Interaction/SnapResolution.cpp
    Interaction/InputEvent.cpp
    Interaction/HotkeyChart.cpp
    Interaction/ToolSession.cpp
    Interaction/TransformGizmo.cpp
    Document/SceneDocument.cpp
    Document/FigureRecipe.cpp
    Document/UndoSequence.cpp
    Console/CommandCodec.cpp
    Console/ConsoleHost.cpp
    Console/ConsoleInteraction.cpp
    Console/ConsoleSelection.cpp
    Editor/SolidArcOutlinerAdapter.cpp
)

Objects=()
for Rel in "${Core[@]}"; do
    Obj="$Work/obj/${Rel//\//_}.o"
    "$Compiler" "${Flags[@]}" -c "$Root/$Rel" -o "$Obj"
    Objects+=("$Obj")
done

ConsoleObj="$Work/obj/SolidArcConsole.o"
"$Compiler" "${Flags[@]}" -c "$Root/Console/SolidArcConsole.cpp" -o "$ConsoleObj"
"$Compiler" "${Objects[@]}" "$ConsoleObj" -o "$Work/SolidArc"
"$Work/SolidArc" --help >/dev/null

echo "[SolidArc] console target links and starts"

for Sample in Samples/ToyCar.arc Samples/ToySailboat.arc Samples/ToyBiplane.arc; do
    if [ ! -s "$Root/$Sample" ]; then
        echo "[SolidArc] RED — missing sample script $Sample"
        exit 1
    fi
done
echo "[SolidArc] sample .arc scripts present: ToyCar, ToySailboat, ToyBiplane"

cat > "$Work/SolidArcOutlinerProof.cpp" <<'PROOF'
#include "Editor/SolidArcOutlinerAdapter.h"
#include <cstdio>
#include <cstring>

int main()
{
    Frontier::ConsoleHost Host("/tmp/solidarc-outliner-proof", 320, 220);
    if (!Host.Execute("box (0,0,0) (1,1,1) --name=ProofBox")) return 2;
    if (!Host.Execute("line (0,0) (2,0) --name=ProofLine")) return 3;

    Frontier::EditorInstance Rows[Frontier::kMaxEditorInstances] = {};
    Frontier::SolidArcOutlinerBinding Bindings[Frontier::kMaxEditorInstances] = {};
    Frontier::EditorReadout Readout = {};
    const uint32_t Count = Frontier::BuildSolidArcOutliner(Host, Rows, Bindings, Frontier::kMaxEditorInstances, &Readout);
    if (Count < 8u) return 4;
    bool SawSketches = false, SawBodies = false, SawSurfaces = false, SawConstruction = false, SawDimensions = false;
    bool SawBody = false, SawCurve = false;
    uint32_t BodyRow = Frontier::kNoEditorInstance;
    for (uint32_t I = 0; I < Count; ++I)
    {
        SawSketches     = SawSketches     || std::strcmp(Rows[I].Label, "Sketches") == 0;
        SawBodies       = SawBodies       || std::strcmp(Rows[I].Label, "Bodies") == 0;
        SawSurfaces     = SawSurfaces     || std::strcmp(Rows[I].Label, "Surfaces") == 0;
        SawConstruction = SawConstruction || std::strcmp(Rows[I].Label, "Construction") == 0;
        SawDimensions   = SawDimensions   || std::strcmp(Rows[I].Label, "Dimensions") == 0;
        SawBody         = SawBody         || std::strcmp(Rows[I].Label, "ProofBox") == 0;
        SawCurve        = SawCurve        || std::strcmp(Rows[I].Label, "ProofLine") == 0;
        if (std::strcmp(Rows[I].Label, "ProofBox") == 0) BodyRow = I;
    }
    if (!SawSketches || !SawBodies || !SawSurfaces || !SawConstruction || !SawDimensions || !SawBody || !SawCurve || BodyRow == Frontier::kNoEditorInstance) return 5;
    if (Bindings[BodyRow].RowRole != Frontier::SolidArcOutlinerBinding::Role::Figure) return 6;
    Frontier::EditorSheet Sheet = {};
    if (!Frontier::BuildSolidArcInspectorSheet(Host, Bindings[BodyRow], &Sheet)) return 7;
    bool SawIdentity = false, SawGeometry = false, SawBounds = false;
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
    {
        SawIdentity = SawIdentity || std::strcmp(Sheet.Groups[G].Title, "Identity") == 0;
        SawGeometry = SawGeometry || std::strcmp(Sheet.Groups[G].Title, "CAD geometry") == 0;
        SawBounds   = SawBounds   || std::strcmp(Sheet.Groups[G].Title, "Bounds") == 0;
    }
    if (!SawIdentity || !SawGeometry || !SawBounds) return 8;
    Rows[BodyRow].Visible = false;
    Frontier::ApplySolidArcOutlinerVisibility(Host, Rows, Bindings, Count);
    Frontier::SceneFigure* Box = Host.Document().Find("ProofBox");
    if (Box == nullptr || !Box->Hidden) return 9;
    std::printf("[SolidArc] outliner proof mapped %u rows; CAD folders and inspector sheet write back\n", Count);
    return 0;
}
PROOF
ProofObj="$Work/obj/SolidArcOutlinerProof.o"
"$Compiler" "${Flags[@]}" -I. -c "$Work/SolidArcOutlinerProof.cpp" -o "$ProofObj"
"$Compiler" "${Objects[@]}" "$ProofObj" -o "$Work/SolidArcOutlinerProof"
"$Work/SolidArcOutlinerProof"

cat > "$Work/Makefile" <<EOF
CXX ?= g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Wno-unused-function -I$Root -I$Root/Presentation -DSOLIDARC_PROOF_FOLDER=\\"$Work/proofs\\"
all:
EOF

while IFS= read -r Source; do
    Name="$(basename "$Source" .cpp)"
    echo "$Work/obj/${Name}.o: $Source" >> "$Work/Makefile"
    echo -e "\t\$(CXX) \$(CXXFLAGS) -c \$< -o \$@" >> "$Work/Makefile"
    echo "all: $Work/obj/${Name}.o" >> "$Work/Makefile"
done < <(find "$Root/Verification" -maxdepth 1 -name '*Verification.cpp' | sort)

make -j"$(nproc)" -f "$Work/Makefile"

VerifyCount=$(find "$Root/Verification" -maxdepth 1 -name '*Verification.cpp' | wc -l)
echo "[SolidArc] compiled $VerifyCount C++ verification translation units"
echo "[SolidArc] GREEN — C++ side builds without external packages"
