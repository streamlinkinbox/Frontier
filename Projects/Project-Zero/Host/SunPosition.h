//================================================================================
// SunPosition.h — host-side solar ephemeris (the panel's own sunDirAt +
// kelvinRGB, in double precision). The .slang core takes the solved sun as
// inputs (see SkyConfiguration); this header solves them. Also used by the Vulkan
// engine to fill the uniform buffer.
//================================================================================
#ifndef PROJECT_ZERO_SUN_POSITION_H
#define PROJECT_ZERO_SUN_POSITION_H

#include <algorithm>
#include <cmath>

namespace ProjectZero {

constexpr double kPiPanel = 3.141592653589793;
constexpr double kDeg2RadPanel = kPiPanel / 180.0;

struct SunState
{
    double dirX, dirY, dirZ;    // [-] unit, sky frame (+Y up)
    double elevationDeg;        // [deg]
    double colorR, colorG, colorB;
};

// sunDirAt(t): lat = sun_lat, HA = (t-12)/24 * 2 pi, sinE = cos(lat) cos(HA),
// az = atan2(sin HA, cos HA sin lat) + pi + sun_az. Verbatim from the panel.
inline SunState SolveSun(double localHours, double latitudeDeg, double azimuthOffsetDeg,
                         double colourTemperatureK) noexcept
{
    const double lat = latitudeDeg * kDeg2RadPanel;
    const double ha = (localHours - 12.0) / 24.0 * kPiPanel * 2.0;
    const double sinE = std::cos(lat) * std::cos(ha);
    const double el = std::asin(std::clamp(sinE, -1.0, 1.0));
    const double az = std::atan2(std::sin(ha), std::cos(ha) * std::sin(lat))
                    + kPiPanel + azimuthOffsetDeg * kDeg2RadPanel;
    SunState s;
    s.dirX = std::sin(az) * std::cos(el);
    s.dirY = std::sin(el);
    s.dirZ = -std::cos(az) * std::cos(el);
    s.elevationDeg = el / kDeg2RadPanel;

    // kelvinRGB: Tanner Helland's fit, exactly as the panel writes it.
    const double k = colourTemperatureK / 100.0;
    double r = (k <= 66.0) ? 255.0 : 329.7 * std::pow(k - 60.0, -0.133);
    double g = (k <= 66.0) ? 99.47 * std::log(k) - 161.1 : 288.1 * std::pow(k - 60.0, -0.0755);
    double b = (k >= 66.0) ? 255.0 : ((k <= 19.0) ? 0.0 : 138.5 * std::log(k - 10.0) - 305.0);
    s.colorR = std::clamp(r, 0.0, 255.0) / 255.0;
    s.colorG = std::clamp(g, 0.0, 255.0) / 255.0;
    s.colorB = std::clamp(b, 0.0, 255.0) / 255.0;
    return s;
}

} // namespace ProjectZero

#endif // PROJECT_ZERO_SUN_POSITION_H
