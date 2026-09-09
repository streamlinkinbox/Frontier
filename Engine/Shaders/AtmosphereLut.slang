//============================================================================================================================================
//                                                        ATMOSPHERELUT.SLANG
//============================================================================================================================================
// 🧩 Builds the two constant atmosphere tables. Runs at bring-up and again only when a parameter changes — never
//    per frame, which is the entire reason the tables are worth having.
//
//    One kernel, two targets, selected by a push constant. Two pipelines would mean two places to keep in step
//    with AtmosphereScattering.slang, and the second would eventually drift.
//
//    🔴 Deliberately expensive. 64 optical-depth steps for transmittance, and 64 directions × 20 steps for
//    multiple scattering. That is thousands of times the per-texel budget the frame path could afford, and it is
//    affordable precisely because 16 384 + 1 024 texels are computed once rather than a million per frame.
//
//    ⚠️ Order matters: multiple scattering CALLS SunTransmittance, so it depends on the same physics the
//    transmittance table encodes. It re-marches rather than sampling the other table, so the two can be built in
//    either order and neither can be silently stale relative to the other.

#version 460
#extension GL_GOOGLE_include_directive : require

#include "AtmosphereScattering.slang"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba32f, binding = 0) uniform writeonly image2D TargetLut;

layout(push_constant) uniform AtmosphereLutConstants
{
    uint Width;        // [px]
    uint Height;       // [px]
    uint Target;       // 0 = transmittance, 1 = multiple scattering
    uint Directions;   // [cnt] sphere samples for the multiple-scattering gather
    uint Steps;        // [cnt] march steps along each of those directions
    float Turbidity;   // [-]   A7b: the aerosol load the tables are baked AT
    uint Padding1;
    uint Padding2;
};

void main()
{
    // A7b ⚠️ Baked at the same aerosol load the frame kernel will march with. A table built at one turbidity and
    //    sampled at another is the worst kind of disagreement: the sun's survival comes from the table and the
    //    sky's scattering from the march, so the horizon would be lit for a different atmosphere than the air
    //    above it — and nothing about the image says which half is wrong.
    gAerosolTurbidity = clamp(Turbidity, 0.0, 16.0);

    const uvec2 Texel = gl_GlobalInvocationID.xy;
    if (Texel.x >= Width || Texel.y >= Height) return;

    // Texel CENTRES. Sampling at the corner biases every row toward one end of its range, which shows up as the
    //    horizon sitting slightly in the wrong place — small, consistent, and very hard to attribute later.
    const vec2 Uv = (vec2(Texel) + 0.5) / vec2(float(Width), float(Height));

    vec3 Value;
    if (Target == 0u) Value = ComputeTransmittanceTexel(Uv);
    else              Value = ComputeMultiScatterTexel(Uv, int(Directions), int(Steps));

    imageStore(TargetLut, ivec2(Texel), vec4(Value, 1.0));
}
