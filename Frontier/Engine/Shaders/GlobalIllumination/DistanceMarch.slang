//============================================================================================================================================
//                                                          DISTANCEMARCH.SLANG
//============================================================================================================================================
// 🧩 Sphere march of the ONE scene distance field — four nested toroidal cascades, never a walk over occupants.

#pragma once
#include "IntrinsicParity.slang"

// 📝 Group 1 is the scene distance field and nothing else. Every ray in the exhibit — indirect candidates, reuse
//    visibility, card shadows, card radiosity, reflections, the inspection march — is answered by this file.
//
//    A cascade is a Resolution³ lattice of rgba16float texels addressed toroidally: world voxel v lives at texel
//    v mod Resolution, so the window scrolls with the camera by rewriting only the slabs it newly covers. Because the
//    sampler repeats, uvw = position / (Resolution · VoxelExtent) lands on the right texel with no indirection.
//    Texel .x is the truncated signed distance [m]; .y is the identity of the occupant nearest the voxel centre.

#include "DistanceLayouts.slang"

  [[vk::binding(0, 1)]] ConstantBuffer<DistanceConstants> Distance;
  [[vk::binding(1, 1)]] Texture3D<float4>        SceneClip0;
  [[vk::binding(2, 1)]] Texture3D<float4>        SceneClip1;
  [[vk::binding(3, 1)]] Texture3D<float4>        SceneClip2;
  [[vk::binding(4, 1)]] Texture3D<float4>        SceneClip3;
  [[vk::binding(5, 1)]] SamplerState      SceneSampler;
  [[vk::binding(6, 1)]] StructuredBuffer<uint> BrickEpochs;

//------------------------------------------------------------------------------------------------------------------------
//                                                  CASCADE ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

float4 SampleClipTexel(uint Level, float3 Uvw)
{
    switch (Level)
    {
        case 0u:      { return SceneClip0.SampleLevel(SceneSampler, Uvw, 0.0); } break;
        case 1u:      { return SceneClip1.SampleLevel(SceneSampler, Uvw, 0.0); } break;
        case 2u:      { return SceneClip2.SampleLevel(SceneSampler, Uvw, 0.0); } break;
        default:      { return SceneClip3.SampleLevel(SceneSampler, Uvw, 0.0); } break;
    }
}

float4 LoadClipTexel(uint Level, int3 Texel)
{
    switch (Level)
    {
        case 0u:      { return LoadTexel(SceneClip0, Texel, 0); } break;
        case 1u:      { return LoadTexel(SceneClip1, Texel, 0); } break;
        case 2u:      { return LoadTexel(SceneClip2, Texel, 0); } break;
        default:      { return LoadTexel(SceneClip3, Texel, 0); } break;
    }
}

bool ClipContains(uint Level, float3 Position, float MarginVoxels)
{
    let Clip = Distance.Clips[Level];
    let Voxel = Position / Clip.VoxelExtent;
    let Low = float3(Clip.OriginVoxel) + float3(MarginVoxels);
    let High = float3(Clip.OriginVoxel) + float3(float(Clip.Resolution) - MarginVoxels);
    return all(Voxel >= Low) && all(Voxel <= High);
}

uint ResolveFinestClip(float3 Position, float MarginVoxels)
{
    for (var Level = 0u; Level < Distance.ClipCount; Level++)
    {
        if (ClipContains(Level, Position, MarginVoxels))
        {
            return Level;
        }
    }
    return Distance.ClipCount;
}

float SampleClipDistance(uint Level, float3 Position)
{
    return SampleClipTexel(Level, Position * Distance.Clips[Level].InverseWorldExtent).x;
}

uint ResolveClipIdentity(uint Level, float3 Position)
{
    let Clip = Distance.Clips[Level];
    let Voxel = int3(floor(Position / Clip.VoxelExtent));
    let Resolution = ToSigned(Clip.Resolution);
    let Texel = ((Voxel % Resolution) + Resolution) % Resolution;
    return ToUnsigned(LoadClipTexel(Level, Texel).y + 0.5);
}

