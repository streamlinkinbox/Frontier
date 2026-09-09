//============================================================================================================================================
// 📦 Engine/Shaders/HiZReduce.slang — Reverse-Z min-reduction of the depth buffer into a mip pyramid (one level per dispatch)
//============================================================================================================================================
// Level 0 copies the depth attachment (rendered region only; the rest of the image is left at 0 = "infinitely far",
//    which is the correct conservative value for the min). Level N reads the 2×2 (or 3×3 on odd edges) footprint of
//    level N−1 and keeps the MINIMUM — with reverse-Z that is the farthest surface, so an occludee is culled only when it
//    is behind everything in its footprint.
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.

#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0)        uniform sampler2D     SourceLevel;   // depth attachment (level 0 pass) or HiZ level N−1
layout(set = 0, binding = 1, r32f)  uniform writeonly image2D TargetLevel;

layout(push_constant) uniform ReduceConstants
{
    uvec2 SourceExtent;      // [px] valid texels of the source level
    uvec2 TargetExtent;      // [px] texels to write in the target level
    uint  CopyLevelZero;     // 1 = plain copy from the depth attachment
};

void main()
{
    uvec2 T = gl_GlobalInvocationID.xy;
    if (T.x >= TargetExtent.x || T.y >= TargetExtent.y) return;

    if (CopyLevelZero != 0u)
    {
        imageStore(TargetLevel, ivec2(T), vec4(texelFetch(SourceLevel, ivec2(T), 0).r));
        return;
    }

    ivec2 S  = ivec2(T) * 2;
    ivec2 Sm = ivec2(SourceExtent) - 1;
    float D  = texelFetch(SourceLevel, min(S,                Sm), 0).r;
    D = min(D, texelFetch(SourceLevel, min(S + ivec2(1, 0), Sm), 0).r);
    D = min(D, texelFetch(SourceLevel, min(S + ivec2(0, 1), Sm), 0).r);
    D = min(D, texelFetch(SourceLevel, min(S + ivec2(1, 1), Sm), 0).r);

    // Odd source extents: the last target texel also covers a third source column / row.
    bool ExtraX = (SourceExtent.x & 1u) != 0u && T.x == TargetExtent.x - 1u;
    bool ExtraY = (SourceExtent.y & 1u) != 0u && T.y == TargetExtent.y - 1u;
    if (ExtraX)           { D = min(D, texelFetch(SourceLevel, min(S + ivec2(2, 0), Sm), 0).r); D = min(D, texelFetch(SourceLevel, min(S + ivec2(2, 1), Sm), 0).r); }
    if (ExtraY)           { D = min(D, texelFetch(SourceLevel, min(S + ivec2(0, 2), Sm), 0).r); D = min(D, texelFetch(SourceLevel, min(S + ivec2(1, 2), Sm), 0).r); }
    if (ExtraX && ExtraY) { D = min(D, texelFetch(SourceLevel, min(S + ivec2(2, 2), Sm), 0).r); }

    imageStore(TargetLevel, ivec2(T), vec4(D));
}
