//============================================================================================================================================
//                                                          INSPECTIONMARCH.SLANG
//============================================================================================================================================
// 🧩 Primary rays through the scene distance field alone — the field, its stride cost, and the cards as the tracers see them.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 DistanceInspection selects what each camera ray reports:
//    1  surface normal of the field, tinted by the cascade that answered, with bricks rewritten in the last
//       half-second glowing — the field updating around the moving vehicle is visible here directly;
//    2  traversal steps as a heat ramp;
//    3  card radiance at the hit (Lumen's "surface cache" view): the scene exactly as every indirect ray sees it.
//    With ray tracing on, the same three readings come from the BVH: its hits, its traversal steps, and next-event light.

#include "CardSampling.slang"
#include "SurfaceReflectance.slang"
#include "DiscrepancySequence.slang"
#include "RayIntersection.slang"

  [[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> InspectionTarget;

static const float3 CascadeTints[5] = {float3(1.0, 0.92, 0.80), float3(0.70, 0.90, 1.0), float3(0.78, 1.0, 0.72), float3(1.0, 0.75, 0.95),
                                       float3(0.6, 0.6, 0.6)};

float3 ProjectHeat(float Fraction)
{
    let Clamped = clamp(Fraction, 0.0, 1.0);
    return clamp(float3(1.5 * Clamped, 1.5 * Clamped - 0.4, 0.6 - 1.2 * Clamped) + float3(0.0, 0.0, 0.3 * (1.0 - Clamped)), float3(0.0), float3(1.0));
}

float2 ResolveHitBrickAge(MarchHit Hit)
{
    if (Hit.Level >= Distance.ClipCount)
    {
        return float2(1.0e9, 0.0);
    }
    let Voxel = int3(floor(Hit.Position / Distance.Clips[Hit.Level].VoxelExtent));
    let Brick = int3(floor(float3(Voxel) / float(BrickEdge)));
    return ResolveBrickAge(BrickEpochs[ResolveBrickSlot(Hit.Level, Brick, Distance.BricksPerAxis)], Refresh.RefreshIndex);
}

float3 ResolveEscapeShade(float3 Direction)
{
    return lerp(float3(0.10, 0.11, 0.14), float3(0.22, 0.26, 0.34), clamp(Direction.z * 2.0 + 0.5, 0.0, 1.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void MarchInspection(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = Invocation.xy;
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Far = ReconstructWorldPosition(float2(Pixel) + 0.5, 1.0e-6);
    let Direction = normalize(Far - Refresh.CameraPosition);
    var Colour = float3(0.0);

    if (FeatureEnabled(Refresh, FeatureRayTracing))
    {
        let RtHit = TraceSceneRay(Refresh.CameraPosition, Direction, 0.01, 600.0);
        switch (ResolveDistanceInspectionMode(Refresh))
        {
            case 2u:
            {
                Colour = ProjectHeat(float(RtHit.Steps) / 96.0);
            }
            break;
            case 3u:
            {
                if (RtHit.Hit)
                {
                    var Seed = ConstructPixelSeed(Pixel, Refresh.RefreshIndex, 53u);
                    let Surface = ProjectSurfaceSample(RayMaterials[RtHit.Family]);
                    Colour = Surface.Incandescence * Refresh.IncandescenceScale
                           + EvaluateDirectNextEvent(RtHit.Position, RtHit.Normal, -Direction, Surface, Seed, true);
                }
                else
                {
                    Colour = SampleSkyRadiance(Direction);
                }
            }
            break;
            default:
            {
                if (RtHit.Hit)
                {
                    let Surface = ProjectSurfaceSample(RayMaterials[RtHit.Family]);
                    let Shade = 0.35 + 0.65 * max(dot(RtHit.Normal, normalize(float3(0.4, 0.3, 0.85))), 0.0);
                    Colour = lerp(Surface.Albedo, RtHit.Normal * 0.5 + 0.5, 0.55) * Shade;
                    if (RtHit.Identity < ArrayCount(Occupants))
                    {
                        let Occupant = Occupants[RtHit.Identity];
                        if ((Occupant.Participation & ParticipationDynamic) != 0u)
                        {
                            Colour = lerp(Colour, float3(0.25, 0.90, 0.70), 0.30);
                        }
                    }
                    let Lattice = frac(RtHit.Position * 2.0);
                    let Edge = min(min(min(Lattice.x, 1.0 - Lattice.x), min(Lattice.y, 1.0 - Lattice.y)), min(Lattice.z, 1.0 - Lattice.z));
                    Colour *= lerp(0.84, 1.0, smoothstep(0.0, 0.05, Edge));
                }
                else
                {
                    Colour = ResolveEscapeShade(Direction);
                }
            }
            break;
        }
        InspectionTarget[int2(Pixel)] = float4(Colour, 1.0);
        return;
    }

    let Hit = TraverseSceneDistance(Refresh.CameraPosition, Direction, 0.0, 600.0, 0.0);
    switch (ResolveDistanceInspectionMode(Refresh))
    {
        case 2u:
        {
            Colour = ProjectHeat(float(Hit.Steps) / float(MaximumMarchSteps));
        }
        break;
        case 3u:
        {
            Colour = ResolveMarchRadiance(Hit, Direction);
        }
        break;
        default:
        {
            if (Hit.Hit)
            {
                let Shade = 0.35 + 0.65 * max(dot(Hit.Normal, normalize(float3(0.4, 0.3, 0.85))), 0.0);
                Colour = (Hit.Normal * 0.5 + 0.5) * CascadeTints[min(Hit.Level, 4u)] * Shade;
                let Age = ResolveHitBrickAge(Hit);
                if (Age.y > 0.5 && Age.x < 24.0)
                {
                    Colour = lerp(Colour, float3(1.0, 0.15, 0.85), 0.7 * (1.0 - Age.x / 24.0));
                }
                else if (Age.x < 6.0)
                {
                    Colour = lerp(Colour, float3(0.2, 0.85, 0.9), 0.3 * (1.0 - Age.x / 6.0));
                }
                // 📝 Voxel lattice lines of the answering cascade, so its resolution reads at a glance.
                let Lattice = frac(Hit.Position / Distance.Clips[min(Hit.Level, Distance.ClipCount - 1u)].VoxelExtent);
                let Edge = min(min(min(Lattice.x, 1.0 - Lattice.x), min(Lattice.y, 1.0 - Lattice.y)), min(Lattice.z, 1.0 - Lattice.z));
                Colour *= lerp(0.82, 1.0, smoothstep(0.0, 0.06, Edge));
            }
            else
            {
                Colour = ResolveEscapeShade(Direction);
            }
        }
        break;
    }
    InspectionTarget[int2(Pixel)] = float4(Colour, 1.0);
}
