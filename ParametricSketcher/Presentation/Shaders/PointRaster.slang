//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/Shaders/PointRaster.slang — Billboard markers: vertices, control points, snap glyphs
//============================================================================================================================================
// One instance per point, expanded to a screen-aligned square of DrawRecord.Selection.w pixels. The fragment stage
//    draws a signed-distance glyph selected per instance: 0 disc, 1 square, 2 diamond, 3 cross, 4 ring. Outline is
//    a darker rim so markers read on both light and dark surfaces.

#include "ShadingRecords.slang"

struct PointVarying
{
    float4 Clip;                // clip-space position (SV_Position)
    float2 Local;               // −1 .. +1 across the billboard
    float  Glyph;               // glyph selector (flat)
};

PointVarying PointVertex(ViewRecord View, DrawRecord Draw, float3 Position, float Glyph, uint Corner)
{
    PointVarying Out;
    float4 Clip   = mul(View.ViewClip, mul(Draw.ModelWorld, float4(Position, 1.0)));
    float2 Local  = float2((Corner & 1u) == 1u ? 1.0 : -1.0, Corner >= 2u ? 1.0 : -1.0);
    float  Half   = Draw.Selection.w * 0.5 + 1.0;
    float2 Offset = Local * Half * View.Viewport.zw * 2.0 * Clip.w;
    Out.Local = Local;
    Out.Glyph = Glyph;
    Out.Clip = float4(Clip.xy + Offset, Clip.z - Clip.w * 4e-4, Clip.w);
    return Out;
}

float PointGlyphDistance(float2 P, float Glyph, float Radius)
{
    if (Glyph < 0.5) return length(P) - Radius;                                             // disc
    if (Glyph < 1.5) { float2 D = abs(P) - float2(Radius, Radius); return max(D.x, D.y); }   // square
    if (Glyph < 2.5) return (abs(P.x) + abs(P.y)) - Radius;                                 // diamond
    if (Glyph < 3.5) { float2 D = abs(P); return min(D.x, D.y) - Radius * 0.22; }           // cross (thin bars)
    return abs(length(P) - Radius) - Radius * 0.28;                                          // ring
}

float4 PointFragment(ViewRecord View, DrawRecord Draw, PointVarying In)
{
    float Half   = Draw.Selection.w * 0.5 + 1.0;
    float2 Pixel = In.Local * Half;                                                          // pixel offset from centre
    float Radius = Draw.Selection.w * 0.5 - 0.5;
    float D      = PointGlyphDistance(Pixel, In.Glyph, Radius);
    if (In.Glyph > 2.5 && In.Glyph < 3.5) D = max(D, max(abs(Pixel.x), abs(Pixel.y)) - Radius);   // clip cross arms
    float Fill    = 1.0 - smoothstep(-0.5, 0.5, D);
    float Outline = 1.0 - smoothstep(0.5, 1.5, D);
    float3 Colour = Draw.Tint.xyz;
    if (Draw.Selection.x > 1.5)      Colour = lerp(Colour, float3(1.00, 0.62, 0.15), 0.8);
    else if (Draw.Selection.x > 0.5) Colour = lerp(Colour, float3(1.0, 1.0, 1.0), 0.5);
    float3 Rim = Colour * 0.25;
    return float4(lerp(Rim, Colour, Fill), Draw.Tint.w * max(Fill, Outline * 0.9));
}
