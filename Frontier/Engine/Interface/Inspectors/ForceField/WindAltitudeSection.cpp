/*==========================================================================================================================================
                                                         WINDALTITUDESECTION.CPP
==========================================================================================================================================*/
// 🧩 Altitude: the Ekman profile — the speed through the first 3 km and the hodograph of the travel direction veering with
//    height — and the shear and veer per kilometre.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

struct ProfilePoint
{
    double Altitude = 0.0;
    double Speed    = 0.0;
    double East     = 0.0;
    double North    = 0.0;
};

std::vector<ProfilePoint> ResolveProfile(const WindConfiguration& Wind)
{
    std::vector<ProfilePoint> Points;
    for (int Altitude = 0; Altitude <= 3000; Altitude += 100)
    {
        const double Kilometres = Altitude / 1000.0;
        const double Speed      = Wind.SpeedMetresPerSecond * std::max(0.15, 1.0 + Wind.ShearPerKilometre * std::min(Kilometres, 12.0));
        const double Bearing    = (Wind.DirectionDegrees + Wind.VeerDegreesPerKilometre * Kilometres) * 3.14159265358979323846 / 180.0;
        Points.push_back({static_cast<double>(Altitude), Speed, std::sin(Bearing) * Speed, std::cos(Bearing) * Speed});
    }
    return Points;
}

} // namespace

bool WindAltitudeSection::Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading&)
{
    using namespace SectionRows;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "+%.0f %%/km \xC2\xB7 %.0f\xC2\xB0/km", Wind.ShearPerKilometre * 100.0, Wind.VeerDegreesPerKilometre);
    BeginSection(Page, "Altitude", "Layers", Status,
                 "Ekman profile: the speed gains with height (never below 15 % of the base) and the direction veers. First the speed "
                 "through 0\xE2\x80\x93" "3 km, then the hodograph, in plan with north up.");
    const std::vector<ProfilePoint> Points = ResolveProfile(Wind);
    double                          Fastest = 1.0;
    for (const ProfilePoint& Point : Points) Fastest = std::max(Fastest, Point.Speed);
    constexpr int Marked[] = {0, 10, 20, 30};
    char          Text[64];

    ForceFieldPieces::CurvePlot Profile;
    ForceFieldPieces::PlotCurve Speed;
    for (const ProfilePoint& Point : Points) Speed.Points.push_back({Point.Speed, Point.Altitude});
    Speed.Stroke = IM_COL32(223, 231, 243, 255);
    Profile.Curves.push_back(std::move(Speed));
    for (const int Index : Marked)
    {
        std::snprintf(Text, sizeof(Text), "%.1f m/s", Points[Index].Speed);
        Profile.Markers.push_back({Points[Index].Speed, Points[Index].Altitude, Text});
    }
    const double Reach = Fastest * 1.15;
    Profile.XRange     = {0.0, Reach};
    Profile.YRange     = {0.0, 3000.0};
    char Half[24], Full[24];
    std::snprintf(Half, sizeof(Half), "%.0f m/s", Reach / 2.0);
    std::snprintf(Full, sizeof(Full), "%.0f", Reach);
    Profile.XTicks = {{0.0, "0"}, {Reach / 2.0, Half}, {Reach, Full}};
    Profile.YTicks = {{0.0, "0"}, {1000.0, "1 km"}, {2000.0, "2 km"}, {3000.0, "3 km"}};
    Profile.Height = 300.0f;
    ForceFieldPieces::ConstructCurvePlot(Page, "WindAltitude", Profile);

    // 📝 The hodograph keeps one scale on both axes (the plot is 560 × 248 logical px), fitted around the track and the origin.
    ForceFieldPieces::CurvePlot Hodograph;
    double                      MinimumX = 0.0, MaximumX = 0.0, MinimumY = 0.0, MaximumY = 0.0;
    for (const ProfilePoint& Point : Points)
    {
        MinimumX = std::min(MinimumX, Point.East);
        MaximumX = std::max(MaximumX, Point.East);
        MinimumY = std::min(MinimumY, Point.North);
        MaximumY = std::max(MaximumY, Point.North);
    }
    const double Aspect  = 560.0 / 248.0;
    double       HalfX   = std::max(0.5, (MaximumX - MinimumX) * 0.62);
    double       HalfY   = std::max(0.5, (MaximumY - MinimumY) * 0.62);
    HalfX                = std::max(HalfX, HalfY * Aspect);
    HalfY                = HalfX / Aspect;
    const double MiddleX = (MinimumX + MaximumX) / 2.0, MiddleY = (MinimumY + MaximumY) / 2.0;
    Hodograph.XRange     = {MiddleX - HalfX, MiddleX + HalfX};
    Hodograph.YRange     = {MiddleY - HalfY, MiddleY + HalfY};
    Hodograph.Curves.push_back({{{Hodograph.XRange[0], 0.0}, {Hodograph.XRange[1], 0.0}}, IM_COL32(255, 255, 255, 31), 1.0f, false, 0});
    Hodograph.Curves.push_back({{{0.0, Hodograph.YRange[0]}, {0.0, Hodograph.YRange[1]}}, IM_COL32(255, 255, 255, 31), 1.0f, false, 0});
    ForceFieldPieces::PlotCurve Track;
    for (const ProfilePoint& Point : Points) Track.Points.push_back({Point.East, Point.North});
    Track.Stroke = IM_COL32(159, 180, 214, 255);
    Hodograph.Curves.push_back(std::move(Track));
    for (const int Index : Marked)
    {
        Hodograph.Curves.push_back({{{0.0, 0.0}, {Points[Index].East, Points[Index].North}}, IM_COL32(223, 231, 243, 115), 1.0f, Index > 0, 0});
        Hodograph.Markers.push_back({Points[Index].East, Points[Index].North, Index == 0 ? "ground" : (Index == 30 ? "3 km" : "")});
    }
    std::snprintf(Text, sizeof(Text), "Toward %.0f\xC2\xB0 at the ground, %.0f\xC2\xB0 at 3 km", Wind.DirectionDegrees,
                  Wind.DirectionDegrees + Wind.VeerDegreesPerKilometre * 3.0);
    Hodograph.Caption = Text;
    Hodograph.Height  = 320.0f;
    ForceFieldPieces::ConstructCurvePlot(Page, "WindHodograph", Hodograph);

    bool Changed = SliderRow::Construct(Page, "WindShear", "Speed gain per km", Wind.ShearPerKilometre, 0, 2, 0.01f, "/km");
    Changed |= SliderRow::Construct(Page, "WindVeer", "Direction veer per km", Wind.VeerDegreesPerKilometre, -30, 30, 0.5f, "\xC2\xB0/km");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
