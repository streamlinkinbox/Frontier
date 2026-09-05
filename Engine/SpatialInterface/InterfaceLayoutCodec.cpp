//============================================================================================================================================
//                                                   INTERFACELAYOUTCODEC.CPP
//============================================================================================================================================

#include "InterfaceLayoutCodec.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   PLACEMENT COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

WorldPlacement ComposePlacement(const PlanePlacement& Placement) noexcept
{
    const float Cz = std::cos(Placement.RotationZ), Sz = std::sin(Placement.RotationZ);
    const float Cx = std::cos(Placement.RotationX), Sx = std::sin(Placement.RotationX);
    const float Cy = std::cos(Placement.RotationY), Sy = std::sin(Placement.RotationY);
    const float K  = Placement.Scale;

    // R = Ry · Rx · Rz  (roll in the plane first, then pitch toward the eye, then yaw into place).
    const float R00 =  Cy * Cz + Sy * Sx * Sz;
    const float R01 = -Cy * Sz + Sy * Sx * Cz;
    const float R02 =  Sy * Cx;
    const float R10 =  Cx * Sz;
    const float R11 =  Cx * Cz;
    const float R12 = -Sx;
    const float R20 = -Sy * Cz + Cy * Sx * Sz;
    const float R21 =  Sy * Sz + Cy * Sx * Cz;
    const float R22 =  Cy * Cx;

    WorldPlacement Composed;
    Composed.Row[0][0] = R00 * K;  Composed.Row[0][1] = R01 * K;  Composed.Row[0][2] = R02 * K;  Composed.Row[0][3] = Placement.Origin.X;
    Composed.Row[1][0] = R10 * K;  Composed.Row[1][1] = R11 * K;  Composed.Row[1][2] = R12 * K;  Composed.Row[1][3] = Placement.Origin.Y;
    Composed.Row[2][0] = R20 * K;  Composed.Row[2][1] = R21 * K;  Composed.Row[2][2] = R22 * K;  Composed.Row[2][3] = Placement.Origin.Z;
    return Composed;
}

WorldPlacement CombinePlacement(const WorldPlacement& Ancestor, const WorldPlacement& Local) noexcept
{
    WorldPlacement Combined;
    for (int R = 0; R < 3; ++R)
    {
        for (int C = 0; C < 3; ++C)
        {
            Combined.Row[R][C] = Ancestor.Row[R][0] * Local.Row[0][C]
                               + Ancestor.Row[R][1] * Local.Row[1][C]
                               + Ancestor.Row[R][2] * Local.Row[2][C];
        }
        Combined.Row[R][3] = Ancestor.Row[R][0] * Local.Row[0][3]
                           + Ancestor.Row[R][1] * Local.Row[1][3]
                           + Ancestor.Row[R][2] * Local.Row[2][3]
                           + Ancestor.Row[R][3];
    }
    return Combined;
}

