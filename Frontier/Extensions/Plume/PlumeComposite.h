/*==========================================================================================================================================
                                                          PLUMECOMPOSITE.H
==========================================================================================================================================*/
// 🧩 The picture of one plume refresh: the march's radiance laid over a flat backdrop (a sky that pales toward the horizon, a two-metre checker
//    for the ground) and encoded as sRGB bytes. The backdrop does not depend on the plume, so it is kept between refreshes and resolved again only when
//    the camera or the picture's size is not the last one's.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::GlobalIllumination
{
struct CameraPose;
}

namespace Frontier
{

inline constexpr float PlumeExposure = 0.42f; // [-] the march's radiance as the picture shows it (the plume exhibit's)

// 📝 What a refresh keeps for the composite: the backdrop of the last camera and picture size (linear RGB, three floats a pixel; it does not depend on the plume,
//    so a refresh that finds both as the last one left them takes it from here) and the march's texels as floats, one a channel.
struct PlumeBackdropPicture
{
    bool                    Resolved = false;
    std::array<double, 8>   Key{};          // [-]  the camera's position, forward, vertical field of view and aspect
    std::array<uint32_t, 2> Picture{};      // [px] the size the backdrop was resolved for
    std::vector<float>      Colours{};      // [-]  the backdrop, row by row
    std::vector<float>      Texels{};       // [-]  the march's rgba, a float a channel
};

// 📝 Radiance is the march's rgba16float read back (Trace texels, row by row); Idle: no march was recorded, the picture is the backdrop alone. Each picture
//    pixel takes the march's texel nearest it (Scale picture pixels to a texel): premultiplied radiance times PlumeExposure plus the texel's alpha times the
//    backdrop. A value that is not finite counts as dark (opaque in alpha); the alpha is held to [0, 1]. Pixels is resized to Picture, rgba8, opaque.
void ResolvePlumePicture(const std::vector<uint8_t>& Radiance, bool Idle, const GlobalIllumination::CameraPose& Camera, std::array<uint32_t, 2> Picture,
                         std::array<uint32_t, 2> Trace, uint32_t Scale, PlumeBackdropPicture& Backdrop, std::vector<uint8_t>& Pixels);

} // namespace Frontier
