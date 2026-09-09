//============================================================================================================================================
// 📦 Project-Zero/Source/TracingIndex.h — Analytical Triangle Geometry, Rays, and Photometric Material Topology
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../../../Engine/DeviceExchange/OrientationClassifier.h"
#include <cstdint>
#include <vector>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                 ANALYTICAL MATERIAL
//------------------------------------------------------------------------------------------------------------------------

struct AnalyticalMaterial
{
    Vector3                 AlbedoColor;                        // [0..1] diffuse surface reflectance
    Vector3                 EmissiveRadiance;                   // [lux] self-emitted luminous radiance
    float                   RoughnessValue;                     // [0..1] microfacet surface roughness
    float                   MetallicValue;                      // [0..1] conductor or dielectric parameter
    uint32_t                MaterialIdentifier;                 // [id] unique material index
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 TRIANGLE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct TriangleGeometry
{
    Vector3                 VertexAlpha;                        // [m] first triangle vertex
    Vector3                 VertexBeta;                         // [m] second triangle vertex
    Vector3                 VertexGamma;                        // [m] third triangle vertex
    Vector3                 SurfaceNormal;                      // [-] geometric surface normal
    uint32_t                MaterialIndex;                      // [index] assigned material slot
    uint32_t                TriangleIndex;                      // [index] unique primitive index
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RAY RECORD
//------------------------------------------------------------------------------------------------------------------------

struct RayStructure
{
    Vector3                 SpatialOrigin;                      // [m] ray emission coordinate
    Vector3                 RayDirection;                       // [-] normalized ray vector
    float                   MinimumDistance;                    // [m] ray near clipping distance
    float                   MaximumDistance;                    // [m] ray far clipping distance
};

//------------------------------------------------------------------------------------------------------------------------
//                                               HIT INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

struct HitIntersection
{
    Vector3                 HitLocation;                        // [m] surface point in 3D world space
    Vector3                 SurfaceNormal;                      // [-] unit geometric normal at hit point
    float                   RayDistance;                        // [m] parametric distance along ray
    uint32_t                MaterialIndex;                      // [index] hit material index
    uint32_t                TriangleIndex;                      // [index] hit triangle primitive index
    bool                    ValidCondition;                     // [bool] true if ray intersected geometry
};

} // namespace Frontier::ProjectZero
