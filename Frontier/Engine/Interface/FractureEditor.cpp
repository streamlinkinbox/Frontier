/*==========================================================================================================================================
                                                        FRACTUREEDITOR.CPP
==========================================================================================================================================*/
// 🧩 Standalone Voronoi convex decomposition authoring editor bound to FractureInspector.

#include "Engine/Interface/FractureEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cmath>

namespace Frontier
{

void FractureEditor::ConstructInterface(RecordToken Token, RecordInspector& Inspector)
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    DiagramControls::ConstructEditorModalFrame(
        "FractureEditor — Voronoi Convex Decomposition", "Hammer", Token, Inspector,
        [&](ImDrawList* DrawList, ImVec2 Min, ImVec2 Max) {
            const ImVec2 Centre((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.52f);
            const ImU32  Mint = ParseHexSpectrum("#8fb3a4", Palette.LiveGreen);
            DrawList->AddRect(ImVec2(Centre.x - 135.0f, Centre.y - 105.0f),
                              ImVec2(Centre.x + 135.0f, Centre.y + 105.0f), Mint, 4.0f, 0, 1.8f);
            for (int Cell = 0; Cell < 32; ++Cell)
            {
                const float  Theta = static_cast<float>(Cell) * 2.399963f;
                const float  Dist  = 16.0f + std::sqrt(static_cast<float>(Cell) / 32.0f) * 108.0f;
                const ImVec2 Seed(Centre.x + std::cos(Theta) * Dist * 1.12f,
                                  Centre.y + std::sin(Theta) * Dist * 0.86f);
                DrawList->AddCircleFilled(Seed, 2.4f, Mint, 8);
                DrawList->AddLine(Centre, Seed, ModulateSpectrumAlpha(Mint, 0.28f), 1.0f);
            }
            DrawList->AddCircle(Centre, 28.0f, Palette.WarmAmber, 24, 1.8f);
            DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(Min.x + 16.0f, Min.y + 14.0f),
                              Palette.TextSecondary, "VORONOI CONVEX HULL LATTICE · IMPACT ENVELOPE");
        });
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void FractureEditor::ConstructInterface(RecordToken, RecordInspector&) {}
} // namespace Frontier

#endif
