/*============================================================================================================================================
                                                           VEHICLEINSPECTOR.H
============================================================================================================================================*/
// 🧩 VehicleConfiguration: spawn, the full induction family (stock demo turbo law, unified turbo, Roots / TwinScrew /
//    Centrifugal superchargers with bypass + parasitic tax), transmission, chassis, per-corner soft-body tyres
//    (Spring · XPBD lattice · XPBD projection · Hex8 FEM solid) with pressure/temperature/wear, and the live
//    telemetry strip published by the physics thread.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 Three panes laid over one page (Powertrain · Chassis & Tyres · Physics & Live). The pane selection follows the
//    RecordInspector section contract: QuerySectionCount reports three, ConfigureSection chooses one, and the page's
//    own segmented row mirrors it while the runtime keeps the header, toggle and switch grid.
class VehicleInspector final : public RecordInspector
{
  public:
    explicit VehicleInspector(VehicleConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "VEHICLE · GT-04 CONTROL"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Powertrain + Chassis + Tyres"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return 3; }
    void                      ConfigureSection(int Section) noexcept override { ActivePane = Section < 0 ? 0 : (Section > 2 ? 2 : Section); }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    [[nodiscard]] std::string                QueryDirectoryReadout() const override
    {
        static const char* const kInductionShort[5] = { "NA", "Turbo", "Roots SC", "TwinScrew SC", "Centrifugal SC" };
        const int Induction = Parameters.Induction < 0 ? 0 : (Parameters.Induction > 4 ? 4 : Parameters.Induction);
        return std::to_string(static_cast<int>(Parameters.MassKilograms)) + " kg · " + kInductionShort[Induction];
    }
    [[nodiscard]] RecordOperationalCondition QueryDirectoryCondition() const noexcept override
    {
        return Parameters.Enabled && Parameters.PhysicsActive ? RecordOperationalCondition::Live
                                                              : RecordOperationalCondition::Standby;
    }
    void ConstructInspector() override;

  private:
    void ConstructPowertrainPane();
    void ConstructChassisTyresPane();
    void ConstructPhysicsLivePane();

    VehicleConfiguration& Parameters;
    int                   ActivePane = 0;   // [-] 0 Powertrain · 1 Chassis & Tyres · 2 Physics & Live
    int                   TyreCornerChoice[4] = { 0, 0, 0, 0 };   // [-] segmented-row mirrors of the carcass enums
    bool                  TyreChoicesReady = false;               // [-] mirrors seeded from the configuration once
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
