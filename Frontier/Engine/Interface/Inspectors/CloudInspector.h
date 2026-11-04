/*==========================================================================================================================================
                                                              CLOUDINSPECTOR.H
==========================================================================================================================================*/
// 🧩 CloudConfiguration: the global cloud layer and bounded local clouds.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/RasterPlate.h"
#endif

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOUD INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class CloudInspector final : public RecordInspector
{
  public:
    explicit CloudInspector(CloudConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return Parameters.LocalVolume ? "ENVIRONMENT · LOCAL CLOUD" : "ENVIRONMENT · CLOUDS";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return "3D Perlin-Worley raymarch";
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
    [[nodiscard]] bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    [[nodiscard]] int QuerySectionCount() const noexcept override
    {
        return Parameters.LocalVolume ? 0 : 4;
    }
    void ConfigureSection(int Section) noexcept override
    {
        ActivePane = std::clamp(Section, 0, 3);
    }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches) override;
    void                      ConstructInspector() override;
    [[nodiscard]] const char *QueryDirectoryGlyph() const noexcept override
    {
        return Parameters.LocalVolume ? "NativeLocalCloud" : "NativeClouds";
    }

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        if (!Parameters.LocalVolume) return false;
        View.Position = {Parameters.CentreMetres[0],Parameters.CentreMetres[1],Parameters.CentreMetres[2]};
        View.Radius = std::max({Parameters.HalfExtentsMetres[0],Parameters.HalfExtentsMetres[1],Parameters.HalfExtentsMetres[2]});
        return true;
    }

  private:
    bool ConstructOverview(ControlColumn &Page);
    bool ConstructFormation(ControlColumn &Page);
    bool ConstructRendering(ControlColumn &Page);
    bool ConstructWeather(ControlColumn &Page);
    bool ConstructLocal(ControlColumn &Page);
    void RefreshDensity(double Aspect);

    CloudConfiguration &Parameters;
    int                 ActivePane    = 0;
    bool                LayerExpanded = false;
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    RasterPlate            TopDensity;
    RasterPlate            SideDensity;
    std::array<double, 24> DensityQuery{};
#endif
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
