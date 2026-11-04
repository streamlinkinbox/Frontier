/*==========================================================================================================================================
                                                        STARCATALOGUEREFERENCE.CPP
==========================================================================================================================================*/
// 🧩 Little-endian SLAT v1 decoding and flux-cutoff counts for the catalogue planisphere.

#include "Engine/Interface/Inspectors/StarCatalogueReference.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLAT V1
//------------------------------------------------------------------------------------------------------------------------

StarCatalogueReference DecodeStarCatalogue(std::span<const uint8_t> Bytes)
{
    StarCatalogueReference Result;
    const auto             Word = [&](size_t Offset)
    {
        return static_cast<uint32_t>(Bytes[Offset]) | static_cast<uint32_t>(Bytes[Offset + 1]) << 8 |
               static_cast<uint32_t>(Bytes[Offset + 2]) << 16 | static_cast<uint32_t>(Bytes[Offset + 3]) << 24;
    };
    if (Bytes.size() < 16 || Word(0) != 0x54414C53 || Word(4) != 1)
    {
        Result.Diagnostic = "Invalid native SLAT star catalogue";
        return Result;
    }
    const uint32_t Count = Word(8);
    if (Count == 0 || Count > 200000 || Bytes.size() != 16 + static_cast<size_t>(Count) * 28)
    {
        Result.Diagnostic = "Invalid star catalogue count";
        return Result;
    }
    Result.Entries.resize(Count);
    for (size_t Entry = 0; Entry < Count; ++Entry)
    {
        for (size_t Channel = 0; Channel < 7; ++Channel)
        {
            const float Quantity = std::bit_cast<float>(Word(16 + (Entry * 7 + Channel) * 4));
            if (!std::isfinite(Quantity))
            {
                Result.Entries.clear();
                Result.Diagnostic = "Non-finite star catalogue";
                return Result;
            }
            Result.Entries[Entry][Channel] = Quantity;
        }
    }
    return Result;
}

StarCatalogueReference ReadStarCatalogue(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    if (!Input) return {{}, "Star catalogue unavailable: BrightStars.bin could not be opened"};
    const auto Length = Input.tellg();
    if (Length < 16 || Length > 16 + 200000 * 28) return {{}, "Star catalogue length is outside SLAT limits"};
    std::vector<uint8_t> Bytes(static_cast<size_t>(Length));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Bytes.data()), Length)) return {{}, "Star catalogue read failed"};
    return DecodeStarCatalogue(Bytes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAGNITUDE CENSUS
//------------------------------------------------------------------------------------------------------------------------

StarMagnitudeCensus EvaluateStarCensus(std::span<const std::array<float, 7>> Entries, float LimitingMagnitude)
{
    StarMagnitudeCensus Census;
    const double        Threshold = std::pow(10.0, -0.4 * LimitingMagnitude);
    for (const auto &Star : Entries)
    {
        const double Magnitude = -2.5 * std::log10(std::max(1.0e-30, static_cast<double>(Star[3])));
        const size_t Bin       = static_cast<size_t>(std::clamp(std::floor(Magnitude + 2), 0.0, 9.0));
        ++Census.TotalBins[Bin];
        if (Star[3] >= Threshold)
        {
            ++Census.Eligible;
            ++Census.EligibleBins[Bin];
        }
    }
    return Census;
}

} // namespace Frontier
