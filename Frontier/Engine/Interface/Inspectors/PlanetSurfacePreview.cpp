/*==========================================================================================================================================
                                                          PLANETSURFACEPREVIEW.CPP
==========================================================================================================================================*/
// 🧩 Pure CPU preview thumbnail generator for PlanetInspector.

#include "Engine/Interface/Inspectors/PlanetSurfacePreview.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

std::vector<uint8_t> GeneratePlanetSurfacePreview(const PlanetConfiguration &Planet)
{
    constexpr int        Width = 320, Height = 240;
    std::vector<uint8_t> Pixels(static_cast<size_t>(Width * Height * 4), 0u);
    const float          Radius = 86.0f;
    const float          Cx = Width * 0.5f, Cy = Height * 0.5f;
    for (int Row = 0; Row < Height; ++Row)
    {
        for (int Column = 0; Column < Width; ++Column)
        {
            const float Dx = (static_cast<float>(Column) + 0.5f - Cx) / Radius;
            const float Dy = (Cy - static_cast<float>(Row) - 0.5f) / Radius;
            const float R2 = Dx * Dx + Dy * Dy;
            if (R2 <= 1.0f)
            {
                const float Dz   = std::sqrt(std::max(0.0f, 1.0f - R2));
                const float Ndl  = std::max(0.04f, -0.35f * Dx + 0.40f * Dy + 0.846f * Dz);
                const float Band = 0.85f + 0.15f * std::sin(Dy * 11.0f * std::max(0.2f, Planet.BandContrast));
                const float Rim  = std::pow(1.0f - Dz, 2.4f) * std::clamp(Planet.LimbGlow, 0.0f, 2.0f) * 0.35f;
                const size_t Off = static_cast<size_t>((Row * Width + Column) * 4);
                Pixels[Off + 0]  = static_cast<uint8_t>(std::clamp((Planet.SurfaceTint[0] * Ndl * Band + Rim * 0.45f) * 255.0f, 0.0f, 255.0f));
                Pixels[Off + 1]  = static_cast<uint8_t>(std::clamp((Planet.SurfaceTint[1] * Ndl * Band + Rim * 0.65f) * 255.0f, 0.0f, 255.0f));
                Pixels[Off + 2]  = static_cast<uint8_t>(std::clamp((Planet.SurfaceTint[2] * Ndl * Band + Rim * 0.95f) * 255.0f, 0.0f, 255.0f));
                Pixels[Off + 3]  = 255u;
            }
        }
    }
    return Pixels;
}

} // namespace Frontier
