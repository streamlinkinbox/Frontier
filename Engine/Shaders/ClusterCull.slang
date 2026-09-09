//============================================================================================================================================
// 📦 Engine/Shaders/ClusterCull.slang — GPU cluster culling: frustum → normal cone → HiZ, appends indirect draws
//============================================================================================================================================
// Two-phase occlusion culling (the "Nanite / Niagara" scheme):
//    phase 1  clusters visible LAST frame: frustum + cone only → draw list A (rasterised first, HiZ is built from it)
//    phase 2  every cluster: frustum + cone + HiZ(from phase 1) → next frame's visibility bit; clusters that were NOT
//             drawn in phase 1 and pass go to draw list B. Newly disoccluded clusters therefore appear in the same frame
//             (no popping); the only cost is that last frame's visible set is rasterised without an occlusion test.
//    One thread per cluster. Draw lists are consumed by vkCmdDrawIndexedIndirectCount (one call per phase).
// NOTE: GLSL source lowered by glslc -fshader-stage=compute (see ToolchainSequence.ps1 / CMakeLists.txt).

#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "Shaders/SceneRecords.slang"

layout(std430, set = 0, binding = 1) readonly  buffer InstanceExtent   { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 2) readonly  buffer ClusterExtent    { ClusterRecord  Clusters[];  };
layout(std430, set = 0, binding = 3)           buffer DrawExtent       { DrawCommand    Draws[];     };   // [0, N) phase 1, [N, 2N) phase 2
layout(std430, set = 0, binding = 4)           buffer CounterExtent    { uint           Counters[];  };
layout(std430, set = 0, binding = 5) readonly  buffer PreviousVisible  { uint           WasVisible[]; };  // 1 bit per cluster, last frame
layout(std430, set = 0, binding = 6)           buffer CurrentVisible   { uint           IsVisible[];  };  // 1 bit per cluster, this frame
layout(set = 0, binding = 7) uniform sampler2D HiZ;                                                        // reverse-Z min pyramid of phase 1

const uint kControlConeCull = 2u;   // Control.w bit 1 — off by default: the kernel treats triangles as two-sided

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPHERE TESTS
//------------------------------------------------------------------------------------------------------------------------

bool InsideFrustum(vec3 Center, float Radius)
{
    for (int P = 0; P < 5; ++P)
        if (dot(FrustumPlanes[P].xyz, Center) + FrustumPlanes[P].w < -Radius) return false;
    return true;
}

// meshoptimizer cone test with the bounding sphere as apex proxy: every triangle faces away from the eye when
//    dot(C − eye, axis) ≥ cutoff · |C − eye| + radius.
bool FacesAway(vec3 Center, float Radius, vec3 Axis, float Cutoff)
{
    if (Cutoff >= 1.0) return false;
    vec3  ToCenter = Center - CameraOrigin.xyz;
    float Distance = length(ToCenter);
    return dot(ToCenter, Axis) >= Cutoff * Distance + Radius;
}

// Screen-space bounds of the sphere's world AABB; false when any corner is at or behind the near plane (→ treat as
//    visible). Depth returned is the sphere's NEAREST reverse-Z value (largest), so the test is conservative.
bool ProjectSphere(vec3 Center, float Radius, out vec2 MinimumUv, out vec2 MaximumUv, out float NearestDepth)
{
    float ViewDepth = dot(Center - CameraOrigin.xyz, CameraForward.xyz);
    float Near      = CameraOrigin.w;
    if (ViewDepth - Radius <= Near) return false;
    NearestDepth = Near / (ViewDepth - Radius);

    MinimumUv = vec2( 1e30);
    MaximumUv = vec2(-1e30);
    for (int I = 0; I < 8; ++I)
    {
        vec3 Corner = Center + Radius * vec3((I & 1) != 0 ? 1.0 : -1.0, (I & 2) != 0 ? 1.0 : -1.0, (I & 4) != 0 ? 1.0 : -1.0);
        vec4 Clip   = ViewClip * vec4(Corner, 1.0);
        if (Clip.w <= 1e-6) return false;
        vec2 Uv = Clip.xy / Clip.w * 0.5 + 0.5;               // NDC y already points down (projection flip) → image uv
        MinimumUv = min(MinimumUv, Uv);
        MaximumUv = max(MaximumUv, Uv);
    }
    return true;
}

