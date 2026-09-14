//================================================================================
// CelestialReSTIR.slang — ReSTIR direct + indirect lighting for the celestial
// sky. THE ONLY lighting in Project Zero: every shaded pixel is either a sky
// backdrop (display, not lighting) or a ReSTIR estimate (DI + GI). There is
// no ambient term, no analytic sun lamp, no legacy path.
//
// Layout:
//   - DUAL REGION (pure functions, same contract as CelestialCore.slang):
//     deterministic RNG, candidate draws, DI/GI reservoirs, RIS update/combine.
//     Compiled as C++17 by the Host convergence harness AND as Slang.
//   - SLANG-ONLY SHELL (#ifndef SLANG_COMPAT_CXX): uniform buffers, G-buffer
//     and reservoir descriptors, TLAS ray queries, the 8 compute entries.
//
// Authoritative algorithm references: the panel (for every Le term) and the
// ReSTIR papers (for the reservoir mechanics). The Host harness
// (Host/ReSTIRConvergence.cpp) mirrors these kernels over an analytic scene
// and gates them against independent brute-force references.
//================================================================================
#ifndef PROJECT_ZERO_CELESTIAL_RESTIR_SLANG
#define PROJECT_ZERO_CELESTIAL_RESTIR_SLANG

#include "CelestialCore.slang"

//--------------------------------------------------------------------------------
// Dual region: RNG, draws, reservoirs.
//--------------------------------------------------------------------------------
struct CelRng
{
    uint state;
};

CelRng celRngMake(uint state)
{
    CelRng r;
    r.state = state;
    return r;
}

// PCG32 output permutation as a stateless hash (O'Neill): avalanche-mixes the
// LCG state so low seed bits still decorrelate neighbouring pixels/frames.
uint celPCGHash(uint seed)
{
    uint x = seed * 747796405u + 2891336453u;
    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (w >> 22u) ^ w;
}

// Seed stream: frame-major, then pixel, then kernel tag (0 = diInitial,
// 1 = diTemporal, 2 = diSpatial, 3 = giInitial, 4 = giTemporal, 5 = giSpatial;
// shade/skyViewport draw no random numbers).
uint celRngSeed(uint x, uint y, uint frame, uint tag)
{
    uint h = (frame * 73856093u) ^ (y * 19349663u) ^ (x * 83492791u);
    h = h ^ (tag * 2971215073u);
    return celPCGHash(h);
}

struct CelRandU
{
    CelRng rng;
    uint u;
};

struct CelRandF
{
    CelRng rng;
    float f;
};

CelRandU celRandUMake(in CelRng rng, uint u)
{
    CelRandU o;
    o.rng = rng;
    o.u = u;
    return o;
}

CelRandF celRandFMake(in CelRng rng, float f)
{
    CelRandF o;
    o.rng = rng;
    o.f = f;
    return o;
}

// LCG step + integer avalanche (splitmix64-style fmix32): full-period,
// branch-free, identical on CPU and GPU.
CelRandU celRandNextU(in CelRng rngIn)
{
    CelRng rng = rngIn;
    rng.state = rng.state * 1664525u + 1013904223u;
    uint x = rng.state;
    x = ((x >> 16u) ^ x) * 73244475u;
    x = ((x >> 16u) ^ x) * 73244475u;
    x = (x >> 16u) ^ x;
    return celRandUMake(rng, x);
}

// [0, 1): 24-bit mantissa fill, never returning 1.0.
CelRandF celRandNextF(in CelRng rngIn)
{
    CelRandU ru = celRandNextU(rngIn);
    float f = float(ru.u >> 8u) * (1.0f / 16777216.0f);
    return celRandFMake(ru.rng, f);
}

struct CelDraw
{
    CelRng rng;   // advanced RNG (functional threading, no inout)
    float3 dir;   // sky frame
    float pdf;    // the draw's own pdf (for the joint-pdf chain)
};

CelDraw celDrawMake(in CelRng rng, float3 dir, float pdf)
{
    CelDraw d;
    d.rng = rng;
    d.dir = dir;
    d.pdf = pdf;
    return d;
}

