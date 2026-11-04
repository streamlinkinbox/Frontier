/*==========================================================================================================================================
                                                          ILLUMINATIONSECTION.CPP
==========================================================================================================================================*/
// 🧩 Global illumination: indirect light and its path bounces, next-event estimation and Russian roulette, the ReSTIR reuse, the
//    light intensities and the surface cache.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

bool IlluminationSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    if (Reading.RayTraced) std::snprintf(Status, sizeof(Status), "%dB \xC2\xB7 M%d", P.MaxBounces, P.InitialCandidates);
    else std::snprintf(Status, sizeof(Status), "%s", Reading.Analytic ? "Analytic" : "Distance field");
    BeginSection(Page, "Global illumination", "Sparkles", Status,
                 Reading.RayTraced ? "ReSTIR GI: paths traced through the BVH from every trace pixel, reused across refreshes and neighbours."
                                   : "The distance-field gather: cone steps through the scene field, light read from the surface cards.");
    const char* Lighting = RenderAvailability::Lighting(P);
    const char* Indirect = RenderAvailability::Indirect(P);
    const char* Paths    = Indirect != nullptr ? Indirect : RenderAvailability::RayTracedOnly(P);
    bool Changed = Switch(Page, "GlobalIllumination", "Indirect global illumination", P.GlobalIllumination, Lighting,
                          "ReSTIR GI (RTX) or the distance-field gather (GTX)");
    Changed |= Count(Page, "MaxBounces", "Multi-bounce \xC2\xB7 path bounces", P.MaxBounces, 1, 4, "B", Paths);
    Changed |= Switch(Page, "MultiBounceNee", "Multi-bounce NEE + RIS", P.MultiBounceNee, Paths, "Sun, fire and lamp light sampled at every path vertex");
    Changed |= Switch(Page, "RussianRoulette", "Russian roulette", P.RussianRoulette, Paths, "Throughput-weighted, unbiased path termination");

    SubHeading(Page, "ReSTIR reuse");
    Changed |= Switch(Page, "TemporalReuse", "Temporal reuse", P.TemporalReuse, Indirect, "Reservoirs carried through motion vectors");
    Changed |= Switch(Page, "SpatialReuse", "Spatial reuse", P.SpatialReuse, Indirect, "Neighbouring reservoirs merged per pixel");
    Changed |= Switch(Page, "VisibilityReuse", "Visibility validation", P.VisibilityReuse, Paths, "Reused samples re-traced for occlusion");
    Changed |= Switch(Page, "RadianceRefresh", "Refresh reused radiance", P.RadianceRefresh, Indirect, "Reused samples re-shaded under this refresh's light");
    const char* Spatial  = Indirect != nullptr ? Indirect : (P.SpatialReuse ? nullptr : "Spatial reuse is off");
    const char* Temporal = Indirect != nullptr ? Indirect : (P.TemporalReuse ? nullptr : "Temporal reuse is off");
    Changed |= Count(Page, "SpatialCount", "Spatial candidates", P.SpatialCount, 0, 8, "", Spatial);
    Changed |= Amount(Page, "SpatialRadius", "Spatial radius", P.SpatialRadius, 2.0f, 48.0f, 1.0f, "px", Spatial);
    Changed |= Amount(Page, "TemporalTallyLimit", "Temporal M limit", P.TemporalTallyLimit, 1.0f, 40.0f, 1.0f, "", Temporal,
                      "Low: responsive to change; high: stable, slower to forget");

    SubHeading(Page, "Intensity");
    Changed |= Amount(Page, "IndirectScale", "Indirect intensity", P.IndirectScale, 0.0f, 3.0f, 0.05f, "\xC3\x97", Indirect);
    Changed |= Amount(Page, "SkyScale", "Sky light", P.SkyScale, 0.0f, 3.0f, 0.05f, "\xC3\x97", Lighting);
    Changed |= Amount(Page, "IncandescenceScale", "Emissive surfaces", P.IncandescenceScale, 0.0f, 3.0f, 0.05f, "\xC3\x97", Lighting);

    SubHeading(Page, "Surface cache");
    char CardNote[96];
    if (P.Measured.SurfaceCards > 0) std::snprintf(CardNote, sizeof(CardNote), "Multi-bounce light inside the surface cache (%u cards)", P.Measured.SurfaceCards);
    else std::snprintf(CardNote, sizeof(CardNote), "Multi-bounce light inside the surface cache");
    Changed |= Switch(Page, "CardRadiosity", "Card radiosity", P.CardRadiosity, Lighting, CardNote);
    const char* Tiles = Lighting != nullptr ? Lighting : (P.CardRadiosity ? nullptr : "Card radiosity is off");
    char TileLabel[64];
    std::snprintf(TileLabel, sizeof(TileLabel), "Static tiles per refresh \xC2\xB7 0 = the grade's %u", Reading.Grade->TileBudget);
    Changed |= Count(Page, "CardTileBudget", TileLabel, P.CardTileBudget, 0, 8192, "", Tiles);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
