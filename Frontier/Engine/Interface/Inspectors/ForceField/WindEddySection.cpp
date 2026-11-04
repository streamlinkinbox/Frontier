/*==========================================================================================================================================
                                                           WINDEDDYSECTION.CPP
==========================================================================================================================================*/
// 🧩 Eddies: the curl turbulence the wind carries, previewed with the shared force-field visualization over eight eddy scales of
//    the wind alone (coloured by vorticity, at 10 m, on the field's clock), with the turbulence, its scale and its seed.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"

#include <algorithm>
#include <cstdio>

namespace Frontier
{

namespace
{

// 📝 The preview samples the wind alone (no localized force) with its eddies on, across eight eddy scales around the origin.
void ConstructEddyPreview(ControlColumn& Page, const WindConfiguration& Wind, const WindPageReading& Reading)
{
    if (Reading.EddyPreview == nullptr) return;
    ForceFieldVisualization& Preview  = *Reading.EddyPreview;
    ForceFieldConfiguration  Controls = Reading.Forces != nullptr ? *Reading.Forces : ForceFieldConfiguration{};
    WindConfiguration        Alone    = Wind;
    Controls.IncludeTurbulence        = true;
    Alone.Enabled                     = true;
    ForceFieldSpace Space;
    ConfigureAtmosphericFlow(Space, Controls, Alone, Reading.TimelineSeconds);
    Preview.ConfigureField(Space, Wind.Revision * 7919ull + Controls.Revision);
    FieldVisualizationOptions& Options = Preview.Options;
    Options.Quantity                   = ForceFieldQuantity::Vorticity;
    Options.Contribution               = ForceContribution::Flow;
    Options.Arrows                     = true;
    Options.ArrowSpacing               = 30.0f;
    Options.Tracers                    = true;
    Options.TracerCount                = 140;
    Options.ShowScaleBar               = true;
    Options.ShowLegend                 = false;
    Options.ShowSources                = false;
    Options.ShowGroundGrid             = false;
    Options.ShowLabels                 = false;
    Options.CellPixels                 = 4.0f;
    Options.SliceHeight                = Wind.GroundHeightMetres + 10.0;
    Options.RefreshSeconds             = 0.12;
    Options.HasExtent                  = false;
    Preview.View.Mode                  = FieldViewMode::Map;
    Preview.View.CentreX               = 0.0;
    Preview.View.CentreZ               = 0.0;
    Preview.View.Span                  = std::clamp(static_cast<double>(Wind.TurbulenceScaleMetres) * 8.0, 60.0, 8000.0);
    const float  Scale                 = Page.QueryScale();
    const ImVec2 Top                   = Page.Space(220.0f, 4.0f, 12.0f);
    const ImVec2 Bottom(Top.x + Page.QueryWidth(), Top.y + 220.0f * Scale);
    Preview.Render(Page.QueryDrawList(), Page.QueryFaces(), Top, Bottom, Reading.TimelineSeconds, Scale);
    // 📝 The first look waits for its lattice, so the page never opens on an empty plate.
    if (Preview.QueryStatistics().Refreshes == 0)
    {
        Preview.SettleSampling();
        Preview.Render(Page.QueryDrawList(), Page.QueryFaces(), Top, Bottom, Reading.TimelineSeconds, Scale);
    }
    Page.QueryDrawList()->AddRect(Top, Bottom, ResolveCssColour(0xFFFFFF1A), 12.0f * Scale, 0, Scale);
}

} // namespace

bool WindEddySection::Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading)
{
    using namespace SectionRows;
    char Scale[32], Status[48];
    FormatDistance(Wind.TurbulenceScaleMetres, Scale, sizeof(Scale));
    std::snprintf(Status, sizeof(Status), "%.2f \xC2\xB7 %s", Wind.CurlNoiseTurbulence, Scale);
    BeginSection(Page, "Turbulence", "Swirl", Status,
                 "Curl eddies: divergence-free, drifting downwind at 0.35 \xC3\x97 the wind. Their strength scales with the wind "
                 "(0.45 \xC3\x97 speed \xC3\x97 turbulence); the preview colours their vorticity \xCF\x89.");
    const bool Included = Reading.Forces == nullptr || Reading.Forces->IncludeTurbulence;
    ConstructEddyPreview(Page, Wind, Reading);
    if (!Included) CaptionText::Construct(Page, "The Force Field leaves the eddies out (its Eddies switch); the preview shows them anyway.", 0.0f, 8.0f);
    bool Changed = SliderRow::Construct(Page, "WindTurbulence", "Turbulence", Wind.CurlNoiseTurbulence, 0, 1, 0.01f, "");
    double Length = Wind.TurbulenceScaleMetres;
    if (Distance(Page, "TurbulenceScale", "Turbulence scale", Length, 4.0, 1000.0))
    {
        Wind.TurbulenceScaleMetres = static_cast<float>(Length);
        Changed                    = true;
    }
    int Seed = static_cast<int>(Wind.Seed);
    if (Count(Page, "WindSeed", "Seed \xC2\xB7 proposed", Seed, 1, 9999, ""))
    {
        Wind.Seed = static_cast<uint32_t>(Seed);
        Changed   = true;
    }
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