CelDraw celDrawSphere(in CelRng rngIn)
{
    CelRandF r1 = celRandNextF(rngIn);
    CelRandF r2 = celRandNextF(r1.rng);
    float z = 1.0f - 2.0f * r1.f;
    float a = 6.2831853f * r2.f;
    float s = sqrt(max(1.0f - z * z, 0.0f));
    float3 dir;
    dir.x = s * cos(a);
    dir.y = z;
    dir.z = s * sin(a);
    return celDrawMake(r2.rng, dir, 1.0f / (4.0f * CEL_PI));
}

// Uniform cone around the sun, sized to the panel's disc + soft edge.
CelDraw celDrawSunCone(in CelRng rngIn, in CelParams p)
{
    float cosR = cos(p.sunAngularRadius * 1.6f);
    CelRandF r1 = celRandNextF(rngIn);
    CelRandF r2 = celRandNextF(r1.rng);
    float z = 1.0f - r1.f * (1.0f - cosR);
    float a = 6.2831853f * r2.f;
    float s = sqrt(max(1.0f - z * z, 0.0f));
    float3 up = abs(p.sunDir.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                        : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, p.sunDir));
    float3 tv = cross(p.sunDir, tu);
    float3 dir = tu * (s * cos(a)) + tv * (s * sin(a)) + p.sunDir * z;
    float pdf = 1.0f / max(6.2831853f * (1.0f - cosR), 1e-9f);
    return celDrawMake(r2.rng, dir, pdf);
}

// Photon-map-era power-lobe sampler (n = 8, Lafortune/Blinn): concentrate
// candidates in the aureole the panel actually renders.
CelDraw celDrawAureole(in CelRng rngIn, in CelParams p)
{
    CelRandF r1 = celRandNextF(rngIn);
    CelRandF r2 = celRandNextF(r1.rng);
    float cp = celPow(max(1.0f - r1.f, 1e-9f), 1.0f / 9.0f);
    float sp = sqrt(max(1.0f - cp * cp, 0.0f));
    float a = 6.2831853f * r2.f;
    float3 up = abs(p.sunDir.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                        : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, p.sunDir));
    float3 tv = cross(p.sunDir, tu);
    float3 dir = tu * (sp * cos(a)) + tv * (sp * sin(a)) + p.sunDir * cp;
    float pdf = 9.0f * celPow(max(cp, 1e-9f), 8.0f) / (6.2831853f * 2.0f);
    return celDrawMake(r2.rng, dir, pdf);
}

CelDraw celDrawCosine(in CelRng rngIn, float3 n)
{
    CelRandF r1 = celRandNextF(rngIn);
    CelRandF r2 = celRandNextF(r1.rng);
    float a = 6.2831853f * r1.f;
    float sr = sqrt(max(r2.f, 0.0f));
    float3 up = abs(n.y) > 0.99f ? float3(1.0f, 0.0f, 0.0f)
                                 : float3(0.0f, 1.0f, 0.0f);
    float3 tu = normalize(cross(up, n));
    float3 tv = cross(n, tu);
    float3 dir = tu * (sr * cos(a)) + tv * (sr * sin(a))
               + n * sqrt(max(1.0f - r2.f, 0.0f));
    float pdf = max(dot(n, dir), 0.0f) / CEL_PI;
    return celDrawMake(r2.rng, dir, pdf);
}

// Technique pdfs evaluated at an arbitrary direction (balance heuristic).
// Formulas match celDrawSunCone / celDrawAureole exactly.
float celPdfSunConeAt(float3 dir, in CelParams p)
{
    float cosR = cos(p.sunAngularRadius * 1.6f);
    float c = dot(dir, p.sunDir);
    float omega = 6.2831853f * (1.0f - cosR);
    return (c >= cosR) ? 1.0f / max(omega, 1e-9f) : 0.0f;
}

float celPdfAureoleAt(float3 dir, in CelParams p)
{
    float cp = dot(dir, p.sunDir);
    if (cp <= 0.0f)
    {
        return 0.0f;
    }
    return 9.0f * celPow(cp, 8.0f) / (6.2831853f * 2.0f);
}

