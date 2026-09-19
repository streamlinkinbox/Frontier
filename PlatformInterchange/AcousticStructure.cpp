//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/AcousticStructure.cpp — Acoustic Waveform Storage Implementation
//============================================================================================================================================

#include "AcousticStructure.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                WAVEFORM INGESTION
//------------------------------------------------------------------------------------------------------------------------

uint32_t AcousticStructure::RegisterWaveform(std::string WaveformName, const float* PcmSamples, size_t SampleCount) noexcept
{
    uint32_t Token = static_cast<uint32_t>(WaveformNames.size());
    WaveformNames.push_back(std::move(WaveformName));

    if (PcmSamples && SampleCount > 0)
    {
        PcmStorage.emplace_back(PcmSamples, PcmSamples + SampleCount);
    }
    else
    {
        PcmStorage.emplace_back();
    }

    return Token;
}

} // namespace Frontier
