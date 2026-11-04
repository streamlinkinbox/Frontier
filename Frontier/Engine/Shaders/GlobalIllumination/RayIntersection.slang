//============================================================================================================================================
//                                                         RAYINTERSECTION.SLANG
//============================================================================================================================================
// 🧩 Hardware-agnostic Two-Level BVH Ray-Triangle Intersection (TLAS over scene occupants + Binned-SAH BLAS per mesh),

#pragma once
#include "IntrinsicParity.slang"
//    Two-Layer PBR BSDF Importance Sampling, RIS Next-Event Estimation (Sun + IES Clustered Lights + Fire),
//    Multiple Importance Sampling (MIS), Russian Roulette, and Multi-Bounce Path Tracing for ReSTIR GI.

#include "CardSampling.slang"
#include "SurfaceReflectance.slang"
#include "PackingCodec.slang"
#include "DiscrepancySequence.slang"
#include "FireIllumination.slang"
#include "ClusteredLights.slang"
#include "ReservoirLayouts.slang"

struct BoundingEntry
{
    float3 Minimum;     
    uint LeftOrFirst; 
    float3 Maximum;     
    uint Count;       
};

  [[vk::binding(0, 3)]] StructuredBuffer<BoundingEntry>     BvhEntries;
  [[vk::binding(1, 3)]] StructuredBuffer<float4> BvhTriangles;
  [[vk::binding(2, 3)]] StructuredBuffer<MaterialProfile> RayMaterials;
  [[vk::binding(3, 3)]] ConstantBuffer<ClusteredLightRegister>          RayLights;
  [[vk::binding(4, 3)]] Texture2D<float4>            HmiTabletTexture;
  [[vk::binding(5, 3)]] Texture2D<float4>           HmiClusterTexture;

static const uint TlasLeafBit = 0x80000000u;
static const uint TlasIndexMask = 0x7FFFFFFFu;
static const uint EmptyTlasSentinel = 0x7FFFFFFFu;
static const uint BvhStackCapacity = 48u;
static const uint MaxBvhSteps = 384u;
static const float RayEpsilon = 2.0e-3;
static const float SolarAngularRadius = 0.0047;

struct RayHit
{
    float3 Position;    // [m]   world-space intersection point
    float Travel;        // [m]   ray parameter t
    float3 Normal;      // [-]   interpolated smooth shading normal in world space
    float3 Geometry;    // [-]   geometric triangle normal in world space
    uint Identity;      // [-]   Occupant index, GroundIdentity, or SkyIdentity on miss
    uint Family;        // [-]   exact triangle MaterialProfile index
    uint Steps;         // [-]   BVH march step count
    bool Hit;      
};

float3 SafeReciprocal(float3 Direction)
{
    let Safe = SelectBranch(Direction, float3(1.0e-9), abs(Direction) < float3(1.0e-9));
    return 1.0 / Safe;
}

