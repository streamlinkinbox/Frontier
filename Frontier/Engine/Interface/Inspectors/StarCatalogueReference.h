/*==========================================================================================================================================
                                                         STARCATALOGUEREFERENCE.H
==========================================================================================================================================*/
// 🧩 Bounded SLAT v1 decoding and the VehicleHMI magnitude census; never substitutes random stars for a missing archive.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CATALOGUE REFERENCE
//------------------------------------------------------------------------------------------------------------------------

struct StarCatalogueReference
{
    std::vector<std::array<float, 7>> Entries;
    std::string                       Diagnostic;
};

struct StarMagnitudeCensus
{
    std::array<uint32_t, 10> TotalBins{};
    std::array<uint32_t, 10> EligibleBins{};
    uint32_t                 Eligible = 0;
};

[[nodiscard]] StarCatalogueReference DecodeStarCatalogue(std::span<const uint8_t> Bytes);
[[nodiscard]] StarCatalogueReference ReadStarCatalogue(const std::filesystem::path &Path);
[[nodiscard]] StarMagnitudeCensus    EvaluateStarCensus(std::span<const std::array<float, 7>> Entries, float LimitingMagnitude);

} // namespace Frontier
