/*==========================================================================================================================================
                                                          PLUMECOMPOSITE.CPP
==========================================================================================================================================*/
// 🧩 The picture of one plume refresh: the march's radiance laid over a flat backdrop and encoded as sRGB bytes (PlumeComposite.h).

#include "Frontier/Extensions/Plume/PlumeComposite.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/ShadowProjection.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

using namespace GlobalIllumination;

namespace
{

float DecodeHalf(uint16_t Half)
{
    const uint32_t Sign = (Half >> 15) & 1u, Exponent = (Half >> 10) & 31u, Mantissa = Half & 1023u;
    float          Magnitude;
    if (Exponent == 0) { Magnitude = std::ldexp(static_cast<float>(Mantissa), -24); }
    else if (Exponent == 31) { Magnitude = Mantissa ? std::nanf("") : INFINITY; }
    else { Magnitude = std::ldexp(static_cast<float>(Mantissa | 1024u), static_cast<int>(Exponent) - 25); }
    return Sign ? -Magnitude : Magnitude;
}

// 📐 The sRGB encoding of a linear value in [0, 1], as a byte: a table of 4097 entries (the slope at black is 12.92, so the table's step is finer than a byte).
uint8_t EncodeSrgb(float Linear)
{
    static const std::array<uint8_t, 4097> Table = [] {
        std::array<uint8_t, 4097> Bytes{};
        for (size_t Index = 0; Index < Bytes.size(); ++Index)
        {
            const double LinearLevel  = static_cast<double>(Index) / 4096.0;
            const double EncodedLevel = LinearLevel <= 0.0031308 ? 12.92 * LinearLevel : 1.055 * std::pow(LinearLevel, 1.0 / 2.4) - 0.055;
            Bytes[Index]              = static_cast<uint8_t>(std::lround(std::clamp(EncodedLevel, 0.0, 1.0) * 255.0));
        }
        return Bytes;
    }();
    if (!(Linear > 0.0f)) return 0;
    return Table[static_cast<size_t>(std::min(Linear, 1.0f) * 4096.0f + 0.5f)];
}

// 📝 The flat backdrop the march's radiance is composited over: a sky that pales toward the horizon, a two-metre checker for the ground.
std::array<float, 3> ResolveBackdrop(const Vector3& Origin, const Vector3& Direction)
{
    if (Direction[2] >= -1.0e-4)
    {
        const float HorizonBlend = static_cast<float>(std::min(1.0, std::max(0.0, Direction[2] * 2.2)));
        return { 0.62f + (0.16f - 0.62f) * HorizonBlend, 0.74f + (0.30f - 0.74f) * HorizonBlend, 0.88f + (0.55f - 0.88f) * HorizonBlend };
    }
    const double Travel = -Origin[2] / Direction[2];
    const double X = Origin[0] + Direction[0] * Travel, Y = Origin[1] + Direction[1] * Travel;
    const bool   DarkCell    = (static_cast<long>(std::floor(X / 2.0)) + static_cast<long>(std::floor(Y / 2.0))) % 2 == 0;
    const float  HorizonFade = static_cast<float>(std::min(1.0, Travel / 90.0));
    const float  GroundTone  = DarkCell ? 0.16f : 0.21f;
    return { GroundTone + (0.62f - GroundTone) * HorizonFade, GroundTone + (0.74f - GroundTone) * HorizonFade, GroundTone + (0.88f - GroundTone) * HorizonFade };
}

// 📝 The march's rgba16float as floats, once a texel and not once a pixel: a value that is not finite is dark (opaque in alpha), the alpha is held to [0, 1].
void ResolveRadianceTexels(const std::vector<uint8_t>& Radiance, std::vector<float>& Texels)
{
    const size_t Count = Radiance.size() / 2u;
    Texels.resize(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        uint16_t Half;
        std::memcpy(&Half, &Radiance[Index * 2u], 2);
        const float Decoded      = DecodeHalf(Half);
        const bool  AlphaChannel = (Index & 3u) == 3u;
        float       Held         = std::isfinite(Decoded) ? Decoded : (AlphaChannel ? 1.0f : 0.0f);
        if (AlphaChannel) Held = std::clamp(Held, 0.0f, 1.0f);
        Texels[Index] = Held;
    }
}

// 📝 The backdrop of a camera and a picture size, linear RGB. It does not depend on the plume, so it is resolved again only when the camera or the size is
//    not the last one's: a refresh with the camera at rest takes it as it was.
void ResolveBackdropPicture(const CameraPose& Camera, std::array<uint32_t, 2> Picture, PlumeBackdropPicture& Backdrop)
{
    const std::array<double, 8> Key = { Camera.Position[0], Camera.Position[1], Camera.Position[2], Camera.Forward[0], Camera.Forward[1], Camera.Forward[2],
                                        Camera.VerticalFieldOfView, Camera.Aspect };
    if (Backdrop.Resolved && Backdrop.Key == Key && Backdrop.Picture == Picture) return;
    const Vector3 Right      = NormalizeVector(CrossVector(Camera.Forward, { 0.0, 0.0, 1.0 }));
    const Vector3 Up         = CrossVector(Right, Camera.Forward);
    const double  HalfHeight = std::tan(Camera.VerticalFieldOfView * 0.5);
    Backdrop.Colours.resize(static_cast<size_t>(Picture[0]) * Picture[1] * 3u);
    for (uint32_t Y = 0; Y < Picture[1]; ++Y)
    {
        const double V = (1.0 - (Y + 0.5) / Picture[1] * 2.0) * HalfHeight;
        for (uint32_t X = 0; X < Picture[0]; ++X)
        {
            const double  U         = ((X + 0.5) / Picture[0] * 2.0 - 1.0) * HalfHeight * Camera.Aspect;
            const Vector3 Direction = NormalizeVector(AddVector(Camera.Forward, AddVector(ScaleVector(Right, U), ScaleVector(Up, V))));
            const auto    Colour    = ResolveBackdrop(Camera.Position, Direction);
            float*        Written   = &Backdrop.Colours[(static_cast<size_t>(Y) * Picture[0] + X) * 3u];
            Written[0] = Colour[0];
            Written[1] = Colour[1];
            Written[2] = Colour[2];
        }
    }
    Backdrop.Key      = Key;
    Backdrop.Picture  = Picture;
    Backdrop.Resolved = true;
}

} // namespace

