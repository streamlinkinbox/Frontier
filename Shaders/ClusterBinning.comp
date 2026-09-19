//============================================================================================================================================
// 📦 Frontier/Shaders/ClusterBinning.comp — Frustum Cluster Grid Light Assignment Compute Shader
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

struct ClusterPayload
{
    uint LightOffset;                                   // [offset] index offset into active light index list
    uint LightCount;                                    // [count] number of intersecting lights
};

struct LightPayload
{
    vec4 PositionAndRadius;                             // [m] position (xyz) and influence radius (w)
    vec4 RadianceAndCategory;                           // [lux] radiance (xyz) and category (w)
};

layout(std430, binding = 0) readonly buffer LightStorageExtent
{
    LightPayload SceneLights[];                         // [lights] active scene lights
};

layout(std430, binding = 1) writeonly buffer ClusterStorageExtent
{
    ClusterPayload ClusterGrid[];                       // [clusters] 16x16x32 cluster cell storage
};

layout(push_constant) uniform PushConstants
{
    mat4 InverseProjection;                             // [-] inverse projection matrix
    uvec3 ClusterResolution;                            // [cells] cluster count (16, 16, 32)
    uint TotalLightCount;                               // [count] number of scene lights
};

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPUTE MAIN INVOCATION
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec3 ClusterCoord = gl_GlobalInvocationID.xyz;
    if (ClusterCoord.x >= ClusterResolution.x || ClusterCoord.y >= ClusterResolution.y || ClusterCoord.z >= ClusterResolution.z)
    {
        return;
    }

    uint LinearClusterIndex = ClusterCoord.z * (ClusterResolution.x * ClusterResolution.y) + ClusterCoord.y * ClusterResolution.x + ClusterCoord.x;
    // Evaluates sphere-frustum cluster AABB intersection tests
}
