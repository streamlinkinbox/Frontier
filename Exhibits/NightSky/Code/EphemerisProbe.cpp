/*==========================================================================================================================================
                                                 EPHEMERIS PROBE
==========================================================================================================================================*/
// 🧭 Export old two-term and production lunar positions for an independent astronomy comparison.

#include "Engine/Rendering/CelestialModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double ToRad = Pi / 180.0;
constexpr double TtMinusUtc = 69.184 / 86400.0; // 2026; UTC approximates UT1 for this probe

struct Site { const char* Name; double Latitude, Longitude, Height; };
constexpr Site Sites[] = {{"Benoni", -26.1885, 28.3209, 0.0}, {"Greenwich", 51.4779, -0.0015, 46.0},
                          {"Equator", 0.0, 0.0, 0.0}, {"NorthPole", 90.0, 0.0, 0.0},
                          {"SouthPole", -90.0, 0.0, 2800.0}};

Frontier::SpatialVector Local(double RightAscension, double Declination, double Julian, const Site& Place)
{
    const double N = Julian - 2451545.0;
    const double Sidereal = (std::fmod(18.697374558 + 24.06570982441908*N, 24.0)*15.0 + Place.Longitude)*ToRad;
    const double H = Sidereal - RightAscension, P = Place.Latitude*ToRad;
    const double C = std::cos(Declination), S = std::sin(Declination);
    return {-C*std::sin(H), S*std::cos(P)-C*std::cos(H)*std::sin(P),
            S*std::sin(P)+C*std::cos(H)*std::cos(P)};
}

Frontier::SpatialVector EclipticToLocal(double Lambda, double Beta, double Julian, const Site& Place)
{
    const double Days = Julian - 2451545.0;
    const double Obliquity = (23.439 - 0.0000004*Days)*ToRad;
    const double RightAscension = std::atan2(std::sin(Lambda)*std::cos(Obliquity)-std::tan(Beta)*std::sin(Obliquity),
                                             std::cos(Lambda));
    const double Declination = std::asin(std::clamp(std::sin(Beta)*std::cos(Obliquity)+
        std::cos(Beta)*std::sin(Obliquity)*std::sin(Lambda), -1.0, 1.0));
    return Local(RightAscension, Declination, Julian, Place);
}

Frontier::SpatialVector OldTwoTermMoon(double Julian, const Site& Place)
{
    const double Days = Julian - 2451545.0;
    const double MeanLongitude = (218.316+13.176396*Days)*ToRad;
    const double MeanAnomaly = (134.963+13.064993*Days)*ToRad;
    const double F = (93.272+13.229350*Days)*ToRad;
    return EclipticToLocal(MeanLongitude + 6.289*ToRad*std::sin(MeanAnomaly),
                            5.128*ToRad*std::sin(F), Julian, Place);
}
} // namespace

int main()
{
    // Meeus example 47.a, also published in the Sonia Keys source's tests.
    const auto Example = Frontier::MoonGeocentric(Frontier::JulianDate(1992,4,12,0));
    if (std::abs(Example.LongitudeRad/ToRad - 133.162655) > 0.000002 ||
        std::abs(Example.LatitudeRad/ToRad - (-3.229126)) > 0.000002 ||
        std::abs(Example.DistanceKm - 368409.7) > 0.1)
    {
        std::fputs("Meeus example 47.a failed\n", stderr);
        return 1;
    }
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    if (Frontier::MoonGeocentric(NaN).DistanceKm != 0.0 ||
        Frontier::MoonGeocentric(Frontier::JulianDate(1400,1,1,0)).DistanceKm != 0.0 ||
        Frontier::MoonTopocentric(Example, 2451545.0, 91.0, 0.0).DistanceKm != 0.0 ||
        Frontier::MoonTopocentric({}, 2451545.0, 0.0, 0.0).DistanceKm != 0.0)
    {
        std::fputs("invalid ephemeris inputs were not rejected\n", stderr);
        return 1;
    }
    std::puts("site,latitude,longitude,height_m,year,month,day,hour,sun_e,sun_n,sun_u,old_e,old_n,old_u,geo_e,geo_n,geo_u,topo_e,topo_n,topo_u,distance_km,radius_rad,mean_phase,geometric_phase");
    for (const auto& Place : Sites)
        for (int Month = 1; Month <= 12; ++Month)
            for (int Day : {1,8,15,22})
                for (int Hour : {0,6,12,18})
                {
                    const double Utc = Frontier::JulianDate(2026, Month, Day, Hour);
                    const auto Sun = Frontier::SunDirectionLocal(Frontier::SunPosition(Utc, Place.Latitude, Place.Longitude));
                    const auto Old = OldTwoTermMoon(Utc, Place);
                    const auto Geocentric = Frontier::MoonGeocentric(Utc + TtMinusUtc);
                    const auto Geo = EclipticToLocal(Geocentric.LongitudeRad, Geocentric.LatitudeRad, Utc, Place);
                    const auto Topo = Frontier::MoonTopocentric(Geocentric, Utc, Place.Latitude, Place.Longitude, Place.Height);
                    std::printf("%s,%.17g,%.17g,%.17g,2026,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                        Place.Name, Place.Latitude, Place.Longitude, Place.Height, Month, Day, Hour,
                        Sun.X, Sun.Y, Sun.Z, Old.X, Old.Y, Old.Z, Geo.X, Geo.Y, Geo.Z,
                        Topo.Direction.X, Topo.Direction.Y, Topo.Direction.Z, Topo.DistanceKm, Topo.AngularRadiusRad,
                        Frontier::MoonPhase(Utc).Illumination, Frontier::MoonGeometricIllumination(Geocentric, Utc + TtMinusUtc));
                }
}
