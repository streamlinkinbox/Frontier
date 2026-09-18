//============================================================================================================================================
// 📦 Frontier/Shaders/FogSpecification.slang — Atmospheric and Local Volumetric Fog (dual Slang/C++17)
// Dual-compile: this file is the shipped GPU source AND the CPU implementation
// (via SlangInterchange.h). Pure functions only: no globals, no textures, no
// ray queries, no inout/out params (C++ passes by value). All march loops are
// fixed-count and deterministic: the same inputs produce bit-identical outputs
// on CPU and GPU by construction (integer-hash noise, no sin anywhere).
//
// Theory: Wronski 2014 (froxel pipeline), Hillaire/Frostbite 2015 (closed-form
// slab integral), Crytek 2007 (analytic box/sphere volumes), Lin et al. 2021
// (Volumetric ReSTIR: volumes reuse the winning ReSTIR DI light sample).
// See Reviews/Fog-Research-2026-09-14.md.
#ifndef FOG_SPECIFICATION_INCLUDED
#define FOG_SPECIFICATION_INCLUDED

#include "SkySpecification.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                 FOG SHAPES AND LIMITS                                                  
//------------------------------------------------------------------------------------------------------------------------

static const uint FogShapeBox = 0u;
static const uint FogShapeSphere = 1u;
static const uint FogVolumeLimit = 4u;

//------------------------------------------------------------------------------------------------------------------------
//                                                     FOG STRUCTURES                                                     
//------------------------------------------------------------------------------------------------------------------------

struct FogVolumeStructure
{
    uint shape;                 // [-] 0 box, 1 sphere
    float3 center;              // [m] volume center in world space
    float3 extents;             // [m] box half-extents, or sphere radius in .x
    float density;              // [1/m] peak extinction coefficient
    float heightFalloff;        // [1/m] vertical thinning above the center
    float3 albedo;              // [0..1] single-scatter albedo
    float phaseG;               // [-1..1] Henyey-Greenstein asymmetry
    float3 emissive;            // [lux] source term per unit extinction
    float noiseAmount;          // [0..1] density modulation by noise
    float noiseScale;           // [1/m] noise spatial frequency
};

struct FogConfiguration
{
    float globalDensity;        // [1/m] height-fog extinction at/below base
    float globalHeightFalloff;  // [1/m] height-fog vertical falloff
    float globalBaseHeight;     // [m] height-fog full-density ceiling
    float3 globalAlbedo;        // [0..1] height-fog single-scatter albedo
    float globalPhaseG;         // [-1..1] height-fog HG asymmetry
    float3 ambientRadiance;     // [lux] sky ambient (diffuse scatter stand-in)
    float timeSeconds;          // [s] animation clock for noise drift
    uint volumeCount;           // [count] active volumes (<= FogVolumeLimit)
    uint marchSteps;            // [steps] fixed march steps per segment
    float maxMarchDistance;     // [m] march far plane
    FogVolumeStructure volumes[FogVolumeLimit];
};

struct FogMediumStructure
{
    float extinction;           // [1/m] sigma_t at the query point
    float3 albedo;              // [0..1] density-weighted albedo
    float phaseG;               // [-1..1] density-weighted asymmetry
    float3 emissive;            // [lux] density-weighted emissive source
};

struct FogMarchStructure
{
    float3 scatter;             // [lux] integrated in-scatter along the ray
    float transmittance;        // [0..1] surviving fraction (Beer-Lambert)
};

