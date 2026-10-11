//@@HDR: 📦 Frontier/Shaders/ReSTIRSequence.slang — ReSTIR DI/GI Reservoirs and Compute Entries (dual Slang/C++17)
#ifndef RESTIR_SEQUENCE_INCLUDED
#define RESTIR_SEQUENCE_INCLUDED

// ReSTIRSequence.slang — ReSTIR direct + indirect lighting for the celestial
// sky. THE ONLY lighting in Project Zero: every shaded pixel is either a sky
// backdrop (display, not lighting) or a ReSTIR estimate (DI + GI). There is
// no ambient term, no analytic sun lamp, no legacy path.
//
// Layout:
//   - DUAL REGION (pure functions, same contract as SkySpecification.slang):
//     deterministic RNG, candidate draws, DI/GI reservoirs, RIS update/combine.
//     Compiled as C++17 by the Host convergence harness AND as Slang.
//   - SLANG-ONLY SHELL (#ifndef SLANG_COMPAT_CXX): uniform buffers, G-buffer
//     and reservoir descriptors, TLAS ray queries, the 9 compute entries.
//
// Authoritative algorithm references: the panel (for every Le term) and the
// ReSTIR papers (for the reservoir mechanics). The Host harness
// (Host/ReSTIRConvergence.cpp) mirrors these kernels over an analytic scene
// and gates them against independent brute-force references.
#ifndef PROJECT_ZERO_CELESTIAL_RESTIR_SLANG
#define PROJECT_ZERO_CELESTIAL_RESTIR_SLANG

#include "SkySpecification.slang"

// Dual region: RNG, draws, reservoirs.
struct RandomSequenceStructure
{
    uint state;
};

RandomSequenceStructure RandomSequenceMake(uint state)
{
    RandomSequenceStructure r;
    r.state = state;
    return r;
}

// PCG32 output permutation as a stateless hash (O'Neill): avalanche-mixes the
// LCG state so low seed bits still decorrelate neighbouring pixels/frames.
uint RandomPCGHash(uint seed)
{
    uint x = seed * 747796405u + 2891336453u;
    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (w >> 22u) ^ w;
}

// Seed stream: frame-major, then pixel, then kernel tag (0 = DirectInitial,
// 1 = DirectTemporal, 2 = DirectSpatial, 3 = IndirectInitial, 4 = IndirectTemporal, 5 = IndirectSpatial;
// shade/SkyViewport draw no random numbers).
uint RandomSequenceSeed(uint x, uint y, uint frame, uint tag)
{
    uint h = (frame * 73856093u) ^ (y * 19349663u) ^ (x * 83492791u);
    h = h ^ (tag * 2971215073u);
    return RandomPCGHash(h);
}

struct RandomScalarStructure
{
    RandomSequenceStructure rng;
    uint u;
};

struct RandomFloatStructure
{
    RandomSequenceStructure rng;
    float f;
};

RandomScalarStructure RandomScalarMake(in RandomSequenceStructure rng, uint u)
{
    RandomScalarStructure o;
    o.rng = rng;
    o.u = u;
    return o;
}

RandomFloatStructure RandomFloatMake(in RandomSequenceStructure rng, float f)
{
    RandomFloatStructure o;
    o.rng = rng;
    o.f = f;
    return o;
}

// LCG step + integer avalanche (splitmix64-style fmix32): full-period,
// branch-free, identical on CPU and GPU.
RandomScalarStructure RandomScalarNext(in RandomSequenceStructure rngIn)
{
    RandomSequenceStructure rng = rngIn;
    rng.state = rng.state * 1664525u + 1013904223u;
    uint x = rng.state;
    x = ((x >> 16u) ^ x) * 73244475u;
    x = ((x >> 16u) ^ x) * 73244475u;
    x = (x >> 16u) ^ x;
    return RandomScalarMake(rng, x);
}

// [0, 1): 24-bit mantissa fill, never returning 1.0.
RandomFloatStructure RandomFloatNext(in RandomSequenceStructure rngIn)
{
    RandomScalarStructure ru = RandomScalarNext(rngIn);
    float f = float(ru.u >> 8u) * (1.0f / 16777216.0f);
    return RandomFloatMake(ru.rng, f);
}

struct CandidateStructure
{
    RandomSequenceStructure rng;   // advanced RNG (functional threading, no inout)
    float3 dir;   // sky frame
    float pdf;    // the draw's own pdf (for the joint-pdf chain)
};

CandidateStructure CandidateMake(in RandomSequenceStructure rng, float3 dir, float pdf)
{
    CandidateStructure d;
    d.rng = rng;
    d.dir = dir;
    d.pdf = pdf;
    return d;
}

