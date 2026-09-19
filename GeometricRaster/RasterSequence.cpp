//============================================================================================================================================
// 📦 Frontier/GeometricRaster/RasterSequence.cpp — Tile Software Rasterizer Implementation
//============================================================================================================================================

#include "RasterSequence.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RasterSequence::RasterSequence(TaskScheduler* InitialScheduler) noexcept
    : Scheduler(InitialScheduler)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RASTERIZATION PIPELINE
//------------------------------------------------------------------------------------------------------------------------

void RasterSequence::RasterizeGeometry(const GeometryStructure& Geometry, uint32_t InstanceToken, const Matrix4x4& Transform, const Matrix4x4& ViewProj, VisibilityProjection& Target) noexcept
{
    (void)Transform;
    (void)ViewProj;
    const auto& Vertices = Geometry.QueryVertices();
    const auto& Indices = Geometry.QueryIndices();

    if (Indices.empty() || Vertices.empty())
    {
        return;
    }

    size_t TriangleCount = Indices.size() / 3;
    for (size_t t = 0; t < TriangleCount; ++t)
    {
        uint32_t i0 = Indices[t * 3 + 0];
        uint32_t i1 = Indices[t * 3 + 1];
        uint32_t i2 = Indices[t * 3 + 2];

        if (i0 < Vertices.size() && i1 < Vertices.size() && i2 < Vertices.size())
        {
            RasterizeTriangle(Vertices[i0].SpatialLocation, Vertices[i1].SpatialLocation, Vertices[i2].SpatialLocation, InstanceToken, static_cast<uint32_t>(t), Target);
        }
    }
}

void RasterSequence::RasterizeTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, uint32_t InstanceToken, uint32_t PrimitiveToken, VisibilityProjection& Target) noexcept
{
    float W = static_cast<float>(Target.QueryWidth());
    float H = static_cast<float>(Target.QueryHeight());

    auto ScreenProject = [W, H](const Vector3& v) -> Vector3
    {
        float sx = (v.x * 0.5f + 0.5f) * W;
        float sy = (1.0f - (v.y * 0.5f + 0.5f)) * H;
        return Vector3{ sx, sy, v.z };
    };

    Vector3 p0 = ScreenProject(v0);
    Vector3 p1 = ScreenProject(v1);
    Vector3 p2 = ScreenProject(v2);

    int32_t minX = std::max(0, static_cast<int32_t>(std::min({ p0.x, p1.x, p2.x })));
    int32_t maxX = std::min(static_cast<int32_t>(W - 1), static_cast<int32_t>(std::max({ p0.x, p1.x, p2.x })));
    int32_t minY = std::max(0, static_cast<int32_t>(std::min({ p0.y, p1.y, p2.y })));
    int32_t maxY = std::min(static_cast<int32_t>(H - 1), static_cast<int32_t>(std::max({ p0.y, p1.y, p2.y })));

    auto EdgeFunction = [](const Vector3& a, const Vector3& b, float x, float y) -> float
    {
        return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    };

    float Area = EdgeFunction(p0, p1, p2.x, p2.y);
    if (std::abs(Area) <= 1e-6f)
    {
        return;
    }

    VisibilityIdentifier Ident = VisibilityIdentifier::Pack(InstanceToken, PrimitiveToken);

    for (int32_t y = minY; y <= maxY; ++y)
    {
        for (int32_t x = minX; x <= maxX; ++x)
        {
            float fx = static_cast<float>(x) + 0.5f;
            float fy = static_cast<float>(y) + 0.5f;

            float w0 = EdgeFunction(p1, p2, fx, fy);
            float w1 = EdgeFunction(p2, p0, fx, fy);
            float w2 = EdgeFunction(p0, p1, fx, fy);

            if ((w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) || (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f))
            {
                float Depth = (p0.z + p1.z + p2.z) / 3.0f;
                Target.WritePixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), Depth, Ident);
            }
        }
    }
}

} // namespace Frontier
