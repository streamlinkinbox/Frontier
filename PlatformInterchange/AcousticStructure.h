//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/AcousticStructure.h — Acoustic Voice Records, Sound Waveforms and DSP Routing
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACOUSTIC BUS
//------------------------------------------------------------------------------------------------------------------------

enum class AcousticBusCategory : uint32_t
{
    MasterBus                           = 0,                    // Master mix bus
    SoundEffectsBus                     = 1,                    // Spatial sound effects
    AcousticMusicBus                    = 2,                    // Background music score
    PositionalVoiceBus                  = 3,                    // Player voice communication
    EnvironmentalAmbienceBus            = 4                     // Ambient wind, water, weather
};

//------------------------------------------------------------------------------------------------------------------------
//                                                ACOUSTIC VOICE RECORD
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) AcousticVoiceRecord
{
    uint64_t                VoiceIdentifier;                    // [token] unique active sound voice token
    uint32_t                SoundToken;                         // [token] sound waveform identifier
    AcousticBusCategory     TargetBus;                          // [bus] routing audio bus
    Vector3                 SpatialLocation;                    // [m] 3D emitter position
    float                   VolumeGain;                         // [0..1] linear volume gain
    float                   PitchMultiplier;                    // [-] playback speed / pitch
    float                   LowPassFilterCutoffFrequency;       // [Hz] dynamic occlusion filter cutoff
    float                   SpatialPan;                         // [-1..1] stereo/binaural pan factor
    bool                    PlayingCondition;                   // [bool] true when active
    bool                    LoopingCondition;                   // [bool] true when repeating
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ACOUSTIC STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class AcousticStructure
{
public:
    AcousticStructure() noexcept = default;
    ~AcousticStructure() noexcept = default;

    AcousticStructure(const AcousticStructure&) = delete;
    AcousticStructure& operator=(const AcousticStructure&) = delete;

    uint32_t                RegisterWaveform(std::string WaveformName, const float* PcmSamples, size_t SampleCount) noexcept;
    [[nodiscard]] size_t    QueryWaveformCount() const noexcept { return WaveformNames.size(); }

    // Single unified conversion operator for waveform count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<std::string>        WaveformNames;              // [names] sound waveform names
    std::vector<std::vector<float>> PcmStorage;                 // [samples] mono/stereo PCM float samples
};

template<>
inline size_t AcousticStructure::Convert<size_t>() const noexcept
{
    return WaveformNames.size();
}

} // namespace Frontier
