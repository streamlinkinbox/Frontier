//============================================================================================================================================
//                                                          SKYPROJECTION.SLANG
//============================================================================================================================================
// 🧩 The sky-view parametrisation: absolute azimuth on U, square-root elevation on V so the horizon receives the texels.

#pragma once
#include "IntrinsicParity.slang"

// 📝 u = φ/2π + ½,  v = ½ − ½·sign(e)·√(|e| / (π/2)); v = 0 is the zenith row, ½ the horizon, 1 the nadir.
float2 ProjectSkyCoordinates(float3 Direction)
{
    let Azimuth = atan2(Direction.y, Direction.x);
    let Elevation = asin(clamp(Direction.z, -1.0, 1.0));
    let U = Azimuth * (0.5 / 3.14159265358979) + 0.5;
    let V = 0.5 - 0.5 * FloatSign(Elevation) * sqrt(abs(Elevation) / 1.5707963267949);
    return float2(U, V);
}

float3 ReconstructSkyDirection(float2 Coordinates)
{
    let Azimuth = (Coordinates.x - 0.5) * 2.0 * 3.14159265358979;
    let Signed = (0.5 - Coordinates.y) * 2.0;
    let Elevation = FloatSign(Signed) * Signed * Signed * 1.5707963267949;
    return float3(cos(Elevation) * cos(Azimuth), cos(Elevation) * sin(Azimuth), sin(Elevation));
}
