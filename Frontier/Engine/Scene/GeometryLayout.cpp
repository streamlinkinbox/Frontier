/*==========================================================================================================================================
                                                            GEOMETRYLAYOUT.CPP
==========================================================================================================================================*/
// 🧩 The scene geometry's placement algebra: affine transforms as column-major 4 × 4 matrices, the object transform of a
//    SpatialConfiguration, and the modifier stack (Blender's Array modifiers) evaluated into the copies of one object.

#include "Engine/Scene/GeometryLayout.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;

double& At(PlacementMatrix& Matrix, int Row, int Column) noexcept
{
    return Matrix[static_cast<size_t>(Column * 4 + Row)];
}

double At(const PlacementMatrix& Matrix, int Row, int Column) noexcept
{
    return Matrix[static_cast<size_t>(Column * 4 + Row)];
}

PlacementMatrix ConstructZTurn(double Degrees) noexcept
{
    PlacementMatrix Turn = ConstructIdentityPlacement();
    const double    θ    = Degrees * DegreesToRadians;
    At(Turn, 0, 0) = std::cos(θ);
    At(Turn, 0, 1) = -std::sin(θ);
    At(Turn, 1, 0) = std::sin(θ);
    At(Turn, 1, 1) = std::cos(θ);
    return Turn;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENT ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

PlacementMatrix ConstructIdentityPlacement() noexcept
{
    PlacementMatrix Identity{};
    Identity[0] = Identity[5] = Identity[10] = Identity[15] = 1.0;
    return Identity;
}

PlacementMatrix ConstructPlacement(const std::array<double, 3>& Position, const std::array<double, 3>& RotationDegrees,
                                   const std::array<double, 3>& Scale) noexcept
{
    const double α = RotationDegrees[0] * DegreesToRadians, β = RotationDegrees[1] * DegreesToRadians, γ = RotationDegrees[2] * DegreesToRadians;
    const double Cosα = std::cos(α), Sinα = std::sin(α), Cosβ = std::cos(β), Sinβ = std::sin(β), Cosγ = std::cos(γ), Sinγ = std::sin(γ);
    // 📝 R = Rz(γ) · Ry(β) · Rx(α), each column then scaled.
    const double Rotation[3][3] = {{Cosβ * Cosγ, Cosγ * Sinα * Sinβ - Cosα * Sinγ, Cosα * Cosγ * Sinβ + Sinα * Sinγ},
                                   {Cosβ * Sinγ, Cosα * Cosγ + Sinα * Sinβ * Sinγ, Cosα * Sinβ * Sinγ - Cosγ * Sinα},
                                   {-Sinβ, Cosβ * Sinα, Cosα * Cosβ}};
    PlacementMatrix Matrix = ConstructIdentityPlacement();
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column) At(Matrix, Row, Column) = Rotation[Row][Column] * Scale[static_cast<size_t>(Column)];
        At(Matrix, Row, 3) = Position[static_cast<size_t>(Row)];
    }
    return Matrix;
}

PlacementMatrix ResolveSpatialPlacement(const SpatialConfiguration& Spatial) noexcept
{
    return ConstructPlacement({Spatial.PositionMetres[0], Spatial.PositionMetres[1], Spatial.PositionMetres[2]},
                              {Spatial.OrientationDegrees[0], Spatial.OrientationDegrees[1], Spatial.OrientationDegrees[2]},
                              {Spatial.Scale[0], Spatial.Scale[1], Spatial.Scale[2]});
}

PlacementMatrix MultiplyPlacement(const PlacementMatrix& Left, const PlacementMatrix& Right) noexcept
{
    PlacementMatrix Product{};
    for (int Column = 0; Column < 4; ++Column)
    {
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0.0;
            for (int Inner = 0; Inner < 4; ++Inner) Sum += At(Left, Row, Inner) * At(Right, Inner, Column);
            At(Product, Row, Column) = Sum;
        }
    }
    return Product;
}

