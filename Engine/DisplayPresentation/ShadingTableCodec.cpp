//============================================================================================================================================
//                                                    SHADINGTABLECODEC.CPP
//============================================================================================================================================

#include "ShadingTableCodec.h"
#include "LtcSheenTable.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265358979f;

float RadicalInverse(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(Bits) * 2.3283064365386963e-10f;
}

float Lambda(float Wz, float Wx, float Wy, float Alpha) noexcept
{
    const float A2 = Alpha * Alpha;
    return 0.5f * (-1.0f + std::sqrt(1.0f + A2 * (Wx * Wx + Wy * Wy) / std::max(Wz * Wz, 1.0e-12f)));
}

// Isotropic single-scatter GGX split-sum: E(μo) = A + B with Schlick weight, A = E_vndf[G2/G1 (1 − (1−μh)⁵)], B = E_vndf[G2/G1 (1−μh)⁵].
struct EnergyCellResult { float A, B; };
EnergyCellResult EnergyCell(float Mu, float Alpha, uint32_t Samples) noexcept
{
    const float SinO = std::sqrt(std::max(0.0f, 1.0f - Mu * Mu));
    const float Wo[3] = { SinO, 0.0f, Mu };
    const float G1o = 1.0f / (1.0f + Lambda(Wo[2], Wo[0], Wo[1], Alpha));
    double SumA = 0.0, SumB = 0.0;
    for (uint32_t S = 0; S < Samples; ++S)
    {
        const float U1 = (static_cast<float>(S) + 0.5f) / static_cast<float>(Samples), U2 = RadicalInverse(S);
        // Dupuy–Benyoub spherical cap VNDF sample (isotropic).
        float Hx = Alpha * Wo[0], Hy = Alpha * Wo[1], Hz = Wo[2];
        const float Len = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz); Hx /= Len; Hy /= Len; Hz /= Len;
        const float Phi = 2.0f * kPi * U1;
        const float Z = (1.0f - U2) * (1.0f + Hz) - Hz;
        const float Sn = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
        float Mx = Sn * std::cos(Phi) + Hx, My = Sn * std::sin(Phi) + Hy, Mz = Z + Hz;
        Mx *= Alpha; My *= Alpha; Mz = std::max(0.0f, Mz);
        const float ML = std::sqrt(Mx * Mx + My * My + Mz * Mz); if (ML <= 0.0f) continue;
        Mx /= ML; My /= ML; Mz /= ML;
        const float WoH = Wo[0] * Mx + Wo[1] * My + Wo[2] * Mz;
        const float Wi[3] = { 2.0f * WoH * Mx - Wo[0], 2.0f * WoH * My - Wo[1], 2.0f * WoH * Mz - Wo[2] };
        if (Wi[2] <= 0.0f) continue;
        const float G2 = 1.0f / (1.0f + Lambda(Wo[2], Wo[0], Wo[1], Alpha) + Lambda(Wi[2], Wi[0], Wi[1], Alpha));
        const float Fc = std::pow(1.0f - std::max(WoH, 0.0f), 5.0f);
        SumA += G2 / G1o * (1.0f - Fc);
        SumB += G2 / G1o * Fc;
    }
    return { static_cast<float>(SumA / Samples), static_cast<float>(SumB / Samples) };
}

} // namespace

ShadingTableSet ShadingTableCodec::Bake(uint32_t SamplesPerCell) noexcept
{
    ShadingTableSet Set;
    const uint32_t N = ShadingTableSet::kResolution;
    Set.Energy.resize(N * N * 4u);
    Set.Sheen.resize(N * N * 4u);
    for (uint32_t Row = 0; Row < N; ++Row)
    {
        const float Alpha = std::max((static_cast<float>(Row) + 0.5f) / N, 0.0025f);
        double Average = 0.0;   // E_avg = 2 ∫ E(μ) μ dμ over the row's cell centres
        for (uint32_t Column = 0; Column < N; ++Column)
        {
            const float Mu = std::max((static_cast<float>(Column) + 0.5f) / N, 1.0e-3f);
            const EnergyCellResult Cell = EnergyCell(Mu, Alpha, SamplesPerCell);
            float* Texel = &Set.Energy[(Row * N + Column) * 4u];
            Texel[0] = Cell.A; Texel[1] = Cell.B; Texel[3] = 0.0f;
            Average += 2.0 * std::min(1.0f, Cell.A + Cell.B) * Mu / N;
        }
        for (uint32_t Column = 0; Column < N; ++Column)
            Set.Energy[(Row * N + Column) * 4u + 2u] = static_cast<float>(std::min(Average, 1.0));
    }
    for (uint32_t Row = 0; Row < N; ++Row)
        for (uint32_t Column = 0; Column < N; ++Column)
            for (uint32_t K = 0; K < 3; ++K)
                Set.Sheen[(Row * N + Column) * 4u + K] = kLtcSheenVolume[Row][Column][K];
    return Set;
}

namespace {
template <int Channels>
void Bilinear(const std::vector<float>& Table, float X, float Y, float* Out) noexcept
{
    const int N = static_cast<int>(ShadingTableSet::kResolution);
    const float Fx = std::clamp(X * N - 0.5f, 0.0f, static_cast<float>(N - 1));
    const float Fy = std::clamp(Y * N - 0.5f, 0.0f, static_cast<float>(N - 1));
    const int X0 = static_cast<int>(Fx), Y0 = static_cast<int>(Fy);
    const int X1 = std::min(X0 + 1, N - 1), Y1 = std::min(Y0 + 1, N - 1);
    const float Tx = Fx - X0, Ty = Fy - Y0;
    for (int C = 0; C < Channels; ++C)
    {
        const float A = Table[(Y0 * N + X0) * Channels + C], B = Table[(Y0 * N + X1) * Channels + C];
        const float D = Table[(Y1 * N + X0) * Channels + C], E = Table[(Y1 * N + X1) * Channels + C];
        Out[C] = (A * (1.0f - Tx) + B * Tx) * (1.0f - Ty) + (D * (1.0f - Tx) + E * Tx) * Ty;
    }
}
} // namespace

void ShadingTableCodec::SampleEnergy(const ShadingTableSet& Set, float Mu, float Alpha, float Out[3]) noexcept
{
    float Four[4]; Bilinear<4>(Set.Energy, Mu, Alpha, Four);
    Out[0] = Four[0]; Out[1] = Four[1]; Out[2] = Four[2];
}

void ShadingTableCodec::SampleSheen(const ShadingTableSet& Set, float CosTheta, float Alpha, float Out[3]) noexcept
{
    float Four[4]; Bilinear<4>(Set.Sheen, CosTheta, Alpha, Four);
    Out[0] = Four[0]; Out[1] = Four[1]; Out[2] = Four[2];
}

} // namespace Frontier
