//================================================================================
// ReSTIRConvergence — T2/T3/T4 for the shipped ReSTIRSequence.slang dual core.
//
// Scene: a diffuse corner (infinite floor y = 0 + wall z = -4, y in [0, 4],
// uniform albedo 0.5) under the celestial sky, all in the sky frame. The wall
// forces second-bounce GI paths; the uniform albedo keeps GI reuse exact. The
// six reservoir entries are mirrored 1:1 (same dual calls, same RNG streams;
// ray queries replaced by the analytic corner), then gated:
//   T2: DI estimate vs an independent brute-force DI reference (own RNG,
//       own sampling code, balance-heuristic MIS over the same 3 techniques).
//   T3: GI estimate vs an independent brute-force 2-bounce path reference.
//   T4: determinism — run 0 executed twice must match bit-exactly.
// Usage:
//   ReSTIRConvergence [--frames 48] [--runs 12] [--seed0 1000]
//       [--report ReSTIRConvergence.txt] [--ppm restir.ppm]
// Exit code 0 iff every gate passes. Prints the full numbers either way.
//================================================================================
#include "CelHost.h"
#include "ReSTIRSequence.slang"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kW = 64;
constexpr uint32_t kH = 36;
constexpr uint32_t kM0 = 8;
constexpr uint32_t kMCap = 20;
constexpr uint32_t kSpatTaps = 4;
constexpr float kAlbedo = 0.5f;
constexpr float kPiH = 3.14159265f;
constexpr float kWallZ = -4.0f;
constexpr float kWallH = 4.0f;

struct Args
{
    int frames = 48;
    int runs = 12;
    uint32_t seed0 = 1000;
    std::string report = "ReSTIRConvergence.txt";
    std::string ppm;
    bool temporal = true;
    bool spatial = true;
};

bool Parse(int argc, char** argv, Args& a) noexcept
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto need = [&](std::string& v) -> bool
        {
            if (++i >= argc)
            {
                return false;
            }
            v = argv[i];
            return true;
        };
        std::string v;
        if (k == "--frames" && need(v))
        {
            a.frames = std::stoi(v);
        }
        else if (k == "--runs" && need(v))
        {
            a.runs = std::stoi(v);
        }
        else if (k == "--seed0" && need(v))
        {
            a.seed0 = uint32_t(std::stoul(v));
        }
        else if (k == "--report" && need(v))
        {
            a.report = v;
        }
        else if (k == "--ppm" && need(v))
        {
            a.ppm = v;
        }
        else if (k == "--temporal" && need(v))
        {
            a.temporal = (v != "0");
        }
        else if (k == "--spatial" && need(v))
        {
            a.spatial = (v != "0");
        }
        else
        {
            return false;
        }
    }
    return a.frames > 0 && a.runs > 0;
}

struct GB
{
    float3 pos;
    float3 normal;
    float3 albedo;
    float depth;
    float2 motion;
};

struct TraceHit
{
    bool hit;
    float t;
    float3 normal;
};

// Analytic corner: floor y = 0 plus wall z = kWallZ, y in [0, kWallH].
TraceHit TraceCorner(float3 o, float3 d, float tmin) noexcept
{
    TraceHit h;
    h.hit = false;
    h.t = 1e6f;
    h.normal = float3(0.0f, 1.0f, 0.0f);
    if (d.y < -1e-9f)
    {
        const float t = -o.y / d.y;
        if (t > tmin && t < h.t)
        {
            h.hit = true;
            h.t = t;
            h.normal = float3(0.0f, 1.0f, 0.0f);
        }
    }
    if (d.z < -1e-9f)
    {
        const float t = (kWallZ - o.z) / d.z;
        if (t > tmin && t < h.t)
        {
            const float y = o.y + d.y * t;
            if (y >= 0.0f && y <= kWallH)
            {
                h.hit = true;
                h.t = t;
                h.normal = float3(0.0f, 0.0f, 1.0f);
            }
        }
    }
    return h;
}

inline float CornerVis(float3 o, float3 d) noexcept
{
    return TraceCorner(o, d, 1e-3f).hit ? 0.0f : 1.0f;
}

