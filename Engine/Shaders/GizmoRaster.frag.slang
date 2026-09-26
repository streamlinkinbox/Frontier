//============================================================================================================================================
// 📦 Engine/Shaders/GizmoRaster.frag.slang — Transform-gizmo fragment stage: the reference's lighting, verbatim
//============================================================================================================================================
// Shades exactly as References/Gizmo.html does: lit pieces take ambient 0.75 + one directional 0.9 (double
//    sided — the normal flips toward the eye), unlit pieces (plane fills, corner edges, the ring) keep their
//    tint, a hovered grip lifts by 0x555555. The shared arithmetic lives in GizmoRecords.slang so the CPU proof
//    compiles the same text. Alpha blends over the lit scene; only the corner quads carry opacity below one.
// NOTE: GLSL source lowered by glslc -fshader-stage=fragment.

#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3  InWorld;
layout(location = 1) in vec3  InNormal;
layout(location = 2) in vec4  InTint;
layout(location = 3) in float InEmissive;
layout(location = 4) in float InUnlit;

layout(location = 0) out vec4 OutColour;

layout(push_constant) uniform GizmoConstants
{
    mat4 ViewClip;
    vec4 CameraOrigin;
    vec4 LightToward;
};

#include "Shaders/GizmoRecords.slang"

void main()
{
    vec3 TowardEye = normalize(CameraOrigin.xyz - InWorld);
    vec3 Shaded = GizmoShade(InTint.rgb, normalize(InNormal), TowardEye, LightToward.xyz, InEmissive, InUnlit);
    OutColour = vec4(Shaded, InTint.a);
}
