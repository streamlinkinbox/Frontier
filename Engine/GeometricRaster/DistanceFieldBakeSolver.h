//============================================================================================================================================
//                                                  DISTANCEFIELDBAKESOLVER.H
//============================================================================================================================================
// 📦 Exact triangle mesh to 3D Signed Distance Field voxel generation solver with accelerated spatial binning.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "DistanceFieldSpace.h"
#include "GeometryStructure.h"
#include <string>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           DISTANCE FIELD BAKE SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct DistanceFieldBakeSpecification
{
    uint32_t                ResolutionX       = 64u;            // [-] grid voxel resolution x
    uint32_t                ResolutionY       = 64u;            // [-] grid voxel resolution y
    uint32_t                ResolutionZ       = 64u;            // [-] grid voxel resolution z
    float                   BoundaryPadding   = 0.08f;          // [m] spatial margin around mesh bounding box
    uint32_t                SpatialBinCount   = 32u;            // [-] uniform acceleration bin subdivision
};

//------------------------------------------------------------------------------------------------------------------------
//                                              DISTANCE FIELD BAKE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class DistanceFieldBakeSolver
{
public:
    // Solves the signed distance field for a triangle mesh
    [[nodiscard]] static bool Solve(const GeometryStructure& InputMesh,
                                    const DistanceFieldBakeSpecification& Specification,
                                    DistanceFieldSpace& OutDistanceField,
                                    std::string* OutError = nullptr) noexcept;

    // Bakes the high-poly ShaderBall mesh directly from asset path
    [[nodiscard]] static bool BakeShaderBall(const std::string& MeshAssetPath,
                                             const std::string& OutputSdfPath,
                                             const DistanceFieldBakeSpecification& Specification,
                                             std::string* OutError = nullptr) noexcept;
};

} // namespace Frontier
