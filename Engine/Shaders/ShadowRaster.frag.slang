//============================================================================================================================================
// 📦 Engine/Shaders/ShadowRaster.frag.slang — Shadow-map fragment stage: depth only
//============================================================================================================================================
// No colour attachment at all; the depth buffer IS the product. The one thing this stage must still do is honour
//    alpha-masked materials: a foliage card that the primary raster punches holes in has to punch the same holes in
//    its shadow, or the cut-out leaves a solid silhouette on the floor. Bindings 5/6/7 exist for that test alone and
//    mirror VisibilityRaster.frag's use of them exactly.
// NOTE: GLSL source lowered by glslc -fshader-stage=fragment.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "Shaders/SceneRecords.slang"

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent      { InstanceRecord     Instances[];     };
layout(std430, set = 0, binding = 2) readonly buffer ClusterExtent       { ClusterRecord      Clusters[];      };
layout(std430, set = 0, binding = 5) readonly buffer MaterialExtent      { MaterialRecord     Materials[];     };
layout(std430, set = 0, binding = 6) readonly buffer MaterialSlabExtent  { MaterialSlabRecord MaterialSlabs[]; };
layout(set = 0, binding = 7) uniform sampler2D Textures[];   // partially bound; only resident slots are indexed

layout(location = 0) flat in uint InCluster;
layout(location = 1)      in vec2 InTexcoord;

void main()
{
    ClusterRecord  Cluster  = Clusters[InCluster];
    MaterialRecord Material = Materials[Instances[Cluster.InstanceIndex].MaterialIndex];
    if ((Material.Slabs.z & kMaterialFlagAlphaMask) != 0u && Material.Textures.x != kMaterialTextureNone)
    {
        float Opacity = (Material.Slabs.y > 0u ? MaterialSlabs[Material.Slabs.x].Geometry.y : 1.0)   // geometry_opacity
                      * texture(Textures[nonuniformEXT(Material.Textures.x)], InTexcoord).a;
        if (Opacity < Material.AlphaCutoff) discard;
    }
    // Depth is written by fixed function; nothing else to do.
}
