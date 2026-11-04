/*==========================================================================================================================================
                                                             PLANETINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Independent interactive planetary infographics, native surface previews and live sky placement.

#include "Engine/Interface/Inspectors/PlanetInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/PlanetSurfacePreview.h"
#include <cmath>
#include <cstdio>

namespace Frontier
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
std::string     Number(const char *Format, double Quantity)
{
    char Text[96];
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}
CardLook Instrument(ImU32 First, ImU32 Last, float Radius = 22)
{
    CardLook Look;
    Look.Fill            = First;
    Look.FillEnd         = Last;
    Look.GradientDegrees = 145;
    Look.GradientStop    = 1;
    Look.Radius          = Radius;
    Look.PaddingSide = Look.PaddingTop = Look.PaddingBottom = 20;
    Look.MarginBottom                                       = 18;
    return Look;
}
} // namespace

void PlanetInspector::RefreshPreview()
{
    const std::array<double, 20> Query = {double(Parameters.Body),
                                          Parameters.ApparentDiameterDegrees,
                                          Parameters.Brightness,
                                          Parameters.SpinDegrees,
                                          Parameters.TiltDegrees,
                                          Parameters.RollDegrees,
                                          Parameters.SurfaceTint[0],
                                          Parameters.SurfaceTint[1],
                                          Parameters.SurfaceTint[2],
                                          Parameters.BandContrast,
                                          Parameters.SurfaceDetail,
                                          Parameters.CloudVeil,
                                          Parameters.AtmosphereHeight,
                                          Parameters.LimbGlow,
                                          Parameters.RingInnerRadius,
                                          Parameters.RingOuterRadius,
                                          Parameters.RingOpticalDepth,
                                          double(Parameters.RingSystem),
                                          double(Parameters.AtmosphereShell),
                                          0};
    if (Preview.QueryReady() && Query == PreviewQuery) return;
    if (Preview.Configure(320, 240, GeneratePlanetSurfacePreview(Parameters))) PreviewQuery = Query;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VENUS / OPTICAL VEIL
//------------------------------------------------------------------------------------------------------------------------

bool PlanetInspector::ConstructVenus(ControlColumn &Page)
{
    Page.BeginCard(Instrument(ResolveCssColour(0x302D24FF), ResolveCssColour(0x1B1D1BFF), 30), nullptr, nullptr);
    SectionHeading::Construct(Page, "Cloud-shrouded disc", "VENUS / OPTICAL VEIL");
    auto Pen = DiagramCanvas::Begin(Page, "VenusDisc", 640, 380, 0, 12);
    Preview.Construct(Pen.QueryDrawList(), Pen.QueryScreen({90, 0}), Pen.QueryScreen({570, 360}));
    for (int Ring = 0; Ring < 3; ++Ring)
        Pen.Ellipse({320, 180}, 133 + Ring * 18, 133 + Ring * 18, 0, ResolveCssColour(0xD3B17B25));
    Pen.Line({42, 70}, {174, 70}, ResolveCssColour(0xDCCBA268));
    Pen.Text({42, 52}, "CLOUD DECK", ResolveCssColour(0xDCCBA2FF), 13, -1);
    Pen.Text({42, 90}, Number("%.0f%% veil", Parameters.CloudVeil * 100).c_str(), ResolveCssColour(0xAFA58EFF), 12, -1);
    Pen.Line({477, 260}, {607, 260}, ResolveCssColour(0xDCCBA268));
    Pen.Text({607, 282}, "ATMOSPHERIC LIMB", ResolveCssColour(0xDCCBA2FF), 12, 1);
    bool Changed = SliderRow::Construct(Page, "VenusVeil", "Cloud veil", Parameters.CloudVeil, 0, 1, .01f, "%", 100);
    Changed |= SwitchRow::Construct(Page, "VenusAir", "Atmospheric shell", Parameters.AtmosphereShell);
    Page.EndCard();
    Page.BeginCard(Instrument(ResolveCssColour(0x24231DFF), ResolveCssColour(0x171A18FF), 12), nullptr, nullptr);
    SectionHeading::Construct(Page, "Limb profile", "DRAG THE PROFILE TO CHANGE SHELL DEPTH", SectionCaptionPlacement::Below);
    DiagramDrag Drag;
    Pen = DiagramCanvas::Begin(Page, "VenusLimb", 640, 210, 12, 12, &Drag);
    for (int Stripe = 0; Stripe < 24; ++Stripe)
    {
        float Fraction = Stripe / 23.0f;
        float Depth    = 25 + Parameters.AtmosphereHeight * 1200;
        Pen.Rect({55 + Stripe * 22.0f, 170 - std::exp(-Fraction * 4) * Depth}, {20, std::exp(-Fraction * 4) * Depth},
                 ResolveAlpha(ResolveCssColour(0xD5B680FF), .12f + .55f * (1 - Fraction)), 3);
    }
    Pen.Line({42, 170}, {605, 170}, ResolveCssColour(0xB7A78366));
    Pen.Text({42, 198}, "SURFACE", ResolveCssColour(0xB7A783FF), 12, -1);
    Pen.Text({605, 198}, "SPACE", ResolveCssColour(0xB7A783FF), 12, 1);
    if (Drag.Engaged)
    {
        Parameters.AtmosphereHeight = std::clamp((170 - Drag.Point.y) / 1200, .005f, .3f);
        Changed                     = true;
    }
    Changed |= SliderRow::Construct(Page, "VenusDepth", "Shell height / radius", Parameters.AtmosphereHeight, .005f, .3f, .005f, "");
    Changed |= SliderRow::Construct(Page, "VenusGlow", "Limb glow", Parameters.LimbGlow, 0, 3, .01f, "×");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    JUPITER / LATITUDINAL BANDS
//------------------------------------------------------------------------------------------------------------------------

bool PlanetInspector::ConstructJupiter(ControlColumn &Page)
{
    Page.BeginCard(Instrument(ResolveCssColour(0x302720FF), ResolveCssColour(0x1C1C1DFF), 16), nullptr, nullptr);
    SectionHeading::Construct(Page, "Belts & zones", "JUPITER / LATITUDE INSTRUMENT");
    auto Pen = DiagramCanvas::Begin(Page, "JupiterBands", 640, 370, 10, 10);
    for (int Band = 0; Band < 13; ++Band)
    {
        const float Y        = 42 + Band * 22.0f;
        const float Strength = .5f + .5f * std::sin(Band * 1.65f + Parameters.SpinDegrees * .01745f);
        Pen.Rect({24, Y}, {88 + Strength * 110, 14}, ResolveAlpha(ResolveCssColour(0xDDBB8AFF), .15f + .35f * Strength), 4);
        if (Band % 3 == 0) Pen.Text({22, Y - 5}, Number("%.0f°", 90 - Band * 15).c_str(), ResolveCssColour(0xAA9784FF), 11, -1);
    }
    Preview.Construct(Pen.QueryDrawList(), Pen.QueryScreen({200, 0}), Pen.QueryScreen({640, 330}));
    Pen.Text({425, 355}, "ROTATING SURFACE / NATIVE PREVIEW", ResolveCssColour(0xBEAB93FF), 12);
    bool Changed = SliderRow::Construct(Page, "JupiterSpin", "Cloud longitude", Parameters.SpinDegrees, -180, 180, .5f, "°");
    Page.EndCard();
    Page.BeginCard(Instrument(ResolveCssColour(0x25221FFF), ResolveCssColour(0x191B1CFF), 26), nullptr, nullptr);
    SectionHeading::Construct(Page, "Band contrast", "DRAG THE WAVE AMPLITUDE", SectionCaptionPlacement::Below);
    DiagramDrag Drag;
    Pen = DiagramCanvas::Begin(Page, "JupiterContrast", 640, 205, 12, 12, &Drag);
    std::vector<ImVec2> Wave;
    for (int Sample = 0; Sample <= 128; ++Sample)
    {
        const float X = 35 + Sample / 128.0f * 570;
        Wave.push_back({X, 102 + std::sin(Sample / 128.0f * Pi * 12) * Parameters.BandContrast * 34});
    }
    Pen.Line({35, 102}, {605, 102}, ResolveCssColour(0xC3AF942A), 1, 3, 5);
    Pen.Path(Wave, ResolveCssColour(0xD9BE96FF), 2.2f);
    if (Drag.Engaged)
    {
        Parameters.BandContrast = std::clamp(std::abs(Drag.Point.y - 102) / 34, 0.0f, 2.0f);
        Changed                 = true;
    }
    Changed |= SliderRow::Construct(Page, "JupiterContrastValue", "Belt / zone contrast", Parameters.BandContrast, 0, 2, .01f, "×");
    Changed |= SliderRow::Construct(Page, "JupiterTilt", "Axis tilt toward view", Parameters.TiltDegrees, -85, 85, .5f, "°");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MARS / SURFACE RELIEF
//------------------------------------------------------------------------------------------------------------------------

bool PlanetInspector::ConstructMars(ControlColumn &Page)
{
    Page.BeginCard(Instrument(ResolveCssColour(0x302420FF), ResolveCssColour(0x1D1C1DFF), 28), nullptr, nullptr);
    SectionHeading::Construct(Page, "A world of relief", "MARS / DRAG THE GLOBE");
    DiagramDrag Drag;
    auto        Pen = DiagramCanvas::Begin(Page, "MarsGlobe", 640, 390, 0, 8, &Drag);
    Preview.Construct(Pen.QueryDrawList(), Pen.QueryScreen({65, 5}), Pen.QueryScreen({575, 387}));
    for (int Meridian = -2; Meridian <= 2; ++Meridian)
        Pen.Ellipse({320, 194}, std::abs(Meridian) * 44 + 7, 142, 0, ResolveCssColour(0xDF927D29));
    Pen.Line({147, 194}, {493, 194}, ResolveCssColour(0xE3A28B44), 1, 2, 6);
    Pen.Text({45, 35}, "N", ResolveCssColour(0xD7A28EFF), 14, -1);
    Pen.Text({595, 367}, Number("%.0f° longitude", Parameters.SpinDegrees).c_str(), ResolveCssColour(0xD7A28EFF), 14, 1);
    bool Changed = false;
    if (Drag.Engaged)
    {
        Parameters.SpinDegrees = std::clamp(Parameters.SpinDegrees + Drag.Delta.x * .5f, -180.0f, 180.0f);
        Parameters.TiltDegrees = std::clamp(Parameters.TiltDegrees - Drag.Delta.y * .35f, -85.0f, 85.0f);
        Changed                = true;
    }
    Changed |= SliderRow::Construct(Page, "MarsLongitude", "Surface longitude", Parameters.SpinDegrees, -180, 180, .5f, "°");
    Page.EndCard();
    Page.BeginCard(Instrument(ResolveCssColour(0x261E1CFF), ResolveCssColour(0x1A1C1DFF), 8), nullptr, nullptr);
    SectionHeading::Construct(Page, "Relief response", "PROCEDURAL ALBEDO / NOT ELEVATION", SectionCaptionPlacement::Below);
    Pen = DiagramCanvas::Begin(Page, "MarsRelief", 640, 190, 8, 12, &Drag);
    for (int Contour = 0; Contour < 6; ++Contour)
    {
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample < 100; ++Sample)
            Points.push_back(
                {25 + Sample * 5.95f, 35 + Contour * 23.0f + std::sin(Sample * .16f + Contour * .7f) * 12 * Parameters.SurfaceDetail});
        Pen.Path(Points, ResolveAlpha(ResolveCssColour(0xDC8B70FF), .2f + Contour * .11f), 1.3f);
    }
    if (Drag.Engaged)
    {
        Parameters.SurfaceDetail = std::clamp(Drag.Point.x / 320, 0.0f, 2.0f);
        Changed                  = true;
    }
    Changed |= SliderRow::Construct(Page, "MarsDetail", "Surface contrast", Parameters.SurfaceDetail, 0, 2, .01f, "×");
    Changed |= SwitchRow::Construct(Page, "MarsAir", "Thin atmosphere", Parameters.AtmosphereShell);
    Changed |= SliderRow::Construct(Page, "MarsHaze", "Atmosphere depth", Parameters.AtmosphereHeight, .005f, .3f, .005f, "");
    Changed |= SliderRow::Construct(Page, "MarsGlow", "Limb glow", Parameters.LimbGlow, 0, 3, .01f, "×");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SATURN / RING GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

bool PlanetInspector::ConstructSaturn(ControlColumn &Page)
{
    Page.BeginCard(Instrument(ResolveCssColour(0x282B2DFF), ResolveCssColour(0x181C20FF), 32), nullptr, nullptr);
    SectionHeading::Construct(Page, "An inclined annulus", "SATURN / RING PLANE");
    auto Pen = DiagramCanvas::Begin(Page, "SaturnSurface", 640, 400, 0, 10);
    Preview.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, -20}), Pen.QueryScreen({640, 460}));
    Pen.Text({35, 375}, Number("%.2f R inner", Parameters.RingInnerRadius).c_str(), ResolveCssColour(0xC9C1A8FF), 15, -1);
    Pen.Text({605, 375}, Number("%.2f R outer", Parameters.RingOuterRadius).c_str(), ResolveCssColour(0xC9C1A8FF), 15, 1);
    bool Changed = SwitchRow::Construct(Page, "SaturnRings", "Ring system", Parameters.RingSystem);
    Page.EndCard();
    Page.BeginCard(Instrument(ResolveCssColour(0x21262AFF), ResolveCssColour(0x191D20FF), 18), nullptr, nullptr);
    SectionHeading::Construct(Page, "Ring aperture", "DRAG RADIUS / DRAG VERTICAL FOR INCLINATION", SectionCaptionPlacement::Below);
    DiagramDrag Drag;
    Pen               = DiagramCanvas::Begin(Page, "SaturnAnnulus", 640, 280, 12, 12, &Drag);
    const float Outer = Parameters.RingOuterRadius * 58, Inner = Parameters.RingInnerRadius * 58;
    const float Flatten = .12f + .64f * std::abs(std::sin(Parameters.TiltDegrees * Pi / 180));
    Pen.Ellipse({320, 137}, Outer, Outer * Flatten, ResolveCssColour(0xC5B99019), ResolveCssColour(0xD7C79FBB));
    Pen.Ellipse({320, 137}, Inner, Inner * Flatten, ResolveCssColour(0x191D20FF), ResolveCssColour(0xD7C79F55));
    Pen.Circle({320, 137}, 40, ResolveCssColour(0x9D9278FF));
    Pen.Line({35, 137}, {605, 137}, ResolveCssColour(0xC5B99022), 1, 2, 6);
    Pen.Text({320, 270}, Number("%.0f° inclination", Parameters.TiltDegrees).c_str(), ResolveCssColour(0xC9C1A8FF), 15);
    if (Drag.Engaged)
    {
        Parameters.RingOuterRadius = std::clamp(std::abs(Drag.Point.x - 320) / 58, Parameters.RingInnerRadius + .05f, 4.0f);
        Parameters.TiltDegrees     = std::clamp((137 - Drag.Point.y) * .65f, -85.0f, 85.0f);
        Changed                    = true;
    }
    Changed |= SliderRow::Construct(Page, "SaturnInner", "Inner radius", Parameters.RingInnerRadius, 1.01f, 3.8f, .01f, "R");
    Parameters.RingOuterRadius = std::max(Parameters.RingOuterRadius, Parameters.RingInnerRadius + .05f);
    Changed |= SliderRow::Construct(Page, "SaturnOuter", "Outer radius", Parameters.RingOuterRadius, 1.06f, 4, .01f, "R");
    Parameters.RingInnerRadius = std::min(Parameters.RingInnerRadius, Parameters.RingOuterRadius - .05f);
    Changed |= SliderRow::Construct(Page, "SaturnDepth", "Optical depth", Parameters.RingOpticalDepth, 0, 4, .01f, "τ");
    Changed |= SliderRow::Construct(Page, "SaturnTilt", "Ring inclination", Parameters.TiltDegrees, -85, 85, .5f, "°");
    Changed |= SliderRow::Construct(Page, "SaturnRoll", "Ring-plane roll", Parameters.RollDegrees, -180, 180, .5f, "°");
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SKY PLACEMENT AND PAGE
//------------------------------------------------------------------------------------------------------------------------

bool PlanetInspector::ConstructPlacement(ControlColumn &Page)
{
    SectionHeading::Construct(Page, "Sky coordinates", "DATE / OBSERVER OR MANUAL PLACEMENT", SectionCaptionPlacement::Below);
    bool        Changed = SwitchRow::Construct(Page, "PlanetFollow", "Follow orbital solution", Parameters.FollowSky);
    DiagramDrag Drag;
    auto        Pen = DiagramCanvas::Begin(Page, "PlanetPlacement", 640, 300, 12, 18, &Drag);
    Pen.Rect({0, 0}, {640, 300}, ResolveCssColour(0x12181DFF), 16);
    for (int Row = 1; Row < 4; ++Row)
        Pen.Line({32, Row * 65.0f}, {608, Row * 65.0f}, ResolveCssColour(0xB7C9D222), 1, 2, 5);
    for (int Column = 0; Column < 7; ++Column)
        Pen.Line({32 + Column * 96.0f, 25}, {32 + Column * 96.0f, 270}, ResolveCssColour(0xB7C9D222), 1, 2, 5);
    const float X = 32 + (Parameters.AzimuthDegrees + 180) / 360 * 576, Y = 150 - Parameters.ElevationDegrees / 90 * 120;
    Pen.Circle({X, Y}, 9, Page.QueryTheme().Accent);
    Pen.Text({32, 290}, "−180° / EAST ORIGIN", Page.QueryTheme().Muted, 12, -1);
    Pen.Text({608, 290}, "+180°", Page.QueryTheme().Muted, 12, 1);
    if (Drag.Engaged)
    {
        Parameters.AzimuthDegrees   = std::clamp((Drag.Point.x - 32) / 576 * 360 - 180, -180.0f, 180.0f);
        Parameters.ElevationDegrees = std::clamp((150 - Drag.Point.y) / 120 * 90, -90.0f, 90.0f);
        Parameters.FollowSky        = false;
        Changed                     = true;
    }
    if (SliderRow::Construct(Page, "PlanetAzimuth", "Azimuth", Parameters.AzimuthDegrees, -180, 180, .1f, "°"))
    {
        Parameters.FollowSky = false;
        Changed              = true;
    }
    if (SliderRow::Construct(Page, "PlanetElevation", "Elevation", Parameters.ElevationDegrees, -90, 90, .1f, "°"))
    {
        Parameters.FollowSky = false;
        Changed              = true;
    }
    Changed |= SliderRow::Construct(Page, "PlanetRoll", "Surface roll", Parameters.RollDegrees, -180, 180, .5f, "°");
    Changed |= SliderRow::Construct(Page, "PlanetTilt", "Axis tilt", Parameters.TiltDegrees, -85, 85, .5f, "°");
    Changed |= SliderRow::Construct(Page, "PlanetSpin", "Spin longitude", Parameters.SpinDegrees, -180, 180, .5f, "°");
    return Changed;
}

void PlanetInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &Entries)
{
    Entries.push_back({"Rings", "Native annulus", &Parameters.RingSystem});
    Entries.push_back({"Atmosphere", "Native limb scattering", &Parameters.AtmosphereShell});
}

void PlanetInspector::ConstructInspector()
{
    const uint32_t     Body      = std::min(3u, Parameters.Body);
    auto               Theme     = ControlTheme::ResolveNative();
    constexpr uint32_t Accents[] = {0xDFCA9AFF, 0xD9B28CFF, 0xDE987AFF, 0xB7CCDAFF};
    Theme.Accent                 = ResolveCssColour(Accents[Body]);
    Theme.PageFill               = ResolveCssColour(0x14191CFF);
    Theme.Ink                    = ResolveCssColour(0xE0E7E4FF);
    Theme.Subtitle = Theme.Muted = ResolveCssColour(0x96A4A7FF);
    const auto    Faces          = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    const auto   &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (Parameters.FollowSky && Access.CelestialReady)
    {
        Parameters.AzimuthDegrees   = float(std::fmod(Access.PlanetPlacements[Body][0] + 540, 360) - 180);
        Parameters.ElevationDegrees = float(Access.PlanetPlacements[Body][1]);
    }
    constexpr const char *Subtitles[] = {"Cloud veil, limb light and orbital placement.", "Latitudinal bands and a rotating atmosphere.",
                                         "Surface contrast, orientation and sky position.",
                                         "Ring geometry, optical depth and the disc beneath."};
    bool                  Changed     = PageHeading::Construct(Page, "PlanetPower", "SOLAR BODIES / PLANET", PlanetBodyNames[Body],
                                          Parameters.Enabled ? "● Visible" : "○ Hidden", Subtitles[Body]);
    if (Changed) Parameters.Enabled = !Parameters.Enabled;
    const ActionEntry Actions[] = {{"PlanetFind", "Find in sky"}, {"PlanetPlace", "Place in view", Access.CameraReady}};
    ActionRowLook     ActionsLook;
    ActionsLook.MarginTop    = 0;
    ActionsLook.MarginBottom = 16;
    const int Action         = ActionRow::Construct(Page, Actions, ActionsLook);
    if (Action == 1)
    {
        Parameters.AzimuthDegrees   = float(std::atan2(Access.CameraForward[1], Access.CameraForward[0]) * 180 / Pi);
        Parameters.ElevationDegrees = 25;
        Parameters.FollowSky        = false;
        Parameters.Enabled          = true;
        Changed                     = true;
    }
    if (Action >= 0)
        InspectorRuntime::Instance().RequestSkyFocus(Parameters.AzimuthDegrees, Parameters.ElevationDegrees,
                                                     Parameters.ApparentDiameterDegrees *
                                                         (Parameters.RingSystem ? Parameters.RingOuterRadius : 1));
    constexpr const char *SurfaceNames[] = {"Veil", "Bands", "Relief", "Rings"};
    const char           *Tabs[]         = {SurfaceNames[Body], "Placement"};
    TabStrip::Construct(Page, "PlanetPages", Tabs, 2, ActivePane);
    RefreshPreview();
    if (ActivePane == 1)
        Changed |= ConstructPlacement(Page);
    else
    {
        switch (Body)
        {
        case 0:
            Changed |= ConstructVenus(Page);
            break;
        case 1:
            Changed |= ConstructJupiter(Page);
            break;
        case 2:
            Changed |= ConstructMars(Page);
            break;
        default:
            Changed |= ConstructSaturn(Page);
            break;
        }
        Changed |= SliderRow::Construct(Page, "PlanetSize", "Apparent diameter", Parameters.ApparentDiameterDegrees, .05f, 30, .05f, "°");
        Changed |= SliderRow::Construct(Page, "PlanetBrightness", "Disc brightness", Parameters.Brightness, 0, 5, .01f, "×");
        Changed |= TintRow::Construct(Page, "PlanetTint", "Surface tint", Parameters.SurfaceTint);
        CaptionText::Construct(Page, "Live native surface evaluation under fixed reference light; angular scale is compressed for "
                                     "inspection. Sky size and placement update the viewport directly.");
    }
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier
#elif FRONTIER_EDITOR
namespace Frontier
{
void PlanetInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void PlanetInspector::ConstructInspector() {}
} // namespace Frontier
#endif
