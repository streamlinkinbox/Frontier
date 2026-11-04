/*==========================================================================================================================================
                                                          ATMOSPHEREBAKEIMAGES.CPP
==========================================================================================================================================*/
// 🧩 Bilinear sampling of native sky/transport tables, with the same sky-direction coordinates as AtmosphereScattering.slang.

#include "Engine/Interface/Inspectors/AtmosphereBakeImages.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr double Pi = 3.14159265358979323846;

uint8_t Encode(double Radiance, bool Transmission)
{
    if (!std::isfinite(Radiance)) return 0;
    Radiance             = std::max(0.0, Radiance);
    const double Display = Transmission ? std::min(1.0, Radiance) : Radiance / (1 + Radiance);
    return static_cast<uint8_t>(std::lround(255 * std::pow(Display, 1 / 2.2)));
}

std::array<double, 3> Sample(const std::vector<float> &Image, int Width, int Height, double U, double V)
{
    const double          X    = std::clamp(U, 0.0, 1.0) * Width - .5;
    const double          Y    = std::clamp(V, 0.0, 1.0) * Height - .5;
    const int             Left = static_cast<int>(std::floor(X)), Top = static_cast<int>(std::floor(Y));
    const double          Across = X - Left, Down = Y - Top;
    std::array<double, 3> Rgb{};
    for (int Row = 0; Row < 2; ++Row)
        for (int Column = 0; Column < 2; ++Column)
        {
            const size_t Pixel  = (std::clamp(Top + Row, 0, Height - 1) * Width + std::clamp(Left + Column, 0, Width - 1)) * 4;
            const double Weight = (Row ? Down : 1 - Down) * (Column ? Across : 1 - Across);
            for (size_t Band = 0; Band < 3; ++Band)
                Rgb[Band] += Image[Pixel + Band] * Weight;
        }
    return Rgb;
}

std::array<double, 2> SkyCoordinate(const AtmosphereBakeReadout &Bake, const std::array<double, 3> &Direction)
{
    const double Ground     = std::max(100.0f, Bake.Inputs.PlanetRadiusKm);
    const double Limb       = -std::asin(Ground / (Ground + std::max(.001f,Bake.ObserverAltitudeKm)));
    const double Elevation  = std::asin(std::clamp(Direction[1], -1.0, 1.0));
    const double Vertical   = Elevation < Limb
                                  ? .5 - .5 * std::sqrt(std::clamp((Elevation - Limb) / std::max(-Pi * .5 - Limb, -.0001), 0.0, 1.0))
                                  : .5 + .5 * std::sqrt(std::clamp((Elevation - Limb) / (Pi * .5 - Limb), 0.0, 1.0));
    const double Horizontal = std::hypot(Direction[0], Direction[2]);
    const double Solar      = std::hypot(Bake.SolarDirection[0], Bake.SolarDirection[2]);
    const double Cosine     = Horizontal > 1e-9 && Solar > 1e-9
                                  ? (Direction[0] * Bake.SolarDirection[0] + Direction[2] * Bake.SolarDirection[2]) / (Horizontal * Solar)
                                  : 1;
    return {std::acos(std::clamp(Cosine, -1.0, 1.0)) / Pi, Vertical};
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPLAY PIXELS
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> ResolveLookupPixels(const std::vector<float> &LinearRgba, bool Transmission)
{
    if (LinearRgba.empty() || LinearRgba.size() % 4) return {};
    std::vector<uint8_t> Rgba(LinearRgba.size(), 255);
    for (size_t Index = 0; Index < LinearRgba.size(); ++Index)
        if (Index % 4 != 3) Rgba[Index] = Encode(LinearRgba[Index], Transmission);
    return Rgba;
}

std::vector<uint8_t> ProjectAtmosphereLookup(const AtmosphereBakeReadout &Bake, int View)
{
    if (!Bake.Ready || Bake.SkyView.size() != 192u * 108u * 4u || Bake.Transmittance.size() != 256u * 64u * 4u) return {};
    constexpr int        Width = 320, Height = 160;
    std::vector<uint8_t> Pixels(Width * Height * 4, 0);
    for (int Row = 0; Row < Height; ++Row)
        for (int Column = 0; Column < Width; ++Column)
        {
            double                U = (Column + .5) / Width, V = (Row + .5) / Height;
            std::array<double, 3> Rgb;
            if (View == 2)
                Rgb = Sample(Bake.Transmittance, 256, 64, U, V);
            else
            {
                if (View == 1)
                {
                    const double Azimuth = U * Pi * 2, Elevation = (.5 - V) * Pi;
                    const auto   Coordinate = SkyCoordinate(
                        Bake, {std::cos(Elevation) * std::cos(Azimuth), std::sin(Elevation), std::cos(Elevation) * std::sin(Azimuth)});
                    U = Coordinate[0];
                    V = Coordinate[1];
                }
                else if (View == 3)
                {
                    const double Along = (Column + .5 - 160) / 72, Up = (80 - Row - .5) / 72;
                    const double Radius = Along * Along + Up * Up;
                    if (Radius > 1) continue;
                    const auto Coordinate = SkyCoordinate(Bake, {Along, Up, std::sqrt(1 - Radius)});
                    U                     = Coordinate[0];
                    V                     = Coordinate[1];
                }
                Rgb = Sample(Bake.SkyView, 192, 108, U, V);
            }
            const size_t Pixel = (Row * Width + Column) * 4;
            for (size_t Band = 0; Band < 3; ++Band)
                Pixels[Pixel + Band] = Encode(Rgb[Band], View == 2);
            Pixels[Pixel + 3] = 255;
        }
    return Pixels;
}

} // namespace Frontier
