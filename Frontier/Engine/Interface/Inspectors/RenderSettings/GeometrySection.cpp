/*==========================================================================================================================================
                                                            GEOMETRYSECTION.CPP
==========================================================================================================================================*/
// 🧩 Geometry: the virtualized geometry (cluster nesting of eligible static sets) and its stream slots, the scene's measured counts and
//    the distance-field cascades of the quality grade.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

std::string Thousands(uint64_t Count)
{
    std::string Text = std::to_string(Count);
    for (int Digit = static_cast<int>(Text.size()) - 3; Digit > 0; Digit -= 3) Text.insert(static_cast<size_t>(Digit), ",");
    return Text;
}

} // namespace

bool GeometrySection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    BeginSection(Page, "Geometry", "Boxes", P.VirtualizedGeometry ? "Virtualized" : "Clusters",
                 "The scene the renderer loaded: its triangles, the surface clusters the raster culls, and the distance field.");
    bool Changed = Switch(Page, "VirtualizedGeometry", "Virtualized geometry", P.VirtualizedGeometry, nullptr,
                          "Cluster nesting of eligible static sets: recording calls from 6,452 to 10 (the demo's ?hierarchy=1)");
    Changed |= Count(Page, "VirtualizedStreamSlots", "Stream slots \xC2\xB7 0 = one per page", P.VirtualizedStreamSlots, 0, 64, "",
                     P.VirtualizedGeometry ? nullptr : "Virtualized geometry is off");

    const auto& M = P.Measured;
    const MetricTile Counts[3] = {{"Scene triangles", M.Presenting ? Thousands(M.SceneTriangles) : std::string("\xE2\x80\x94"), ""},
                                  {"Surface clusters", M.Presenting ? Thousands(M.GeometryClusters) : std::string("\xE2\x80\x94"), ""},
                                  {"Surface cards", M.Presenting ? Thousands(M.SurfaceCards) : std::string("\xE2\x80\x94"), ""}};
    MetricTiles::Construct(Page, Counts, 3, MetricTiles::NoLead);

    SubHeading(Page, "Distance field & BVH");
    char Spans[4][24];
    for (size_t Cascade = 0; Cascade < 4; ++Cascade) std::snprintf(Spans[Cascade], sizeof(Spans[Cascade]), "%.0f", Reading.CascadeSpans[Cascade]);
    const MetricTile Cascades[4] = {{"Cascade 0", Spans[0], "m"}, {"Cascade 1", Spans[1], "m"}, {"Cascade 2", Spans[2], "m"}, {"Cascade 3", Spans[3], "m"}};
    MetricTiles::Construct(Page, Cascades, 4, MetricTiles::NoLead);
    char Line[160];
    if (M.Presenting && M.RayTracing)
        std::snprintf(Line, sizeof(Line), "BVH: %u TLAS instances over the static BLAS. Cascades of %u\xC2\xB3 voxels, finest %.3g m (%s grade).", M.TlasInstances,
                      Reading.Grade->ClipResolution, Reading.Grade->FinestVoxel, Reading.Grade->Name);
    else
        std::snprintf(Line, sizeof(Line), "Cascades of %u\xC2\xB3 voxels, finest %.3g m (%s grade); each doubles the last. Debug view 06 shows the field.",
                      Reading.Grade->ClipResolution, Reading.Grade->FinestVoxel, Reading.Grade->Name);
    CaptionText::Construct(Page, Line, 2.0f, 12.0f);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
