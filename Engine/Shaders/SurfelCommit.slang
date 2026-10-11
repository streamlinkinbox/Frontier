//============================================================================================================================================
// 📦 Engine/Shaders/SurfelCommit.slang — Jacobi commit for the surfel field (pass 1.5 of the GI stage).
//============================================================================================================================================
// SurfelIrradianceUpdate.slang writes each surfel's new running-mean irradiance into IrradianceNew and leaves the
//    previous value in Irradiance untouched, because GatherField() during the update reads NEIGHBOURS' Irradiance
//    (the previous frame's committed value) — a Gauss–Seidel in-place write would make the result depend on thread
//    order. This trivial pass performs the deferred swap Irradiance = IrradianceNew for every live surfel, AFTER
//    the whole update dispatch has finished (a compute→compute barrier separates them). It is what the update
//    shader's closing comment ("host commits Irradiance = IrradianceNew") refers to; keeping it a GPU pass avoids a
//    per-frame readback. One thread per surfel, same 64-wide group and same GpuSurfel record as the update pass.
//
// NOTE: GLSL source, lowered by glslc / glslang -S comp (see BuildGlslang.sh).

#version 460
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct GpuSurfel
{
    vec4 PositionRadius;    // xyz = world position, w = world-space radius
    vec4 NormalAge;         // xyz = surface normal (unit), w = age in frames
    vec4 Albedo;            // xyz = diffuse albedo, w = alive flag (>0.5)
    vec4 Irradiance;        // xyz = E (committed) — gathered by neighbours next frame
    vec4 IrradianceNew;     // xyz = E' (this frame's running-mean result)
};

layout(std430, binding = 0) buffer SurfelExtent { GpuSurfel Surfels[]; };

layout(push_constant) uniform SurfelCommitConstants
{
    uvec4 Counts;   // x = surfel count; yzw unused (kept uvec4 for std430 push alignment parity with the other passes)
} PC;

void main()
{
    uint si = gl_GlobalInvocationID.x;
    if (si >= PC.Counts.x) return;
    if (Surfels[si].Albedo.w < 0.5) return;         // skip dead slots
    Surfels[si].Irradiance = Surfels[si].IrradianceNew;
}
