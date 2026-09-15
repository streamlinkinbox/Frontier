//============================================================================================================================================
//                                                     SHADINGTABLECODEC.H
//============================================================================================================================================
// 🧩 Bakes the two lookup tables MaterialEvaluation.slang reads (R4b): the single-scatter GGX directional albedo
//    split-sum (A, B, E_avg) for Kulla–Conty multiple-scattering compensation (binding 14, RGBA32F 32×32), and the LTC sheen
//    coefficients (aInv, bInv, R) of Zeltner–Burley–Chiang 2022 (binding 15, RGBA32F 32×32, from LtcSheenTable.h).
//    Deterministic (fixed-seed Hammersley), a few ms at start-up; the harness checks the furnace closure from it.

#pragma once

#include <cstdint>
#include <vector>

namespace Frontier {

struct ShadingTableSet
{
    static constexpr uint32_t kResolution = 32u;
    std::vector<float> Energy;   // [α row][μ column], 4 floats (A, B, E_avg, 0): E_ss(μ, α) = F0·A + B (Schlick), height-correlated Smith
    std::vector<float> Sheen;    // [α row][cosθ column], 4 floats (aInv, bInv, R, 0)
};

class ShadingTableCodec
{
public:
    // SamplesPerCell: VNDF samples per (μ, α) cell (default 4096 → error < 0.5 %).
    [[nodiscard]] static ShadingTableSet Bake(uint32_t SamplesPerCell = 4096u) noexcept;

    // Bilinear reads used by the CPU harness (mirror textureLod with clamp-to-edge, texel centres at (i + 0.5) / N).
    static void SampleEnergy(const ShadingTableSet& Set, float Mu, float Alpha, float Out[3]) noexcept;
    static void SampleSheen(const ShadingTableSet& Set, float CosTheta, float Alpha, float Out[3]) noexcept;
};

} // namespace Frontier
