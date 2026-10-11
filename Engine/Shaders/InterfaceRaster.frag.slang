//============================================================================================================================================
// 📦 Engine/Shaders/InterfaceRaster.frag.slang — Spatial-interface fragment stage: signed distance → coverage → colour
//============================================================================================================================================
// Every figure category is evaluated here from the same 96-byte slot by the same include — a dial with sixty ticks,
//    a redline band and a needle costs a few dozen ALU ops on one quad, against thousands of triangles that would
//    have to be rebuilt whenever a value moved.
//
// The edge width comes from the screen-space derivative of the local coordinate, so a figure stays exactly as sharp
//    when it is two metres away and tilted forty degrees as when it fills the screen. That is the property a
//    pre-rasterised vector image cannot hold, and the reason this pipeline draws shapes instead of sampling images.
//
// Output is premultiplied alpha, to be composited with (ONE, ONE_MINUS_SRC_ALPHA) against the resolved scene.
// NOTE: GLSL source lowered by glslc -fshader-stage=fragment.

#version 460
#extension GL_GOOGLE_include_directive : require
#include "Shaders/InterfaceRecords.slang"
#include "Shaders/InterfaceSignedDistance.slang"

layout(location = 0) flat in uint  InFigure;
layout(location = 1)      in vec2  InLocal;

layout(location = 0) out vec4 OutColour;

void main()
{
    InterfaceInstanceFigure Figure = Figures[InFigure];

    // One screen pixel, expressed in the figure's local metres. fwidth is the sum of the absolute derivatives, which
    //    is the standard conservative estimate and behaves correctly under perspective and rotation alike.
    float PixelWidth = max(fwidth(InLocal.x), fwidth(InLocal.y));

    uint  Category = Figure.CategoryPalette >> 24;
    float Distance = DistanceFigure(Category, InLocal, Figure.HalfExtent,
                                    Figure.CornerRadius, Figure.ScalarAlpha, Figure.ScalarBeta);

    float Coverage = CoverageFromDistance(Distance, PixelWidth);

    // ⑥ Shader clip: a rounded rectangle in the figure's own plane. Works under any transform, unlike a scissor
    //    rectangle, and costs a handful of ALU ops in the shader that was already running.
    Coverage *= ClipCoverage(InLocal, Figure.ClipExtent, 0.0, PixelWidth);

    if (Coverage <= 0.0) discard;

    vec4  Tint  = InterfaceUnpackTint(Figure.Tint);
    float Alpha = Coverage * Figure.Opacity * Tint.w;

    if (Alpha <= 0.0) discard;

    // ⑦ Surface response. Emissive elements show their tint at full strength regardless of the room. Albedo-side
    //    elements (the housing) are lit by AmbientIrradiance, so a dark bezel sits DOWN in a dim room instead of
    //    glowing like a decal. AmbientIrradiance is a single scene-average term in P0 — the proxy luminaire in
    //    References/InterfaceLightContribution-Plan.md upgrades this to real transport without touching the slot.
    vec3  Albedo   = InterfaceUnpackTint(Figure.BaseColour).xyz;
    vec3  Emitted  = Tint.xyz;
    vec3  Received = Albedo * AmbientIrradiance.xyz;
    vec3  Colour   = mix(Received, Emitted, clamp(Figure.EmissiveWeight, 0.0, 1.0));

    OutColour = vec4(Colour * Alpha, Alpha);   // premultiplied
}
