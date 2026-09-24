//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/Shaders/LatticeProjection.slang — Analytic infinite Z-up ground lattice with LOD fade and world axes
//============================================================================================================================================
// A full-screen triangle. Each fragment reconstructs its view ray, intersects the z = 0 plane analytically, and shades
//    an anti-aliased two-level lattice (minor / major) whose minor cell doubles as the camera pulls back so line density
//    stays roughly constant on screen. Radial fade dissolves it into the background instead of a hard horizon.
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

// Coverage of the nearest lattice line of spacing Cell, anti-aliased over one pixel. Extent is world units per pixel.
float LatticeLineCoverage(float2 Position, float Cell, float Extent, float HalfWidthPixels)
{
    float2 Scaled   = Position / Cell;
    float2 ToLine   = abs(fract(Scaled + float2(0.5, 0.5)) - float2(0.5, 0.5)) * Cell;
    float  Distance = min(ToLine.x, ToLine.y);
    float  Pixels   = Distance / max(Extent, 1e-6);
    return 1.0 - smoothstep(HalfWidthPixels - 0.5, HalfWidthPixels + 0.5, Pixels);
}

// Coverage of a single axis line (distance to the line x = 0 or y = 0), slightly bolder than lattice lines.
float AxisCoverage(float Distance, float Extent, float HalfWidthPixels)
{
    float Pixels = abs(Distance) / max(Extent, 1e-6);
    return 1.0 - smoothstep(HalfWidthPixels - 0.5, HalfWidthPixels + 0.5, Pixels);
}

LatticeSample LatticeShade(ViewRecord View, float3 Origin, float3 Direction, float PixelAngle, float PixelWorld)
{
    LatticeSample Result;
    Result.Colour = float4(0.0, 0.0, 0.0, 0.0);
    Result.Depth  = 1.0;

    if (abs(Direction.z) < 1e-7) return Result;
    float T = -Origin.z / Direction.z;
    if (T <= 0.0) return Result;
    float3 Hit = Origin + Direction * T;

    // World size of one pixel at the hit, stretched by the grazing angle so lines stay one pixel wide when seen flat.
    float Grazing   = max(abs(Direction.z), 0.05);
    float Extent = (PixelAngle > 0.0 ? T * PixelAngle : PixelWorld) / Grazing;

    // LOD: choose the power-of-ten minor cell such that a cell spans at least ~14 pixels; blend between levels.
    float MinorCell   = View.LatticeStyle.x;
    float TargetCell  = Extent * 14.0;
    float LevelExact  = max(0.0, log(TargetCell / MinorCell) / log(10.0));
    float Level       = floor(LevelExact);
    float LevelMix  = LevelExact - Level;
    float CellA       = MinorCell * pow(10.0, Level);
    float CellB       = CellA * 10.0;

    float HalfWidth   = View.LatticeStyle.w;
    float MinorA      = LatticeLineCoverage(Hit.xy, CellA, Extent, HalfWidth) * (1.0 - LevelMix);
    float MinorB      = LatticeLineCoverage(Hit.xy, CellB, Extent, HalfWidth);
    float MajorB      = LatticeLineCoverage(Hit.xy, CellB * View.LatticeStyle.y, Extent, HalfWidth + 0.25);

    float3 MinorTint  = float3(0.36, 0.38, 0.43);
    float3 MajorTint  = float3(0.50, 0.53, 0.60);
    float  Coverage   = max(MinorA * 0.55, max(MinorB * 0.75, MajorB));
    float3 Tint       = lerp(MinorTint, MajorTint, saturate(MajorB));

    // World axes on top: X red, Y green. Drawn only where the other coordinate is near zero.
    float AxisX = AxisCoverage(Hit.y, Extent, HalfWidth + 0.5);
    float AxisY = AxisCoverage(Hit.x, Extent, HalfWidth + 0.5);
    Tint     = lerp(Tint, float3(0.86, 0.24, 0.24), AxisX);
    Tint     = lerp(Tint, float3(0.30, 0.78, 0.30), AxisY);
    Coverage = max(Coverage, max(AxisX, AxisY));

    // Radial fade around the eye's ground foot, plus a horizon fade from the grazing angle.
    float FadeRadius  = View.LatticeStyle.z;
    float Radial      = length(Hit.xy - Origin.xy);
    float Fade        = 1.0 - smoothstep(FadeRadius * 0.35, FadeRadius, Radial);
    float Horizon     = smoothstep(0.0, 0.18, abs(Direction.z));
    Coverage         *= Fade * Horizon;

    float4 Clip   = mul(View.ViewClip, float4(Hit, 1.0));
    Result.Depth  = Clip.z / Clip.w;
    Result.Colour = float4(Tint, saturate(Coverage));
    return Result;
}
