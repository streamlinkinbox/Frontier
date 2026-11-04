/*==========================================================================================================================================
                                                        PARTICLEEDITOR.CPP
==========================================================================================================================================*/
// 🧩 Standalone GPU particle dynamics authoring editor bound to ParticleInspector.

#include "Engine/Interface/ParticleEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cmath>

namespace Frontier
{

void ParticleEditor::ConstructInterface(RecordToken Token, RecordInspector& Inspector)
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    DiagramControls::ConstructEditorModalFrame(
        "ParticleEditor — Ballistic & Precipitation Dynamics", "Sparkles", Token, Inspector,
        [&](ImDrawList* DrawList, ImVec2 Min, ImVec2 Max) {
            const ImVec2 Centre((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.52f);
            const ImU32  Lilac = ParseHexSpectrum("#b494f4", Palette.ThemeAccent);
            for (int Index = 0; Index < 44; ++Index)
            {
                const float  Theta = -0.35f - (static_cast<float>(Index % 17) / 17.0f) * 2.2f;
                const float  R     = 24.0f + static_cast<float>((Index * 29) % 130);
                const ImVec2 P0(Centre.x + std::cos(Theta) * R, Centre.y + 40.0f + std::sin(Theta) * R * 0.78f);
                const ImVec2 P1(P0.x + std::cos(Theta) * 11.0f, P0.y + std::sin(Theta) * 11.0f + 4.0f);
                DrawList->AddLine(P0, P1, Index % 3 == 0 ? Palette.WarmAmber : Lilac, 1.6f);
            }
            DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(Min.x + 16.0f, Min.y + 14.0f),
                              Palette.TextSecondary, "GPU BALLISTIC TRAJECTORIES · AERODYNAMIC DRAG & BOUNCE");
        });
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void ParticleEditor::ConstructInterface(RecordToken, RecordInspector&) {}
} // namespace Frontier

#endif