bool OccludedByHiZ(vec3 Center, float Radius)
{
    vec2 MinimumUv, MaximumUv;
    float NearestDepth;
    if (!ProjectSphere(Center, Radius, MinimumUv, MaximumUv, NearestDepth)) return false;

    MinimumUv = clamp(MinimumUv, vec2(0.0), vec2(1.0));
    MaximumUv = clamp(MaximumUv, vec2(0.0), vec2(1.0));
    if (MaximumUv.x <= MinimumUv.x || MaximumUv.y <= MinimumUv.y) return false;

    // HiZ mip 0 is 1:1 with render pixels at the top-left of the image; mip L texel = pixel >> L.
    vec2  Extent0  = vec2(Extent.xy);
    vec2  MinPixel = MinimumUv * Extent0;
    vec2  MaxPixel = MaximumUv * Extent0;
    float Width    = max(MaxPixel.x - MinPixel.x, MaxPixel.y - MinPixel.y);
    int   Level    = clamp(int(ceil(log2(max(Width, 1.0)))), 0, int(Extent.z) - 1);
    ivec2 LevelExtent = textureSize(HiZ, Level);
    ivec2 T0 = clamp(ivec2(MinPixel) >> Level, ivec2(0), LevelExtent - 1);
    ivec2 T1 = clamp(ivec2(MaxPixel) >> Level, ivec2(0), LevelExtent - 1);

    // Reverse-Z: the pyramid holds the FARTHEST (minimum) depth of each footprint. Occluded iff the sphere's nearest
    //    point is still farther than everything already drawn there.
    float Farthest = min(min(texelFetch(HiZ, T0,               Level).r, texelFetch(HiZ, ivec2(T1.x, T0.y), Level).r),
                         min(texelFetch(HiZ, ivec2(T0.x, T1.y), Level).r, texelFetch(HiZ, T1,               Level).r));
    return NearestDepth < Farthest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MAIN
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uint ClusterIndex = gl_GlobalInvocationID.x;
    if (ClusterIndex >= Extent.w) return;

    uint Phase    = Control.x;
    uint Word     = ClusterIndex >> 5;
    uint Bit      = 1u << (ClusterIndex & 31u);
    bool Previous = (WasVisible[Word] & Bit) != 0u;

    if (Phase == 1u && !Previous) return;                       // phase 1 only re-draws last frame's visible set

    ClusterRecord  Cluster  = Clusters[ClusterIndex];
    InstanceRecord Instance = Instances[Cluster.InstanceIndex];

    vec3  Center = (Instance.World * vec4(Cluster.Sphere.xyz, 1.0)).xyz;
    float Scale  = max(max(length(Instance.World[0].xyz), length(Instance.World[1].xyz)), length(Instance.World[2].xyz));
    float Radius = Cluster.Sphere.w * Scale;

    if (Phase == 2u) atomicAdd(Counters[kCounterClusterTotal], 1u);

    if (!InsideFrustum(Center, Radius))
    {
        if (Phase == 2u) atomicAnd(IsVisible[Word], ~Bit);
        return;
    }
    if (Phase == 2u) atomicAdd(Counters[kCounterFrustumPassed], 1u);

    if ((Control.w & kControlConeCull) != 0u && (Instance.Flags & kInstanceFlagDoubleSided) == 0u)
    {
        vec3 Axis = normalize(mat3(Instance.World) * Cluster.Cone.xyz);
        if (FacesAway(Center, Radius, Axis, Cluster.Cone.w))
        {
            if (Phase == 2u) atomicAnd(IsVisible[Word], ~Bit);
            return;
        }
    }
    if (Phase == 2u) atomicAdd(Counters[kCounterConePassed], 1u);

    bool Visible = true;
    if (Phase == 2u && (Control.w & kControlHiZEnabled) != 0u)
        Visible = !OccludedByHiZ(Center, Radius);

    if (Phase == 2u)
    {
        if (Visible) { atomicOr(IsVisible[Word], Bit); atomicAdd(Counters[kCounterHiZPassed], 1u); }
        else         { atomicAnd(IsVisible[Word], ~Bit); }
        if (!Visible || Previous) return;                       // already drawn in phase 1, or occluded
    }

    uint Slot = atomicAdd(Counters[Phase == 1u ? kCounterDrawCount : kCounterDrawCountPhaseTwo], 1u);
    uint Base = Phase == 1u ? 0u : Extent.w;

    DrawCommand Draw;
    Draw.IndexCount    = Cluster.TriangleCount * 3u;
    Draw.InstanceCount = 1u;
    Draw.FirstIndex    = Cluster.FirstIndex;
    Draw.VertexOffset  = int(Instance.VertexOffset);
    Draw.FirstInstance = ClusterIndex;
    Draws[Base + Slot] = Draw;

    atomicAdd(Counters[kCounterTriangleDrawn], Cluster.TriangleCount);
    if (Phase == 1u) { atomicAdd(Counters[kCounterPhaseOneDrawn], 1u); atomicOr(IsVisible[Word], Bit); }
}