void BuildGBuffer(std::vector<GB>& gb, const SkyProjection& cam) noexcept
{
    gb.resize(size_t(kW) * kH);
    const float3 eye = float3(0.0f, 2.0f, 3.0f);
    int nFloor = 0, nWall = 0, nSky = 0;
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            GB g;
            g.albedo = float3(kAlbedo, kAlbedo, kAlbedo);
            g.motion = float2(0.0f, 0.0f);
            const float3 dir = ProjectZero::PrimaryRay(cam, x, y, kW, kH);
            const TraceHit h = TraceCorner(eye, dir, 1e-3f);
            if (h.hit)
            {
                g.pos = eye + dir * h.t;
                g.normal = h.normal;
                g.depth = h.t;
                if (h.normal.y > 0.5f)
                {
                    ++nFloor;
                }
                else
                {
                    ++nWall;
                }
            }
            else
            {
                g.pos = dir; // background convention: pos carries the ray dir
                g.normal = float3(0.0f, 1.0f, 0.0f);
                g.depth = 0.0f;
                ++nSky;
            }
            gb[size_t(y) * kW + x] = g;
        }
    }
    std::printf("gbuffer: floor %d wall %d sky %d\n", nFloor, nWall, nSky);
}

// ---- Entry mirrors (same dual calls, same RNG streams as the .slang) ----

void RunDiInitial(const std::vector<GB>& gb, std::vector<DirectReservoirStructure>& cur,
                  const SkyConfiguration& p, uint frame, uint RandomSeed) noexcept
{
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            DirectReservoirStructure res = DirectReservoirMake();
            res.m0 = float(kM0);
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 0u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                const float3 nSky = normalize(g.normal);
                const float3 origin = g.pos + g.normal * 1e-3f;
                for (uint i = 0u; i < kM0; i++)
                {
                    const uint s = i % 4u;
                    CandidateStructure d;
                    if (s <= 1u)
                    {
                        d = CandidateSunCone(rng, p);
                    }
                    else if (s == 2u)
                    {
                        d = CandidateAureole(rng, p);
                    }
                    else
                    {
                        d = CandidateSphere(rng);
                    }
                    rng = d.rng;
                    // DI lights with THE sky (disc + aureole + atmosphere +
                    // glow + stars + media), the same SkyRadianceCompute GI uses.
                    const float3 leFull = SkyRadianceCompute(d.dir, p);
                    const float qmix = 0.5f * DensitySunConeAt(d.dir, p)
                                     + 0.25f * DensityAureoleAt(d.dir, p)
                                     + 0.25f * (1.0f / (4.0f * SkyPi));
                    d.pdf = qmix;
                    const float vis = CornerVis(origin, d.dir);
                    const float cosT = max(dot(nSky, d.dir), 0.0f);
                    res = DirectReservoirUpdate(res, d, leFull, cosT, g.albedo, rng, vis);
                    const RandomScalarStructure ru = RandomScalarNext(rng);
                    rng = ru.rng;
                }
            }
            cur[idx] = res;
        }
    }
}

void RunDiTemporal(const std::vector<GB>& gb, const std::vector<DirectReservoirStructure>& read,
                   std::vector<DirectReservoirStructure>& cur, uint frame, uint RandomSeed) noexcept
{
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            DirectReservoirStructure step = cur[idx];
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 1u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                const float3 origin = g.pos + g.normal * 1e-3f;
                const int px = int(x) + int(g.motion.x);
                const int py = int(y) + int(g.motion.y);
                if (px >= 0 && py >= 0 && px < int(kW) && py < int(kH))
                {
                    const size_t pidx = size_t(py) * kW + uint32_t(px);
                    const GB& gp = gb[pidx];
                    DirectReservoirStructure cad = read[pidx];
                    const float dMax = max(g.depth, 0.1f);
                    const bool geomOk = fabsf(gp.depth - g.depth) < 0.1f * dMax
                                     && dot(gp.normal, g.normal) > 0.9f;
                    if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
                    {
                        if (cad.m > float(kMCap))
                        {
                            cad.wsum *= float(kMCap) / cad.m;
                            cad.m = float(kMCap);
                        }
                        const float3 nSky = normalize(g.normal);
                        const float3 nPSky = normalize(gp.normal);
                        step = DirectReservoirCombine(step, nSky, g.albedo, cad, nPSky,
                                            gp.albedo, rng);
                        const RandomScalarStructure ru = RandomScalarNext(rng);
                        rng = ru.rng;
                    }
                }
                if (step.m > 0.0f)
                {
                    step.vis = CornerVis(origin, step.dir);
                }
            }
            cur[idx] = step;
        }
    }
}

