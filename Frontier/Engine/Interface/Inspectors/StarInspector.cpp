/*==========================================================================================================================================
                                                             STARINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 StarsInspectorInfographics.js: the original catalogue, magnitude census and three-frequency scintillation reference.

#include "Engine/Interface/Inspectors/StarInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <cstdio>
#include <string>

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

std::string Thousands(uint32_t Count)
{
    std::string Text = std::to_string(Count);
    for (int Digit = static_cast<int>(Text.size()) - 3; Digit > 0; Digit -= 3)
        Text.insert(Digit, ",");
    return Text;
}

void BeginStarCard(ControlColumn &Page, const char *Heading, const char *Caption, ImU32 Fill, float Radius = 25)
{
    CardLook Look;
    Look.Fill          = Fill;
    Look.Radius        = Radius;
    Look.PaddingTop    = 17;
    Look.PaddingSide   = 14;
    Look.PaddingBottom = 10;
    Look.MarginBottom  = 23;
    if (Radius == 25)
    {
        Look.Border     = ResolveCssColour(0xAFC9EA14);
        Look.Glow       = ResolveCssColour(0x29354F20);
        Look.GlowCentre = {0.5f, 0.6f};
        Look.GlowExtent = 0.65f;
    }
    if (Radius == 4)
    {
        Look.LeftRule      = ResolveCssColour(0x9BADC14A);
        Look.PaddingBottom = 17;
    }
    if (Radius == 24)
    {
        Look.FillEnd         = ResolveCssColour(0x1D2028FF);
        Look.GradientDegrees = 150;
        Look.GradientStop    = 1;
        Look.PaddingSide     = 15;
        Look.PaddingBottom   = 17;
    }
    Page.BeginCard(Look, nullptr, nullptr);
    SectionHeading::Construct(Page, Heading, Caption, SectionCaptionPlacement::Beside);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLANISPHERE
//------------------------------------------------------------------------------------------------------------------------

bool StarInspector::ConstructMap(ControlColumn &Page)
{
    BeginStarCard(Page, "Celestial atlas", "EQUAL-AREA / ALL SKY", ResolveCssColour(0x10151CFF));
    const ImVec2 Origin = Page.QueryCardOrigin();
    ReadoutBlock::ConstructAt(Page, {Origin.x + 16 * Page.QueryScale(), Origin.y + 48 * Page.QueryScale()}, "Catalogue entries",
                              Thousands(Census.Eligible).c_str(), "meet magnitude cutoff", 33, 8, -1.2f, 1.15f, 0,
                              ResolveCssColour(0x8E9CAFFF), ResolveCssColour(0x8595ACFF), 9, 6, true);
    DiagramDrag Drag;
    auto Pen = DiagramCanvas::Begin(Page, "StarChart", 640, 580, 0, 0, &Drag, "Catalogue planisphere · drag or use arrow keys to rotate");
    bool Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
        Parameters.RotationDegrees = EnvironmentReadings::WrapDegrees(
            static_cast<float>(std::atan2(290 - Drag.Point.y, Drag.Point.x - 320) * 180 / EnvironmentReadings::Pi));
    else if (Drag.Key == ImGuiKey_Home)
        Parameters.RotationDegrees = 0;
    else if (Drag.Key != ImGuiKey_None)
        Parameters.RotationDegrees =
            EnvironmentReadings::WrapDegrees(Parameters.RotationDegrees + (Drag.Key == ImGuiKey_LeftArrow ? -1 : 1) * Drag.Step);
    const double Rotation = Parameters.RotationDegrees * EnvironmentReadings::Pi / 180;
    for (const int Declination : {-60, -30, 0, 30, 60})
        Pen.Circle({320, 290}, static_cast<float>(std::sqrt((1 - std::sin(Declination * EnvironmentReadings::Pi / 180)) / 2) * 228), 0,
                   ResolveCssColour(Declination == 0 ? 0xB8C6E04A : 0xB8C6E019));
    for (int Meridian = 0; Meridian < 12; ++Meridian)
    {
        const double θ = Meridian * EnvironmentReadings::Pi / 6 - Rotation;
        Pen.Line({320, 290}, {320 + static_cast<float>(std::cos(θ) * 228), 290 + static_cast<float>(std::sin(θ) * 228)},
                 ResolveCssColour(0xB8C6E018), 1, 2, 6);
    }
    Pen.Circle({320, 290}, 228, 0, ResolveCssColour(0xC4CCE34A));
    const double Threshold = std::pow(10.0, -0.4 * Parameters.LimitingMagnitude);
    for (const auto &Star : Catalogue.Entries)
    {
        if (Star[3] < Threshold) continue;
        const auto Position = EnvironmentReadings::StarChartPoint(Star, Parameters.RotationDegrees);
        const auto Channel  = [](float Linear)
        { return static_cast<int>(std::lround(std::pow(std::clamp(Linear, 0.0f, 1.0f), 1 / 2.2) * 255)); };
        const double FluxRoot = std::sqrt(std::max(0.0f, Star[3]));
        const int    Alpha    = static_cast<int>(std::clamp((0.25 + FluxRoot * 0.35) * Parameters.MagnitudeGain, 0.0, 1.0) * 255);
        Pen.Circle({320 + static_cast<float>(Position[0] * 228), 290 + static_cast<float>(Position[1] * 228)},
                   static_cast<float>(std::clamp(0.65 + FluxRoot * 0.65, 0.55, 3.2)) * Parameters.PointSize,
                   IM_COL32(Channel(Star[4]), Channel(Star[5]), Channel(Star[6]), Alpha));
    }
    Pen.Circle({320, 290}, 5, 0, ResolveCssColour(0xD3DCED99));
    Pen.Line({306, 290}, {334, 290}, ResolveCssColour(0xD3DCED66));
    Pen.Line({320, 276}, {320, 304}, ResolveCssColour(0xD3DCED66));
    constexpr const char *Hours[] = {"0h", "6h", "12h", "18h"};
    for (int Meridian = 0; Meridian < 4; ++Meridian)
    {
        const double θ = Meridian * EnvironmentReadings::Pi / 2 - Rotation;
        Pen.Text({320 + static_cast<float>(std::cos(θ) * 258), 290 - static_cast<float>(std::sin(θ) * 258)}, Hours[Meridian],
                 ResolveCssColour(0x9CA9C1FF), 20);
    }
    Pen.Text({28, 556}, "NORTH POLE / CENTRE", ResolveCssColour(0x8695AFFF), 18, -1);
    Pen.Text({612, 556}, "DRAG TO ROTATE", ResolveCssColour(0x8695AFFF), 18, 1);
    CaptionText::Construct(Page,
                           "Actual catalogue directions and colours. Equal-area projection about the north celestial pole; not your local "
                           "horizon or a lit camera view.",
                           4, 10);
    if (!Catalogue.Diagnostic.empty()) CaptionText::Construct(Page, Catalogue.Diagnostic.c_str());
    Page.EndCard();
    Changed |= SwitchRow::Construct(Page, "StarsEnabled", "Star field", Parameters.Enabled);
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    CaptionText::Construct(Page,
                           !Parameters.Enabled ? "Star renderer disabled · atlas remains a catalogue reference."
                           : Access.Solar && Access.Solar->Enabled && Access.Solar->ElevationDegrees >= -3
                               ? "Daylight masks the field. Preview a clear night to inspect it."
                           : Access.Clouds && Access.Clouds->Enabled ? "Night sky · clouds can obscure catalogue stars."
                                                                     : "Clear night configuration · look above the horizon.",
                           14, 14);
    if (NextButton::Construct(Page, "PreviewClearNight", "Preview clear night  ↗", Access.Solar != nullptr,
                              "A Sun record is required for the clear-night action."))
    {
        Access.Solar->TimeLinked       = false;
        Access.Solar->DayCycle         = false;
        Access.Solar->ElevationDegrees = -18;
        Access.Solar->AdvanceRevision();
        for (auto *Cloud : {Access.Clouds, Access.LocalCloud})
        {
            if (Cloud != nullptr)
            {
                Cloud->Enabled = false;
                Cloud->AdvanceRevision();
            }
        }
        if (Access.LocalFog != nullptr)
        {
            Access.LocalFog->Enabled = false;
            Access.LocalFog->AdvanceRevision();
        }
        Parameters.Enabled = true;
        Changed            = true;
    }
    CaptionText::Construct(
        Page, "Night preview lowers the Sun to −18° and disables global/local clouds and local fog. Moonlight is retained.", 10, 10);
    Changed |= SliderRow::Construct(Page, "StarRotation", "Catalogue rotation", Parameters.RotationDegrees, 0, 360, 1, "°");
    Parameters.RotationDegrees = EnvironmentReadings::WrapDegrees(Parameters.RotationDegrees);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CENSUS AND POINT SPREAD
//------------------------------------------------------------------------------------------------------------------------

bool StarInspector::ConstructPopulation(ControlColumn &Page)
{
    SectionHeading::Construct(Page, "Magnitude census", "ACTUAL CATALOGUE COUNTS", SectionCaptionPlacement::Beside);
    const ImVec2 Summary = Page.Space(86, 10, 12);
    const float  Scale = Page.QueryScale(), Half = Page.QueryWidth() / 2;
    ReadoutBlock::ConstructAt(Page, Summary, "Eligible", Thousands(Census.Eligible).c_str(), "", 35, 9, -1.2f, 1.15f);
    ReadoutBlock::ConstructAt(Page, {Summary.x + Half + 20 * Scale, Summary.y}, "Catalogue total",
                              Thousands(static_cast<uint32_t>(Catalogue.Entries.size())).c_str(), "", 35, 9, -1.2f, 1.15f);
    Page.QueryDrawList()->AddLine({Summary.x + Half, Summary.y}, {Summary.x + Half, Summary.y + 68 * Scale}, ResolveCssColour(0xB5C7E319));
    Page.QueryDrawList()->AddLine({Summary.x, Summary.y + 86 * Scale}, {Summary.x + Page.QueryWidth(), Summary.y + 86 * Scale},
                                  ResolveCssColour(0xB5C7E319));
    DiagramDrag Drag;
    auto        Pen = DiagramCanvas::Begin(Page, "StarCensus", 640, 320, 0, 14, &Drag, "Magnitude cutoff editor · drag or use arrow keys");
    bool        Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
        Parameters.LimitingMagnitude = std::clamp(std::round(((Drag.Point.x - 40) / 560 * 10 - 2) * 10) / 10, 0.0f, 8.0f);
    else if (Drag.Key == ImGuiKey_Home)
        Parameters.LimitingMagnitude = 0;
    else if (Drag.Key != ImGuiKey_None)
        Parameters.LimitingMagnitude =
            std::clamp(Parameters.LimitingMagnitude + (Drag.Key == ImGuiKey_LeftArrow ? -0.1f : 0.1f) * Drag.Step, 0.0f, 8.0f);
    const float Peak = static_cast<float>(std::max(1u, *std::max_element(Census.TotalBins.begin(), Census.TotalBins.end())));
    for (size_t Bin = 0; Bin < 10; ++Bin)
    {
        const float Height = Census.TotalBins[Bin] / Peak * 180, Left = 45.0f + static_cast<float>(Bin) * 56;
        Pen.Rect({Left, 230 - Height}, {39, Height}, ResolveCssColour(0xB3C6E332), 4);
        const float Eligible = Census.EligibleBins[Bin] / Peak * 180;
        Pen.Rect({Left, 230 - Eligible}, {39, Eligible}, ResolveCssColour(0xB3C6E3AA), 4);
        Pen.Text({Left + 19, 260}, std::to_string(static_cast<int>(Bin) - 2).c_str(), ResolveCssColour(0x8E9CB5FF), 20);
    }
    const float Cutoff = 40 + (Parameters.LimitingMagnitude + 2) / 10 * 560;
    Pen.Line({Cutoff, 30}, {Cutoff, 240}, ResolveCssColour(0xEBD8ABFF), 2, 3, 6);
    Pen.Circle({Cutoff, 30}, 7, ResolveCssColour(0xEBD8ABFF));
    Pen.Text({40, 303}, "BRIGHTER", ResolveCssColour(0x8E9CB5FF), 20, -1);
    Pen.Text({600, 303}, "FAINTER / mag", ResolveCssColour(0x8E9CB5FF), 20, 1);
    Changed |= SliderRow::Construct(Page, "StarLimit", "Limiting magnitude", Parameters.LimitingMagnitude, 0, 8, 0.1f, "mag");
    CaptionText::Construct(Page,
                           "Muted bars: total; bright bars: eligible. One-magnitude bins, clipped to −2…8. Eligible is the flux cutoff "
                           "count, not visible screen pixels; daylight, horizon and clouds are excluded.",
                           10, 23);
    BeginStarCard(Page, "Point spread", "RENDERER CONTROLS", ResolveCssColour(0x20262EFF), 4);
    Changed |= SliderRow::Construct(Page, "StarBrightness", "Star brightness", Parameters.MagnitudeGain, 0, 5, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "StarSize", "Point size", Parameters.PointSize, 0.2f, 3, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "StarHalo", "Star halo", Parameters.HaloGain, 0, 2, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "MilkyWay", "Milky Way", Parameters.MilkyWayLuminance, 0, 3, 0.01f, "×");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCINTILLATION REFERENCE
//------------------------------------------------------------------------------------------------------------------------

bool StarInspector::ConstructScintillation(ControlColumn &Page)
{
    BeginStarCard(Page, "One star, four seconds", "SHADER-MATCHED REFERENCE", ResolveCssColour(0x292633FF), 24);
    bool       Changed   = SwitchRow::Construct(Page, "TwinkleEnabled", "Scintillation", Parameters.ScintillationEnabled);
    const auto Brightest = std::max_element(Catalogue.Entries.begin(), Catalogue.Entries.end(),
                                            [](const auto &Left, const auto &Right) { return Left[3] < Right[3]; });
    auto       Pen       = DiagramCanvas::Begin(Page, "StarScintillation", 640, 330, 15, 15);
    for (int Stripe = 0; Stripe < 32; ++Stripe)
        Pen.Line({Stripe * 20.0f, 0}, {Stripe * 20.0f, 330}, ResolveCssColour(0xB9B9D805));
    // 📝 150 px per unit of gain: a full-depth twinkle (0 … 2) still fits the 330 px plot.
    for (const float Gain : {0.5f, 1.0f, 1.5f})
    {
        const float Height = 165 - (Gain - 1) * 150;
        Pen.Line({65, Height}, {606, Height}, ResolveCssColour(0xB9B9D827), 1, 2, 6);
        Pen.Text({50, Height + 5}, Figure("%.1f", Gain).c_str(), ResolveCssColour(0x9497ADFF), 20, 1);
    }
    if (Brightest != Catalogue.Entries.end())
    {
        const double        Time = std::floor(ImGui::GetTime() * 8) / 8;
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 160; ++Sample)
            Points.push_back(
                {65 + Sample / 160.0f * 541,
                 165 - static_cast<float>((EnvironmentReadings::StarScintillation(Time - 4 + Sample / 40.0, Parameters, *Brightest) - 1) *
                                          150)});
        Pen.Path(Points, ResolveCssColour(0xD7C4E5FF), 2.4f);
        Pen.Circle(Points.back(), 6, ResolveCssColour(0xEBD8F4FF));
    }
    else
        Pen.Text({320, 165}, "Catalogue unavailable", ResolveCssColour(0x9497ADFF), 20);
    Pen.Text({65, 300}, "−4 s", ResolveCssColour(0x9497ADFF), 20, -1);
    Pen.Text({606, 300}, "NOW", ResolveCssColour(0x9497ADFF), 20, 1);
    Changed |= SliderRow::Construct(Page, "TwinkleDepth", "Twinkle depth", Parameters.AtmosphericScintillate, 0, 1, 0.01f, "%", 100);
    Changed |= SliderRow::Construct(Page, "TwinkleRate", "Twinkle rate", Parameters.ScintillationRateHz, 0.2f, 3, 0.1f, "Hz");
    CaptionText::Construct(
        Page,
        "The sky's own twinkle for the brightest catalogue entry at the zenith (the demo's three-frequency signal; depth is the peak "
        "swing), sampled at 8 Hz on the UI clock. Toward the horizon the sky deepens it: × 1.33 at 30°, × 1.9 at 10°. Relative gain, "
        "not a capture.",
        10, 10);
    Page.EndCard();
    return Changed;
}

void StarInspector::ConstructInspector()
{
    if (!CatalogueLoaded)
    {
        Catalogue       = ReadStarCatalogue(ResolveContentDirectory("EngineContent") / "EnvironmentArchives/CelestialMaps/BrightStars.bin");
        CatalogueLoaded = true;
    }
    if (CensusLimit != Parameters.LimitingMagnitude)
    {
        Census      = EvaluateStarCensus(Catalogue.Entries, Parameters.LimitingMagnitude);
        CensusLimit = Parameters.LimitingMagnitude;
    }
    const auto    Theme = ControlTheme::ResolveStars();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    PageHeading::Construct(Page, "StarsHeader", "ENVIRONMENT / CELESTIAL", "Stars", nullptr, "A catalogue of light. Not a field of noise.");
    constexpr const char *Tabs[] = {"Star map", "Population", "Scintillation"};
    TabStrip::Construct(Page, "StarsTabs", Tabs, 3, ActivePane);
    bool Changed = false;
    switch (ActivePane)
    {
    case 0:
        Changed = ConstructMap(Page);
        break;
    case 1:
        Changed = ConstructPopulation(Page);
        break;
    case 2:
        Changed = ConstructScintillation(Page);
        break;
    }
    CaptionText::Construct(Page, "SLAT catalogue reference. Rotation, cutoff, point size and twinkle rate edit these instruments; the "
                                 "viewport retains its native Hipparcos catalogue and controls.");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void StarInspector::ConstructInspector() {}
} // namespace Frontier

#endif
