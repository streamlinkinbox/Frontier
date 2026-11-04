/*==========================================================================================================================================
                                                           PARTICLEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 ParticleConfiguration: GPU particles, with its ParticleEditor.

#include "Engine/Interface/Inspectors/ParticleInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/ParticleEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void ParticleInspector::ConstructEditor(RecordToken Token)
{
    ParticleEditor::ConstructInterface(Token, *this);
}

void ParticleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Precipitation", "Weather volume", &Parameters.GpuWeatherPrecipitation});
    OutSwitches.push_back({"Collisions", "Depth buffer", &Parameters.DepthBufferCollisions});
}

void ParticleInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtSpk", "Emission Rate", Parameters.SparkRatePerSecond, 0.0f, 2000.0f, "%.0f /s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtRain", "Rain Density", Parameters.RainDropDensity, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtSnow", "Snow Density", Parameters.SnowFlakeDensity, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtGrav", "Gravity Scale", Parameters.GravityMultiplier, -2.0f, 3.0f, "%.2f g");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtDrag", "Aerodynamic Drag", Parameters.AerodynamicDrag, 0.0f, 2.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtBnc", "Bounce Restitution", Parameters.BounceRestitution, 0.0f, 1.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void ParticleInspector::ConstructEditor(RecordToken) {}
void ParticleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ParticleInspector::ConstructInspector() {}

#endif

} // namespace Frontier
