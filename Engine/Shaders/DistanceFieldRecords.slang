//============================================================================================================================================
// 📦 Engine/Shaders/DistanceFieldRecords.slang — Signed Distance Field records and ray-marching utilities (include only)
//============================================================================================================================================

#ifndef FRONTIER_DISTANCE_FIELD_RECORDS
#define FRONTIER_DISTANCE_FIELD_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                           DISTANCE FIELD CONSTANTS (std140)
//------------------------------------------------------------------------------------------------------------------------

layout(std140, set = 2, binding = 0) uniform DistanceFieldConstants
{
    vec4 DistanceFieldBoundMin;         // xyz = [m] bounding minimum, w = voxel spacing x
    vec4 DistanceFieldBoundMax;         // xyz = [m] bounding maximum, w = voxel spacing y
    uvec4 DistanceFieldGridDimensions;  // xyz = [voxels] grid dimensions, w = max ray march steps
    vec4 DistanceFieldMarchParameters;  // x = step relaxation, y = surface threshold [m], z = penumbra light angle [rad], w = transition dist [m]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                RAY HIT STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

struct DistanceFieldHit
{
    vec3 HitPosition;                   // [m] world or object space hit point
    vec3 SurfaceNormal;                 // [-] outward surface normal vector
    float TravelDistance;               // [m] ray parametric distance
    uint StepCount;                     // [-] sphere tracing steps taken
    bool HasHit;                        // [-] true when surface threshold intersected
};

//------------------------------------------------------------------------------------------------------------------------
//                                            DISTANCE SAMPLING UTILITY
//------------------------------------------------------------------------------------------------------------------------

float SampleDistanceFieldVolume(sampler3D VolumeSampler, vec3 SpatialPosition, vec3 BMin, vec3 BMax)
{
    vec3 UVW = (SpatialPosition - BMin) / (BMax - BMin);
    if (any(lessThan(UVW, vec3(0.0))) || any(greaterThan(UVW, vec3(1.0))))
    {
        return 1e4;
    }
    return texture(VolumeSampler, UVW).r;
}

vec3 SampleDistanceFieldNormal(sampler3D VolumeSampler, vec3 SpatialPosition, vec3 BMin, vec3 BMax, float Epsilon)
{
    float DX = SampleDistanceFieldVolume(VolumeSampler, SpatialPosition + vec3(Epsilon, 0.0, 0.0), BMin, BMax)
             - SampleDistanceFieldVolume(VolumeSampler, SpatialPosition - vec3(Epsilon, 0.0, 0.0), BMin, BMax);
    float DY = SampleDistanceFieldVolume(VolumeSampler, SpatialPosition + vec3(0.0, Epsilon, 0.0), BMin, BMax)
             - SampleDistanceFieldVolume(VolumeSampler, SpatialPosition - vec3(0.0, Epsilon, 0.0), BMin, BMax);
    float DZ = SampleDistanceFieldVolume(VolumeSampler, SpatialPosition + vec3(0.0, 0.0, Epsilon), BMin, BMax)
             - SampleDistanceFieldVolume(VolumeSampler, SpatialPosition - vec3(0.0, 0.0, Epsilon), BMin, BMax);
    vec3 Grad = vec3(DX, DY, DZ);
    float Len = length(Grad);
    return (Len > 1e-6) ? (Grad / Len) : vec3(0.0, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SPHERE TRACING SOFT SHADOW
//------------------------------------------------------------------------------------------------------------------------

float MarchDistanceFieldSoftShadow(sampler3D VolumeSampler,
                                   vec3 RayOrigin,
                                   vec3 LightDirection,
                                   vec3 BMin,
                                   vec3 BMax,
                                   float MinDist,
                                   float MaxDist,
                                   float LightAngleRad,
                                   uint MaxSteps)
{
    float ShadowFactor = 1.0;
    float CurrentDist = MinDist;
    float PenumbraScale = 1.0 / max(0.01, tan(LightAngleRad));

    for (uint Step = 0u; Step < MaxSteps && CurrentDist < MaxDist; ++Step)
    {
        vec3 P = RayOrigin + LightDirection * CurrentDist;
        if (any(lessThan(P, BMin)) || any(greaterThan(P, BMax)))
        {
            CurrentDist += 0.05;
            continue;
        }

        float D = SampleDistanceFieldVolume(VolumeSampler, P, BMin, BMax);
        if (D <= 0.001)
        {
            return 0.0;
        }

        float StepPenumbra = PenumbraScale * D / CurrentDist;
        ShadowFactor = min(ShadowFactor, StepPenumbra);
        CurrentDist += max(D * 0.85, 0.004);
    }

    return clamp(ShadowFactor, 0.0, 1.0);
}

#endif // FRONTIER_DISTANCE_FIELD_RECORDS
