/*==========================================================================================================================================
                                                           WINDGUSTSECTION.CPP
==========================================================================================================================================*/
// 🧩 Gusts: the coherent gust wave that travels downwind, as the field evaluates it over a minute at the origin 10 m up with the
//    eddies left out, and its strength and frequency.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

bool WindGustSection::Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "%.0f %% \xC2\xB7 %.3f Hz", Wind.GustStrength * 100.0, Wind.GustFrequencyHz);
    BeginSection(Page, "Gusts", "Activity", Status,
                 "A coherent gust wave travels downwind: 0.62 sin \xCF\x86 + 0.38 sin(2.17 \xCF\x86 + 1.13), with \xCF\x86 = 2\xCF\x80 f t "
                 "\xE2\x88\x92 0.011 \xC2\xB7 distance along the wind.");

    ForceFieldPieces::CurvePlot Plot;
    ForceFieldPieces::PlotCurve Curve;
    ForceFieldSpace             Space;
    ForceFieldConfiguration     Controls = Reading.Forces != nullptr ? *Reading.Forces : ForceFieldConfiguration{};
    WindConfiguration           Reference = Wind;
    Controls.IncludeTurbulence            = false;
    Reference.Enabled                     = true;
    Reference.CurlNoiseTurbulence         = 0.0f;
    std::array<double, 2> Peak{0.0, 0.0};
    for (int Step = 0; Step <= 240; ++Step)
    {
        const double Second = Step * 0.25;
        ConfigureAtmosphericFlow(Space, Controls, Reference, Second);
        const auto   Velocity = EvaluateWorldFlow(Space, {0.0, 0.0, Wind.GroundHeightMetres + 10.0}, false);
        const double Speed    = std::hypot(Velocity[0], Velocity[1], Velocity[2]);
        Curve.Points.push_back({Second, Speed});
        if (Speed > Peak[1]) Peak = {Second, Speed};
    }
    const double Ceiling = std::max({Peak[1], Wind.SpeedMetresPerSecond * 1.1, 1.0}) * 1.15;
    Curve.Stroke = IM_COL32(222, 222, 222, 255);
    Curve.Fill   = IM_COL32(255, 255, 255, 16);
    Plot.Curves.push_back(std::move(Curve));
    Plot.XRange = {0.0, 60.0};
    Plot.YRange = {0.0, Ceiling};
    char Text[64];
    std::snprintf(Text, sizeof(Text), "Base %.1f m/s", Wind.SpeedMetresPerSecond);
    Plot.Guides.push_back({Wind.SpeedMetresPerSecond, Text});
    std::snprintf(Text, sizeof(Text), "%.1f m/s", Peak[1]);
    Plot.Markers.push_back({Peak[0], Peak[1], Text});
    for (int Second = 0; Second <= 60; Second += 15) Plot.XTicks.push_back({static_cast<double>(Second), std::to_string(Second) + " s"});
    const double Top = Ceiling / 1.15;
    char         Half[16], Full[16];
    std::snprintf(Half, sizeof(Half), "%.0f", Top / 2.0);
    std::snprintf(Full, sizeof(Full), "%.0f", Top);
    Plot.YTicks = {{0.0, "0"}, {Top / 2.0, Half}, {Top, Full}};
    std::snprintf(Text, sizeof(Text), "At the origin, 10 m up, eddies left out \xC2\xB7 period %.1f s", 1.0 / std::max(0.001f, Wind.GustFrequencyHz));
    Plot.Caption = Text;
    Plot.Height  = 290.0f;
    ForceFieldPieces::ConstructCurvePlot(Page, "GustEnvelope", Plot);

    bool Changed = SliderRow::Construct(Page, "WindGust", "Gust strength", Wind.GustStrength, 0, 1, 0.01f, "\xC3\x97");
    Changed |= SliderRow::Construct(Page, "GustFrequency", "Gust frequency", Wind.GustFrequencyHz, 0.01f, 2.0f, 0.005f, "Hz");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
