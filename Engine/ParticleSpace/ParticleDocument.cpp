/*============================================================================================================================================
                                                           PARTICLEDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 ParticleDocument rig management, cache reconstruction & default Sparks authoring setup

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"

#include "Engine/ColourSpace/ColourPresetRepository.h"

#include <algorithm>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC MEMBER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ParticleDocument::PopulateDefaultSparksRig()
{
    Rigs.clear();
    ParticleRig Sparks{};
    Sparks.RigId          = 1u;
    Sparks.RigTitle       = "Welding Sparks";
    Sparks.PresetOriginId = "sparks";
    Sparks.Origin         = PointOriginSpec{ { 0.0f, 0.0f, 1.5f } };
    Sparks.Carrier        = SpriteBillboardCarrier{
        SpriteCutoutCategory::Streak,
        LeafSubMode::Leaf,
        BokehApertureCategory::Mixed,
        1.0f,
        0.55f,
        ColourSpace::OpticalBlendEquation::Additive,
    };
    Sparks.Sizing.CapacitySlots     = 4096u;
    Sparks.Sizing.ReleaseRatePerSec = 520.0f;
    Sparks.Sizing.LifetimeMinSec    = 0.8f;
    Sparks.Sizing.LifetimeMaxSec    = 1.9f;
    Sparks.Sizing.SizeStartMeters   = 0.035f;
    Sparks.Sizing.SizeEndMeters     = 0.008f;
    Sparks.Sizing.SizeByVelocity    = true;

    Sparks.Dynamics.Family                       = ParticleSimulationFamily::Ballistic;
    Sparks.Dynamics.Ballistics.DirectionAxis     = { 0.0f, 0.0f, 1.0f };
    Sparks.Dynamics.Ballistics.SpreadRadians     = 1.15f;
    Sparks.Dynamics.Ballistics.SpeedMin          = 2.6f;
    Sparks.Dynamics.Ballistics.SpeedMax          = 6.4f;
    Sparks.Dynamics.Ballistics.DragPerSecond     = 0.35f;
    Sparks.Dynamics.Ballistics.GravityFactor     = 1.0f;
    Sparks.Dynamics.Ballistics.BounceRestitution = 0.58f;

    // Default blackbody incandescent spark ramp
    ColourSpace::ColourPresetRepository Presets;
    if (const auto* FirePreset = Presets.FindRampPreset("thermal"))
    {
        Sparks.TintBinding.SourceProfile = ColourSpace::ConstructProfileFromRampPreset(*FirePreset);
    }
    else if (const auto* SunsetPreset = Presets.FindRampPreset("sunset"))
    {
        Sparks.TintBinding.SourceProfile = ColourSpace::ConstructProfileFromRampPreset(*SunsetPreset);
    }
    Sparks.TintBinding.ScalarChannel = ParticleScalarChannel::NormalizedLifetime;
    Sparks.RebuildBakedColourLut();
    Sparks.RebuildOriginCaches(RandomSeed);
    Rigs.push_back(std::move(Sparks));
}

void ParticleDocument::RebuildAllRigCaches()
{
    for (ParticleRig& Rig : Rigs)
    {
        Rig.RebuildBakedColourLut();
        Rig.RebuildOriginCaches(RandomSeed);
    }
}

const ParticleRig* ParticleDocument::FindRig(uint32_t RigId) const noexcept
{
    for (const ParticleRig& Rig : Rigs)
    {
        if (Rig.RigId == RigId)
        {
            return &Rig;
        }
    }
    return nullptr;
}

ParticleRig* ParticleDocument::FindRigMutable(uint32_t RigId) noexcept
{
    for (ParticleRig& Rig : Rigs)
    {
        if (Rig.RigId == RigId)
        {
            return &Rig;
        }
    }
    return nullptr;
}

const ParticleRig* ParticleDocument::FindRigByTitle(std::string_view Title) const noexcept
{
    for (const ParticleRig& Rig : Rigs)
    {
        if (Rig.RigTitle == Title)
        {
            return &Rig;
        }
    }
    return nullptr;
}

uint32_t ParticleDocument::AddRig(ParticleRig Rig)
{
    uint32_t NextId = 1u;
    for (const ParticleRig& Existing : Rigs)
    {
        NextId = std::max(NextId, Existing.RigId + 1u);
    }
    if (Rig.RigId == 0u || FindRig(Rig.RigId) != nullptr)
    {
        Rig.RigId = NextId;
    }
    Rig.RebuildBakedColourLut();
    Rig.RebuildOriginCaches(RandomSeed);
    const uint32_t AssignedId = Rig.RigId;
    Rigs.push_back(std::move(Rig));
    return AssignedId;
}

bool ParticleDocument::RemoveRig(uint32_t RigId)
{
    const auto It = std::remove_if(
        Rigs.begin(),
        Rigs.end(),
        [RigId](const ParticleRig& Rig) noexcept { return Rig.RigId == RigId; });
    if (It == Rigs.end())
    {
        return false;
    }
    Rigs.erase(It, Rigs.end());
    return true;
}

uint32_t ParticleDocument::QueryTotalAllocatedSlots() const noexcept
{
    uint32_t Total = 0u;
    for (const ParticleRig& Rig : Rigs)
    {
        if (Rig.Enabled)
        {
            Total += std::max(1u, Rig.Sizing.CapacitySlots);
        }
    }
    return Total;
}

} // namespace Frontier::ParticleSpace