struct FogSlabStructure
{
    float3 scatterDelta;        // [lux] slab in-scatter before accumulation
    float transmitStep;         // [0..1] slab transmittance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  STRUCTURE FACTORIES                                                   
//------------------------------------------------------------------------------------------------------------------------

FogVolumeStructure FogVolumeMake(uint shape, float3 center, float3 extents,
    float density, float heightFalloff, float3 albedo, float phaseG,
    float3 emissive, float noiseAmount, float noiseScale)
{
    FogVolumeStructure v;
    v.shape = shape;
    v.center = center;
    v.extents = extents;
    v.density = density;
    v.heightFalloff = heightFalloff;
    v.albedo = albedo;
    v.phaseG = phaseG;
    v.emissive = emissive;
    v.noiseAmount = noiseAmount;
    v.noiseScale = noiseScale;
    return v;
}

FogConfiguration FogConfigurationMake(float globalDensity,
    float globalHeightFalloff, float globalBaseHeight, float3 globalAlbedo,
    float globalPhaseG, float3 ambientRadiance, float timeSeconds,
    uint marchSteps, float maxMarchDistance)
{
    FogConfiguration c;
    c.globalDensity = globalDensity;
    c.globalHeightFalloff = globalHeightFalloff;
    c.globalBaseHeight = globalBaseHeight;
    c.globalAlbedo = globalAlbedo;
    c.globalPhaseG = globalPhaseG;
    c.ambientRadiance = ambientRadiance;
    c.timeSeconds = timeSeconds;
    c.volumeCount = 0u;
    c.marchSteps = marchSteps;
    c.maxMarchDistance = maxMarchDistance;
    for (uint i = 0u; i < FogVolumeLimit; i++)
    {
        c.volumes[i] = FogVolumeMake(FogShapeBox, float3(0.0f, 0.0f, 0.0f),
            float3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f,
            float3(1.0f, 1.0f, 1.0f), 0.0f, float3(0.0f, 0.0f, 0.0f),
            0.0f, 1.0f);
    }
    return c;
}

FogMarchStructure FogMarchMake(float3 scatter, float transmittance)
{
    FogMarchStructure m;
    m.scatter = scatter;
    m.transmittance = transmittance;
    return m;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    DETERMINISTIC VALUE NOISE (INTEGER HASH, NO SIN)                                    
//------------------------------------------------------------------------------------------------------------------------

// 10 exact bits: uint->float is bit-exact below 2^24 on CPU and GPU alike.
float FogHashCompute(uint3 cell)
{
    uint3 q = cell * 374761393u + 668265263u;
    q = (q ^ (q >> 13u)) * 1274126177u;
    uint h = q.x ^ q.y ^ q.z;
    return float(h % 1024u) * 0.0009765625f;
}

float FogNoiseCompute(float3 p)
{
    int3 base = int3(int(floor(p.x)), int(floor(p.y)),
                     int(floor(p.z)));
    float3 f = float3(fract(p.x), fract(p.y), fract(p.z));
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float n000 = FogHashCompute(uint3(base));
    float n100 = FogHashCompute(uint3(base + int3(1, 0, 0)));
    float n010 = FogHashCompute(uint3(base + int3(0, 1, 0)));
    float n110 = FogHashCompute(uint3(base + int3(1, 1, 0)));
    float n001 = FogHashCompute(uint3(base + int3(0, 0, 1)));
    float n101 = FogHashCompute(uint3(base + int3(1, 0, 1)));
    float n011 = FogHashCompute(uint3(base + int3(0, 1, 1)));
    float n111 = FogHashCompute(uint3(base + int3(1, 1, 1)));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float FogNoiseFractal(float3 p)
{
    float v = FogNoiseCompute(p) * 0.57142857f;
    v = v + FogNoiseCompute(p * 2.03f + 17.17f) * 0.28571429f;
    v = v + FogNoiseCompute(p * 4.12f + 41.41f) * 0.14285714f;
    return v;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                DENSITY AND MEDIUM QUERY                                                
//------------------------------------------------------------------------------------------------------------------------

// Smooth analytic mask in [0..1]: full core fading over the outer 30 %.
float FogVolumeMaskCompute(float3 p, in FogVolumeStructure v)
{
    if (v.shape == FogShapeSphere)
    {
        float r = length((p - v.center) / max(v.extents.x, 1e-6f));
        return 1.0f - smoothstep(0.7f, 1.0f, r);
    }
    float3 safeE = max(v.extents, float3(1e-6f, 1e-6f, 1e-6f));
    float3 q = (p - v.center) / safeE;
    float d = max(abs(q.x), max(abs(q.y), abs(q.z)));
    return 1.0f - smoothstep(0.7f, 1.0f, d);
}

// Total medium at a point: global height fog plus density-weighted volumes
// (Strand accumulation: scattering/extinction/emissive add, phase mixes).
FogMediumStructure FogMediumQuery(float3 worldPos, in FogConfiguration cfg)
{
    float hgt = worldPos.y - cfg.globalBaseHeight;
    float gExt = cfg.globalDensity * exp(-max(hgt, 0.0f)
                 * cfg.globalHeightFalloff);
    float3 aSum = cfg.globalAlbedo * gExt;
    float gSum = cfg.globalPhaseG * gExt;
    float3 eSum = float3(0.0f, 0.0f, 0.0f);
    float ext = gExt;
    for (uint i = 0u; i < cfg.volumeCount; i++)
    {
        FogVolumeStructure v = cfg.volumes[i];
        if (v.density > 0.0f)
        {
            float mask = FogVolumeMaskCompute(worldPos, v);
            if (mask <= 0.0f)
            {
                continue;
            }
            float hf = exp(-max(worldPos.y - v.center.y, 0.0f)
                       * v.heightFalloff);
            float n = FogNoiseFractal(worldPos * v.noiseScale
                      + cfg.timeSeconds * 0.01f);
            float vd = v.density * mask * hf
                       * (1.0f - v.noiseAmount + v.noiseAmount * n);
            ext = ext + vd;
            aSum = aSum + v.albedo * vd;
            gSum = gSum + v.phaseG * vd;
            eSum = eSum + v.emissive * vd;
        }
    }
    FogMediumStructure m;
    m.extinction = ext;
    float inv = 1.0f / max(ext, 1e-9f);
    m.albedo = aSum * inv;
    m.phaseG = gSum * inv;
    m.emissive = eSum * inv;
    return m;
}

// Scalar extinction: doubles as the FluidSolver grid-injection source.
float FogDensityQuery(float3 worldPos, in FogConfiguration cfg)
{
    return FogMediumQuery(worldPos, cfg).extinction;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PHASE AND SLAB INTEGRATION                                               
//------------------------------------------------------------------------------------------------------------------------

// Henyey-Greenstein (matches AtmosphereIntegrator::SamplePhaseHenyeyGreenstein).
float FogPhaseCompute(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (12.56637061f * denom * sqrt(denom));
}

// Hillaire closed-form slab: constant source S over thickness D.
FogSlabStructure FogSlabEvaluate(float3 source, float sigmaT, float stepLength)
{
    float tr = exp(-sigmaT * stepLength);
    float sint = (1.0f - tr) / max(sigmaT, 1e-6f);
    FogSlabStructure s;
    s.scatterDelta = source * sint;
    s.transmitStep = tr;
    return s;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ANALYTIC VOLUME INTERSECTION (DIVISION-BY-ZERO SAFE)                                  
//------------------------------------------------------------------------------------------------------------------------

// Returns (t0, t1); t0 > t1 means miss. Parallel axes are branched, never
// divided, so CPU IEEE and GPU agree by construction.
float2 FogBoxIntersect(float3 origin, float3 dir, float3 center,
    float3 extents)
{
    float3 lo = center - extents;
    float3 hi = center + extents;
    float t0 = -1e30f;
    float t1 = 1e30f;
    float oo;
    float dd;
    float ta;
    float tb;
    oo = origin.x; dd = dir.x;
    if (abs(dd) < 1e-9f)
    {
        if (oo < lo.x || oo > hi.x) { return float2(1.0f, -1.0f); }
    }
    else
    {
        ta = (lo.x - oo) / dd; tb = (hi.x - oo) / dd;
        t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
    }
    oo = origin.y; dd = dir.y;
    if (abs(dd) < 1e-9f)
    {
        if (oo < lo.y || oo > hi.y) { return float2(1.0f, -1.0f); }
    }
    else
    {
        ta = (lo.y - oo) / dd; tb = (hi.y - oo) / dd;
        t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
    }
    oo = origin.z; dd = dir.z;
    if (abs(dd) < 1e-9f)
    {
        if (oo < lo.z || oo > hi.z) { return float2(1.0f, -1.0f); }
    }
    else
    {
        ta = (lo.z - oo) / dd; tb = (hi.z - oo) / dd;
        t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
    }
    return float2(t0, t1);
}

float2 FogSphereIntersect(float3 origin, float3 dir, float3 center,
    float radius)
{
    float3 oc = origin - dir * dot(dir, origin - center);
    float3 toC = center - origin;
    float tca = dot(toC, dir);
    float r2 = radius * radius;
    float d2 = dot(oc - center, oc - center);
    if (d2 > r2)
    {
        return float2(1.0f, -1.0f);
    }
    float thc = sqrt(max(r2 - d2, 0.0f));
    return float2(tca - thc, tca + thc);
}

float2 FogVolumeIntersect(float3 origin, float3 dir,
    in FogVolumeStructure v)
{
    if (v.shape == FogShapeSphere)
    {
        return FogSphereIntersect(origin, dir, v.center, v.extents.x);
    }
    return FogBoxIntersect(origin, dir, v.center, v.extents);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      VOLUME RAYMARCH (RESTIR-LIT SINGLE SCATTER)                                       
//------------------------------------------------------------------------------------------------------------------------

// Marches the merged volume segments along the ray. The direct term reuses
// ONE light: the winning ReSTIR DI reservoir sample (direction + radiance +
// visibility) passed by the caller — Volumetric-ReSTIR-style reuse (Lin et
// al. 2021). The ambient term is the sky ambient stand-in for diffuse
// multiple scattering. Fixed step counts: deterministic.
FogMarchStructure FogMarchVolumes(float3 origin, float3 dir, float tMin,
    float tMax, float3 lightDir, float3 lightRadiance, float lightVisibility,
    in FogConfiguration cfg)
{
    float seg0[FogVolumeLimit + 1u];
    float seg1[FogVolumeLimit + 1u];
    uint segCount = 0u;
    for (uint i = 0u; i < cfg.volumeCount; i++)
    {
        float2 hit = FogVolumeIntersect(origin, dir, cfg.volumes[i]);
        float a = max(hit.x, tMin);
        float b = min(hit.y, min(tMax, cfg.maxMarchDistance));
        if (a < b)
        {
            seg0[segCount] = a;
            seg1[segCount] = b;
            segCount = segCount + 1u;
        }
    }
    // The global height fog rides the whole surviving span, not just the
    // local volumes: without this segment the veil only exists inside
    // boxes and every ray that misses them marches nothing.
    if (cfg.globalDensity > 0.0f)
    {
        float fullB = min(tMax, cfg.maxMarchDistance);
        if (tMin < fullB)
        {
            seg0[segCount] = tMin;
            seg1[segCount] = fullB;
            segCount = segCount + 1u;
        }
    }
    // Insertion sort by start, then merge overlaps (deterministic).
    for (uint i = 1u; i < segCount; i++)
    {
        float a = seg0[i];
        float b = seg1[i];
        uint j = i;
        while (j > 0u && seg0[j - 1u] > a)
        {
            seg0[j] = seg0[j - 1u];
            seg1[j] = seg1[j - 1u];
            j = j - 1u;
        }
        seg0[j] = a;
        seg1[j] = b;
    }
    uint merged = 0u;
    for (uint i = 0u; i < segCount; i++)
    {
        if (merged > 0u && seg0[i] <= seg1[merged - 1u])
        {
            seg1[merged - 1u] = max(seg1[merged - 1u], seg1[i]);
        }
        else
        {
            seg0[merged] = seg0[i];
            seg1[merged] = seg1[i];
            merged = merged + 1u;
        }
    }
    // Scatter angle: incident travel is sun->point (-lightDir), scattered
    // travel is point->camera (-dir); cos = (-lightDir).(-dir) = dir.lightDir.
    float cosT = dot(dir, lightDir);
    float3 scatter = float3(0.0f, 0.0f, 0.0f);
    float tr = 1.0f;
    for (uint s = 0u; s < merged; s++)
    {
        float len = seg1[s] - seg0[s];
        float dt = len / max(float(cfg.marchSteps), 1.0f);
        for (uint i = 0u; i < cfg.marchSteps; i++)
        {
            float tm = seg0[s] + (float(i) + 0.5f) * dt;
            float3 pp = origin + dir * tm;
            FogMediumStructure med = FogMediumQuery(pp, cfg);
            if (med.extinction > 1e-6f)
            {
                float3 sigmaS = med.albedo * med.extinction;
                float phase = FogPhaseCompute(cosT, med.phaseG);
                float3 src = med.emissive * med.extinction
                           + sigmaS * (cfg.ambientRadiance + lightRadiance
                           * (phase * lightVisibility));
                FogSlabStructure slab = FogSlabEvaluate(src, med.extinction,
                                                        dt);
                scatter = scatter + slab.scatterDelta * tr;
                tr = tr * slab.transmitStep;
            }
        }
    }
    return FogMarchMake(scatter, tr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    ATMOSPHERIC FOG FOR SURFACES (PROVEN MEDIA MATH)                                    
//------------------------------------------------------------------------------------------------------------------------

// Beer-Lambert sun-path transmittance over a short segment (PZ scale): exact
// to < 0.3 % at 100 m; the full 20x8 integration is reserved for sky rays,
// which carry their own f.trans through SkyPixelCompute.
float3 FogSunTransmittance(float3 origin, float3 dir, float dist,
    in SkyConfiguration sky)
{
    float3 betaR = float3(5.8e-6f, 13.5e-6f, 33.1e-6f) * sky.rayleigh;
    float3 betaM = float3(21e-6f, 21e-6f, 21e-6f) * sky.mie;
    float3 betaO = float3(0.65e-6f, 1.881e-6f, 0.085e-6f) * sky.ozone;
    float hAvg = origin.y + dir.y * dist * 0.5f;
    float fR = exp(-max(hAvg, 0.0f) / sky.rayleighH);
    float fM = exp(-max(hAvg, 0.0f) / sky.mieH);
    float3 tau = (betaR * fR + betaM * fM + betaO * fM) * dist;
    return exp(-tau);
}

// Surface aerial perspective: the G3-gated sky media math (ground fog +
// atmosphere-fog), applied along the camera ray to an opaque surface color.
// hzGlow is the caller's dawn-glow at the ray direction (DawnGlowCompute).
float3 FogAtmosphereApply(float3 surfaceColor, float3 rayOrigin,
    float3 rayDir, float rayDistance, float3 hzGlow, in SkyConfiguration sky)
{
    float3 tr = FogSunTransmittance(rayOrigin, rayDir, rayDistance, sky);
    return SkyMediaApply(surfaceColor, rayDir, rayDistance, hzGlow, tr, sky);
}

#endif // FOG_SPECIFICATION_INCLUDED