PlacementMatrix InvertPlacement(const PlacementMatrix& Matrix) noexcept
{
    // 📝 Affine: the 3 × 3 block (entries Mrc, row r, column c) by its adjugate over the determinant, then the translation
    //    carried through it.
    const double M00 = At(Matrix, 0, 0), M01 = At(Matrix, 0, 1), M02 = At(Matrix, 0, 2);
    const double M10 = At(Matrix, 1, 0), M11 = At(Matrix, 1, 1), M12 = At(Matrix, 1, 2);
    const double M20 = At(Matrix, 2, 0), M21 = At(Matrix, 2, 1), M22 = At(Matrix, 2, 2);
    const double Determinant = M00 * (M11 * M22 - M12 * M21) - M01 * (M10 * M22 - M12 * M20) + M02 * (M10 * M21 - M11 * M20);
    const double Reciprocal  = std::fabs(Determinant) > 1.0e-300 ? 1.0 / Determinant : 0.0;
    PlacementMatrix Inverse = ConstructIdentityPlacement();
    At(Inverse, 0, 0) = (M11 * M22 - M12 * M21) * Reciprocal;
    At(Inverse, 0, 1) = (M02 * M21 - M01 * M22) * Reciprocal;
    At(Inverse, 0, 2) = (M01 * M12 - M02 * M11) * Reciprocal;
    At(Inverse, 1, 0) = (M12 * M20 - M10 * M22) * Reciprocal;
    At(Inverse, 1, 1) = (M00 * M22 - M02 * M20) * Reciprocal;
    At(Inverse, 1, 2) = (M02 * M10 - M00 * M12) * Reciprocal;
    At(Inverse, 2, 0) = (M10 * M21 - M11 * M20) * Reciprocal;
    At(Inverse, 2, 1) = (M01 * M20 - M00 * M21) * Reciprocal;
    At(Inverse, 2, 2) = (M00 * M11 - M01 * M10) * Reciprocal;
    for (int Row = 0; Row < 3; ++Row)
    {
        double Sum = 0.0;
        for (int Inner = 0; Inner < 3; ++Inner) Sum += At(Inverse, Row, Inner) * At(Matrix, Inner, 3);
        At(Inverse, Row, 3) = -Sum;
    }
    return Inverse;
}

void DecomposePlacement(const PlacementMatrix& Matrix, std::array<double, 3>& Position, std::array<double, 3>& RotationDegrees,
                        std::array<double, 3>& Scale) noexcept
{
    double Rotation[3][3];
    for (int Column = 0; Column < 3; ++Column)
    {
        const double Length = std::sqrt(At(Matrix, 0, Column) * At(Matrix, 0, Column) + At(Matrix, 1, Column) * At(Matrix, 1, Column) +
                                        At(Matrix, 2, Column) * At(Matrix, 2, Column));
        Scale[static_cast<size_t>(Column)] = Length;
        for (int Row = 0; Row < 3; ++Row) Rotation[Row][Column] = Length > 0.0 ? At(Matrix, Row, Column) / Length : 0.0;
    }
    const double Determinant = Rotation[0][0] * (Rotation[1][1] * Rotation[2][2] - Rotation[1][2] * Rotation[2][1]) -
                               Rotation[0][1] * (Rotation[1][0] * Rotation[2][2] - Rotation[1][2] * Rotation[2][0]) +
                               Rotation[0][2] * (Rotation[1][0] * Rotation[2][1] - Rotation[1][1] * Rotation[2][0]);
    if (Determinant < 0.0)
    {
        Scale[0] = -Scale[0];
        for (auto& Row : Rotation) Row[0] = -Row[0];
    }
    // 📝 R = Rz(γ) · Ry(β) · Rx(α): R20 = −sin β; at the gimbal (|sin β| = 1) α is 0 and γ comes from R01 and R11.
    const double Sinβ = std::clamp(-Rotation[2][0], -1.0, 1.0);
    double       α = 0.0, β = std::asin(Sinβ), γ = 0.0;
    if (std::fabs(Sinβ) < 0.9999999)
    {
        α = std::atan2(Rotation[2][1], Rotation[2][2]);
        γ = std::atan2(Rotation[1][0], Rotation[0][0]);
    }
    else
    {
        γ = std::atan2(-Rotation[0][1], Rotation[1][1]);
    }
    RotationDegrees = {α / DegreesToRadians, β / DegreesToRadians, γ / DegreesToRadians};
    Position        = {At(Matrix, 0, 3), At(Matrix, 1, 3), At(Matrix, 2, 3)};
}