CandidateStructure CandidateSphere(in RandomSequenceStructure rngIn)
{
    RandomFloatStructure r1 = RandomFloatNext(rngIn);
    RandomFloatStructure r2 = RandomFloatNext(r1.rng);
    float z = 1.0f - 2.0f * r1.f;
    float a = 6.2831853f * r2.f;
    float s = sqrt(max(1.0f - z * z, 0.0f));
    float3 dir;
    dir.x = s * cos(a);
    dir.y = z;
    dir.z = s * sin(a);
    return CandidateMake(r2.rng, dir, 1.0f / (4.0f * SkyPi));
}

// Uniform cone around the sun, sized to the panel's disc + soft edge.
CandidateStructure CandidateSunCone(in RandomSequenceStructure rngIn, in SkyConfiguration p)
{
    float cosR = cos(p.sunAngularRadius * 1.6f);
    RandomFloatStructure r1 = RandomFloatNext(rngIn);
    RandomFloatStructure r2 = RandomFloatNext(r1.rng);
    float z = 1.0f - r1.f * (1.0f - cosR);
    float a = 6.2831853f * r2.f;
    float s = sqrt(max(1.0f - z * z, 0.0f));
    float3 up = abs(p.sunDir.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                        : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, p.sunDir));
    float3 tv = cross(p.sunDir, tu);
    float3 dir = tu * (s * cos(a)) + tv * (s * sin(a)) + p.sunDir * z;
    float pdf = 1.0f / max(6.2831853f * (1.0f - cosR), 1e-9f);
    return CandidateMake(r2.rng, dir, pdf);
}

// Photon-map-era power-lobe sampler (n = 8, Lafortune/Blinn): concentrate
// candidates in the aureole the panel actually renders.
CandidateStructure CandidateAureole(in RandomSequenceStructure rngIn, in SkyConfiguration p)
{
    RandomFloatStructure r1 = RandomFloatNext(rngIn);
    RandomFloatStructure r2 = RandomFloatNext(r1.rng);
    float cp = MathPow(max(1.0f - r1.f, 1e-9f), 1.0f / 9.0f);
    float sp = sqrt(max(1.0f - cp * cp, 0.0f));
    float a = 6.2831853f * r2.f;
    float3 up = abs(p.sunDir.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                        : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, p.sunDir));
    float3 tv = cross(p.sunDir, tu);
    float3 dir = tu * (sp * cos(a)) + tv * (sp * sin(a)) + p.sunDir * cp;
    float pdf = 9.0f * MathPow(max(cp, 1e-9f), 8.0f) / (6.2831853f * 2.0f);
    return CandidateMake(r2.rng, dir, pdf);
}

CandidateStructure CandidateCosine(in RandomSequenceStructure rngIn, float3 n)
{
    RandomFloatStructure r1 = RandomFloatNext(rngIn);
    RandomFloatStructure r2 = RandomFloatNext(r1.rng);
    float a = 6.2831853f * r1.f;
    float sr = sqrt(max(r2.f, 0.0f));
    float3 up = abs(n.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                 : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, n));
    float3 tv = cross(n, tu);
    float3 dir = tu * (sr * cos(a)) + tv * (sr * sin(a))
               + n * sqrt(max(1.0f - r2.f, 0.0f));
    float pdf = max(dot(n, dir), 0.0f) / SkyPi;
    return CandidateMake(r2.rng, dir, pdf);
}

// Technique pdfs evaluated at an arbitrary direction (balance heuristic).
// Formulas match CandidateSunCone / CandidateAureole exactly.
float DensitySunConeAt(float3 dir, in SkyConfiguration p)
{
    float cosR = cos(p.sunAngularRadius * 1.6f);
    float c = dot(dir, p.sunDir);
    float omega = 6.2831853f * (1.0f - cosR);
    return (c >= cosR) ? 1.0f / max(omega, 1e-9f) : 0.0f;
}

float DensityAureoleAt(float3 dir, in SkyConfiguration p)
{
    float cp = dot(dir, p.sunDir);
    if (cp <= 0.0f)
    {
        return 0.0f;
    }
    return 9.0f * MathPow(cp, 8.0f) / (6.2831853f * 2.0f);
}

struct DirectReservoirStructure
{
    float3 dir;   // winning direction, sky frame
    float3 le;    // winning emission (panel Le, unshadowed)
    float pdf;    // candidate pdf of the winner
    float wsum;   // RIS weight sum
    float m;      // candidate count
    float m0;     // initial candidates this frame (for the M-cap)
    float vis;    // visibility of the winner (0/1)
};

struct IndirectReservoirStructure
{
    float3 dir;         // winning first-bounce direction, sky frame
    float3 throughput;  // Lambert chain WITHOUT sampling pdfs (they live in pdf)
    float3 le;          // emission at the path end
    float pdf;          // joint candidate pdf
    float wsum;
    float m;
    float m0;
};

DirectReservoirStructure DirectReservoirMake()
{
    DirectReservoirStructure r;
    r.dir = float3(0.0f, 0.0f, 0.0f);
    r.le = float3(0.0f, 0.0f, 0.0f);
    r.pdf = 0.0f;
    r.wsum = 0.0f;
    r.m = 0.0f;
    r.m0 = 0.0f;
    r.vis = 0.0f;
    return r;
}

