/*==========================================================================================================================================
                                                        OPTICALFLAREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 NativeFlarePanel.js: source drag, HDR export, optical presets and the ghost, halo and diffraction instruments.

#include "Engine/Interface/Inspectors/OpticalFlareInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"
#include "Engine/Interface/Inspectors/OpticalReference.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace Frontier
{

namespace
{

std::string Figure(const char *Format, double Quantity)
{
    char Text[80]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

std::array<double, 21> ReferenceQuery(const OpticalFlareConfiguration &Parameters)
{
    return {static_cast<double>(Parameters.Preset),
            Parameters.Intensity,
            Parameters.Spread,
            Parameters.ChromaticDispersion,
            static_cast<double>(Parameters.GhostCount),
            Parameters.GhostBrightness,
            static_cast<double>(Parameters.GhostShape),
            Parameters.GhostSpacing,
            Parameters.HaloRadius,
            Parameters.HaloWidth,
            Parameters.HaloBrightness,
            static_cast<double>(Parameters.AnamorphicStreak),
            static_cast<double>(Parameters.WarmStreak),
            static_cast<double>(Parameters.DiffractionStarburst),
            Parameters.RotationDegrees,
            static_cast<double>(Parameters.RayCount),
            Parameters.StreakStrength,
            static_cast<double>(Parameters.ApertureBladeCount),
            Parameters.PreviewPosition[0],
            Parameters.PreviewPosition[1],
            0};
}

void BeginOpticalCard(ControlColumn &Page, const char *Heading, const char *Caption)
{
    CardLook Look;
    Look.Fill            = ResolveCssColour(0x26252BFF);
    Look.FillEnd         = ResolveCssColour(0x1A1B20FF);
    Look.Border          = ResolveCssColour(0xC5B6D222);
    Look.GradientDegrees = 145;
    Look.GradientStop    = 1;
    Look.Radius          = 22;
    Look.PaddingTop = Look.PaddingBottom = Look.PaddingSide = 19;
    Look.MarginTop                                          = 0;
    Look.MarginBottom                                       = 18;
    Page.BeginCard(Look, nullptr, nullptr);
    SectionHeading::Construct(Page, Heading, Caption);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    REFERENCE RESIDENCE AND EXPORT
//------------------------------------------------------------------------------------------------------------------------

void OpticalFlareInspector::RefreshReference()
{
    if (PendingRadiance.valid() && PendingRadiance.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            LinearRadiance = PendingRadiance.get();
            if (LinearRadiance.size() != 640u * 280u * 3u) throw std::runtime_error("Optical reference dimensions or radiance are invalid");
            std::vector<uint8_t> Rgba(640 * 280 * 4, 255);
            for (size_t Pixel = 0; Pixel < 640 * 280; ++Pixel)
            {
                for (size_t Band = 0; Band < 3; ++Band)
                {
                    const double Radiance  = std::max(0.0f, LinearRadiance[Pixel * 3 + Band]);
                    Rgba[Pixel * 4 + Band] = static_cast<uint8_t>(std::lround(255 * std::pow(Radiance / (1 + Radiance), 1 / 2.2)));
                }
            }
            if (Composite.Configure(640, 280, Rgba))
                ResidentQuery = PendingQuery;
            else
                ReferenceDiagnostic = "Optical reference texture could not be registered";
        }
        catch (const std::exception &Failure)
        {
            ReferenceDiagnostic = Failure.what();
        }
    }
    const auto Query = ReferenceQuery(Parameters);
    if (!PendingRadiance.valid() && (ResidentQuery != Query || !Composite.QueryReady()) && ReferenceDiagnostic.empty())
    {
        PendingQuery        = Query;
        const auto Snapshot = Parameters;
        PendingRadiance     = std::async(std::launch::async, [Snapshot] { return GenerateOpticalReference(Snapshot); });
    }
}

void OpticalFlareInspector::ConstructExport()
{
    if (!ImGui::BeginPopupModal("Export HDR", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Linear RGB · 640 × 280 · little-endian PFM");
    ImGui::SetNextItemWidth(420 * InspectorRuntime::Instance().ResolveInterfaceScale());
    ImGui::InputText("Destination", ExportDestination.data(), ExportDestination.size());
    if (ImGui::Button("Save PFM"))
    {
        try
        {
            const auto Radiance = ResidentQuery == ReferenceQuery(Parameters) && !LinearRadiance.empty()
                                      ? LinearRadiance
                                      : GenerateOpticalReference(Parameters);
            if (EncodeLinearPfm(DecodeContentPath(ExportDestination.data()), Radiance, 640, 280, ExportDiagnostic))
                ExportDiagnostic += " · " + EncodeContentPath(std::filesystem::absolute(DecodeContentPath(ExportDestination.data())));
        }
        catch (const std::exception &Failure)
        {
            ExportDiagnostic = Failure.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    if (!ExportDiagnostic.empty()) ImGui::TextWrapped("%s", ExportDiagnostic.c_str());
    ImGui::EndPopup();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPTICAL LAYERS
//------------------------------------------------------------------------------------------------------------------------

bool OpticalFlareInspector::ConstructLightStack(ControlColumn &Page)
{
    BeginOpticalCard(Page, "Light stack", "02 / SOURCE & PRESET");
    constexpr const char *Presets[] = {"Cinematic", "Anamorphic", "Starburst", "Halo", "Custom layers"};
    int                   Preset    = static_cast<int>(Parameters.Preset);
    bool                  Changed   = SelectRow::Construct(Page, "FlarePreset", "Optical preset", Presets, 5, Preset);
    if (Changed) ConfigureOpticalPreset(Parameters, static_cast<uint32_t>(Preset));
    Changed |= SliderRow::Construct(Page, "FlareIntensity", "Intensity", Parameters.Intensity, 0, 5, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "FlareSpread", "Spread", Parameters.Spread, 0.25f, 2, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "FlareChromatic", "Chromatic separation", Parameters.ChromaticDispersion, 0, 1, 0.01f, "");
    CaptionText::Construct(Page,
                           "Presets retain their original reference behaviour. Editing layer geometry or a layer switch selects Custom "
                           "layers; global intensity, spread and chromatic separation do not.",
                           16, 0);
    Page.EndCard();
    return Changed;
}

bool OpticalFlareInspector::ConstructGhosts(ControlColumn &Page)
{
    BeginOpticalCard(Page, "Internal reflections", "03 / SENSOR-PLANE POSITIONS");
    DiagramDrag Drag;
    auto        Pen     = DiagramCanvas::Begin(Page, "FlareGhostAxis", 640, 300, 15, 15, &Drag,
                                               "Ghost optical axis · drag or use arrow keys to edit spacing");
    bool        Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
        Parameters.GhostSpacing = std::clamp(Drag.Point.x / 640 * 3, 0.0f, 3.0f);
    else if (Drag.Key == ImGuiKey_Home)
        Parameters.GhostSpacing = 1;
    else if (Drag.Key != ImGuiKey_None)
        Parameters.GhostSpacing = std::clamp(
            Parameters.GhostSpacing + (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_DownArrow ? -0.05f : 0.05f) * Drag.Step, 0.0f,
            3.0f);
    Pen.Rect({35, 30}, {570, 230}, ResolveCssColour(0x101318FF), 12);
    Pen.Line({35, 145}, {605, 145}, ResolveCssColour(0xBAC5DA18), 1, 3, 5);
    Pen.Line({320, 30}, {320, 260}, ResolveCssColour(0xBAC5DA18), 1, 3, 5);
    const ImVec2 Source(35 + Parameters.PreviewPosition[0] * 570, 30 + Parameters.PreviewPosition[1] * 230);
    Pen.Line(Source, {320 - (Source.x - 320) * 2.8f, 145 - (Source.y - 145) * 2.8f}, ResolveCssColour(0xB8A8CE55));
    for (uint32_t Ghost = 0; Ghost < std::min(24u, Parameters.GhostCount); ++Ghost)
    {
        const auto   Position = EvaluateFlareGhost(Parameters, Ghost);
        const ImVec2 Centre(35 + static_cast<float>(Position[0]) * 570, 30 + static_cast<float>(Position[1]) * 230);
        if (Centre.x < 40 || Centre.x > 600 || Centre.y < 36 || Centre.y > 253) continue;
        const float Radius = 9.0f + (Ghost % 4) * 4;
        if (Parameters.GhostShape >= 3)
        {
            std::vector<ImVec2> Points;
            const uint32_t      Sides = std::min(32u, Parameters.GhostShape);
            for (uint32_t Corner = 0; Corner <= Sides; ++Corner)
            {
                const double θ = Corner * EnvironmentReadings::Pi * 2 / Sides;
                Points.push_back(
                    {Centre.x + Radius * static_cast<float>(std::cos(θ)), Centre.y + Radius * static_cast<float>(std::sin(θ))});
            }
            Pen.Path(Points, ResolveCssColour(0xC3AFD2AA), 1.5f);
        }
        else
            Pen.Circle(Centre, Radius, ResolveCssColour(0xC3AFD210), ResolveCssColour(0xC3AFD2AA));
    }
    Pen.Circle(Source, 5, ResolveCssColour(0xF3DEB2FF));
    Pen.Circle({320, 145}, 4, ResolveCssColour(0xC1C9D5FF));
    Pen.Text({35, 291}, "SOURCE → SENSOR CENTRE → REFLECTIONS", ResolveCssColour(0x9B9DABFF), 18, -1);
    CaptionText::Construct(Page,
                           "Positions follow the reference’s optical axis; outlines are a geometry guide, not radiance or exact ghost "
                           "sizes. Off-sensor centres are omitted.",
                           16, 0);
    constexpr const char *Shapes[] = {"Round", "Hexagon", "Octagon"};
    constexpr uint32_t    Sides[]  = {0, 6, 8};
    int                   Shape    = Parameters.GhostShape == 6 ? 1 : Parameters.GhostShape == 8 ? 2 : 0;
    if (SelectRow::Construct(Page, "FlareGhostShape", "Ghost shape", Shapes, 3, Shape))
    {
        Parameters.GhostShape = Sides[Shape];
        Changed               = true;
    }
    Changed |= SliderRow::ConstructInteger(Page, "FlareGhosts", "Ghost count", Parameters.GhostCount, 0, 24, "");
    Changed |= SliderRow::Construct(Page, "FlareGhostBrightness", "Ghost brightness", Parameters.GhostBrightness, 0, 2, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "FlareGhostSpacing", "Ghost spacing", Parameters.GhostSpacing, 0, 3, 0.01f, "×");
    if (Changed) Parameters.Preset = 4;
    Page.EndCard();
    return Changed;
}

bool OpticalFlareInspector::ConstructHalo(ControlColumn &Page)
{
    BeginOpticalCard(Page, "Annular response", "04 / RADIAL CROSS-SECTION");
    DiagramDrag Drag;
    auto        Pen = DiagramCanvas::Begin(Page, "FlareHaloGuide", 640, 280, 15, 15, &Drag, "Halo radius guide · drag or use arrow keys");
    bool        Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
        Parameters.HaloRadius = std::clamp(std::hypot(Drag.Point.x - 220, Drag.Point.y - 130) / (95 / 0.6f), 0.01f, 0.6f);
    else if (Drag.Key == ImGuiKey_Home)
        Parameters.HaloRadius = 0.22f;
    else if (Drag.Key != ImGuiKey_None)
        Parameters.HaloRadius = std::clamp(
            Parameters.HaloRadius + (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_DownArrow ? -0.01f : 0.01f) * Drag.Step, 0.01f,
            0.6f);
    const float Radius = Parameters.HaloRadius * 95 / 0.6f, Width = Parameters.HaloWidth * 95 / 0.6f;
    Pen.Circle({220, 130}, 105, 0, ResolveCssColour(0xBDBEA91D));
    Pen.Circle({220, 130}, Radius, 0, ResolveCssColour(0xE5D5AB66));
    Pen.Circle({220, 130}, std::max(0.0f, Radius - Width), 0, ResolveCssColour(0xE5D5AB22));
    Pen.Circle({220, 130}, Radius + Width, 0, ResolveCssColour(0xE5D5AB22));
    Pen.Line({220, 130}, {220 + Radius, 130}, ResolveCssColour(0xE5D5AB99), 2);
    Pen.Circle({220 + Radius, 130}, 6, ResolveCssColour(0xE5D5ABFF));
    Pen.Text({470, 100}, Figure("%.2f", Parameters.HaloRadius).c_str(), ResolveCssColour(0xE5D5ABFF), 40);
    Pen.Text({470, 130}, "RADIUS / UV UNITS", ResolveCssColour(0xA3A393FF), 18);
    Pen.Text({470, 180}, Figure("± %.3f", Parameters.HaloWidth).c_str(), ResolveCssColour(0xC0BAA9FF), 24);
    Pen.Text({470, 206}, "RESPONSE SUPPORT", ResolveCssColour(0xA3A393FF), 17);
    Changed |= SliderRow::Construct(Page, "FlareHaloRadius", "Halo radius", Parameters.HaloRadius, 0.01f, 0.6f, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "FlareHaloWidth", "Halo width", Parameters.HaloWidth, 0.005f, 0.2f, 0.001f, "");
    Changed |= SliderRow::Construct(Page, "FlareHaloBrightness", "Halo brightness", Parameters.HaloBrightness, 0, 2, 0.01f, "×");
    CaptionText::Construct(Page,
                           "Radius and ± width mark the smooth radial response support in aspect-corrected UV units. This enlarged guide "
                           "is not a physical lens section; the composite above is the procedural result.",
                           16, 0);
    if (Changed) Parameters.Preset = 4;
    Page.EndCard();
    return Changed;
}

bool OpticalFlareInspector::ConstructRays(ControlColumn &Page)
{
    BeginOpticalCard(Page, "Diffraction & streaks", "05 / LAYER MIXER");
    const SwitchTile Layers[] = {{"Anamorphic", &Parameters.AnamorphicStreak},
                                 {"Warm streak", &Parameters.WarmStreak},
                                 {"Starburst", &Parameters.DiffractionStarburst}};
    bool             Changed  = SwitchTiles::Construct(Page, "FlareLayers", Layers, 3, 2);
    char             Caption[100]{};
    std::snprintf(Caption, sizeof(Caption), "%u ray pairs · %s", ResolveFlareRayPairs(Parameters),
                  Parameters.Preset == 4 ? "Custom ray control" : "Legacy aperture control");
    ReadoutBlock::Construct(Page, "", Caption, "", 15, 10, 0, 1.5f);
    Changed |= SliderRow::Construct(Page, "FlareRotation", "Rotation", Parameters.RotationDegrees, 0, 360, 1, "°");
    Changed |= SliderRow::ConstructInteger(Page, "FlareRays", "Ray pairs", Parameters.RayCount, 2, 12, "");
    Changed |= SliderRow::Construct(Page, "FlareStreak", "Streak gain", Parameters.StreakStrength, 0, 3, 0.01f, "×");
    if (Changed) Parameters.Preset = 4;
    Changed |= SliderRow::ConstructInteger(Page, "FlareAperture", "Aperture blades", Parameters.ApertureBladeCount, 2, 12, "");
    CaptionText::Construct(Page,
                           "Preset starbursts use Aperture blades; Custom layers uses Ray pairs. Rotation turns the warm streak and burst, "
                           "while the cool anamorphic streak stays horizontal, matching the demo reference.",
                           16, 0);
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void OpticalFlareInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Anamorphic", "Horizontal streak", &Parameters.AnamorphicStreak});
    OutSwitches.push_back({"Starburst", "Aperture diffraction", &Parameters.DiffractionStarburst});
}

void OpticalFlareInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveOptics();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    PageHeading::Construct(Page, "LensHeader", "ENVIRONMENT / CAMERA OPTICS", "Lens flare", nullptr,
                           "Reflections, diffraction, and light across the sensor.");
    bool Changed = false;
    const ActionEntry Actions[] = {{"FlareEnabled", Parameters.Enabled ? "● Enabled" : "○ Disabled"}, {"FlareExport", "Export HDR ↗"}};
    ActionRowLook ActionsLook;
    ActionsLook.Trailing = true;
    const int Action     = ActionRow::Construct(Page, Actions, ActionsLook);
    if (Action == 0)
    {
        Parameters.Enabled = !Parameters.Enabled;
        Changed            = true;
    }
    if (Action == 1) ImGui::OpenPopup("Export HDR");
    ConstructExport();
    BeginOpticalCard(Page, "Optical composite", "01 / LIVE PROCEDURAL REFERENCE");
    DiagramDrag Drag;
    auto        Pen =
        DiagramCanvas::Begin(Page, "FlareSource", 640, 280, 15, 15, &Drag, "Drag the flare source; arrow keys move the preview light");
    if (Drag.Engaged || Drag.Key != ImGuiKey_None)
    {
        if (Drag.Engaged)
        {
            Parameters.PreviewPosition[0] = Drag.Point.x / 640;
            Parameters.PreviewPosition[1] = Drag.Point.y / 280;
        }
        else if (Drag.Key == ImGuiKey_Home)
        {
            Parameters.PreviewPosition[0] = 0.28f;
            Parameters.PreviewPosition[1] = 0.43f;
        }
        else
        {
            Parameters.PreviewPosition[0] += (Drag.Key == ImGuiKey_RightArrow  ? 0.02f
                                              : Drag.Key == ImGuiKey_LeftArrow ? -0.02f
                                                                               : 0) *
                                             Drag.Step;
            Parameters.PreviewPosition[1] += (Drag.Key == ImGuiKey_DownArrow ? 0.02f
                                              : Drag.Key == ImGuiKey_UpArrow ? -0.02f
                                                                             : 0) *
                                             Drag.Step;
        }
        Parameters.PreviewPosition[0] = std::clamp(Parameters.PreviewPosition[0], 0.06f, 0.94f);
        Parameters.PreviewPosition[1] = std::clamp(Parameters.PreviewPosition[1], 0.08f, 0.92f);
        Changed                       = true;
    }
    RefreshReference();
    Pen.Rect({0, 0}, {640, 280}, ResolveCssColour(0x080808FF), 14);
    Composite.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 280}), IM_COL32_WHITE, 14 * Page.QueryScale());
    char Source[80]{};
    std::snprintf(Source, sizeof(Source), "SOURCE %.2f / %.2f", static_cast<double>(Parameters.PreviewPosition[0]),
                  static_cast<double>(Parameters.PreviewPosition[1]));
    const ActionEntry ResetAction[] = {{"FlareSourceReset", "Reset source"}};
    ActionRowLook     ResetLook;
    ResetLook.FontSize     = 10;
    ResetLook.PaddingSide  = 10;
    ResetLook.PaddingTop   = 5;
    ResetLook.MarginTop    = 0;
    ResetLook.MarginBottom = 0;
    ResetLook.Trailing     = true;
    ResetLook.Leading      = Source;
    if (ActionRow::Construct(Page, ResetAction, ResetLook) == 0)
    {
        Parameters.PreviewPosition[0] = 0.28f;
        Parameters.PreviewPosition[1] = 0.43f;
        Changed                       = true;
    }
    CaptionText::Construct(Page,
                           "Drag the source. Authoring preview stays visible when scene flare is disabled. Display-mapped thumbnail · "
                           "export retains linear HDR.",
                           16, 0);
    if (PendingRadiance.valid()) CaptionText::Construct(Page, "Rendering reference…", 8, 0);
    if (!ReferenceDiagnostic.empty()) CaptionText::Construct(Page, ReferenceDiagnostic.c_str(), 8, 0);
    Page.EndCard();
    constexpr const char *Tabs[] = {"Compose", "Ghosts", "Halo", "Rays & streaks"};
    TabStrip::Construct(Page, "FlareTabs", Tabs, 4, ActivePane);
    switch (ActivePane)
    {
    case 0:
        Changed |= ConstructLightStack(Page);
        break;
    case 1:
        Changed |= ConstructGhosts(Page);
        break;
    case 2:
        Changed |= ConstructHalo(Page);
        break;
    case 3:
        Changed |= ConstructRays(Page);
        break;
    }
    CaptionText::Construct(Page, "The composite and HDR export use the demo’s optical equations. The viewport retains PlanetaryAtmosphere "
                                 "optics; custom layers, spacing, width, rotation and ray count are not connected to it.");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void OpticalFlareInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void OpticalFlareInspector::ConstructInspector() {}
} // namespace Frontier

#endif
