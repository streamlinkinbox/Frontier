/*==========================================================================================================================================
                                                       FLUIDPLUMEEDITOR.CPP
==========================================================================================================================================*/
// 🧩 Standalone Eulerian fluid plume & blackbody combustion authoring editor bound to FluidPlumeInspector.

#include "Engine/Interface/FluidPlumeEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cmath>

namespace Frontier
{

void FluidPlumeEditor::ConstructInterface(RecordToken Token, RecordInspector& Inspector)
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    DiagramControls::ConstructEditorModalFrame(
        "FluidPlumeEditor — Toroidal Advection & Combustion", "Flame", Token, Inspector,
        [&](ImDrawList* DrawList, ImVec2 Min, ImVec2 Max) {
            const ImVec2 Centre((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.52f);
            DrawList->AddRect(ImVec2(Centre.x - 150.0f, Centre.y - 110.0f),
                              ImVec2(Centre.x + 150.0f, Centre.y + 96.0f),
                              ParseHexSpectrum("#e8833a", Palette.WarmAmber), 6.0f, 0, 1.4f);
            for (int Puff = 0; Puff < 11; ++Puff)
            {
                const float Ratio  = static_cast<float>(Puff) / 10.0f;
                const float SwirlX = std::sin(Ratio * 4.5f + 1.85f) * (18.0f + Ratio * 26.0f);
                const float PuffY  = Centre.y + 72.0f - Ratio * 150.0f;
                const float Radius = 18.0f + Ratio * 32.0f;
                const ImU32 Tint   = Ratio < 0.38f
                                         ? ModulateSpectrumAlpha(ParseHexSpectrum("#ff9f3f", Palette.WarmAmber), 0.38f)
                                         : ModulateSpectrumAlpha(Palette.TextSecondary, 0.16f);
                DrawList->AddCircleFilled(ImVec2(Centre.x + SwirlX, PuffY), Radius, Tint, 24);
            }
            DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(Min.x + 16.0f, Min.y + 14.0f),
                              Palette.TextSecondary, "TOROIDAL EULERIAN PLUME · 128x64x128 · BLACKBODY CORE");
        });
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void FluidPlumeEditor::ConstructInterface(RecordToken, RecordInspector&) {}
} // namespace Frontier

#endif
