/*==========================================================================================================================================
                                                             MATCAPFIXTURES.H
==========================================================================================================================================*/
// 🧩 Deterministic lookup images and analytical normal-calibration geometry; no CAD import, project preset or production asset generator.

#pragma once
#include "Engine/Render/MatCapProjection.h"
#include "BracketFixture.h"
#include <numbers>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     DISPLAY-REFERRED LOOKUP TEXTURES
//------------------------------------------------------------------------------------------------------------------------

inline MatCapImage ConstructMatCapGradient()
{
    MatCapImage Image{64,64,{}};
    Image.Rgba.resize(64*64*4);
    for (uint32_t Row = 0; Row < 64; ++Row)
        for (uint32_t Column = 0; Column < 64; ++Column)
        {
            const size_t Offset = (Row*64+Column)*4;
            Image.Rgba[Offset]   = uint8_t(std::lround(255.0*Column/63));
            Image.Rgba[Offset+1] = uint8_t(std::lround(255.0*Row/63));
            Image.Rgba[Offset+2] = 47;
            Image.Rgba[Offset+3] = 0;
        }
    return Image;
}

inline MatCapImage ConstructCopperMatCap()
{
    MatCapImage Image{256,256,{}};
    Image.Rgba.resize(256*256*4);
    for (uint32_t Row = 0; Row < Image.Height; ++Row)
        for (uint32_t Column = 0; Column < Image.Width; ++Column)
        {
            double Horizontal = (Column+0.5)*2/Image.Width-1;
            double Vertical   = 1-(Row+0.5)*2/Image.Height;
            const double RadiusSquared = Horizontal*Horizontal+Vertical*Vertical;
            if (RadiusSquared > 1)
            {
                Horizontal /= std::sqrt(RadiusSquared);
                Vertical   /= std::sqrt(RadiusSquared);
            }
            const double Forward = std::sqrt(std::max(0.0,1-Horizontal*Horizontal-Vertical*Vertical));
            const double Key = std::max(0.0,-0.45*Horizontal+0.6*Vertical+0.66*Forward);
            const double Highlight = std::pow(std::max(0.0,-0.27*Horizontal+0.36*Vertical+0.893*Forward),72);
            const double Rim = std::pow(1-Forward,3);
            const std::array<double,3> Colour{0.12+0.62*Key+0.9*Highlight+0.2*Rim,
                                              0.055+0.31*Key+0.8*Highlight+0.14*Rim,
                                              0.035+0.13*Key+0.7*Highlight+0.12*Rim};
            const size_t Offset = (Row*Image.Width+Column)*4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Image.Rgba[Offset+Channel] = uint8_t(std::lround(std::clamp(Colour[Channel],0.0,1.0)*255));
            Image.Rgba[Offset+3] = 255;
        }
    return Image;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ANALYTICAL NORMAL COVERAGE
//------------------------------------------------------------------------------------------------------------------------

inline SurfaceGeometry ConstructMatCapPlane(std::array<float,3> Normal = {0,0,1})
{
    SurfaceGeometry Geometry;
    for (const auto Position : {std::array<float,3>{-.8f,-.8f,.4f}, {.8f,-.8f,.4f}, {.8f,.8f,.4f}, {-.8f,.8f,.4f}})
        Geometry.Corners.push_back({Position,Normal});
    Geometry.TriangleCorners = {0,1,2,0,2,3};
    return Geometry;
}

inline SurfaceGeometry ConstructNormalCalibrationSphere()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Slices = 96, Rings = 48;
    for (uint32_t Ring = 0; Ring <= Rings; ++Ring)
        for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
        {
            const double θ = std::numbers::pi * Ring / Rings;
            const double φ = 2 * std::numbers::pi * Slice / Slices;
            const std::array<float,3> Normal{float(std::sin(θ)*std::cos(φ)),float(std::sin(θ)*std::sin(φ)),float(std::cos(θ))};
            Geometry.Corners.push_back({{Normal[0]*.85f,Normal[1]*.85f,Normal[2]*.85f},Normal});
        }
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Ring*(Slices+1)+Slice;
            for (const auto Index : {First,First+1,First+Slices+1,First+1,First+Slices+2,First+Slices+1})
                Geometry.TriangleCorners.push_back(Index);
        }
    return Geometry;
}

inline std::array<double,16> DeriveBracketNormalView(uint32_t Width, uint32_t Height)
{
    const auto Projection = ProjectBracketFixture(Width,Height);
    std::array<double,16> View{};
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        View[Axis*4]   = Projection.ClipFromSurface[Axis*4] * double(Width)/Height*2.25;
        View[Axis*4+1] = -Projection.ClipFromSurface[Axis*4+1]*2.25;
        View[Axis*4+2] = -Projection.ClipFromSurface[Axis*4+2]*49.9;
    }
    View[15] = 1;
    return View;
}
} // namespace Frontier
