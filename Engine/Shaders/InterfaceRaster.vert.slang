//============================================================================================================================================
// 📦 Engine/Shaders/InterfaceRaster.vert.slang — Spatial-interface vertex stage: instance → plane corner → clip
//============================================================================================================================================
// One instanced draw for the entire interface: vkCmdDraw(4, InstanceCount, 0, 0) over a triangle strip, with NO
//    vertex buffer bound at all. gl_VertexIndex picks the corner, gl_InstanceIndex picks the 96-byte slot. That is
//    the whole reason the draw count is independent of the figure count.
//
// The fragment stage needs the position on the figure's local plane (to evaluate the signed distance) and the local
//    size of one screen pixel (to keep the anti-aliased edge exactly one pixel wide at any distance or tilt).
// NOTE: GLSL source lowered by glslc -fshader-stage=vertex.

#version 460
#extension GL_GOOGLE_include_directive : require
#include "Shaders/InterfaceRecords.slang"

layout(location = 0) flat out uint  OutFigure;      // instance ordinal — the fragment stage re-reads the slot
layout(location = 1)      out vec2  OutLocal;       // position on the figure's local plane [m]

void main()
{
    uint                    Ordinal = uint(gl_InstanceIndex);
    InterfaceInstanceFigure Figure  = Figures[Ordinal];

    // Expand the quad by one corner radius' worth of feather so the anti-aliased edge is never clipped by the
    //    geometry itself. Two millimetres is generous at every practical panel size.
    vec2 Padded = Figure.HalfExtent + vec2(0.002);
    vec2 Local  = InterfaceCorner(uint(gl_VertexIndex), Padded);

    vec3 World  = InterfaceLocalToWorld(Figure, vec3(Local, 0.0));

    OutFigure   = Ordinal;
    OutLocal    = Local;
    gl_Position = ViewClip * vec4(World, 1.0);

    // Back-face rejection for single-sided figures. Done here rather than with pipeline culling because the quad's
    //    winding depends on how the project oriented the panel, whereas the figure's own +Z axis is unambiguous:
    //    if the eye is behind the plane, the viewer is looking at the back of the fascia and must see nothing.
    //    Collapsing the vertex to the origin discards the whole quad before rasterisation, so no fragment runs.
    if (!InterfaceIsDoubleSided(Figure) && InterfaceEyeSide(Figure, CameraOrigin.xyz) < 0.0)
        gl_Position = vec4(0.0, 0.0, 0.0, 0.0);
}
