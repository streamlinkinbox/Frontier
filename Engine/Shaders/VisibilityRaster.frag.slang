//============================================================================================================================================
// 📦 Engine/Shaders/VisibilityRaster.frag.slang — Visibility-buffer fragment stage: visibility id + motion vector
//============================================================================================================================================
// Attachments: 0 = R32_UINT visibility id (instance << 14 | primitive), 1 = RG16F motion (uv delta current − previous,
//    in [0,1] image space, y down), depth = D32 reverse-Z. Primitive id = cluster's first primitive + gl_PrimitiveID
//    (gl_PrimitiveID restarts at 0 for every draw). No colour, no derivatives — everything else is resolved later.
// R4b: alpha-masked materials (kMaterialFlagAlphaMask) discard fragments whose geometry_opacity × base-colour alpha is
//    below AlphaCutoff, so foliage cards leave holes in the visibility buffer exactly where the kernel's shadow rays see
//    them. Bindings 5 (materials), 6 (slabs) and 7 (bindless Textures[]) exist for this test only; LOD = hardware gradient.
// NOTE: GLSL source lowered by glslc -fshader-stage=fragment.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "Shaders/SceneRecords.slang"

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer ClusterExtent  { ClusterRecord  Clusters[];  };
layout(std430, set = 0, binding = 5) readonly buffer MaterialExtent     { MaterialRecord     Materials[];     };
layout(std430, set = 0, binding = 6) readonly buffer MaterialSlabExtent { MaterialSlabRecord MaterialSlabs[]; };
layout(set = 0, binding = 7) uniform sampler2D Textures[];   // partially bound; only resident slots are indexed

layout(location = 0) flat in uint  InCluster;
layout(location = 1)      in vec4  InPreviousClip;
layout(location = 2)      in vec4  InCurrentClip;
layout(location = 3)      in vec2  InTexcoord;

layout(location = 0) out uint OutVisibility;
layout(location = 1) out vec2 OutMotion;

void main()
{
    ClusterRecord Cluster = Clusters[InCluster];
    MaterialRecord Material = Materials[Instances[Cluster.InstanceIndex].MaterialIndex];
    if ((Material.Slabs.z & kMaterialFlagAlphaMask) != 0u && Material.Textures.x != kMaterialTextureNone)
    {
        float Opacity = (Material.Slabs.y > 0u ? MaterialSlabs[Material.Slabs.x].Geometry.y : 1.0)   // geometry_opacity
                      * texture(Textures[nonuniformEXT(Material.Textures.x)], InTexcoord).a;
        if (Opacity < Material.AlphaCutoff) discard;
    }
    OutVisibility = PackVisibility(Cluster.InstanceIndex, Cluster.FirstPrimitive + uint(gl_PrimitiveID));

    vec2 CurrentUv  = InCurrentClip.xy  / InCurrentClip.w  * 0.5 + 0.5;
    vec2 PreviousUv = InPreviousClip.xy / InPreviousClip.w * 0.5 + 0.5;
    OutMotion = CurrentUv - PreviousUv;
}