struct CelResDI
{
    float3 dir;   // winning direction, sky frame
    float3 le;    // winning emission (panel Le, unshadowed)
    float pdf;    // candidate pdf of the winner
    float wsum;   // RIS weight sum
    float m;      // candidate count
    float m0;     // initial candidates this frame (for the M-cap)
    float vis;    // visibility of the winner (0/1)
};

struct CelResGI
{
    float3 dir;         // winning first-bounce direction, sky frame
    float3 throughput;  // Lambert chain WITHOUT sampling pdfs (they live in pdf)
    float3 le;          // emission at the path end
    float pdf;          // joint candidate pdf
    float wsum;
    float m;
    float m0;
};

CelResDI celResDIMake()
{
    CelResDI r;
    r.dir = float3(0.0f, 0.0f, 0.0f);
    r.le = float3(0.0f, 0.0f, 0.0f);
    r.pdf = 0.0f;
    r.wsum = 0.0f;
    r.m = 0.0f;
    r.m0 = 0.0f;
    r.vis = 0.0f;
    return r;
}

CelResGI celResGIMake()
{
    CelResGI r;
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
float celTargetDI(float3 le, float cosT, float3 albedo)
{
    return celLuminanceDW(le * albedo * (cosT / CEL_PI));
}

float celTargetGI(float3 throughput, float3 le)
{
    return celLuminanceDW(throughput * le);
}

// Streaming RIS update (ReSTIR eq. 3): w = target / pdf.
CelResDI celUpdateDI(in CelResDI acc, in CelDraw d, float3 le, float cosT,
                     float3 albedo, in CelRng rngIn, float vis)
{
    float target = celTargetDI(le, cosT, albedo);
    float w = (d.pdf > 0.0f) ? target / d.pdf : 0.0f;
    CelResDI o = acc;
    o.wsum = acc.wsum + w;
    o.m = acc.m + 1.0f;
    CelRandF ru = celRandNextF(rngIn);
    if (ru.f * o.wsum < w && w > 0.0f)
    {
        o.dir = d.dir;
        o.le = le;
        o.pdf = d.pdf;
        o.vis = vis;
    }
    return o;
}

CelResGI celUpdateGI(in CelResGI acc, in CelDraw d, float3 throughput, float3 le,
                     float pdf, in CelRng rngIn)
{
    float target = celTargetGI(throughput, le);
    float w = (pdf > 0.0f) ? target / pdf : 0.0f;
    CelResGI o = acc;
    o.wsum = acc.wsum + w;
    o.m = acc.m + 1.0f;
    CelRandF ru = celRandNextF(rngIn);
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
CelResDI celCombineDI(in CelResDI acc, float3 nAcc, float3 albAcc,
                       in CelResDI b, float3 nB, float3 albB, in CelRng rngIn)
{
    float cosB = max(dot(nAcc, b.dir), 0.0f);
    float pB = (b.m > 0.0f) ? celTargetDI(b.le, cosB, albAcc) : 0.0f;
    float wA = (acc.m > 0.0f) ? acc.wsum : 0.0f;
    float wB = 0.0f;
    if (b.m > 0.0f)
    {
        float cosBb = max(dot(nB, b.dir), 0.0f);
        float pBb = celTargetDI(b.le, cosBb, albB);
        wB = (pBb > 0.0f) ? pB * b.wsum / max(pBb, 1e-12f) : 0.0f;
    }
    CelResDI o = acc;
    o.wsum = wA + wB;
    o.m = acc.m + b.m;
    CelRandF ru = celRandNextF(rngIn);
    if (ru.f * o.wsum < wB && wB > 0.0f)
    {
        o.dir = b.dir;
        o.le = b.le;
        o.pdf = b.pdf;
        o.vis = b.vis;
    }
    return o;
}

CelResGI celCombineGI(in CelResGI acc, in CelResGI b, in CelRng rngIn)
{
    float wA = (acc.m > 0.0f) ? acc.wsum : 0.0f;
    float wB = (b.m > 0.0f) ? b.wsum : 0.0f;
    CelResGI o = acc;
    o.wsum = wA + wB;
    o.m = acc.m + b.m;
    CelRandF ru = celRandNextF(rngIn);
    if (ru.f * o.wsum < wB && wB > 0.0f)
    {
        o.dir = b.dir;
        o.throughput = b.throughput;
        o.le = b.le;
        o.pdf = b.pdf;
    }
    return o;
}

//--------------------------------------------------------------------------------
// Slang-only shell: descriptors, ray queries, compute entries.
// (Still inside the include guard; excluded from the C++ path.)
//--------------------------------------------------------------------------------
#ifndef SLANG_COMPAT_CXX

// 16-byte aligned frame constants (std140/HLSL compatible: every member is a
// float4, offsets are multiples of 16 whatever the target).
cbuffer CelFrame : register(b0)
{
    float4 gSunDir;     // xyz = sunDir, w unused
    float4 gSunColor;   // rgb = kelvin color, w = elevationDeg
    float4 gSunParams;  // intensity, angRadius, softness, discBoost
    float4 gAtmoA;      // rayleigh, mie, mieG, ozone
    float4 gAtmoB;      // planetR, atmoH, rayleighH, mieH
    float4 gSkyTint;    // rgb = tint, w = skyBright
    float4 gGround;     // rgb = albedo, w = groundBright
    float4 gPostA;      // dawnI, lineI, lineAuto, evStops
    float4 gPostB;      // tonemap, bloom, vignette, grain
    float4 gMisc;       // camHeight, timeSeconds, unused, unused
};

cbuffer CelLaunch : register(b1)
{
    uint gWidth;
    uint gHeight;
    uint gFrame;
    uint gM0;       // initial candidates per pixel (DI; GI uses gM0 / 2)
    uint gMCap;     // temporal M cap (20)
    uint gSpatTaps; // spatial taps (4)
    uint gSeed;     // run seed (0 for the deterministic proof)
    uint gPad;
};

// World-frame camera basis for the skyViewport entry (the G-buffer pass owns
// the camera for every other entry).
cbuffer CelCam : register(b2)
{
    float4 gCamFwd;     // xyz = fwd, w unused
    float4 gCamRight;   // xyz = right, w unused
    float4 gCamUp;      // xyz = up, w = tanHalf
    float4 gCamPad;
};

struct CelGBuffer
{
    float3 pos;         // [m] world frame; background pixels (depth <= 0)
                        // carry the world-frame primary ray direction here
    float3 normal;      // [-] world frame, unit
    float3 albedo;      // [-]
    float depth;        // [m] primary-ray distance; <= 0 = background
    float2 motion;      // [px] previous-frame position = pixel + motion
};

struct CelInstance
{
    uint vtxBase;
    uint idxBase;
    uint pad0;
    uint pad1;
};

struct CelHit
{
    bool hit;
    float3 pos;     // world frame
    float3 normal;  // world frame, unit
    float t;
};

CelHit celHitMake(bool hit, float3 pos, float3 normal, float t)
{
    CelHit h;
    h.hit = hit;
    h.pos = pos;
    h.normal = normal;
    h.t = t;
    return h;
}

StructuredBuffer<CelGBuffer> gGBuffer : register(t0);
RaytracingAccelerationStructure gScene : register(t1);
StructuredBuffer<float3> gPositions : register(t2);
StructuredBuffer<float3> gNormals : register(t3);
StructuredBuffer<uint> gIndices : register(t4);
StructuredBuffer<CelInstance> gInstances : register(t5);
StructuredBuffer<CelResDI> gResDIRead : register(t6);
StructuredBuffer<CelResGI> gResGIRead : register(t7);
RWStructuredBuffer<CelResDI> gResDIWrite : register(u0);
RWStructuredBuffer<CelResGI> gResGIWrite : register(u1);
RWStructuredBuffer<float4> gOutColor : register(u2);

CelParams celParamsFromGPU()
{
    CelParams p;
    p.sunDir = float3(gSunDir.x, gSunDir.y, gSunDir.z);
    p.sunColor = float3(gSunColor.x, gSunColor.y, gSunColor.z);
    p.sunElevationDeg = gSunColor.w;
    p.sunIntensity = gSunParams.x;
    p.sunAngularRadius = gSunParams.y;
    p.sunSoftness = gSunParams.z;
    p.sunDiscBoost = gSunParams.w;
    p.rayleigh = gAtmoA.x;
    p.mie = gAtmoA.y;
    p.mieG = gAtmoA.z;
    p.ozone = gAtmoA.w;
    p.planetRadius = gAtmoB.x;
    p.atmoHeight = gAtmoB.y;
    p.rayleighH = gAtmoB.z;
    p.mieH = gAtmoB.w;
    p.skyTint = float3(gSkyTint.x, gSkyTint.y, gSkyTint.z);
    p.skyBright = gSkyTint.w;
    p.groundAlbedo = float3(gGround.x, gGround.y, gGround.z);
    p.groundBright = gGround.w;
    p.dawnIntensity = gPostA.x;
    p.lineIntensity = gPostA.y;
    p.lineAuto = uint(gPostA.z);
    p.exposureStops = gPostA.w;
    p.tonemap = uint(gPostB.x);
    p.bloom = gPostB.y;
    p.vignette = gPostB.z;
    p.grain = gPostB.w;
    p.camHeight = gMisc.x;
    p.timeSeconds = gMisc.y;
    return p;
}

// Sky frame from world frame (the panel's seam: WorldFrameOf(Sky)).
float3 celSkyFrameOf(float3 w)
{
    float3 s;
    s.x = w.x;
    s.y = w.z;
    s.z = -w.y;
    return s;
}

float3 celWorldFrameOf(float3 s)
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
CelHit celTraceBounce(float3 origin, float3 dir, float tmin, float tmax)
{
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tmin;
    ray.Direction = dir;
    ray.TMax = tmax;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFFu, ray);
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            q.CommitNonOpaqueTriangleHit();
        }
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return celHitMake(false, float3(0.0f, 0.0f, 0.0f),
                          float3(0.0f, 1.0f, 0.0f), tmax);
    }
    uint inst = q.CommittedInstanceID();
    uint prim = q.CommittedPrimitiveIndex();
    CelInstance ci = gInstances[inst];
    uint i0 = gIndices[ci.idxBase + prim * 3u + 0u];
    uint i1 = gIndices[ci.idxBase + prim * 3u + 1u];
    uint i2 = gIndices[ci.idxBase + prim * 3u + 2u];
    float3 n0 = gNormals[ci.vtxBase + i0];
    float3 n1 = gNormals[ci.vtxBase + i1];
    float3 n2 = gNormals[ci.vtxBase + i2];
    float2 bc = q.CommittedTriangleBarycentrics();
    float b0 = 1.0f - bc.x - bc.y;
    float3 n = normalize(n0 * b0 + n1 * bc.x + n2 * bc.y);
    float t = q.CommittedRayT();
    return celHitMake(true, origin + dir * t, n, t);
}

