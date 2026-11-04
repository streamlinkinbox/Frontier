/*==========================================================================================================================================
                                                         FIELDVECTORSSECTION.CPP
==========================================================================================================================================*/
// 🧩 World vectors: the field as arrows in the 3D scene viewport (WorldVectorOverlay) over an inspection region, and the slice
//    laid over the scene; the arrows sample the same field the media follow.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cmath>
#include <cstdio>

namespace Frontier
{

bool FieldVectorsSection::Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    BeginSection(Page, "World vectors", "Move", Field.Visualize ? "Shown" : "Hidden",
                 "The field in the 3D scene viewport: arrows over an inspection region (perspective-projected samples, clipped to "
                 "the viewport rather than hidden by surfaces), or the slice itself laid over the scene.");
    bool Changed = Switch(Page, "FieldVectors", "Show world vectors", Field.Visualize, nullptr, "Draws the field as arrows in the scene viewport");
    Changed |= Switch(Page, "FieldSceneOverlay", "Slice over the scene", Field.SceneOverlay, nullptr,
                      "The Field view's heatmap and arrows on the ground plane of the scene viewport");
    int Samples = static_cast<int>(Field.GridResolution);
    if (Count(Page, "FieldGrid", "Samples per axis", Samples, 3, 8, ""))
    {
        Field.GridResolution = static_cast<uint32_t>(Samples);
        Changed              = true;
    }
    Changed |= Amount(Page, "FieldArrowTime", "Arrow time scale", Field.ArrowSeconds, 0.05f, 2.0f, 0.05f, "s", nullptr,
                      "Arrow length = velocity \xC3\x97 time scale; the display never changes the field");
    const bool Camera = Reading.Access != nullptr && Reading.Access->CameraReady;
    if (NextButton::Construct(Page, "CentreField", "Place the region ahead of the camera", Camera, "Requires a viewport camera"))
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Field.CentreMetres[Axis] = static_cast<float>(Reading.Access->CameraEye[Axis] + Reading.Access->CameraForward[Axis] * 30.0);
        Changed = true;
    }
    Changed |= AxisFields::Construct(Page, "FieldCentre", "Region centre \xC2\xB7 X / Y / Z (m)", Field.CentreMetres, {-100000, -100000, -10000},
                                     {100000, 100000, 20000});
    Changed |= AxisFields::Construct(Page, "FieldHalfExtent", "Region half extents \xC2\xB7 X / Y / Z (m)", Field.HalfExtentsMetres, {1, 1, 1},
                                     {1000, 1000, 1000});
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
