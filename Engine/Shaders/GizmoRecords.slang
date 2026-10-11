//============================================================================================================================================
// 📦 Engine/Shaders/GizmoRecords.slang — Transform-gizmo shading shared by the GPU raster and the CPU proof
//============================================================================================================================================
// 🧩 The gizmo's lighting, written once. GizmoRaster.frag.slang includes this on the GPU; the CPU proof compiles
//    this exact text as C++ (SlangCpuShim.h, FRONTIER_CPU_PORT) and rasterises the same vertices with it, so the
//    proof PNG and the engine build shade the same figures with the same arithmetic.
//
//    The reference (References/Gizmo.html) lights its lit pieces with an ambient of 0.75 and one directional
//    of 0.9, and leaves the plane fills, the corner edges and the central ring unlit. A hovered grip adds an
//    emissive lift of 0x555555. Those three figures are reproduced here verbatim.

#ifndef FRONTIER_GIZMO_RECORDS
#define FRONTIER_GIZMO_RECORDS

const float kGizmoAmbient     = 0.75;      // the reference's AmbientLight intensity
const float kGizmoDirectional = 0.9;       // the reference's DirectionalLight intensity
const float kGizmoHoverLift   = 0.3333;    // emissive 0x555555 on the hovered grip

// One shading rule for every gizmo pixel. Unlit pieces (plane fills, corner edges, the central ring) pass
//    Unlit = 1 and keep their tint exactly; lit pieces take ambient + one directional, both faces lit the way
//    the reference's DoubleSide materials are — the normal is flipped toward the eye before the dot.
vec3 GizmoShade(vec3 Tint, vec3 Normal, vec3 TowardEye, vec3 TowardLight, float Emissive, float Unlit)
{
    if (Unlit > 0.5)
        return Tint;
    vec3 Facing = dot(Normal, TowardEye) < 0.0 ? -Normal : Normal;
    float Incidence = max(dot(Facing, TowardLight), 0.0);
    vec3 Lit = Tint * (kGizmoAmbient + kGizmoDirectional * Incidence);
    return min(Lit + vec3(Emissive, Emissive, Emissive), vec3(1.0, 1.0, 1.0));
}

#endif