bool celTraceOccluded(float3 origin, float3 dir, float tmin, float tmax)
{
    RayQuery<RAY_FLAG_TERMINATE_ON_FIRST_HIT | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tmin;
    ray.Direction = dir;
    ray.TMax = tmax;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFFu, ray);
    while (q.Proceed())
    {
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// DI initial candidates: a deterministic mixture over (sun cone, sun cone,
// aureole lobe, sphere). Every candidate evaluates the FULL emission
// (disc + aureole + sky) and is weighted by the balance heuristic against the
// mixture pdf (1/2 cone + 1/4 lobe + 1/4 sphere) — per-stratum targets without
// the mixture denominator would bias the estimate (MIS requirement).
[shader("compute")]
[numthreads(8, 8, 1)]
void diInitial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    CelParams cp = celParamsFromGPU();
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResDI res = celResDIMake();
    res.m0 = float(gM0);
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 0u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(celSkyFrameOf(g.normal));
        float3 posW = g.pos + g.normal * 1e-3f;
        for (uint i = 0u; i < gM0; i++)
        {
            uint s = i % 4u;
            CelDraw d;
            if (s <= 1u)
            {
                d = celDrawSunCone(rng, cp);
            }
            else if (s == 2u)
            {
                d = celDrawAureole(rng, cp);
            }
            else
            {
                d = celDrawSphere(rng);
            }
            rng = d.rng;
            CelAtmo f = celSkyField(d.dir, cp);
            float3 leFull = celSunDisc(d.dir, f.trans, cp)
                          + celSunAureole(d.dir, f.trans, cp)
                          + ((d.dir.y > 0.0f)
                             ? f.sky : float3(0.0f, 0.0f, 0.0f));
            float qmix = 0.5f * celPdfSunConeAt(d.dir, cp)
                       + 0.25f * celPdfAureoleAt(d.dir, cp)
                       + 0.25f * (1.0f / (4.0f * CEL_PI));
            d.pdf = qmix;
            float3 dirW = celWorldFrameOf(d.dir);
            float vis = celTraceOccluded(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
            float cosT = max(dot(nSky, d.dir), 0.0f);
            res = celUpdateDI(res, d, leFull, cosT, g.albedo, rng, vis);
            CelRandU ru = celRandNextU(rng);
            rng = ru.rng;
        }
    }
    gResDIWrite[idx] = res;
}