IndirectReservoirStructure IndirectReservoirMake()
{
    IndirectReservoirStructure r;
    r.dir = float3(0.0f, 0.0f, 0.0f);
    r.throughput = float3(0.0f, 0.0f, 0.0f);
    r.le = float3(0.0f, 0.0f, 0.0f);
    r.pdf = 0.0f;
    r.wsum = 0.0f;
    r.m = 0.0f;
    r.m0 = 0.0f;
    return r;
}

// DI target: luminance of (emission * albedo * cos / pi). Visibility is
// applied at shade time, never baked into the target.
float DirectTargetCompute(float3 le, float cosT, float3 albedo)
{
    return LuminanceDWCompute(le * albedo * (cosT / SkyPi));
}

float IndirectTargetCompute(float3 throughput, float3 le)
{
    return LuminanceDWCompute(throughput * le);
}

// Streaming RIS update (ReSTIR eq. 3): w = target / pdf.
DirectReservoirStructure DirectReservoirUpdate(in DirectReservoirStructure acc, in CandidateStructure d, float3 le, float cosT,
                     float3 albedo, in RandomSequenceStructure rngIn, float vis)
{
    float target = DirectTargetCompute(le, cosT, albedo);
    float w = (d.pdf > 0.0f) ? target / d.pdf : 0.0f;
    DirectReservoirStructure o = acc;
    o.wsum = acc.wsum + w;
    o.m = acc.m + 1.0f;
    RandomFloatStructure ru = RandomFloatNext(rngIn);
    if (ru.f * o.wsum < w && w > 0.0f)
    {
        o.dir = d.dir;
        o.le = le;
        o.pdf = d.pdf;
        o.vis = vis;
    }
    return o;
}

IndirectReservoirStructure IndirectReservoirUpdate(in IndirectReservoirStructure acc, in CandidateStructure d, float3 throughput, float3 le,
                     float pdf, in RandomSequenceStructure rngIn)
{
    float target = IndirectTargetCompute(throughput, le);
    float w = (pdf > 0.0f) ? target / pdf : 0.0f;
    IndirectReservoirStructure o = acc;
    o.wsum = acc.wsum + w;
    o.m = acc.m + 1.0f;
    RandomFloatStructure ru = RandomFloatNext(rngIn);
    if (ru.f * o.wsum < w && w > 0.0f)
    {
        o.dir = d.dir;
        o.throughput = throughput;
        o.le = le;
        o.pdf = pdf;
    }
    return o;
}

// Pairwise reservoir combine with target re-evaluation at the receiver
// (ReSTIR eq. 5-7): the loser's sample is re-weighted by the receiver's
// target, then one winner is kept with probability proportional to the
// re-weighted sums. m is M-capped by the caller.
DirectReservoirStructure DirectReservoirCombine(in DirectReservoirStructure acc, float3 nAcc, float3 albAcc,
                       in DirectReservoirStructure b, float3 nB, float3 albB, in RandomSequenceStructure rngIn)
{
    float cosB = max(dot(nAcc, b.dir), 0.0f);
    float pB = (b.m > 0.0f) ? DirectTargetCompute(b.le, cosB, albAcc) : 0.0f;
    float wA = (acc.m > 0.0f) ? acc.wsum : 0.0f;
    float wB = 0.0f;
    if (b.m > 0.0f)
    {
        float cosBb = max(dot(nB, b.dir), 0.0f);
        float pBb = DirectTargetCompute(b.le, cosBb, albB);
        wB = (pBb > 0.0f) ? pB * b.wsum / max(pBb, 1e-12f) : 0.0f;
    }
    DirectReservoirStructure o = acc;
    o.wsum = wA + wB;
    o.m = acc.m + b.m;
    RandomFloatStructure ru = RandomFloatNext(rngIn);
    if (ru.f * o.wsum < wB && wB > 0.0f)
    {
        o.dir = b.dir;
        o.le = b.le;
        o.pdf = b.pdf;
        o.vis = b.vis;
    }
    return o;
}

IndirectReservoirStructure IndirectReservoirCombine(in IndirectReservoirStructure acc, in IndirectReservoirStructure b, in RandomSequenceStructure rngIn)
{
    float wA = (acc.m > 0.0f) ? acc.wsum : 0.0f;
    float wB = (b.m > 0.0f) ? b.wsum : 0.0f;
    IndirectReservoirStructure o = acc;
    o.wsum = wA + wB;
    o.m = acc.m + b.m;
    RandomFloatStructure ru = RandomFloatNext(rngIn);
    if (ru.f * o.wsum < wB && wB > 0.0f)
    {
        o.dir = b.dir;
        o.throughput = b.throughput;
        o.le = b.le;
        o.pdf = b.pdf;
    }
    return o;
}