bool ConfirmProximity(const PlacementMatrix& Left, const PlacementMatrix& Right, double LinearTolerance, double MetreTolerance) noexcept
{
    for (int Column = 0; Column < 3; ++Column)
        for (int Row = 0; Row < 3; ++Row)
            if (std::fabs(At(Left, Row, Column) - At(Right, Row, Column)) > LinearTolerance) return false;
    for (int Row = 0; Row < 3; ++Row)
        if (std::fabs(At(Left, Row, 3) - At(Right, Row, 3)) > MetreTolerance) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER STACK
//------------------------------------------------------------------------------------------------------------------------

std::array<double, 3> ResolveArrayStep(const ArrayModifierConfiguration& Modifier, const std::array<double, 3>& Extent) noexcept
{
    std::array<double, 3> Step{};
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        if (Modifier.ConstantOffset) Step[Axis] += Modifier.ConstantMetres[Axis];
        if (Modifier.RelativeOffset) Step[Axis] += static_cast<double>(Modifier.RelativeFactor[Axis]) * Extent[Axis];
    }
    return Step;
}

size_t CountModifierCopies(size_t BaseCopies, const std::vector<ArrayModifierConfiguration>& Modifiers) noexcept
{
    size_t Copies = BaseCopies;
    for (const ArrayModifierConfiguration& Modifier : Modifiers)
    {
        if (!Modifier.Enabled) continue;
        Copies = std::min<size_t>(CopyCeiling, Copies * std::max<uint32_t>(1u, Modifier.Count));
    }
    return Copies;
}

std::vector<PlacementMatrix> EvaluateModifierStack(const std::vector<PlacementMatrix>& Bases, const std::vector<ArrayModifierConfiguration>& Modifiers,
                                                   const std::array<double, 3>& Extent)
{
    std::vector<PlacementMatrix> Copies(Bases.begin(), Bases.begin() + static_cast<std::ptrdiff_t>(std::min(Bases.size(), CopyCeiling)));
    for (const ArrayModifierConfiguration& Modifier : Modifiers)
    {
        if (!Modifier.Enabled || Copies.empty()) continue;
        const std::array<double, 3> Step  = ResolveArrayStep(Modifier, Extent);
        const uint32_t              Count = std::max<uint32_t>(1u, Modifier.Count);
        std::vector<PlacementMatrix> Next;
        Next.reserve(std::min<size_t>(CopyCeiling, Copies.size() * Count));
        for (uint32_t Copy = 0; Copy < Count && Next.size() < CopyCeiling; ++Copy)
        {
            // 📝 Radial: turned about the object's origin, then moved. In place: moved, and turned about the copy's own origin.
            const double          Ordinal = static_cast<double>(Copy);
            const double          Turn    = Ordinal * Modifier.RotationStepDegrees;
            const PlacementMatrix Shift   = ConstructPlacement({Ordinal * Step[0], Ordinal * Step[1], Ordinal * Step[2]},
                                                               {0.0, 0.0, Modifier.RadialPivot ? Turn : 0.0}, {1.0, 1.0, 1.0});
            const PlacementMatrix Local = ConstructZTurn(Modifier.RadialPivot ? 0.0 : Turn);
            for (const PlacementMatrix& Source : Copies)
            {
                if (Next.size() >= CopyCeiling) break;
                Next.push_back(MultiplyPlacement(MultiplyPlacement(Shift, Source), Local));
            }
        }
        Copies = std::move(Next);
    }
    return Copies;
}

} // namespace Frontier