// DI temporal reuse: one previous-frame neighbour + M-cap.
[shader("compute")]
[numthreads(8, 8, 1)]
void diTemporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResDI cur = gResDIWrite[idx];
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 1u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        float3 posW = g.pos + g.normal * 1e-3f;
        int2 prev = int2(tid.xy) + int2(int(g.motion.x), int(g.motion.y));
        if (prev.x >= 0 && prev.y >= 0
            && prev.x < int(gWidth) && prev.y < int(gHeight))
        {
            uint pidx = uint(prev.y) * gWidth + uint(prev.x);
            CelGBuffer gp = gGBuffer[pidx];
            CelResDI cad = gResDIRead[pidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gp.depth - g.depth) < 0.1f * dMax
                       && dot(gp.normal, g.normal) > 0.9f;
            if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                if (cad.m > float(gMCap))
                {
                    cad.wsum *= float(gMCap) / cad.m;
                    cad.m = float(gMCap);
                }
                float3 nSky = normalize(celSkyFrameOf(g.normal));
                float3 nPSky = normalize(celSkyFrameOf(gp.normal));
                cur = celCombineDI(cur, nSky, g.albedo, cad, nPSky, gp.albedo, rng);
                CelRandU ru = celRandNextU(rng);
                rng = ru.rng;
            }
        }
        if (cur.m > 0.0f)
        {
            float3 dirW = celWorldFrameOf(cur.dir);
            cur.vis = celTraceOccluded(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
        }
    }
    gResDIWrite[idx] = cur;
}