// Slang-only shell: descriptors, ray queries, compute entries.
// (Still inside the include guard; excluded from the C++ path.)
#ifndef SLANG_COMPAT_CXX

// 16-byte aligned frame constants (std140/HLSL compatible: every member is a
// float4, offsets are multiples of 16 whatever the target).
cbuffer SkyExchange : register(b0)
{
    float4 SunDirection;     // xyz = sunDir, w unused
    float4 SunColor;   // rgb = kelvin color, w = elevationDeg
    float4 SunFactors;  // intensity, angRadius, softness, discBoost
    float4 AtmosphereA;      // rayleigh, mie, mieG, ozone
    float4 AtmosphereB;      // planetR, atmoH, rayleighH, mieH
    float4 SkyTint;    // rgb = tint, w = skyBright
    float4 GroundAlbedo;     // rgb = albedo, w = groundBright
    float4 PostA;      // dawnI, lineI, lineAuto, evStops
    float4 PostB;      // tonemap, bloom, vignette, grain
    float4 MiscFactors;       // camHeight, timeSeconds, unused, unused
    float4 MediaA;     // fogOn, fogDensity, fogHeight, fogScatter
    float4 MediaB;     // fogColor rgb, afOn
    float4 MediaC;     // afDensity, afHeight, afStart, afMie
    float4 MediaD;     // afG, afSky, afTint.r, afTint.g
    float4 MediaE;     // afTint.b, starsOn, starDensity, starBright
    float4 StarA;      // starSize, starGlow, starTwinkle, starColor
    float4 StarB;      // starLimit, starMilky, starRot, starTilt
    float4 StarC;      // starLayers, starAA, starCeil, unused
};

cbuffer SkySequenceConfiguration : register(b1)
{
    uint ViewportWidth;
    uint ViewportHeight;
    uint TemporalIndex;
    uint InitialCandidates;       // initial candidates per pixel (DI; GI uses InitialCandidates / 2)
    uint ReservoirCap;     // temporal M cap (20)
    uint SpatialTaps; // spatial taps (4)
    uint RandomSeed;     // run seed (0 for the deterministic proof)
    uint LaunchPad;
};

// World-frame camera basis for the SkyViewport entry (the G-buffer pass owns
// the camera for every other entry).
cbuffer SkyViewExchange : register(b2)
{
    float4 CameraForward;     // xyz = fwd, w unused
    float4 CameraRight;   // xyz = right, w unused
    float4 CameraUp;      // xyz = up, w = tanHalf
    float4 CameraPad;
};

struct VisibilityStructure
{
    float3 pos;         // [m] world frame; background pixels (depth <= 0)
                        // carry the world-frame primary ray direction here
    float3 normal;      // [-] world frame, unit
    float3 albedo;      // [-]
    float depth;        // [m] primary-ray distance; <= 0 = background
    float2 motion;      // [px] previous-frame position = pixel + motion
};

struct InstanceStructure
{
    uint vtxBase;
    uint idxBase;
    uint pad0;
    uint pad1;
};

struct IntersectionStructure
{
    bool hit;
    float3 pos;     // world frame
    float3 normal;  // world frame, unit
    float t;
};

IntersectionStructure IntersectionMake(bool hit, float3 pos, float3 normal, float t)
{
    IntersectionStructure h;
    h.hit = hit;
    h.pos = pos;
    h.normal = normal;
    h.t = t;
    return h;
}

StructuredBuffer<VisibilityStructure> VisibilityExtent : register(t0);
RaytracingAccelerationStructure SceneAcceleration : register(t1);
StructuredBuffer<float3> PositionExtent : register(t2);
StructuredBuffer<float3> NormalExtent : register(t3);
StructuredBuffer<uint> IndexExtent : register(t4);
StructuredBuffer<InstanceStructure> InstanceExtent : register(t5);
StructuredBuffer<DirectReservoirStructure> DirectReservoirPrior : register(t6);
StructuredBuffer<IndirectReservoirStructure> IndirectReservoirPrior : register(t7);
RWStructuredBuffer<DirectReservoirStructure> DirectReservoirExtent : register(u0);
RWStructuredBuffer<IndirectReservoirStructure> IndirectReservoirExtent : register(u1);
RWStructuredBuffer<float4> DisplayExtent : register(u2);
RWStructuredBuffer<float3> SkyAmbientExtent : register(u3);