void RunDiSpatial(const std::vector<GB>& gb, std::vector<DirectReservoirStructure>& cur,
                  uint frame, uint RandomSeed) noexcept
{
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            DirectReservoirStructure step = cur[idx];
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 2u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                const float3 nSky = normalize(g.normal);
                const float3 origin = g.pos + g.normal * 1e-3f;
                for (uint k = 0u; k < kSpatTaps; k++)
                {
                    const RandomFloatStructure r1 = RandomFloatNext(rng);
                    rng = r1.rng;
                    const RandomFloatStructure r2 = RandomFloatNext(rng);
                    rng = r2.rng;
                    const float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
                    const float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
                    const int tx = int(x) + int(rr * cos(aa));
                    const int ty = int(y) + int(rr * sin(aa));
                    if (tx < 0 || ty < 0 || tx >= int(kW) || ty >= int(kH))
                    {
                        continue;
                    }
                    const size_t nidx = size_t(ty) * kW + uint32_t(tx);
                    const GB& gn = gb[nidx];
                    const DirectReservoirStructure cad = cur[nidx];
                    const float dMax = max(g.depth, 0.1f);
                    const bool geomOk = fabsf(gn.depth - g.depth) < 0.1f * dMax
                                     && dot(gn.normal, g.normal) > 0.9f;
                    if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
                    {
                        const float3 nNSky = normalize(gn.normal);
                        step = DirectReservoirCombine(step, nSky, g.albedo, cad, nNSky,
                                            gn.albedo, rng);
                        const RandomScalarStructure ru = RandomScalarNext(rng);
                        rng = ru.rng;
                    }
                }
                if (step.m > 0.0f)
                {
                    step.vis = CornerVis(origin, step.dir);
                }
            }
            cur[idx] = step;
        }
    }
}

void RunGiInitial(const std::vector<GB>& gb, std::vector<IndirectReservoirStructure>& cur,
                  const SkyConfiguration& p, uint frame, uint RandomSeed) noexcept
{
    const uint m0 = kM0 / 2u;
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            IndirectReservoirStructure res = IndirectReservoirMake();
            res.m0 = float(m0);
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 3u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                const float3 nSky = normalize(g.normal);
                const float3 origin = g.pos + g.normal * 1e-3f;
                for (uint i = 0u; i < m0; i++)
                {
                    CandidateStructure d = CandidateCosine(rng, nSky);
                    rng = d.rng;
                    const TraceHit h = TraceCorner(origin, d.dir, 1e-3f);
                    const float cosT = max(dot(nSky, d.dir), 0.0f);
                    float3 throughput = g.albedo * (cosT / SkyPi);
                    float3 le;
                    float pdf = d.pdf;
                    if (h.hit)
                    {
                        const float3 n2 = h.normal;
                        const float3 p2 = origin + d.dir * h.t + n2 * 1e-3f;
                        const CandidateStructure d2 = CandidateCosine(rng, n2);
                        rng = d2.rng;
                        const TraceHit h2 = TraceCorner(p2, d2.dir, 1e-3f);
                        const float cos2 = max(dot(n2, d2.dir), 0.0f);
                        throughput = throughput * g.albedo * (cos2 / SkyPi);
                        pdf = d.pdf * d2.pdf;
                        if (h2.hit)
                        {
                            le = float3(0.0f, 0.0f, 0.0f);
                        }
                        else
                        {
                            le = SkyRadianceCompute(d2.dir, p);
                        }
                    }
                    else
                    {
                        le = SkyRadianceCompute(d.dir, p);
                    }
                    res = IndirectReservoirUpdate(res, d, throughput, le, pdf, rng);
                    const RandomScalarStructure ru = RandomScalarNext(rng);
                    rng = ru.rng;
                }
            }
            cur[idx] = res;
        }
    }
}

void RunGiTemporal(const std::vector<GB>& gb, const std::vector<IndirectReservoirStructure>& read,
                   std::vector<IndirectReservoirStructure>& cur, uint frame, uint RandomSeed) noexcept
{
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            IndirectReservoirStructure step = cur[idx];
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 4u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                const int px = int(x) + int(g.motion.x);
                const int py = int(y) + int(g.motion.y);
                if (px >= 0 && py >= 0 && px < int(kW) && py < int(kH))
                {
                    const size_t pidx = size_t(py) * kW + uint32_t(px);
                    const GB& gp = gb[pidx];
                    IndirectReservoirStructure cad = read[pidx];
                    const float dMax = max(g.depth, 0.1f);
                    const bool geomOk = fabsf(gp.depth - g.depth) < 0.1f * dMax
                                     && dot(gp.normal, g.normal) > 0.9f;
                    if (gp.depth > 0.0f && geomOk && cad.m > 0.0f)
                    {
                        if (cad.m > float(kMCap))
                        {
                            cad.wsum *= float(kMCap) / cad.m;
                            cad.m = float(kMCap);
                        }
                        step = IndirectReservoirCombine(step, cad, rng);
                        const RandomScalarStructure ru = RandomScalarNext(rng);
                        rng = ru.rng;
                    }
                }
            }
            cur[idx] = step;
        }
    }
}