// 📝 Central-difference gradient over ±¾ voxel; the field is a distance, so its gradient is the surface normal.
float3 ResolveClipNormal(uint Level, float3 Position)
{
    let Step = Distance.Clips[Level].VoxelExtent * 0.75;
    let Gradient = float3(SampleClipDistance(Level, Position + float3(Step, 0.0, 0.0)) - SampleClipDistance(Level, Position - float3(Step, 0.0, 0.0)),
                         SampleClipDistance(Level, Position + float3(0.0, Step, 0.0)) - SampleClipDistance(Level, Position - float3(0.0, Step, 0.0)),
                         SampleClipDistance(Level, Position + float3(0.0, 0.0, Step)) - SampleClipDistance(Level, Position - float3(0.0, 0.0, Step)));
    let Length = length(Gradient);
    return SelectBranch(float3(0.0, 0.0, 1.0), Gradient / Length, Length > 1.0e-6);
}

// 📝 Distance at a point from the finest cascade holding it; the analytic plane answers outside every cascade.
float2 SampleSceneDistance(float3 Position)
{
    let Level = ResolveFinestClip(Position, 1.0);
    if (Level >= Distance.ClipCount)
    {
        return float2(Position.z - Distance.GroundHeight, float(Distance.ClipCount));
    }
    return float2(SampleClipDistance(Level, Position), float(Level));
}