SkyConfiguration SkyConfigurationFromExchange()
{
    SkyConfiguration p;
    p.sunDir = float3(SunDirection.x, SunDirection.y, SunDirection.z);
    p.sunColor = float3(SunColor.x, SunColor.y, SunColor.z);
    p.sunElevationDeg = SunColor.w;
    p.sunIntensity = SunFactors.x;
    p.sunAngularRadius = SunFactors.y;
    p.sunSoftness = SunFactors.z;
    p.sunDiscBoost = SunFactors.w;
    p.rayleigh = AtmosphereA.x;
    p.mie = AtmosphereA.y;
    p.mieG = AtmosphereA.z;
    p.ozone = AtmosphereA.w;
    p.planetRadius = AtmosphereB.x;
    p.atmoHeight = AtmosphereB.y;
    p.rayleighH = AtmosphereB.z;
    p.mieH = AtmosphereB.w;
    p.skyTint = float3(SkyTint.x, SkyTint.y, SkyTint.z);
    p.skyBright = SkyTint.w;
    p.groundAlbedo = float3(GroundAlbedo.x, GroundAlbedo.y, GroundAlbedo.z);
    p.groundBright = GroundAlbedo.w;
    p.dawnIntensity = PostA.x;
    p.lineIntensity = PostA.y;
    p.lineAuto = uint(PostA.z);
    p.exposureStops = PostA.w;
    p.tonemap = uint(PostB.x);
    p.bloom = PostB.y;
    p.vignette = PostB.z;
    p.grain = PostB.w;
    p.camHeight = MiscFactors.x;
    p.timeSeconds = MiscFactors.y;
    p.fogOn = uint(MediaA.x);
    p.fogDensity = MediaA.y;
    p.fogHeight = MediaA.z;
    p.fogScatter = MediaA.w;
    p.fogColor = float3(MediaB.x, MediaB.y, MediaB.z);
    p.afOn = uint(MediaB.w);
    p.afDensity = MediaC.x;
    p.afHeight = MediaC.y;
    p.afStart = MediaC.z;
    p.afMie = MediaC.w;
    p.afG = MediaD.x;
    p.afSky = MediaD.y;
    p.afTint = float3(MediaD.z, MediaD.w, MediaE.x);
    p.skyAmb = SkyAmbientExtent[0];
    p.starsOn = uint(MediaE.y);
    p.starDensity = MediaE.z;
    p.starBright = MediaE.w;
    p.starSize = StarA.x;
    p.starGlow = StarA.y;
    p.starTwinkle = StarA.z;
    p.starColor = StarA.w;
    p.starLimit = StarB.x;
    p.starMilky = StarB.y;
    p.starRot = StarB.z;
    p.starTilt = StarB.w;
    p.starLayers = StarC.x;
    p.starAA = StarC.y;
    p.starCeil = StarC.z;
    p.pixAngle = 2.0f * CameraUp.w / float(ViewportHeight);
    return p;
}

// Sky frame from world frame (the panel's seam: WorldFrameOf(Sky)).
float3 SkyFrameTransform(float3 w)
{
    float3 s;
    s.x = w.x;
    s.y = w.z;
    s.z = -w.y;
    return s;
}

float3 WorldFrameTransform(float3 s)
{
    float3 w;
    w.x = s.x;
    w.y = -s.z;
    w.z = s.y;
    return w;
}

// Closest-hit bounce against the TLAS with barycentric normals. Non-opaque
// candidates are committed (conservative: alpha-tested geometry counts as a
// hit for DI/GI).
IntersectionStructure TraceBounceCompute(float3 origin, float3 dir, float tmin, float tmax)
{
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tmin;
    ray.Direction = dir;
    ray.TMax = tmax;
    q.TraceRayInline(SceneAcceleration, RAY_FLAG_NONE, 0xFFu, ray);
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            q.CommitNonOpaqueTriangleHit();
        }
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return IntersectionMake(false, float3(0.0f, 0.0f, 0.0f),
                          float3(0.0f, 1.0f, 0.0f), tmax);
    }
    uint inst = q.CommittedInstanceID();
    uint prim = q.CommittedPrimitiveIndex();
    InstanceStructure ci = InstanceExtent[inst];
    uint i0 = IndexExtent[ci.idxBase + prim * 3u + 0u];
    uint i1 = IndexExtent[ci.idxBase + prim * 3u + 1u];
    uint i2 = IndexExtent[ci.idxBase + prim * 3u + 2u];
    float3 n0 = NormalExtent[ci.vtxBase + i0];
    float3 n1 = NormalExtent[ci.vtxBase + i1];
    float3 n2 = NormalExtent[ci.vtxBase + i2];
    float2 bc = q.CommittedTriangleBarycentrics();
    float b0 = 1.0f - bc.x - bc.y;
    float3 n = normalize(n0 * b0 + n1 * bc.x + n2 * bc.y);
    float t = q.CommittedRayT();
    return IntersectionMake(true, origin + dir * t, n, t);
}

