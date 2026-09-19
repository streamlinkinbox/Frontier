//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/AcousticIntegrator.cpp — 3D Spatial Audio Implementation
//============================================================================================================================================

#include "AcousticIntegrator.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

AcousticIntegrator::AcousticIntegrator() noexcept
    : ListenerPosition{ 0.0f, 1.7f, 0.0f }
    , ListenerForward{ 0.0f, 0.0f, -1.0f }
    , ListenerUp{ 0.0f, 1.0f, 0.0f }
    , VoiceIdentifierCounter(1)
{
    ActiveVoices.reserve(64);
}

void AcousticIntegrator::AssignListenerTransform(const Vector3& Position, const Vector3& Forward, const Vector3& Up) noexcept
{
    ListenerPosition = Position;
    ListenerForward = Forward.Normalized();
    ListenerUp = Up.Normalized();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                VOICE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t AcousticIntegrator::PlaySound(uint32_t SoundToken, AcousticBusCategory Bus, const Vector3& Position, float Volume, bool Looping) noexcept
{
    uint64_t Identifier = VoiceIdentifierCounter++;
    AcousticVoiceRecord Voice{};
    Voice.VoiceIdentifier              = Identifier;
    Voice.SoundToken                   = SoundToken;
    Voice.TargetBus                    = Bus;
    Voice.SpatialLocation              = Position;
    Voice.VolumeGain                   = Volume;
    Voice.PitchMultiplier              = 1.0f;
    Voice.LowPassFilterCutoffFrequency = 20000.0f;
    Voice.SpatialPan                   = 0.0f;
    Voice.PlayingCondition             = true;
    Voice.LoopingCondition             = Looping;

    ActiveVoices.push_back(Voice);
    return Identifier;
}

void AcousticIntegrator::StopSound(uint64_t DesiredVoiceIdentifier) noexcept
{
    auto iterator = std::remove_if(ActiveVoices.begin(), ActiveVoices.end(),
        [DesiredVoiceIdentifier](const AcousticVoiceRecord& v) { return v.VoiceIdentifier == DesiredVoiceIdentifier; });
    ActiveVoices.erase(iterator, ActiveVoices.end());
}

//------------------------------------------------------------------------------------------------------------------------
//                                              AUDIO ADVANCEMENT
//------------------------------------------------------------------------------------------------------------------------

void AcousticIntegrator::AdvanceAudio(float Δτ, const LevelSetSpace* Obstacles) noexcept
{
    (void)Δτ;
    Vector3 ListenerRight = OrientationClassifier::CrossProduct(ListenerForward, ListenerUp).Normalized();

    for (auto& Voice : ActiveVoices)
    {
        if (!Voice.PlayingCondition)
        {
            continue;
        }

        Vector3 EmitterVec = Voice.SpatialLocation - ListenerPosition;
        float Dist = EmitterVec.Length();
        Vector3 EmitterDir = (Dist > 1e-4f) ? (EmitterVec / Dist) : ListenerForward;

        Voice.SpatialPan = std::clamp(OrientationClassifier::DotProduct(EmitterDir, ListenerRight), -1.0f, 1.0f);

        float OcclusionFactor = 0.0f;
        if (Obstacles && Dist > 0.5f)
        {
            uint32_t Steps = 8;
            float StepSize = Dist / static_cast<float>(Steps);
            for (uint32_t s = 1; s < Steps; ++s)
            {
                Vector3 SamplePoint = ListenerPosition + EmitterDir * (static_cast<float>(s) * StepSize);
                if (Obstacles->IsInsideSolid(SamplePoint))
                {
                    OcclusionFactor = 1.0f;
                    break;
                }
            }
        }

        Voice.LowPassFilterCutoffFrequency = 20000.0f * (1.0f - OcclusionFactor) + 800.0f * OcclusionFactor;
    }
}

size_t AcousticIntegrator::QueryActiveVoiceCount() const noexcept
{
    return ActiveVoices.size();
}

} // namespace Frontier
