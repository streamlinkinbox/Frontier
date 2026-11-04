/*==========================================================================================================================================
                                                         WINDMESOSCALESECTION.CPP
==========================================================================================================================================*/
// 🧩 Mesoscale: the slow variation of speed and direction over tens of kilometres, so a weather-scale view is not one uniform
//    arrow — charted across a 100 km transect — with its length and strength.

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

bool WindMesoscaleSection::Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading)
{
    using namespace SectionRows;
    char Length[32];
    FormatDistance(Wind.MesoscaleScaleMetres, Length, sizeof(Length));
    BeginSection(Page, "Mesoscale", "Globe", Wind.MesoscaleStrength > 0.0f ? Length : "Off",
                 "Proposed for the 100 km range: a slow variation of speed (\xC2\xB1strength) and direction (\xC2\xB1" "55\xC2\xB0 \xC3\x97 "
                 "strength) over tens of kilometres, drifting with the wind.");

    // 📝 The speed 10 m up along a west–east transect through the origin, gusts and eddies left out.
    ForceFieldPieces::CurvePlot Plot;
    ForceFieldPieces::PlotCurve Curve;
    ForceFieldSpace             Space;
    ForceFieldConfiguration     Controls  = Reading.Forces != nullptr ? *Reading.Forces : ForceFieldConfiguration{};
    WindConfiguration           Reference = Wind;
    Controls.IncludeTurbulence            = false;
    Reference.Enabled                     = true;
    Reference.GustStrength                = 0.0f;
    Reference.CurlNoiseTurbulence         = 0.0f;
    ConfigureAtmosphericFlow(Space, Controls, Reference, Reading.TimelineSeconds);
    double Fastest = std::max(1.0, static_cast<double>(Wind.SpeedMetresPerSecond));
    for (int Step = 0; Step <= 200; ++Step)
    {
        const double East     = -50000.0 + Step * 500.0;
        const auto   Velocity = EvaluateWorldFlow(Space, {East, 0.0, Wind.GroundHeightMetres + 10.0}, false);
        const double Speed    = std::hypot(Velocity[0], Velocity[1]);
        Curve.Points.push_back({East / 1000.0, Speed});
        Fastest = std::max(Fastest, Speed);
    }
    Curve.Stroke = IM_COL32(159, 180, 214, 255);
    Curve.Fill   = IM_COL32(159, 180, 214, 22);
    Plot.Curves.push_back(std::move(Curve));
    Plot.XRange = {-50.0, 50.0};
    Plot.YRange = {0.0, Fastest * 1.2};
    char Text[64];
    std::snprintf(Text, sizeof(Text), "Base %.1f m/s", Wind.SpeedMetresPerSecond);
    Plot.Guides.push_back({Wind.SpeedMetresPerSecond, Text});
    Plot.XTicks = {{-50.0, "\xE2\x88\x92" "50 km"}, {-25.0, "\xE2\x88\x92" "25"}, {0.0, "0"}, {25.0, "25"}, {50.0, "50 km"}};
    char Full[16];
    std::snprintf(Full, sizeof(Full), "%.0f", Fastest);
    Plot.YTicks  = {{0.0, "0"}, {Fastest, Full}};
    Plot.Caption = "Speed 10 m up along a west\xE2\x80\x93" "east transect, gusts and eddies left out";
    Plot.Height  = 280.0f;
    ForceFieldPieces::ConstructCurvePlot(Page, "MesoscaleTransect", Plot);

    bool   Changed = false;
    double Scale   = Wind.MesoscaleScaleMetres;
    if (Distance(Page, "MesoscaleLength", "Mesoscale length \xC2\xB7 proposed", Scale, 2000.0, 100000.0))
    {
        Wind.MesoscaleScaleMetres = static_cast<float>(Scale);
        Changed                   = true;
    }
    Changed |= Amount(Page, "MesoscaleStrength", "Mesoscale strength \xC2\xB7 proposed", Wind.MesoscaleStrength, 0.0f, 0.6f, 0.01f, "");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
