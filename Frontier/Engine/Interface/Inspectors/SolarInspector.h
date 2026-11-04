/*==========================================================================================================================================
                                                              SOLARINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The demo's Sun page (SunInspectorDashboard.js + SunInspector.css) on the level's SolarConfiguration.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLAR INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 The demo's Sun page (SunInspectorDashboard.js), occupying the whole Inspector column.
class SolarInspector final : public RecordInspector
{
  public:
    explicit SolarInspector(SolarConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · SOLAR"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Directional emitter + shafts"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return 4; }
    void                      ConfigureSection(int Section) noexcept override { ActivePane = Section < 0 ? 0 : (Section > 3 ? 3 : Section); }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;
    // 📝 The demo's Sun entity: Meta "38°", State live above the horizon, warm below it.
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    [[nodiscard]] RecordOperationalCondition QueryDirectoryCondition() const noexcept override
    {
        return Parameters.ElevationDegrees > 0.0f ? RecordOperationalCondition::Live : RecordOperationalCondition::Warm;
    }

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        const auto& Body = Parameters;
        View.Celestial = true; View.Azimuth = Body.AzimuthDegrees; View.Elevation = Body.ElevationDegrees;
        View.Diameter = Body.AngularDiameterDeg;
        return true;
    }

  private:
    // 📝 SolarDayData: the elevation every quarter hour for one date and place, with its 0° crossings.
    struct SolarDayPath
    {
        std::string            Key{};
        std::array<float, 97>  Elevations{};
        float                  RiseHours = -1.0f;   // [h] −1: none
        float                  SetHours  = -1.0f;   // [h] −1: none
        float                  DayHours  = 0.0f;    // [h]
        bool                   Solved    = false;
    };

    void ResolveDayPath();

    SolarConfiguration& Parameters;
    int                 ActivePane    = 0;
    int                 PressedPreset = -1;
    SolarDayPath        DayPath{};
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
