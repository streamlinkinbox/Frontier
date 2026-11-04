/*==========================================================================================================================================
                                                           FRACTUREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 FractureConfiguration: Voronoi fracture, with its FractureEditor.

#include "Engine/Interface/Inspectors/FractureInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/FractureEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void FractureInspector::ConstructEditor(RecordToken Token)
{
    FractureEditor::ConstructInterface(Token, *this);
}

void FractureInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Armed", "Impulse trigger", &Parameters.FractureArmed});
    OutSwitches.push_back({"Impact Dust", "Debris burst", &Parameters.ImpactDustBurst});
}

void FractureInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##VorCells", "Voronoi Cell Count", Parameters.VoronoiCellCount, 4u, 256u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ImpThr", "Impulse Threshold", Parameters.ImpulseThresholdNs, 1.0f, 500.0f, "%.1f N*s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RadBias", "Impact Radial Bias", Parameters.ImpactRadialBias, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DebrisLife", "Debris Lifetime", Parameters.DebrisLifetimeSeconds, 1.0f, 60.0f, "%.1f s");
    if (InspectorControls::ConstructActionButton(Palette, Faces, "##PreFracture", "Recompute Convex Cells"))
    {
        ++Parameters.PrefractureTriggerCount;
        Changed = true;
    }
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void FractureInspector::ConstructEditor(RecordToken) {}
void FractureInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FractureInspector::ConstructInspector() {}

#endif

} // namespace Frontier
