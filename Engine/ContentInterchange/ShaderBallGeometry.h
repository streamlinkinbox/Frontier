//============================================================================================================================================
//                                                     SHADERBALLGEOMETRY.H
//============================================================================================================================================
// 📦 Loads the ShaderBall mesh asset into a GeometryStructure the scene can register as shared topology.

/// The material-evaluation geometry: derkreature's ShaderBall (Mat Makin, Unlicense), converted to the engine's
///    axes and units by Exhibits/Workbench/ShaderBallAsset/ConvertShaderBall.py.
///
///    Why a mesh and not a sphere. A sphere shows a material at one curvature with no occlusion and no edges, so
///    sheen, coat, anisotropy and subsurface all read as a single highlight on a single gradient. The shader ball
///    carries a flat plate, a convex dome, a concave sweep, a thin lip and a self-occluding cushion, which is what
///    makes a coat distinguishable from a polish and a sheen distinguishable from a wide specular.
///
///    Full resolution is 67 832 triangles, and that is what every consumer gets. The showcase places it 400 times
///    through SceneStructure's shared topology, so it is resident ONCE (35 897 vertices, ≈ 1.9 MB) however many
///    placements there are.
///
///    ⚠️ No decimated variant. A proof that three render paths agree about a material is void if the paths are
///    not looking at the same triangles, so the CPU parity harness traces this exact mesh and renders FEWER
///    balls rather than smaller ones — 20 at 67 832 triangles is 1.4 M, which a CPU BVH handles comfortably;
///    only the full 400-ball grid at 27 M was ever the problem. The converter still has a --decimate flag for
///    ad-hoc inspection, and it tears this mesh: see its docstring before trusting anything it emits.
///
/// use : ShaderBallGeometry::Load(path, mesh, &error) → SceneStructure::RegisterTopology(mesh)
/// cost: ~1.9 MB resident, one file read; the caller registers the topology once and places it many times
/// tag : shaderball, geometry, shared-topology

#pragma once

#include "GeometricRaster/GeometryStructure.h"

#include <cstdint>
#include <string>

namespace Frontier {

// Where the converter writes, relative to the repository root.
inline constexpr const char* kShaderBallAssetPath = "Exhibits/Assets/ShaderBall/ShaderBall.mesh";

// The seated height the converter targets, in metres. Published so the grid can size its spacing against the
//    mesh rather than against a number repeated in two files.
inline constexpr float kShaderBallHeight = 1.10f;

class ShaderBallGeometry
{
public:
    // Fills Mesh from the binary at Path. Object space, Z-up, metres, resting on z = 0 and centred in xy.
    //    Answers false with a reason in Error when the file is missing, truncated or not this format.
    [[nodiscard]] static bool Load(const std::string& Path, GeometryStructure& Mesh, std::string* Error) noexcept;

    // Tries Path, then the same name under each of a few likely roots, so a harness run from a build directory
    //    finds the asset without every caller hand-rolling the search. Reports the path it used in Resolved.
    [[nodiscard]] static bool LoadResolved(const std::string& Path, GeometryStructure& Mesh,
                                           std::string* Resolved, std::string* Error) noexcept;
};

} // namespace Frontier
