/*==========================================================================================================================================
                                                        RENDERSETTINGSINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The Render settings inspector: the whole renderer as one Outliner component (the RenderPipeline record) and one page, after the
//    approved HTML prototype (Experimental/RenderSettingsInspector).

#include "Engine/Interface/Inspectors/RenderSettingsInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsReadings.h"
#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

constexpr const char* SectionLabels[RenderSettingsInspector::SectionCount] = {"Sampling", "Lighting", "Reflections", "Shadows", "Denoise",
                                                                              "Exposure", "Geometry", "Views",    "Timing"};
constexpr int UpperTabs = 4;   // [-] Sampling · Lighting · Reflections · Shadows on the first row, the other five on the second

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER ROW
//------------------------------------------------------------------------------------------------------------------------

std::string RenderSettingsInspector::QueryDirectoryReadout() const
{
    const RenderSettingsReading Reading = ResolveRenderSettingsReading(Parameters);
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%s \xC2\xB7 %u\xC3\x97%u", Parameters.AnalyticIllumination ? "Analytic" : (Parameters.HardwareRayQueryTier ? "RTX" : "GTX"),
                  Reading.Render[0], Reading.Render[1]);
    return Text;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HERO
//------------------------------------------------------------------------------------------------------------------------

bool RenderSettingsInspector::ConstructRenderer(ControlColumn& Page, const RenderSettingsReading& Reading)
{
    static const char* const Renderers[2] = {"RTX \xC2\xB7 ReSTIR", "GTX \xC2\xB7 Distance field"};
    int Renderer = Parameters.HardwareRayQueryTier ? 0 : 1;
    const bool Changed = TabStrip::Construct(Page, "Renderer", Renderers, 2, Renderer);
    if (Changed) Parameters.HardwareRayQueryTier = Renderer == 0;
    if (Reading.Analytic)
        CaptionText::Construct(Page, "The analytic illumination is showing (Debug views, or the viewport bar): the lighting settings below wait for the "
                                     "global illumination to return.", 0.0f, 12.0f);
    return Changed;
}

// 📝 The measured refresh time against the budget, with the recent refreshes charted under the dashed budget line.
void RenderSettingsInspector::ConstructTimingCard(ControlColumn& Page)
{
    const ControlTheme& Theme  = Page.QueryTheme();
    const auto&         M      = Parameters.Measured;
    const double        Budget = 1000.0 / std::max(1, Parameters.TargetRate);
    const bool          Within = M.Presenting && M.RefreshMs <= Budget;
    CardLook Look;
    Look.Radius        = 24.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 16.0f;
    Look.PaddingBottom = 12.0f;
    Look.MarginBottom  = 14.0f;
    Look.HeadingGlyph  = M.Presenting ? (Within ? "Check" : "TriangleAlert") : "Timer";
    Page.BeginCard(Look, !M.Presenting ? "Waiting for the viewport" : (Within ? "Within budget" : "Over budget"), "MEASURED");
    char Figure[32], Unit[48];
    std::snprintf(Figure, sizeof(Figure), "%.1f", M.Presenting ? M.RefreshMs : 0.0);
    const double PerSecond = M.Presenting && M.RefreshMs > 0.0 ? 1000.0 / M.RefreshMs : 0.0;
    std::snprintf(Unit, sizeof(Unit), PerSecond < 10.0 ? "ms \xC2\xB7 %.2f per second" : "ms \xC2\xB7 %.0f per second", PerSecond);
    ReadoutBlock::Construct(Page, "Refresh time", M.Presenting ? Figure : "\xE2\x80\x94", Unit, 34.0f);

    DiagramPen Pen = DiagramCanvas::Begin(Page, "RefreshChart", 640, 190, 4, 6);
    const uint32_t Count = std::min<uint32_t>(M.HistoryCount, static_cast<uint32_t>(M.HistoryMs.size()));
    float Ceiling = static_cast<float>(Budget) * 1.6f;
    for (uint32_t Index = 0; Index < Count; ++Index) Ceiling = std::max(Ceiling, M.HistoryMs[Index] * 1.15f);
    const auto Height = [&](double Milliseconds) { return 170.0f - static_cast<float>(Milliseconds / Ceiling) * 160.0f; };
    for (int Guide = 1; Guide <= 3; ++Guide)
    {
        char Label[24];
        const double Level = Ceiling * Guide / 3.0;
        std::snprintf(Label, sizeof(Label), "%.0f ms", Level);
        Pen.Line({0, Height(Level)}, {580, Height(Level)}, ResolveCssColour(0xFFFFFF10));
        Pen.Text({640, Height(Level) + 6}, Label, Theme.Muted, 15, 1);
    }
    char BudgetLabel[32];
    std::snprintf(BudgetLabel, sizeof(BudgetLabel), "Budget %.1f ms", Budget);
    Pen.Line({0, Height(Budget)}, {580, Height(Budget)}, ResolveCssColour(0xFFFFFF80), 1.5f, 8, 6);
    Pen.Text({4, Height(Budget) - 8}, BudgetLabel, Theme.Muted, 15, -1);
    if (Count >= 2)
    {
        // 📝 The ring in time order, oldest at the left; RefreshCount says where the newest entry sits.
        std::vector<ImVec2> Line;
        const uint32_t      Newest = static_cast<uint32_t>((M.RefreshCount + M.HistoryMs.size() - 1) % M.HistoryMs.size());
        for (uint32_t Step = 0; Step < Count; ++Step)
        {
            const uint32_t Index = (Newest + static_cast<uint32_t>(M.HistoryMs.size()) - (Count - 1 - Step)) % static_cast<uint32_t>(M.HistoryMs.size());
            Line.push_back({580.0f * Step / static_cast<float>(M.HistoryMs.size() - 1) + 580.0f * (M.HistoryMs.size() - Count) / (M.HistoryMs.size() - 1),
                            Height(M.HistoryMs[Index])});
        }
        Pen.Path(Line, ResolveCssColour(0xF0F0F0E6), 2.0f);
        Pen.Circle(Line.back(), 5.0f, ResolveCssColour(0xFFFFFFFF), ResolveCssColour(0x00000080));
    }
    else
    {
        Pen.Text({290, 100}, "The chart fills as the viewport refreshes", Theme.Muted, 16, 0);
    }
    Page.EndCard();
}

void RenderSettingsInspector::ConstructReadouts(ControlColumn& Page, const RenderSettingsReading& Reading)
{
    char Render[32], Trace[32], Paths[16], Bounces[16], Rays[16], Pixels[16], Scale[16];
    std::snprintf(Render, sizeof(Render), "%u\xC3\x97%u", Reading.Render[0], Reading.Render[1]);
    std::snprintf(Trace, sizeof(Trace), "%u\xC3\x97%u", Reading.Trace[0], Reading.Trace[1]);
    std::snprintf(Paths, sizeof(Paths), "%d", Reading.RayTraced ? Parameters.InitialCandidates : 0);
    std::snprintf(Bounces, sizeof(Bounces), "%d\xC2\xB7%d", Parameters.MaxBounces, Parameters.ReflectionBounces);
    FormatRayCount(Reading.TotalRays, Rays, sizeof(Rays));
    std::snprintf(Pixels, sizeof(Pixels), "%.2f", static_cast<double>(Reading.Render[0]) * Reading.Render[1] / 1.0e6);
    std::snprintf(Scale, sizeof(Scale), "%.2f", Parameters.RenderScale);
    const char* TraceUnit = Parameters.TraceScale <= 1 ? "full" : (Parameters.TraceScale >= 4 ? "\xC2\xBC" : "\xC2\xBD");
    const MetricTile Extents[2] = {{Reading.Measured ? "Render \xC2\xB7 measured" : "Render", Render, "px"}, {"Trace", Trace, TraceUnit}};
    const MetricTile Work[3]    = {{"Rays per refresh, at most", Reading.RayTraced ? Rays : "0", ""}, {"Render pixels", Pixels, "M"},
                                   {"Viewport scale", Scale, "\xC3\x97"}};
    const MetricTile Depth[3]    = {{"Paths per pixel", Paths, "M"}, {"Bounces GI \xC2\xB7 reflections", Bounces, ""}, {"Quality", Reading.Grade->Name, ""}};
    MetricTiles::Construct(Page, Extents, 2, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Work, 3, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Depth, 3, MetricTiles::NoLead);
}

// 📝 Nine sections on two rows of pills sharing one selection (the column is too narrow for nine equal pills on one row).
void RenderSettingsInspector::ConstructSectionTabs(ControlColumn& Page)
{
    int Upper = ActiveSection < UpperTabs ? ActiveSection : -1;
    if (TabStrip::Construct(Page, "SectionsUpper", SectionLabels, UpperTabs, Upper) && Upper >= 0) ActiveSection = Upper;
    int Lower = ActiveSection >= UpperTabs ? ActiveSection - UpperTabs : -1;
    if (TabStrip::Construct(Page, "SectionsLower", SectionLabels + UpperTabs, SectionCount - UpperTabs, Lower) && Lower >= 0)
        ActiveSection = UpperTabs + Lower;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void RenderSettingsInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool Changed = false;
    if (PageHeading::Construct(Page, "RenderSettingsHeader", "RENDERING / PIPELINE", "Render settings", "Restore demo",
                               "The renderer as one component: global illumination, ray tracing, reflections, resolution, geometry and debug views.",
                               "Every setting back to the VehicleHMI demo's (the quality grade, renderer and debug view stay)"))
    {
        // 📝 The demo's settings; what selects the scene (grade, renderer), what is being looked at and the measurements stay.
        RenderPipelineConfiguration Demo;
        Demo.Enabled              = Parameters.Enabled;
        Demo.HardwareRayQueryTier = Parameters.HardwareRayQueryTier;
        Demo.QualityTier          = Parameters.QualityTier;
        Demo.InspectionModeIndex  = Parameters.InspectionModeIndex;
        Demo.AnalyticIllumination = Parameters.AnalyticIllumination;
        Demo.TargetRate           = Parameters.TargetRate;
        Demo.Measured             = Parameters.Measured;
        Demo.Revision             = Parameters.Revision;
        Parameters                = Demo;
        Changed                   = true;
    }
    const RenderSettingsReading Reading = ResolveRenderSettingsReading(Parameters);
    Changed |= ConstructRenderer(Page, Reading);
    ConstructTimingCard(Page);
    ConstructReadouts(Page, Reading);
    ConstructSectionTabs(Page);
    switch (ActiveSection)
    {
        case 0: Changed |= SamplingSection::Construct(Page, Parameters, Reading); break;
        case 1: Changed |= IlluminationSection::Construct(Page, Parameters, Reading); break;
        case 2: Changed |= ReflectionSection::Construct(Page, Parameters, Reading); break;
        case 3: Changed |= ShadowSection::Construct(Page, Parameters, Reading); break;
        case 4: Changed |= DenoiseSection::Construct(Page, Parameters, Reading); break;
        case 5: Changed |= ExposureSection::Construct(Page, Parameters, Reading); break;
        case 6: Changed |= GeometrySection::Construct(Page, Parameters, Reading); break;
        case 7: Changed |= ViewSection::Construct(Page, Parameters, Reading); break;
        default: Changed |= TimingSection::Construct(Page, Parameters, Reading); break;
    }
    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 RenderPipeline", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
std::string RenderSettingsInspector::QueryDirectoryReadout() const { return {}; }
void        RenderSettingsInspector::ConstructInspector() {}
} // namespace Frontier

#endif