void RunGiSpatial(const std::vector<GB>& gb, std::vector<IndirectReservoirStructure>& cur,
                  uint frame, uint RandomSeed) noexcept
{
    for (uint32_t y = 0; y < kH; ++y)
    {
        for (uint32_t x = 0; x < kW; ++x)
        {
            const size_t idx = size_t(y) * kW + x;
            const GB& g = gb[idx];
            IndirectReservoirStructure step = cur[idx];
            RandomSequenceStructure rng = RandomSequenceMake(RandomSequenceSeed(uint(x), uint(y), frame, 5u) ^ RandomSeed);
            if (g.depth > 0.0f)
            {
                for (uint k = 0u; k < kSpatTaps; k++)
                {
                    const RandomFloatStructure r1 = RandomFloatNext(rng);
                    rng = r1.rng;
                    const RandomFloatStructure r2 = RandomFloatNext(rng);
                    rng = r2.rng;
                    const float rr = 30.0f * sqrt(max(r1.f, 1e-6f));
                    const float aa = 6.2831853f * r2.f + 2.3999632f * float(k);
                    const int tx = int(x) + int(rr * cos(aa));
                    const int ty = int(y) + int(rr * sin(aa));
                    if (tx < 0 || ty < 0 || tx >= int(kW) || ty >= int(kH))
                    {
                        continue;
                    }
                    const size_t nidx = size_t(ty) * kW + uint32_t(tx);
                    const GB& gn = gb[nidx];
                    const IndirectReservoirStructure cad = cur[nidx];
                    const float dMax = max(g.depth, 0.1f);
                    const bool geomOk = fabsf(gn.depth - g.depth) < 0.1f * dMax
                                     && dot(gn.normal, g.normal) > 0.9f;
                    if (gn.depth > 0.0f && geomOk && cad.m > 0.0f)
                    {
                        step = IndirectReservoirCombine(step, cad, rng);
                        const RandomScalarStructure ru = RandomScalarNext(rng);
                        rng = ru.rng;
                    }
                }
            }
            cur[idx] = step;
        }
    }
}

struct ShadeOut
{
    float3 di;
    float3 gi;
};

ShadeOut ShadePixel(const GB& g, const DirectReservoirStructure& rd, const IndirectReservoirStructure& rg) noexcept
{
    ShadeOut o;
    o.di = float3(0.0f, 0.0f, 0.0f);
    o.gi = float3(0.0f, 0.0f, 0.0f);
    if (g.depth <= 0.0f)
    {
        return o;
    }
    const float3 nSky = normalize(g.normal);
    const float cosT = max(dot(nSky, rd.dir), 0.0f);
    const float pDI = (rd.m > 0.0f) ? DirectTargetCompute(rd.le, cosT, g.albedo) : 0.0f;
    const float wDI = (pDI > 0.0f) ? rd.wsum / max(rd.m * pDI, 1e-12f) : 0.0f;
    o.di = wDI * rd.le * rd.vis * cosT * g.albedo / SkyPi;
    const float pGI = (rg.m > 0.0f) ? IndirectTargetCompute(rg.throughput, rg.le) : 0.0f;
    const float wGI = (pGI > 0.0f) ? rg.wsum / max(rg.m * pGI, 1e-12f) : 0.0f;
    o.gi = wGI * rg.throughput * rg.le;
    return o;
}

// ---- Independent brute-force references (own RNG, own sampling code) ----

struct BruteRef
{
    float3 mean;
    float lum;
    float lumSigma;
};

void BatchStats(const double* batch, int kB, int kN, float& lum, float& sigma) noexcept
{
    double m = 0.0;
    for (int b = 0; b < kB; ++b)
    {
        m += batch[b] / kN;
    }
    double v = 0.0;
    for (int b = 0; b < kB; ++b)
    {
        const double d = batch[b] / kN - m / kB;
        v += d * d;
    }
    lum = float(m);
    sigma = float(std::sqrt(v / (kB - 1)) / std::sqrt(double(kB)) * kB);
}

