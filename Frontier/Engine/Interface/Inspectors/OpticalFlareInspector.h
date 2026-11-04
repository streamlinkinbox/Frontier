/*==========================================================================================================================================
                                                          OPTICALFLAREINSPECTOR.H
==========================================================================================================================================*/
// 🧩 OpticalFlareConfiguration: the aperture lens flare.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/RasterPlate.h"
#include <future>
#endif

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPTICAL FLARE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class OpticalFlareInspector final : public RecordInspector
{
  public:
    explicit OpticalFlareInspector(OpticalFlareConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return "ENVIRONMENT · OPTICAL FLARE";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return "Aperture diffraction";
    }
    [[nodiscard]] bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    void EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches) override;
    void ConstructInspector() override;
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    [[nodiscard]] bool QueryReferenceReady() const noexcept
    {
        return Composite.QueryReady();
    }
#endif
    [[nodiscard]] bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    [[nodiscard]] int QuerySectionCount() const noexcept override
    {
        return 4;
    }
    void ConfigureSection(int Section) noexcept override
    {
        ActivePane = std::clamp(Section, 0, 3);
    }

  private:
    void RefreshReference();
    void ConstructExport();
    bool ConstructLightStack(ControlColumn &Page);
    bool ConstructGhosts(ControlColumn &Page);
    bool ConstructHalo(ControlColumn &Page);
    bool ConstructRays(ControlColumn &Page);

    OpticalFlareConfiguration &Parameters;
    int                        ActivePane = 0;
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    RasterPlate                     Composite;
    std::future<std::vector<float>> PendingRadiance;
    std::vector<float>              LinearRadiance;
    std::array<double, 21>          ResidentQuery{};
    std::array<double, 21>          PendingQuery{};
    std::array<char, 1024>          ExportDestination = {'L', 'e', 'n', 's', 'F', 'l', 'a', 'r', 'e', '.', 'p', 'f', 'm', 0};
    std::string                     ReferenceDiagnostic;
    std::string                     ExportDiagnostic;
#endif
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