bool TraceOccludedQuery(float3 origin, float3 dir, float tmin, float tmax)
{
    RayQuery<RAY_FLAG_TERMINATE_ON_FIRST_HIT | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tmin;
    ray.Direction = dir;
    ray.TMax = tmax;
    q.TraceRayInline(SceneAcceleration, RAY_FLAG_NONE, 0xFFu, ray);
    while (q.Proceed())
    {
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Sky-ambient probe: the panel's uProbe pass (3 atmosphere samples through
// SkyAmbientCompute) as a single thread. The host dispatches this before every
// other entry each frame and barriers on u3; all entries read the result
// through SkyConfigurationFromExchange (p.skyAmb).
[shader("compute")]
[numthreads(1, 1, 1)]
//@@BAN: COMPUTE ENTRY SKYAMBIENT
void SkyAmbient(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x != 0u || tid.y != 0u)
    {
        return;
    }
    SkyConfiguration cp = SkyConfigurationFromExchange();
    SkyAmbientExtent[0] = SkyAmbientCompute(cp);
}

// DI initial candidates: a deterministic mixture over (sun cone, sun cone,
// aureole lobe, sphere). Every candidate evaluates THE sky (SkyRadianceCompute:
// disc + aureole + atmosphere + glow + stars + media) and is weighted by the
// balance heuristic against the
// mixture pdf (1/2 cone + 1/4 lobe + 1/4 sphere) — per-stratum targets without
// the mixture denominator would bias the estimate (MIS requirement).
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY DIRECTINITIAL
void DirectInitial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    SkyConfiguration cp = SkyConfigurationFromExchange();
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    DirectReservoirStructure res = DirectReservoirMake();
    res.m0 = float(InitialCandidates);
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 0u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(SkyFrameTransform(g.normal));
        float3 posW = g.pos + g.normal * 1e-3f;
        for (uint i = 0u; i < InitialCandidates; i++)
        {
            uint s = i % 4u;
            CandidateStructure d;
            if (s <= 1u)
            {
                d = CandidateSunCone(rng, cp);
            }
            else if (s == 2u)
            {
                d = CandidateAureole(rng, cp);
            }
            else
            {
                d = CandidateSphere(rng);
            }
            rng = d.rng;
            // DI lights with THE sky, the same SkyRadianceCompute GI uses.
            float3 leFull = SkyRadianceCompute(d.dir, cp);
            float qmix = 0.5f * DensitySunConeAt(d.dir, cp)
                       + 0.25f * DensityAureoleAt(d.dir, cp)
                       + 0.25f * (1.0f / (4.0f * SkyPi));
            d.pdf = qmix;
            float3 dirW = WorldFrameTransform(d.dir);
            float vis = TraceOccludedQuery(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
            float cosT = max(dot(nSky, d.dir), 0.0f);
            res = DirectReservoirUpdate(res, d, leFull, cosT, g.albedo, rng, vis);
            RandomScalarStructure ru = RandomScalarNext(rng);
            rng = ru.rng;
        }
    }
    DirectReservoirExtent[idx] = res;
}

// DI temporal reuse: one previous-frame neighbour + M-cap.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY DIRECTTEMPORAL
void DirectTemporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    DirectReservoirStructure cur = DirectReservoirExtent[idx];
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 1u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        float3 posW = g.pos + g.normal * 1e-3f;
        int2 prev = int2(tid.xy) + int2(int(g.motion.x), int(g.motion.y));
        if (prev.x >= 0 && prev.y >= 0
            && prev.x < int(ViewportWidth) && prev.y < int(ViewportHeight))
        {
            uint pidx = uint(prev.y) * ViewportWidth + uint(prev.x);
            VisibilityStructure gp = VisibilityExtent[pidx];
            DirectReservoirStructure cad = DirectReservoirPrior[pidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gp.depth - g.depth) < 0.1f * dMax
                       && dot(gp.normal, g.normal) > 0.9f;
            if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                if (cad.m > float(ReservoirCap))
                {
                    cad.wsum *= float(ReservoirCap) / cad.m;
                    cad.m = float(ReservoirCap);
                }
                float3 nSky = normalize(SkyFrameTransform(g.normal));
                float3 nPSky = normalize(SkyFrameTransform(gp.normal));
                cur = DirectReservoirCombine(cur, nSky, g.albedo, cad, nPSky, gp.albedo, rng);
                RandomScalarStructure ru = RandomScalarNext(rng);
                rng = ru.rng;
            }
        }
        if (cur.m > 0.0f)
        {
            float3 dirW = WorldFrameTransform(cur.dir);
            cur.vis = TraceOccludedQuery(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
        }
    }
    DirectReservoirExtent[idx] = cur;
}