void TransformPlanePoint(const WorldPlacement& Placement, float LocalX, float LocalY, float LocalZ,
                         float& WorldX, float& WorldY, float& WorldZ) noexcept
{
    WorldX = Placement.Row[0][0] * LocalX + Placement.Row[0][1] * LocalY + Placement.Row[0][2] * LocalZ + Placement.Row[0][3];
    WorldY = Placement.Row[1][0] * LocalX + Placement.Row[1][1] * LocalY + Placement.Row[1][2] * LocalZ + Placement.Row[1][3];
    WorldZ = Placement.Row[2][0] * LocalX + Placement.Row[2][1] * LocalY + Placement.Row[2][2] * LocalZ + Placement.Row[2][3];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CATEGORY / PALETTE PACKING
//------------------------------------------------------------------------------------------------------------------------

uint32_t InterfaceLayoutCodec::ComposeCategoryPalette(InterfaceCategory Category, PaletteSlot Palette) noexcept
{
    return (static_cast<uint32_t>(Category) << 24) | (static_cast<uint32_t>(Palette) & 0x00FFFFFFu);
}

InterfaceCategory InterfaceLayoutCodec::ExtractCategory(uint32_t CategoryPalette) noexcept
{
    const uint32_t Ordinal = CategoryPalette >> 24;
    return Ordinal < static_cast<uint32_t>(InterfaceCategory::Count) ? static_cast<InterfaceCategory>(Ordinal)
                                                                     : InterfaceCategory::Surface;
}

PaletteSlot InterfaceLayoutCodec::ExtractPalette(uint32_t CategoryPalette) noexcept
{
    const uint32_t Ordinal = CategoryPalette & 0x00FFFFFFu;
    return Ordinal < static_cast<uint32_t>(PaletteSlot::Count) ? static_cast<PaletteSlot>(Ordinal)
                                                               : PaletteSlot::Surface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENCODE
//------------------------------------------------------------------------------------------------------------------------

void InterfaceLayoutCodec::Encode(const InterfaceFigure& Figure, const WorldPlacement& Placement,
                                  const PaletteConfiguration& Palette, InterfaceInstanceFigure& Slot) noexcept
{
    Slot.RowXx = Placement.Row[0][0];  Slot.RowXy = Placement.Row[0][1];  Slot.RowXz = Placement.Row[0][2];  Slot.RowXw = Placement.Row[0][3];
    Slot.RowYx = Placement.Row[1][0];  Slot.RowYy = Placement.Row[1][1];  Slot.RowYz = Placement.Row[1][2];  Slot.RowYw = Placement.Row[1][3];
    Slot.RowZx = Placement.Row[2][0];  Slot.RowZy = Placement.Row[2][1];  Slot.RowZz = Placement.Row[2][2];  Slot.RowZw = Placement.Row[2][3];

    Slot.HalfWidth    = Figure.HalfWidth;
    Slot.HalfHeight   = Figure.HalfHeight;
    Slot.CornerRadius = Figure.CornerRadius < 0.0f ? Palette.QueryTokenRadius() : Figure.CornerRadius;

    // A corner radius can never exceed the shorter half extent, or the rounded-rectangle distance folds inside out.
    const float RadiusLimit = std::min(Figure.HalfWidth, Figure.HalfHeight);
    Slot.CornerRadius = std::clamp(Slot.CornerRadius, 0.0f, RadiusLimit);

    Slot.Opacity      = std::clamp(Figure.Opacity, 0.0f, 1.0f) * Palette.QueryGroupOpacity();

    Slot.ClipMinimumX = Figure.ClipMinimumX;
    Slot.ClipMinimumY = Figure.ClipMinimumY;
    Slot.ClipMaximumX = Figure.ClipMaximumX;
    Slot.ClipMaximumY = Figure.ClipMaximumY;

    Slot.CategoryPalette = ComposeCategoryPalette(Figure.Category, Figure.Palette);
    Slot.ScalarAlpha     = Figure.ScalarAlpha;
    Slot.ScalarBeta      = Figure.ScalarBeta;
    Slot.Tint            = Figure.TintOverride != 0u ? Figure.TintOverride : Palette.QueryPacked(Figure.Palette);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DECODE
//------------------------------------------------------------------------------------------------------------------------

void InterfaceLayoutCodec::Decode(const InterfaceInstanceFigure& Slot, InterfaceFigure& Figure, WorldPlacement& Placement) noexcept
{
    Placement.Row[0][0] = Slot.RowXx;  Placement.Row[0][1] = Slot.RowXy;  Placement.Row[0][2] = Slot.RowXz;  Placement.Row[0][3] = Slot.RowXw;
    Placement.Row[1][0] = Slot.RowYx;  Placement.Row[1][1] = Slot.RowYy;  Placement.Row[1][2] = Slot.RowYz;  Placement.Row[1][3] = Slot.RowYw;
    Placement.Row[2][0] = Slot.RowZx;  Placement.Row[2][1] = Slot.RowZy;  Placement.Row[2][2] = Slot.RowZz;  Placement.Row[2][3] = Slot.RowZw;

    Figure.HalfWidth    = Slot.HalfWidth;
    Figure.HalfHeight   = Slot.HalfHeight;
    Figure.CornerRadius = Slot.CornerRadius;
    Figure.Opacity      = Slot.Opacity;

    Figure.ClipMinimumX = Slot.ClipMinimumX;
    Figure.ClipMinimumY = Slot.ClipMinimumY;
    Figure.ClipMaximumX = Slot.ClipMaximumX;
    Figure.ClipMaximumY = Slot.ClipMaximumY;

    Figure.Category     = ExtractCategory(Slot.CategoryPalette);
    Figure.Palette      = ExtractPalette (Slot.CategoryPalette);
    Figure.ScalarAlpha  = Slot.ScalarAlpha;
    Figure.ScalarBeta   = Slot.ScalarBeta;
    Figure.TintOverride = Slot.Tint;
}

} // namespace Frontier
