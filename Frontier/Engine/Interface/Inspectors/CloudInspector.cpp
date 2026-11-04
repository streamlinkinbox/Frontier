/*==========================================================================================================================================
                                                             CLOUDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 VehicleHMI Clouds: density atlas, formation and layer stack, rendering budget, weather and bounded local-cloud cards.

#include "Engine/Interface/Inspectors/CloudInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/CloudDensityReference.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"
#include "Engine/Interface/Inspectors/LightningInspector.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

constexpr const char *Formations[] = {"Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus"};
constexpr const char *BudgetPending =
    "The projected cloud-shadow map retains fixed softness and sampling. This artistic shadow control is not connected.";

std::string Figure(const char *Format, double Quantity)
{
    char Text[96]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

CardLook CloudCard()
{
    CardLook Look;
    Look.Fill          = ResolveCssColour(0x20221FFF);
    Look.Radius        = 22;
    Look.MarginBottom  = 14;
    Look.HeadingMargin = 13;
    return Look;
}

CardLook LocalCard()
{
    CardLook Look;
    Look.Fill            = ResolveCssColour(0x252525FF);
    Look.FillEnd         = ResolveCssColour(0x202020FF);
    Look.GradientDegrees = 135;
    Look.GradientStop    = 1;
    Look.Border          = ResolveCssColour(0x343434FF);
    Look.Radius          = 22;
    Look.PaddingTop      = 22;
    Look.PaddingSide = Look.PaddingBottom = 24;
    Look.MarginBottom                     = 16;
    Look.HeadingMargin                    = 28;
    return Look;
}

bool PlaceCloud(CloudConfiguration &Cloud, int Preset)
{
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (!Access.CameraReady) return false;
    if (Preset >= 0)
    {
        constexpr float Dimensions[3][3] = {{90, 90, 55}, {300, 160, 70}, {140, 65, 25}};
        constexpr float Features[] = {80, 120, 65}, Coverage[] = {0.72f, 0.8f, 0.58f}, Density[] = {1, 0.8f, 0.5f};
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Cloud.HalfExtentsMetres[Axis] = Dimensions[Preset][Axis];
        Cloud.LocalFeatureMetres = Features[Preset];
        Cloud.Coverage           = Coverage[Preset];
        Cloud.ExtinctionDensity  = Density[Preset];
    }
    const double Length   = std::hypot(Access.CameraForward[0], Access.CameraForward[1], Access.CameraForward[2]);
    const double Distance = std::max({80.0f, Cloud.HalfExtentsMetres[0], Cloud.HalfExtentsMetres[1], Cloud.HalfExtentsMetres[2]}) * 2.4;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        const double Direction = Length > 1.0e-6 ? Access.CameraForward[Axis] / Length : (Axis == 0 ? 1.0 : 0.0);
        Cloud.CentreMetres[Axis] =
            static_cast<float>(std::clamp(std::round(Access.CameraEye[Axis] + Direction * Distance), -100000.0, 100000.0));
    }
    Cloud.CentreMetres[2] = std::clamp(Cloud.CentreMetres[2], 5.0f, 20000.0f);
    Cloud.Enabled         = true;
    if (Cloud.Coverage <= 0) Cloud.Coverage = 0.72f;
    if (Cloud.ExtinctionDensity <= 0) Cloud.ExtinctionDensity = 1.0f;
    return true;
}

bool Shadows(ControlColumn &Page, CloudConfiguration &Cloud)
{
    bool Changed = ToggleRow::Construct(Page, "CloudShadows", "Cloud shadows", Cloud.CastGroundShadows);
    Changed |=
        SliderRow::Construct(Page, "CloudShadowStrength", "Shadow strength", Cloud.ShadowStrength, 0, 1, 0.01f, "%", 100, false,
                             "The native projected shadow uses physical optical depth; the demo's artistic strength is not connected.");
    Changed |= SliderRow::Construct(Page, "CloudShadowSoftness", "Shadow softness", Cloud.ShadowSoftnessMetres, 0, 200, 1, "m", 1, false,
                                    BudgetPending);
    Changed |= SliderRow::Construct(Page, "CloudShadowDistance", "Field span", Cloud.ShadowSpanMetres, 1000, 16000, 100, "m", 1, false,
                                    BudgetPending);
    Changed |= SliderRow::ConstructInteger(Page, "CloudShadowSamples", "Shadow samples", Cloud.ShadowSamples, 4, 12, "/ medium", false,
                                           BudgetPending);
    CaptionText::Construct(Page, "Native projected cloud shadow · direct sunlight only. GPU timing is not exposed to this page.");
    return Changed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGNOSTIC IMAGES
//------------------------------------------------------------------------------------------------------------------------

void CloudInspector::RefreshDensity(double Aspect)
{
    const auto                  &Cloud = Parameters;
    const std::array<double, 24> Query = {Aspect,
                                          double(Cloud.Enabled),
                                          double(Cloud.LocalVolume),
                                          Cloud.Coverage,
                                          Cloud.ExtinctionDensity,
                                          Cloud.BaseAltitudeMetres,
                                          Cloud.LayerThicknessMetres,
                                          Cloud.CeilingMetres,
                                          Cloud.FeatureScale,
                                          double(Cloud.Formation),
                                          Cloud.AnvilShaping,
                                          Cloud.EdgeErosion,
                                          double(Cloud.BoundsShape),
                                          double(Cloud.LocalFormation),
                                          Cloud.EdgeSoftness,
                                          Cloud.LocalFeatureMetres,
                                          Cloud.CentreMetres[0],
                                          Cloud.CentreMetres[1],
                                          Cloud.CentreMetres[2],
                                          Cloud.HalfExtentsMetres[0],
                                          Cloud.HalfExtentsMetres[1],
                                          Cloud.HalfExtentsMetres[2],
                                          0,
                                          0};
    if (Query == DensityQuery && TopDensity.QueryReady() && SideDensity.QueryReady()) return;
    std::vector<uint8_t> Top, Side;
    CloudDensityReference::ConstructImages(Cloud, Aspect, Top, Side);
    if (TopDensity.Configure(192, 96, Top) && SideDensity.Configure(192, 96, Side)) DensityQuery = Query;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OVERVIEW
//------------------------------------------------------------------------------------------------------------------------

bool CloudInspector::ConstructOverview(ControlColumn &Page)
{
    auto Look       = CloudCard();
    Look.Fill       = ResolveCssColour(0x1B241EFF);
    Look.Radius     = 24;
    Look.PaddingTop = Look.PaddingSide = Look.PaddingBottom = 0;
    Page.BeginCard(Look, nullptr, nullptr);
    const auto Origin = Page.QueryCardOrigin();
    RefreshDensity(640.0 / 440.0);
    auto Pen = DiagramCanvas::Begin(Page, "CloudAtlas", 640, 440, 0, 0);
    TopDensity.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 440}), IM_COL32_WHITE, 24 * Page.QueryScale(),
                         ImDrawFlags_RoundCornersTop);
    for (int Line = 0; Line < 7; ++Line)
        Pen.Line({30.0f + Line * 96.0f, 18}, {30.0f + Line * 96.0f, 420}, ResolveCssColour(0xBBD7C524), 1, 2, 6);
    for (float Height : {80, 180, 280, 380})
        Pen.Line({18, Height}, {622, Height}, ResolveCssColour(0xBBD7C524), 1, 2, 6);
    Pen.Line({320, 190}, {320, 250}, ResolveCssColour(0xE4EBD7AA));
    Pen.Line({290, 220}, {350, 220}, ResolveCssColour(0xE4EBD7AA));
    Pen.Circle({320, 220}, 28, 0, ResolveCssColour(0xD4E6A880));
    Pen.Circle({320, 220}, 4, ResolveCssColour(0xDCEAB4FF));
    Pen.Text({28, 410}, "XY / WORLD FIELD", ResolveCssColour(0xD8E4DBFF), 20, -1);
    Pen.Text({612, 410}, "DENSITY →", ResolveCssColour(0xD8E4DBFF), 20, 1);
    const float Scale       = Page.QueryScale();
    const auto &Faces       = Page.QueryFaces();
    const auto &Theme       = Page.QueryTheme();
    auto       *Ink         = Page.QueryDrawList();
    const int   FirstVertex = Ink->VtxBuffer.Size;
    const float ShadeHeight = Page.QueryWidth() * 440 / 640 * 0.65f;
    Ink->AddRectFilled(Origin, {Origin.x + Page.QueryWidth(), Origin.y + ShadeHeight}, ResolveCssColour(0x07130DD9), 24 * Scale,
                       ImDrawFlags_RoundCornersTop);
    for (int Vertex = FirstVertex; Vertex < Ink->VtxBuffer.Size; ++Vertex)
    {
        auto       &Point = Ink->VtxBuffer[Vertex];
        const float Fade  = 1 - std::clamp((Point.pos.y - Origin.y) / ShadeHeight, 0.0f, 1.0f);
        Point.col         = FadeColour(Point.col, Fade);
    }
    GlyphRun::Construct(Page, Faces.Regular, 12 * Scale,
                        {Origin.x + 17 * Scale, GlyphRun::ResolveBaseline(Origin.y + 15 * Scale, 12 * Scale, 1.4f)}, Theme.CardHeading,
                        "Density atlas", -0.1f * Scale);
    const float IndexWidth = GlyphRun::Measure(Page, Faces.Regular, 8 * Scale, "01 / FIELD", 0.8f * Scale);
    GlyphRun::Construct(
        Page, Faces.Regular, 8 * Scale,
        {Origin.x + Page.QueryWidth() - 17 * Scale - IndexWidth, GlyphRun::ResolveBaseline(Origin.y + 15 * Scale, 8 * Scale, 1.8f)},
        Theme.Index, "01 / FIELD", 0.8f * Scale);
    ReadoutBlock::ConstructAt(Page, {Origin.x + 17 * Scale, Origin.y + 45 * Scale}, "Coverage setting",
                              Figure("%.0f", Parameters.Coverage * 100).c_str(), "%", 43, 17, -1.8f, 1.25f, 0, ResolveCssColour(0xD0D8C5FF),
                              ResolveCssColour(0xD0D8C5FF));
    const char *FormationName = Formations[std::min<uint32_t>(5, Parameters.Formation)];
    TagPill::ConstructAt(Page, "CloudFormationTag",
                         {Origin.x + Page.QueryWidth() - 17 * Scale - TagPill::Measure(Page, FormationName), Origin.y + 45 * Scale},
                         FormationName, false, nullptr);
    const auto  Footer   = Page.Space(36.6f);
    const float Baseline = GlyphRun::ResolveBaseline(Footer.y + 12 * Scale, 9 * Scale, 1.4f);
    GlyphRun::Construct(Page, Faces.Regular, 9 * Scale, {Footer.x + 15 * Scale, Baseline}, Theme.AtlasFooterText, "Density diagnostic",
                        0.2f * Scale);
    const float FooterWidth = GlyphRun::Measure(Page, Faces.Regular, 9 * Scale, "Not a lit sky render ↗", 0.2f * Scale);
    GlyphRun::Construct(Page, Faces.Regular, 9 * Scale, {Footer.x + Page.QueryWidth() - 15 * Scale - FooterWidth, Baseline},
                        Theme.AtlasFooterText, "Not a lit sky render ↗", 0.2f * Scale);
    Page.EndCard();
    const MetricTile Metrics[] = {{"Layer base", Figure("%.2f", Parameters.BaseAltitudeMetres / 1000), "km"},
                                  {"Effective top", Figure("%.2f", EnvironmentReadings::CloudTop(Parameters) / 1000), "km"},
                                  {"GPU cloud pass", "—", "ms · unavailable"}};
    MetricTiles::Construct(Page, Metrics, 3);
    auto MixLook       = CloudCard();
    MixLook.Fill       = 0;
    MixLook.Radius     = 0;
    MixLook.TopRule    = ResolveCssColour(0xFFFFFF13);
    MixLook.PaddingTop = MixLook.PaddingBottom = 17;
    MixLook.PaddingSide                        = 0;
    Page.BeginCard(MixLook, "Shape the field", "02 / MIX");
    CaptionText::Construct(Page, "Coverage × density · drag the point or use arrow keys");
    DiagramDrag Drag;
    Pen = DiagramCanvas::Begin(Page, "CloudMix", 640, 230, 12, 12, &Drag, "Coverage and density editor");
    Pen.Rect({0, 0}, {640, 230}, ResolveCssColour(0x1E221DFF), 16 * 640 / (Page.QueryWidth() / Page.QueryScale()));
    bool Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
    {
        Parameters.Coverage          = std::clamp((Drag.Point.x - 32) / 576, 0.0f, 1.0f);
        Parameters.ExtinctionDensity = std::clamp((190 - Drag.Point.y) / 160 * 4, 0.0f, 4.0f);
    }
    if (Drag.Key == ImGuiKey_Home)
    {
        Parameters.Coverage          = 0.55f;
        Parameters.ExtinctionDensity = 1;
    }
    if (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_RightArrow)
        Parameters.Coverage = std::clamp(Parameters.Coverage + (Drag.Key == ImGuiKey_RightArrow ? 0.01f : -0.01f) * Drag.Step, 0.0f, 1.0f);
    if (Drag.Key == ImGuiKey_UpArrow || Drag.Key == ImGuiKey_DownArrow)
        Parameters.ExtinctionDensity =
            std::clamp(Parameters.ExtinctionDensity + (Drag.Key == ImGuiKey_UpArrow ? 0.05f : -0.05f) * Drag.Step, 0.0f, 4.0f);
    for (int Line = 0; Line < 5; ++Line)
        Pen.Line({32, 30.0f + Line * 40}, {608, 30.0f + Line * 40}, ResolveCssColour(0xAAB6A125), 1, 3, 7);
    for (int Line = 0; Line < 7; ++Line)
        Pen.Line({32.0f + Line * 96, 30}, {32.0f + Line * 96, 190}, ResolveCssColour(0xAAB6A117), 1, 2, 6);
    const float Along = 32 + Parameters.Coverage * 576, Height = 190 - Parameters.ExtinctionDensity / 4 * 160;
    Pen.Rect({32, Height}, {Along - 32, 190 - Height}, ResolveCssColour(0xC7DD9620), 3);
    Pen.Line({32, Height}, {Along, Height}, ResolveCssColour(0xCEDEAA80), 1, 3, 5);
    Pen.Line({Along, Height}, {Along, 190}, ResolveCssColour(0xCEDEAA80), 1, 3, 5);
    Pen.Circle({Along, Height}, 13, ResolveCssColour(0xCCE1A21C), ResolveCssColour(0xCCE1A28C));
    Pen.Circle({Along, Height}, 5, ResolveCssColour(0xD6E7AEFF));
    Pen.Text({32, 218}, "CLEAR", ResolveCssColour(0x8D9A8CFF), 14, -1);
    Pen.Text({608, 218}, "OVERCAST", ResolveCssColour(0x8D9A8CFF), 14, 1);
    Pen.Text({32, 19}, "DENSER ↑", ResolveCssColour(0x8D9A8CFF), 12, -1);
    Changed |= SliderRow::Construct(Page, "CloudCoverage", "Coverage", Parameters.Coverage, 0, 1, 0.01f, "%", 100);
    Changed |= SliderRow::Construct(Page, "CloudDensity", "Density multiplier", Parameters.ExtinctionDensity, 0, 4, 0.05f, "×");
    Page.EndCard();
    if (NextButton::Construct(Page, "FormationLink", "Formation & altitude   ↗")) ActivePane = 1;
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORMATION AND LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

bool CloudInspector::ConstructFormation(ControlColumn &Page)
{
    auto ProfileLook = CloudCard();
    ProfileLook.Fill = ResolveCssColour(0x242722FF);
    Page.BeginCard(ProfileLook, "Layer anatomy", "03 / PROFILE");
    int  Formation       = static_cast<int>(Parameters.Formation);
    bool Changed         = SelectRow::Construct(Page, "CloudType", "Formation", Formations, 6, Formation);
    Parameters.Formation = static_cast<uint32_t>(Formation);
    auto         Pen     = DiagramCanvas::Begin(Page, "CloudProfile", 640, 330, 12, 12);
    const double Top = EnvironmentReadings::CloudTop(Parameters), Depth = Top - Parameters.BaseAltitudeMetres;
    for (int Line = 0; Line < 5; ++Line)
    {
        const float Height = 40.0f + Line * 58;
        Pen.Line({65, Height}, {575, Height}, ResolveCssColour(0xD3DFC221), 1, 2, 6);
        Pen.Text({49, Height}, Figure("%.1f", (Top - Depth * Line / 4) / 1000).c_str(), ResolveCssColour(0x96A18FFF), 14, 1);
    }
    std::vector<ImVec2> Points;
    for (int Sample = 0; Sample <= 64; ++Sample)
        Points.push_back(
            {100 + static_cast<float>(EnvironmentReadings::CloudProfile(1 - Sample / 64.0, Parameters.Formation, Parameters.AnvilShaping) *
                                      340),
             40 + Sample / 64.0f * 232});
    Pen.Path(Points, ResolveCssColour(0xD4E5A8FF), 2.5f);
    Pen.Line({100, 40}, {100, 272}, ResolveCssColour(0xD4E5A83C));
    Pen.Text({103, 310}, "NORMALISED DENSITY PROFILE", ResolveCssColour(0x96A18FFF), 14, -1);
    const bool Clipped = Parameters.BaseAltitudeMetres + Parameters.LayerThicknessMetres > Parameters.CeilingMetres;
    Pen.Text({600, 30}, Clipped ? "CEILING LIMITED" : "WORLD Z / km", ResolveCssColour(0xD4E5A8FF), 13, 1);
    Changed |= SliderRow::Construct(Page, "CloudBase", "Base altitude", Parameters.BaseAltitudeMetres, 100, 20000, 100, "m");
    Changed |= SliderRow::Construct(Page, "CloudThickness", "Layer thickness", Parameters.LayerThicknessMetres, 100, 6000, 100, "m");
    Changed |= SliderRow::Construct(Page, "CloudCeiling", "Ceiling limit", Parameters.CeilingMetres, 4000, 20000, 100, "m");
    Parameters.BaseAltitudeMetres = std::min(Parameters.BaseAltitudeMetres, Parameters.CeilingMetres);
    Changed |= SliderRow::Construct(Page, "CloudScale", "Feature scale", Parameters.FeatureScale, 0.2f, 3, 0.05f, "×");
    Changed |= SliderRow::Construct(Page, "CloudAnvil", "Anvil shaping", Parameters.AnvilShaping, 0, 1, 0.01f, "%", 100);
    Changed |= SliderRow::Construct(Page, "CloudDetail", "Edge erosion", Parameters.EdgeErosion, 0, 1, 0.01f, "%", 100);
    CaptionText::Construct(Page, Clipped
                                     ? "Ceiling limited · only the effective layer depth appears in the diagnostic."
                                     : "Altitude is world Z, not height above terrain. Profile is a density envelope, not a lit render.");
    CaptionText::Construct(Page, "Formation, ceiling, scale, erosion and lighting now drive the native cloud volume. This atlas remains an "
                                 "isolated density reference, not a camera capture.");
    Page.EndCard();
    if (DisclosureRow::Construct(Page, "LayerDisclosure", "Layer stack editor ↗", LayerExpanded))
    {
        Page.BeginCard(LocalCard(), "Layer thickness", nullptr);
        ReadoutBlock::Construct(Page, "LAYER DEPTH", Figure("%.2f", Parameters.LayerThicknessMetres / 1000).c_str(), "km");
        CaptionText::Construct(Page, "Lift the top of the stack to grow the layer.");
        DiagramDrag Drag;
        Pen = DiagramCanvas::Begin(Page, "LayerStack", 340, 210, 12, 12, &Drag, "Layer thickness stack · drag top or use arrow keys");
        if (Drag.Engaged || Drag.Key != ImGuiKey_None)
        {
            float Thickness = Parameters.LayerThicknessMetres;
            if (Drag.Engaged)
                Thickness = 6000 * std::pow(std::clamp((166 - Drag.Point.y) / 120, 0.0f, 1.0f), 2.0f);
            else if (Drag.Key == ImGuiKey_Home)
                Thickness = 100;
            else
                Thickness += (Drag.Key == ImGuiKey_UpArrow || Drag.Key == ImGuiKey_RightArrow ? 100 : -100) * Drag.Step;
            Parameters.LayerThicknessMetres = std::clamp(std::round(Thickness), 100.0f, 6000.0f);
            Changed                         = true;
        }
        const float Roof =
            166 - 120 * std::sqrt(static_cast<float>((EnvironmentReadings::CloudTop(Parameters) - Parameters.BaseAltitudeMetres) / 6000));
        Pen.Rect({0, 0}, {340, 210}, ResolveCssColour(0x191D2080), 14);
        for (int Line = 0; Line < 6; ++Line)
            Pen.Line({15, 26.0f + Line * 28}, {326, 26.0f + Line * 28}, ResolveCssColour(0x76838B20), 1, 2, 5);
        Pen.Ellipse({215, 178}, 89, 18, ResolveCssColour(0x0C0F1250));
        for (int Tier = 0; Tier < 19 && Roof < 166; ++Tier)
        {
            const float Height = (Tier + 0.5f) / 19, Vertical = 166 - (166 - Roof) * Height;
            const float Radius = 58 + static_cast<float>(std::min(
                                          1.0, EnvironmentReadings::CloudProfile(Height, Parameters.Formation, Parameters.AnvilShaping))) *
                                          28;
            Pen.Ellipse({214 + std::sin(Height * 7) * 4, Vertical}, Radius, 15, ResolveCssColour(0xBDD5DF12), ResolveCssColour(0xB8D1DE24));
            Pen.Ellipse({183, Vertical - 3}, Radius * 0.45f, 11, ResolveCssColour(0xE1EFF514));
        }
        Pen.Line({58, Roof}, {58, 166}, ResolveCssColour(0xA8C7D7FF), 1.5f);
        for (float Height : {Roof, 166.0f})
            Pen.Line({73, Height}, {300, Height}, ResolveCssColour(0xCAE7F2A0), 1, 3, 4);
        Pen.Rect({48, Roof - 5}, {20, 10}, ResolveCssColour(0xBCDBE8FF), 5);
        Pen.Text({14, Roof - 13}, "TOP", ResolveCssColour(0xB9D4E0FF), 10, -1);
        Pen.Text({14, 192}, "BASE", ResolveCssColour(0x899BA6FF), 10, -1);
        Pen.Text({322, std::max(15.0f, Roof - 11)}, Figure("%.2f km", EnvironmentReadings::CloudTop(Parameters) / 1000).c_str(),
                 ResolveCssColour(0xD3E5EDFF), 11, 1);
        Changed |= SliderRow::Construct(Page, "StackThickness", "Thickness", Parameters.LayerThicknessMetres, 100, 6000, 1, "m");
        CaptionText::Construct(Page, "World Z altitude · stack is vertically exaggerated for editing.");
        Page.EndCard();
    }
    Page.BeginCard(CloudCard(), "Light response", "04 / SCATTER");
    Changed |= SliderRow::Construct(Page, "CloudAnisotropy", "Forward scattering", Parameters.PhaseAnisotropy, -0.8f, 0.8f, 0.01f, "g");
    Changed |= SliderRow::Construct(Page, "CloudAbsorption", "Light absorption", Parameters.LightAbsorption, 0, 1, 0.01f, "%", 100);
    Changed |= SliderRow::Construct(Page, "CloudPowder", "Powder response", Parameters.PowderResponse, 0, 1, 0.01f, "%", 100);
    Changed |= SliderRow::ConstructInteger(Page, "CloudLightSteps", "Sun-shadow taps", Parameters.LightSamples, 0, 4, "taps");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERING AND WEATHER
//------------------------------------------------------------------------------------------------------------------------

bool CloudInspector::ConstructRendering(ControlColumn &Page)
{
    if (Parameters.LocalVolume)
    {
        auto *Global    = InspectorRuntime::Instance().QueryEnvironmentAccess().Clouds;
        auto &Rendering = Global ? *Global : Parameters;
        Page.BeginCard(LocalCard(), "Cloud rendering · resolution & budget", nullptr);
        bool Changed =
            SliderRow::ConstructInteger(Page, "CloudResolution", "Cloud resolution", Rendering.ResolutionPercent, 25, 100, "% / axis");
        ReadoutBlock::Construct(Page, "",
                                Figure("%.1f%% pixels", Rendering.ResolutionPercent * Rendering.ResolutionPercent / 100.0).c_str(), "");
        CaptionText::Construct(Page, "Shared sky/local target scale · 50% width × 50% height ≈ 25% of full-resolution pixels. Reference "
                                     "uses a lower-resolution native ray trace with depth-aware reconstruction.");
        Changed |= SliderRow::ConstructInteger(Page, "CloudSteps", "Sky base samples", Rendering.SkySamples, 8, 128, "samples");
        Changed |= SliderRow::ConstructInteger(Page, "CloudLocalSteps", "Local base samples", Rendering.LocalSamples, 8, 128, "samples");
        CaptionText::Construct(
            Page, "Sampling and resolution control the native trace. GPU timestamps are not exposed; no timing is fabricated.");
        Page.EndCard();
        Page.BeginCard(LocalCard(), "Cloud shadows", nullptr);
        Changed |= Shadows(Page, Rendering);
        Page.EndCard();
        if (Changed && Global) Global->AdvanceRevision();
        return Changed && Global == nullptr;
    }
    Page.BeginCard(CloudCard(), "Render footprint", "05 / BUDGET");
    const MetricTile Metrics[] = {
        {"Full-frame pixel share", Figure("%.1f", Parameters.ResolutionPercent * Parameters.ResolutionPercent / 100.0), "%"},
        {"View samples", std::to_string(Parameters.SkySamples), "samples"}};
    MetricTiles::Construct(Page, Metrics, 2);
    auto        Pen   = DiagramCanvas::Begin(Page, "ResolutionDiagram", Page.QueryWidth() / Page.QueryScale(), 135, 15, 15);
    const float Width = Page.QueryWidth() / Page.QueryScale(), Fraction = Parameters.ResolutionPercent / 100.0f;
    Pen.Rect({0, 0}, {Width, 135}, ResolveCssColour(0x151A13FF), 12);
    for (float Along = 0; Along < Width; Along += 17)
        Pen.Line({Along, 0}, {Along, 135}, ResolveCssColour(0xD5E6A50A));
    for (float Height = 0; Height < 135; Height += 17)
        Pen.Line({0, Height}, {Width, Height}, ResolveCssColour(0xD5E6A50A));
    Pen.Rect({0, 135 * (1 - Fraction)}, {Width * Fraction, 135 * Fraction}, ResolveCssColour(0xD5E6A51A), 9);
    Pen.Text({Width * Fraction * 0.5f, 135 - 135 * Fraction * 0.5f}, Figure("%.0f%% / axis", Parameters.ResolutionPercent).c_str(),
             ResolveCssColour(0xD5E6A5FF), 9);
    bool Changed =
        SliderRow::ConstructInteger(Page, "CloudResolution", "Cloud resolution", Parameters.ResolutionPercent, 25, 100, "% / axis");
    Changed |= SliderRow::ConstructInteger(Page, "CloudSteps", "Sky base samples", Parameters.SkySamples, 8, 128, "samples");
    Changed |= SliderRow::ConstructInteger(Page, "CloudLocalSteps", "Local base samples", Parameters.LocalSamples, 8, 128, "samples");
    CaptionText::Construct(Page, "Pixel share describes trace samples, not a frame-time or memory prediction. View samples and sun-shadow "
                                 "taps control integration quality.");
    Page.EndCard();
    Page.BeginCard(CloudCard(), "Ground shadow field", "06 / OCCLUSION");
    Changed |= Shadows(Page, Parameters);
    Page.EndCard();
    if (const auto Readout = InspectorRuntime::Instance().QueryEnvironmentAccess().Rendering; Readout && Readout->Ready)
    {
        CaptionText::Construct(Page, ("Actual native cloud trace: " + std::to_string(Readout->CloudTraceExtent[0]) + " × " +
                                      std::to_string(Readout->CloudTraceExtent[1]) + " pixels")
                                         .c_str());
    }
    return Changed;
}

bool CloudInspector::ConstructWeather(ControlColumn &Page)
{
    Page.BeginCard(CloudCard(), "Connected atmosphere", "07 / FORCEFIELD");
    bool Changed = ToggleRow::Construct(Page, "CloudFollowWind", "Follow shared wind", Parameters.FollowWind);
    if (NextButton::Construct(Page, "OpenWind", "Open Wind ForceField   ↗")) InspectorRuntime::Instance().RequestSelection("Wind");
    CaptionText::Construct(Page, "This layer keeps its own follow switch. The same evaluated Force Field drives density advection and the "
                                 "world-vector display; bounds remain fixed.");
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (NextButton::Construct(Page, "PlaceLocalCloud", "Place a local cloud   ↗", Access.LocalCloud && Access.CameraReady,
                              "Requires a local-cloud record and a viewport camera."))
    {
        if (PlaceCloud(*Access.LocalCloud, 0)) Access.LocalCloud->AdvanceRevision();
        InspectorRuntime::Instance().RequestSelection("LocalCloud");
    }
    Page.EndCard();
    if (Access.Lightning) LightningInspector::ConstructControls(Page, *Access.Lightning);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCAL VOLUME
//------------------------------------------------------------------------------------------------------------------------

bool CloudInspector::ConstructLocal(ControlColumn &Page)
{
    const auto  Header      = Page.Space(110);
    const float HeaderScale = Page.QueryScale();
    const auto &Faces       = Page.QueryFaces();
    GlyphRun::Construct(Page, Faces.Regular, 8 * HeaderScale,
                        {Header.x, GlyphRun::ResolveBaseline(Header.y + 8 * HeaderScale, 8 * HeaderScale, 1.4f)}, Page.QueryTheme().Muted,
                        "Inspector / Environment");
    GlyphRun::Construct(Page, Faces.Regular, 25 * HeaderScale,
                        {Header.x, GlyphRun::ResolveBaseline(Header.y + 48 * HeaderScale, 25 * HeaderScale, 1.1f)}, Page.QueryTheme().Ink,
                        "Clouds", -0.8f * HeaderScale);
    GlyphRun::Construct(Page, Faces.Regular, 10 * HeaderScale,
                        {Header.x, GlyphRun::ResolveBaseline(Header.y + 83 * HeaderScale, 10 * HeaderScale, 1.4f)}, Page.QueryTheme().Muted,
                        "LOCAL VOLUMETRIC CLOUD · bounded volume");
    auto SettingsLook          = LocalCard();
    SettingsLook.HeadingMargin = 15;
    SettingsLook.PaddingBottom = 32;
    Page.BeginCard(SettingsLook, "Cloud settings", nullptr);
    const SwitchTile Switches[] = {{"Enabled", &Parameters.Enabled, TileGlyph::Sun, false, false, nullptr, "Power"},
                                   {"Follow wind", &Parameters.FollowWind, TileGlyph::Sun, false, false, nullptr, "Power"}};
    bool             Changed    = SwitchTiles::Construct(Page, "LocalCloudSwitches", Switches, 2, 2, 0, 0, true);
    Page.EndCard();
    Page.BeginCard(LocalCard(), "Local volume · quick start", nullptr);
    CaptionText::Construct(Page, "Demo-based local cloud material in movable Box/Ellipsoid bounds. Wind advects the density through this "
                                 "placed volume. Presets place it in view. Raise Centre Z to lift it clear of the ground.");
    const bool        CameraReady = InspectorRuntime::Instance().QueryEnvironmentAccess().CameraReady;
    const ActionEntry Presets[]   = {
        {"LocalCloudPuff", "Puff", CameraReady}, {"LocalCloudBank", "Bank", CameraReady}, {"LocalCloudWisp", "Wisp", CameraReady}};
    ActionRowLook ActionLook;
    ActionLook.PaddingSide  = 12;
    ActionLook.MarginTop    = 0;
    ActionLook.MarginBottom = 16;
    const int Preset        = ActionRow::Construct(Page, Presets, ActionLook);
    if (Preset >= 0) Changed |= PlaceCloud(Parameters, Preset);
    const ActionEntry Place[] = {{"LocalCloudPlace", "Place in front of camera", CameraReady, "Requires a viewport camera."}};
    if (ActionRow::Construct(Page, Place, ActionLook) == 0) Changed |= PlaceCloud(Parameters, -1);
    Page.EndCard();
    RefreshDensity(640.0 / 364.0);
    Page.BeginCard(LocalCard(), "Cloud coverage", nullptr);
    ReadoutBlock::Construct(Page, "", Figure("%.0f", Parameters.Coverage * 100).c_str(), "%");
    CaptionText::Construct(Page, "One continuous density field within the local bounds");
    auto Pen = DiagramCanvas::Begin(Page, "LocalCloudTop", 640, 364, 12, 12);
    TopDensity.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 364}));
    Pen.Text({16, 26}, "TOP-DOWN DENSITY", ResolveCssColour(0xC2D3E5FF), 20, -1);
    Changed |= SliderRow::Construct(Page, "LocalCloudCoverage", "Coverage", Parameters.Coverage, 0, 1, 0.01f, "%", 100);
    CaptionText::Construct(Page, "Static density diagnostic · not measured sky cover or a lit render");
    Page.EndCard();
    Page.BeginCard(LocalCard(), "Local bounds", nullptr);
    Pen = DiagramCanvas::Begin(Page, "LocalCloudBounds", 340, 200, 12, 12);
    const float Extent =
        std::max({1.0f, Parameters.HalfExtentsMetres[0], Parameters.HalfExtentsMetres[1], Parameters.HalfExtentsMetres[2]});
    const float RadiusX = Parameters.HalfExtentsMetres[0] / Extent * 110, RadiusY = Parameters.HalfExtentsMetres[1] / Extent * 40;
    const float RadiusZ = Parameters.HalfExtentsMetres[2] / Extent * 75;
    Pen.Ellipse({170, 104}, RadiusX, RadiusY, ResolveCssColour(0xA8CDDD0C), ResolveCssColour(0xA8CDDD80));
    for (int Axis = 0; Axis < 2; ++Axis)
    {
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 64; ++Sample)
        {
            const float θ = Sample / 64.0f * 2 * static_cast<float>(EnvironmentReadings::Pi);
            Points.push_back(
                {170 + std::cos(θ) * (Axis ? RadiusX * 0.45f : RadiusX), 104 - std::sin(θ) * RadiusZ + std::cos(θ) * (Axis ? RadiusY : 0)});
        }
        Pen.Path(Points, ResolveCssColour(0xA8CDDD90));
    }
    Pen.Line({170, 22}, {170, 181}, ResolveCssColour(0x7D98A344), 1, 2, 4);
    Pen.Circle({170, 104}, 3, ResolveCssColour(0xD8EDF5FF));
    Pen.Text({170, 194}, "Outer envelope · Z up", ResolveCssColour(0x919191FF), 9);
    Changed |= AxisFields::Construct(Page, "LocalCloudOrigin", "Placement origin · X / Y / Z (m)", Parameters.CentreMetres,
                                     {-100000, -100000, -10000}, {100000, 100000, 20000});
    Changed |= AxisFields::Construct(Page, "LocalCloudExtents", "Half extents · X / Y / Z (m)", Parameters.HalfExtentsMetres, {1, 1, 1},
                                     {10000, 10000, 10000});
    Page.EndCard();
    Page.BeginCard(LocalCard(), "Volume section", nullptr);
    CaptionText::Construct(Page, "X–Z section · actual bounded density");
    Pen = DiagramCanvas::Begin(Page, "LocalCloudSection", 640, 354, 12, 12);
    SideDensity.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 354}));
    CaptionText::Construct(
        Page, "Demo density profile and multi-octave procedural erosion. Bounds define the placed volume; wind advects its density.");
    Page.EndCard();
    Page.BeginCard(LocalCard(), "Cloud body", nullptr);
    Changed |= SliderRow::Construct(Page, "LocalCloudDensity", "Density", Parameters.ExtinctionDensity, 0, 4, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "LocalCloudScale", "Feature Scale", Parameters.LocalFeatureMetres, 2, 200, 1, "m");
    Changed |= SliderRow::Construct(Page, "LocalCloudAnisotropy", "Forward scattering", Parameters.PhaseAnisotropy, -0.8f, 0.8f, 0.01f, "");
    Page.EndCard();
    Page.BeginCard(LocalCard(), "Density & light response", nullptr);
    Changed |= SliderRow::Construct(Page, "LocalCloudDetail", "Edge erosion", Parameters.EdgeErosion, 0, 1, 0.01f, "");
    constexpr const char *Shapes[]          = {"Ellipsoid", "Box", "Sphere", "Dome"};
    constexpr const char *LocalFormations[] = {"Stratiform", "Cumuliform", "Wispy"};
    int                   Shape = static_cast<int>(Parameters.BoundsShape), Formation = static_cast<int>(Parameters.LocalFormation);
    Changed |= SelectRow::Construct(Page, "LocalCloudShape", "Shape", Shapes, 4, Shape);
    Changed |= SelectRow::Construct(Page, "LocalCloudForm", "Formation", LocalFormations, 3, Formation);
    Parameters.BoundsShape    = Shape;
    Parameters.LocalFormation = Formation;
    Changed |= SliderRow::Construct(Page, "LocalCloudSoftness", "Edge softness", Parameters.EdgeSoftness, 0.05f, 1, 0.01f, "");
    auto *Global          = InspectorRuntime::Instance().QueryEnvironmentAccess().Clouds;
    auto &Lighting        = Global ? *Global : Parameters;
    bool  LightingChanged = SliderRow::ConstructInteger(Page, "CloudLightSteps", "Sun-shadow taps", Lighting.LightSamples, 0, 4, "taps");
    LightingChanged |= SliderRow::Construct(Page, "CloudAbsorption", "Light absorption", Lighting.LightAbsorption, 0, 1, .01f, "");
    LightingChanged |= SliderRow::Construct(Page, "CloudPowder", "Powder response", Lighting.PowderResponse, 0, 1, .01f, "");
    if (LightingChanged && Global) Global->AdvanceRevision();
    Changed |= LightingChanged && !Global;
    Page.EndCard();
    CaptionText::Construct(Page, "The bounded volume renders natively. Follow wind advects its density through fixed bounds; camera "
                                 "placement and presets enable the volume.");
    Changed |= ConstructRendering(Page);
    if (auto *Lightning = InspectorRuntime::Instance().QueryEnvironmentAccess().Lightning)
        LightningInspector::ConstructControls(Page, *Lightning);
    if (NextButton::Construct(Page, "LocalWind", "Open Wind ForceField")) InspectorRuntime::Instance().RequestSelection("Wind");
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void CloudInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Follow wind", "Density advection", &Parameters.FollowWind});
    OutSwitches.push_back({"Ground Shadows", "Transmittance", &Parameters.CastGroundShadows});
}

void CloudInspector::ConstructInspector()
{
    const auto    Theme = Parameters.LocalVolume ? ControlTheme::ResolveNative() : ControlTheme::ResolveCloud();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;
    if (Parameters.LocalVolume)
        Changed = ConstructLocal(Page);
    else
    {
        if (PageHeading::Construct(Page, "CloudPower", "ENVIRONMENT  /  01", "Clouds", Parameters.Enabled ? "● Live" : "○ Off",
                                   "Density field / shape & illumination.", "Toggle global clouds"))
        {
            Parameters.Enabled = !Parameters.Enabled;
            Changed            = true;
        }
        constexpr const char *Tabs[] = {"Overview", "Formation", "Rendering", "Weather"};
        TabStrip::Construct(Page, "CloudTabs", Tabs, 4, ActivePane);
        switch (ActivePane)
        {
        case 0:
            Changed |= ConstructOverview(Page);
            break;
        case 1:
            Changed |= ConstructFormation(Page);
            break;
        case 2:
            Changed |= ConstructRendering(Page);
            break;
        case 3:
            Changed |= ConstructWeather(Page);
            break;
        }
        FooterRow::Construct(Page, "DENSITY FIELD REFERENCE", "Native availability stated above");
    }
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void CloudInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void CloudInspector::ConstructInspector() {}
} // namespace Frontier

#endif
