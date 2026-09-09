//============================================================================================================================================
// 📦 Engine/Shaders/VisibilityRaster.vert.slang — Visibility-buffer vertex stage: clip position + previous-frame clip
//============================================================================================================================================
// One indirect draw per cluster; gl_InstanceIndex == cluster index (firstInstance), gl_VertexIndex == index value +
//    vertexOffset (already instance-relative → absolute into the shared VertexRecord buffer). Depth is reverse-Z (see
//    GeometricRaster/ClipProjection.h); the fragment stage only needs the triangle id, so nothing else is interpolated.
// NOTE: GLSL source lowered by glslc -fshader-stage=vertex.

#version 460
#extension GL_GOOGLE_include_directive : require
#include "Shaders/SceneRecords.slang"

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer ClusterExtent  { ClusterRecord  Clusters[];  };
layout(std430, set = 0, binding = 3) readonly buffer VertexExtent   { VertexRecord   Vertices[];  };

layout(location = 0) flat out uint  OutCluster;
layout(location = 1)      out vec4  OutPreviousClip;    // previous frame, unjittered
layout(location = 2)      out vec4  OutCurrentClip;     // current frame, unjittered
layout(location = 3)      out vec2  OutTexcoord;        // R4b: uv for the alpha-mask test in the fragment stage

void main()
{
    uint           ClusterIndex = uint(gl_InstanceIndex);
    ClusterRecord  Cluster      = Clusters[ClusterIndex];
    InstanceRecord Instance     = Instances[Cluster.InstanceIndex];

    vec3 Object   = Vertices[uint(gl_VertexIndex)].Position.xyz;
    vec4 World    = Instance.World         * vec4(Object, 1.0);
    vec4 Previous = Instance.PreviousWorld * vec4(Object, 1.0);

    vec4 Clip = ViewClip * World;
    OutCurrentClip  = Clip;
    OutPreviousClip = PreviousViewClip * Previous;
    OutCluster      = ClusterIndex;
    OutTexcoord     = Vertices[uint(gl_VertexIndex)].Texcoord.xy;

    Clip.xy    += Jitter.xy * Clip.w;   // sub-pixel jitter (AA) on the rasterised position only
    gl_Position = Clip;
}