// DI spatial reuse: SpatialTaps Poisson-disc neighbours, same guards.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY DIRECTSPATIAL
void DirectSpatial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    DirectReservoirStructure cur = DirectReservoirExtent[idx];
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 2u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(SkyFrameTransform(g.normal));
        float3 posW = g.pos + g.normal * 1e-3f;
        for (uint k = 0u; k < SpatialTaps; k++)
        {
            RandomFloatStructure r1 = RandomFloatNext(rng);
            rng = r1.rng;
            RandomFloatStructure r2 = RandomFloatNext(rng);
            rng = r2.rng;
            float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
            float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
            int2 tap = int2(tid.xy) + int2(int(rr * cos(aa)), int(rr * sin(aa)));
            if (tap.x < 0 || tap.y < 0
                || tap.x >= int(ViewportWidth) || tap.y >= int(ViewportHeight))
            {
                continue;
            }
            uint nidx = uint(tap.y) * ViewportWidth + uint(tap.x);
            VisibilityStructure gn = VisibilityExtent[nidx];
            DirectReservoirStructure cad = DirectReservoirExtent[nidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gn.depth - g.depth) < 0.1f * dMax
                       && dot(gn.normal, g.normal) > 0.9f;
            if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                float3 nNSky = normalize(SkyFrameTransform(gn.normal));
                cur = DirectReservoirCombine(cur, nSky, g.albedo, cad, nNSky, gn.albedo, rng);
                RandomScalarStructure ru = RandomScalarNext(rng);
                rng = ru.rng;
            }
        }
        if (cur.m > 0.0f)
        {
            float3 dirW = WorldFrameTransform(cur.dir);
            cur.vis = TraceOccludedQuery(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
        }
    }
    DirectReservoirExtent[idx] = cur;
}

// GI initial candidates: cosine bounce, one explicit vertex, then the sky.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY INDIRECTINITIAL
void IndirectInitial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    SkyConfiguration cp = SkyConfigurationFromExchange();
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    IndirectReservoirStructure res = IndirectReservoirMake();
    uint m0 = InitialCandidates / 2u;
    res.m0 = float(m0);
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 3u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(SkyFrameTransform(g.normal));
        for (uint i = 0u; i < m0; i++)
        {
            CandidateStructure d = CandidateCosine(rng, nSky);
            rng = d.rng;
            float3 originW = g.pos + g.normal * 1e-3f;
            float3 dirW = WorldFrameTransform(d.dir);
            IntersectionStructure h = TraceBounceCompute(originW, dirW, 1e-3f, 1e6f);
            float cosT = max(dot(nSky, d.dir), 0.0f);
            float3 throughput = g.albedo * (cosT / SkyPi);
            float3 le;
            float pdf = d.pdf;
            if (h.hit)
            {
                float3 n2Sky = normalize(SkyFrameTransform(h.normal));
                CandidateStructure d2 = CandidateCosine(rng, n2Sky);
                rng = d2.rng;
                float3 o2W = h.pos + h.normal * 1e-3f;
                float3 d2W = WorldFrameTransform(d2.dir);
                IntersectionStructure h2 = TraceBounceCompute(o2W, d2W, 1e-3f, 1e6f);
                float cos2 = max(dot(n2Sky, d2.dir), 0.0f);
                throughput = throughput * g.albedo * (cos2 / SkyPi);
                pdf = d.pdf * d2.pdf;
                if (h2.hit)
                {
                    le = float3(0.0f, 0.0f, 0.0f);
                }
                else
                {
                    le = SkyRadianceCompute(d2.dir, cp);
                }
            }
            else
            {
                le = SkyRadianceCompute(d.dir, cp);
            }
            res = IndirectReservoirUpdate(res, d, throughput, le, pdf, rng);
            RandomScalarStructure ru = RandomScalarNext(rng);
            rng = ru.rng;
        }
    }
    IndirectReservoirExtent[idx] = res;
}

// GI temporal reuse + M-cap.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY INDIRECTTEMPORAL
void IndirectTemporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    IndirectReservoirStructure cur = IndirectReservoirExtent[idx];
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 4u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        int2 prev = int2(tid.xy) + int2(int(g.motion.x), int(g.motion.y));
        if (prev.x >= 0 && prev.y >= 0
            && prev.x < int(ViewportWidth) && prev.y < int(ViewportHeight))
        {
            uint pidx = uint(prev.y) * ViewportWidth + uint(prev.x);
            VisibilityStructure gp = VisibilityExtent[pidx];
            IndirectReservoirStructure cad = IndirectReservoirPrior[pidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gp.depth - g.depth) < 0.1f * dMax
                       && dot(gp.normal, g.normal) > 0.9f;
            if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                if (cad.m > float(ReservoirCap))
                {
                    cad.wsum *= float(ReservoirCap) / cad.m;
                    cad.m = float(ReservoirCap);
                }
                cur = IndirectReservoirCombine(cur, cad, rng);
                RandomScalarStructure ru = RandomScalarNext(rng);
                rng = ru.rng;
            }
        }
    }
    IndirectReservoirExtent[idx] = cur;
}

