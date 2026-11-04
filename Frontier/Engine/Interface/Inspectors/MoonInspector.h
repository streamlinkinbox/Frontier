/*==========================================================================================================================================
                                                              MOONINSPECTOR.H
==========================================================================================================================================*/
// 🧩 MoonConfiguration: the scene moons.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <string>

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/RasterPlate.h"
#include "Engine/Interface/Inspectors/LunarSurfaceReference.h"
#endif

namespace Frontier
{

class ControlColumn;
class DiagramPen;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    MOON INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class MoonInspector final : public RecordInspector
{
  public:
    explicit MoonInspector(MoonConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return "ENVIRONMENT · MOONS";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return "Dual-body orbital albedo";
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
        Parameters.Enabled              = Enabled;
        Parameters.Instances[0].Visible = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    void               EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches) override;
    void               ConstructInspector() override;
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
    void ConfigureInstance(int Instance) noexcept
    {
        ActiveInstance = std::clamp(Instance, 0, 3);
    }
    // 📝 The demo's Moons entity: Meta "<lit bodies>/4".
    [[nodiscard]] std::string QueryDirectoryReadout() const override
    {
        return std::to_string(
                   std::count_if(Parameters.Instances.begin(), Parameters.Instances.end(), [](const auto &Moon) { return Moon.Visible; })) +
               "/4";
    }

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        const auto& Body = Parameters.Instances[ActiveInstance];
        View.Celestial = true; View.Azimuth = Body.AzimuthDegrees; View.Elevation = Body.ElevationDegrees;
        View.Diameter = Body.AngularDiameterDegrees;
        return true;
    }

  private:
    void RefreshSurface();
    void ConstructSurface(const DiagramPen &Pen, bool Comparison = false, bool Guide = false);
    bool ConstructPhase(ControlColumn &Page);
    bool ConstructPlacement(ControlColumn &Page);
    bool ConstructLight(ControlColumn &Page);
    bool ConstructBodies(ControlColumn &Page);

    MoonConfiguration &Parameters;
    int                ActivePane      = 0;
    int                ActiveInstance  = 0;
    float              CatalogueOffset = 0;
    bool               PlanetsExpanded = false;
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    std::array<LunarSurfaceReference, 6> Surfaces;
    std::array<bool, 6>                  SurfacesLoaded{};
    std::array<RasterPlate, 6>           Portraits;
    RasterPlate                          Surface;
    std::array<double, 7>                SurfaceQuery{};
    double                               LastSurfaceTime = -1.0;
#endif
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
