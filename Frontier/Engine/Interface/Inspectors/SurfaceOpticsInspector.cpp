/*==========================================================================================================================================
                                                         SURFACEOPTICSINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 SurfaceOpticsConfiguration: albedo, roughness and metalness.

#include "Engine/Interface/Inspectors/SurfaceOpticsInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void SurfaceOpticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Shadows", "Direct ray/SDF", &Parameters.CastShadows});
    OutSwitches.push_back({"Indirect GI", "Bounce history", &Parameters.ReceiveGlobalIllumination});
}

void SurfaceOpticsInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructColourRow(Palette, Faces, "##AlbedoRgb", "Base Albedo", Parameters.AlbedoRgb);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Rough", "Roughness", Parameters.Roughness, 0.02f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Metal", "Metallic", Parameters.Metallic, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Ior", "Index of Refraction", Parameters.IndexOfRefraction, 1.0f, 2.6f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void SurfaceOpticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void SurfaceOpticsInspector::ConstructInspector() {}

#endif

} // namespace Frontier
