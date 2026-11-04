/*==========================================================================================================================================
                                                              PLANETINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Four distinct planetary instruments: Venus veil, Jupiter bands, Mars relief and Saturn ring geometry.

#pragma once

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"
#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/RasterPlate.h"
#endif

namespace Frontier
{
class ControlColumn;
#if FRONTIER_EDITOR
class PlanetInspector final : public RecordInspector
{
  public:
    explicit PlanetInspector(PlanetConfiguration &Configuration) : Parameters(Configuration) {}
    const char *QueryEyebrowText() const noexcept override
    {
        return "ENVIRONMENT · PLANET";
    }
    bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    int QuerySectionCount() const noexcept override
    {
        return 2;
    }
    void ConfigureSection(int Section) noexcept override
    {
        ActivePane = std::clamp(Section, 0, 1);
    }
    const char *QueryDirectoryGlyph() const noexcept override
    {
        return "NativeOrbit";
    }
    void EnumerateSwitches(std::vector<InspectorSwitchEntry> &Entries) override;
    void ConstructInspector() override;

    bool QueryViewTarget(InspectorViewTarget &View) const noexcept override
    {
        const auto &Body = Parameters;
        View.Celestial   = true;
        View.Azimuth     = Body.AzimuthDegrees;
        View.Elevation   = Body.ElevationDegrees;
        View.Diameter    = Body.ApparentDiameterDegrees;
        return true;
    }

  private:
    bool                 ConstructVenus(ControlColumn &Page);
    bool                 ConstructJupiter(ControlColumn &Page);
    bool                 ConstructMars(ControlColumn &Page);
    bool                 ConstructSaturn(ControlColumn &Page);
    bool                 ConstructPlacement(ControlColumn &Page);
    void                 RefreshPreview();
    PlanetConfiguration &Parameters;
    int                  ActivePane = 0;
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    RasterPlate            Preview;
    std::array<double, 20> PreviewQuery{};
#endif
};
#endif
} // namespace Frontier
