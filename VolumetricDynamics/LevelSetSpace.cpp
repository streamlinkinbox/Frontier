//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/LevelSetSpace.cpp — Fluid Domain Signed Distance Field Implementation
//============================================================================================================================================

#include "LevelSetSpace.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

LevelSetSpace::LevelSetSpace(uint32_t DesiredExtentX, uint32_t DesiredExtentY, uint32_t DesiredExtentZ, float DesiredCellSize) noexcept
    : ExtentX(DesiredExtentX)
    , ExtentY(DesiredExtentY)
    , ExtentZ(DesiredExtentZ)
    , CellSize(DesiredCellSize > 1e-4f ? DesiredCellSize : 0.1f)
    , DistanceField(DesiredExtentX * DesiredExtentY * DesiredExtentZ, 1000.0f)
{
}

size_t LevelSetSpace::EvaluateLinearIndex(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    return static_cast<size_t>(z * (ExtentX * ExtentY) + y * ExtentX + x);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FIELD INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

void LevelSetSpace::InitializeSphere(const Vector3& Center, float Radius) noexcept
{
    for (uint32_t z = 0; z < ExtentZ; ++z)
    {
        for (uint32_t y = 0; y < ExtentY; ++y)
        {
            for (uint32_t x = 0; x < ExtentX; ++x)
            {
                Vector3 WorldPos{
                    static_cast<float>(x) * CellSize,
                    static_cast<float>(y) * CellSize,
                    static_cast<float>(z) * CellSize
                };
                float Dist = (WorldPos - Center).Length() - Radius;
                DistanceField[EvaluateLinearIndex(x, y, z)] = Dist;
            }
        }
    }
}

void LevelSetSpace::InitializeBox(const BoundingExtent& Box) noexcept
{
    Vector3 Center = Box.QueryCenter();
    Vector3 Extents = Box.QueryExtents();

    for (uint32_t z = 0; z < ExtentZ; ++z)
    {
        for (uint32_t y = 0; y < ExtentY; ++y)
        {
            for (uint32_t x = 0; x < ExtentX; ++x)
            {
                Vector3 WorldPos{
                    static_cast<float>(x) * CellSize,
                    static_cast<float>(y) * CellSize,
                    static_cast<float>(z) * CellSize
                };
                Vector3 d{
                    std::abs(WorldPos.x - Center.x) - Extents.x,
                    std::abs(WorldPos.y - Center.y) - Extents.y,
                    std::abs(WorldPos.z - Center.z) - Extents.z
                };
                Vector3 maxD{ std::max(d.x, 0.0f), std::max(d.y, 0.0f), std::max(d.z, 0.0f) };
                float Dist = maxD.Length() + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
                DistanceField[EvaluateLinearIndex(x, y, z)] = Dist;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SAMPLING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

float LevelSetSpace::SampleDistance(const Vector3& Position) const noexcept
{
    float fx = Position.x / CellSize;
    float fy = Position.y / CellSize;
    float fz = Position.z / CellSize;

    int32_t ix = std::clamp(static_cast<int32_t>(fx), 0, static_cast<int32_t>(ExtentX - 1));
    int32_t iy = std::clamp(static_cast<int32_t>(fy), 0, static_cast<int32_t>(ExtentY - 1));
    int32_t iz = std::clamp(static_cast<int32_t>(fz), 0, static_cast<int32_t>(ExtentZ - 1));

    return DistanceField[EvaluateLinearIndex(ix, iy, iz)];
}

Vector3 LevelSetSpace::SampleGradient(const Vector3& Position) const noexcept
{
    float h = CellSize;
    float dx = SampleDistance(Position + Vector3{ h, 0.0f, 0.0f }) - SampleDistance(Position - Vector3{ h, 0.0f, 0.0f });
    float dy = SampleDistance(Position + Vector3{ 0.0f, h, 0.0f }) - SampleDistance(Position - Vector3{ 0.0f, h, 0.0f });
    float dz = SampleDistance(Position + Vector3{ 0.0f, 0.0f, h }) - SampleDistance(Position - Vector3{ 0.0f, 0.0f, h });

    Vector3 Grad{ dx / (2.0f * h), dy / (2.0f * h), dz / (2.0f * h) };
    return Grad.Normalized();
}

bool LevelSetSpace::IsInsideSolid(const Vector3& Position) const noexcept
{
    return SampleDistance(Position) <= 0.0f;
}

} // namespace Frontier
