/*==========================================================================================================================================
                                                         MATERIALBAKETIMING.CPP
==========================================================================================================================================*/
// 🧩 Measure retained native denim procedural evaluation against four-channel bilinear sampling of its baked float maps.

#include "Engine/Render/MaterialProceduralBaking.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace Frontier;

int main()
{
    try
    {
        constexpr uint32_t Extent = 512, Count = Extent * Extent;
        WeaveFieldParameters P;
        P.Type = 4; P.Weave = 8; P.Scale = 150; P.Relief = .9f; P.FiberDetail = 12;
        P.Fuzz = .5f; P.Slub = .3f; P.DenimFade = .12f; P.Seed = 17;
        if (!DecodeHexLinearRgb("#283d57", P.Warp) || !DecodeHexLinearRgb("#aab6b9", P.Weft))
            throw std::runtime_error("Invalid existing denim recipe colours");
        auto Start = std::chrono::steady_clock::now();
        auto Maps = BakeWeaveMaps(P, Extent);
        auto Milliseconds = [](auto Since) {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Since).count();
        };
        double BakeMs = Milliseconds(Start), Sum = 0, Maximum = 0;
        auto Sample = [&](float U, float V) {
            float X = U * Extent - .5f, Y = V * Extent - .5f;
            int IX = int(std::floor(X)), IY = int(std::floor(Y));
            float FX = X - IX, FY = Y - IY;
            WeaveFieldSample S;
            for (uint32_t C = 0; C < 4; ++C)
            {
                float Accumulated = 0;
                for (int J = 0; J < 2; ++J)
                    for (int I = 0; I < 2; ++I)
                    {
                        uint32_t At = uint32_t((IY + J + Extent) % Extent) * Extent + uint32_t((IX + I + Extent) % Extent);
                        float Coefficient = C == 3 ? Maps.Relief[At] : Maps.Albedo[At * 3 + C];
                        Accumulated += Coefficient * (I ? FX : 1 - FX) * (J ? FY : 1 - FY);
                    }
                if (C == 3) S.Relief = Accumulated; else S.Albedo[C] = Accumulated;
            }
            return S;
        };
        for (uint32_t I = 0; I < Count; I += 17)
        {
            float U = (float(I % Extent) + .5f) / Extent, V = (float(I / Extent) + .5f) / Extent;
            auto A = EvaluateWeaveField(U, V, P, P.Scale / Extent), B = Sample(U, V);
            for (uint32_t C = 0; C < 3; ++C) Maximum = std::max(Maximum, double(std::abs(A.Albedo[C] - B.Albedo[C])));
            Maximum = std::max(Maximum, double(std::abs(A.Relief - B.Relief)));
        }
        if (Maximum != 0) throw std::runtime_error("Bake differs at texel centres");
        std::array<std::vector<double>, 2> Times;
        for (int Repetition = -1; Repetition < 7; ++Repetition)
            for (uint32_t Ordinal = 0; Ordinal < 2; ++Ordinal)
            {
                uint32_t Method = (Ordinal + Repetition + 1) % 2;
                Start = std::chrono::steady_clock::now();
                for (uint32_t I = 0; I < Count; ++I)
                {
                    float U = float((I * 1664525u + 1013904223u) & 0xffffffu) / 16777216.f;
                    float V = float((I * 747796405u + 2891336453u) & 0xffffffu) / 16777216.f;
                    auto S = Method == 0 ? EvaluateWeaveField(U, V, P, P.Scale / Extent) : Sample(U, V);
                    Sum += S.Albedo[0] + S.Albedo[1] + S.Albedo[2] + S.Relief;
                }
                double Ms = Milliseconds(Start);
                if (Repetition >= 0) Times[Method].push_back(Ms);
            }
        double Error = 0, Peak = 0;
        for (uint32_t I = 0; I < 8192; ++I)
        {
            float U = float((I * 1664525u + 1013904223u) & 0xffffffu) / 16777216.f;
            float V = float((I * 747796405u + 2891336453u) & 0xffffffu) / 16777216.f;
            auto A = EvaluateWeaveField(U, V, P, P.Scale / Extent), B = Sample(U, V);
            for (uint32_t C = 0; C < 3; ++C)
            {
                double Delta = std::abs(A.Albedo[C] - B.Albedo[C]);
                Error += Delta; Peak = std::max(Peak, Delta);
            }
        }
        if (!std::isfinite(Sum)) throw std::runtime_error("Nonfinite procedural/sampled material");
        for (auto& T : Times) std::sort(T.begin(), T.end());
        std::cout << std::setprecision(9)
                  << "MATERIAL_BAKE_TIMING cpu_only=1 hardware_gpu=0 extent=" << Extent << " samples=" << Count
                  << " repetitions=7 bake_ms=" << BakeMs << " procedural_p50_ms=" << Times[0][3]
                  << " bilinear_p50_ms=" << Times[1][3] << " ratio=" << Times[0][3] / Times[1][3]
                  << " float_map_bytes=" << Count * 16 << " texel_centre_max_error=" << Maximum
                  << " offgrid_albedo_mae=" << Error / (8192 * 3) << " offgrid_albedo_max=" << Peak
                  << " checksum=" << Sum << '\n';
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
