/*==========================================================================================================================================
                                                         EVERESTFIELDVIEWPORT.CPP
==========================================================================================================================================*/
// 🧩 The Force Field in the viewport: the Field view (the slice from 100 m to 100 km with its HUD, minimap and timeline lanes),
//    the slice over the rendered scene, and the timeline clock the windowed and triggered forces follow.

#include "EverestWorkspace.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/NativeInspectors.h"
#include "Engine/Interface/ViewportChromeControls.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneWorld.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace Everest
{

namespace
{

struct FieldRecords
{
    Frontier::ForceFieldRecord*               Field = nullptr;
    Frontier::WindRecord*                     Wind  = nullptr;
    std::vector<Frontier::ForceSourceRecord*> Sources;
};

FieldRecords ResolveFieldRecords(Frontier::SceneWorld& World)
{
    FieldRecords Records;
    Records.Field = dynamic_cast<Frontier::ForceFieldRecord*>(World.FindFirstByType("ForceField"));
    Records.Wind  = dynamic_cast<Frontier::WindRecord*>(World.FindFirstByType("Wind"));
    for (const Frontier::SceneRecordBase* Entry : World.FindAllByType("ForceSource"))
        if (auto* Source = dynamic_cast<Frontier::ForceSourceRecord*>(World.FindRecord(Entry->QueryToken()))) Records.Sources.push_back(Source);
    return Records;
}

constexpr const char* RangeLabels[]     = {"100 m", "1 km", "10 km", "100 km"};
constexpr double      RangeSpans[]      = {100.0, 1000.0, 10000.0, 100000.0};
constexpr const char* ViewLabels[]      = {"Scene", "Field"};
constexpr const char* ProjectionNames[] = {"Map", "Oblique"};
constexpr const char* RateLabels[]      = {"\xC3\x97" "1", "\xC3\x97" "10", "\xC3\x97" "60"};
constexpr float       RateValues[]      = {1.0f, 10.0f, 60.0f};

ImU32 ContributionTint(Frontier::ForceContribution Contribution, int Alpha)
{
    const uint32_t Rgb = Frontier::ResolveForceContributionDescriptor(Contribution).ColourRgb;
    return IM_COL32((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}

uint32_t FieldLogCategory()
{
    static const uint32_t Category = Frontier::LogCategories().Register("Forces", Frontier::LogColour::Mint, true);
    return Category;
}

// 📝 A dark rounded panel of the viewport chrome (the prototype's .viewport-hud and .minimap).
void ChromePanel(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    Ink->AddRectFilled(Minimum, Maximum, IM_COL32(10, 12, 16, 214), 10.0f * Scale);
    Ink->AddRect(Minimum, Maximum, IM_COL32(255, 255, 255, 20), 10.0f * Scale, 0, Scale);
}

float MeasureFace(ImFont* Face, float Size, const char* Text)
{
    return Face != nullptr ? Face->CalcTextSizeA(Size, 1e6f, 0.0f, Text).x : ImGui::CalcTextSize(Text).x;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIMELINE CLOCK
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Force Field's clock: windows and triggers follow it, in the editor and in the renderer. It loops at its length; a
//    fired trigger keeps its firing time.
void EverestWorkspace::AdvanceFieldTimeline(double DeltaSeconds)
{
    auto* Record = dynamic_cast<Frontier::ForceFieldRecord*>(QuerySceneWorld().FindFirstByType("ForceField"));
    if (Record == nullptr) return;
    Frontier::ForceFieldConfiguration& Forces = Record->AccessConfiguration();
    if (!Forces.TimelinePlaying || Field.Scrubbing) return;
    const double Duration  = std::max(1.0, static_cast<double>(Forces.TimelineDurationSeconds));
    Forces.TimelineSeconds = std::fmod(std::max(0.0, Forces.TimelineSeconds + std::max(0.0, DeltaSeconds) * Forces.TimelineRate), Duration);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLICE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 One slice from the level's records: the field as the renderer evaluates it (ConfigureAtmosphericFlow, at the timeline's
//    time), the Force Field's view settings and the sources' outlines. False without a Force Field record.
bool EverestWorkspace::ConfigureFieldSlice(Frontier::ForceFieldVisualization& Slice, bool Camera)
{
    const FieldRecords Records = ResolveFieldRecords(QuerySceneWorld());
    if (Records.Field == nullptr) return false;
    const Frontier::ForceFieldConfiguration& Forces = Records.Field->QueryConfiguration();
    Frontier::WindConfiguration              Wind   = Records.Wind != nullptr ? Records.Wind->QueryConfiguration() : Frontier::WindConfiguration{};
    if (Records.Wind == nullptr) Wind.Enabled = false;
    std::vector<Frontier::ForceSourceConfiguration> Sources;
    std::vector<std::string>                        Names;
    int32_t                                         Selected = -1;
    uint64_t Revision = Forces.Revision * 1000003ull + Wind.Revision * 7919ull + Records.Sources.size();
    for (size_t Index = 0; Index < Records.Sources.size(); ++Index)
    {
        Sources.push_back(Records.Sources[Index]->QueryConfiguration());
        Names.push_back(Records.Sources[Index]->QueryRecordName());
        Revision = Revision * 31ull + Sources.back().Revision;
        if (Records.Sources[Index]->QueryToken().Value == OutlinerPanel.QuerySelected().Value) Selected = static_cast<int32_t>(Index);
    }
    Frontier::ForceFieldSpace Space;
    Frontier::ConfigureAtmosphericFlow(Space, Forces, Wind, Forces.TimelineSeconds, Sources);
    Slice.ConfigureField(Space, Revision);

    Frontier::FieldVisualizationOptions& Options = Slice.Options;
    Options.Contribution   = Forces.ViewContribution;
    Options.Quantity       = Forces.ViewContribution == Frontier::ForceContribution::Damp ? Frontier::ForceFieldQuantity::Speed : Forces.Quantity;
    Options.AutomaticScale = Forces.AutomaticScale;
    Options.FixedScale     = Forces.FixedScale;
    Options.Heatmap        = Forces.Heatmap;
    Options.SmoothHeatmap  = Forces.SmoothHeatmap;
    Options.Opacity        = std::clamp(Forces.HeatmapOpacity, 0.0f, 1.0f) * (Camera ? 0.55f : 1.0f);
    Options.Arrows         = Forces.Arrows;
    Options.ArrowSpacing   = Forces.ArrowSpacingPixels;
    Options.Tracers        = Forces.Tracers;
    Options.TracerCount    = Camera ? std::min<uint32_t>(Forces.TracerCount, 240u) : Forces.TracerCount;
    Options.SliceHeight    = Wind.GroundHeightMetres + Forces.SliceHeightMetres;
    Options.CellPixels     = Forces.CellPixels;
    Options.CameraCells    = 64;
    Options.ShowLabels     = Forces.Labels;
    Options.ShowCascades   = Forces.ShowCascades && !Camera;
    Options.ShowScaleBar   = !Camera;
    Options.ShowGroundGrid = !Camera;
    Options.Cascades.clear();
    const Frontier::ForceFieldBudget Budget = Frontier::ResolveForceFieldBudget(Forces, Sources);
    for (uint32_t Level = 0; Level < Budget.LevelCount; ++Level) Options.Cascades.push_back(Budget.Levels[Level]);
    Options.CascadeFocus = {Slice.View.CentreX, Slice.View.CentreZ};
    Options.Sources      = Frontier::ForceFieldVisualization::DescribeSources(Space, Names, Selected, Forces.ViewContribution, Forces.TimelineDurationSeconds);
    const double Half    = std::max(50.0, static_cast<double>(Forces.ExtentMetres) / 2.0);
    Options.HasExtent    = true;
    Options.Extent       = {-Half, -Half, Half, Half};
    Options.MinimumSpan  = 40.0;
    Options.MaximumSpan  = 2.4e5;
    if (!Camera) Slice.View.Mode = Forces.Projection == Frontier::ForceFieldProjection::Oblique ? Frontier::FieldViewMode::Oblique : Frontier::FieldViewMode::Map;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIELD VIEW
//------------------------------------------------------------------------------------------------------------------------

void EverestWorkspace::ConstructFieldViewport(const ImVec2& StageMin, const ImVec2& StageMax, float Scale)
{
    const Frontier::EditorTypefaces Faces = Frontier::EditorTypefaces::Resolve();
    ImDrawList*                     Ink   = ImGui::GetWindowDrawList();
    if (!Field.Slice)
    {
        Field.Slice            = std::make_unique<Frontier::ForceFieldVisualization>();
        Field.Slice->View.Span = 6000.0;
    }
    Frontier::ForceFieldVisualization& Slice = *Field.Slice;
    Ink->PushClipRect(StageMin, StageMax, true);
    if (!ConfigureFieldSlice(Slice, false))
    {
        Ink->AddRectFilled(StageMin, StageMax, IM_COL32(5, 11, 18, 255));
        Frontier::ViewportChromeControls::ConstructOverlayNotice(Ink, Faces, StageMin, StageMax, "Add a Force Field record to see the field", Scale);
        Ink->PopClipRect();
        return;
    }
    FieldRecords                       Records = ResolveFieldRecords(QuerySceneWorld());
    Frontier::ForceFieldConfiguration& Forces  = Records.Field->AccessConfiguration();

    const float  TimelineHeight = 92.0f * Scale;
    const ImVec2 MapMax(StageMax.x, std::max(StageMin.y + 80.0f * Scale, StageMax.y - TimelineHeight));

    // 📝 Navigation first (the map's input), then the picture; the minimap and the timeline take their own input on top.
    const Frontier::FieldInteraction Outcome = Slice.ConstructNavigation("##FieldSlice", StageMin, MapMax);
    if (Outcome.PickedSource >= 0 && static_cast<size_t>(Outcome.PickedSource) < Records.Sources.size())
    {
        OutlinerPanel.Select(Records.Sources[static_cast<size_t>(Outcome.PickedSource)]->QueryToken());
        OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    }
    if (Outcome.MovedSource >= 0 && static_cast<size_t>(Outcome.MovedSource) < Records.Sources.size())
    {
        // 📝 The handle is where the source is now; a moving source (track, drift, front) keeps its travel, so the authored
        //    centre moves by the same amount.
        Frontier::ForceSourceRecord&        Record = *Records.Sources[static_cast<size_t>(Outcome.MovedSource)];
        Frontier::ForceSourceConfiguration& Source = Record.AccessConfiguration();
        double DriftX = 0.0, DriftZ = 0.0;
        for (const Frontier::FieldSourceOutline& Outline : Slice.Options.Sources)
            if (Outline.Index == Outcome.MovedSource)
            {
                const Frontier::SpatialVector Authored = Frontier::ConvertSceneToFieldAxes(Source.CentreMetres);
                DriftX = Outline.X - Authored.X;
                DriftZ = Outline.Z - Authored.Z;
            }
        Source.CentreMetres[0] = std::round((Outcome.MovedTo[0] - DriftX) * 100.0) / 100.0;
        Source.CentreMetres[1] = std::round(-(Outcome.MovedTo[1] - DriftZ) * 100.0) / 100.0;
        Source.AdvanceRevision();
    }
    Field.Hovering   = Outcome.Hovering;
    Field.HoverPoint = Outcome.HoverPoint;

    Slice.Render(Ink, Faces, StageMin, MapMax, Forces.TimelineSeconds, Scale);
    const Frontier::FieldStatistics& Statistics = Slice.QueryStatistics();
    ImFont* Mono = Faces.Monospace != nullptr ? Faces.Monospace : Faces.Regular;

    // 📝 The HUD (top left): view, span, cell and level of detail, slice height, time.
    {
        char Span[32], Cell[32], SliceHeight[32], Clock[32];
        Frontier::ForceFieldVisualization::FormatDistance(Slice.View.Span, Span, sizeof(Span));
        Frontier::ForceFieldVisualization::FormatDistance(Statistics.CellSpan, Cell, sizeof(Cell));
        Frontier::ForceFieldVisualization::FormatDistance(Forces.SliceHeightMetres, SliceHeight, sizeof(SliceHeight));
        std::snprintf(Clock, sizeof(Clock), "%.1f s", Forces.TimelineSeconds);
        const int Level = static_cast<int>(std::lround(std::log2(std::max(1e-6, Statistics.CellSpan))));
        const std::pair<const char*, std::string> Parts[] = {{"VIEW ", Slice.View.Mode == Frontier::FieldViewMode::Oblique ? "Oblique" : "Map"},
                                                             {"SPAN ", Span},
                                                             {"CELL ", std::string(Cell) + " \xC2\xB7 LOD " + std::to_string(Level)},
                                                             {"SLICE ", SliceHeight},
                                                             {"t ", Clock}};
        const float Size = 11.0f * Scale;
        float       Wide = 16.0f * Scale;
        for (const auto& [Label, Value] : Parts) Wide += MeasureFace(Mono, Size, Label) + MeasureFace(Mono, Size, Value.c_str()) + 12.0f * Scale;
        const ImVec2 Corner(StageMin.x + 12.0f * Scale, StageMin.y + 12.0f * Scale);
        ChromePanel(Ink, Corner, ImVec2(Corner.x + Wide, Corner.y + 26.0f * Scale), Scale);
        float X = Corner.x + 10.0f * Scale;
        for (const auto& [Label, Value] : Parts)
        {
            Ink->AddText(Mono, Size, ImVec2(X, Corner.y + 7.0f * Scale), IM_COL32(120, 128, 140, 255), Label);
            X += MeasureFace(Mono, Size, Label);
            Ink->AddText(Mono, Size, ImVec2(X, Corner.y + 7.0f * Scale), IM_COL32(232, 236, 242, 255), Value.c_str());
            X += MeasureFace(Mono, Size, Value.c_str()) + 12.0f * Scale;
        }
    }

    // 📝 The minimap (top right): the whole authored extent; a click centres the view there.
    {
        const float  Side = std::min(170.0f * Scale, (MapMax.y - StageMin.y) * 0.42f);
        const ImVec2 Panel(MapMax.x - 12.0f * Scale - Side - 12.0f * Scale, StageMin.y + 12.0f * Scale);
        ChromePanel(Ink, Panel, ImVec2(Panel.x + Side + 12.0f * Scale, Panel.y + Side + 30.0f * Scale), Scale);
        (void)Slice.ConstructMinimap("##FieldMinimap", Ink, ImVec2(Panel.x + 6.0f * Scale, Panel.y + 6.0f * Scale),
                                     ImVec2(Panel.x + 6.0f * Scale + Side, Panel.y + 6.0f * Scale + Side), Forces.TimelineSeconds, Scale);
        char Extent[32];
        Frontier::ForceFieldVisualization::FormatDistance(Forces.ExtentMetres, Extent, sizeof(Extent));
        const float Size = 10.0f * Scale;
        Ink->AddText(Faces.Regular, Size, ImVec2(Panel.x + 10.0f * Scale, Panel.y + Side + 13.0f * Scale), IM_COL32(140, 146, 156, 255), "Extent");
        Ink->AddText(Faces.Regular, Size, ImVec2(Panel.x + Side + 2.0f * Scale - MeasureFace(Faces.Regular, Size, Extent), Panel.y + Side + 13.0f * Scale),
                     IM_COL32(200, 206, 214, 255), Extent);
    }

    // 📝 The reading under the pointer, and the navigation hint.
    if (Field.Hovering)
    {
        const Frontier::FieldProbeReading Probe = Slice.ProbeWorld(Field.HoverPoint[0], Field.HoverPoint[1]);
        if (Probe.Valid)
        {
            char X[32], Z[32], Y[32], Lines[5][160];
            int  Count = 0;
            Frontier::ForceFieldVisualization::FormatDistance(Probe.X, X, sizeof(X));
            Frontier::ForceFieldVisualization::FormatDistance(-Probe.Z, Z, sizeof(Z));
            Frontier::ForceFieldVisualization::FormatDistance(Probe.Y, Y, sizeof(Y));
            std::snprintf(Lines[Count++], sizeof(Lines[0]), "east %s   north %s   up %s", X, Z, Y);
            if (Forces.ViewContribution == Frontier::ForceContribution::Damp)
                std::snprintf(Lines[Count++], sizeof(Lines[0]), "damping %.3f 1/s", Probe.Vector.X);
            else
            {
                std::snprintf(Lines[Count++], sizeof(Lines[0]), "|v| %.2f %s   toward %.0f\xC2\xB0", Probe.Speed, Probe.Unit, Probe.Bearing);
                std::snprintf(Lines[Count++], sizeof(Lines[0]), "v  %.2f  %.2f  %.2f", Probe.Vector.X, Probe.Vector.Y, Probe.Vector.Z);
                char Vorticity[24], Divergence[24];
                Frontier::ForceFieldVisualization::FormatReading(Probe.Vorticity, Vorticity, sizeof(Vorticity));
                Frontier::ForceFieldVisualization::FormatReading(Probe.Divergence, Divergence, sizeof(Divergence));
                std::snprintf(Lines[Count++], sizeof(Lines[0]), "\xCF\x89 %s   \xE2\x88\x87\xC2\xB7v %s   per s", Vorticity, Divergence);
                if (Forces.ViewContribution == Frontier::ForceContribution::Flow)
                {
                    const uint32_t Force = Frontier::ResolveBeaufortForce(Probe.Horizontal);
                    std::snprintf(Lines[Count++], sizeof(Lines[0]), "Beaufort %u \xC2\xB7 %s", Force, Frontier::ResolveBeaufortName(Force));
                }
            }
            const float Size = 11.0f * Scale;
            float       Wide = 0.0f;
            for (int Index = 0; Index < Count; ++Index) Wide = std::max(Wide, MeasureFace(Mono, Size, Lines[Index]));
            const ImVec2 Pointer = ImGui::GetIO().MousePos;
            ImVec2       Corner(Pointer.x + 18.0f * Scale, Pointer.y + 18.0f * Scale);
            const float  Tall = (Count * 16.0f + 12.0f) * Scale;
            if (Corner.x + Wide + 20.0f * Scale > MapMax.x) Corner.x = Pointer.x - Wide - 38.0f * Scale;
            if (Corner.y + Tall > MapMax.y) Corner.y = Pointer.y - Tall - 18.0f * Scale;
            ChromePanel(Ink, Corner, ImVec2(Corner.x + Wide + 20.0f * Scale, Corner.y + Tall), Scale);
            for (int Index = 0; Index < Count; ++Index)
                Ink->AddText(Mono, Size, ImVec2(Corner.x + 10.0f * Scale, Corner.y + (6.0f + Index * 16.0f) * Scale),
                             Index == 0 ? IM_COL32(150, 156, 166, 255) : IM_COL32(232, 236, 242, 255), Lines[Index]);
        }
    }
    {
        const char*  Hint = Slice.View.Mode == Frontier::FieldViewMode::Oblique
                                ? "Drag to pan \xC2\xB7 wheel to zoom \xC2\xB7 drag a source to move it \xC2\xB7 right-drag turns and tilts"
                                : "Drag to pan \xC2\xB7 wheel to zoom \xC2\xB7 drag a source to move it \xC2\xB7 double-click centres";
        const float  Size = 10.0f * Scale;
        const float  Wide = MeasureFace(Faces.Regular, Size, Hint);
        const ImVec2 Corner(MapMax.x - Wide - 32.0f * Scale, MapMax.y - 34.0f * Scale);
        if (Corner.x > StageMin.x + 240.0f * Scale)
        {
            Ink->AddRectFilled(Corner, ImVec2(Corner.x + Wide + 20.0f * Scale, Corner.y + 22.0f * Scale), IM_COL32(10, 12, 16, 170), 11.0f * Scale);
            Ink->AddText(Faces.Regular, Size, ImVec2(Corner.x + 10.0f * Scale, Corner.y + 5.0f * Scale), IM_COL32(170, 176, 186, 255), Hint);
        }
    }
    Ink->PopClipRect();
    ConstructFieldTimeline(ImVec2(StageMin.x, MapMax.y), StageMax, Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIMELINE LANES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The prototype's timeline strip: transport and rates on the left; a ruler and one lane per windowed or triggered force,
//    its windows drawn in the force's colour, a Fire pill for the triggers, and the playhead. Dragging the ruler scrubs.
void EverestWorkspace::ConstructFieldTimeline(const ImVec2& Min, const ImVec2& Max, float Scale)
{
    using Frontier::ViewportChromeControls;
    const Frontier::EditorTypefaces Faces   = Frontier::EditorTypefaces::Resolve();
    ImDrawList*                     Ink     = ImGui::GetWindowDrawList();
    FieldRecords                    Records = ResolveFieldRecords(QuerySceneWorld());
    if (Records.Field == nullptr) return;
    Frontier::ForceFieldConfiguration& Forces   = Records.Field->AccessConfiguration();
    const double                       Duration = std::max(1.0, static_cast<double>(Forces.TimelineDurationSeconds));
    ImFont*                            Mono     = Faces.Monospace != nullptr ? Faces.Monospace : Faces.Regular;

    const ImVec2 Panel(Min.x + 8.0f * Scale, Min.y + 6.0f * Scale);
    const ImVec2 PanelMax(Max.x - 8.0f * Scale, Max.y - 6.0f * Scale);
    Ink->AddRectFilled(Panel, PanelMax, IM_COL32(17, 19, 23, 255), 12.0f * Scale);
    Ink->AddRect(Panel, PanelMax, IM_COL32(255, 255, 255, 16), 12.0f * Scale, 0, Scale);

    // 📝 Transport: play / pause, back to 0 s, the clock; the rates and how many forces are always on.
    const float Button = 28.0f * Scale;
    float       X      = Panel.x + 12.0f * Scale;
    const float RowA   = Panel.y + 10.0f * Scale;
    if (ViewportChromeControls::ConstructIconButton("##FieldPlay", ImVec2(X, RowA), Button, Forces.TimelinePlaying ? "Pause" : "Play", false, true,
                                                    "Play / pause the timeline"))
    {
        Forces.TimelinePlaying = !Forces.TimelinePlaying;
        Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "Timeline", "%s at %.1f s",
                                 Forces.TimelinePlaying ? "Playing" : "Paused", Forces.TimelineSeconds);
    }
    X += Button + 6.0f * Scale;
    if (ViewportChromeControls::ConstructIconButton("##FieldRestart", ImVec2(X, RowA), Button, "RotateCcw", false, true,
                                                    "Back to 0 s (fired triggers are armed again)"))
    {
        Forces.TimelineSeconds = 0.0;
        for (Frontier::ForceSourceRecord* Source : Records.Sources)
            if (Source->QueryConfiguration().Activation == Frontier::ForceActivation::Trigger) Source->AccessConfiguration().TriggeredAtSeconds = -1.0;
    }
    X += Button + 10.0f * Scale;
    char Clock[48], Length[24];
    std::snprintf(Clock, sizeof(Clock), "%.1f s", Forces.TimelineSeconds);
    std::snprintf(Length, sizeof(Length), " / %.0f s", Duration);
    Ink->AddText(Mono, 13.0f * Scale, ImVec2(X, RowA + 6.0f * Scale), IM_COL32(236, 240, 246, 255), Clock);
    Ink->AddText(Mono, 10.5f * Scale, ImVec2(X + MeasureFace(Mono, 13.0f * Scale, Clock), RowA + 8.0f * Scale), IM_COL32(130, 136, 146, 255), Length);
    const float RowB = RowA + Button + 8.0f * Scale;
    int         Rate = Forces.TimelineRate >= 30.0f ? 2 : Forces.TimelineRate >= 5.0f ? 1 : 0;
    if (ViewportChromeControls::ConstructSegmentedSwitch("##FieldRate", Faces, ImVec2(Panel.x + 12.0f * Scale, RowB), ImVec2(126.0f * Scale, 26.0f * Scale),
                                                        RateLabels, 3, Rate, nullptr, nullptr, Scale))
        Forces.TimelineRate = RateValues[Rate];
    int Always = 0;
    for (const Frontier::ForceSourceRecord* Source : Records.Sources)
        Always += Source->QueryConfiguration().Activation == Frontier::ForceActivation::Always && Source->QueryConfiguration().Enabled ? 1 : 0;
    char AlwaysText[32];
    std::snprintf(AlwaysText, sizeof(AlwaysText), "%d always on", Always);
    Ink->AddText(Mono, 10.5f * Scale, ImVec2(Panel.x + 146.0f * Scale, RowB + 7.0f * Scale), IM_COL32(150, 156, 166, 255), AlwaysText);

    // 📝 Tracks: name column, the ruler and lanes sharing one scale, the Fire column.
    const float TracksLeft = Panel.x + 238.0f * Scale;
    const float NameWidth  = 110.0f * Scale;
    const float FireWidth  = 58.0f * Scale;
    const float RulerLeft  = TracksLeft + NameWidth;
    const float RulerRight = PanelMax.x - FireWidth - 10.0f * Scale;
    if (RulerRight - RulerLeft < 60.0f * Scale) return;
    const float RulerTop = Panel.y + 8.0f * Scale;
    const auto  At       = [&](double Seconds) { return RulerLeft + static_cast<float>(std::clamp(Seconds / Duration, 0.0, 1.0)) * (RulerRight - RulerLeft); };
    Ink->AddText(Faces.Regular, 9.5f * Scale, ImVec2(TracksLeft, RulerTop + 2.0f * Scale), IM_COL32(120, 126, 136, 255), "TIME");
    const double Step = Duration <= 60 ? 10 : Duration <= 180 ? 20 : 60;
    for (double Second = 0; Second <= Duration + 1e-6; Second += Step)
    {
        const float TickX = At(Second);
        Ink->AddLine(ImVec2(TickX, RulerTop + 2.0f * Scale), ImVec2(TickX, RulerTop + 14.0f * Scale), IM_COL32(255, 255, 255, 40), Scale);
        char Label[16];
        std::snprintf(Label, sizeof(Label), "%.0fs", Second);
        Ink->AddText(Faces.Regular, 9.5f * Scale, ImVec2(TickX + 3.0f * Scale, RulerTop + 2.0f * Scale), IM_COL32(120, 126, 136, 255), Label);
    }
    float       LaneTop    = RulerTop + 20.0f * Scale;
    const float LaneHeight = 18.0f * Scale;
    int         Lanes      = 0;
    for (Frontier::ForceSourceRecord* Record : Records.Sources)
    {
        Frontier::ForceSourceConfiguration& Source = Record->AccessConfiguration();
        if (Source.Activation == Frontier::ForceActivation::Always) continue;
        if (LaneTop + LaneHeight > PanelMax.y - 4.0f * Scale) break;
        ++Lanes;
        const bool Selected     = Record->QueryToken().Value == OutlinerPanel.QuerySelected().Value;
        const auto Contribution = Frontier::ResolveForceSourceDescriptor(Source.Category).Contribution;
        if (Selected)
            Ink->AddRectFilled(ImVec2(TracksLeft - 4.0f * Scale, LaneTop - 1.0f * Scale), ImVec2(PanelMax.x - 6.0f * Scale, LaneTop + LaneHeight),
                               IM_COL32(255, 255, 255, 12), 6.0f * Scale);
        Ink->AddText(Faces.Regular, 10.5f * Scale, ImVec2(TracksLeft, LaneTop + 3.0f * Scale), Selected ? IM_COL32_WHITE : IM_COL32(190, 196, 204, 255),
                     Record->QueryRecordName().c_str());
        Ink->AddRectFilled(ImVec2(RulerLeft, LaneTop + 7.0f * Scale), ImVec2(RulerRight, LaneTop + 11.0f * Scale), IM_COL32(255, 255, 255, 14), 2.0f * Scale);
        std::vector<std::pair<double, double>> Windows;
        if (Source.Activation == Frontier::ForceActivation::Window)
        {
            const bool Repeats = Source.RepeatsSeconds > 0 && Source.LastsSeconds > 0;
            for (double Start = Source.BeginsSeconds; Start < Duration && Windows.size() < 60; Start += Repeats ? Source.RepeatsSeconds : Duration * 2)
                Windows.push_back({Start, Source.LastsSeconds > 0 ? std::min(Duration, Start + Source.LastsSeconds) : Duration});
        }
        else if (Source.TriggeredAtSeconds >= 0)
            Windows.push_back({Source.TriggeredAtSeconds, Source.LastsSeconds > 0 ? std::min(Duration, Source.TriggeredAtSeconds + Source.LastsSeconds) : Duration});
        for (const auto& [Start, End] : Windows)
            Ink->AddRectFilled(ImVec2(At(Start), LaneTop + 5.0f * Scale), ImVec2(std::max(At(Start) + 3.0f * Scale, At(End)), LaneTop + 13.0f * Scale),
                               ContributionTint(Contribution, Source.Enabled ? 200 : 80), 4.0f * Scale);
        ImGui::PushID(Record);
        ImGui::SetCursorScreenPos(ImVec2(TracksLeft, LaneTop));
        if (ImGui::InvisibleButton("##Lane", ImVec2(NameWidth, LaneHeight)))
        {
            OutlinerPanel.Select(Record->QueryToken());
            OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
        }
        if (Source.Activation == Frontier::ForceActivation::Trigger)
        {
            const ImVec2 FireMin(PanelMax.x - FireWidth - 4.0f * Scale, LaneTop);
            const ImVec2 FireMax(FireMin.x + FireWidth - 4.0f * Scale, FireMin.y + LaneHeight);
            ImGui::SetCursorScreenPos(FireMin);
            const bool Fire    = ImGui::InvisibleButton("##Fire", ImVec2(FireMax.x - FireMin.x, LaneHeight));
            const bool Hovered = ImGui::IsItemHovered();
            Ink->AddRectFilled(FireMin, FireMax, Hovered ? IM_COL32(232, 194, 122, 60) : IM_COL32(232, 194, 122, 26), 9.0f * Scale);
            Ink->AddRect(FireMin, FireMax, IM_COL32(232, 194, 122, 150), 9.0f * Scale, 0, Scale);
            const float Wide = MeasureFace(Faces.SemiBold, 9.5f * Scale, "FIRE");
            Ink->AddText(Faces.SemiBold, 9.5f * Scale, ImVec2(FireMin.x + (FireMax.x - FireMin.x - Wide) * 0.5f, FireMin.y + 4.0f * Scale),
                         IM_COL32(232, 194, 122, 255), "FIRE");
            if (Fire)
            {
                Source.TriggeredAtSeconds = Forces.TimelineSeconds;
                Source.AdvanceRevision();
                Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "Forces", "%s: fired at %.1f s",
                                         Record->QueryRecordName().c_str(), Forces.TimelineSeconds);
            }
        }
        ImGui::PopID();
        LaneTop += LaneHeight + 2.0f * Scale;
    }
    if (Lanes == 0)
        Ink->AddText(Faces.Regular, 10.5f * Scale, ImVec2(TracksLeft, LaneTop + 3.0f * Scale), IM_COL32(130, 136, 146, 255),
                     "Every force is always on. Give one a time window or a trigger to see it here.");

    // 📝 Scrubbing: a press on the ruler moves the clock and holds it while dragged.
    ImGui::SetCursorScreenPos(ImVec2(RulerLeft, RulerTop));
    ImGui::InvisibleButton("##FieldRuler", ImVec2(RulerRight - RulerLeft, 18.0f * Scale));
    Field.Scrubbing = ImGui::IsItemActive();
    if (Field.Scrubbing) Forces.TimelineSeconds = std::clamp((ImGui::GetIO().MousePos.x - RulerLeft) / (RulerRight - RulerLeft), 0.0f, 1.0f) * Duration;
    const float Head = At(Forces.TimelineSeconds);
    Ink->AddLine(ImVec2(Head, RulerTop + 2.0f * Scale), ImVec2(Head, PanelMax.y - 6.0f * Scale), IM_COL32_WHITE, 1.5f * Scale);
    Ink->AddCircleFilled(ImVec2(Head, RulerTop + 3.0f * Scale), 4.0f * Scale, IM_COL32_WHITE, 12);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BAR AND OVERLAY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The viewport bar in the Field view: the title on the left; on the right the sampling chip, Map / Oblique, the range pills,
//    and Scene | Field, centring and fullscreen where the scene's bar has Scene | Field, the readout and fullscreen.
void EverestWorkspace::ConstructFieldBar(const ImVec2& Min, const ImVec2& Max, float Scale)
{
    using Frontier::ViewportChromeControls;
    const Frontier::EditorTypefaces Faces  = Frontier::EditorTypefaces::Resolve();
    ImDrawList*                     Ink    = ImGui::GetWindowDrawList();
    const float                     Height = Max.y - Min.y, Control = 34.0f * Scale, Gap = 8.0f * Scale;
    const float                     Top    = Min.y + (Height - Control) * 0.5f;
    float                           X      = Min.x + 14.0f * Scale;
    (void)Frontier::GlyphSymbol::Construct(Ink, "Wind", ImVec2(X, Min.y + (Height - 20.0f * Scale) * 0.5f), 20.0f * Scale, 1.8f, IM_COL32(200, 206, 214, 255));
    X += 30.0f * Scale;
    Ink->AddText(Faces.Medium, 13.0f * Scale, ImVec2(X, Min.y + Height * 0.5f - 15.0f * Scale), IM_COL32(236, 240, 246, 255), "Force field");
    char Meta[160];
    std::snprintf(Meta, sizeof(Meta), "Everest \xC2\xB7 %s \xC2\xB7 100 m \xE2\x80\x93 100 km", ActiveLevelPath.filename().string().c_str());
    Ink->AddText(Faces.Regular, 10.0f * Scale, ImVec2(X, Min.y + Height * 0.5f + 2.0f * Scale), IM_COL32(130, 136, 146, 255), Meta);
    X += std::max(MeasureFace(Faces.Medium, 13.0f * Scale, "Force field"), MeasureFace(Faces.Regular, 10.0f * Scale, Meta)) + 16.0f * Scale;

    float Right = Max.x - 12.0f * Scale;
    if (ViewportChromeControls::ConstructIconButton("##FieldFullscreen", ImVec2(Right - Control, Top), Control, "ScanCorners", Overlays.Fullscreen, true,
                                                    "Fullscreen viewport (V), Escape restores panels"))
        ToggleViewportOverlay(2);
    Right -= Control + Gap;
    if (ViewportChromeControls::ConstructIconButton("##FieldCentre", ImVec2(Right - Control, Top), Control, "Focus", false, true,
                                                    "Centre the view on the selected force (or the field's centre)") &&
        Field.Slice)
    {
        Field.Slice->View.CentreX = 0.0;
        Field.Slice->View.CentreZ = 0.0;
        for (const Frontier::FieldSourceOutline& Outline : Field.Slice->Options.Sources)
            if (Outline.Selected)
            {
                Field.Slice->View.CentreX = Outline.X;
                Field.Slice->View.CentreZ = Outline.Z;
            }
        Field.Slice->Invalidate();
    }
    Right -= Control + Gap;
    int Shown = 1;
    if (ViewportChromeControls::ConstructSegmentedSwitch("##SceneField", Faces, ImVec2(Right - 132.0f * Scale, Top), ImVec2(132.0f * Scale, Control),
                                                        ViewLabels, 2, Shown, nullptr, nullptr, Scale))
        Field.Shown = Shown == 1;
    Right -= 132.0f * Scale + Gap;

    auto* Record = dynamic_cast<Frontier::ForceFieldRecord*>(QuerySceneWorld().FindFirstByType("ForceField"));
    if (Record == nullptr) return;
    Frontier::ForceFieldConfiguration& Forces = Record->AccessConfiguration();
    // 📝 Range pills: the one closest to the span lights.
    const float RangeWidth = 236.0f * Scale;
    if (Right - RangeWidth > X)
    {
        int Range = -1;
        if (Field.Slice)
            for (int Index = 0; Index < 4; ++Index)
            {
                const double Ratio = Field.Slice->View.Span / RangeSpans[Index];
                if (Ratio > 0.7 && Ratio < 1.45) Range = Index;
            }
        int Picked = Range;
        if (ViewportChromeControls::ConstructSegmentedSwitch("##FieldRange", Faces, ImVec2(Right - RangeWidth, Top), ImVec2(RangeWidth, Control), RangeLabels, 4,
                                                            Picked, nullptr, nullptr, Scale) &&
            Picked >= 0 && Field.Slice)
        {
            Field.Slice->JumpToSpan(RangeSpans[Picked]);
            char Span[32];
            Frontier::ForceFieldVisualization::FormatDistance(RangeSpans[Picked], Span, sizeof(Span));
            Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "View", "Range: %s", Span);
        }
        Right -= RangeWidth + Gap;
    }
    const float ProjectionWidth = 150.0f * Scale;
    if (Right - ProjectionWidth > X)
    {
        int Projection = Forces.Projection == Frontier::ForceFieldProjection::Oblique ? 1 : 0;
        if (ViewportChromeControls::ConstructSegmentedSwitch("##FieldProjection", Faces, ImVec2(Right - ProjectionWidth, Top), ImVec2(ProjectionWidth, Control),
                                                            ProjectionNames, 2, Projection, nullptr, nullptr, Scale))
        {
            Forces.Projection = Projection == 1 ? Frontier::ForceFieldProjection::Oblique : Frontier::ForceFieldProjection::Map;
            Forces.AdvanceRevision();
        }
        Right -= ProjectionWidth + Gap;
    }
    // 📝 The sampling chip: what the view is (the CPU field, analytic) and what one refresh costs.
    char Chip[96];
    if (Field.Slice)
        std::snprintf(Chip, sizeof(Chip), "ANALYTIC \xC2\xB7 %u SAMPLES \xC2\xB7 %.0f MS", Field.Slice->QueryStatistics().Samples,
                      Field.Slice->QueryStatistics().Milliseconds);
    else std::snprintf(Chip, sizeof(Chip), "ANALYTIC \xC2\xB7 CPU SAMPLED");
    const float ChipSize = 9.5f * Scale;
    const float ChipWide = MeasureFace(Faces.Medium, ChipSize, Chip) + 30.0f * Scale;
    if (Right - ChipWide > X)
    {
        const ImVec2 ChipMin(Right - ChipWide, Min.y + (Height - 24.0f * Scale) * 0.5f);
        Ink->AddRectFilled(ChipMin, ImVec2(Right, ChipMin.y + 24.0f * Scale), IM_COL32(143, 211, 196, 24), 12.0f * Scale);
        Ink->AddRect(ChipMin, ImVec2(Right, ChipMin.y + 24.0f * Scale), IM_COL32(143, 211, 196, 90), 12.0f * Scale, 0, Scale);
        Ink->AddCircleFilled(ImVec2(ChipMin.x + 12.0f * Scale, ChipMin.y + 12.0f * Scale), 3.0f * Scale, IM_COL32(143, 211, 196, 255), 10);
        Ink->AddText(Faces.Medium, ChipSize, ImVec2(ChipMin.x + 21.0f * Scale, ChipMin.y + 6.0f * Scale), IM_COL32(143, 211, 196, 255), Chip);
    }
}

// 📝 The Force Field's Scene overlay: the slice in the scene camera's perspective over the rendered image, faded so the scene
//    shows through.
void EverestWorkspace::ConstructFieldOverlay(const ImVec2& StageMin, const ImVec2& StageMax, float Scale)
{
    const auto* Record = dynamic_cast<const Frontier::ForceFieldRecord*>(QuerySceneWorld().FindFirstByType("ForceField"));
    if (Record == nullptr || !Record->QueryConfiguration().SceneOverlay || !Record->QueryConfiguration().Enabled) return;
    if (!Field.Overlay) Field.Overlay = std::make_unique<Frontier::ForceFieldVisualization>();
    Frontier::ForceFieldVisualization& Overlay = *Field.Overlay;
    Overlay.View.Mode                          = Frontier::FieldViewMode::Camera;
    Frontier::FieldCamera& Camera              = Overlay.View.Camera;
    Camera.Eye                                 = Flight.Position;
    Camera.Forward                             = Flight.ResolveForward();
    const auto& F                              = Camera.Forward;
    const auto& U                              = Navigation.Up;
    Camera.Right        = {F[1] * U[2] - F[2] * U[1], F[2] * U[0] - F[0] * U[2], F[0] * U[1] - F[1] * U[0]};
    const double Length = std::max(1e-8, std::hypot(Camera.Right[0], Camera.Right[1], Camera.Right[2]));
    for (double& Axis : Camera.Right) Axis /= Length;
    const auto& R              = Camera.Right;
    Camera.Up                  = {R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0]};
    Camera.VerticalFieldOfView = Flight.ResolveFieldOfView();
    if (!ConfigureFieldSlice(Overlay, true)) return;
    Overlay.Render(ImGui::GetWindowDrawList(), Frontier::EditorTypefaces::Resolve(), StageMin, StageMax, Record->QueryConfiguration().TimelineSeconds, Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE LINKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Force Field page reads the forces under the field and the Field view's measurements; while the view shows, its peak
//    joins the hero's history about ten times a second (a change of unit starts the history again).
void EverestWorkspace::PublishFieldReadings(Frontier::EnvironmentEditAccess& Access, double DeltaSeconds)
{
    for (const Frontier::SceneRecordBase* Entry : QuerySceneWorld().FindAllByType("ForceSource"))
        if (auto* Source = dynamic_cast<Frontier::ForceSourceRecord*>(QuerySceneWorld().FindRecord(Entry->QueryToken())))
            Access.ForceSources.push_back({Source->QueryToken().Value, Source->QueryRecordName(), &Source->AccessConfiguration()});
    // 📝 The contribution the page shows joins the Log Stream when it changes (the prototype's VIEW lines).
    if (Access.Forces != nullptr && static_cast<int>(Access.Forces->ViewContribution) != Field.LoggedContribution)
    {
        const bool Opening       = Field.LoggedContribution < 0;
        Field.LoggedContribution = static_cast<int>(Access.Forces->ViewContribution);
        if (!Opening)
        {
            const Frontier::ForceContributionDescriptor& Facts = Frontier::ResolveForceContributionDescriptor(Access.Forces->ViewContribution);
            Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "View", "Showing %s \xC2\xB7 %s",
                                     Facts.Label, Facts.Unit);
        }
    }
    Frontier::ForceFieldReadings& Readings = Field.Readings;
    Readings.FieldShown                    = Field.Shown;
    if (Field.Shown && Field.Slice && Field.Slice->QueryStatistics().Samples > 0)
    {
        const Frontier::FieldStatistics& Statistics = Field.Slice->QueryStatistics();
        const char*                      Unit       = Field.Slice->QueryUnit();
        if (std::string_view(Unit) != Readings.Unit) Readings.HistoryCount = Readings.HistoryCursor = 0;
        Readings.Measured     = true;
        Readings.Peak         = Statistics.Peak;
        Readings.Span         = Field.Slice->View.Span;
        Readings.Cell         = Statistics.CellSpan;
        Readings.Samples      = Statistics.Samples;
        Readings.Milliseconds = Statistics.Milliseconds;
        Readings.Unit         = Unit;
        Field.ReadingSeconds += std::max(0.0, DeltaSeconds);
        if (Field.ReadingSeconds >= 0.1 || Readings.HistoryCount == 0)
        {
            Field.ReadingSeconds                     = 0.0;
            Readings.History[Readings.HistoryCursor] = static_cast<float>(Statistics.Peak);
            Readings.HistoryCursor                   = (Readings.HistoryCursor + 1u) % static_cast<uint32_t>(Readings.History.size());
            Readings.HistoryCount                    = std::min<uint32_t>(Readings.HistoryCount + 1u, static_cast<uint32_t>(Readings.History.size()));
        }
    }
    Access.FieldReadings = Readings;
}

// 📝 What the Force Field and force pages ask the workspace for: select a force, add one (by type, or the Construct dialog
//    on its Forces group), show or leave the Field view, and jump the view to a range.
void EverestWorkspace::ServiceFieldActions()
{
    for (const std::string& Action : Frontier::InspectorRuntime::Instance().ConsumeWorkspaceActions())
    {
        const size_t      Colon    = Action.find(':');
        const std::string Verb     = Action.substr(0, Colon);
        const std::string Argument = Colon == std::string::npos ? std::string{} : Action.substr(Colon + 1);
        if (Verb == "select")
        {
            OutlinerPanel.Select(Frontier::RecordToken{std::strtoull(Argument.c_str(), nullptr, 10)});
            OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
        }
        else if (Verb == "add-force" && Argument.empty())
            ConstructDialog.Open("Forces");
        else if (Verb == "add-force")
        {
            const Frontier::ForceSourceDescriptor* Descriptor = Frontier::ResolveForceSourceDescriptor(Argument);
            if (Descriptor == nullptr) continue;
            const std::string           Name    = ResolveUniqueName(Descriptor->Name);
            const Frontier::RecordToken Created = ConstructForceRecord(Argument, Name, true);
            if (Created.IsValid())
                Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "Forces",
                                         "Added '%s' (%s) under the Force Field.", Name.c_str(), Descriptor->Name);
        }
        else if (Verb == "field-view")
        {
            Field.Shown = Argument != "off";
            Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "View", "%s",
                                     Field.Shown ? "Field view shown from the Force Field page" : "Back to the scene view");
        }
        else if (Verb == "field-span")
        {
            Field.Shown = true;
            if (!Field.Slice)
            {
                Field.Slice            = std::make_unique<Frontier::ForceFieldVisualization>();
                Field.Slice->View.Span = 6000.0;
            }
            Field.Slice->JumpToSpan(std::clamp(std::atof(Argument.c_str()), 40.0, 2.4e5));
            char Span[32];
            Frontier::ForceFieldVisualization::FormatDistance(Field.Slice->View.Span, Span, sizeof(Span));
            Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "View", "Field view across %s", Span);
        }
        else if (Verb == "fire")
        {
            auto* Record = dynamic_cast<Frontier::ForceSourceRecord*>(
                QuerySceneWorld().FindRecord(Frontier::RecordToken{std::strtoull(Argument.c_str(), nullptr, 10)}));
            auto* Forces = dynamic_cast<Frontier::ForceFieldRecord*>(QuerySceneWorld().FindFirstByType("ForceField"));
            if (Record == nullptr || Forces == nullptr) continue;
            Frontier::ForceSourceConfiguration& Source = Record->AccessConfiguration();
            Source.TriggeredAtSeconds                  = Forces->QueryConfiguration().TimelineSeconds;
            Source.AdvanceRevision();
            Frontier::WriteLogFormat(FieldLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "Forces", "%s: fired at %.1f s",
                                     Record->QueryRecordName().c_str(), Source.TriggeredAtSeconds);
        }
    }
}

// 📝 A ForceSource record under the Force Field with its type's defaults: in the middle of the Field view while it shows,
//    otherwise on the ground where the camera looks.
Frontier::RecordToken EverestWorkspace::ConstructForceRecord(const std::string& Identifier, const std::string& Name, bool Visible)
{
    const Frontier::ForceSourceDescriptor* Descriptor = Frontier::ResolveForceSourceDescriptor(Identifier);
    if (Descriptor == nullptr) return {};
    Frontier::RecordToken Owner{};
    if (Frontier::SceneRecordBase* FieldRecord = QuerySceneWorld().FindFirstByType("ForceField")) Owner = FieldRecord->QueryToken();
    const Frontier::RecordToken Created = QuerySceneWorld().ConstructRecord("ForceSource", Name, Owner);
    auto*                       Record  = dynamic_cast<Frontier::ForceSourceRecord*>(QuerySceneWorld().FindRecord(Created));
    if (Record == nullptr) return {};
    Frontier::ForceSourceConfiguration& Source   = Record->AccessConfiguration();
    const uint64_t                      Revision = Source.Revision;
    Source                                       = Frontier::ConstructForceSource(Descriptor->Category);
    Source.Revision                              = Revision;
    if (Field.Shown && Field.Slice)
    {
        Source.CentreMetres[0] = std::round(Field.Slice->View.CentreX);
        Source.CentreMetres[1] = std::round(-Field.Slice->View.CentreZ);
    }
    else
    {
        const std::array<double, 3> Ground = ResolveGroundTarget();
        Source.CentreMetres[0]             = std::round(Ground[0] * 10.0) / 10.0;
        Source.CentreMetres[1]             = std::round(Ground[1] * 10.0) / 10.0;
    }
    Source.AdvanceRevision();
    if (!Visible) (void)Frontier::SceneDirectory::Instance().ConfigureVisible(Created, false);
    OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    OutlinerPanel.Select(Created);
    return Created;
}

} // namespace Everest

#endif