// GI spatial reuse.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY INDIRECTSPATIAL
void IndirectSpatial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    IndirectReservoirStructure cur = IndirectReservoirExtent[idx];
    RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(tid.x, tid.y, TemporalIndex, 5u) ^ RandomSeed);
    if (g.depth > 0.0f)
    {
        for (uint k = 0u; k < SpatialTaps; k++)
        {
            RandomFloatStructure r1 = RandomFloatNext(rng);
            rng = r1.rng;
            RandomFloatStructure r2 = RandomFloatNext(rng);
            rng = r2.rng;
            float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
            float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
            int2 tap = int2(tid.xy) + int2(int(rr * cos(aa)), int(rr * sin(aa)));
            if (tap.x < 0 || tap.y < 0
                || tap.x >= int(ViewportWidth) || tap.y >= int(ViewportHeight))
            {
                continue;
            }
            uint nidx = uint(tap.y) * ViewportWidth + uint(tap.x);
            VisibilityStructure gn = VisibilityExtent[nidx];
            IndirectReservoirStructure cad = IndirectReservoirExtent[nidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gn.depth - g.depth) < 0.1f * dMax
                       && dot(gn.normal, g.normal) > 0.9f;
            if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                cur = IndirectReservoirCombine(cur, cad, rng);
                RandomScalarStructure ru = RandomScalarNext(rng);
                rng = ru.rng;
            }
        }
    }
    IndirectReservoirExtent[idx] = cur;
}

// Shade: backdrop over misses, DI + GI estimates over hits, then post.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY PIXELSHADE
void PixelShade(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    SkyConfiguration cp = SkyConfigurationFromExchange();
    uint idx = tid.y * ViewportWidth + tid.x;
    VisibilityStructure g = VisibilityExtent[idx];
    float3 col;
    if (g.depth <= 0.0f)
    {
        SkyProjection cam;
        cam.fwd = normalize(SkyFrameTransform(float3(CameraForward.x, CameraForward.y, CameraForward.z)));
        cam.right = normalize(SkyFrameTransform(float3(CameraRight.x, CameraRight.y, CameraRight.z)));
        cam.up = normalize(SkyFrameTransform(float3(CameraUp.x, CameraUp.y, CameraUp.z)));
        cam.tanHalf = CameraUp.w;
        float3 dir = SkyFrameTransform(normalize(g.pos));
        col = SkyPixelCompute(dir, cam, cp);
    }
    else
    {
        DirectReservoirStructure rd = DirectReservoirExtent[idx];
        IndirectReservoirStructure rg = IndirectReservoirExtent[idx];
        float3 nSky = normalize(SkyFrameTransform(g.normal));
        float cosT = max(dot(nSky, rd.dir), 0.0f);
        float pDI = (rd.m > 0.0f) ? DirectTargetCompute(rd.le, cosT, g.albedo) : 0.0f;
        float wDI = (pDI > 0.0f) ? rd.wsum / max(rd.m * pDI, 1e-12f) : 0.0f;
        float3 di = wDI * rd.le * rd.vis * cosT * g.albedo / SkyPi;
        float pGI = (rg.m > 0.0f) ? IndirectTargetCompute(rg.throughput, rg.le) : 0.0f;
        float wGI = (pGI > 0.0f) ? rg.wsum / max(rg.m * pGI, 1e-12f) : 0.0f;
        float3 gi = wGI * rg.throughput * rg.le;
        col = di + gi;
    }
    float2 uv = ViewportUVCompute(tid.x, tid.y, ViewportWidth, ViewportHeight);
    float3 ldr = SkyPostApply(col, uv.x, uv.y, float(ViewportWidth), float(ViewportHeight),
                              tid.x, tid.y, cp);
    DisplayExtent[idx] = float4(ldr, 1.0f);
}

// Reference viewport: the sky backdrop alone (no ReSTIR), for the parity gate.
[shader("compute")]
[numthreads(8, 8, 1)]
//@@BAN: COMPUTE ENTRY SKYVIEWPORT
void SkyViewport(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ViewportWidth || tid.y >= ViewportHeight)
    {
        return;
    }
    SkyConfiguration cp = SkyConfigurationFromExchange();
    uint idx = tid.y * ViewportWidth + tid.x;
    float2 uv = ViewportUVCompute(tid.x, tid.y, ViewportWidth, ViewportHeight);
    SkyProjection cam;
    cam.fwd = normalize(SkyFrameTransform(float3(CameraForward.x, CameraForward.y, CameraForward.z)));
    cam.right = normalize(SkyFrameTransform(float3(CameraRight.x, CameraRight.y, CameraRight.z)));
    cam.up = normalize(SkyFrameTransform(float3(CameraUp.x, CameraUp.y, CameraUp.z)));
    cam.tanHalf = CameraUp.w;
    float3 dir = ViewportDirectionCompute(cam, uv);
    float3 hdr = SkyPixelCompute(dir, cam, cp);
    float3 ldr = SkyPostApply(hdr, uv.x, uv.y, float(ViewportWidth), float(ViewportHeight),
                              tid.x, tid.y, cp);
    DisplayExtent[idx] = float4(ldr, 1.0f);
}

#endif // SLANG_COMPAT_CXX (Slang-only shell)

#endif // PROJECT_ZERO_CELESTIAL_RESTIR_SLANG

#endif // RESTIR_SEQUENCE_INCLUDED