// DI spatial reuse: gSpatTaps Poisson-disc neighbours, same guards.
[shader("compute")]
[numthreads(8, 8, 1)]
void diSpatial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResDI cur = gResDIWrite[idx];
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 2u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(celSkyFrameOf(g.normal));
        float3 posW = g.pos + g.normal * 1e-3f;
        for (uint k = 0u; k < gSpatTaps; k++)
        {
            CelRandF r1 = celRandNextF(rng);
            rng = r1.rng;
            CelRandF r2 = celRandNextF(rng);
            rng = r2.rng;
            float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
            float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
            int2 tap = int2(tid.xy) + int2(int(rr * cos(aa)), int(rr * sin(aa)));
            if (tap.x < 0 || tap.y < 0
                || tap.x >= int(gWidth) || tap.y >= int(gHeight))
            {
                continue;
            }
            uint nidx = uint(tap.y) * gWidth + uint(tap.x);
            CelGBuffer gn = gGBuffer[nidx];
            CelResDI cad = gResDIWrite[nidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gn.depth - g.depth) < 0.1f * dMax
                       && dot(gn.normal, g.normal) > 0.9f;
            if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                float3 nNSky = normalize(celSkyFrameOf(gn.normal));
                cur = celCombineDI(cur, nSky, g.albedo, cad, nNSky, gn.albedo, rng);
                CelRandU ru = celRandNextU(rng);
                rng = ru.rng;
            }
        }
        if (cur.m > 0.0f)
        {
            float3 dirW = celWorldFrameOf(cur.dir);
            cur.vis = celTraceOccluded(posW, dirW, 1e-3f, 1e6f) ? 0.0f : 1.0f;
        }
    }
    gResDIWrite[idx] = cur;
}