float IntersectEntryBounds(float3 Origin, float3 Reciprocal, float3 Minimum, float3 Maximum, float Limit)
{
    let Near = (Minimum - Origin) * Reciprocal;
    let Far = (Maximum - Origin) * Reciprocal;
    let Low = min(Near, Far);
    let High = max(Near, Far);
    let Entry = max(max(Low.x, Low.y), max(Low.z, 0.0));
    let Exit = min(min(High.x, High.y), min(High.z, Limit));
    return SelectBranch(1.0e30, Entry, Entry <= Exit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS CLOSEST-HIT TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact Two-Level BVH ray-triangle intersection (Möller–Trumbore 1997) with near-first child ordering.
RayHit TraceSceneRay(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel)
{
    var Result : RayHit = {};
    Result.Position = Origin + Direction * MaximumTravel;
    Result.Travel   = MaximumTravel;
    Result.Normal   = -Direction;
    Result.Geometry = -Direction;
    Result.Identity = SkyIdentity;
    Result.Family   = 0u;
    Result.Steps    = 0u;
    Result.Hit      = false;

    if (BvhEntries[0].LeftOrFirst == EmptyTlasSentinel && BvhEntries[0].Count == 0u)
    {
        return Result;
    }

    let WorldRecip = SafeReciprocal(Direction);
    var Stack : uint[BvhStackCapacity] = {};
    var Depth = 1u;
    Stack[0]       = 0u;
    var BlasFloor = 0xFFFFFFFFu;

    var Nearest = MaximumTravel;
    var BestTri = 0xFFFFFFFFu;
    var BestOcc = 0xFFFFFFFFu;
    var BestUV = float2(0.0);
    var Steps = 0u;

    var CurrentOcc = 0xFFFFFFFFu;
    var OccParticipation = 0u;
    var LocalOrigin = Origin;
    var LocalDir = Direction;
    var LocalRecip = WorldRecip;

    while (Depth > 0u && Steps < MaxBvhSteps)
    {
        if (Depth <= BlasFloor)
        {
            BlasFloor = 0xFFFFFFFFu;
        }
        Depth -= 1u;
        let EntryIndex = Stack[Depth];
        let Entry = BvhEntries[EntryIndex];
        Steps += 1u;

        if (BlasFloor == 0xFFFFFFFFu)
        {
            if (IntersectEntryBounds(Origin, WorldRecip, Entry.Minimum, Entry.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if ((Entry.Count & TlasLeafBit) != 0u)
            {
                // 📝 Beyond the demo: an object switched out of indirect light is invisible to every traced ray but shadow rays.
                if ((Occupants[Entry.Count & TlasIndexMask].Participation & ParticipationIndirectExempt) != 0u)
                {
                    continue;
                }
                CurrentOcc  = Entry.Count & TlasIndexMask;
                let Occ = Occupants[CurrentOcc];
                OccParticipation    = Occ.Participation;
                LocalOrigin = (mul(float4(Origin, 1.0), Occ.WorldToLocal)).xyz;
                LocalDir    = (mul(float4(Direction, 0.0), Occ.WorldToLocal)).xyz;
                LocalRecip  = SafeReciprocal(LocalDir);
                if (Depth < BvhStackCapacity)
                {
                    BlasFloor    = Depth;
                    Stack[Depth] = Entry.LeftOrFirst;
                    Depth += 1u;
                }
            }
            else
            {
                let Left = Entry.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectEntryBounds(Origin, WorldRecip, BvhEntries[Left].Minimum, BvhEntries[Left].Maximum, Nearest);
                let RDist = IntersectEntryBounds(Origin, WorldRecip, BvhEntries[Right].Minimum, BvhEntries[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
        else
        {
            if (IntersectEntryBounds(LocalOrigin, LocalRecip, Entry.Minimum, Entry.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if (Entry.Count > 0u)
            {
                let FirstTri = Entry.LeftOrFirst;
                let TriEnd = FirstTri + Entry.Count;
                for (var Tri = FirstTri; Tri < TriEnd; Tri += 1u)
                {
                    let Base = Tri * 4u;
                    let T0 = BvhTriangles[Base];
                    let RawFamily = asuint(T0.w);
                    if (RawFamily == VehicleGlassFamily)
                    {
                        continue;
                    }
                    if (RawFamily == TyreFamily && (OccParticipation & ParticipationShredded) != 0u)
                    {
                        continue;
                    }
                    let E1 = BvhTriangles[Base + 1u].xyz;
                    let E2 = BvhTriangles[Base + 2u].xyz;
                    let P = cross(LocalDir, E2);
                    let Det = dot(E1, P);
                    if (abs(Det) < 1.0e-9)
                    {
                        continue;
                    }
                    let InvDet = 1.0 / Det;
                    let T = LocalOrigin - T0.xyz;
                    let U = dot(T, P) * InvDet;
                    if (U < 0.0 || U > 1.0)
                    {
                        continue;
                    }
                    let Q = cross(T, E1);
                    let V = dot(LocalDir, Q) * InvDet;
                    if (V < 0.0 || U + V > 1.0)
                    {
                        continue;
                    }
                    let Travel = dot(E2, Q) * InvDet;
                    if (Travel >= MinimumTravel && Travel < Nearest)
                    {
                        Nearest = Travel;
                        BestTri = Tri;
                        BestOcc = CurrentOcc;
                        BestUV  = float2(U, V);
                    }
                }
            }
            else
            {
                let Left = Entry.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectEntryBounds(LocalOrigin, LocalRecip, BvhEntries[Left].Minimum, BvhEntries[Left].Maximum, Nearest);
                let RDist = IntersectEntryBounds(LocalOrigin, LocalRecip, BvhEntries[Right].Minimum, BvhEntries[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
    }

    Result.Steps = Steps;

    if (BestTri != 0xFFFFFFFFu)
    {
        let Base = BestTri * 4u;
        let T0 = BvhTriangles[Base];
        let T1 = BvhTriangles[Base + 1u];
        let T2 = BvhTriangles[Base + 2u];
        let T3 = BvhTriangles[Base + 3u];

        let NA = UnpackOctahedral(asuint(T1.w));
        let NB = UnpackOctahedral(asuint(T2.w));
        let NC = UnpackOctahedral(asuint(T3.w));
        let W = 1.0 - BestUV.x - BestUV.y;
        let LocalShading = normalize(NA * W + NB * BestUV.x + NC * BestUV.y);

        let Occ = Occupants[BestOcc];
        var WorldNorm = normalize((mul(float4(LocalShading, 0.0), Occ.LocalToWorld)).xyz);
        var WorldGeom = normalize((mul(float4(T3.xyz, 0.0), Occ.LocalToWorld)).xyz);
        if (dot(WorldGeom, Direction) > 0.0)
        {
            WorldGeom = -WorldGeom;
        }
        if (dot(WorldNorm, Direction) > 0.0)
        {
            WorldNorm = -WorldNorm;
        }

        let RawFamily = asuint(T0.w);
        Result.Position = Origin + Direction * Nearest;
        Result.Travel   = Nearest;
        Result.Normal   = WorldNorm;
        Result.Geometry = WorldGeom;
        Result.Family   = ResolveSurfaceFamily(RawFamily, Occ.MaterialOverride);
        Result.Identity = SelectBranch(BestOcc, GroundIdentity, BestOcc == GroundOccupantIndex);
        Result.Hit      = true;
        return Result;
    }

    // Far-field horizon ground fallback beyond the authored 220m mesh
    if (Direction.z < -1.0e-4)
    {
        let GroundTravel = (Distance.GroundHeight - Origin.z) / Direction.z;
        if (GroundTravel >= MinimumTravel && GroundTravel <= MaximumTravel)
        {
            Result.Position = Origin + Direction * GroundTravel;
            Result.Travel   = GroundTravel;
            Result.Normal   = float3(0.0, 0.0, 1.0);
            Result.Geometry = float3(0.0, 0.0, 1.0);
            Result.Identity = GroundIdentity;
            Result.Family   = SurroundFamily;
            Result.Hit      = true;
        }
    }

    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS ANY-HIT VISIBILITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact any-hit shadow ray march: exits on the very first triangle intersection in [MinimumTravel, MaximumTravel) and returns its
//    travel (an occluder, not necessarily the nearest), or -1 when the ray is unoccluded.
float TraceSceneOcclusion(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel)
{
    if (MaximumTravel <= MinimumTravel)
    {
        return -1.0;
    }
    if (BvhEntries[0].LeftOrFirst == EmptyTlasSentinel && BvhEntries[0].Count == 0u)
    {
        return -1.0;
    }

    let WorldRecip = SafeReciprocal(Direction);
    var Stack : uint[BvhStackCapacity] = {};
    var Depth = 1u;
    Stack[0]       = 0u;
    var BlasFloor = 0xFFFFFFFFu;
    var Steps = 0u;

    var OccParticipation = 0u;
    var LocalOrigin = Origin;
    var LocalDir = Direction;
    var LocalRecip = WorldRecip;

    while (Depth > 0u && Steps < MaxBvhSteps)
    {
        if (Depth <= BlasFloor)
        {
            BlasFloor = 0xFFFFFFFFu;
        }
        Depth -= 1u;
        let EntryIndex = Stack[Depth];
        let Entry = BvhEntries[EntryIndex];
        Steps += 1u;

        if (BlasFloor == 0xFFFFFFFFu)
        {
            if (IntersectEntryBounds(Origin, WorldRecip, Entry.Minimum, Entry.Maximum, MaximumTravel) >= MaximumTravel)
            {
                continue;
            }
            if ((Entry.Count & TlasLeafBit) != 0u)
            {
                let OccIdx = Entry.Count & TlasIndexMask;
                let Occ = Occupants[OccIdx];
                // 📝 Beyond the demo: an object switched out of shadows lets every shadow ray through.
                if ((Occ.Participation & ParticipationShadowExempt) != 0u)
                {
                    continue;
                }
                OccParticipation    = Occ.Participation;
                LocalOrigin = (mul(float4(Origin, 1.0), Occ.WorldToLocal)).xyz;
                LocalDir    = (mul(float4(Direction, 0.0), Occ.WorldToLocal)).xyz;
                LocalRecip  = SafeReciprocal(LocalDir);
                if (Depth < BvhStackCapacity)
                {
                    BlasFloor    = Depth;
                    Stack[Depth] = Entry.LeftOrFirst;
                    Depth += 1u;
                }
            }
            else
            {
                let Left = Entry.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectEntryBounds(Origin, WorldRecip, BvhEntries[Left].Minimum, BvhEntries[Left].Maximum, MaximumTravel);
                let RDist = IntersectEntryBounds(Origin, WorldRecip, BvhEntries[Right].Minimum, BvhEntries[Right].Maximum, MaximumTravel);
                if (LDist < RDist)
                {
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
        else
        {
            if (IntersectEntryBounds(LocalOrigin, LocalRecip, Entry.Minimum, Entry.Maximum, MaximumTravel) >= MaximumTravel)
            {
                continue;
            }
            if (Entry.Count > 0u)
            {
                let FirstTri = Entry.LeftOrFirst;
                let TriEnd = FirstTri + Entry.Count;
                for (var Tri = FirstTri; Tri < TriEnd; Tri += 1u)
                {
                    let Base = Tri * 4u;
                    let T0 = BvhTriangles[Base];
                    let RawFamily = asuint(T0.w);
                    if (RawFamily == VehicleGlassFamily)
                    {
                        continue;
                    }
                    if (RawFamily == TyreFamily && (OccParticipation & ParticipationShredded) != 0u)
                    {
                        continue;
                    }
                    let E1 = BvhTriangles[Base + 1u].xyz;
                    let E2 = BvhTriangles[Base + 2u].xyz;
                    let P = cross(LocalDir, E2);
                    let Det = dot(E1, P);
                    if (abs(Det) < 1.0e-9)
                    {
                        continue;
                    }
                    let InvDet = 1.0 / Det;
                    let T = LocalOrigin - T0.xyz;
                    let U = dot(T, P) * InvDet;
                    if (U < 0.0 || U > 1.0)
                    {
                        continue;
                    }
                    let Q = cross(T, E1);
                    let V = dot(LocalDir, Q) * InvDet;
                    if (V < 0.0 || U + V > 1.0)
                    {
                        continue;
                    }
                    let Travel = dot(E2, Q) * InvDet;
                    if (Travel >= MinimumTravel && Travel < MaximumTravel)
                    {
                        return Travel;
                    }
                }
            }
            else
            {
                let Left = Entry.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectEntryBounds(LocalOrigin, LocalRecip, BvhEntries[Left].Minimum, BvhEntries[Left].Maximum, MaximumTravel);
                let RDist = IntersectEntryBounds(LocalOrigin, LocalRecip, BvhEntries[Right].Minimum, BvhEntries[Right].Maximum, MaximumTravel);
                if (LDist < RDist)
                {
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < MaximumTravel && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
    }

    return -1.0;
}

// 📝 The demo's verdict: 1 unoccluded, 0 occluded.
float TraceSceneVisibility(float3 Origin, float3 Direction, float MinimumTravel, float MaximumTravel)
{
    return SelectBranch(1.0, 0.0, TraceSceneOcclusion(Origin, Direction, MinimumTravel, MaximumTravel) >= 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                     SOLAR CONE, RIS LIGHT NEE & BSDF SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Jitters a solar ray within the sun's angular radius (~0.27°) for physically accurate soft penumbrae.
float3 SampleSunConeDirection(float3 SunDirection, float2 Pair)
{
    let Disk = ProjectUniformDisk(Pair) * SolarAngularRadius;
    let Basis = ConstructTangentBasis(SunDirection);
    return normalize(SunDirection + Basis[0] * Disk.x + Basis[1] * Disk.y);
}

// 📝 RefinementSolarDisc: the sun reflected as the disc it is, toward the disc's centre (no sampled direction); the demo evaluates
//    its point-light reflectance toward the shadow ray's jittered direction, which spikes on smooth surfaces.
float3 ResolveSunReflectance(SurfaceSample Surface, float3 Normal, float3 View, float3 SampledDirection)
{
    if (RefinementEnabled(Refresh, RefinementSolarDisc))
    {
        return ResolveDiscReflectance(Surface, Normal, View, Refresh.SunDirection, SolarAngularRadius);
    }
    return ResolveDirectReflectance(Surface, Normal, View, SampledDirection);
}

float ResolveReflectedDiscWeight()
{
    return SelectBranch(1.0, 0.0, RefinementEnabled(Refresh, RefinementSolarDisc));
}

// 📝 RefinementStratifiedSun: neighbours in a 4×4 block take consecutive points of the R2 sequence (Bayer rank + 16 per refresh), so
//    every block covers the disc evenly in each refresh; 32-bit fixed point keeps the sequence exact for 2^28 refreshes.
float2 ProjectStratifiedPair(uint2 Pixel, uint RefreshIndex)
{
    let Bayer : uint[16] = {0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u};
    let Index = Bayer[(Pixel.y & 3u) * 4u + (Pixel.x & 3u)] + 16u * RefreshIndex;
    return float2(float((Index * 3242174889u + 2147483648u) >> 8u), float((Index * 2447445414u + 2147483648u) >> 8u)) * (1.0 / 16777216.0);
}

// 📝 Heitz 2018, "Sampling the GGX Distribution of Visible Normals"; view in tangent space, Alpha = Roughness².
float3 ProjectVisibleNormal(float3 View, float Alpha, float2 Pair)
{
    let Stretched = normalize(float3(Alpha * View.x, Alpha * View.y, max(View.z, 1.0e-4)));
    let LengthSquared = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
    let T1 = SelectBranch(float3(1.0, 0.0, 0.0), float3(-Stretched.y, Stretched.x, 0.0) * rsqrt(LengthSquared), LengthSquared > 0.0);
    let T2 = cross(Stretched, T1);
    let Radius = sqrt(Pair.x);
    let Phi = 2.0 * Pi * Pair.y;
    let P1 = Radius * cos(Phi);
    let Interpolant = 0.5 * (1.0 + Stretched.z);
    let P2 = lerp(sqrt(max(0.0, 1.0 - P1 * P1)), Radius * sin(Phi), Interpolant);
    let Hemisphere = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
    return normalize(float3(Alpha * Hemisphere.x, Alpha * Hemisphere.y, max(1.0e-4, Hemisphere.z)));
}

// 📝 GGX VNDF reflection PDF (Heitz 2018): D_v(H) / (4 · (V·H)) = D(H) · G1(V) / (4 · (N·V)).
float EvaluateGgxVndfPdf(float NoV, float NoH, float VoH, float Roughness)
{
    let Alpha = max(Roughness * Roughness, 1.0e-3);
    let Alpha2 = Alpha * Alpha;
    let D = ResolveGgxDistribution(NoH, Roughness);
    let G1 = 2.0 * NoV / max(NoV + sqrt(Alpha2 + (1.0 - Alpha2) * NoV * NoV), 1.0e-4);
    return (D * G1) / max(4.0 * NoV, 1.0e-4);
}

// 📝 Two-layer PBR BSDF importance sampling (GGX VNDF specular/clearcoat lobe + cosine diffuse lobe) with exact mixture PDF.
bool SampleSurfaceBsdf(
    SurfaceSample Surface,
    float3 Normal,
    float3 View,
    inout uint Seed,
    inout float3 OutDirection,
    inout float3 OutThroughput,
    inout float OutPdf)
{
    let NoV = max(dot(Normal, View), 1.0e-4);
    let F0 = ResolveSpecularColour(Surface);
    let FresnelLum = ResolveLuminance(ResolveFresnel(F0, NoV)) + Surface.CoatWeight * 0.04;
    let DiffuseLum = ResolveLuminance(ResolveDiffuseAlbedo(Surface));

    var SpecProb = FresnelLum / max(FresnelLum + DiffuseLum, 1.0e-4);
    SpecProb = SelectBranch(clamp(SpecProb, 0.15, 0.85), 0.92, Surface.Metalness > 0.9 && Surface.Roughness < 0.25);

    let EffectiveRough = clamp(
        lerp(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight * 0.5),
        0.04,
        1.0
    );

    let Basis = ConstructTangentBasis(Normal);
    let Choice = AdvanceUniform(Seed);
    let Pair = float2(AdvanceUniform(Seed), AdvanceUniform(Seed));
    var LightDir = Normal;

    if (Choice < SpecProb)
    {
        let ViewLocal = float3(dot(View, Basis[0]), dot(View, Basis[1]), max(dot(View, Basis[2]), 1.0e-3));
        let HalfLocal = ProjectVisibleNormal(ViewLocal, EffectiveRough * EffectiveRough, Pair);
        let HalfWorld = normalize(mul(HalfLocal, Basis));
        LightDir      = reflect(-View, HalfWorld);
        if (dot(Normal, LightDir) <= 1.0e-4)
        {
            LightDir = normalize(mul(ProjectCosineHemisphere(Pair), Basis));
        }
    }
    else
    {
        LightDir = normalize(mul(ProjectCosineHemisphere(Pair), Basis));
    }

    let NoL = dot(Normal, LightDir);
    if (NoL <= 1.0e-4)
    {
        return false;
    }

    let Half = normalize(View + LightDir);
    let NoH = saturate(dot(Normal, Half));
    let VoH = max(dot(View, Half), 1.0e-4);
    let DiffPdf = NoL * InversePi;
    let SpecPdf = EvaluateGgxVndfPdf(NoV, NoH, VoH, EffectiveRough);
    let MixPdf = max(lerp(DiffPdf, SpecPdf, SpecProb), 1.0e-4);

    let Brdf = ResolveDirectReflectance(Surface, Normal, View, LightDir);
    OutDirection  = LightDir;
    OutThroughput = min(Brdf / MixPdf, float3(8.0));
    OutPdf        = MixPdf;
    return true;
}

// 📝 Physical HMI screen texture evaluation & rectangular area-light Next-Event Estimation (NEE) inside the vehicle cabin.
static const float3 HmiTabletCentre = float3(0.96721675, 0.0, 0.51285865);
static const float3 HmiTabletRight = float3(0.0, -1.0, 0.0);
static const float3 HmiTabletUp = float3(0.40776045, 0.0, 0.91308894);
static const float3 HmiTabletNormal = float3(-0.91308894, 0.0, 0.40776045);

static const float3 HmiClusterCentre = float3(0.65755436, 0.42, 0.71216928);
static const float3 HmiClusterRight = float3(0.0, -1.0, 0.0);
static const float3 HmiClusterUp = float3(0.21822962, 0.0, 0.97589745);
static const float3 HmiClusterNormal = float3(-0.97589745, 0.0, 0.21822962);

bool IsCockpitSurface(uint Identity, uint Family)
{
    return Identity == 0u && Family >= CockpitFamilyFirst && Family <= TabletHousingFamily && Family != RimMarkFamily;
}

float3 LoadHmiTexel(bool IsCluster, float2 UV)
{
    let Clamped = saturate(UV);
    if (IsCluster)
    {
        let Dims = float2(TextureExtent(HmiClusterTexture, 0));
        let Texel = clamp(int2(Clamped * Dims), int2(0), max(int2(Dims) - int2(1), int2(0)));
        return LoadTexel(HmiClusterTexture, Texel, 0).rgb;
    }
    let Dims = float2(TextureExtent(HmiTabletTexture, 0));
    let Texel = clamp(int2(Clamped * Dims), int2(0), max(int2(Dims) - int2(1), int2(0)));
    return LoadTexel(HmiTabletTexture, Texel, 0).rgb;
}

// 📝 Multi-tap filtered read of the live HMI texture at UV so area-light NEE integrates the screen's spatial colour distribution.
float3 SampleHmiScreenFiltered(bool IsCluster, float2 UV)
{
    let C0 = LoadHmiTexel(IsCluster, UV);
    let C1 = LoadHmiTexel(IsCluster, UV + float2( 0.14,  0.09));
    let C2 = LoadHmiTexel(IsCluster, UV + float2(-0.14,  0.09));
    let C3 = LoadHmiTexel(IsCluster, UV + float2( 0.14, -0.09));
    let C4 = LoadHmiTexel(IsCluster, UV + float2(-0.14, -0.09));
    return C0 * 0.40 + (C1 + C2 + C3 + C4) * 0.15;
}

// 📝 Evaluates the live radiated radiance L_e(x, ω) when a GI or reflection ray intersects the Tablet or Instrument Cluster display.
//    When ApplyMisWeight is true (for diffuse/BSDF GI rays), applies Veach's balance/power heuristic weight w_bsdf complementary to
//    EvaluateHmiAreaLightNee so direct NEE + BSDF hits sum to 1 without double counting.
float3 EvaluateHmiScreenRadiance(RayHit Hit, float3 IncomingRayDir, bool ApplyMisWeight)
{
    if (Hit.Identity != 0u)
    {
        return float3(0.0);
    }
    let HmiBright = ResolveHmiBrightness(Refresh);
    if (HmiBright <= 1.0e-4)
    {
        return float3(0.0);
    }
    let Occ = Occupants[0];
    let LocalPos = (mul(float4(Hit.Position, 1.0), Occ.WorldToLocal)).xyz;
    let LocalView = normalize((mul(float4(-IncomingRayDir, 0.0), Occ.WorldToLocal)).xyz);

    if (Hit.Family == DisplayGlassFamily)
    {
        let CosExitant = dot(LocalView, HmiTabletNormal);
        if (CosExitant <= 0.0)
        {
            return float3(0.0);
        }
        let HmiScale = ResolveHmiScale(Refresh);
        let Width = 0.30 * HmiScale;
        let Height = 0.1875 * HmiScale;
        let Rel = LocalPos - HmiTabletCentre;
        let U = 0.5 + dot(Rel, HmiTabletRight) / max(Width, 1.0e-4);
        let V = 0.5 - dot(Rel, HmiTabletUp) / max(Height, 1.0e-4);
        if (U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0)
        {
            var Weight = 1.0;
            if (ApplyMisWeight)
            {
                let Area = Width * Height;
                let DistSq = max(Hit.Travel * Hit.Travel, 0.030);
                let SolidAngle = (Area * CosExitant) / DistSq;
                let PdfBsdf = InversePi;
                let Prod = PdfBsdf * SolidAngle;
                Weight         = (Prod * Prod) / (1.0 + Prod * Prod);
            }
            return LoadHmiTexel(false, float2(U, V)) * (3.0 * HmiBright * Weight);
        }
    }
    else if (Hit.Family == SteeringWheelFamily)
    {
        let CosExitant = dot(LocalView, HmiClusterNormal);
        if (CosExitant <= 0.0)
        {
            return float3(0.0);
        }
        let Rel = LocalPos - HmiClusterCentre;
        let Plane = abs(dot(Rel, HmiClusterNormal));
        if (Plane < 0.016)
        {
            let X = dot(Rel, HmiClusterRight) / 0.135;
            let Y = dot(Rel, HmiClusterUp) / 0.0755;
            if (abs(Y) <= 1.0 && (abs(X) + 0.38 * abs(Y)) <= 1.02)
            {
                var Weight = 1.0;
                if (ApplyMisWeight)
                {
                    let Area = 0.27 * 0.151 * 0.81;
                    let DistSq = max(Hit.Travel * Hit.Travel, 0.025);
                    let SolidAngle = (Area * CosExitant) / DistSq;
                    let PdfBsdf = InversePi;
                    let Prod = PdfBsdf * SolidAngle;
                    Weight         = (Prod * Prod) / (1.0 + Prod * Prod);
                }
                let U = 0.5 + 0.5 * X;
                let V = 0.5 - 0.5 * Y;
                return LoadHmiTexel(true, float2(U, V)) * (3.5 * HmiBright * Weight);
            }
        }
    }
    return float3(0.0);
}

// 📝 Physical Rectangular Area-Light Next-Event Estimation (NEE) over the 3D Tablet and Instrument Cluster displays
//    with BVH shadow ray occlusion and Veach power-heuristic MIS weight w_nee inside the vehicle cabin.
float3 EvaluateHmiAreaLightNee(
    float3 Position,
    float3 Normal,
    float3 View,
    SurfaceSample Surface,
    float2 Pair,
    bool TraceShadow)
{
    let HmiBright = ResolveHmiBrightness(Refresh);
    if (HmiBright <= 1.0e-4)
    {
        return float3(0.0);
    }
    let Occ = Occupants[0];
    let LocalPos = (mul(float4(Position, 1.0), Occ.WorldToLocal)).xyz;
    // Fast cabin bounding check in vehicle-local space
    if (LocalPos.x < -1.35 || LocalPos.x > 1.55 || abs(LocalPos.y) > 1.15 || LocalPos.z < -0.25 || LocalPos.z > 1.35)
    {
        return float3(0.0);
    }

    var Radiance = float3(0.0);
    let Origin = Position + Normal * RayEpsilon;

    // 1. Centre-Stack Tablet Display Area Light (0.30m × 0.1875m × HmiScale²)
    let HmiScale = ResolveHmiScale(Refresh);
    let TabWidth = 0.30 * HmiScale;
    let TabHeight = 0.1875 * HmiScale;
    let TabArea = TabWidth * TabHeight;
    let TabLocalPt = HmiTabletCentre
                    + HmiTabletRight * ((Pair.x - 0.5) * TabWidth)
                    + HmiTabletUp    * ((0.5 - Pair.y) * TabHeight);
    let TabWorldPt = (mul(float4(TabLocalPt, 1.0), Occ.LocalToWorld)).xyz;
    let TabWorldNorm = normalize((mul(float4(HmiTabletNormal, 0.0), Occ.LocalToWorld)).xyz);
    let ToTablet = TabWorldPt - Position;
    let TabDistSq = dot(ToTablet, ToTablet);
    if (TabDistSq > 9.0e-4 && TabDistSq < 3.6)
    {
        let TabDist = sqrt(TabDistSq);
        let TabDir = ToTablet / TabDist;
        let NoL = dot(Normal, TabDir);
        let CosScreen = dot(TabWorldNorm, -TabDir);
        if (NoL > 0.0 && CosScreen > 0.0)
        {
            var Vis = 1.0;
            if (TraceShadow && TabDist > 0.05)
            {
                Vis = TraceSceneVisibility(Origin, TabDir, RayEpsilon, max(TabDist - 0.035, RayEpsilon));
            }
            if (Vis > 0.0)
            {
                let ScreenLe = SampleHmiScreenFiltered(false, Pair) * (3.0 * HmiBright);
                let SolidAngle = (TabArea * CosScreen) / max(TabDistSq, 0.030);
                let Prod = (NoL * InversePi) * SolidAngle;
                let MisWeight = 1.0 / (1.0 + Prod * Prod);
                let Brdf = ResolveDirectReflectance(Surface, Normal, View, TabDir) * Pi;
                Radiance      += ScreenLe * SolidAngle * MisWeight * Vis * Brdf;
            }
        }
    }

    // 2. Instrument Cluster Display Area Light (0.27m × 0.151m hexagonal binnacle)
    let CluPair = frac(Pair + float2(0.618034, 0.381966));
    let CluWidth = 0.27;
    let CluHeight = 0.151;
    let CluArea = CluWidth * CluHeight * 0.81;
    let CluLocalPt = HmiClusterCentre
                    + HmiClusterRight * ((CluPair.x - 0.5) * CluWidth)
                    + HmiClusterUp    * ((0.5 - CluPair.y) * CluHeight);
    let CluWorldPt = (mul(float4(CluLocalPt, 1.0), Occ.LocalToWorld)).xyz;
    let CluWorldNorm = normalize((mul(float4(HmiClusterNormal, 0.0), Occ.LocalToWorld)).xyz);
    let ToCluster = CluWorldPt - Position;
    let CluDistSq = dot(ToCluster, ToCluster);
    if (CluDistSq > 9.0e-4 && CluDistSq < 2.2)
    {
        let CluDist = sqrt(CluDistSq);
        let CluDir = ToCluster / CluDist;
        let NoL = dot(Normal, CluDir);
        let CosScreen = dot(CluWorldNorm, -CluDir);
        if (NoL > 0.0 && CosScreen > 0.0)
        {
            var Vis = 1.0;
            if (TraceShadow && CluDist > 0.05)
            {
                Vis = TraceSceneVisibility(Origin, CluDir, RayEpsilon, max(CluDist - 0.035, RayEpsilon));
            }
            if (Vis > 0.0)
            {
                let ScreenLe = SampleHmiScreenFiltered(true, CluPair) * (3.5 * HmiBright);
                let SolidAngle = (CluArea * CosScreen) / max(CluDistSq, 0.025);
                let Prod = (NoL * InversePi) * SolidAngle;
                let MisWeight = 1.0 / (1.0 + Prod * Prod);
                let Brdf = ResolveDirectReflectance(Surface, Normal, View, CluDir) * Pi;
                Radiance      += ScreenLe * SolidAngle * MisWeight * Vis * Brdf;
            }
        }
    }

    return Radiance;
}

// 📝 Next-Event Estimation (NEE) at a path vertex:
//    1. Soft solar cone NEE with exact BVH shadow ray + MIS weight
//    2. Active tyre fire point light NEE
//    3. Live HMI Tablet & Instrument Cluster rectangular area-light NEE inside the vehicle cabin
//    4. Resampled Importance Sampling (RIS) over all 32 clustered photometric / IES lights with 1 exact BVH shadow ray
float3 EvaluateDirectNextEvent(
    float3 Position,
    float3 Normal,
    float3 View,
    SurfaceSample Surface,
    inout uint Seed,
    bool TraceShadows)
{
    var Direct = float3(0.0);
    let Origin = Position + Normal * RayEpsilon;

    // 1. Direct Sun NEE (scaled by Pi to match the engine's exitant radiance convention where DisplayRecombination applies InversePi)
    let Pair = float2(AdvanceUniform(Seed), AdvanceUniform(Seed));
    let SunDir = SampleSunConeDirection(Refresh.SunDirection, Pair);
    if (dot(Normal, SunDir) > 0.0)
    {
        let SunBrdf = ResolveSunReflectance(Surface, Normal, View, SunDir) * Pi;
        if (any(SunBrdf > float3(0.0)))
        {
            var SunVis = 1.0;
            if (TraceShadows && FeatureEnabled(Refresh, FeatureDirectShadow))
            {
                SunVis = TraceSceneVisibility(Origin, SunDir, RayEpsilon, 400.0);
            }
            Direct += Refresh.SunIlluminance * SunVis * SunBrdf;
        }
    }

    // 2. Tyre Fire NEE
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > float3(0.0)))
    {
        Direct += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction) * Pi;
    }

    // 3. Live HMI Display Rectangular Area-Light NEE (Tablet + Instrument Cluster)
    Direct += EvaluateHmiAreaLightNee(Position, Normal, View, Surface, Pair, TraceShadows);

    // 3. Inline RIS over 3D Clustered Lights (point lights, roadway IES street lamps, headlights, fog & roof beams)
    let CellIdx = ResolveClusterCellIndex(RayLights.GridMinimum, RayLights.GridScale, Position);
    if (CellIdx != -1)
    {
        let Pair = RayLights.CellMasks[ToUnsigned(max(CellIdx, 0)) >> 1u];
        let Mask2 = ResolveClusterMaskFromPair(CellIdx, Pair, RayLights.GridMinimum.w);
        if ((Mask2.x | Mask2.y) != 0u)
        {
            var SelectedContrib = float3(0.0);
            var SelectedDir = float3(0.0, 0.0, 1.0);
            var SelectedDist = 0.0;
            var SelectedWeight = 0.0;
            var WeightSum = 0.0;

            for (var Word = 0u; Word < 2u; Word += 1u)
            {
                var Mask = SelectBranch(Mask2.x, Mask2.y, Word == 1u);
                let Base = Word * 32u;
                while (Mask != 0u)
                {
                    let Bit = firstbitlow(Mask);
                    Mask       &= Mask - 1u;
                    let Light = RayLights.Lights[Base + Bit];
                    let Arrival = EvaluateLightIlluminance(Light, Position, Normal);
                    if (all(Arrival <= float3(1.0e-5)))
                    {
                        continue;
                    }
                    let Toward = Light.PositionType.xyz - Position;
                    let Dist = length(Toward);
                    let LDir = Toward / max(Dist, 1.0e-4);
                    let Contrib = Arrival * ResolveDirectReflectance(Surface, Normal, View, LDir) * Pi;
                    let Weight = ResolveLuminance(Contrib);
                    if (Weight <= 1.0e-5)
                    {
                        continue;
                    }
                    WeightSum += Weight;
                    if (AdvanceUniform(Seed) * WeightSum < Weight)
                    {
                        SelectedContrib = Contrib;
                        SelectedDir     = LDir;
                        SelectedDist    = Dist;
                        SelectedWeight  = Weight;
                    }
                }
            }

            if (SelectedWeight > 0.0)
            {
                var LightVis = 1.0;
                let SunLum = ResolveLuminance(Refresh.SunIlluminance);
                if (TraceShadows && WeightSum > 0.12 + 0.03 * SunLum)
                {
                    LightVis = TraceSceneVisibility(Origin, SelectedDir, RayEpsilon, max(SelectedDist - 0.65, RayEpsilon));
                }
                let RisWeight = WeightSum / SelectedWeight;
                Direct += SelectedContrib * LightVis * RisWeight;
            }
        }
    }

    return Direct;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MULTI-BOUNCE PATH TRACER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Traces a full multi-bounce path starting along (Origin, FirstDirection), returning the first bounce surface vertex
//    x_s and the complete multi-bounce incoming radiance L_s gathered along the path for ReSTIR GI resampling.
ReservoirSample TraceMultiBouncePath(float3 Origin, float3 FirstDirection, inout uint Seed)
{
    let Hit1 = TraceSceneRay(Origin, FirstDirection, RayEpsilon, 500.0);
    if (!Hit1.Hit)
    {
        var SkySample : ReservoirSample = {};
        SkySample.Position = Origin + FirstDirection * SkyReach;
        SkySample.Normal   = -FirstDirection;
        SkySample.Radiance = SampleSkyRadiance(FirstDirection);
        SkySample.Identity = SkyIdentity;
        return SkySample;
    }

    let Mat1 = RayMaterials[Hit1.Family];
    let Surf1 = ProjectSurfaceSample(Mat1);

    // Bounce 1 direct incandescence (including live HMI screen incandescence) + Next-Event Estimation
    var TotalRadiance = Surf1.Incandescence * Refresh.IncandescenceScale + EvaluateHmiScreenRadiance(Hit1, FirstDirection, true);
    TotalRadiance += EvaluateDirectNextEvent(Hit1.Position, Hit1.Normal, -FirstDirection, Surf1, Seed, true);

    let MaxBounces = ResolveMaxBounces(Refresh);
    var PathThroughput = float3(1.0);
    var CurrPos = Hit1.Position;
    var CurrNormal = Hit1.Normal;
    var CurrView = -FirstDirection;
    var CurrSurf = Surf1;
    var CurrIdentity = Hit1.Identity;
    var CurrFamily = Hit1.Family;
    var EscapedToSky = false;

    // Secondary & higher-order path bounces (bounce 2 .. MaxBounces)
    for (var Bounce = 2u; Bounce <= MaxBounces; Bounce += 1u)
    {
        var NextDir = CurrNormal;
        var BounceThru = float3(0.0);
        var BsdfPdf = 1.0;
        if (!SampleSurfaceBsdf(CurrSurf, CurrNormal, CurrView, Seed, NextDir, BounceThru, BsdfPdf))
        {
            break;
        }
        PathThroughput *= BounceThru;

        // Russian Roulette termination
        if (FeatureEnabled(Refresh, FeatureRussianRoulette) && Bounce >= 2u)
        {
            let Survival = clamp(max(PathThroughput.r, max(PathThroughput.g, PathThroughput.b)), 0.12, 0.95);
            if (AdvanceUniform(Seed) > Survival)
            {
                break;
            }
            PathThroughput /= Survival;
        }

        let HitB = TraceSceneRay(CurrPos + CurrNormal * RayEpsilon, NextDir, RayEpsilon, 350.0);
        if (!HitB.Hit)
        {
            TotalRadiance += PathThroughput * SampleSkyRadiance(NextDir);
            EscapedToSky = true;
            break;
        }

        let MatB = RayMaterials[HitB.Family];
        let SurfB = ProjectSurfaceSample(MatB);

        // Balance-heuristic MIS weight on secondary incandescent hits
        let DistSq = max(HitB.Travel * HitB.Travel, 0.04);
        let CosAtHit = max(dot(HitB.Normal, -NextDir), 0.05);
        let GeomTerm = CosAtHit / DistSq;
        let MisWeight = clamp((BsdfPdf * BsdfPdf) / max(BsdfPdf * BsdfPdf + GeomTerm * GeomTerm * 0.01, 1.0e-5), 0.15, 1.0);

        let HitIncandescence = SurfB.Incandescence * Refresh.IncandescenceScale + EvaluateHmiScreenRadiance(HitB, NextDir, true);
        TotalRadiance  += PathThroughput * HitIncandescence * MisWeight;

        if (FeatureEnabled(Refresh, FeatureMultiBounceNEE))
        {
            let DirectB = EvaluateDirectNextEvent(HitB.Position, HitB.Normal, -NextDir, SurfB, Seed, Bounce <= 2u);
            TotalRadiance += PathThroughput * DirectB;
        }

        CurrPos      = HitB.Position;
        CurrNormal   = HitB.Normal;
        CurrView     = -NextDir;
        CurrSurf     = SurfB;
        CurrIdentity = HitB.Identity;
        CurrFamily   = HitB.Family;
    }

    // Infinite-bounce radiosity store tail beyond MaxBounces (excluded on interior cabin surfaces to prevent exterior car-card leakage)
    if (!EscapedToSky && FeatureEnabled(Refresh, FeatureCardRadiosity) && !IsCockpitSurface(CurrIdentity, CurrFamily))
    {
        let TailClip = ResolveFinestClip(CurrPos, 1.0);
        let CardTail = SampleCardRadiance(CurrPos, CurrNormal, CurrIdentity, ResolveCardTolerance(TailClip));
        if (CardTail.w > 0.0)
        {
            TotalRadiance += PathThroughput * ResolveDiffuseAlbedo(CurrSurf) * InversePi * CardTail.rgb * 0.35;
        }
    }

    // Firefly & boiling suppression clamp on individual path samples
    if (FeatureEnabled(Refresh, FeatureFireflyFilter))
    {
        let Lum = ResolveLuminance(TotalRadiance);
        let MaxLum = 24.0;
        if (Lum > MaxLum)
        {
            TotalRadiance *= MaxLum / Lum;
        }
    }

    var OutSample : ReservoirSample = {};
    OutSample.Position = Hit1.Position;
    OutSample.Normal   = Hit1.Normal;
    OutSample.Radiance = max(TotalRadiance, float3(0.0));
    OutSample.Identity = Hit1.Identity;
    return OutSample;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-BOUNCE RAY-TRACED REFLECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Traces a chained multi-bounce specular / GGX-VNDF reflection path up to ResolveReflectionBounces(Refresh) bounces
//    through the TLAS/BLAS triangle nesting, shading each hit with incandescence, direct NEE (Sun + Fire + RIS IES lights
//    with BVH shadow rays), indirect diffuse lighting, and recursive Fresnel reflection continuation.
float3 TraceMultiBounceReflection(
    float3 Origin,
    float3 FirstDirection,
    float PrimaryRoughness,
    inout uint Seed)
{
    let MaxReflBounces = ResolveReflectionBounces(Refresh);
    var TotalRadiance = float3(0.0);
    var Throughput = float3(1.0);
    var RayOrigin = Origin;
    var RayDir = FirstDirection;
    var CurrentRoughness = PrimaryRoughness;

    for (var Bounce = 1u; Bounce <= MaxReflBounces; Bounce += 1u)
    {
        let Hit = TraceSceneRay(RayOrigin, RayDir, RayEpsilon, 500.0);
        if (!Hit.Hit)
        {
            var SkyRad = SampleSkyRadiance(RayDir);
            if (CurrentRoughness<0.25){SkyRad += ResolveCelestialRadiance(RayDir,MoonAtlas,SkySampler,ResolveReflectedDiscWeight()) * (1.0-smoothstep(0.05,0.25,CurrentRoughness));}
            TotalRadiance += Throughput * SkyRad;
            break;
        }

        let HitSurf = ProjectSurfaceSample(RayMaterials[Hit.Family]);
        let ViewDir = -RayDir;
        var HitRadiance = HitSurf.Incandescence * Refresh.IncandescenceScale + EvaluateHmiScreenRadiance(Hit, RayDir, false);

        // Direct Next-Event Estimation (Sun cone + Fire + HMI screens + RIS over 32 IES lights with exact BVH shadow rays)
        HitRadiance += EvaluateDirectNextEvent(Hit.Position, Hit.Normal, ViewDir, HitSurf, Seed, Bounce <= 2u) * InversePi;

        // Indirect diffuse illumination on the reflected surface so shadowed faces retain natural colour and depth
        let DiffuseAlbedo = ResolveDiffuseAlbedo(HitSurf);
        if (any(DiffuseAlbedo > float3(1.0e-3)))
        {
            let InCockpit = IsCockpitSurface(Hit.Identity, Hit.Family);
            let AmbientScale = SelectBranch(0.28 + 0.28 * saturate(Hit.Normal.z), 0.04 + 0.04 * saturate(Hit.Normal.z), InCockpit);
            var AmbientIrr = SampleSkyRadiance(Hit.Normal) * AmbientScale;
            if (FeatureEnabled(Refresh, FeatureCardRadiosity) && !InCockpit)
            {
                let Clip = ResolveFinestClip(Hit.Position, 1.0);
                let Card = SampleCardRadiance(Hit.Position, Hit.Normal, Hit.Identity, ResolveCardTolerance(Clip));
                if (Card.w > 0.0)
                {
                    AmbientIrr = max(AmbientIrr, Card.rgb * InversePi);
                }
            }
            HitRadiance += DiffuseAlbedo * AmbientIrr;
        }

        TotalRadiance += Throughput * HitRadiance;

        if (Bounce >= MaxReflBounces)
        {
            break;
        }

        // Recursive specular / clearcoat Fresnel continuation for reflection bounce b + 1
        let NoV = max(dot(Hit.Normal, ViewDir), 1.0e-4);
        let HitRoughness = SelectBranch(HitSurf.Roughness, HitSurf.CoatRoughness, HitSurf.CoatWeight > 0.0);
        let F0 = ResolveSpecularColour(HitSurf);
        var SpecWeight = ResolveFresnel(F0, NoV) * (1.0 - 0.55 * HitSurf.Roughness);
        if (HitSurf.CoatWeight > 0.0)
        {
            let CoatF = ResolveFresnel(float3(0.04), NoV).x * HitSurf.CoatWeight * (1.0 - 0.35 * HitSurf.CoatRoughness);
            SpecWeight = SpecWeight * (1.0 - CoatF) + float3(CoatF);
        }
        Throughput *= SpecWeight;
        if (max(Throughput.r, max(Throughput.g, Throughput.b)) < 0.015)
        {
            break;
        }

        let Basis = ConstructTangentBasis(Hit.Normal);
        let TangentView = mul(ViewDir, transpose(Basis));
        let Pair = float2(AdvanceUniform(Seed), AdvanceUniform(Seed));
        let CombinedRough = clamp(sqrt(CurrentRoughness * CurrentRoughness + HitRoughness * HitRoughness), 0.03, 1.0);
        let Half = mul(ProjectVisibleNormal(TangentView, CombinedRough * CombinedRough, Pair), Basis);
        var NextDir = reflect(-ViewDir, Half);
        if (dot(NextDir, Hit.Normal) <= 1.0e-4)
        {
            NextDir = reflect(-ViewDir, Hit.Normal);
        }
        if (dot(NextDir, Hit.Normal) <= 1.0e-4)
        {
            break;
        }
        RayOrigin        = Hit.Position + Hit.Normal * RayEpsilon;
        RayDir           = NextDir;
        CurrentRoughness = CombinedRough;
    }

    return min(TotalRadiance, float3(200.0));
}
