/*==========================================================================================================================================
                                                           WINDFLOWSECTION.CPP
==========================================================================================================================================*/
// 🧩 Flow: where the air goes. The compass dial edits the direction of travel and the base speed (drag the source dot; arrows
//    turn or speed it; Home is 7 m/s from the north), with the base speed, the bearing it comes from and the ground height.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

// 📝 The dial: 155 px to 40 m/s around (320, 235) of a 640 × 480 canvas; the dot is where the wind comes from.
bool ConstructCompass(ControlColumn& Page, WindConfiguration& Wind)
{
    const ControlTheme& Theme = Page.QueryTheme();
    DiagramDrag         Drag;
    DiagramPen          Pen   = DiagramCanvas::Begin(Page, "WindCompass", 640, 480, 6, 12, &Drag,
                                                     "Wind flow compass \xC2\xB7 drag the source bearing and speed; arrows edit bearing or speed");
    const bool          Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
    {
        const float Along = Drag.Point.x - 320, Across = 235 - Drag.Point.y;
        Wind.DirectionDegrees     = EnvironmentReadings::WrapDegrees(static_cast<float>(std::atan2(Along, Across) * 180 / EnvironmentReadings::Pi) + 180);
        Wind.SpeedMetresPerSecond = std::clamp(std::hypot(Along, Across) / 155 * 40, 0.0f, 40.0f);
    }
    if (Drag.Key == ImGuiKey_Home)
    {
        Wind.DirectionDegrees     = 180;
        Wind.SpeedMetresPerSecond = 7;
    }
    if (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_RightArrow)
        Wind.DirectionDegrees = EnvironmentReadings::WrapDegrees(Wind.DirectionDegrees + (Drag.Key == ImGuiKey_RightArrow ? 1 : -1) * Drag.Step);
    if (Drag.Key == ImGuiKey_UpArrow || Drag.Key == ImGuiKey_DownArrow)
        Wind.SpeedMetresPerSecond = std::clamp(Wind.SpeedMetresPerSecond + (Drag.Key == ImGuiKey_UpArrow ? 0.1f : -0.1f) * Drag.Step, 0.0f, 40.0f);
    const float  θ = static_cast<float>(Wind.DirectionDegrees * EnvironmentReadings::Pi / 180);
    const ImVec2 Forward(std::sin(θ), -std::cos(θ)), Lateral(-Forward.y, Forward.x);
    const auto   Point = [&](float Along, float Across) {
        return ImVec2(320 + Forward.x * Along + Lateral.x * Across, 235 + Forward.y * Along + Lateral.y * Across);
    };
    Pen.Circle({320, 235}, 155, ResolveCssColour(0xFFFFFF05), ResolveCssColour(0xFFFFFF2D));
    Pen.Circle({320, 235}, 98, 0, ResolveCssColour(0xFFFFFF14));
    for (int Tick = 0; Tick < 24; ++Tick)
    {
        const float Angle = static_cast<float>(Tick / 24.0 * EnvironmentReadings::Pi * 2);
        Pen.Line({320 + std::sin(Angle) * 150, 235 - std::cos(Angle) * 150}, {320 + std::sin(Angle) * 155, 235 - std::cos(Angle) * 155},
                 ResolveCssColour(0xFFFFFF58));
    }
    for (int Lane = -2; Lane <= 2; ++Lane)
    {
        const float         Across = Lane * 39.0f, Length = std::sqrt(std::max(0.0f, 135 * 135 - Across * Across));
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 36; ++Sample)
            Points.push_back(Point(-Length + 2 * Length * Sample / 36, Across + std::sin(Sample / 36.0f * static_cast<float>(EnvironmentReadings::Pi)) * Lane * 3));
        Pen.Path(Points, ResolveCssColour(0xFFFFFF26), 1.5f);
        if (Wind.SpeedMetresPerSecond > 0)
        {
            const auto End = Point(Length * 0.48f, Across);
            Pen.Line(Point(Length * 0.48f - 12, Across), End, ResolveCssColour(0xDFE7F3FF), 2);
            Pen.Path({Point(Length * 0.48f - 9, Across - 5), End, Point(Length * 0.48f - 9, Across + 5)}, ResolveCssColour(0xDFE7F3FF), 2);
        }
    }
    const auto From = Point(-155 * Wind.SpeedMetresPerSecond / 40, 0);
    Pen.Line({320, 235}, From, ResolveCssColour(0xFFFFFF66), 1, 4, 6);
    Pen.Circle(From, 9, ResolveCssColour(0xFFFFFFFF));
    Pen.Circle({320, 235}, 4, ResolveCssColour(0x9FB4D6FF));
    Pen.Text({320, 54}, "N / +Y", Theme.Muted, 19);
    Pen.Text({516, 240}, "E", Theme.Muted, 19);
    Pen.Text({320, 433}, "S", Theme.Muted, 19);
    Pen.Text({126, 240}, "W", Theme.Muted, 19);
    Pen.Text({320, 475}, "DOT = WIND FROM / ARROWS = TRAVEL TO", Theme.Muted, 17);
    return Changed;
}

} // namespace

bool WindFlowSection::Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading&)
{
    using namespace SectionRows;
    char Status[32];
    std::snprintf(Status, sizeof(Status), "%.1f m/s", Wind.SpeedMetresPerSecond);
    BeginSection(Page, "Flow", "Compass", Status,
                 "Where the air goes. The dial is the direction of travel (DirectionDegrees); meteorology names the opposite, the "
                 "bearing it comes from. Drag the source dot radially for the base speed.");
    bool Changed = ConstructCompass(Page, Wind);
    Changed |= SliderRow::Construct(Page, "CloudWindSpeed", "Base speed", Wind.SpeedMetresPerSecond, 0, 40, 0.1f, "m/s");
    float FromBearing = EnvironmentReadings::WrapDegrees(Wind.DirectionDegrees + 180);
    if (SliderRow::Construct(Page, "CloudWindBearing", "Bearing from", FromBearing, 0, 360, 1, "\xC2\xB0"))
    {
        Wind.DirectionDegrees = EnvironmentReadings::WrapDegrees(FromBearing + 180);
        Changed               = true;
    }
    Changed |= Amount(Page, "WindGroundHeight", "Ground height \xC2\xB7 proposed", Wind.GroundHeightMetres, -200.0f, 3000.0f, 1.0f, "m", nullptr,
                      "The height the profile's 10 m reference stands on");
    const auto Readout = InspectorRuntime::Instance().QueryEnvironmentAccess().Rendering;
    char       Line[96];
    if (Readout && Readout->Ready)
        std::snprintf(Line, sizeof(Line), "Field speed at the camera: %.2f m/s (every contributor).",
                      std::hypot(Readout->WindAtCamera[0], Readout->WindAtCamera[1], Readout->WindAtCamera[2]));
    else
        std::snprintf(Line, sizeof(Line), "No live viewport sample: the dial shows the authored base flow.");
    CaptionText::Construct(Page, Line, 4.0f, 8.0f);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
