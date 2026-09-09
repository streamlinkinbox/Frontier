//============================================================================================================================================
//                                                        DAYLIGHTSOLVER.H
//============================================================================================================================================
// 🧩 How much light the sky is putting on the scene, in lux. An INCIDENT reading — what a handheld meter gives
//    with the dome on, pointed at the sky — as opposed to the reflected reading a frame of pixels can give.
//
//    🔴 WHY THIS EXISTS. Adaptive exposure metered the frame, and a frame changes when the camera moves. That
//    was reported four times as "the sky changes brightness when I move closer to or further from the box", and
//    the observation that the SKY was moving is what proves it: sky radiance depends on view direction alone, so
//    if it changes when the camera translates, the cause is the exposure and nothing else. Three successive
//    metering rules — a dark-pixel floor, a percentile window, a median-anchored window — each reduced it and
//    none could remove it, because all three ask the frame.
//
//    The illuminance falling on a scene is a property of the SUN AND SKY. It is the same wherever the camera
//    stands, so an exposure anchored to it cannot move when the camera does. That is the whole idea.
//
//    ⚠️ It is an integral over the sky, not a formula. The diffuse term is the sky's radiance integrated over
//    the hemisphere against the cosine, which at twilight is most of the light there is and is not proportional
//    to anything simple. It is evaluated with the shared CPU port of the shader's own atmosphere, so the number
//    the exposure uses and the sky the viewer sees cannot describe different air.
//
//    Cached, because it is far too expensive to run per frame and far too slow-moving to need to be: it depends
//    on the sun's elevation and the aerosol load, and on a fast clock those move slowly enough that recomputing
//    on a small change is free.

#pragma once

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        SOLVER
//------------------------------------------------------------------------------------------------------------------------

class DaylightSolver
{
public:
    // Horizontal illuminance at ground level: the direct beam projected onto the ground, plus the whole sky
    //    integrated against the cosine. SunIlluminance is the above-atmosphere figure the sky record carries.
    [[nodiscard]] float QueryIlluminance(float SunIlluminance, float SunElevationRadians, float Turbidity) noexcept;

    // The two halves separately, for the readout and for the proof — the diffuse term is the one that carries
    //    twilight, and a bug in it would be invisible in the sum while the sun is up.
    [[nodiscard]] float QueryDirect()  const noexcept { return CachedDirect;  }
    [[nodiscard]] float QueryDiffuse() const noexcept { return CachedDiffuse; }

    // 🔴 What the exposure should actually be anchored to, and NOT the illuminance above. Illuminance is
    //    cosine-weighted — it answers "how much light lands on the ground" — and at a low sun nearly all of the
    //    sky's light is in a band a few degrees above the horizon, where the cosine is almost zero. So the
    //    illuminance collapses while the thing the camera is pointed at stays bright. Measured, the two
    //    disagreed by 4.7 stops at a 10° sun and 22 by deep twilight, which put every sunrise and sunset
    //    outside the exposure's dead zone and handed metering straight back to the frame — restoring the
    //    camera dependence this was built to remove, exactly when it was being looked at.
    //
    //    This is the mean radiance a camera would see pointed in an arbitrary direction: the sky averaged over
    //    the upper hemisphere by SOLID ANGLE, and the lit ground over the lower. Still camera-independent —
    //    it averages over all directions rather than using any particular one — but it tracks what is on screen.
    [[nodiscard]] float QueryAnchorLuminance(float SunIlluminance, float SunElevationRadians, float Turbidity) noexcept;

    // How far the sun may move, and the aerosol load change, before the integral is worth running again.
    static constexpr float kElevationTolerance = 0.0035f;   // [rad] 0.2°, well under a minute of a real day
    static constexpr float kTurbidityTolerance = 0.02f;     // [-]

private:
    float CachedElevation   = -1.0e9f;
    float CachedTurbidity   = -1.0e9f;
    float CachedSun         = -1.0e9f;
    float CachedDirect      = 0.0f;
    float CachedDiffuse     = 0.0f;
    float CachedIlluminance = 0.0f;
    float CachedMeanSky     = 0.0f;   // [cd/m²] solid-angle mean over the upper hemisphere
};

} // namespace Frontier
