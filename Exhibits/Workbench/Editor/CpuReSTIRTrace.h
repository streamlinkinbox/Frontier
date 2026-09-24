//============================================================================================================================================
//                                                       CPUReSTIRTRACE.H
//============================================================================================================================================
// 🧩 CPU simulation of the ReSTIR viewport for the editor proof: the same scene the engine traces (RayTracingSolver's
//    Cornell box), the same estimator shape (RIS direct lighting over the luminaire triangles + one NEE bounce,
//    running-mean accumulation, ACES tone map). The Vulkan build renders this on the GPU through ReSTIRViewport.slang;
//    here it is traced on the CPU so the proof image shows a real render inside the Viewport panel, not a placeholder.
//
//    Kept in Scratchpad: this is proof tooling, never part of the engine or the shipping game.
#pragma once

#include "../../Projects/Project-Zero/Source/RayTracingSolver.h"
#include "../../Projects/Project-Zero/Source/FlyThroughSolver.h"
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace CpuReSTIR {

struct Rng
{
    uint32_t S;
    explicit Rng(uint32_t Seed) noexcept : S(Seed * 747796405u + 2891336453u) {}
    float Next() noexcept
    {
        S = S * 747796405u + 2891336453u;
        uint32_t W = ((S >> ((S >> 28u) + 4u)) ^ S) * 277803737u;
        W = (W >> 22u) ^ W;
        return static_cast<float>(W) * (1.0f / 4294967296.0f);
    }
};

using Frontier::Vector3;
using Frontier::ProjectZero::RayTracingSolver;
using Frontier::ProjectZero::RayStructure;
using Frontier::ProjectZero::HitIntersection;
using Frontier::ProjectZero::TriangleGeometry;

inline Vector3 Add(Vector3 A, Vector3 B) noexcept { return { A.x + B.x, A.y + B.y, A.z + B.z }; }
inline Vector3 Sub(Vector3 A, Vector3 B) noexcept { return { A.x - B.x, A.y - B.y, A.z - B.z }; }
inline Vector3 Mul(Vector3 A, float S) noexcept   { return { A.x * S, A.y * S, A.z * S }; }
inline Vector3 Had(Vector3 A, Vector3 B) noexcept { return { A.x * B.x, A.y * B.y, A.z * B.z }; }
inline float   Dot(Vector3 A, Vector3 B) noexcept { return A.x * B.x + A.y * B.y + A.z * B.z; }
inline Vector3 Cross(Vector3 A, Vector3 B) noexcept
{
    return { A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x };
}
inline Vector3 Norm(Vector3 A) noexcept
{
    const float L = std::sqrt(Dot(A, A));
    return L > 0.0f ? Mul(A, 1.0f / L) : Vector3{ 0.0f, 0.0f, 1.0f };
}

inline Vector3 CosineHemisphere(Vector3 N, float U1, float U2) noexcept
{
    const float R = std::sqrt(U1), Phi = 6.28318530f * U2;
    const Vector3 T = Norm(std::fabs(N.z) < 0.9f ? Cross(N, Vector3{ 0.0f, 0.0f, 1.0f }) : Cross(N, Vector3{ 1.0f, 0.0f, 0.0f }));
    const Vector3 B = Cross(N, T);
    const float X = R * std::cos(Phi), Y = R * std::sin(Phi), Z = std::sqrt(std::fmax(0.0f, 1.0f - U1));
    return Norm(Add(Add(Mul(T, X), Mul(B, Y)), Mul(N, Z)));
}

struct Luminaire
{
    uint32_t Triangle;
    float    Area;
};

struct Tracer
{
    const RayTracingSolver* Scene = nullptr;
    std::vector<Luminaire>  Lights;
    float                   TotalArea = 0.0f;

    void Bind(const RayTracingSolver& S) noexcept
    {
        Scene = &S;
        Lights.clear();
        TotalArea = 0.0f;
        const auto& Tris = S.QueryTriangles();
        const auto& Mats = S.QueryMaterials();
        for (uint32_t I = 0u; I < Tris.size(); ++I)
        {
            const auto& M = Mats[Tris[I].MaterialIndex];
            if (M.EmissiveRadiance.x + M.EmissiveRadiance.y + M.EmissiveRadiance.z <= 0.0f) continue;
            const Vector3 E1 = Sub(Tris[I].VertexBeta, Tris[I].VertexAlpha), E2 = Sub(Tris[I].VertexGamma, Tris[I].VertexAlpha);
            const Vector3 C = Cross(E1, E2);
            const float Area = 0.5f * std::sqrt(Dot(C, C));
            Lights.push_back({ I, Area });
            TotalArea += Area;
        }
    }

    // Uniform point on a luminaire triangle (area-weighted pick), with its pdf over area.
    Vector3 SampleLight(Rng& R, Vector3& N, Vector3& Le, float& Pdf) const noexcept
    {
        float Pick = R.Next() * TotalArea;
        uint32_t Chosen = 0u;
        for (; Chosen + 1u < Lights.size() && Pick > Lights[Chosen].Area; ++Chosen) Pick -= Lights[Chosen].Area;
        const TriangleGeometry& T = Scene->QueryTriangles()[Lights[Chosen].Triangle];
        float U = R.Next(), V = R.Next();
        if (U + V > 1.0f) { U = 1.0f - U; V = 1.0f - V; }
        const Vector3 P = Add(T.VertexAlpha, Add(Mul(Sub(T.VertexBeta, T.VertexAlpha), U), Mul(Sub(T.VertexGamma, T.VertexAlpha), V)));
        N   = T.SurfaceNormal;
        Le  = Scene->QueryMaterials()[T.MaterialIndex].EmissiveRadiance;
        Pdf = 1.0f / TotalArea;
        return P;
    }

    // RIS direct lighting: M candidates from the area sampler, the winner shadow-tested once. Open scenes
    //    (the showcase) carry no luminaire triangles — the SUN lights those, shadow-tested through the solver's
    //    BVH. Fixed harness staging: warm and low, ahead of the showcase camera, so the proof view reads.
    Vector3 Direct(Vector3 P, Vector3 N, Vector3 Albedo, Rng& R, uint32_t Candidates) const noexcept
    {
        if (Lights.empty())
        {
            const Vector3 SunDir = Norm(Vector3{ -0.61f, -0.73f, 0.31f });
            const Vector3 SunRad{ 24.0f, 17.0f, 11.0f };
            const float NdotL = Dot(N, SunDir);
            if (NdotL <= 0.0f) return { 0.0f, 0.0f, 0.0f };
            const Vector3 Bias = Add(P, Mul(N, 1e-3f));
            if (Scene->EvaluateOcclusion(Bias, Add(Bias, Mul(SunDir, 1e4f)))) return { 0.0f, 0.0f, 0.0f };
            return Mul(Had(Albedo, SunRad), NdotL / 3.14159265f);
        }
        float WSum = 0.0f, ChosenTarget = 0.0f;
        Vector3 ChosenP{}, ChosenContribution{};
        for (uint32_t C = 0u; C < Candidates; ++C)
        {
            Vector3 Ln, Le; float Pdf;
            const Vector3 Lp = SampleLight(R, Ln, Le, Pdf);
            const Vector3 D  = Sub(Lp, P);
            const float Dist2 = std::fmax(Dot(D, D), 1e-6f);
            const Vector3 Wi = Mul(D, 1.0f / std::sqrt(Dist2));
            const float CosS = std::fmax(Dot(N, Wi), 0.0f), CosL = std::fmax(-Dot(Ln, Wi), 0.0f);
            const float G = CosS * CosL / Dist2;
            const Vector3 F = Mul(Had(Albedo, Le), G / 3.14159265f);
            const float Target = F.x + F.y + F.z;
            const float W = Target / Pdf;
            WSum += W;
            if (W > 0.0f && R.Next() * WSum <= W) { ChosenP = Lp; ChosenContribution = F; ChosenTarget = Target; }
        }
        if (ChosenTarget <= 0.0f) return { 0.0f, 0.0f, 0.0f };
        const float Weight = WSum / (static_cast<float>(Candidates) * ChosenTarget);
        const Vector3 Bias = Add(P, Mul(N, 1e-3f));
        if (Scene->EvaluateOcclusion(Bias, Sub(ChosenP, Mul(Norm(Sub(ChosenP, Bias)), 1e-3f)))) return { 0.0f, 0.0f, 0.0f };
        return Mul(ChosenContribution, Weight);
    }

    Vector3 Radiance(RayStructure Ray, Rng& R, uint32_t Candidates) const noexcept
    {
        Vector3 L{ 0.0f, 0.0f, 0.0f };
        Vector3 Beta{ 1.0f, 1.0f, 1.0f };
        for (uint32_t Bounce = 0u; Bounce < 2u; ++Bounce)
        {
            const HitIntersection Hit = Scene->EvaluateIntersection(Ray);
            if (!Hit.ValidCondition)
            {
                // The open face and roof aperture see the sky: a dim blue fill stands in for the celestial integral.
                const float T = 0.5f * (Ray.RayDirection.z + 1.0f);
                L = Add(L, Had(Beta, Vector3{ 0.10f + 0.15f * T, 0.14f + 0.22f * T, 0.22f + 0.40f * T }));
                break;
            }
            const auto& M = Scene->QueryMaterials()[Hit.MaterialIndex];
            Vector3 N = Hit.SurfaceNormal;
            if (Dot(N, Ray.RayDirection) > 0.0f) N = Mul(N, -1.0f);
            if (Bounce == 0u) L = Add(L, Had(Beta, M.EmissiveRadiance));
            L = Add(L, Had(Beta, Direct(Hit.HitLocation, N, M.AlbedoColor, R, Candidates)));
            // One NEE bounce: cosine-weighted continuation, the albedo carried as the throughput.
            Beta = Had(Beta, M.AlbedoColor);
            Ray.SpatialOrigin = Add(Hit.HitLocation, Mul(N, 1e-3f));
            Ray.RayDirection  = CosineHemisphere(N, R.Next(), R.Next());
            Ray.MinimumDistance = 1e-4f; Ray.MaximumDistance = 1e9f;
        }
        return L;
    }
};

inline float Aces(float X) noexcept
{
    const float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
    const float Y = (X * (A * X + B)) / (X * (C * X + D) + E);
    return Y < 0.0f ? 0.0f : (Y > 1.0f ? 1.0f : Y);
}

// Traces Frames progressive frames into Rgba (Width×Height×4), tone-mapped with the integrator's exposure.
inline void Render(const RayTracingSolver& Scene, const Frontier::ProjectZero::FlyThroughSolver& Camera,
                   uint32_t Width, uint32_t Height, uint32_t Frames, uint32_t Candidates, float Exposure,
                   unsigned char* Rgba) noexcept
{
    Tracer T; T.Bind(Scene);
    std::vector<float> Sum(static_cast<size_t>(Width) * Height * 3u, 0.0f);
    const Vector3 O = Camera.QuerySpatialLocation(), F = Camera.QueryForwardVector(), Rt = Camera.QueryRightVector(), Up = Camera.QueryUpwardVector();
    const float TanHalf = std::tan(Camera.QueryFieldOfViewRadians() * 0.5f);
    const float Aspect  = static_cast<float>(Width) / static_cast<float>(Height);
    const uint32_t Threads = std::thread::hardware_concurrency() > 0u ? std::thread::hardware_concurrency() : 2u;
    for (uint32_t Frame = 0u; Frame < Frames; ++Frame)
    {
        std::vector<std::thread> Pool;
        for (uint32_t Th = 0u; Th < Threads; ++Th)
            Pool.emplace_back([&, Th]()
            {
                for (uint32_t Y = Th; Y < Height; Y += Threads)
                    for (uint32_t X = 0u; X < Width; ++X)
                    {
                        Rng R(Frame * 9781u + Y * Width + X + 1u);
                        const float Jx = R.Next(), Jy = R.Next();
                        const float Sx = ((static_cast<float>(X) + Jx) / static_cast<float>(Width) * 2.0f - 1.0f) * TanHalf * Aspect;
                        const float Sy = (1.0f - (static_cast<float>(Y) + Jy) / static_cast<float>(Height) * 2.0f) * TanHalf;
                        RayStructure Ray{ O, Norm(Add(F, Add(Mul(Rt, Sx), Mul(Up, Sy)))), 1e-4f, 1e9f };
                        const Vector3 L = T.Radiance(Ray, R, Candidates);
                        float* S = &Sum[(static_cast<size_t>(Y) * Width + X) * 3u];
                        S[0] += L.x; S[1] += L.y; S[2] += L.z;
                    }
            });
        for (auto& Th : Pool) Th.join();
    }
    const float Inv = Exposure / static_cast<float>(Frames);
    for (size_t I = 0u; I < static_cast<size_t>(Width) * Height; ++I)
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            const float Lin = Aces(Sum[I * 3u + C] * Inv);
            Rgba[I * 4u + C] = static_cast<unsigned char>(std::pow(Lin, 1.0f / 2.2f) * 255.0f + 0.5f);
            Rgba[I * 4u + 3u] = 255u;
        }
}

} // namespace CpuReSTIR
