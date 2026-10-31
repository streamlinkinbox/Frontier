/*==========================================================================================================================================
                                                        SCENE INSPECTOR PANEL
                                     ------------------------------------------------------------------
           Global Engine Inspector host: renders Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind, and custom inspectors.
==========================================================================================================================================*/

// 🧩 Global Engine Inspector host: renders Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind, and custom inspectors.

#pragma once

#include <cstdint>

namespace Frontier
{

class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE INSPECTOR PANEL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Portable + SDK-backed Inspector host shared across all Frontier projects. Selects the active record from
//    SceneDirectoryPanel, resolves its RecordEditorDescription from SceneDirectory, and renders either one of the
//    six global VehicleHMI observatory dashboards (Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind), a project's
//    custom inspector callback, or declarative property cards. Every colour token derives from ControlKit::Palette().
class SceneInspectorPanel
{
  public:
    void ConstructInterface(SceneDirectoryPanel& DirectoryPanel);

    void ConfigureSunTab(uint32_t TabIndex) noexcept { SunTabIndex = TabIndex % 4u; }
    void ConfigureFlareTab(uint32_t TabIndex) noexcept { FlareTabIndex = TabIndex % 4u; }
    void ConfigureAtmosphereTab(uint32_t TabIndex) noexcept { AtmosphereTabIndex = TabIndex % 4u; }
    void ConfigureCloudTab(uint32_t TabIndex) noexcept { CloudTabIndex = TabIndex % 4u; }
    void ConfigureWindTab(uint32_t TabIndex) noexcept { WindTabIndex = TabIndex % 4u; }
    void ConfigureFogTab(uint32_t TabIndex) noexcept { FogTabIndex = TabIndex % 4u; }
    void ConfigureStarsTab(uint32_t TabIndex) noexcept { StarsTabIndex = TabIndex % 4u; }
    void ConfigureMoonTab(uint32_t TabIndex) noexcept { MoonTabIndex = TabIndex % 4u; }
    void ConfigureWeatherTab(uint32_t TabIndex) noexcept { WeatherTabIndex = TabIndex % 4u; }

    [[nodiscard]] uint32_t QuerySunTab() const noexcept { return SunTabIndex; }
    [[nodiscard]] uint32_t QueryFlareTab() const noexcept { return FlareTabIndex; }
    [[nodiscard]] uint32_t QueryAtmosphereTab() const noexcept { return AtmosphereTabIndex; }
    [[nodiscard]] uint32_t QueryCloudTab() const noexcept { return CloudTabIndex; }
    [[nodiscard]] uint32_t QueryWindTab() const noexcept { return WindTabIndex; }
    [[nodiscard]] uint32_t QueryFogTab() const noexcept { return FogTabIndex; }
    [[nodiscard]] uint32_t QueryStarsTab() const noexcept { return StarsTabIndex; }
    [[nodiscard]] uint32_t QueryMoonTab() const noexcept { return MoonTabIndex; }
    [[nodiscard]] uint32_t QueryWeatherTab() const noexcept { return WeatherTabIndex; }

  private:
    uint32_t SunTabIndex        = 0;   // [-] - 0=Overview, 1=Daylight, 2=Light & colour, 3=Disc & optics
    uint32_t FlareTabIndex      = 0;   // [-] - 0=Composite, 1=Ghosts, 2=Halo, 3=Rays & streaks
    uint32_t AtmosphereTabIndex = 0;   // [-] - 0=Overview, 1=Medium, 2=Light response, 3=Baking
    uint32_t CloudTabIndex      = 0;   // [-] - 0=Overview, 1=Formation, 2=Rendering, 3=Weather
    uint32_t WindTabIndex       = 0;   // [-] - 0=Flow, 1=Variability, 2=Altitude, 3=Receivers
    uint32_t FogTabIndex        = 0;   // [-] - 0=Visibility, 1=Height bank, 2=Aerial gate, 3=Sun phase
    uint32_t StarsTabIndex      = 0;   // [-] - 0=Planisphere, 1=Magnitudes, 2=Scintillation, 3=Galactic
    uint32_t MoonTabIndex       = 0;   // [-] - 0=Selenograph, 1=Synodic, 2=Angular tape, 3=Bodies
    uint32_t WeatherTabIndex    = 0;   // [-] - 0=Lightning, 1=Thunder, 2=Precipitation, 3=Deposit grid
};

} // namespace Frontier
