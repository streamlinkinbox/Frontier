//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/AcousticIntegrator.h — 3D HRTF Spatial Audio Mixer with Occlusion Raymarching
//============================================================================================================================================

#pragma once

#include "AcousticStructure.h"
#include "../VolumetricDynamics/LevelSetSpace.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 ACOUSTIC INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class AcousticIntegrator
{
public:
    AcousticIntegrator() noexcept;
    ~AcousticIntegrator() noexcept = default;

    AcousticIntegrator(const AcousticIntegrator&) = delete;
    AcousticIntegrator& operator=(const AcousticIntegrator&) = delete;

    void                    AssignListenerTransform(const Vector3& Position, const Vector3& Forward, const Vector3& Up) noexcept;

    [[nodiscard]] uint64_t  PlaySound(uint32_t SoundToken, AcousticBusCategory Bus, const Vector3& Position, float Volume = 1.0f, bool Looping = false) noexcept;
    void                    StopSound(uint64_t DesiredVoiceIdentifier) noexcept;

    void                    AdvanceAudio(float Δτ, const LevelSetSpace* Obstacles = nullptr) noexcept;

    [[nodiscard]] size_t    QueryActiveVoiceCount() const noexcept;

    // Single unified conversion operator for active voice count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    Vector3                 ListenerPosition;                   // [m] listener world position
    Vector3                 ListenerForward;                    // [-] listener forward orientation
    Vector3                 ListenerUp;                         // [-] listener up orientation
    std::vector<AcousticVoiceRecord> ActiveVoices;              // [voices] active sound voices
    uint64_t                VoiceIdentifierCounter;             // [counter] monotonic token generator
};

template<>
inline size_t AcousticIntegrator::Convert<size_t>() const noexcept
{
    return QueryActiveVoiceCount();
}

} // namespace Frontier