// GI initial candidates: cosine bounce, one explicit vertex, then the sky.
[shader("compute")]
[numthreads(8, 8, 1)]
void giInitial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    CelParams cp = celParamsFromGPU();
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResGI res = celResGIMake();
    uint m0 = gM0 / 2u;
    res.m0 = float(m0);
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 3u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        float3 nSky = normalize(celSkyFrameOf(g.normal));
        for (uint i = 0u; i < m0; i++)
        {
            CelDraw d = celDrawCosine(rng, nSky);
            rng = d.rng;
            float3 originW = g.pos + g.normal * 1e-3f;
            float3 dirW = celWorldFrameOf(d.dir);
            CelHit h = celTraceBounce(originW, dirW, 1e-3f, 1e6f);
            float cosT = max(dot(nSky, d.dir), 0.0f);
            float3 throughput = g.albedo * (cosT / CEL_PI);
            float3 le;
            float pdf = d.pdf;
            if (h.hit)
            {
                float3 n2Sky = normalize(celSkyFrameOf(h.normal));
                CelDraw d2 = celDrawCosine(rng, n2Sky);
                rng = d2.rng;
                float3 o2W = h.pos + h.normal * 1e-3f;
                float3 d2W = celWorldFrameOf(d2.dir);
                CelHit h2 = celTraceBounce(o2W, d2W, 1e-3f, 1e6f);
                float cos2 = max(dot(n2Sky, d2.dir), 0.0f);
                throughput = throughput * g.albedo * (cos2 / CEL_PI);
                pdf = d.pdf * d2.pdf;
                if (h2.hit)
                {
                    le = float3(0.0f, 0.0f, 0.0f);
                }
                else
                {
                    le = celSkyFull(d2.dir, cp);
                }
            }
            else
            {
                le = celSkyFull(d.dir, cp);
            }
            res = celUpdateGI(res, d, throughput, le, pdf, rng);
            CelRandU ru = celRandNextU(rng);
            rng = ru.rng;
        }
    }
    gResGIWrite[idx] = res;
}

// GI temporal reuse + M-cap.
[shader("compute")]
[numthreads(8, 8, 1)]
void giTemporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResGI cur = gResGIWrite[idx];
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 4u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        int2 prev = int2(tid.xy) + int2(int(g.motion.x), int(g.motion.y));
        if (prev.x >= 0 && prev.y >= 0
            && prev.x < int(gWidth) && prev.y < int(gHeight))
        {
            uint pidx = uint(prev.y) * gWidth + uint(prev.x);
            CelGBuffer gp = gGBuffer[pidx];
            CelResGI cad = gResGIRead[pidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gp.depth - g.depth) < 0.1f * dMax
                       && dot(gp.normal, g.normal) > 0.9f;
            if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                if (cad.m > float(gMCap))
                {
                    cad.wsum *= float(gMCap) / cad.m;
                    cad.m = float(gMCap);
                }
                cur = celCombineGI(cur, cad, rng);
                CelRandU ru = celRandNextU(rng);
                rng = ru.rng;
            }
        }
    }
    gResGIWrite[idx] = cur;
}