// 📝 Normal-and-stride bias that lifts a ray origin off the surface it leaves, scaled by the cascade there.
float ResolveSurfaceBias(float3 Position)
{
    let Level = min(ResolveFinestClip(Position, 2.0), Distance.ClipCount - 1u);
    return Distance.Clips[Level].VoxelExtent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SPHERE TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

struct MarchHit
{
    float3 Position;    // [m]
    float Travel;        // [m]   ray parameter at the surface
    float3 Normal;      // [-]   distance gradient
    uint Identity;      // [-]   occupant, GroundIdentity, or SkyIdentity on a miss
    uint Level;         // [-]   cascade that answered; ClipCount for the far-field plane
    uint Steps;         // [-]
    bool Hit;      
};

// 📝 ① sample the finest cascade holding the point; ② a saturated sample (no surface within that cascade's band)
//    asks the next coarser cascade, whose band is twice as long, for a longer conservative stride; ③ the surface is
//    declared within a voxel fraction, widened along the ray by an optional cone; ④ past the outermost cascade the
//    analytic course plane is the far field. A march that exhausts its steps reports a hit rather than leaking.
MarchHit TraverseSceneDistance(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel, float ConeSlope)
{
    var Result : MarchHit = {};
    Result.Hit      = false;
    Result.Identity = SkyIdentity;
    Result.Level    = Distance.ClipCount;
    Result.Steps    = 0u;
    Result.Normal   = -Direction;
    Result.Travel   = MaximumTravel;
    Result.Position = Origin + Direction * MaximumTravel;

    var Travel = MinimumTravel;
    var Escaped = false;
    var Exhausted = true;
    for (var Step = 0u; Step < MaximumMarchSteps; Step++)
    {
        let Position = Origin + Direction * Travel;
        let Level = ResolveFinestClip(Position, 2.0);
        if (Level >= Distance.ClipCount)
        {
            Escaped = true;
            Exhausted = false;
            break;
        }
        let Clip = Distance.Clips[Level];
        var Sampled = SampleClipDistance(Level, Position);
        var Saturation = Clip.Band * 0.9;
        var Coarser = Level + 1u;
        while (Sampled >= Saturation && Coarser < Distance.ClipCount)
        {
            if (!ClipContains(Coarser, Position, 2.0))
            {
                break;
            }
            let CoarseClip = Distance.Clips[Coarser];
            Sampled = max(Sampled, SampleClipDistance(Coarser, Position) - 0.5 * CoarseClip.VoxelExtent);
            Saturation = CoarseClip.Band * 0.9;
            Coarser++;
        }
        Result.Steps = Step + 1u;
        let Threshold = max(Clip.VoxelExtent * Distance.HitFraction, Travel * ConeSlope);
        if (Sampled < Threshold)
        {
            Result.Hit      = true;
            Result.Travel   = Travel;
            Result.Position = Position;
            Result.Level    = Level;
            Exhausted = false;
            break;
        }
        Travel += max(Sampled, Clip.VoxelExtent * 0.125);
        if (Travel > MaximumTravel)
        {
            Exhausted = false;
            break;
        }
    }

    if (Result.Hit)
    {
        Result.Normal   = ResolveClipNormal(Result.Level, Result.Position);
        let Inward = Result.Position - Result.Normal * (0.5 * Distance.Clips[Result.Level].VoxelExtent);
        Result.Identity = ResolveClipIdentity(Result.Level, Inward);
        return Result;
    }
    if (Exhausted)
    {
        let Position = Origin + Direction * Travel;
        let Level = min(ResolveFinestClip(Position, 2.0), Distance.ClipCount - 1u);
        Result.Hit      = true;
        Result.Travel   = Travel;
        Result.Position = Position;
        Result.Level    = Level;
        Result.Normal   = ResolveClipNormal(Level, Position);
        Result.Identity = ResolveClipIdentity(Level, Position);
        return Result;
    }
    if (Escaped && Direction.z < -1.0e-4)
    {
        let GroundTravel = (Distance.GroundHeight - Origin.z) / Direction.z;
        if (GroundTravel >= MinimumTravel && GroundTravel <= MaximumTravel)
        {
            Result.Hit      = true;
            Result.Travel   = GroundTravel;
            Result.Position = Origin + Direction * GroundTravel;
            Result.Normal   = float3(0.0, 0.0, 1.0);
            Result.Identity = GroundIdentity;
            Result.Level    = Distance.ClipCount;
        }
    }
    return Result;
}

// 📝 Binary visibility along a segment, for reuse validation: 1 when nothing intervenes before MaximumTravel.
float TraverseSceneVisibility(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel)
{
    let Hit = TraverseSceneDistance(Origin, Direction, MinimumTravel, MaximumTravel, 0.0);
    return SelectBranch(1.0, 0.0, Hit.Hit && Hit.Travel < MaximumTravel);
}

// 📝 Penumbra-estimating visibility toward a distant light: min over the march of k·d/t (Quílez), clamped to [0, 1].
//    Used for card shadows, where a soft cascade-resolution penumbra is the honest answer.
float TraverseSceneShadow(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel, float Hardness)
{
    var Travel = MinimumTravel;
    var Visibility = 1.0;
    for (var Step = 0u; Step < MaximumMarchSteps; Step++)
    {
        let Position = Origin + Direction * Travel;
        let Level = ResolveFinestClip(Position, 2.0);
        if (Level >= Distance.ClipCount)
        {
            if (Direction.z < -1.0e-4)
            {
                return 0.0;
            }
            return Visibility;
        }
        let Clip = Distance.Clips[Level];
        var Sampled = SampleClipDistance(Level, Position);
        var Saturation = Clip.Band * 0.9;
        var Coarser = Level + 1u;
        while (Sampled >= Saturation && Coarser < Distance.ClipCount)
        {
            if (!ClipContains(Coarser, Position, 2.0))
            {
                break;
            }
            let CoarseClip = Distance.Clips[Coarser];
            Sampled = max(Sampled, SampleClipDistance(Coarser, Position) - 0.5 * CoarseClip.VoxelExtent);
            Saturation = CoarseClip.Band * 0.9;
            Coarser++;
        }
        if (Sampled < Clip.VoxelExtent * Distance.HitFraction)
        {
            return 0.0;
        }
        Visibility = min(Visibility, Hardness * Sampled / max(Travel, 1.0e-3));
        Travel += max(Sampled, Clip.VoxelExtent * 0.125);
        if (Travel > MaximumTravel)
        {
            break;
        }
    }
    return clamp(Visibility, 0.0, 1.0);
}