// 📝 Premultiplied radiance plus transmittance times the backdrop, the march's own texel nearest each picture pixel. A plume that is not resident records
//    no march at all: the picture then holds the backdrop alone, as a host's composite would without a plume.
void ResolvePlumePicture(const std::vector<uint8_t>& Radiance, bool Idle, const CameraPose& Camera, std::array<uint32_t, 2> Picture, std::array<uint32_t, 2> Trace,
                         uint32_t Scale, PlumeBackdropPicture& Backdrop, std::vector<uint8_t>& Pixels)
{
    ResolveBackdropPicture(Camera, Picture, Backdrop);
    if (!Idle) ResolveRadianceTexels(Radiance, Backdrop.Texels);
    Pixels.resize(static_cast<size_t>(Picture[0]) * Picture[1] * 4u);
    for (uint32_t Y = 0; Y < Picture[1]; ++Y)
    {
        for (uint32_t X = 0; X < Picture[0]; ++X)
        {
            const float* Colour      = &Backdrop.Colours[(static_cast<size_t>(Y) * Picture[0] + X) * 3u];
            float        Channels[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            if (!Idle)
            {
                const size_t Texel = (static_cast<size_t>(std::min(Y / Scale, Trace[1] - 1u)) * Trace[0] + std::min(X / Scale, Trace[0] - 1u)) * 4u;
                for (int Channel = 0; Channel < 4; ++Channel) Channels[Channel] = Backdrop.Texels[Texel + Channel];
            }
            uint8_t* Destination = &Pixels[(static_cast<size_t>(Y) * Picture[0] + X) * 4u];
            for (int Channel = 0; Channel < 3; ++Channel) Destination[Channel] = EncodeSrgb(Channels[Channel] * PlumeExposure + Channels[3] * Colour[Channel]);
            Destination[3] = 255;
        }
    }
}

} // namespace Frontier