BruteRef BruteForceDI(const SkyConfiguration& p, uint64_t seed, float3 P, float3 N) noexcept
{
    constexpr int kN = 1 << 19;
    constexpr int kB = 32;
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    const double sx = p.sunDir.x, sy = p.sunDir.y, sz = p.sunDir.z;
    double hx = 0.0, hy = 1.0, hz = 0.0;
    if (std::fabs(sy) > 0.99)
    {
        hx = 1.0;
        hy = 0.0;
        hz = 0.0;
    }
    double tux = hy * sz - hz * sy, tuy = hz * sx - hx * sz, tuz = hx * sy - hy * sx;
    const double tl = std::sqrt(tux * tux + tuy * tuy + tuz * tuz);
    tux /= tl;
    tuy /= tl;
    tuz /= tl;
    const double tvx = sy * tuz - sz * tuy, tvy = sz * tux - sx * tuz,
                 tvz = sx * tuy - sy * tux;
    const double cosR = std::cos(double(p.sunAngularRadius) * 1.6);
    const double omegaC = 2.0 * kPiH * (1.0 - cosR);
    const float3 origin = P + N * 1e-3f;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    double batch[32] = { 0.0 };
    for (int n = 0; n < kN; ++n)
    {
        const double pick = u01(rng);
        const double u1 = u01(rng);
        const double u2 = u01(rng);
        double dx, dy, dz;
        if (pick < 0.5)
        {
            const double z = 1.0 - u1 * (1.0 - cosR);
            const double a = 2.0 * kPiH * u2;
            const double s = std::sqrt(std::max(1.0 - z * z, 0.0));
            dx = tux * s * std::cos(a) + tvx * s * std::sin(a) + sx * z;
            dy = tuy * s * std::cos(a) + tvy * s * std::sin(a) + sy * z;
            dz = tuz * s * std::cos(a) + tvz * s * std::sin(a) + sz * z;
        }
        else if (pick < 0.75)
        {
            const double cp = std::pow(std::max(1.0 - u1, 1e-9), 1.0 / 9.0);
            const double sp = std::sqrt(std::max(1.0 - cp * cp, 0.0));
            const double a = 2.0 * kPiH * u2;
            dx = tux * sp * std::cos(a) + tvx * sp * std::sin(a) + sx * cp;
            dy = tuy * sp * std::cos(a) + tvy * sp * std::sin(a) + sy * cp;
            dz = tuz * sp * std::cos(a) + tvz * sp * std::sin(a) + sz * cp;
        }
        else
        {
            const double z = 1.0 - 2.0 * u1;
            const double a = 2.0 * kPiH * u2;
            const double s = std::sqrt(std::max(1.0 - z * z, 0.0));
            dx = s * std::cos(a);
            dy = z;
            dz = s * std::sin(a);
        }
        const float3 dir = float3(float(dx), float(dy), float(dz));
        const float3 le = SkyRadianceCompute(dir, p);
        const double cosSun = dx * sx + dy * sy + dz * sz;
        const double qCone = (cosSun >= cosR) ? 1.0 / std::max(omegaC, 1e-9) : 0.0;
        const double qAur = (cosSun > 0.0) ? 9.0 * std::pow(cosSun, 8.0) / (4.0 * kPiH) : 0.0;
        const double qMix = 0.5 * qCone + 0.25 * qAur + 0.25 / (4.0 * kPiH);
        const double cosT = std::max(dx * N.x + dy * N.y + dz * N.z, 0.0);
        const double vis = TraceCorner(origin, dir, 1e-3f).hit ? 0.0 : 1.0;
        const double w = vis * cosT * kAlbedo / kPiH / qMix;
        sumR += le.x * w;
        sumG += le.y * w;
        sumB += le.z * w;
        batch[n % kB] += (le.x * 0.212671 + le.y * 0.715160 + le.z * 0.072169) * w;
    }
    BruteRef r;
    r.mean = float3(float(sumR / kN), float(sumG / kN), float(sumB / kN));
    BatchStats(batch, kB, kN, r.lum, r.lumSigma);
    return r;
}

