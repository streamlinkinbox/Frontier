//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/Shaders/LatticeProjection.slang — Analytic finite Z-up ground lattice with one-pixel lines, LOD and world axes
//============================================================================================================================================
// A full-screen triangle. Each fragment reconstructs its view ray, intersects the z = 0 plane analytically, and shades
//    a one-pixel anti-aliased two-level lattice (minor / major) whose cell steps up as the camera pulls back so line density
//    stays roughly constant on screen. A square fade about the origin ends the pad instead of a hard horizon.
//    Colours: X axis red, Y axis green (Z-up world, Blender / Plasticity convention).
//
// 📝 LatticeShade() is the whole shader. Its inputs are plain values so the software raster calls the identical body:
//      Origin, Direction  : view ray for this pixel (world)
//      PixelAngle         : perspective — radians subtended by one pixel; orthographic — 0
//      PixelWorld         : orthographic — world units per pixel; perspective — 0
//    It returns straight rgb + coverage; the raster blends it over the clear colour and writes the depth it returns.

#include "ShadingRecords.slang"

struct LatticeSample
{
    float4 Colour;              // rgb straight, a = coverage
    float  Depth;               // clip depth of the hit, 1.0 when the ray misses the plane
};

// Coverage of the nearest line of spacing Cell along ONE world coordinate. Foot is the world length one pixel spans along
//    that coordinate on the ground, so Pixels is the true on-screen distance to the line: a line keeps one thin, crisp
//    pixel width at every distance and every viewing angle. Coverage ramps over a single pixel; there is no smoothstep
//    skirt to widen it. The line also thins out of existence once the cells packed into the pixels run too tight, rather
//    than aliasing into a grey wash.
float LatticeLineCoverage(float Coordinate, float Cell, float Foot, float HalfWidthPixels)
{
    float ToLine  = abs(fract(Coordinate / Cell + 0.5f) - 0.5f) * Cell;
    float Pixels  = ToLine / max(Foot, 1e-6f);
    float Line    = saturate(HalfWidthPixels + 0.5f - Pixels);
    float Spacing = Cell / max(Foot, 1e-6f);                         // pixels between neighbouring lines
    return Line * saturate((Spacing - 3.0f) / 3.0f);
}

// Coverage of a single axis line (distance to the line x = 0 or y = 0), same one-pixel ramp.
float AxisCoverage(float Distance, float Foot, float HalfWidthPixels)
{
    float Pixels = abs(Distance) / max(Foot, 1e-6f);
    return saturate(HalfWidthPixels + 0.5f - Pixels);
}

LatticeSample LatticeShade(ViewRecord View, float3 Origin, float3 Direction, float PixelAngle, float PixelWorld)
{
    LatticeSample Result;
    Result.Colour = float4(0.0f, 0.0f, 0.0f, 0.0f);
    Result.Depth  = 1.0f;

    if (abs(Direction.z) < 1e-7f) return Result;
    float T = -Origin.z / Direction.z;
    if (T <= 0.0f) return Result;
    float3 Hit = Origin + Direction * T;

    // One pixel on the ground is an ellipse: Lateral across the view direction, Radial along it (stretched by the
    //    grazing angle). Each line family is measured against the ellipse's extent along ITS OWN normal, so only the
    //    lines that really are foreshortened are treated as such; nothing is blown up wholesale by the angle.
    float Grazing = max(abs(Direction.z), 0.05f);
    float Lateral = PixelAngle > 0.0f ? T * PixelAngle : PixelWorld;
    float Radial  = Lateral / Grazing;
    float2 Out    = length(Direction.xy) > 1e-6f ? normalize(Direction.xy) : float2(1.0f, 0.0f);
    float FootX   = sqrt(Out.x * Out.x * Radial * Radial + Out.y * Out.y * Lateral * Lateral);
    float FootY   = sqrt(Out.y * Out.y * Radial * Radial + Out.x * Out.x * Lateral * Lateral);

    // LOD from the unstretched pixel, so the cell never jumps because the camera tilts: the minor cell is the smallest
    //    power of Ratio (times the base cell) that spans about 14 pixels, blended into the next level.
    float MinorCell  = View.LatticeStyle.x;
    float Ratio      = View.LatticeStyle.y;
    float LevelExact = max(0.0f, log(Lateral * 14.0f / MinorCell) / log(Ratio));
    float Level      = floor(LevelExact);
    float LevelMix   = LevelExact - Level;
    float CellA      = MinorCell * pow(Ratio, Level);
    float CellB      = CellA * Ratio;

    float HalfWidth = View.LatticeStyle.w;
    float MinorA = max(LatticeLineCoverage(Hit.x, CellA, FootX, HalfWidth), LatticeLineCoverage(Hit.y, CellA, FootY, HalfWidth)) * (1.0f - LevelMix);
    float MajorB = max(LatticeLineCoverage(Hit.x, CellB, FootX, HalfWidth), LatticeLineCoverage(Hit.y, CellB, FootY, HalfWidth));

    float3 MinorTint = float3(0.36f, 0.38f, 0.43f);
    float3 MajorTint = float3(0.50f, 0.53f, 0.60f);
    float  Coverage  = max(MinorA * 0.6f, MajorB * 0.9f);
    float3 Tint      = lerp(MinorTint, MajorTint, saturate(MajorB));

    // World axes on top: X red, Y green. Drawn only where the other coordinate is near zero; a touch bolder.
    float AxisX = AxisCoverage(Hit.y, FootY, HalfWidth + 0.15f);
    float AxisY = AxisCoverage(Hit.x, FootX, HalfWidth + 0.15f);
    Tint     = lerp(Tint, float3(0.86f, 0.24f, 0.24f), AxisX);
    Tint     = lerp(Tint, float3(0.30f, 0.78f, 0.30f), AxisY);
    Coverage = max(Coverage, max(AxisX, AxisY));

    // A finite pad, not an endless plane: square about the world origin, dissolving over its outer 40 percent.
    float Half    = View.LatticeStyle.z;
    float Reach   = max(abs(Hit.x), abs(Hit.y));
    float Fade    = 1.0f - smoothstep(Half * 0.6f, Half, Reach);
    float Horizon = smoothstep(0.0f, 0.06f, abs(Direction.z));
    Coverage     *= Fade * Horizon;

    float4 Clip   = mul(View.ViewClip, float4(Hit, 1.0f));
    Result.Depth  = Clip.z / Clip.w;
    Result.Colour = float4(Tint, saturate(Coverage));
    return Result;
}
