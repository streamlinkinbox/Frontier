//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/Shaders/PointRaster.slang — Billboard markers: vertices, control points, snap glyphs
//============================================================================================================================================
// One instance per point, expanded to a screen-aligned square of DrawRecord.Selection.w pixels. The fragment shaders
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
    float4 Clip   = mul(View.ViewClip, mul(Draw.LocalWorld, float4(Position, 1.0f)));
    float2 Local  = float2((Corner & 1u) == 1u ? 1.0f : -1.0f, Corner >= 2u ? 1.0f : -1.0f);
    float  Half   = Draw.Selection.w * 0.5f + 1.0f;
    float2 Offset = Local * Half * View.Viewport.zw * 2.0f * Clip.w;
    Out.Local = Local;
    Out.Glyph = Glyph;
    Out.Clip = float4(Clip.xy + Offset, Clip.z - Clip.w * 4e-4f, Clip.w);
    return Out;
}

float PointGlyphDistance(float2 P, float Glyph, float Radius)
{
    if (Glyph < 0.5f) return length(P) - Radius;                                             // disc
    if (Glyph < 1.5f) { float2 D = abs(P) - float2(Radius, Radius); return max(D.x, D.y); }   // square
    if (Glyph < 2.5f) return (abs(P.x) + abs(P.y)) - Radius;                                 // diamond
    if (Glyph < 3.5f) { float2 D = abs(P); return min(D.x, D.y) - Radius * 0.22f; }           // cross (thin bars)
    return abs(length(P) - Radius) - Radius * 0.28f;                                          // ring
}

float4 PointFragment(ViewRecord View, DrawRecord Draw, PointVarying In)
{
    float Half   = Draw.Selection.w * 0.5f + 1.0f;
    float2 Pixel = In.Local * Half;                                                          // pixel offset from centre
    float Radius = Draw.Selection.w * 0.5f - 0.5f;
    float D      = PointGlyphDistance(Pixel, In.Glyph, Radius);
    if (In.Glyph > 2.5f && In.Glyph < 3.5f) D = max(D, max(abs(Pixel.x), abs(Pixel.y)) - Radius);   // clip cross arms
    float Fill    = 1.0f - smoothstep(-0.5f, 0.5f, D);
    float Outline = 1.0f - smoothstep(0.5f, 1.5f, D);
    float3 Colour = Draw.Tint.xyz;
    if (Draw.Selection.x > 1.5f)      Colour = lerp(Colour, float3(1.00f, 0.62f, 0.15f), 0.8f);
    else if (Draw.Selection.x > 0.5f) Colour = lerp(Colour, float3(1.0f, 1.0f, 1.0f), 0.5f);
    float3 Rim = Colour * 0.25f;
    return float4(lerp(Rim, Colour, Fill), Draw.Tint.w * max(Fill, Outline * 0.9f));
}
