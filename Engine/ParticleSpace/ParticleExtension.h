/*============================================================================================================================================
                                                            PARTICLEEXTENSION.H
============================================================================================================================================*/
// 🧩 Designated ParticleSpace lifecycle coordinator managing active ParticleDocument, ColourSpace link, LUT sheet & WorkspaceExchange

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourExtension.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/Workspaces/WorkspaceExchange.h"
#include "Engine/Workspaces/WorkspaceToken.h"

#include <cstdint>
#include <string_view>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleExtensionMetrics
{
    uint32_t ActiveRigCount      = 0u;   // [count] - Total enabled rigs in active document
    uint32_t AllocatedSlotCount  = 0u;   // [count] - Sum of allocated ParticleSlot records
    uint32_t ForceSourceCount    = 0u;   // [count] - Coupled vector field force sources
    double   ElapsedClockSeconds = 0.0;  // [s]     - Accumulated particle simulation time
    uint64_t TickCounter         = 0ull; // [count] - Total simulation ticks executed
    bool     Initialized         = false;// [-]     - True between Initialize() and Finalize()
    bool     Suspended           = false;// [-]     - True while paused
};

class ParticleExtension
{
public:
    ParticleExtension() = default;

    bool Initialize(
        ColourSpace::ColourExtension*  ColourCoordinator = nullptr,
        Workspaces::WorkspaceExchange* ExchangeBus       = nullptr,
        Workspaces::WorkspaceToken     OwnerToken        = {}) noexcept;

    void Tick(double DeltaSeconds) noexcept;
    void Suspend() noexcept;
    void Resume() noexcept;
    void Finalize() noexcept;
    void ResetClock() noexcept;

    [[nodiscard]] ParticleDocument&       AccessActiveDocument() noexcept { return ActiveDocument; }
    [[nodiscard]] const ParticleDocument& QueryActiveDocument() const noexcept { return ActiveDocument; }

    void NotifyDocumentEdited(std::string_view Summary) noexcept;

    [[nodiscard]] ParticleExtensionMetrics InspectMetrics() const noexcept;

private:
    void PublishChangeNotice(std::string_view Summary) noexcept;

    ParticleDocument               ActiveDocument       {};      // [-]   - Active authored particle document
    ColourSpace::ColourExtension*  ColourDomain         = nullptr;// [-]  - Optional shared ColourSpace coordinator
    Workspaces::WorkspaceExchange* Exchange             = nullptr;// [-]  - Optional cross-workspace notification bus
    Workspaces::WorkspaceToken     DocumentToken        {};      // [-]   - Owning .ParticleSpace workspace token
    double                         ClockSeconds         = 0.0;   // [s]   - Simulation clock
    uint64_t                       StepCount            = 0ull;  // [count]- Monotonic tick counter
    bool                           InitializationActive = false; // [-]   - Lifecycle initialized marker
    bool                           SuspensionActive     = false; // [-]   - Lifecycle suspended marker
};

} // namespace Frontier::ParticleSpace
