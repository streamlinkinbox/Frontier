//============================================================================================================================================
// 📦 Engine/Shaders/SurfaceCacheRecords.slang — Surface Cache card layout, atlas packing, and radiance queries (include only)
//============================================================================================================================================

#ifndef FRONTIER_SURFACE_CACHE_RECORDS
#define FRONTIER_SURFACE_CACHE_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                           SURFACE CACHE ATLAS BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(set = 3, binding = 0) uniform sampler2D SurfaceCacheDirectAtlas;     // rgb = direct lighting * shadow
layout(set = 3, binding = 1) uniform sampler2D SurfaceCacheIrradianceAtlas; // rgb = indirect multi-bounce irradiance
layout(set = 3, binding = 2) uniform sampler2D SurfaceCacheAlbedoAtlas;     // rgb = surface albedo reflectancy

const float kSurfaceCachePi = 3.14159265358979323846;

//------------------------------------------------------------------------------------------------------------------------
//                                                ATLAS SAMPLING
//------------------------------------------------------------------------------------------------------------------------

vec3 SampleSurfaceCacheRadiance(vec2 UV)
{
    vec3 Direct     = texture(SurfaceCacheDirectAtlas, UV).rgb;
    vec3 Irradiance = texture(SurfaceCacheIrradianceAtlas, UV).rgb;
    vec3 Albedo     = texture(SurfaceCacheAlbedoAtlas, UV).rgb;
    return (Direct + Irradiance) * Albedo;
}

vec3 SampleSurfaceCacheFromNormal(vec3 SurfaceNormal)
{
    float Longitude = atan(SurfaceNormal.y, SurfaceNormal.x);
    float U = (Longitude < 0.0 ? (Longitude + 2.0 * kSurfaceCachePi) : Longitude) / (2.0 * kSurfaceCachePi);
    float V = clamp(SurfaceNormal.z * 0.5 + 0.5, 0.0, 1.0);
    return SampleSurfaceCacheRadiance(vec2(U, V));
}

#endif // FRONTIER_SURFACE_CACHE_RECORDS
