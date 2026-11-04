/*==========================================================================================================================================
                                                            SPATIALINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 SpatialConfiguration: position, orientation and scale.

#include "Engine/Interface/Inspectors/SpatialInspector.h"

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

void SpatialInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    InspectorControls::ConstructSectionDivider(Palette, Faces, "TRANSLATION", "Metres");
    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosX", "Position X", Parameters.PositionMetres[0], -200.0f, 200.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosY", "Position Y", Parameters.PositionMetres[1], -50.0f, 200.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosZ", "Position Z", Parameters.PositionMetres[2], -200.0f, 200.0f, "%.2f m");

    InspectorControls::ConstructSectionDivider(Palette, Faces, "ORIENTATION & SCALE");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotX", "Pitch", Parameters.OrientationDegrees[0], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotY", "Yaw", Parameters.OrientationDegrees[1], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotZ", "Roll", Parameters.OrientationDegrees[2], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclX", "Scale X", Parameters.Scale[0], 0.05f, 25.0f, "%.2f x");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclY", "Scale Y", Parameters.Scale[1], 0.05f, 25.0f, "%.2f x");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclZ", "Scale Z", Parameters.Scale[2], 0.05f, 25.0f, "%.2f x");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void SpatialInspector::ConstructInspector() {}

#endif

} // namespace Frontier
