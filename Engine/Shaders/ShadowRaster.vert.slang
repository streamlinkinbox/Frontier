//============================================================================================================================================
// 📦 Engine/Shaders/ShadowRaster.vert.slang — Shadow-map vertex stage: world → light clip
//============================================================================================================================================
// Depth-only pass, one draw per cluster exactly like VisibilityRaster.vert (gl_InstanceIndex == cluster index via
//    firstInstance, gl_VertexIndex already absolute into the shared VertexRecord buffer). The only difference is the
//    matrix: LightClip instead of ViewClip, and no jitter — a jittered shadow map would shimmer under the filter.
//
// Depth convention: this pass writes ORDINARY depth (near → 0, far → 1) with LESS, not the primary pass's reverse-Z.
//    The filters need a linear-ish distance they can compare and average, and the resolve reconstructs metres from
//    gl_FragCoord.z through the light's near/far — see ShadowSample.slang.
// NOTE: GLSL source lowered by glslc -fshader-stage=vertex.

#version 460
#extension GL_GOOGLE_include_directive : require
#include "Shaders/SceneRecords.slang"
#include "Shaders/ShadowRecords.slang"

layout(push_constant) uniform ShadowPush { uint ShadowTapIndex; };   // which tap this draw is filling

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer ClusterExtent  { ClusterRecord  Clusters[];  };
layout(std430, set = 0, binding = 3) readonly buffer VertexExtent   { VertexRecord   Vertices[];  };

layout(location = 0) flat out uint OutCluster;
layout(location = 1)      out vec2 OutTexcoord;   // alpha-masked cut-outs must punch holes in the shadow too

void main()
{
    uint           ClusterIndex = uint(gl_InstanceIndex);
    ClusterRecord  Cluster      = Clusters[ClusterIndex];
    InstanceRecord Instance     = Instances[Cluster.InstanceIndex];

    vec3 Object = Vertices[uint(gl_VertexIndex)].Position.xyz;
    vec4 World  = Instance.World * vec4(Object, 1.0);

    OutCluster  = ClusterIndex;
    OutTexcoord = Vertices[uint(gl_VertexIndex)].Texcoord.xy;
    gl_Position = ShadowLightClip[ShadowTapIndex] * World;
}
