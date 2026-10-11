//============================================================================================================================================
// 📦 Engine/Shaders/GlobalDistanceField.slang — Global Distance Field compositing and scene traversal (include only)
//============================================================================================================================================

#ifndef FRONTIER_GLOBAL_DISTANCE_FIELD
#define FRONTIER_GLOBAL_DISTANCE_FIELD

#include "DistanceFieldRecords.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                           GLOBAL DISTANCE FIELD BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(set = 2, binding = 1) uniform sampler3D GlobalDistanceFieldSampler;
layout(set = 2, binding = 2) uniform sampler3D MeshDistanceFieldSampler;

//------------------------------------------------------------------------------------------------------------------------
//                                            TWO-TIER SCENE RAY MARCH
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldHit MarchSceneGlobalDistanceField(vec3 RayOrigin,
                                               vec3 RayDirection,
                                               vec3 GlobalMin,
                                               vec3 GlobalMax,
                                               vec3 MeshMin,
                                               vec3 MeshMax,
                                               float MinDist,
                                               float MaxDist,
                                               float SurfaceThreshold,
                                               uint MaxSteps,
                                               float StepRelaxation,
                                               float TransitionDistance)
{
    DistanceFieldHit Hit;
    Hit.TravelDistance = MinDist;
    Hit.StepCount = 0u;
    Hit.HasHit = false;

    float CurrentDist = MinDist;

    for (uint Step = 0u; Step < MaxSteps && CurrentDist < MaxDist; ++Step)
    {
        Hit.StepCount++;
        vec3 P = RayOrigin + RayDirection * CurrentDist;

        // Ground plane collision at z = 0
        if (P.z <= SurfaceThreshold && RayDirection.z < -1e-4)
        {
            float HitT = (0.0 - RayOrigin.z) / RayDirection.z;
            if (HitT >= MinDist && HitT <= MaxDist)
            {
                Hit.HasHit = true;
                Hit.TravelDistance = HitT;
                Hit.HitPosition = RayOrigin + RayDirection * HitT;
                Hit.SurfaceNormal = vec3(0.0, 0.0, 1.0);
                return Hit;
            }
        }

        // Sample coarse global distance field
        float CoarseD = SampleDistanceFieldVolume(GlobalDistanceFieldSampler, P, GlobalMin, GlobalMax);

        // Accelerated coarse step through empty space
        if (CoarseD > TransitionDistance)
        {
            CurrentDist += CoarseD * StepRelaxation;
            continue;
        }

        // Near surface: query local mesh distance field
        float FineD = SampleDistanceFieldVolume(MeshDistanceFieldSampler, P, MeshMin, MeshMax);

        if (FineD <= SurfaceThreshold)
        {
            Hit.HasHit = true;
            Hit.TravelDistance = CurrentDist;
            Hit.HitPosition = P;
            Hit.SurfaceNormal = SampleDistanceFieldNormal(MeshDistanceFieldSampler, P, MeshMin, MeshMax, 0.005);
            return Hit;
        }

        float SafeAdvance = max(FineD * StepRelaxation, SurfaceThreshold * 0.5);
        CurrentDist += SafeAdvance;
    }

    return Hit;
}

#endif // FRONTIER_GLOBAL_DISTANCE_FIELD
