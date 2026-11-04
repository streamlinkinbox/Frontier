/*==========================================================================================================================================
                                                           ATMOSPHEREINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The demo's Atmosphere page (AtmosphereInspectorDashboard.js + AtmosphereInspector.css) on the level's AtmosphereConfiguration.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"
#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/Controls/RasterPlate.h"
#endif

namespace Frontier
{

#if FRONTIER_EDITOR

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ATMOSPHERE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 The demo's Atmosphere page, occupying the whole Inspector column: Overview (the density scan, coefficients, the sky-view
//    capture, the medium recipe), Medium (scale heights, constituents, phase), Light response (transmission, sky & surface,
//    horizon shaping, integration) and Baking.
class AtmosphereInspector final : public RecordInspector
{
  public:
    explicit AtmosphereInspector(AtmosphereConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · ATMOSPHERE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Rayleigh + Mie + O3"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return 4; }
    void                      ConfigureSection(int Section) noexcept override { ActivePane = Section < 0 ? 0 : (Section > 3 ? 3 : Section); }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    bool ConstructOverview(ControlColumn& Page);
    bool ConstructMedium(ControlColumn& Page);
    bool ConstructLightResponse(ControlColumn& Page);
    bool ConstructBaking(ControlColumn& Page);
    void RefreshBakedImages();

    AtmosphereConfiguration& Parameters;
    int                      ActivePane    = 0;
    float                    ProbeAltitude = 8000.0f;   // [m] the density scan's diagnostic probe (AtmosphereColumnInstrument.js)
    int                      ScaleRibbon   = 0;         // the comparator's selected ribbon: 0 Rayleigh, 1 Mie
    bool                     HorizonOpen   = false;     // the Horizon shaping drawer
    int                      BakedView     = 0;         // the Baked atmosphere card's view tab
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    std::array<RasterPlate,4> BakeViews;
    RasterPlate SkyPlate, TransmittancePlate, MultiplePlate;
    uint64_t UploadedGeneration = 0;
    uint64_t UploadedSource = 0;
#endif
    bool                     LookupsOpen   = false;     // the GPU lookup tables drawer
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