BruteRef BruteForceGI(const SkyConfiguration& p, uint64_t seed, float3 P, float3 N) noexcept
{
    constexpr int kN = 1 << 18;
    constexpr int kB = 32;
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    const double nx = N.x, ny = N.y, nz = N.z;
    double hx = 0.0, hy = 1.0, hz = 0.0;
    if (std::fabs(ny) > 0.99)
    {
        hx = 1.0;
        hy = 0.0;
        hz = 0.0;
    }
    double tux = hy * nz - hz * ny, tuy = hz * nx - hx * nz, tuz = hx * ny - hy * nx;
    const double tl = std::sqrt(tux * tux + tuy * tuy + tuz * tuz);
    tux /= tl;
    tuy /= tl;
    tuz /= tl;
    const double tvx = ny * tuz - nz * tuy, tvy = nz * tux - nx * tuz,
                 tvz = nx * tuy - ny * tux;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    double batch[32] = { 0.0 };
    for (int n = 0; n < kN; ++n)
    {
        const double a1 = 2.0 * kPiH * u01(rng);
        const double e1 = u01(rng);
        const double sr1 = std::sqrt(e1);
        const double c1 = std::sqrt(std::max(1.0 - e1, 0.0));
        const double dx = tux * sr1 * std::cos(a1) + tvx * sr1 * std::sin(a1) + nx * c1;
        const double dy = tuy * sr1 * std::cos(a1) + tvy * sr1 * std::sin(a1) + ny * c1;
        const double dz = tuz * sr1 * std::cos(a1) + tvz * sr1 * std::sin(a1) + nz * c1;
        const float3 d1 = float3(float(dx), float(dy), float(dz));
        const float3 o1 = P + N * 1e-3f;
        const TraceHit h1 = TraceCorner(o1, d1, 1e-3f);
        const double q1 = std::max(dx * nx + dy * ny + dz * nz, 0.0) / kPiH;
        double tput = kAlbedo * (std::max(dx * nx + dy * ny + dz * nz, 0.0) / kPiH);
        double leR, leG, leB;
        double q = q1;
        if (!h1.hit)
        {
            const float3 le = SkyRadianceCompute(d1, p);
            leR = le.x;
            leG = le.y;
            leB = le.z;
        }
        else
        {
            const double mx = h1.normal.x, my = h1.normal.y, mz = h1.normal.z;
            double jx = 0.0, jy = 1.0, jz = 0.0;
            if (std::fabs(my) > 0.99)
            {
                jx = 1.0;
                jy = 0.0;
                jz = 0.0;
            }
            double wux = jy * mz - jz * my, wuy = jz * mx - jx * mz,
                   wuz = jx * my - jy * mx;
            const double wl = std::sqrt(wux * wux + wuy * wuy + wuz * wuz);
            wux /= wl;
            wuy /= wl;
            wuz /= wl;
            const double wvx = my * wuz - mz * wuy, wvy = mz * wux - mx * wuz,
                         wvz = mx * wuy - my * wux;
            const double a2 = 2.0 * kPiH * u01(rng);
            const double e2 = u01(rng);
            const double sr2 = std::sqrt(e2);
            const double c2 = std::sqrt(std::max(1.0 - e2, 0.0));
            const double ex = wux * sr2 * std::cos(a2) + wvx * sr2 * std::sin(a2) + mx * c2;
            const double ey = wuy * sr2 * std::cos(a2) + wvy * sr2 * std::sin(a2) + my * c2;
            const double ez = wuz * sr2 * std::cos(a2) + wvz * sr2 * std::sin(a2) + mz * c2;
            const float3 d2 = float3(float(ex), float(ey), float(ez));
            const float3 o2 = o1 + d1 * float(h1.t) + h1.normal * 1e-3f;
            const TraceHit h2 = TraceCorner(o2, d2, 1e-3f);
            const double cos2 = std::max(ex * mx + ey * my + ez * mz, 0.0);
            tput *= kAlbedo * (cos2 / kPiH);
            q = q1 * (cos2 / kPiH);
            if (!h2.hit)
            {
                const float3 le = SkyRadianceCompute(d2, p);
                leR = le.x;
                leG = le.y;
                leB = le.z;
            }
            else
            {
                leR = leG = leB = 0.0;
            }
        }
        sumR += tput * leR / q;
        sumG += tput * leG / q;
        sumB += tput * leB / q;
        batch[n % kB] += tput * (leR * 0.212671 + leG * 0.715160 + leB * 0.072169) / q;
    }
    BruteRef r;
    r.mean = float3(float(sumR / kN), float(sumG / kN), float(sumB / kN));
    BatchStats(batch, kB, kN, r.lum, r.lumSigma);
    return r;
}

double MeanOf(const std::vector<double>& v) noexcept
{
    double s = 0.0;
    for (double x : v)
    {
        s += x;
    }
    return s / double(v.size());
}

