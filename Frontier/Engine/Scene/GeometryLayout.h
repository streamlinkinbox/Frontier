/*==========================================================================================================================================
                                                             GEOMETRYLAYOUT.H
==========================================================================================================================================*/
// 🧩 The scene geometry's placement algebra: affine transforms as column-major 4 × 4 matrices, the object transform of a
//    SpatialConfiguration, and the modifier stack (Blender's Array modifiers) evaluated into the copies of one object.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <optional>
#include <cstddef>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Column-major, element [Column * 4 + Row], metres: the illumination world's convention (z up).
using PlacementMatrix = std::array<double, 16>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENT ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] PlacementMatrix ConstructIdentityPlacement() noexcept;

// 📝 Translation · Rz · Ry · Rx · Scale: Blender's XYZ Euler order, angles in degrees.
[[nodiscard]] PlacementMatrix ConstructPlacement(const std::array<double, 3>& Position,
                                                 const std::array<double, 3>& RotationDegrees,
                                                 const std::array<double, 3>& Scale) noexcept;

[[nodiscard]] PlacementMatrix ResolveSpatialPlacement(const SpatialConfiguration& Spatial) noexcept;
[[nodiscard]] PlacementMatrix MultiplyPlacement(const PlacementMatrix& Left, const PlacementMatrix& Right) noexcept;
[[nodiscard]] PlacementMatrix InvertPlacement(const PlacementMatrix& Matrix) noexcept;

// 📝 Position, XYZ Euler degrees and scale of an affine placement without shear (a negative determinant flips X).
void DecomposePlacement(const PlacementMatrix& Matrix,
                        std::array<double, 3>& Position,
                        std::array<double, 3>& RotationDegrees,
                        std::array<double, 3>& Scale) noexcept;

// 📝 True when every rotation-scale element differs by less than LinearTolerance and the translation by less than MetreTolerance.
[[nodiscard]] bool ConfirmProximity(const PlacementMatrix& Left, const PlacementMatrix& Right, double LinearTolerance,
                                    double MetreTolerance) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER STACK
//------------------------------------------------------------------------------------------------------------------------

constexpr size_t CopyCeiling = 4096;   // [-] copies one evaluation produces at most

// 📝 The step each copy of an Array modifier adds, in metres along the object's axes: the constant offset plus the relative
//    factors times one copy's extent.
[[nodiscard]] std::array<double, 3> ResolveArrayStep(const ArrayModifierConfiguration& Modifier, const std::array<double, 3>& Extent) noexcept;

// 📝 The copies the stack produces from BaseCopies without evaluating it (saturates at CopyCeiling).
[[nodiscard]] size_t CountModifierCopies(size_t BaseCopies, const std::vector<ArrayModifierConfiguration>& Modifiers) noexcept;

// 📝 The copies of an object in its own frame: the base copies, replicated by every enabled Array modifier in turn. The order
//    is step-major: all copies of the stack above at step 0, then all of them at step 1, and so on.
[[nodiscard]] std::vector<PlacementMatrix> EvaluateModifierStack(const std::vector<PlacementMatrix>&             Bases,
                                                                 const std::vector<ArrayModifierConfiguration>& Modifiers,
                                                                 const std::array<double, 3>&                    Extent);

} // namespace Frontier
