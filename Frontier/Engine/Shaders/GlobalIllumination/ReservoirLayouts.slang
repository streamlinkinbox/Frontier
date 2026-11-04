//============================================================================================================================================
//                                                         RESERVOIRLAYOUTS.SLANG
//============================================================================================================================================
// 🧩 ReSTIR GI reservoirs (Ouyang et al. 2021): the reconnection vertex, its radiance, W and M, and weighted merging.

#pragma once
#include "IntrinsicParity.slang"

// 📝 A sample is the secondary vertex a cosine ray from a primary surface reached in the scene distance field: its
//    position, its distance-field normal, the card radiance it sends back and the occupant it belongs to. A ray that
//    escaped stores its direction pushed far away and SkyIdentity, and is treated directionally under every shift.
//
//    32 bytes per reservoir: position 12, packed normal 4, packed half radiance + identity 8, W 4, M 4.

#include "PackingCodec.slang"
#include "DistanceLayouts.slang"

struct PackedReservoir
{
    float3 SamplePosition;    // [m]   reconnection vertex x_s
    uint SampleNormal;        // [-]   octahedral n_s
    uint2 Radiance;          // [-]   half rgb radiance, identity in the fourth half
    float Weight;              // [-]   unbiased contribution weight W
    float Tally;               // [-]   M
};

struct ReservoirSample
{
    float3 Position; 
    float3 Normal;   
    float3 Radiance; 
    uint Identity; 
};

struct Reservoir
{
    ReservoirSample Sample;    
    float WeightSum; 
    float Tally;     
    float Weight;    
    bool Adopted;      // the selected sample came from another pixel's reservoir
};

static const float SkyReach = 10000.0;   // [m] - how far an escaped direction is pushed to become a position

Reservoir ConstructEmptyReservoir()
{
    var Empty : Reservoir = {};
    Empty.Sample.Position = float3(0.0);
    Empty.Sample.Normal   = float3(0.0, 0.0, 1.0);
    Empty.Sample.Radiance = float3(0.0);
    Empty.Sample.Identity = SkyIdentity;
    Empty.WeightSum = 0.0;
    Empty.Tally     = 0.0;
    Empty.Weight    = 0.0;
    Empty.Adopted   = false;
    return Empty;
}

PackedReservoir PackReservoir(Reservoir Source)
{
    var Record : PackedReservoir = {};
    Record.SamplePosition = Source.Sample.Position;
    Record.SampleNormal   = PackOctahedral(Source.Sample.Normal);
    Record.Radiance       = PackHalfTriple(min(Source.Sample.Radiance, float3(60000.0)), float(Source.Sample.Identity));
    Record.Weight         = Source.Weight;
    Record.Tally          = Source.Tally;
    return Record;
}

Reservoir UnpackReservoir(PackedReservoir Record)
{
    var Result = ConstructEmptyReservoir();
    let Radiance = UnpackHalfTriple(Record.Radiance);
    Result.Sample.Position = Record.SamplePosition;
    Result.Sample.Normal   = UnpackOctahedral(Record.SampleNormal);
    Result.Sample.Radiance = Radiance.rgb;
    Result.Sample.Identity = ToUnsigned(Radiance.w + 0.5);
    Result.Weight = Record.Weight;
    Result.Tally  = Record.Tally;
    return Result;
}

// 📝 Weighted reservoir sampling: accumulate w into Σw and M into the tally, keep the candidate with probability w/Σw.
bool MergeReservoir(inout Reservoir Target, ReservoirSample Candidate, float Weight, float Tally, float Random)
{
    (Target).WeightSum += Weight;
    (Target).Tally     += Tally;
    if (Weight > 0.0 && Random * (Target).WeightSum < Weight)
    {
        (Target).Sample = Candidate;
        return true;
    }
    return false;
}

float3 ResolveSampleDirection(ReservoirSample Sample, float3 Position)
{
    return normalize(Sample.Position - Position);
}

// 📝 Target function p̂(y) = luminance(L(y)) · max(0, n_v · ω) at the shading point (x_v, n_v): the integrand of the
//    irradiance without the visibility term, which the reuse dispatches test explicitly where they need it.
float ResolveReservoirTarget(ReservoirSample Sample, float3 Position, float3 Normal)
{
    let Cosine = dot(Normal, ResolveSampleDirection(Sample, Position));
    return SelectBranch(0.0, ResolveLuminance(Sample.Radiance) * Cosine, Cosine > 0.0);
}