// GI spatial reuse.
[shader("compute")]
[numthreads(8, 8, 1)]
void giSpatial(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    CelResGI cur = gResGIWrite[idx];
    CelRng rng = celRngMake(celRngSeed(tid.x, tid.y, gFrame, 5u) ^ gSeed);
    if (g.depth > 0.0f)
    {
        for (uint k = 0u; k < gSpatTaps; k++)
        {
            CelRandF r1 = celRandNextF(rng);
            rng = r1.rng;
            CelRandF r2 = celRandNextF(rng);
            rng = r2.rng;
            float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
            float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
            int2 tap = int2(tid.xy) + int2(int(rr * cos(aa)), int(rr * sin(aa)));
            if (tap.x < 0 || tap.y < 0
                || tap.x >= int(gWidth) || tap.y >= int(gHeight))
            {
                continue;
            }
            uint nidx = uint(tap.y) * gWidth + uint(tap.x);
            CelGBuffer gn = gGBuffer[nidx];
            CelResGI cad = gResGIWrite[nidx];
            float dMax = max(g.depth, 0.1f);
            bool geomOk = abs(gn.depth - g.depth) < 0.1f * dMax
                       && dot(gn.normal, g.normal) > 0.9f;
            if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
            {
                cur = celCombineGI(cur, cad, rng);
                CelRandU ru = celRandNextU(rng);
                rng = ru.rng;
            }
        }
    }
    gResGIWrite[idx] = cur;
}

// Shade: backdrop over misses, DI + GI estimates over hits, then post.
[shader("compute")]
[numthreads(8, 8, 1)]
void shade(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    CelParams cp = celParamsFromGPU();
    uint idx = tid.y * gWidth + tid.x;
    CelGBuffer g = gGBuffer[idx];
    float3 col;
    if (g.depth <= 0.0f)
    {
        float3 dir = celSkyFrameOf(normalize(g.pos));
        col = celSkyFull(dir, cp);
    }
    else
    {
        CelResDI rd = gResDIWrite[idx];
        CelResGI rg = gResGIWrite[idx];
        float3 nSky = normalize(celSkyFrameOf(g.normal));
        float cosT = max(dot(nSky, rd.dir), 0.0f);
        float pDI = (rd.m > 0.0f) ? celTargetDI(rd.le, cosT, g.albedo) : 0.0f;
        float wDI = (pDI > 0.0f) ? rd.wsum / max(rd.m * pDI, 1e-12f) : 0.0f;
        float3 di = wDI * rd.le * rd.vis * cosT * g.albedo / CEL_PI;
        float pGI = (rg.m > 0.0f) ? celTargetGI(rg.throughput, rg.le) : 0.0f;
        float wGI = (pGI > 0.0f) ? rg.wsum / max(rg.m * pGI, 1e-12f) : 0.0f;
        float3 gi = wGI * rg.throughput * rg.le;
        col = di + gi;
    }
    float2 uv = celViewportUV(tid.x, tid.y, gWidth, gHeight);
    float3 ldr = celApplyPost(col, uv.x, uv.y, float(gWidth), float(gHeight),
                              tid.x, tid.y, cp);
    gOutColor[idx] = float4(ldr, 1.0f);
}

// Reference viewport: the sky backdrop alone (no ReSTIR), for the parity gate.
[shader("compute")]
[numthreads(8, 8, 1)]
void skyViewport(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gWidth || tid.y >= gHeight)
    {
        return;
    }
    CelParams cp = celParamsFromGPU();
    uint idx = tid.y * gWidth + tid.x;
    float2 uv = celViewportUV(tid.x, tid.y, gWidth, gHeight);
    CelCamera cam;
    cam.fwd = normalize(celSkyFrameOf(float3(gCamFwd.x, gCamFwd.y, gCamFwd.z)));
    cam.right = normalize(celSkyFrameOf(float3(gCamRight.x, gCamRight.y, gCamRight.z)));
    cam.up = normalize(celSkyFrameOf(float3(gCamUp.x, gCamUp.y, gCamUp.z)));
    cam.tanHalf = gCamUp.w;
    float3 dir = celViewportDir(cam, uv);
    float3 hdr = celSkyFull(dir, cp);
    float3 ldr = celApplyPost(hdr, uv.x, uv.y, float(gWidth), float(gHeight),
                              tid.x, tid.y, cp);
    gOutColor[idx] = float4(ldr, 1.0f);
}

#endif // SLANG_COMPAT_CXX (Slang-only shell)

#endif // PROJECT_ZERO_CELESTIAL_RESTIR_SLANG
