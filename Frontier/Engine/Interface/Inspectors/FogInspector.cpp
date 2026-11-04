/*==========================================================================================================================================
                                                              FOGINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 FogInspectorDashboard.js and NativeLocalFogPanel.js: contrast targets, density terraces, spectral shutters and local media.

#include "Engine/Interface/Inspectors/FogInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

std::string Figure(const char *Format, double Quantity)
{
    char Text[96]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

void BeginFogCard(ControlColumn &Page, const char *Heading, const char *Caption, bool Aerial)
{
    CardLook Look;
    Look.Fill        = ResolveCssColour(Aerial ? 0x22292AFF : 0x202724FF);
    Look.FillEnd     = ResolveCssColour(Aerial ? 0x181D20FF : 0x181D1DFF);
    Look.Border      = ResolveCssColour(0xAAC4B61A);
    Look.Radius      = 22;
    Look.PaddingSide = Look.PaddingTop = Look.PaddingBottom = 20;
    Look.MarginTop = Look.MarginBottom = 14;
    Look.GradientDegrees               = 145;
    Look.GradientStop                  = 1;
    Page.BeginCard(Look, nullptr, nullptr);
    SectionHeading::Construct(Page, Heading, Caption, SectionCaptionPlacement::Below);
}

void BeginLocalCard(ControlColumn &Page, const char *Heading)
{
    CardLook Look;
    Look.Fill            = ResolveCssColour(0x252525FF);
    Look.Border          = ResolveCssColour(0x343434FF);
    Look.FillEnd         = ResolveCssColour(0x202020FF);
    Look.GradientDegrees = 135;
    Look.GradientStop    = 1;
    Look.PaddingTop      = 22;
    Look.PaddingSide = Look.PaddingBottom = 24;
    Look.HeadingGlyph                     = "NativeFog";
    Look.HeadingMargin                    = 28;
    Look.Radius                           = 22;
    Look.MarginBottom                     = 16;
    Page.BeginCard(Look, Heading, nullptr);
}

std::array<double, 3> Transmission(const FogConfiguration &Fog, float Height, float Distance, bool Aerial)
{
    return EnvironmentReadings::FogTransmission(Fog, {0, 0, Height}, {Distance, 0, Height}, Aerial);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VISIBILITY TRANSECT
//------------------------------------------------------------------------------------------------------------------------

bool FogInspector::ConstructVisibility(ControlColumn &Page)
{
    const float Range = Aerial ? 20000.0f : 500.0f;
    BeginFogCard(Page, "A walk into the fog", "NEUTRAL CONTRAST TARGETS", Aerial);
    ReadoutBlock::Construct(
        Page, "",
        Figure("%.1f", EnvironmentReadings::NeutralContrast(Transmission(Parameters, ProbeHeight, ProbeDistance, Aerial)) * 100).c_str(),
        "% contrast retained", 52, 11, -2, 1);
    DiagramDrag Drag;
    auto        Pen =
        DiagramCanvas::Begin(Page, "FogTransect", 640, 425, 12, 20, &Drag, "Fog distance transect · drag the ruler or use arrow keys");
    if (Drag.Engaged) ProbeDistance = std::clamp((Drag.Point.x - 45) / 550 * Range, 0.0f, Range);
    if (Drag.Key == ImGuiKey_Home)
        ProbeDistance = 0;
    else if (Drag.Key != ImGuiKey_None)
        ProbeDistance = std::clamp(ProbeDistance + (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_DownArrow ? -1 : 1) * Range /
                                                       100 * Drag.Step,
                                   0.0f, Range);
    Pen.Path({{45, 350}, {305, 110}, {590, 350}}, ResolveCssColour(0xB1C7C328));
    Pen.Line({40, 350}, {600, 350}, ResolveCssColour(0xB1C7C336));
    for (int Target = 5; Target >= 0; --Target)
    {
        const float Distance = Range * Target / 5, Scale = 1 / (1 + Target * 0.38f);
        const float Width = 90 * Scale, Height = 145 * Scale, CentreX = 100.0f + Target * 90, CentreY = 315.0f - Target * 28;
        const auto  Channels = Transmission(Parameters, ProbeHeight, Distance, Aerial);
        const auto  Colour   = [&](double Linear)
        {
            int Rgb[3]{};
            for (int Band = 0; Band < 3; ++Band)
                Rgb[Band] = static_cast<int>(std::lround(std::pow(Linear * Channels[Band] + 0.16 * (1 - Channels[Band]), 1 / 2.2) * 255));
            return IM_COL32(Rgb[0], Rgb[1], Rgb[2], 255);
        };
        Pen.Rect({CentreX - Width / 2 - 4, CentreY - Height - 4}, {Width + 8, Height + 8}, ResolveCssColour(0x131B1CFF), 7);
        Pen.Rect({CentreX - Width / 2, CentreY - Height}, {Width, Height}, Colour(0.08), 3);
        Pen.Rect({CentreX - Width / 2, CentreY - Height}, {Width / 2, Height}, Colour(0.8), 3);
        Pen.Text({CentreX, CentreY + 23},
                 Figure(Distance >= 1000 ? "%.1f km" : "%.0f m", Distance >= 1000 ? Distance / 1000 : Distance).c_str(),
                 ResolveCssColour(0xA8BBB7FF), 18);
    }
    const float ProbeX = 45 + ProbeDistance / Range * 550;
    Pen.Line({45, 390}, {595, 390}, ResolveCssColour(0xB4CFCA44), 2);
    Pen.Circle({ProbeX, 390}, 10, ResolveCssColour(0xD5E4DCFF));
    Pen.Line({ProbeX, 370}, {ProbeX, 405}, ResolveCssColour(0xD5E4DC66));
    Pen.Text({45, 36}, "NEAR / CLEAR", ResolveCssColour(0x94AAA4FF), 19, -1);
    Pen.Text({595, 36}, "FAR / ATTENUATED", ResolveCssColour(0x94AAA4FF), 19, 1);
    SliderRow::Construct(Page, "FogProbeDistance", "Probe distance", ProbeDistance, 0, Range, 1, "m");
    SliderRow::Construct(Page, "FogProbeHeight", "Probe height", ProbeHeight, 0, 1000, 1, "m");
    CaptionText::Construct(Page,
                           "Fixed neutral targets under fixed reference light, not a scene render. Probe edits do not change the scene. "
                           "This is the demo’s extinction reference; PlanetaryAtmosphere retains its native optical transport.",
                           16, 0);
    Page.EndCard();
    CaptionText::Construct(Page,
                           (Aerial ? Parameters.AerialEnabled : Parameters.Enabled)
                               ? "Configured medium enabled · diagnostic shows reference extinction."
                               : "Configured medium is off · its reference remains visible.",
                           0, 20);
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DENSITY AND CLEAR INTERVAL
//------------------------------------------------------------------------------------------------------------------------

bool FogInspector::ConstructMedium(ControlColumn &Page)
{
    BeginFogCard(Page, Aerial ? "The clear interval" : "Density terraces", Aerial ? "DISTANCE GATE" : "WORLD HEIGHT / RELATIVE DENSITY",
                 Aerial);
    bool Changed = false;
    if (!Aerial)
    {
        float       FalloffHeight = 1 / std::max(0.002f, Parameters.HeightFalloff);
        auto        Pen           = DiagramCanvas::Begin(Page, "DensityTerraces", 640, 455, 12, 20);
        const float Top           = std::max(200.0f, FalloffHeight * 6);
        for (int Tier = 7; Tier >= 0; --Tier)
        {
            const float Altitude = Tier / 7.0f * Top, ρ = std::exp(-Altitude / FalloffHeight);
            const float Height = 380.0f - Tier * 43, Width = 310 * ρ;
            Pen.Path({{110, Height}, {420, Height}, {455, Height - 17}, {145, Height - 17}, {110, Height}}, ResolveCssColour(0xA8C8BE21));
            if (Width > 0.2f)
            {
                const ImVec2 Points[] = {Pen.QueryScreen({110, Height}), Pen.QueryScreen({110 + Width, Height}),
                                         Pen.QueryScreen({145 + Width, Height - 17}), Pen.QueryScreen({145, Height - 17})};
                Pen.QueryDrawList()->AddConvexPolyFilled(Points, 4, ResolveAlpha(ResolveCssColour(0xA4C6B6FF), 0.14f + ρ * 0.5f));
            }
            Pen.Text({78, Height - 2}, Figure("%.0f m", Altitude).c_str(), ResolveCssColour(0x92AAA0FF), 19, 1);
            Pen.Text({590, Height - 2}, Figure("%.1f%%", ρ * 100).c_str(), ResolveCssColour(0xBDD1C4FF), 19, 1);
        }
        Pen.Text({110, 435}, "SEA LEVEL → UPPER MEDIUM", ResolveCssColour(0x8BA396FF), 19, -1);
        Changed |= SliderRow::Construct(Page, "FogDensity", "Fog density", Parameters.BaseDensity, 0, 0.05f, 0.0001f, "/m");
        if (SliderRow::Construct(Page, "FogHeight", "Falloff height", FalloffHeight, 1, 500, 1, "m"))
        {
            Parameters.HeightFalloff = 1 / FalloffHeight;
            Changed                  = true;
        }
        CaptionText::Construct(Page,
                               "Each terrace is a world altitude. Filled width is exp(−height / falloff height), relative to sea level. "
                               "The altitude range adapts to the falloff setting; density remains the sea-level coefficient.",
                               16, 0);
    }
    else
    {
        DiagramDrag Drag;
        auto Pen = DiagramCanvas::Begin(Page, "AerialGate", 640, 265, 12, 20, &Drag, "Aerial clear-start gate · drag or use arrow keys");
        if (Drag.Engaged || Drag.Key != ImGuiKey_None)
        {
            float Distance = Parameters.AerialStartMetres;
            if (Drag.Engaged)
                Distance = (Drag.Point.x - 48) / 544 * 2000;
            else if (Drag.Key == ImGuiKey_Home)
                Distance = 0;
            else
                Distance += (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_DownArrow ? -10 : 10) * Drag.Step;
            Parameters.AerialStartMetres = std::clamp(Distance, 0.0f, 2000.0f);
            Changed                      = true;
        }
        const float Gate = 48 + Parameters.AerialStartMetres / 2000 * 544;
        Pen.Rect({48, 40}, {Gate - 48, 145}, ResolveCssColour(0xA8C8BE0C), 8);
        Pen.Line({48, 185}, {592, 185}, ResolveCssColour(0xA8C8BE55));
        Pen.Line({Gate, 30}, {Gate, 195}, ResolveCssColour(0xD8E1BBFF), 2, 3, 6);
        Pen.Circle({Gate, 30}, 8, ResolveCssColour(0xD8E1BBFF));
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 80; ++Sample)
        {
            const float Distance = Sample / 80.0f * 2000;
            Points.push_back(
                {48 + Sample / 80.0f * 544,
                 175 - 120 * (1 - std::exp(-Parameters.AerialDensity * std::max(0.0f, Distance - Parameters.AerialStartMetres)))});
        }
        Pen.Path(Points, ResolveCssColour(0xB4D1CDFF), 2.5f);
        Pen.Text({48, 225}, "0 m", ResolveCssColour(0x8BA396FF), 20, -1);
        Pen.Text({592, 225}, "2,000 m", ResolveCssColour(0x8BA396FF), 20, 1);
        Pen.Text({320, 261}, "NO EXTINCTION BEFORE THE START", ResolveCssColour(0x8BA396FF), 19);
        Changed |= SliderRow::Construct(Page, "AerialDensity", "Aerial density", Parameters.AerialDensity, 0, 0.001f, 0.00001f, "/m");
        Changed |= SliderRow::Construct(Page, "AerialStart", "Clear start distance", Parameters.AerialStartMetres, 0, 2000, 10, "m");
        CaptionText::Construct(Page,
                               "The clear interval is distance from the camera, not world altitude. The schematic curves grey-reference "
                               "attenuation; channel-specific transmission is in Spectrum.",
                               16, 0);
        CaptionText::Construct(
            Page,
            "These coefficients edit the demo diagnostic and are saved. Native aerial perspective uses the atmosphere medium instead.", 16,
            0);
    }
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECTRUM AND SCATTER
//------------------------------------------------------------------------------------------------------------------------

bool FogInspector::ConstructResponse(ControlColumn &Page)
{
    BeginFogCard(Page, Aerial ? "Three spectral shutters" : "In-scattered colour", Aerial ? "RGB TRANSMISSION" : "LINEAR RGB SOURCE",
                 Aerial);
    bool Changed = false;
    if (Aerial)
    {
        auto                  Pen       = DiagramCanvas::Begin(Page, "AerialShutters", 640, 340, 12, 20);
        const auto            Channels  = Transmission(Parameters, ProbeHeight, ProbeDistance, true);
        constexpr uint32_t    Colours[] = {0xC9A397FF, 0xB9CCA1FF, 0xA6C7D3FF};
        constexpr const char *Labels[]  = {"RED", "GREEN", "BLUE"};
        for (int Band = 0; Band < 3; ++Band)
        {
            const float Left = 65.0f + Band * 190;
            Pen.Rect({Left, 40}, {130, 205}, ResolveCssColour(0x101617FF), 15);
            for (int Slat = 0; Slat < 12; ++Slat)
            {
                const float Height = 48.0f + Slat * 16, Fill = static_cast<float>(std::clamp(Channels[Band] * 12 - Slat, 0.0, 1.0));
                Pen.Rect({Left + 8, Height}, {114, 10}, ResolveCssColour(0xFFFFFF08), 3);
                if (Fill > 0) Pen.Rect({Left + 8, Height}, {114 * Fill, 10}, ResolveCssColour(Colours[Band]), 3);
            }
            Pen.Text({Left + 65, 285}, Figure("%.1f%%", Channels[Band] * 100).c_str(), ResolveCssColour(Colours[Band]), 30);
            Pen.Text({Left + 65, 325}, Labels[Band], ResolveCssColour(0x8FA79DFF), 19);
        }
        Changed |= SliderRow::Construct(Page, "AerialMie", "Mie blend", Parameters.AerialMieBlend, 0, 1, 0.01f, "");
        SliderRow::Construct(Page, "SpectrumProbe", "Spectrum probe distance", ProbeDistance, 0, 20000, 1, "m");
        CaptionText::Construct(Page,
                               "Spectral extinction blends RGB coefficients [0.45, 0.70, 1.00] toward equal grey extinction as Mie blend "
                               "increases. These are the demo shader coefficients, not measured wavelengths.",
                               16, 0);
        Page.EndCard();
    }
    else
    {
        Changed |= TintRow::Construct(Page, "FogColour", "Fog tint", Parameters.TintLinear);
        CaptionText::Construct(Page,
                               "Picker values are sRGB-encoded; settings store linear RGB. This authored tint is not a change to "
                               "transmission. Native fog tint is not connected.",
                               16, 0);
        Page.EndCard();
        BeginFogCard(Page, "Facing the Sun", "FORWARD WEIGHT / cos⁸", false);
        auto Pen = DiagramCanvas::Begin(Page, "FogForward", 640, 260, 12, 20);
        Pen.Line({45, 195}, {595, 195}, ResolveCssColour(0xA8C8BE33));
        for (int Sample = 0; Sample <= 60; ++Sample)
        {
            const float Height = static_cast<float>(EnvironmentReadings::FogForwardWeight(
                                     (-90 + Sample * 3) * EnvironmentReadings::Pi / 180, Parameters.SunScatter)) *
                                 150;
            Pen.Rect({45.0f + Sample * 9, 195 - Height}, {5, Height}, ResolveCssColour(0xCFDAB29C), 2);
        }
        Pen.Text({45, 235}, "−90°", ResolveCssColour(0x8BA396FF), 20, -1);
        Pen.Text({320, 235}, "TOWARD SUN", ResolveCssColour(0xB8C8ADFF), 20);
        Pen.Text({595, 235}, "+90°", ResolveCssColour(0x8BA396FF), 20, 1);
        Changed |= SliderRow::Construct(Page, "FogScatter", "Sun scatter", Parameters.SunScatter, 0, 1, 0.01f, "");
        CaptionText::Construct(Page,
                               "The exact diagnostic weight is max(0, cos(angle))⁸ × Sun scatter. This edits the demo reference, not "
                               "native fog radiance; PlanetaryAtmosphere uses its Henyey–Greenstein phase function.",
                               16, 0);
        Page.EndCard();
    }
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCAL VOLUME
//------------------------------------------------------------------------------------------------------------------------

bool FogInspector::ConstructLocal(ControlColumn &Page)
{
    PageHeading::Construct(Page, "LocalFogHeader", "ENVIRONMENT", "Local Volumetric Fog", nullptr, "FINITE SCATTERING MEDIUM");
    auto Tile = [&](const char *Id, const char *Caption, bool &Enabled, const char *Symbol)
    {
        const SwitchTile Choices[] = {{Caption, &Enabled, TileGlyph::Sun, false, false, nullptr, Symbol}};
        return SwitchTiles::Construct(Page, Id, Choices, 1, 1, 12, 0);
    };
    bool Changed = Tile("LocalFogEnabled", "Enabled", Parameters.Enabled, "NativeLocalFog");
    BeginLocalCard(Page, "Volume");
    constexpr const char *Shapes[] = {"Box", "Ellipsoid", "Sphere", "Dome"};
    int Shape = Parameters.BoundsShape == 0 ? 1 : Parameters.BoundsShape == 1 ? 0 : static_cast<int>(Parameters.BoundsShape);
    Changed |= SelectRow::Construct(Page, "LocalFogShape", "Shape", Shapes, 4, Shape);
    Parameters.BoundsShape = Shape == 0 ? 1 : Shape == 1 ? 0 : Shape;
    Changed |= Tile("LocalFogFalloff", "Density falloff", Parameters.DensityFalloff, "NativeFog");
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (NextButton::Construct(Page, "PlaceLocalFog", "Place in front of camera", Access.CameraReady, "Requires a viewport camera."))
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Parameters.CentreMetres[Axis] = static_cast<float>(std::round(Access.CameraEye[Axis] + Access.CameraForward[Axis] * 25));
        Parameters.Enabled = true;
        Changed            = true;
    }
    constexpr const char *Positions[] = {"Position X", "Position Y", "Centre height"};
    constexpr const char *Extents[]   = {"Extent X", "Extent Y", "Extent Z"};
    for (size_t Axis = 0; Axis < 3; ++Axis)
        Changed |= SliderRow::Construct(Page, Positions[Axis], Positions[Axis], Parameters.CentreMetres[Axis], Axis == 2 ? -5.0f : -100.0f,
                                        Axis == 2 ? 60.0f : 100.0f, 0.1f, "m");
    if (Parameters.BoundsShape < 2)
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Changed |= SliderRow::Construct(Page, Extents[Axis], Extents[Axis], Parameters.HalfExtentsMetres[Axis], Axis == 2 ? 0.5f : 1.0f,
                                            Axis == 2 ? 60.0f : 200.0f, 0.5f, "m");
    }
    else
    {
        Changed |= SliderRow::Construct(Page, "LocalFogRadius", "Radius", Parameters.HalfExtentsMetres[0], 1, 200, 0.5f, "m");
        if (Parameters.BoundsShape == 3)
            Changed |=
                SliderRow::Construct(Page, "LocalFogDomeHeight", "Dome height", Parameters.HalfExtentsMetres[2], 0.5f, 60, 0.5f, "m");
    }
    Changed |= SliderRow::Construct(Page, "LocalFogSoftness", "Edge softness", Parameters.EdgeSoftness, 0.02f, 1, 0.01f, "");
    CaptionText::Construct(Page, "Z-up adaptation: horizontal X/Y, vertical Z. Dome centre height sets its base. Extents are half sizes, "
                                 "matching the demo volume bounds.");
    Page.EndCard();
    BeginLocalCard(Page, "Medium");
    Changed |= SliderRow::Construct(Page, "LocalFogDensity", "Density σ", Parameters.LocalDensity, 0, 2, 0.01f, "/m");
    Changed |= SliderRow::Construct(Page, "LocalFogNoise", "Heterogeneity", Parameters.Heterogeneity, 0, 1, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "LocalFogScale", "Noise scale", Parameters.NoiseScaleMetres, 0.5f, 30, 0.1f, "m");
    Changed |= SliderRow::Construct(Page, "LocalFogSpeed", "Independent wind", Parameters.IndependentWindMps, 0, 5, 0.05f, "m/s");
    Changed |= Tile("LocalFogFollowWind", "Follow Wind Field", Parameters.FollowWind, "Wind");
    CaptionText::Construct(Page, "Independent wind is used only when Follow Wind Field is off.");
    Changed |= TintRow::Construct(Page, "LocalFogAlbedo", "Albedo", Parameters.Albedo);
    Changed |= SliderRow::Construct(Page, "LocalFogAbsorption", "Absorption", Parameters.Absorption, 0, 1, 0.01f, "");
    Page.EndCard();
    BeginLocalCard(Page, "Lighting");
    Changed |= SliderRow::Construct(Page, "LocalFogAnisotropy", "Anisotropy g", Parameters.PhaseAnisotropy, 0, 0.95f, 0.01f, "");
    Changed |= Tile("LocalFogSelfShadow", "Self-shadowing", Parameters.SelfShadow, "Sun");
    constexpr const char *Qualities[] = {"16", "32", "64", "128"};
    constexpr uint32_t    Samples[]   = {16, 32, 64, 128};
    int                   Current     = -1;
    for (int Index = 0; Index < 4; ++Index)
        if (Parameters.LocalSamples == Samples[Index]) Current = Index;
    const int Quality = PresetStrip::Construct(Page, "LocalFogQuality", Qualities, 4, Current);
    if (Quality >= 0)
    {
        Parameters.LocalSamples = Samples[Quality];
        Changed                 = true;
    }
    Changed |= SliderRow::ConstructInteger(Page, "LocalFogSteps", "Ray-march samples", Parameters.LocalSamples, 8, 128, "samples");
    CaptionText::Construct(Page, "Bounded native ray marching with geometry-depth clipping, shape falloff, spatial noise and sun "
                                 "self-shadowing. Enabled local volumes share the highest requested fog sample budget.");
    if (const auto Readout = InspectorRuntime::Instance().QueryEnvironmentAccess().Rendering; Readout && Readout->Ready)
        CaptionText::Construct(Page, ("Actual native fog integration: " + std::to_string(Readout->FogMarchSamples) + " samples").c_str());
    Page.EndCard();
    BeginLocalCard(Page, "Wind Field");
    CaptionText::Construct(Page, "Shared force field · edit Force, Steadiness, Shear and Driving in Forcefield → Wind. This medium keeps "
                                 "its own Follow wind switch.");
    if (NextButton::Construct(Page, "OpenWind", "Open Wind ForceField")) InspectorRuntime::Instance().RequestSelection("Wind");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void FogInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Ray Shadows", "Sun shafts", &Parameters.ReceiveRayShadow});
}

void FogInspector::ConstructInspector()
{
    if (Aerial != Parameters.AerialOnly)
    {
        Aerial        = Parameters.AerialOnly;
        ProbeDistance = Aerial ? 8000.0f : 200.0f;
    }
    const auto    Theme = Parameters.LocalVolume ? ControlTheme::ResolveNative() : ControlTheme::ResolveFog();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;
    if (Parameters.LocalVolume)
        Changed = ConstructLocal(Page);
    else
    {
        bool &Enabled = Aerial ? Parameters.AerialEnabled : Parameters.Enabled;
        if (PageHeading::Construct(
                Page, "FogPower", "ENVIRONMENT / GEOMETRY MEDIA", Aerial ? "Atmospheric fog" : "Height fog", Enabled ? "● On" : "○ Off",
                Aerial ? "Distance, a clear start, then spectral extinction." : "A low-lying medium. A measurable loss of contrast.",
                Aerial ? "Toggle atmospheric fog" : "Toggle height fog"))
        {
            Enabled = !Enabled;
            Changed = true;
        }
        const char *Tabs[] = {"Visibility", "Medium", Aerial ? "Spectrum" : "Colour & sun"};
        TabStrip::Construct(Page, "FogTabs", Tabs, 3, ActivePane);
        switch (ActivePane)
        {
        case 0:
            Changed |= ConstructVisibility(Page);
            break;
        case 1:
            Changed |= ConstructMedium(Page);
            break;
        case 2:
            Changed |= ConstructResponse(Page);
            break;
        }
    }
    if (Aerial && !Parameters.LocalVolume)
        CaptionText::Construct(
            Page, "Atmospheric fog is a saved demo reference. The viewport’s aerial-perspective switch remains on the Atmosphere page.");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void FogInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void FogInspector::ConstructInspector() {}
} // namespace Frontier

#endif
