/*==========================================================================================================================================
                                                           SOLARINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The demo's Sun page (SunInspectorDashboard.js + SunInspector.css) 1:1 on the level's SolarConfiguration: the header
//    with Reset ↗, four section tabs, the solar vector dial, the direction tiles and quick switches, aim sliders and
//    lighting presets, the solar day ephemeris, time & location, radiance and the thermal spectrum, the apparent disc,
//    and the link to the Lens flare page.

#include "Engine/Interface/Inspectors/SolarInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Controls/PageControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

namespace Frontier
{

namespace
{

constexpr float SolarPi = 3.14159265358979f;

// 📝 SunInspectorDashboard.js Ranges, in the demo's own keys.
constexpr float AzimuthMinimum     = -180.0f, AzimuthMaximum = 180.0f;
constexpr float ElevationMinimum   = -90.0f, ElevationMaximum = 90.0f;
constexpr float MultiplierMaximum  = 60.0f;                       // SolarIlluminance
constexpr float KelvinMinimum      = 2000.0f, KelvinMaximum = 10000.0f;
constexpr float RadiusMinimum      = 0.05f, RadiusMaximum = 2.0f;  // SunRadius [°]
constexpr float AureoleMaximum     = 3.0f;                         // SunGlow
constexpr float KilaluxPerMultiplier = 98.0f / 12.0f;              // the exhibit scale: 98 klx ≙ SolarIlluminance 12

// 📝 InspectorPages.js LightingPresets: elevation and azimuth (SolarIlluminance stays 12).
struct LightingPreset
{
    const char* Name;
    float       Elevation;
    float       Azimuth;
};
constexpr std::array<LightingPreset, 4> LightingPresets = {{{"Noon", 64.0f, 145.0f}, {"Golden", 10.0f, 160.0f}, {"Dusk", 1.5f, 172.0f},
                                                            {"Night", -14.0f, 172.0f}}};

[[nodiscard]] float WrapAzimuth(float Degrees) noexcept
{
    float Wrapped = std::fmod(Degrees + 180.0f, 360.0f);
    if (Wrapped < 0.0f) Wrapped += 360.0f;
    return Wrapped - 180.0f;
}

// 📝 SolarClock: HH:MM of a local hour.
[[nodiscard]] std::string ResolveSolarClock(float Hours)
{
    const int Minutes = ((static_cast<int>(std::lround(Hours * 60.0f)) % 1440) + 1440) % 1440;
    char      Text[16]{};
    std::snprintf(Text, sizeof(Text), "%02d:%02d", Minutes / 60, Minutes % 60);
    return Text;
}

[[nodiscard]] std::string ResolveFigure(const char* Format, double Quantity)
{
    char Text[48]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

// 📝 SolarSpectrum: 65 Planck samples over 380–780 nm, normalised to their peak.
[[nodiscard]] std::array<float, 65> ResolveThermalSpectrum(float Kelvin)
{
    std::array<double, 65> Radiance{};
    double                 Peak = 0.0;
    for (size_t Sample = 0; Sample < Radiance.size(); ++Sample)
    {
        const double λ = (380.0 + static_cast<double>(Sample) / 64.0 * 400.0) * 1.0e-9;
        Radiance[Sample] = 1.0 / (std::pow(λ, 5.0) * std::expm1(0.01438776877 / (λ * static_cast<double>(Kelvin))));
        Peak             = std::max(Peak, Radiance[Sample]);
    }
    std::array<float, 65> Normalised{};
    for (size_t Sample = 0; Sample < Radiance.size(); ++Sample) Normalised[Sample] = static_cast<float>(Radiance[Sample] / std::max(Peak, 1.0e-30));
    return Normalised;
}

// 📝 The spectrum gradient: ['#8f81b5', '#82a7d5', '#91bdac', '#d6c77e', '#ca8a76'] at even stops, at Alpha.
[[nodiscard]] ImU32 ResolveSpectrumColour(float Fraction, float Alpha)
{
    static constexpr std::array<uint32_t, 5> Stops = {0x8F81B5u, 0x82A7D5u, 0x91BDACu, 0xD6C77Eu, 0xCA8A76u};
    const float  Position = std::clamp(Fraction, 0.0f, 1.0f) * 4.0f;
    const size_t Lower    = std::min<size_t>(3, static_cast<size_t>(Position));
    const float  Blend    = Position - static_cast<float>(Lower);
    auto Channel = [&](int Shift) {
        const float From = static_cast<float>((Stops[Lower] >> Shift) & 0xFF);
        const float To   = static_cast<float>((Stops[Lower + 1] >> Shift) & 0xFF);
        return static_cast<int>(From + (To - From) * Blend + 0.5f);
    };
    return IM_COL32(Channel(16), Channel(8), Channel(0), static_cast<int>(Alpha * 255.0f + 0.5f));
}

// 📝 The page's instruments ([data-instrument] rules of SunInspector.css, its second block).
CardLook DirectionLook()
{
    CardLook Look;
    Look.Fill       = ResolveCssColour(0x23241EFF);
    Look.Glow       = ResolveCssColour(0x6A603326);
    Look.GlowCentre = ImVec2(0.55f, 0.60f);
    Look.GlowExtent = 0.70f;
    Look.Border     = ResolveCssColour(0xF0D99C09);
    Look.Radius     = 24.0f;
    Look.PaddingTop = 17.0f;
    return Look;
}

CardLook AimLook()
{
    CardLook Look;
    Look.TopRule     = ResolveCssColour(0xFFFFFF13);
    Look.Radius      = 0.0f;
    Look.PaddingTop  = 17.0f;
    Look.PaddingSide = 0.0f;
    Look.PaddingBottom = 17.0f;
    Look.MarginTop   = 18.0f;
    return Look;
}

CardLook FlatLook(uint32_t Fill, float Radius)
{
    CardLook Look;
    Look.Fill   = ResolveCssColour(Fill);
    Look.Radius = Radius;
    return Look;
}

CardLook RadianceLook()
{
    CardLook Look;
    Look.Border      = ResolveCssColour(0xF0D99C09);
    Look.Radius      = 0.0f;
    Look.PaddingTop  = 0.0f;
    Look.PaddingSide = 0.0f;
    Look.PaddingBottom = 0.0f;
    return Look;
}

CardLook SpectrumLook()
{
    CardLook Look;
    Look.Fill       = ResolveCssColour(0x25251FFF);
    Look.Glow       = ResolveCssColour(0x89714918);
    Look.GlowCentre = ImVec2(0.5f, 0.8f);
    Look.GlowExtent = 0.70f;
    return Look;
}

CardLook AngularLook()
{
    CardLook Look;
    Look.Fill       = ResolveCssColour(0x24251FFF);
    Look.Glow       = ResolveCssColour(0x86774320);
    Look.GlowCentre = ImVec2(0.5f, 0.45f);
    Look.GlowExtent = 0.65f;
    Look.Border     = ResolveCssColour(0xF0D99C09);
    Look.Radius     = 24.0f;
    return Look;
}

CardLook SubcomponentLook()
{
    CardLook Look;
    Look.Border = ResolveCssColour(0xFFFFFF15);
    Look.Radius = 18.0f;
    return Look;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLAR DAY
//------------------------------------------------------------------------------------------------------------------------

void SolarInspector::ResolveDayPath()
{
    const SolarPlacementSolver Solver = InspectorRuntime::Instance().QuerySolarPlacementSolver();
    char Key[96]{};
    std::snprintf(Key, sizeof(Key), "%s|%.4f|%.4f|%.2f", Parameters.CalendarDate.c_str(), static_cast<double>(Parameters.LatitudeDegrees),
                  static_cast<double>(Parameters.LongitudeDegrees), static_cast<double>(Parameters.UtcOffsetHours));
    if (DayPath.Solved && DayPath.Key == Key) return;
    DayPath     = SolarDayPath{};
    DayPath.Key = Key;
    if (Solver == nullptr) return;
    SolarPlacementQuery Query;
    Query.CalendarDate     = Parameters.CalendarDate;
    Query.LatitudeDegrees  = Parameters.LatitudeDegrees;
    Query.LongitudeDegrees = Parameters.LongitudeDegrees;
    Query.UtcOffsetHours   = Parameters.UtcOffsetHours;
    for (size_t Sample = 0; Sample < DayPath.Elevations.size(); ++Sample)
    {
        Query.LocalSolarHours      = static_cast<double>(Sample) / 4.0;
        DayPath.Elevations[Sample] = static_cast<float>(Solver(Query).ElevationDegrees);
    }
    // 📝 SolarDayData: linear 0° crossings between quarter hours; the first rise and the first set.
    for (size_t Sample = 1; Sample < DayPath.Elevations.size(); ++Sample)
    {
        const float From = DayPath.Elevations[Sample - 1], To = DayPath.Elevations[Sample];
        const float Hour = static_cast<float>(Sample - 1) / 4.0f;
        if (From >= 0.0f && To >= 0.0f)
        {
            DayPath.DayHours += 0.25f;
        }
        else if ((From < 0.0f) != (To < 0.0f))
        {
            const float Fraction = -From / (To - From);
            const float Crossing = Hour + Fraction * 0.25f;
            if (From < 0.0f)
            {
                if (DayPath.RiseHours < 0.0f) DayPath.RiseHours = Crossing;
                DayPath.DayHours += (1.0f - Fraction) * 0.25f;
            }
            else
            {
                if (DayPath.SetHours < 0.0f) DayPath.SetHours = Crossing;
                DayPath.DayHours += Fraction * 0.25f;
            }
        }
    }
    DayPath.Solved = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void SolarInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Ray Shadows", "Soft penumbra", &Parameters.CastRaytracedShadow});
    OutSwitches.push_back({"Light Shafts", "Froxel shafts", &Parameters.VolumetricLightShaft});
}

std::string SolarInspector::QueryDirectoryReadout() const
{
    // 📝 InspectorPages.js: `${S.SunElevation.toFixed(0)}°`.
    char Readout[24] = {};
    std::snprintf(Readout, sizeof(Readout), "%.0f\xC2\xB0", static_cast<double>(Parameters.ElevationDegrees));
    return Readout;
}

void SolarInspector::ConstructInspector()
{
    const ControlTheme Theme = ControlTheme::ResolveSolar();
    const EditorTypefaces  Faces = EditorTypefaces::Resolve();
    ControlColumn      Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    ImDrawList*            DrawList = Page.QueryDrawList();
    const float            S        = Page.QueryScale();
    bool                   Changed  = false;
    ImGui::PushID("SolarInspectorPage");

    // 📝 SetSunInspectorValue: direction edits stop the clock; time and place edits link the sun to it.
    auto Manual = [&]() {
        Parameters.TimeLinked = false;
        Parameters.DayCycle   = false;
    };
    float Multiplier = Parameters.IlluminanceKilolux / KilaluxPerMultiplier;
    float Azimuth    = WrapAzimuth(Parameters.AzimuthDegrees);
    float Radius     = 0.5f * Parameters.AngularDiameterDeg;

    if (PageHeading::Construct(Page, "Header", "ENVIRONMENT / 02", "Sun", "Reset \xE2\x86\x97", "Directional light / position & colour.",
                             "Reset Sun settings"))
    {
        // 📝 Reset ↗: EnvironmentDefaults for the sun keys, the direction fallback {38, 145, 12}, manual, temperature source.
        Parameters.ElevationDegrees   = 38.0f;
        Parameters.AzimuthDegrees     = 145.0f;
        Parameters.IlluminanceKilolux = 12.0f * KilaluxPerMultiplier;
        Parameters.KelvinTemperature  = 5778.0f;
        Parameters.AngularDiameterDeg = 0.54f;
        Parameters.AureoleGain        = 1.0f;
        Parameters.DayCycle           = false;
        Parameters.TimeLinked         = false;
        Parameters.ColourSource       = 0;
        Changed                       = true;
        Multiplier                    = 12.0f;
        Azimuth                       = 145.0f;
        Radius                        = 0.27f;
    }

    static constexpr std::array<const char*, 4> Panes = {"Overview", "Daylight", "Light & colour", "Disc & optics"};
    (void)TabStrip::Construct(Page, "Sun inspector sections", Panes.data(), static_cast<int>(Panes.size()), ActivePane);

    const float  Elevation = Parameters.ElevationDegrees;
    const bool   Lit       = Parameters.Enabled && Elevation >= 0.0f;

    if (ActivePane == 0)
    {
        // ── 01 / DIRECTION ──────────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(DirectionLook(), "Solar vector", "01 / DIRECTION");
        DiagramDrag Drag;
        const DiagramPen   Pen = DiagramCanvas::Begin(Page, "##SolarVector", 640.0f, 420.0f, 7.0f, 0.0f, &Drag, "Sun vector editor \xC2\xB7 drag azimuth and elevation");
        const ImVec2           Origin(320.0f, 210.0f);
        for (const float Ring : {45.0f, 90.0f, 135.0f, 180.0f})
        {
            Pen.Circle(Origin, Ring, 0, Ring == 90.0f ? ResolveCssColour(0xD9C29B70) : ResolveCssColour(0xD9C29B1C));
        }
        for (int Tick = 0; Tick < 24; ++Tick)
        {
            const float Angle = static_cast<float>(Tick) / 24.0f * 2.0f * SolarPi;
            Pen.Line(ImVec2(320.0f + std::cos(Angle) * 174.0f, 210.0f + std::sin(Angle) * 174.0f),
                     ImVec2(320.0f + std::cos(Angle) * 180.0f, 210.0f + std::sin(Angle) * 180.0f), ResolveCssColour(0xE8C78B55), 1.0f);
        }
        Pen.Line(ImVec2(120.0f, 210.0f), ImVec2(520.0f, 210.0f), ResolveCssColour(0xE8C78B20), 1.0f, 2.0f, 6.0f);
        Pen.Line(ImVec2(320.0f, 15.0f), ImVec2(320.0f, 405.0f), ResolveCssColour(0xE8C78B20), 1.0f, 2.0f, 6.0f);
        const float  AzimuthRadians = Azimuth * SolarPi / 180.0f;
        const float  Reach          = 90.0f - Elevation;
        const ImVec2 Sun(320.0f + std::cos(AzimuthRadians) * Reach, 210.0f - std::sin(AzimuthRadians) * Reach);
        Pen.Line(Origin, Sun, Lit ? ResolveCssColour(0xE8C78BFF) : ResolveCssColour(0x87938BFF), 1.5f);
        Pen.Circle(Origin, 4.0f, ResolveCssColour(0xD9CAB0FF));
        Pen.Circle(Sun, 20.0f, Lit ? ResolveCssColour(0xE8C78B16) : ResolveCssColour(0x87938B16), Lit ? ResolveCssColour(0xE8C78B70) : ResolveCssColour(0x87938B70));
        if (Lit) Pen.Glow(Sun, 28.0f, IM_COL32(232, 199, 139, 255), 0.2f);
        Pen.Circle(Sun, 6.0f, Lit ? ResolveCssColour(0xF0D69FFF) : ResolveCssColour(0x8D9B91FF));
        Pen.Text(ImVec2(320.0f, 18.0f), "+Y / N", ResolveCssColour(0xA49886FF), 16.0f);
        Pen.Text(ImVec2(532.0f, 216.0f), "+X / E", ResolveCssColour(0xA49886FF), 16.0f, -1);
        Pen.Text(ImVec2(108.0f, 216.0f), "W", ResolveCssColour(0xA49886FF), 16.0f, 1);
        Pen.Text(ImVec2(320.0f, 408.0f), "S", ResolveCssColour(0xA49886FF), 16.0f);
        Pen.Text(ImVec2(419.0f, 194.0f), "0\xC2\xB0 HORIZON", ResolveCssColour(0xAB9D88FF), 13.0f, -1);
        Pen.Text(ImVec2(24.0f, 389.0f), "DRAG VECTOR", ResolveCssColour(0xAB9D88FF), 14.0f, -1);
        if (Drag.Engaged)
        {
            // 📝 The dial's drag: azimuth from the bearing, elevation 90° − the distance from the centre (≤ 180).
            const float Across = Drag.Point.x - 320.0f, Up = 210.0f - Drag.Point.y;
            const float Distance = std::sqrt(Across * Across + Up * Up);
            if (Distance > 1.0f) Azimuth = std::clamp(std::atan2(Up, Across) * 180.0f / SolarPi, AzimuthMinimum, AzimuthMaximum);
            Parameters.AzimuthDegrees   = Azimuth;
            Parameters.ElevationDegrees = std::clamp(90.0f - std::min(180.0f, Distance), ElevationMinimum, ElevationMaximum);
            Manual();
            Changed = true;
        }

        // 📝 .so-vector-top inside the direction card: absolutely placed 49 px down, 18 px in, over the dial.
        const ImVec2 CardOrigin = Page.QueryCardOrigin();
        const float  CardWidth  = Page.QueryCardWidth();
        const ImVec2 HeroMin(CardOrigin.x + 19.0f * S, CardOrigin.y + 49.0f * S);
        const std::string ElevationFigure = ResolveFigure("%.1f", static_cast<double>(Parameters.ElevationDegrees));
        (void)ReadoutBlock::ConstructAt(Page, HeroMin, "Elevation", ElevationFigure.c_str(), "\xC2\xB0", 37.0f, 17.0f, -2.0f, 1.25f);
        const char* ModeText  = Parameters.TimeLinked ? "Time linked \xE2\x86\x97" : "Manual \xE2\x86\x97";
        const float ModeWidth = TagPill::Measure(Page, ModeText, 9.0f, 10.0f);
        bool        ModePress = false;
        (void)TagPill::ConstructAt(Page, "##SolarMode", ImVec2(CardOrigin.x + CardWidth - 19.0f * S - ModeWidth, HeroMin.y), ModeText, true, &ModePress, 9.0f, 10.0f,
                                  7.0f, 29.0f, ResolveCssColour(0x303128FF), ResolveCssColour(0xE4DBBDFF), "Toggle automatic Sun position");
        if (ModePress)
        {
            Parameters.TimeLinked = !Parameters.TimeLinked;
            if (!Parameters.TimeLinked) Parameters.DayCycle = false;
            Changed = true;
        }
        const float VectorX = std::cos(Elevation * SolarPi / 180.0f) * std::cos(AzimuthRadians);
        const float VectorY = std::cos(Elevation * SolarPi / 180.0f) * std::sin(AzimuthRadians);
        const float VectorZ = std::sin(Elevation * SolarPi / 180.0f);
        char        Vector[64]{};
        std::snprintf(Vector, sizeof(Vector), "%.2f / %.2f / %.2f", static_cast<double>(VectorX), static_cast<double>(VectorY), static_cast<double>(VectorZ));
        FooterRow::ConstructAtlas(Page, "WORLD XYZ \xC2\xB7 Z UP", Vector, 9.0f, 11.0f, 0.0f);
        Page.EndCard();

        // 📝 .co-kpis: Azimuth, Source tint, Sunlight (lit).
        const std::array<MetricTile, 3> Metrics = {{
            {"Azimuth", ResolveFigure("%.1f", static_cast<double>(Azimuth)), "\xC2\xB0 / +X east"},
            {"Source tint", Parameters.ColourSource == 1u ? std::string("RGB") : ResolveFigure("%.0f K", std::round(static_cast<double>(Parameters.KelvinTemperature))),
             "source setting"},
            {"Sunlight", ResolveFigure("%.1f", static_cast<double>(Multiplier)), "\xC3\x97 multiplier"},
        }};
        MetricTiles::Construct(Page, Metrics.data(), static_cast<int>(Metrics.size()));

        // 📝 The native quick toggles: Sunlight, Sun disc, Day cycle (NativeOrbit).
        const std::array<SwitchTile, 3> Quick = {{
            {"Sunlight", &Parameters.Enabled, TileGlyph::Sun},
            {"Sun disc", &Parameters.DiscVisible, TileGlyph::Sun, false, false,
             "Sun disc: the GI sky's disc; the PlanetaryAtmosphere sky still draws its own disc"},
            {"Day cycle", &Parameters.DayCycle, TileGlyph::Orbit},
        }};
        const bool Narrow = Page.QueryContainerWidth() <= 369.0f;
        if (SwitchTiles::Construct(Page, "Quick", Quick.data(), static_cast<int>(Quick.size()), Narrow ? 2 : 3))
        {
            if (Parameters.DayCycle) Parameters.TimeLinked = true;
            Changed = true;
        }

        // ── 02 / AIM ───────────────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(AimLook(), "Direction controls", "02 / AIM");
        if (SliderRow::Construct(Page, "Azimuth", "Azimuth", Azimuth, AzimuthMinimum, AzimuthMaximum, 0.1f, "\xC2\xB0"))
        {
            Parameters.AzimuthDegrees = Azimuth;
            Manual();
            Changed = true;
        }
        float ElevationSetting = Parameters.ElevationDegrees;
        if (SliderRow::Construct(Page, "Elevation", "Elevation", ElevationSetting, ElevationMinimum, ElevationMaximum, 0.1f, "\xC2\xB0"))
        {
            Parameters.ElevationDegrees = ElevationSetting;
            Manual();
            Changed = true;
        }
        CaptionText::Construct(Page, "Inner horizon ring: 0\xC2\xB0. Centre: zenith. Outer region: below horizon. Editing direction stops the day cycle and "
                              "switches to manual.");
        Page.EndCard();

        std::array<const char*, 4> PresetNames{};
        for (size_t Index = 0; Index < LightingPresets.size(); ++Index) PresetNames[Index] = LightingPresets[Index].Name;
        const int Chosen = PresetStrip::Construct(Page, "Presets", PresetNames.data(), static_cast<int>(PresetNames.size()), PressedPreset);
        if (Chosen >= 0)
        {
            Manual();
            Parameters.ElevationDegrees   = LightingPresets[static_cast<size_t>(Chosen)].Elevation;
            Parameters.AzimuthDegrees     = LightingPresets[static_cast<size_t>(Chosen)].Azimuth;
            Parameters.IlluminanceKilolux = 12.0f * KilaluxPerMultiplier;
            PressedPreset                 = Chosen;
            Changed                       = true;
        }
    }
    else if (ActivePane == 1)
    {
        ResolveDayPath();
        // ── 03 / EPHEMERIS ─────────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(FlatLook(0x22241FFF, 24.0f), "Solar day", "03 / EPHEMERIS");
        {
            // 📝 .so-vector-top (flex, centred): Local time 49 px, the clock tag at the right.
            const float  HeroTall = 9.0f * 1.4f + 5.0f + 49.0f * 1.25f;
            const ImVec2 HeroMin  = Page.Space(HeroTall, 0.0f, 0.0f);
            const std::string Clock = ResolveSolarClock(Parameters.LocalSolarHours);
            (void)ReadoutBlock::ConstructAt(Page, HeroMin, "Local time", Clock.c_str(), "", 49.0f, 17.0f, -2.0f, 1.25f);
            const char* Playing = Parameters.DayCycle ? "Cycle running" : (Parameters.TimeLinked ? "Time linked" : "Manual direction");
            const float TagWidth = TagPill::Measure(Page, Playing);
            const float TagTall  = (2.0f * 6.0f + 9.0f * 1.4f + 2.0f) * S;
            (void)TagPill::ConstructAt(Page, "##Playing", ImVec2(HeroMin.x + Page.QueryWidth() - TagWidth, HeroMin.y + 0.5f * (HeroTall * S - TagTall)), Playing, false,
                                      nullptr);
        }
        DiagramDrag Drag;
        const DiagramPen   Pen = DiagramCanvas::Begin(Page, "##SolarDay", 640.0f, 320.0f, 12.0f, 12.0f, &Drag, "Solar day timeline \xC2\xB7 drag local time");
        auto                   Height = [](float ElevationDegrees) { return 160.0f - ElevationDegrees / 90.0f * 110.0f; };
        for (const float Mark : {-60.0f, 0.0f, 60.0f})
        {
            Pen.Line(ImVec2(42.0f, Height(Mark)), ImVec2(606.0f, Height(Mark)), Mark == 0.0f ? ResolveCssColour(0xE3CA995C) : ResolveCssColour(0xE3CA991A), 1.0f, 3.0f,
                     6.0f);
            const std::string Caption = ResolveFigure("%.0f\xC2\xB0", static_cast<double>(Mark));
            Pen.Text(ImVec2(32.0f, Height(Mark) + 5.0f), Caption.c_str(), ResolveCssColour(0x9C947FFF), 15.0f, 1);
        }
        if (DayPath.Solved)
        {
            std::vector<ImVec2> Points;
            Points.reserve(DayPath.Elevations.size());
            for (size_t Sample = 0; Sample < DayPath.Elevations.size(); ++Sample)
            {
                Points.emplace_back(42.0f + static_cast<float>(Sample) / 4.0f / 24.0f * 564.0f, Height(DayPath.Elevations[Sample]));
            }
            Pen.Path(Points, ResolveCssColour(0x9F997A99), 1.6f);
            std::vector<ImVec2> Segment;
            for (size_t Sample = 0; Sample < Points.size(); ++Sample)
            {
                if (DayPath.Elevations[Sample] >= 0.0f)
                {
                    Segment.push_back(Points[Sample]);
                }
                else
                {
                    if (Segment.size() > 1) Pen.Path(Segment, ResolveCssColour(0xEFD397FF), 2.6f);
                    Segment.clear();
                }
            }
            if (Segment.size() > 1) Pen.Path(Segment, ResolveCssColour(0xEFD397FF), 2.6f);
        }
        const float Marker = 42.0f + Parameters.LocalSolarHours / 24.0f * 564.0f;
        Pen.Line(ImVec2(Marker, 30.0f), ImVec2(Marker, 280.0f), ResolveCssColour(0xE8C78B66), 1.0f, 3.0f, 5.0f);
        if (Parameters.TimeLinked) Pen.Circle(ImVec2(Marker, Height(Parameters.ElevationDegrees)), 6.0f, ResolveCssColour(0xF1D69EFF));
        for (const int Hour : {0, 6, 12, 18, 24})
        {
            const std::string Caption = ResolveFigure("%02.0f:00", static_cast<double>(Hour));
            Pen.Text(ImVec2(42.0f + static_cast<float>(Hour) / 24.0f * 564.0f, 304.0f), Caption.c_str(), ResolveCssColour(0xA39982FF), 15.0f);
        }
        if (Drag.Engaged)
        {
            Parameters.LocalSolarHours = std::clamp((Drag.Point.x - 42.0f) / 564.0f * 24.0f, 0.0f, 24.0f);
            Parameters.TimeLinked      = true;
            Changed                    = true;
        }
        {
            // 📝 .so-day-metrics: three readouts (22 px figures), border-top, padding 13 px 0.
            const float  RowTall = 9.0f * 1.4f + 5.0f + 22.0f * 1.25f + 14.0f * 1.4f;
            const ImVec2 Min     = Page.Space(1.0f + 13.0f + RowTall + 13.0f, 0.0f, 0.0f);
            DrawList->AddLine(ImVec2(Min.x, Min.y + 0.5f * S), ImVec2(Min.x + Page.QueryWidth(), Min.y + 0.5f * S), ResolveCssColour(0xE8C78B12), S);
            const float Column = (Page.QueryWidth() - 20.0f * S) / 3.0f;
            const std::string Rise  = DayPath.RiseHours < 0.0f ? std::string("\xE2\x80\x94") : ResolveSolarClock(DayPath.RiseHours);
            const std::string Set   = DayPath.SetHours < 0.0f ? std::string("\xE2\x80\x94") : ResolveSolarClock(DayPath.SetHours);
            const std::string Hours = ResolveFigure("%.1f", static_cast<double>(DayPath.DayHours));
            const float       Top   = Min.y + 14.0f * S;
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(Min.x, Top), "Horizon rise", Rise.c_str(), "", 22.0f, 9.0f, -1.3f, 1.25f);
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(Min.x + Column + 10.0f * S, Top), "Horizon set", Set.c_str(), "", 22.0f, 9.0f, -1.3f, 1.25f);
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(Min.x + 2.0f * (Column + 10.0f * S), Top), "Above horizon", Hours.c_str(), "h", 22.0f, 9.0f, -1.3f, 1.25f, 0, 0, 0,
                                          9.0f, 5.0f, true);
        }
        CaptionText::Construct(Page, "Date/location solver \xC2\xB7 geometric 0\xC2\xB0 crossings, without refraction or terrain. This path is a reference while "
                              "direction is manual, not recorded telemetry.");
        if (SliderRow::Construct(Page, "SunTime", "Local solar control time", Parameters.LocalSolarHours, 0.0f, 24.0f, 0.01f, "h"))
        {
            Parameters.TimeLinked = true;
            Changed               = true;
        }
        Changed |= SliderRow::Construct(Page, "DayDuration", "Full day duration", Parameters.DayDurationHours, 0.01f, 24.0f, 0.01f, "real h");
        Page.EndCard();

        // ── 04 / COORDINATES ───────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(FlatLook(0x20221EFF, 24.0f), "Time & location", "04 / COORDINATES");
        std::string Date = Parameters.CalendarDate;
        if (DateRow::Construct(Page, "CalendarDate", "Calendar date", Date, true, "Solar calendar date"))
        {
            // 📝 Only a well-formed YYYY-MM-DD is taken (the demo checks the pattern and Date.parse).
            const bool WellFormed = Date.size() == 10 && Date[4] == '-' && Date[7] == '-' &&
                                    std::all_of(Date.begin(), Date.end(), [](char Glyph) { return Glyph == '-' || (Glyph >= '0' && Glyph <= '9'); });
            if (WellFormed)
            {
                Parameters.CalendarDate = Date;
                Parameters.TimeLinked   = true;
                Changed                 = true;
            }
        }
        for (auto [Id, Label, Target, Minimum, Maximum, Step, Unit] :
             {std::tuple<const char*, const char*, float*, float, float, float, const char*>{"Latitude", "Latitude", &Parameters.LatitudeDegrees, -90.0f, 90.0f, 0.01f,
                                                                                            "\xC2\xB0"},
              std::tuple<const char*, const char*, float*, float, float, float, const char*>{"Longitude", "Longitude", &Parameters.LongitudeDegrees, -180.0f, 180.0f,
                                                                                            0.01f, "\xC2\xB0"},
              std::tuple<const char*, const char*, float*, float, float, float, const char*>{"TimeZone", "UTC offset", &Parameters.UtcOffsetHours, -12.0f, 14.0f, 0.5f,
                                                                                            "h"}})
        {
            if (SliderRow::Construct(Page, Id, Label, *Target, Minimum, Maximum, Step, Unit))
            {
                Parameters.TimeLinked = true;
                Changed               = true;
            }
        }
        CaptionText::Construct(Page, "Changing time, date or location links the Sun to the existing celestial solver. The day-cycle switch stays in the Overview "
                              "quick toggles.");
        Page.EndCard();
    }
    else if (ActivePane == 2)
    {
        // ── 05 / RADIANCE ──────────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(RadianceLook(), "Light source", "05 / RADIANCE");
        const std::string Intensity = ResolveFigure("%.1f", static_cast<double>(Multiplier));
        ReadoutBlock::Construct(Page, "Sunlight intensity", Intensity.c_str(), "\xC3\x97");
        if (SliderRow::Construct(Page, "SolarIlluminance", "Sunlight multiplier", Multiplier, 0.0f, MultiplierMaximum, 0.1f, "\xC3\x97"))
        {
            Parameters.IlluminanceKilolux = Multiplier * KilaluxPerMultiplier;
            Changed                       = true;
        }
        CaptionText::Construct(Page, "Exhibit intensity multiplier, not a measured lux or irradiance value. Atmospheric transmission is applied by the renderer.");
        static constexpr std::array<const char*, 2> Sources = {"Temperature", "RGB"};
        int Source = static_cast<int>(std::min<uint32_t>(Parameters.ColourSource, 1u));
        if (SelectRow::Construct(Page, "ColourSource", "Colour source", Sources.data(), static_cast<int>(Sources.size()), Source, true, "Sun colour source"))
        {
            Parameters.ColourSource = static_cast<uint32_t>(Source);
            Changed                 = true;
        }
        float Tint[3] = {Parameters.TintLinear[0], Parameters.TintLinear[1], Parameters.TintLinear[2]};
        if (TintRow::Construct(Page, "SunTint", "Sun Tint", Tint))
        {
            Parameters.TintLinear   = {Tint[0], Tint[1], Tint[2]};
            Parameters.ColourSource = 1u;
            Changed                 = true;
        }
        if (SliderRow::Construct(Page, "SunTemperature", "Colour temperature", Parameters.KelvinTemperature, KelvinMinimum, KelvinMaximum, 50.0f, "K"))
        {
            Parameters.ColourSource = 0u;
            Changed                 = true;
        }
        Page.EndCard();

        // ── 06 / REFERENCE ─────────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(SpectrumLook(), "Source spectrum", "06 / REFERENCE");
        {
            // 📝 .so-spectrum-hero: flex, centred, padding 10 px 0; the 43 px reading and the source tag (≤ 45 %).
            const float  HeroTall = 9.0f * 1.4f + 5.0f + 43.0f * 1.25f;
            const ImVec2 HeroMin  = Page.Space(10.0f + HeroTall + 10.0f, 0.0f, 0.0f);
            const std::string Kelvin = ResolveFigure("%.0f", std::round(static_cast<double>(Parameters.KelvinTemperature)));
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(HeroMin.x, HeroMin.y + 10.0f * S), "Thermal reference", Kelvin.c_str(), "K", 43.0f, 10.0f, -1.3f, 1.25f);
            const char* Mode     = Parameters.ColourSource == 1u ? "RGB source \xC2\xB7 reference only" : "Temperature source";
            const float TagWidth = TagPill::Measure(Page, Mode);
            const float TagTall  = (2.0f * 6.0f + 9.0f * 1.4f + 2.0f) * S;
            (void)TagPill::ConstructAt(Page, "##ThermalMode", ImVec2(HeroMin.x + Page.QueryWidth() - TagWidth, HeroMin.y + 0.5f * ((10.0f + HeroTall + 10.0f) * S - TagTall)),
                                      Mode, false, nullptr);
        }
        const DiagramPen Pen = DiagramCanvas::Begin(Page, "##Spectrum", 640.0f, 280.0f, 12.0f, 12.0f);
        for (const float Rule : {28.0f, 80.0f, 132.0f, 184.0f}) Pen.Line(ImVec2(32.0f, Rule), ImVec2(608.0f, Rule), ResolveCssColour(0xEAD6AD1A), 1.0f, 2.0f, 6.0f);
        const std::array<float, 65> Spectrum = ResolveThermalSpectrum(Parameters.KelvinTemperature);
        std::vector<ImVec2>         Curve;
        std::vector<ImU32>          Shades;
        for (size_t Sample = 0; Sample < Spectrum.size(); ++Sample)
        {
            const float Fraction = static_cast<float>(Sample) / 64.0f;
            Curve.emplace_back(32.0f + Fraction * 576.0f, 204.0f - Spectrum[Sample] * 165.0f);
            Shades.push_back(ResolveSpectrumColour(Fraction, 0.22f));
        }
        Pen.ShadedArea(Curve, 204.0f, Shades);
        Pen.Path(Curve, ResolveCssColour(0xF0DDBAFF), 2.5f);
        const float Peak = 2897771.955f / Parameters.KelvinTemperature;
        if (Peak >= 380.0f && Peak <= 780.0f)
        {
            const float Across = 32.0f + (Peak - 380.0f) / 400.0f * 576.0f;
            Pen.Line(ImVec2(Across, 28.0f), ImVec2(Across, 204.0f), ResolveCssColour(0xEED6A670), 1.0f, 3.0f, 6.0f);
            Pen.Circle(ImVec2(Across, 39.0f), 5.0f, ResolveCssColour(0xF0D49DFF));
            Pen.Text(ImVec2(Across, 19.0f), "\xCE\xBB MAX", ResolveCssColour(0xC3B596FF), 13.0f);
        }
        std::vector<ImU32> Band;
        for (int Stop = 0; Stop <= 4; ++Stop) Band.push_back(ResolveSpectrumColour(static_cast<float>(Stop) / 4.0f, 1.0f));
        Pen.GradientBar(ImVec2(32.0f, 218.0f), ImVec2(576.0f, 7.0f), Band);
        for (const int Wavelength : {380, 480, 580, 680, 780})
        {
            const std::string Caption = std::to_string(Wavelength);
            Pen.Text(ImVec2(32.0f + static_cast<float>(Wavelength - 380) / 400.0f * 576.0f, 252.0f), Caption.c_str(), ResolveCssColour(0xAC9F86FF), 14.0f);
        }
        Pen.Text(ImVec2(320.0f, 277.0f), "VISIBLE WAVELENGTH / nm", ResolveCssColour(0x877E6AFF), 13.0f);
        {
            // 📝 .so-spectrum-stats: two readouts (24 px), border-top, padding-top 16 px, margin-top 12 px.
            const float  RowTall = 9.0f * 1.4f + 5.0f + 24.0f * 1.25f;
            const ImVec2 Min     = Page.Space(1.0f + 16.0f + RowTall, 12.0f, 0.0f);
            DrawList->AddLine(ImVec2(Min.x, Min.y + 0.5f * S), ImVec2(Min.x + Page.QueryWidth(), Min.y + 0.5f * S), ResolveCssColour(0xE8C78B19), S);
            const std::string PeakFigure = ResolveFigure("%.0f", static_cast<double>(Peak));
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(Min.x, Min.y + 17.0f * S), "Blackbody peak", PeakFigure.c_str(), "nm", 24.0f, 10.0f, -1.3f, 1.25f);
            (void)ReadoutBlock::ConstructAt(Page, ImVec2(Min.x + 0.5f * (Page.QueryWidth() + 12.0f * S), Min.y + 17.0f * S), "Visible window", "380\xE2\x80\x93" "780", "nm",
                                          24.0f, 10.0f, -1.3f, 1.25f);
        }
        CaptionText::Construct(Page, "Normalised thermal reference, not measured radiance. The renderer uses its existing RGB approximation; RGB mode uses your custom "
                              "tint. The blackbody peak may fall outside the visible window.");
        Page.EndCard();

        // 📝 A Frontier addition (no demo counterpart): the native sun's render switches stay reachable.
        Page.BeginCard(FlatLook(0x20221EFF, 24.0f), "Frontier render switches", "09 / ENGINE");
        Changed |= ToggleRow::Construct(Page, "RayShadows", "Ray-traced sun shadows", Parameters.CastRaytracedShadow);
        Changed |= ToggleRow::Construct(Page, "LightShafts", "Volumetric light shafts", Parameters.VolumetricLightShaft);
        Page.EndCard();
    }
    else
    {
        // ── 07 / ANGULAR SIZE ──────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(AngularLook(), "Apparent solar disc", "07 / ANGULAR SIZE");
        const std::string Diameter = ResolveFigure("%.2f", static_cast<double>(Radius * 2.0f));
        ReadoutBlock::Construct(Page, "Angular diameter", Diameter.c_str(), "\xC2\xB0");
        DiagramDrag Drag;
        const DiagramPen   Pen = DiagramCanvas::Begin(Page, "##SolarDisc", 640.0f, 280.0f, 12.0f, 12.0f, &Drag, "Solar disc size editor \xC2\xB7 drag horizontally");
        const ImVec2           Centre(320.0f, 130.0f);
        for (const float Ring : {45.0f, 85.0f, 118.0f}) Pen.Circle(Centre, Ring, 0, ResolveCssColour(0xE8C78B22));
        Pen.Line(ImVec2(180.0f, 130.0f), ImVec2(460.0f, 130.0f), ResolveCssColour(0xE8C78B2A), 1.0f, 2.0f, 5.0f);
        Pen.Line(ImVec2(320.0f, 5.0f), ImVec2(320.0f, 255.0f), ResolveCssColour(0xE8C78B2A), 1.0f, 2.0f, 5.0f);
        const float Disc = 12.0f + Radius / 2.0f * 76.0f;
        Pen.Glow(Centre, Disc + 42.0f, IM_COL32(232, 199, 139, 255), 0.12f * Parameters.AureoleGain);
        Pen.Circle(Centre, Disc, Parameters.DiscVisible ? ResolveCssColour(0xF1D7A6FF) : ResolveCssColour(0x776E5BFF));
        Pen.Line(ImVec2(320.0f - Disc, 130.0f), ImVec2(320.0f + Disc, 130.0f), ResolveCssColour(0xFFF3CF66), 1.0f);
        Pen.Text(ImVec2(24.0f, 261.0f), "ANGULAR SIZE / EXAGGERATED", ResolveCssColour(0xAC9E83FF), 14.0f, -1);
        if (Drag.Engaged && Drag.Delta.x != 0.0f)
        {
            Radius                        = std::clamp(Radius + Drag.Delta.x * 0.005f, RadiusMinimum, RadiusMaximum);
            Parameters.AngularDiameterDeg = 2.0f * Radius;
            Changed                       = true;
        }
        if (SliderRow::Construct(Page, "SunRadius", "Angular diameter", Radius, RadiusMinimum, RadiusMaximum, 0.005f, "\xC2\xB0", 2.0f))
        {
            Parameters.AngularDiameterDeg = 2.0f * Radius;
            Changed                       = true;
        }
        Changed |= SliderRow::Construct(Page, "SunGlow", "Optical aureole", Parameters.AureoleGain, 0.0f, AureoleMaximum, 0.01f, "\xC3\x97");
        CaptionText::Construct(Page, "Disc diagram is exaggerated for editing; the sky uses the actual angular diameter. The Sun disc quick toggle remains in Overview.");
        Page.EndCard();

        // ── 08 / SUBCOMPONENT ──────────────────────────────────────────────────────────────────────────────────────
        Page.BeginCard(SubcomponentLook(), "Connected optics", "08 / SUBCOMPONENT");
        CaptionText::Construct(Page, "Lens flare is a separate optical subcomponent. Opening it does not change sunlight or exposure.");
        if (NextButton::Construct(Page, "##OpenLensFlare", "Open Lens Flare \xE2\x86\x97"))
        {
            InspectorRuntime::Instance().RequestSelection("OpticalFlare");
        }
        Page.EndCard();
    }

    FooterRow::Construct(Page, "DIRECTIONAL LIGHT / Z UP", "Settings apply live \xE2\x86\x97");
    Page.Finalize();
    ImGui::PopID();
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void SolarInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
std::string SolarInspector::QueryDirectoryReadout() const { return {}; }
void SolarInspector::ConstructInspector() {}
void SolarInspector::ResolveDayPath() {}
} // namespace Frontier

#endif
