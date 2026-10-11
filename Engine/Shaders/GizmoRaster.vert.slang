//============================================================================================================================================
// 📦 Engine/Shaders/GizmoRaster.vert.slang — Transform-gizmo vertex stage: pulled vertex → clip
//============================================================================================================================================
// The gizmo's vertices are composed on the CPU each frame (Engine/Editor/GizmoFigures.cpp — the 1:1 port of
//    References/Gizmo.html) and uploaded as a 64-byte-stride SSBO; the draw binds no vertex buffer and pulls by
//    gl_VertexIndex, the same idiom as the visibility raster. Two draws per frame share this stage: the triangle
//    list (cones, cylinders, quads, arcs, ring) and the line list (the corner quads' two opaque edges).
// NOTE: GLSL source lowered by glslc -fshader-stage=vertex.

#version 460

struct GizmoVertexRecord
{
    vec3  Location;    // [m]  world
    float Emissive;    // [-]  hover lift
    vec3  Normal;      // [-]  unit; unused when Unlit
    float Unlit;       // [-]  1 keeps the tint exactly
    vec4  Tint;        // [-]  linear rgb + opacity
    vec2  Spare;
    vec2  Spare2;
};

layout(std430, binding = 0) readonly buffer GizmoVertices
{
    GizmoVertexRecord Vertices[];
};

layout(push_constant) uniform GizmoConstants
{
    mat4 ViewClip;       // world → clip, the visibility raster's own projection
    vec4 CameraOrigin;   // xyz eye, w unused
    vec4 LightToward;    // xyz unit toward the light (the reference's directional at (4,6,5)), w unused
};

layout(location = 0) out vec3  OutWorld;
layout(location = 1) out vec3  OutNormal;
layout(location = 2) out vec4  OutTint;
layout(location = 3) out float OutEmissive;
layout(location = 4) out float OutUnlit;

void main()
{
    GizmoVertexRecord Figure = Vertices[gl_VertexIndex];
    OutWorld    = Figure.Location;
    OutNormal   = Figure.Normal;
    OutTint     = Figure.Tint;
    OutEmissive = Figure.Emissive;
    OutUnlit    = Figure.Unlit;
    gl_Position = ViewClip * vec4(Figure.Location, 1.0);
}
