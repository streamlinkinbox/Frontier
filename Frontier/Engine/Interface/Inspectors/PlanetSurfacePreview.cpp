/*==========================================================================================================================================
                                                          PLANETSURFACEPREVIEW.CPP
==========================================================================================================================================*/
// 🧩 Uses native planetary radiance evaluation and the viewport's authoring conversion; disc scale is compressed for inspection.

#include "Engine/Interface/Inspectors/PlanetSurfacePreview.h"
#include "Engine/PlanetaryAtmosphere/CelestialAuthoring.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

std::vector<uint8_t> GeneratePlanetSurfacePreview(const PlanetConfiguration &Planet)
{
    constexpr int Width = 320, Height = 240;
    auto          Body                   = ResolveAuthoredPlanet(Planet, {0, 0, 1});
    const float   Diameter               = std::clamp(Planet.ApparentDiameterDegrees, .05f, 30.0f);
    Body.AngularRadiusRad                = .075f + .045f * std::sqrt(Diameter / (Diameter + 6));
    const double            Reach        = Body.Rings.Enabled ? Body.Rings.OuterRadiusRatio : 1.4;
    const double            VerticalSpan = .13 * std::max(1.4, Reach);
    PlanetaryDomeProjection Projection;
    std::vector<uint8_t>    Pixels(Width * Height * 4, 0);
    for (int Row = 0; Row < Height; ++Row)
        for (int Column = 0; Column < Width; ++Column)
        {
            const float  Along    = float((Column + .5 - Width * .5) / (Height * .5) * VerticalSpan);
            const float  Up       = float((Height * .5 - Row - .5) / (Height * .5) * VerticalSpan);
            const float  Length   = std::sqrt(Along * Along + Up * Up + 1);
            float        Coverage = 0;
            const auto   Radiance = Projection.EvaluatePlanetaryBody(Body, {Along / Length, Up / Length, 1 / Length}, {-.35f, .4f, -.846f},
                                                                     {3.2f, 3.1f, 3.0f}, float(VerticalSpan * 2 / Height), Coverage);
            const float  Channels[] = {Radiance.X, Radiance.Y, Radiance.Z};
            const size_t Pixel      = (Row * Width + Column) * 4;
            for (int Band = 0; Band < 3; ++Band)
            {
                const double Light   = std::isfinite(Channels[Band]) ? std::max(0.0f, Channels[Band]) : 0;
                Pixels[Pixel + Band] = uint8_t(std::lround(255 * std::pow(Light / (1 + Light), 1 / 2.2)));
            }
            Pixels[Pixel + 3] = (Coverage > .001f || Radiance.X + Radiance.Y + Radiance.Z > .00001f) ? 255 : 0;
        }
    return Pixels;
}

} // namespace Frontier
