//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/Shaders/LineRaster.slang — Screen-space width polylines: curves, edges, gizmo lines, rubber band
//============================================================================================================================================
// One instance per segment (A, B) expanded in the vertex stage to a screen-aligned quad of constant pixel width.
//    Corner index 0..3 picks the quad vertex. Dashing is a per-draw option for construction geometry. Width and
//    colour come from DrawRecord; selection brightens toward orange. Depth is taken from the true 3D segment so
//    lines occlude correctly against surfaces, with a small bias so an edge lying ON a face still wins.

#include "ShadingRecords.slang"

struct LineVarying
{
    float4 Clip;                // clip-space position (SV_Position)
    float  Across;              // −1 .. +1 across the width, for anti-aliased edges
    float  Along;               // pixels along the segment from A, for dashing
    float  SegmentPixels;       // total pixel length of the segment
};

// Returns clip position for corner (0: A left, 1: A right, 2: B left, 3: B right).
LineVarying LineVertex(ViewRecord View, DrawRecord Draw, float3 A, float3 B, uint Corner)
{
    LineVarying Out;
    float4 ClipA = mul(View.ViewClip, mul(Draw.ModelWorld, float4(A, 1.0)));
    float4 ClipB = mul(View.ViewClip, mul(Draw.ModelWorld, float4(B, 1.0)));

    // Clip against the near plane (w > ε) so a segment crossing behind the eye is shortened, not flipped.
    float Near = 1e-4;
    if (ClipA.w < Near && ClipB.w < Near) { Out.Across = 0; Out.Along = 0; Out.SegmentPixels = 0; Out.Clip = float4(0, 0, 2, 1); return Out; }
    if (ClipA.w < Near) { float T = (Near - ClipA.w) / (ClipB.w - ClipA.w); ClipA = lerp(ClipA, ClipB, T); }
    if (ClipB.w < Near) { float T = (Near - ClipB.w) / (ClipA.w - ClipB.w); ClipB = lerp(ClipB, ClipA, T); }

    float2 ScreenA = (ClipA.xy / ClipA.w) * float2(0.5, 0.5) * View.Viewport.xy;
    float2 ScreenB = (ClipB.xy / ClipB.w) * float2(0.5, 0.5) * View.Viewport.xy;
    float2 Delta   = ScreenB - ScreenA;
    float  Pixels  = max(length(Delta), 1e-3);
    float2 Dir     = Delta / Pixels;
    float2 Side    = float2(-Dir.y, Dir.x);

    float  HalfWidth = Draw.Selection.z * 0.5 + 0.5;                            // +0.5 px feather margin
    bool   AtB       = Corner >= 2u;
    float  Sign      = (Corner & 1u) == 1u ? 1.0 : -1.0;
    float2 Screen    = (AtB ? ScreenB : ScreenA) + Side * (HalfWidth * Sign) + Dir * (AtB ? 0.5 : -0.5);
    float4 Clip      = AtB ? ClipB : ClipA;

    Out.Across        = Sign;
    Out.Along         = AtB ? Pixels : 0.0;
    Out.SegmentPixels = Pixels;

    float2 Ndc = Screen / (float2(0.5, 0.5) * View.Viewport.xy);
    Out.Clip = float4(Ndc * Clip.w, Clip.z - Clip.w * 2e-4, Clip.w);        // depth bias toward the eye
    return Out;
}

float4 LineFragment(ViewRecord View, DrawRecord Draw, LineVarying In, bool Dashed)
{
    float HalfWidth = Draw.Selection.z * 0.5;
    float Distance  = abs(In.Across) * (HalfWidth + 0.5);
    float Coverage  = 1.0 - smoothstep(HalfWidth - 0.5, HalfWidth + 0.5, Distance);
    if (Dashed)
    {
        float Phase = fract(In.Along / 12.0);                                   // 12 px period, 50 % duty
        Coverage *= smoothstep(0.0, 0.08, Phase) * (1.0 - smoothstep(0.50, 0.58, Phase));
    }
    float3 Colour = Draw.Tint.xyz;
    if (Draw.Selection.x > 1.5)      Colour = lerp(Colour, float3(1.00, 0.62, 0.15), 0.8);
    else if (Draw.Selection.x > 0.5) Colour = lerp(Colour, float3(1.0, 1.0, 1.0), 0.5);
    return float4(Colour, Draw.Tint.w * Coverage);
}
