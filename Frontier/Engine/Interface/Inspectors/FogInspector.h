/*==========================================================================================================================================
                                                               FOGINSPECTOR.H
==========================================================================================================================================*/
// 🧩 FogConfiguration: height fog and bounded local fog.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOG INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class FogInspector final : public RecordInspector
{
  public:
    explicit FogInspector(FogConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return Parameters.LocalVolume  ? "ENVIRONMENT · LOCAL FOG"
               : Parameters.AerialOnly ? "ENVIRONMENT · ATMOSPHERIC FOG"
                                       : "ENVIRONMENT · HEIGHT FOG";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return Parameters.AerialOnly ? "Spectral extinction reference" : "Height / bounded medium";
    }
    [[nodiscard]] bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.AerialOnly ? Parameters.AerialEnabled : Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        if (Parameters.AerialOnly) Parameters.AerialEnabled = Enabled;
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
        return Parameters.LocalVolume ? 0 : 3;
    }
    void ConfigureSection(int Section) noexcept override
    {
        ActivePane = std::clamp(Section, 0, 2);
    }
    [[nodiscard]] const char *QueryDirectoryGlyph() const noexcept override
    {
        return Parameters.LocalVolume ? "NativeLocalFog" : "NativeFog";
    }

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        if (!Parameters.LocalVolume) return false;
        View.Position = {Parameters.CentreMetres[0],Parameters.CentreMetres[1],Parameters.CentreMetres[2]};
        View.Radius = std::max({Parameters.HalfExtentsMetres[0],Parameters.HalfExtentsMetres[1],Parameters.HalfExtentsMetres[2]});
        return true;
    }

  private:
    bool ConstructVisibility(ControlColumn &Page);
    bool ConstructMedium(ControlColumn &Page);
    bool ConstructResponse(ControlColumn &Page);
    bool ConstructLocal(ControlColumn &Page);

    FogConfiguration &Parameters;
    int               ActivePane    = 0;
    bool              Aerial        = false;
    float             ProbeDistance = 200.0f;
    float             ProbeHeight   = 2.0f;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
