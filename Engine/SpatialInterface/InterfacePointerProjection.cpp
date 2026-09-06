//============================================================================================================================================
//                                                 INTERFACEPOINTERPROJECTION.CPP
//============================================================================================================================================

#include "InterfacePointerProjection.h"

#include <cmath>

namespace Frontier {

namespace {

// Signed distance to a rounded rectangle centred at the origin, in its own plane. Negative inside. This is the
//    same shape the fragment shader rasterises (Shaders/InterfaceSignedDistance.slang), so a pointer hit and a
//    visible pixel agree on where the figure's edge is — including its rounded corners, which a plain extent test
//    would wrongly claim.
[[nodiscard]] float RoundedRectangleDistance(float X, float Y, float HalfWidth, float HalfHeight, float Radius) noexcept
{
    const float Limit  = std::fmin(HalfWidth, HalfHeight);
    const float Corner = std::fmax(0.0f, std::fmin(Radius, Limit));
    const float Dx = std::fabs(X) - (HalfWidth  - Corner);
    const float Dy = std::fabs(Y) - (HalfHeight - Corner);
    const float Ox = std::fmax(Dx, 0.0f);
    const float Oy = std::fmax(Dy, 0.0f);
    const float Outside = std::sqrt(Ox * Ox + Oy * Oy);
    const float Inside  = std::fmin(std::fmax(Dx, Dy), 0.0f);
    return Outside + Inside - Corner;
}

} // namespace

PointerContact InterfacePointerProjection::Project(const InterfaceStructure& Structure,
                                                   const InterfaceSequence&  Composition,
                                                   const PointerRay&         Ray) noexcept
{
    PointerContact Nearest;
    Nearest.Distance = Ray.Reach;

    const uint32_t Count = Structure.QueryCount();
    for (uint32_t Ordinal = 0u; Ordinal < Count; ++Ordinal)
    {
        const InterfaceFigure& Figure = Structure.Query(Ordinal);
        if (!Figure.PointerTarget || !Figure.Visible) continue;

        const WorldPlacement& Placement = Composition.QueryPlacement(Ordinal);

        // Rows are [rotation·scale | translation]. Recover the scale from a row's length so the inverse rotation
        //    can be applied by transpose — cheap, and exact for the uniform-scale placements the layout produces.
        const float Sx = Placement.Row[0][0], Sy = Placement.Row[1][0], Sz = Placement.Row[2][0];
        const float ScaleSquared = Sx * Sx + Sy * Sy + Sz * Sz;
        if (ScaleSquared <= 1.0e-12f) continue;                 // degenerate placement: not pickable
        const float Scale = std::sqrt(ScaleSquared);
        const float Inverse = 1.0f / Scale;

        // Ray into the figure's local frame. Basis rows are the world axes of the figure's local X, Y, Z.
        const float Ox = Ray.OriginX - Placement.Row[0][3];
        const float Oy = Ray.OriginY - Placement.Row[1][3];
        const float Oz = Ray.OriginZ - Placement.Row[2][3];

        const float AxisXx = Placement.Row[0][0] * Inverse, AxisXy = Placement.Row[1][0] * Inverse, AxisXz = Placement.Row[2][0] * Inverse;
        const float AxisYx = Placement.Row[0][1] * Inverse, AxisYy = Placement.Row[1][1] * Inverse, AxisYz = Placement.Row[2][1] * Inverse;
        const float AxisZx = Placement.Row[0][2] * Inverse, AxisZy = Placement.Row[1][2] * Inverse, AxisZz = Placement.Row[2][2] * Inverse;

        const float LocalOriginZ    = Ox * AxisZx + Oy * AxisZy + Oz * AxisZz;
        const float LocalDirectionZ = Ray.DirectionX * AxisZx + Ray.DirectionY * AxisZy + Ray.DirectionZ * AxisZz;

        // Parallel to the plane: no crossing. Grazing rays are rejected rather than producing a hit at infinity.
        if (std::fabs(LocalDirectionZ) < 1.0e-6f) continue;

        const float Travel = -LocalOriginZ / LocalDirectionZ;
        if (Travel <= 0.0f || Travel >= Nearest.Distance) continue;   // behind the eye, or already beaten

        const float HitX = Ray.OriginX + Ray.DirectionX * Travel - Placement.Row[0][3];
        const float HitY = Ray.OriginY + Ray.DirectionY * Travel - Placement.Row[1][3];
        const float HitZ = Ray.OriginZ + Ray.DirectionZ * Travel - Placement.Row[2][3];

        // Project onto the local axes and undo the scale, so the result is in the figure's authored metres.
        const float LocalX = (HitX * AxisXx + HitY * AxisXy + HitZ * AxisXz) * Inverse;
        const float LocalY = (HitX * AxisYx + HitY * AxisYy + HitZ * AxisYz) * Inverse;

        const float Radius = Figure.CornerRadius < 0.0f ? 0.0f : Figure.CornerRadius;
        if (RoundedRectangleDistance(LocalX, LocalY, Figure.HalfWidth, Figure.HalfHeight, Radius) > 0.0f) continue;

        Nearest.Valid     = true;
        Nearest.Ordinal   = Ordinal;
        Nearest.Distance  = Travel;
        Nearest.LocalX    = LocalX;
        Nearest.LocalY    = LocalY;
        Nearest.FractionX = Figure.HalfWidth  > 0.0f ? LocalX / Figure.HalfWidth  : 0.0f;
        Nearest.FractionY = Figure.HalfHeight > 0.0f ? LocalY / Figure.HalfHeight : 0.0f;
    }

    if (!Nearest.Valid) Nearest.Distance = 0.0f;
    return Nearest;
}

PointerRay InterfacePointerProjection::ConstructViewportRay(float PixelX, float PixelY,
                                                            uint32_t Width, uint32_t Height,
                                                            const float EyeOrigin[3],
                                                            const float Forward[3],
                                                            const float Right[3],
                                                            const float Up[3],
                                                            float TanHalfFieldOfView,
                                                            float AspectRatio) noexcept
{
    PointerRay Ray;
    Ray.OriginX = EyeOrigin[0];
    Ray.OriginY = EyeOrigin[1];
    Ray.OriginZ = EyeOrigin[2];

    if (Width == 0u || Height == 0u) return Ray;

    // Pixel → normalised device coordinates. The Y flip is the same single flip GeometricRaster/ClipProjection.h
    //    applies for Vulkan (NDC Y points down), so a cursor at the top of the window looks upward in the world.
    const float U = (PixelX + 0.5f) / static_cast<float>(Width);
    const float V = (PixelY + 0.5f) / static_cast<float>(Height);
    const float ScreenX =  (2.0f * U - 1.0f) * TanHalfFieldOfView * AspectRatio;
    const float ScreenY = -(2.0f * V - 1.0f) * TanHalfFieldOfView;

    float Dx = Forward[0] + Right[0] * ScreenX + Up[0] * ScreenY;
    float Dy = Forward[1] + Right[1] * ScreenX + Up[1] * ScreenY;
    float Dz = Forward[2] + Right[2] * ScreenX + Up[2] * ScreenY;

    const float Length = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    if (Length > 1.0e-12f) { Dx /= Length; Dy /= Length; Dz /= Length; }

    Ray.DirectionX = Dx;
    Ray.DirectionY = Dy;
    Ray.DirectionZ = Dz;
    return Ray;
}

} // namespace Frontier
