/*==========================================================================================================================================
                                                             BRACKETFIXTURE.H
==========================================================================================================================================*/
// 🧩 An authored two-block mounting bracket for raster evidence; never registered as a project preset or decoded CAD file.

#pragma once
#include "Engine/Render/SurfaceGeometry.h"
#include <algorithm>

namespace Frontier
{
//----------------------------------------------------------------------------------------------------------------------
//                                               AUTHORED BLOCK GEOMETRY
//----------------------------------------------------------------------------------------------------------------------

inline void ConstructBracketBlock(SurfaceGeometry &Geometry, std::array<float, 3> Minimum, std::array<float, 3> Maximum)
{
    const std::array<std::array<float, 3>, 8> Positions{{
        {Minimum[0], Minimum[1], Minimum[2]}, {Maximum[0], Minimum[1], Minimum[2]},
        {Maximum[0], Maximum[1], Minimum[2]}, {Minimum[0], Maximum[1], Minimum[2]},
        {Minimum[0], Minimum[1], Maximum[2]}, {Maximum[0], Minimum[1], Maximum[2]},
        {Maximum[0], Maximum[1], Maximum[2]}, {Minimum[0], Maximum[1], Maximum[2]}}};
    constexpr unsigned Faces[6][4] = {{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
    constexpr std::array<std::array<float, 3>, 6> Normals{{{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}}};
    for (size_t Face = 0; Face < 6; ++Face)
    {
        const auto First = uint32_t(Geometry.Corners.size());
        for (auto Corner : Faces[Face]) Geometry.Corners.push_back({Positions[Corner], Normals[Face]});
        for (auto Offset : {0u,1u,2u,0u,2u,3u}) Geometry.TriangleCorners.push_back(First + Offset);
    }
    constexpr unsigned Edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto &Edge : Edges) Geometry.Edges.push_back({Positions[Edge[0]], Positions[Edge[1]]});
}

inline SurfaceGeometry ConstructBracketFixture()
{
    SurfaceGeometry Geometry;
    ConstructBracketBlock(Geometry, {-1.3f,-0.9f,-0.18f}, {1.3f,0.9f,0.18f});
    ConstructBracketBlock(Geometry, {-1.3f,0.54f,0.18f}, {1.3f,0.9f,1.7f});
    return Geometry;
}

inline SurfaceProjection ProjectBracketFixture(uint32_t Width, uint32_t Height, bool Perspective = false)
{
    const auto Normalize = [](std::array<double, 3> Direction)
    {
        const double Length = std::sqrt(Direction[0]*Direction[0]+Direction[1]*Direction[1]+Direction[2]*Direction[2]);
        for (auto &Coordinate : Direction) Coordinate /= Length;
        return Direction;
    };
    const std::array<double, 3> Camera{4,-6,4};
    const auto Forward = Normalize({-4,6,-3.35});
    const auto Right = Normalize({Forward[1],-Forward[0],0});
    const std::array<double, 3> Up{Right[1]*Forward[2],-Right[0]*Forward[2],Right[0]*Forward[1]-Right[1]*Forward[0]};
    const auto Dot = [&](const auto &Direction) { return Direction[0]*Camera[0]+Direction[1]*Camera[1]+Direction[2]*Camera[2]; };
    SurfaceProjection Projection;
    auto &ProjectionCoefficients = Projection.ClipFromSurface;
    const double Horizontal = Perspective ? 2.1 * Height / Width : double(Height) / Width / 2.25;
    const double Vertical = Perspective ? 2.1 : 1.0 / 2.25;
    const double Depth = Perspective ? 50.0 / 49.9 : 1.0 / 49.9;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        ProjectionCoefficients[Axis*4] = float(Right[Axis]*Horizontal);
        ProjectionCoefficients[Axis*4+1] = float(-Up[Axis]*Vertical);
        ProjectionCoefficients[Axis*4+2] = float(Forward[Axis]*Depth);
        ProjectionCoefficients[Axis*4+3] = Perspective ? float(Forward[Axis]) : 0;
    }
    ProjectionCoefficients[12] = float(-Dot(Right)*Horizontal);
    ProjectionCoefficients[13] = float(Dot(Up)*Vertical);
    ProjectionCoefficients[14] = float(-Dot(Forward)*Depth - (Perspective ? 5.0 / 49.9 : 0.1 / 49.9));
    ProjectionCoefficients[15] = Perspective ? float(-Dot(Forward)) : 1;
    Projection.DisplayStroke = {float(Width),float(Height),1.25f,0};
    return Projection;
}
} // namespace Frontier
