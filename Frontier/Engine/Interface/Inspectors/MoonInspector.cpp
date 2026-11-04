/*==========================================================================================================================================
                                                             MOONINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 MoonInspectorInfographics.js and MoonCards.css: four instances, registered surfaces, orbital and orientation controls.

#include "Engine/Interface/Inspectors/MoonInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <imgui_internal.h>

#include <cstdio>

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

void BeginMoonCard(ControlColumn &Page, const char *Heading, bool Settings = false)
{
    CardLook Look;
    Look.HeadingGlyph    = "NativeMoon";
    Look.HeadingMargin   = 20;
    Look.Fill            = ResolveCssColour(0x272B30FF);
    Look.FillEnd         = ResolveCssColour(0x21252AFF);
    Look.GradientDegrees = 135;
    Look.GradientStop    = 1;
    Look.Border          = ResolveCssColour(0xB1C7E11B);
    Look.Radius          = 22;
    Look.PaddingTop = Look.PaddingSide = 18;
    Look.PaddingBottom                 = Settings ? 19.0f : 20.0f;
    Look.MarginBottom                  = 15;
    Page.BeginCard(Look, Heading, nullptr);
}

std::array<double, 7> SurfaceQueryFor(const MoonInstanceConfiguration &Moon)
{
    return {static_cast<double>(Moon.Body), Moon.PhaseDays, Moon.RollDegrees, Moon.PitchDegrees, Moon.SpinDegrees, Moon.Brightness, 1};
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    REGISTERED SURFACE PREVIEW
//------------------------------------------------------------------------------------------------------------------------

void MoonInspector::RefreshSurface()
{
    const auto &Moon  = Parameters.Instances[ActiveInstance];
    const auto  Query = SurfaceQueryFor(Moon);
    if (Surface.QueryReady() && SurfaceQuery == Query) return;
    if (ImGui::GetTime() - LastSurfaceTime < 0.25 && Surface.QueryReady()) return;
    const uint32_t Body = std::min(Moon.Body, 5u);
    if (!SurfacesLoaded[Body])
    {
        Surfaces[Body]       = ReadLunarSurface(ResolveContentDirectory("EngineContent") / "EnvironmentArchives/CelestialMaps" /
                                                LunarSurfacePresets[Body].Filename);
        SurfacesLoaded[Body] = true;
    }
    if (Surfaces[Body].Rgb.empty()) Surface.Reset();
    const auto Pixels = GenerateLunarReference(Surfaces[Body], Moon);
    if (!Pixels.empty() && Surface.Configure(256, 256, Pixels)) SurfaceQuery = Query;
    LastSurfaceTime = ImGui::GetTime();
}

void MoonInspector::ConstructSurface(const DiagramPen &Pen, bool Comparison, bool Guide)
{
    const auto &Moon   = Parameters.Instances[ActiveInstance];
    const float Radius = Comparison ? 20 + 54 * std::sqrt(Moon.AngularDiameterDegrees / (Moon.AngularDiameterDegrees + 12)) : 60;
    if (Comparison) Pen.Circle({128, 104}, 64, 0, ResolveCssColour(0xAAB9CC22));
    if (Surface.QueryReady())
        Surface.Construct(Pen.QueryDrawList(), Pen.QueryScreen({128 - Radius, 104 - Radius}),
                          Pen.QueryScreen({128 + Radius, 104 + Radius}));
    else
        Pen.Text({128, 104}, "Registered surface unavailable", ResolveCssColour(0x869BB6FF), 10);
    const float Roll = static_cast<float>(Moon.RollDegrees * EnvironmentReadings::Pi / 180);
    Pen.Line({128 - std::sin(Roll) * (Radius + 4), 104 - std::cos(Roll) * (Radius + 4)},
             {128 - std::sin(Roll) * (Radius + 10), 104 - std::cos(Roll) * (Radius + 10)}, ResolveCssColour(0xC5D9F2FF));
    if (Guide)
    {
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 120; ++Sample)
        {
            const double θ = Sample * EnvironmentReadings::Pi / 60;
            Points.push_back(
                {128 + static_cast<float>(std::cos(θ)) * Radius,
                 104 + static_cast<float>(std::sin(θ) * std::sin(Moon.PitchDegrees * EnvironmentReadings::Pi / 180)) * Radius});
        }
        Pen.Path(Points, ResolveCssColour(0xD3E2F059), 0.7f, 2, 3);
    }
    char Caption[96]{};
    if (Comparison)
        std::snprintf(Caption, sizeof(Caption), "%.2f° angular diameter", static_cast<double>(Moon.AngularDiameterDegrees));
    else
        std::snprintf(Caption, sizeof(Caption), "Roll %.0f° · Pitch %.0f°", static_cast<double>(Moon.RollDegrees),
                      static_cast<double>(Moon.PitchDegrees));
    Pen.Text({128, 202}, Caption, ResolveCssColour(0x869BB6FF), 9);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHASE AND ORIENTATION
//------------------------------------------------------------------------------------------------------------------------

bool MoonInspector::ConstructPhase(ControlColumn &Page)
{
    auto &Moon = Parameters.Instances[ActiveInstance];
    BeginMoonCard(Page, "Lunar phase");
    const auto  Origin = Page.Space(34, 0, 12);
    const float Scale  = Page.QueryScale();
    const auto  Age    = Figure("%.1f d", Moon.PhaseDays);
    ReadoutBlock::ConstructAt(Page, Origin, "", Age.c_str(), "", 29, 10, -0.7f, 1.15f);
    const auto  Illumination      = Figure("%.1f%% lit", EnvironmentReadings::LunarIllumination(Moon.PhaseDays) * 100);
    const char *Phase             = ResolveLunarPhaseName(Moon.PhaseDays);
    const float IlluminationWidth = GlyphRun::Measure(Page, Page.QueryFaces().Regular, 10 * Scale, Illumination.c_str());
    const float NameWidth         = GlyphRun::Measure(Page, Page.QueryFaces().Regular, 10 * Scale, Phase);
    const float Right = Origin.x + Page.QueryWidth(), Baseline = GlyphRun::ResolveBaseline(Origin.y, 29 * Scale, 1.15f);
    const float AgeWidth = GlyphRun::Measure(Page, Page.QueryFaces().Light, 29 * Scale, Age.c_str(), -0.7f * Scale);
    if (AgeWidth + NameWidth + IlluminationWidth + 24 * Scale <= Page.QueryWidth())
    {
        (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10 * Scale,
                                  {Right - IlluminationWidth - 12 * Scale - NameWidth, Baseline}, ResolveCssColour(0xA5B6CAFF), Phase);
        (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10 * Scale, {Right - IlluminationWidth, Baseline},
                                  ResolveCssColour(0xD7E3F1FF), Illumination.c_str());
    }
    else
    {
        const auto SecondLine = Page.Space(14, 6, 12);
        (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10 * Scale,
                                  {SecondLine.x, GlyphRun::ResolveBaseline(SecondLine.y, 10 * Scale, 1.4f)}, ResolveCssColour(0xA5B6CAFF),
                                  Phase);
        (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10 * Scale,
                                  {Right - IlluminationWidth, GlyphRun::ResolveBaseline(SecondLine.y, 10 * Scale, 1.4f)},
                                  ResolveCssColour(0xD7E3F1FF), Illumination.c_str());
    }
    auto Pen = DiagramCanvas::Begin(Page, "LunarPhase", 640, 440, 7, 13);
    if (Surface.QueryReady())
        Surface.Construct(Pen.QueryDrawList(), Pen.QueryScreen({135, 10}), Pen.QueryScreen({505, 380}));
    else
        Pen.Text({320, 190}, "Registered surface unavailable", ResolveCssColour(0x96A8BFFF), 22);
    const float Roll = static_cast<float>(Moon.RollDegrees * EnvironmentReadings::Pi / 180);
    Pen.Line({320 - std::sin(Roll) * 191, 195 - std::cos(Roll) * 191}, {320 - std::sin(Roll) * 204, 195 - std::cos(Roll) * 204},
             ResolveCssColour(0xB7C9DF88), 1.5f);
    Pen.Text({320, 424}, LunarSurfacePresets[std::min(Moon.Body, 5u)].Name, ResolveCssColour(0xACBDD3FF), 23);
    bool                  Changed  = SliderRow::Construct(Page, "MoonPhase", "Phase age", Moon.PhaseDays, 0, 29.53f, 0.01f, "days");
    constexpr const char *Stops[]  = {"New", "First", "Full", "Last"};
    int                   Selected = -1;
    for (int Stop = 0; Stop < 4; ++Stop)
        if (std::abs(std::fmod(Moon.PhaseDays, 29.53f) - 29.53f * Stop / 4) < 0.01f) Selected = Stop;
    const int Stop = PresetStrip::Construct(Page, "MoonPhaseStops", Stops, 4, Selected);
    if (Stop >= 0)
    {
        Moon.PhaseDays = 29.53f * Stop / 4;
        Changed        = true;
    }
    if (Changed) Moon.FollowSky = false;
    CaptionText::Construct(Page, "Original textured sphere · isolated phase preview. Editing phase switches off Follow sky.", 13, 0);
    Page.EndCard();
    return Changed;
}

bool MoonInspector::ConstructPlacement(ControlColumn &Page)
{
    auto &Moon = Parameters.Instances[ActiveInstance];
    BeginMoonCard(Page, "Lunar position");
    CaptionText::Construct(Page, "Drag the celestial globe to place this body.");
    DiagramDrag Drag;
    auto        Pen = DiagramCanvas::Begin(Page, "LunarGlobe", 360, 257, 15, 15, &Drag, "Lunar position globe · drag or use arrow keys");
    bool        Positioned = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
    {
        Moon.AzimuthDegrees += Drag.Delta.x * 0.7f;
        Moon.ElevationDegrees -= Drag.Delta.y * 0.65f;
    }
    else if (Drag.Key == ImGuiKey_Home)
        Moon.AzimuthDegrees = Moon.ElevationDegrees = 0;
    else if (Drag.Key != ImGuiKey_None)
    {
        Moon.AzimuthDegrees += (Drag.Key == ImGuiKey_RightArrow ? 1 : Drag.Key == ImGuiKey_LeftArrow ? -1 : 0) * Drag.Step;
        Moon.ElevationDegrees += (Drag.Key == ImGuiKey_UpArrow ? 1 : Drag.Key == ImGuiKey_DownArrow ? -1 : 0) * Drag.Step;
    }
    Moon.AzimuthDegrees   = EnvironmentReadings::WrapDegrees(Moon.AzimuthDegrees + 180) - 180;
    Moon.ElevationDegrees = std::clamp(Moon.ElevationDegrees, -90.0f, 90.0f);
    const auto Project    = [](double Azimuth, double Elevation)
    {
        const double θ = Azimuth * EnvironmentReadings::Pi / 180, φ = Elevation * EnvironmentReadings::Pi / 180;
        const double Along = std::sin(θ) * std::cos(φ), Forward = std::cos(θ) * std::cos(φ), Across = std::sin(φ);
        return std::array<float, 3>{180 + static_cast<float>(85 * Along),
                                    117 - static_cast<float>(85 * (Across * std::cos(0.34) - Forward * std::sin(0.34))),
                                    static_cast<float>(Forward * std::cos(0.34) + Across * std::sin(0.34))};
    };
    Pen.Circle({180, 117}, 102, 0, ResolveCssColour(0x91A9C022));
    for (int Tick = 0; Tick < 36; ++Tick)
    {
        const float θ     = static_cast<float>(Tick * EnvironmentReadings::Pi / 18);
        const float Inner = 102.0f - (Tick % 3 == 0 ? 5 : 2);
        Pen.Line({180 + 102 * std::sin(θ), 117 - 102 * std::cos(θ)}, {180 + Inner * std::sin(θ), 117 - Inner * std::cos(θ)},
                 ResolveCssColour(0x8098B073), 0.7f);
    }
    Pen.Circle({180, 117}, 85, ResolveCssColour(0x151B25FF), ResolveCssColour(0x7897BA55));
    Pen.Glow({165, 102}, 80, ResolveCssColour(0x455263FF), 0.7f);
    std::vector<ImVec2> Points;
    for (int Latitude : {-60, -30, 0, 30, 60})
    {
        Points.clear();
        for (int Sample = 0; Sample <= 72; ++Sample)
        {
            const auto Position = Project(Sample * 5, Latitude);
            Points.push_back({Position[0], Position[1]});
        }
        Pen.Path(Points, ResolveCssColour(Latitude == 0 ? 0xB5D5F08C : 0x7A99B533), Latitude == 0 ? 1.1f : 0.6f);
    }
    for (int Longitude : {0, 30, 60, 90, 120, 150})
    {
        Points.clear();
        for (int Sample = 0; Sample <= 72; ++Sample)
        {
            const auto Position = Project(Longitude, Sample * 5);
            Points.push_back({Position[0], Position[1]});
        }
        Pen.Path(Points, ResolveCssColour(0x90AFC730), 0.6f);
    }
    const auto Position = Project(Moon.AzimuthDegrees, Moon.ElevationDegrees);
    Points.clear();
    for (int Sample = 0; Sample <= 36; ++Sample)
    {
        const auto Point = Project(Moon.AzimuthDegrees, Sample * Moon.ElevationDegrees / 36);
        Points.push_back({Point[0], Point[1]});
    }
    Pen.Path(Points, ResolveCssColour(0xAACFEBFF), 1.4f, Position[2] < 0 ? 3.0f : 0.0f, 3);
    Pen.Line({180, 117}, {Position[0], Position[1]}, ResolveCssColour(0xC8DEF259), 1, 2, 4);
    Pen.Circle({Position[0], Position[1]}, 17, ResolveCssColour(0xB6D2ED0C));
    Surface.Construct(Pen.QueryDrawList(), Pen.QueryScreen({Position[0] - 12, Position[1] - 12}),
                      Pen.QueryScreen({Position[0] + 12, Position[1] + 12}));
    Pen.Circle({180, 117}, 2, ResolveCssColour(0xC5D8E5FF));
    Pen.Text({180, 9}, "N", ResolveCssColour(0x8CA2BDFF), 9);
    Pen.Text({68, 121}, "W", ResolveCssColour(0x8CA2BDFF), 9);
    Pen.Text({292, 121}, "E", ResolveCssColour(0x8CA2BDFF), 9);
    Pen.Text({180, 231}, "S", ResolveCssColour(0x8CA2BDFF), 9);
    Pen.Text({180, 252}, Position[2] < 0 ? "Far side · dashed guide" : "Near side · drag to orbit", ResolveCssColour(0x8CA2BDFF), 9);
    Positioned |= SliderRow::Construct(Page, "MoonAzimuth", "Moon azimuth", Moon.AzimuthDegrees, -180, 180, 0.1f, "°");
    Positioned |= SliderRow::Construct(Page, "MoonElevation", "Moon elevation", Moon.ElevationDegrees, -90, 90, 0.1f, "°");
    if (Positioned) Moon.FollowSky = false;
    Page.EndCard();
    BeginMoonCard(Page, "Moon rotation");
    CaptionText::Construct(Page, "Surface orientation is independent of sky position.");
    Pen = DiagramCanvas::Begin(Page, "MoonOrientation", 256, 215, 15, 15, &Drag, "Moon rotation preview · drag or use arrow keys");
    bool Changed = Positioned || Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
    {
        Moon.RollDegrees  = std::clamp(Moon.RollDegrees + Drag.Delta.x * 0.8f, -180.0f, 180.0f);
        Moon.PitchDegrees = std::clamp(Moon.PitchDegrees - Drag.Delta.y * 0.8f, -180.0f, 180.0f);
    }
    else if (Drag.Key == ImGuiKey_Home)
        Moon.RollDegrees = Moon.PitchDegrees = 0;
    else if (Drag.Key != ImGuiKey_None)
    {
        Moon.RollDegrees  = std::clamp(Moon.RollDegrees + (Drag.Key == ImGuiKey_RightArrow  ? 1
                                                           : Drag.Key == ImGuiKey_LeftArrow ? -1
                                                                                            : 0) *
                                                              Drag.Step,
                                       -180.0f, 180.0f);
        Moon.PitchDegrees = std::clamp(Moon.PitchDegrees + (Drag.Key == ImGuiKey_UpArrow     ? 1
                                                            : Drag.Key == ImGuiKey_DownArrow ? -1
                                                                                             : 0) *
                                                               Drag.Step,
                                       -180.0f, 180.0f);
    }
    ConstructSurface(Pen, false, true);
    Changed |= SliderRow::Construct(Page, "MoonRoll", "Surface roll", Moon.RollDegrees, -180, 180, 0.1f, "°");
    Changed |= SliderRow::Construct(Page, "MoonPitch", "Surface pitch", Moon.PitchDegrees, -180, 180, 0.1f, "°");
    Changed |= SliderRow::Construct(Page, "MoonSpin", "Surface spin", Moon.SpinDegrees, -180, 180, 0.1f, "°");
    if (NextButton::Construct(Page, "MoonResetOrientation", "Reset surface frame"))
    {
        Moon.RollDegrees = Moon.PitchDegrees = Moon.SpinDegrees = 0;
        Changed                                                 = true;
    }
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHT AND SIZE
//------------------------------------------------------------------------------------------------------------------------

bool MoonInspector::ConstructLight(ControlColumn &Page)
{
    auto &Moon = Parameters.Instances[ActiveInstance];
    BeginMoonCard(Page, "Moonlight");
    ReadoutBlock::Construct(Page, "", Figure("%.2f", Moon.Brightness).c_str(), "×", 32, 14, -1, 1.2f);
    CaptionText::Construct(Page, "Disc brightness multiplier · not calibrated lux", 8, 0);
    auto Pen = DiagramCanvas::Begin(Page, "MoonLight", 256, 215, 15, 15);
    Pen.Glow({128, 104}, 82, ResolveCssColour(0xA8BCDBFF), 0.075f * Moon.Glow);
    ConstructSurface(Pen);
    bool Changed = SliderRow::Construct(Page, "MoonBrightness", "Disc brightness", Moon.Brightness, 0, 5, 0.01f, "×");
    Changed |= SliderRow::Construct(Page, "MoonGlow", "Lunar glow", Moon.Glow, 0, 3, 0.01f, "×");
    if (ActiveInstance == 0)
    {
        const SwitchTile Light[] = {{"Illuminate scene", &Parameters.IlluminateScene, TileGlyph::Sun, false, false,
                                     "The primary Moon controls the scene moonlight."}};
        Changed |= SwitchTiles::Construct(Page, "MoonLightEnabled", Light, 1, 1);
        Changed |= SliderRow::Construct(Page, "MoonLightIntensity", "Moonlight power", Parameters.LightPower, 0, 5, 0.01f, "×");
        CaptionText::Construct(Page, "The primary slot supplies directional moonlight at night. Phase, elevation, visibility, brightness and this power control update that light.");
    }
    else
        CaptionText::Construct(Page, "Visual body only. Scene lighting comes from the primary Moon.");
    Page.EndCard();
    BeginMoonCard(Page, "Moon size");
    ReadoutBlock::Construct(Page, "", Figure("%.2f", Moon.AngularDiameterDegrees).c_str(), "°", 32, 14, -1, 1.2f);
    CaptionText::Construct(Page, "Angular diameter · compressed visual comparison", 8, 0);
    Pen = DiagramCanvas::Begin(Page, "MoonSize", 256, 215, 15, 15);
    ConstructSurface(Pen, true);
    Changed |= SliderRow::Construct(Page, "MoonSizeField", "Angular diameter", Moon.AngularDiameterDegrees, 0.1f, 179.9f, 0.01f, "°");
    if (NextButton::Construct(Page, "MoonEarthSize", "Earth Moon reference / 0.52°"))
    {
        Moon.AngularDiameterDegrees = 0.52f;
        Changed                     = true;
    }
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LUNAR CATALOGUE AND NEIGHBOURS
//------------------------------------------------------------------------------------------------------------------------

bool MoonInspector::ConstructBodies(ControlColumn &Page)
{
    auto &Moon = Parameters.Instances[ActiveInstance];
    BeginMoonCard(Page, "Moon catalogue");
    const float  Scale  = Page.QueryScale();
    const auto   Origin = Page.QueryCardOrigin();
    const auto   Top    = Page.Space(143, 14, 0);
    const ImVec2 Minimum(Origin.x + 48 * Scale, Top.y), Maximum(Origin.x + Page.QueryCardWidth() - 48 * Scale, Top.y + 143 * Scale);
    const float  VisibleWidth = std::max(1.0f, (Maximum.x - Minimum.x) / Scale);
    bool         Previous = false, Next = false;
    TagPill::ConstructAt(Page, "PreviousMoonBodies", {Origin.x + 12 * Scale, Origin.y + 90 * Scale}, "‹", true, &Previous, 22, 9, 3, 32);
    TagPill::ConstructAt(Page, "NextMoonBodies", {Origin.x + Page.QueryCardWidth() - 44 * Scale, Origin.y + 90 * Scale}, "›", true, &Next,
                         22, 9, 3, 32);
    if (Previous) CatalogueOffset -= 124;
    if (Next) CatalogueOffset += 124;
    if (ImGui::IsMouseHoveringRect(Minimum, Maximum) && ImGui::GetIO().MouseWheelH != 0) CatalogueOffset -= ImGui::GetIO().MouseWheelH * 60;
    CatalogueOffset = std::clamp(CatalogueOffset, 0.0f, std::max(0.0f, 6 * 108.0f - 8 - VisibleWidth));
    bool Changed    = false;
    ImGui::PushClipRect(Minimum, Maximum, true);
    for (size_t Body = 0; Body < LunarSurfacePresets.size(); ++Body)
    {
        const auto &Preset = LunarSurfacePresets[Body];
        if (!SurfacesLoaded[Body])
        {
            Surfaces[Body] =
                ReadLunarSurface(ResolveContentDirectory("EngineContent") / "EnvironmentArchives/CelestialMaps" / Preset.Filename);
            SurfacesLoaded[Body] = true;
        }
        if (!Portraits[Body].QueryReady() && !Surfaces[Body].Rgb.empty())
        {
            MoonInstanceConfiguration Portrait;
            Portrait.Body       = static_cast<uint32_t>(Body);
            Portrait.PhaseDays  = 14.765f;
            Portrait.Brightness = 1;
            const auto Pixels   = GenerateLunarReference(Surfaces[Body], Portrait);
            Portraits[Body].Configure(256, 256, Pixels);
        }
        const ImVec2 ButtonMin(Minimum.x + (Body * 108.0f - CatalogueOffset) * Scale, Minimum.y);
        const ImVec2 ButtonMax(ButtonMin.x + 100 * Scale, ButtonMin.y + 124 * Scale);
        ImGui::SetCursorScreenPos(ButtonMin);
        ImGui::PushID(static_cast<int>(Body));
        if (ImGui::InvisibleButton("ChooseMoonBody", {100 * Scale, 124 * Scale}, ImGuiButtonFlags_EnableNav))
        {
            Moon.Body                   = static_cast<uint32_t>(Body);
            Moon.AngularDiameterDegrees = Preset.Diameter;
            Changed                     = true;
        }
        if (ImGui::IsItemFocused())
        {
            const float Leading = Body * 108.0f, Trailing = Leading + 100;
            if (Leading < CatalogueOffset) CatalogueOffset = Leading;
            if (Trailing > CatalogueOffset + VisibleWidth) CatalogueOffset = Trailing - VisibleWidth;
        }
        ImGui::PopID();
        const bool Selected = Moon.Body == Body;
        Page.QueryDrawList()->AddRectFilled(ButtonMin, ButtonMax, ResolveCssColour(Selected ? 0x344353FF : 0x202020FF), 16 * Scale);
        if (Selected) Page.QueryDrawList()->AddRect(ButtonMin, ButtonMax, ResolveCssColour(0xA9BDD6FF), 16 * Scale, 0, Scale);
        const ImVec2 PortraitMin(ButtonMin.x + (6 + 68.0f / 256 * 88) * Scale, ButtonMin.y + (6 + 44.0f / 256 * 88) * Scale);
        Portraits[Body].Construct(Page.QueryDrawList(), PortraitMin,
                                  {PortraitMin.x + 120.0f / 256 * 88 * Scale, PortraitMin.y + 120.0f / 256 * 88 * Scale});
        if (!Portraits[Body].QueryReady())
            (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 9 * Scale, {ButtonMin.x + 9 * Scale, ButtonMin.y + 50 * Scale},
                                      ResolveCssColour(0x95A4B7FF), "Unavailable");
        const float Advance = GlyphRun::Measure(Page, Page.QueryFaces().Regular, 11 * Scale, Preset.Name);
        (void)GlyphRun::Construct(
            Page, Page.QueryFaces().Regular, 11 * Scale,
            {ButtonMin.x + (100 * Scale - Advance) / 2, GlyphRun::ResolveBaseline(ButtonMin.y + 100 * Scale, 11 * Scale, 1.4f)},
            ResolveCssColour(Selected ? 0xE9E9E9FF : 0x919191FF), Preset.Name);
    }
    ImGui::PopClipRect();
    Page.EndCard();
    if (DisclosureRow::Construct(Page, "MoonNeighbours", "Bright planets / orbital placement", PlanetsExpanded))
    {
        BeginMoonCard(Page, "Bright planets · orbital placement");
        const auto &Access  = InspectorRuntime::Instance().QueryEnvironmentAccess();
        bool        Enabled = false, Available = false;
        float       Brightness = 1;
        for (const auto *Planet : Access.Planets)
        {
            if (Planet == nullptr) continue;
            Enabled |= Planet->Enabled;
            if (!Available) Brightness = Planet->Brightness;
            Available = true;
        }
        const SwitchTile Planets[] = {{"Planets", Available ? &Enabled : nullptr, TileGlyph::Orbit}};
        if (SwitchTiles::Construct(Page, "PlanetsEnabled", Planets, 1, 1))
        {
            for (auto *Planet : Access.Planets)
                if (Planet != nullptr)
                {
                    Planet->Enabled = Enabled;
                    Planet->AdvanceRevision();
                }
        }
        if (SliderRow::Construct(Page, "PlanetBrightness", "Brightness", Brightness, 0, 5, 0.01f, "×", 1, Available,
                                 "The scene contains no planet records."))
        {
            for (auto *Planet : Access.Planets)
                if (Planet != nullptr)
                {
                    Planet->Brightness = Brightness;
                    Planet->AdvanceRevision();
                }
        }
        for (size_t Body = 0; Body < 4; ++Body)
        {
            char Caption[96]{};
            if (Access.CelestialReady)
                std::snprintf(Caption, sizeof(Caption), "%s       %.1f° az · %.1f° el", PlanetBodyNames[Body],
                              Access.PlanetPlacements[Body][0], Access.PlanetPlacements[Body][1]);
            else
                std::snprintf(Caption, sizeof(Caption), "%s       Orbital solution unavailable", PlanetBodyNames[Body]);
            CaptionText::Construct(Page, Caption, 16, 16);
        }
        CaptionText::Construct(Page, "Low-precision orbital positions, not decorative Moon presets. Date and observer location are shared "
                                     "with the Sun. Suitable for visual placement, not navigation.");
        Page.EndCard();
    }
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE AND PAGE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void MoonInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    constexpr const char* Names[] = {"Moon 1","Moon 2","Moon 3","Moon 4"};
    for (size_t Slot=0;Slot<4;++Slot) OutSwitches.push_back({Names[Slot],"Native sky body",&Parameters.Instances[Slot].Visible});
}

void MoonInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveMoon();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool Changed = PageHeading::Construct(Page,"MoonHeader","Inspector / Environment","Moons",Parameters.Enabled ? "● Visible" : "○ Hidden",
        "Scene bodies · phase · surface light","Toggle the Moon group");
    if (Changed) Parameters.Enabled = !Parameters.Enabled;
    BeginMoonCard(Page, "Moon settings", true);
    std::array<std::string, 4>  Names;
    std::array<const char *, 4> Choices;
    for (size_t Instance = 0; Instance < 4; ++Instance)
    {
        Names[Instance] =
            std::to_string(Instance + 1) + " · " + LunarSurfacePresets[std::min(Parameters.Instances[Instance].Body, 5u)].Name;
        Choices[Instance] = Names[Instance].c_str();
    }
    const auto Origin = Page.QueryCardOrigin();
    SelectRow::ConstructAt(Page, "ActiveMoonInstance",
                           {Origin.x + Page.QueryCardWidth() - 141 * Page.QueryScale(), Origin.y + 14 * Page.QueryScale()},
                           123 * Page.QueryScale(), Choices.data(), 4, ActiveInstance, true, "Active moon instance");
    auto            &Moon       = Parameters.Instances[ActiveInstance];
    const SwitchTile Settings[] = {{"Visible", &Moon.Visible, TileGlyph::Sun}, {"Follow sky", &Moon.FollowSky, TileGlyph::Orbit}};
    Changed |= SwitchTiles::Construct(Page, "MoonSettings", Settings, 2, 2, 16.2f, 0);
    Page.EndCard();
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (Moon.FollowSky && Access.CelestialReady)
    {
        Moon.AzimuthDegrees   = EnvironmentReadings::WrapDegrees(static_cast<float>(Access.LunarPlacement[0]) + 180) - 180;
        Moon.ElevationDegrees = static_cast<float>(Access.LunarPlacement[1]);
        Moon.PhaseDays        = static_cast<float>(Access.LunarPlacement[2]);
    }
    const ActionEntry Actions[] = {{"MoonFind","Find in sky"},{"MoonPlace","Place in view",Access.CameraReady}};
    ActionRowLook Look; Look.MarginTop = 0; Look.MarginBottom = 15;
    const int Action = ActionRow::Construct(Page,Actions,Look);
    if (Action == 1)
    {
        Moon.AzimuthDegrees = float(std::atan2(Access.CameraForward[1],Access.CameraForward[0]) * 180 / EnvironmentReadings::Pi);
        Moon.ElevationDegrees = 25; Moon.Visible = true; Moon.FollowSky = false; Parameters.Enabled = true; Changed = true;
    }
    if (Action >= 0) InspectorRuntime::Instance().RequestSkyFocus(Moon.AzimuthDegrees,Moon.ElevationDegrees,Moon.AngularDiameterDegrees);
    RefreshSurface();
    constexpr const char *Tabs[] = {"Phase", "Placement", "Light & size", "Bodies"};
    TabStrip::Construct(Page, "MoonTabs", Tabs, 4, ActivePane);
    switch (ActivePane)
    {
    case 0:
        Changed |= ConstructPhase(Page);
        break;
    case 1:
        Changed |= ConstructPlacement(Page);
        break;
    case 2:
        Changed |= ConstructLight(Page);
        break;
    case 3:
        Changed |= ConstructBodies(Page);
        break;
    }
    if (Moon.FollowSky && !Access.CelestialReady)
        CaptionText::Construct(Page, "Follow sky requires the scene Sun’s date and observer solution.");
    CaptionText::Construct(Page, "All four slots update the native sky. Find in sky points the camera at this slot; Place in view switches off sky following and puts it above the horizon.");
    Page.Finalize();
    if (Changed)
    {
        Parameters.SecondaryMoonEnabled = Parameters.Instances[1].Visible;
        Parameters.AdvanceRevision();
    }
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void MoonInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void MoonInspector::ConstructInspector() {}
} // namespace Frontier

#endif
