//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/LevelSetSpace.h — Dedicated Fluid Domain Signed Distance Field Boundaries
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <vector>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    LEVEL SET SPACE
//------------------------------------------------------------------------------------------------------------------------

class LevelSetSpace
{
public:
    LevelSetSpace(uint32_t ExtentX, uint32_t ExtentY, uint32_t ExtentZ, float CellSize) noexcept;
    ~LevelSetSpace() noexcept = default;

    LevelSetSpace(const LevelSetSpace&) = delete;
    LevelSetSpace& operator=(const LevelSetSpace&) = delete;

    void                    InitializeSphere(const Vector3& Center, float Radius) noexcept;
    void                    InitializeBox(const BoundingExtent& Box) noexcept;

    [[nodiscard]] float     SampleDistance(const Vector3& Position) const noexcept;
    [[nodiscard]] Vector3   SampleGradient(const Vector3& Position) const noexcept;
    [[nodiscard]] bool      IsInsideSolid(const Vector3& Position) const noexcept;

    [[nodiscard]] uint32_t  QueryResolutionX() const noexcept { return ExtentX; }
    [[nodiscard]] uint32_t  QueryResolutionY() const noexcept { return ExtentY; }
    [[nodiscard]] uint32_t  QueryResolutionZ() const noexcept { return ExtentZ; }
    [[nodiscard]] float     QueryCellSize() const noexcept { return CellSize; }

    // Single unified conversion operator for volume cell count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    [[nodiscard]] size_t    EvaluateLinearIndex(uint32_t x, uint32_t y, uint32_t z) const noexcept;

    uint32_t                ExtentX;                            // [cells] resolution along X axis
    uint32_t                ExtentY;                            // [cells] resolution along Y axis
    uint32_t                ExtentZ;                            // [cells] resolution along Z axis
    float                   CellSize;                           // [m] width of each voxel cell
    std::vector<float>      DistanceField;                      // [m] scalar signed distance field phi(x)
};

template<>
inline size_t LevelSetSpace::Convert<size_t>() const noexcept
{
    return DistanceField.size();
}

} // namespace Frontier