double StdErrOf(const std::vector<double>& v, double mean) noexcept
{
    if (v.size() < 2)
    {
        return 0.0;
    }
    double s = 0.0;
    for (double x : v)
    {
        s += (x - mean) * (x - mean);
    }
    return std::sqrt(s / double(v.size() - 1)) / std::sqrt(double(v.size()));
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!Parse(argc, argv, a))
    {
        std::fprintf(stderr, "usage: ReSTIRConvergence [--frames N] [--runs N] "
                             "[--seed0 S] [--report F] [--ppm F] "
                             "[--temporal 0/1] [--spatial 0/1]\n");
        return 2;
    }

    const ProjectZero::SunState sun =
        ProjectZero::SolveSun(7.6, -26.0, 0.0, 5800.0);
    SkyConfiguration p = ProjectZero::MakePanelParams(sun);
    const SkyProjection cam = ProjectZero::MakePanelCamera(0.0, -10.0, 72.0);
    p.pixAngle = 2.0f * cam.tanHalf / float(kH); // panel line 1219
    std::printf("sun 7.60h elev %.3f deg, corner scene, %ux%u, %d frames x %d runs\n",
                sun.elevationDeg, kW, kH, a.frames, a.runs);

    std::vector<GB> gb;
    BuildGBuffer(gb, cam);

    const int probes[3][2] = { { 32, 26 }, { 16, 28 }, { 32, 10 } };
    const char* probeNames[3] = { "floor", "floor-left", "wall" };
    for (int i = 0; i < 3; ++i)
    {
        const GB& g = gb[size_t(probes[i][1]) * kW + probes[i][0]];
        if (!(g.depth > 0.0f))
        {
            std::fprintf(stderr, "probe %s %d,%d misses the scene\n",
                         probeNames[i], probes[i][0], probes[i][1]);
            return 2;
        }
        std::printf("probe %s %d,%d normal (%.1f,%.1f,%.1f)\n", probeNames[i],
                    probes[i][0], probes[i][1], g.normal.x, g.normal.y, g.normal.z);
    }

    std::vector<DirectReservoirStructure> diRead(size_t(kW) * kH), diCur(size_t(kW) * kH);
    std::vector<IndirectReservoirStructure> giRead(size_t(kW) * kH), giCur(size_t(kW) * kH);
    std::vector<double> diEst[3], giEst[3];
    std::vector<float3> fbFirst;
    std::vector<float3> fbMean(size_t(kW) * kH, float3(0.0f, 0.0f, 0.0f));
    std::vector<DirectReservoirStructure> diFirst;
    std::vector<IndirectReservoirStructure> giFirst;
    bool deterministic = true;

    for (int run = 0; run < a.runs + 1; ++run)
    {
        const bool repeat = (run == a.runs);
        const int effRun = repeat ? 0 : run;
        const uint RandomSeed = a.seed0 + uint(effRun);
        for (auto& r : diRead)
        {
            r = DirectReservoirMake();
        }
        for (auto& r : giRead)
        {
            r = IndirectReservoirMake();
        }
        for (int f = 0; f < a.frames; ++f)
        {
            const uint frame = uint(f);
            RunDiInitial(gb, diCur, p, frame, RandomSeed);
            if (a.temporal)
            {
                RunDiTemporal(gb, diRead, diCur, frame, RandomSeed);
            }
            if (a.spatial)
            {
                RunDiSpatial(gb, diCur, frame, RandomSeed);
            }
            RunGiInitial(gb, giCur, p, frame, RandomSeed);
            if (a.temporal)
            {
                RunGiTemporal(gb, giRead, giCur, frame, RandomSeed);
            }
            if (a.spatial)
            {
                RunGiSpatial(gb, giCur, frame, RandomSeed);
            }
            diRead = diCur;
            giRead = giCur;
        }
        if (!repeat)
        {
            std::printf("run %d (seed %u):", run, RandomSeed);
            for (int i = 0; i < 3; ++i)
            {
                const size_t idx = size_t(probes[i][1]) * kW + probes[i][0];
                const ShadeOut s = ShadePixel(gb[idx], diCur[idx], giCur[idx]);
                diEst[i].push_back(LuminanceDWCompute(s.di));
                giEst[i].push_back(LuminanceDWCompute(s.gi));
                std::printf(" %s DI=%.4f GI=%.4f", probeNames[i],
                            LuminanceDWCompute(s.di), LuminanceDWCompute(s.gi));
            }
            std::printf("\n");
            for (size_t i = 0; i < fbMean.size(); ++i)
            {
                const ShadeOut s = ShadePixel(gb[i], diCur[i], giCur[i]);
                fbMean[i] = fbMean[i] + (s.di + s.gi) * (1.0f / float(a.runs));
            }
        }
        if (effRun == 0)
        {
            std::vector<float3> fb(size_t(kW) * kH);
            for (size_t i = 0; i < fb.size(); ++i)
            {
                const ShadeOut s = ShadePixel(gb[i], diCur[i], giCur[i]);
                fb[i] = s.di + s.gi;
            }
            if (!repeat)
            {
                fbFirst = fb;
                diFirst = diCur;
                giFirst = giCur;
            }
            else
            {
                deterministic =
                    fbFirst.size() == fb.size()
                    && std::memcmp(fbFirst.data(), fb.data(),
                                   fb.size() * sizeof(float3)) == 0
                    && std::memcmp(diFirst.data(), diCur.data(),
                                   diCur.size() * sizeof(DirectReservoirStructure)) == 0
                    && std::memcmp(giFirst.data(), giCur.data(),
                                   giCur.size() * sizeof(IndirectReservoirStructure)) == 0;
            }
        }
    }

    bool allPass = deterministic;
    std::printf("T4 determinism: %s\n", deterministic ? "PASS" : "FAIL");
    std::printf("brute-force references...\n");
    BruteRef diRefs[3], giRefs[3];
    for (int i = 0; i < 3; ++i)
    {
        const GB& g = gb[size_t(probes[i][1]) * kW + probes[i][0]];
        const BruteRef diRef = BruteForceDI(p, 0x12345678ull + uint64_t(i), g.pos, g.normal);
        const BruteRef giRef = BruteForceGI(p, 0x87654321ull + uint64_t(i), g.pos, g.normal);
        diRefs[i] = diRef;
        giRefs[i] = giRef;
        const double diMean = MeanOf(diEst[i]);
        const double diSe = StdErrOf(diEst[i], diMean);
        const double giMean = MeanOf(giEst[i]);
        const double giSe = StdErrOf(giEst[i], giMean);
        const double diTol = std::max({ 3.0 * diRef.lumSigma, 3.0 * diSe,
                                        0.02 * std::fabs(diRef.lum), 1e-9 });
        const double giTol = std::max({ 3.0 * giRef.lumSigma, 3.0 * giSe,
                                        0.02 * std::fabs(giRef.lum), 1e-9 });
        const bool diPass = std::fabs(diMean - diRef.lum) <= diTol;
        const bool giPass = std::fabs(giMean - giRef.lum) <= giTol;
        allPass = allPass && diPass && giPass;
        std::printf("probe %s T2 DI: est %.6f se %.6f ref %.6f s %.6f -> %s\n",
                    probeNames[i], diMean, diSe, diRef.lum, diRef.lumSigma,
                    diPass ? "PASS" : "FAIL");
        std::printf("probe %s T3 GI: est %.6f se %.6f ref %.6f s %.6f -> %s\n",
                    probeNames[i], giMean, giSe, giRef.lum, giRef.lumSigma,
                    giPass ? "PASS" : "FAIL");
    }
    std::printf("OVERALL: %s\n", allPass ? "PASS" : "FAIL");

    if (std::FILE* f = std::fopen(a.report.c_str(), "w"))
    {
        std::fprintf(f, "# ReSTIRConvergence: sun 7.6h, corner albedo 0.5, "
                        "%d frames x %d runs\n", a.frames, a.runs);
        std::fprintf(f, "T4 determinism %s\n", deterministic ? "PASS" : "FAIL");
        for (int i = 0; i < 3; ++i)
        {
            std::fprintf(f, "probe %s DI est %.9f se %.9f ref %.9f sigma %.9f\n",
                         probeNames[i], MeanOf(diEst[i]), StdErrOf(diEst[i], MeanOf(diEst[i])),
                         diRefs[i].lum, diRefs[i].lumSigma);
            std::fprintf(f, "probe %s GI est %.9f se %.9f ref %.9f sigma %.9f\n",
                         probeNames[i], MeanOf(giEst[i]), StdErrOf(giEst[i], MeanOf(giEst[i])),
                         giRefs[i].lum, giRefs[i].lumSigma);
        }
        std::fprintf(f, "OVERALL %s\n", allPass ? "PASS" : "FAIL");
        std::fclose(f);
    }

    if (!a.ppm.empty() && !fbFirst.empty())
    {
        const std::string meanPath = a.ppm + ".mean.ppm";
        std::vector<float3> ldr(size_t(kW) * kH);
        std::vector<float3> ldrMean(size_t(kW) * kH);
        for (uint32_t y = 0; y < kH; ++y)
        {
            for (uint32_t x = 0; x < kW; ++x)
            {
                const size_t idx = size_t(y) * kW + x;
                const float2 uv = ViewportUVCompute(uint(x), uint(y), uint(kW), uint(kH));
                if (gb[idx].depth > 0.0f)
                {
                    ldr[idx] = SkyPostApply(fbFirst[idx], uv.x, uv.y, float(kW),
                                            float(kH), uint(x), uint(y), p);
                    ldrMean[idx] = SkyPostApply(fbMean[idx], uv.x, uv.y, float(kW),
                                                float(kH), uint(x), uint(y), p);
                }
                else
                {
                    const float3 dir = normalize(gb[idx].pos);
                    const float3 hdr = SkyPixelCompute(dir, cam, p);
                    ldr[idx] = SkyPostApply(hdr, uv.x, uv.y, float(kW), float(kH),
                                            uint(x), uint(y), p);
                    ldrMean[idx] = ldr[idx];
                }
            }
        }
        ProjectZero::WritePpm(a.ppm, kW, kH, ldr);
        ProjectZero::WritePpm(meanPath, kW, kH, ldrMean);
        std::printf("wrote %s and %s\n", a.ppm.c_str(), meanPath.c_str());
    }
    return allPass ? 0 : 1;
}
