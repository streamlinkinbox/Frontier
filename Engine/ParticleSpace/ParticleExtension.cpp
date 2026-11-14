/*============================================================================================================================================
                                                           PARTICLEEXTENSION.CPP
============================================================================================================================================*/
// 🧩 ParticleExtension lifecycle coordinator implementation (Initialize, Tick, Suspend, Resume, Finalize, WorkspaceExchange notice)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleExtension.h"

#include "Engine/Codecs/SpaceContainerCodec.h"

#include <algorithm>
#include <string>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC MEMBER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ParticleExtension::Initialize(
    ColourSpace::ColourExtension*  ColourCoordinator,
    Workspaces::WorkspaceExchange* ExchangeBus,
    Workspaces::WorkspaceToken     OwnerToken) noexcept
{
    ColourDomain         = ColourCoordinator;
    Exchange             = ExchangeBus;
    DocumentToken        = OwnerToken;
    if (!DocumentToken.VerifyValid())
    {
        DocumentToken.SlotIndex         = 0u;
        DocumentToken.Generation        = 1u;
        DocumentToken.ExtensionCategory = Codecs::SpaceExtensionCategory::ParticleSpace;
    }
    ClockSeconds         = 0.0;
    StepCount            = 0ull;
    SuspensionActive     = false;
    InitializationActive = true;

    if (ActiveDocument.Rigs.empty())
    {
        ActiveDocument.PopulateDefaultSparksRig();
    }
    else
    {
        ActiveDocument.RebuildAllRigCaches();
    }

    PublishChangeNotice("ParticleSpace Initialized");
    return true;
}

void ParticleExtension::Tick(double DeltaSeconds) noexcept
{
    if (!InitializationActive || SuspensionActive || ActiveDocument.PlaybackPaused)
    {
        return;
    }

    const double ClampedDt = std::clamp(DeltaSeconds, 0.0, 0.05) * static_cast<double>(std::max(0.05f, ActiveDocument.TimeScale));
    ClockSeconds += ClampedDt;
    if (ActiveDocument.LoopPlayback && ActiveDocument.PlaybackLoopSec > 0.05f &&
        ClockSeconds >= static_cast<double>(ActiveDocument.PlaybackLoopSec))
    {
        ClockSeconds = std::fmod(ClockSeconds, static_cast<double>(ActiveDocument.PlaybackLoopSec));
    }
    ++StepCount;
}

void ParticleExtension::Suspend() noexcept
{
    if (!InitializationActive)
    {
        return;
    }
    SuspensionActive = true;
}

void ParticleExtension::Resume() noexcept
{
    if (!InitializationActive)
    {
        return;
    }
    SuspensionActive = false;
}

void ParticleExtension::Finalize() noexcept
{
    if (!InitializationActive)
    {
        return;
    }
    PublishChangeNotice("ParticleSpace Finalized");
    InitializationActive = false;
    SuspensionActive     = false;
    Exchange             = nullptr;
    ColourDomain         = nullptr;
}

void ParticleExtension::ResetClock() noexcept
{
    ClockSeconds = 0.0;
    StepCount    = 0ull;
}

void ParticleExtension::NotifyDocumentEdited(std::string_view Summary) noexcept
{
    ActiveDocument.RebuildAllRigCaches();
    PublishChangeNotice(Summary);
}

ParticleExtensionMetrics ParticleExtension::InspectMetrics() const noexcept
{
    ParticleExtensionMetrics Metrics{};
    for (const ParticleRig& Rig : ActiveDocument.Rigs)
    {
        if (Rig.Enabled)
        {
            ++Metrics.ActiveRigCount;
        }
    }
    Metrics.AllocatedSlotCount  = ActiveDocument.QueryTotalAllocatedSlots();
    Metrics.ForceSourceCount    = static_cast<uint32_t>(ActiveDocument.ForceSources.size());
    Metrics.ElapsedClockSeconds = ClockSeconds;
    Metrics.TickCounter         = StepCount;
    Metrics.Initialized         = InitializationActive;
    Metrics.Suspended           = SuspensionActive;
    return Metrics;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PRIVATE MEMBER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ParticleExtension::PublishChangeNotice(std::string_view Summary) noexcept
{
    if (Exchange != nullptr && InitializationActive)
    {
        Exchange->PublishUpdate(DocumentToken, ActiveDocument.QueryTotalAllocatedSlots(), ClockSeconds, Summary);
    }
}

} // namespace Frontier::ParticleSpace
