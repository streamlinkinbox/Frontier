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

    [[nodiscard]] uint32_t QuerySunTab() const noexcept { return SunTabIndex; }
    [[nodiscard]] uint32_t QueryFlareTab() const noexcept { return FlareTabIndex; }
    [[nodiscard]] uint32_t QueryAtmosphereTab() const noexcept { return AtmosphereTabIndex; }
    [[nodiscard]] uint32_t QueryCloudTab() const noexcept { return CloudTabIndex; }
    [[nodiscard]] uint32_t QueryWindTab() const noexcept { return WindTabIndex; }

  private:
    uint32_t SunTabIndex        = 0;   // [-] - 0=Overview, 1=Daylight, 2=Light & colour, 3=Disc & optics
    uint32_t FlareTabIndex      = 0;   // [-] - 0=Compose, 1=Ghosts, 2=Halo, 3=Rays & streaks
    uint32_t AtmosphereTabIndex = 0;   // [-] - 0=Overview, 1=Medium, 2=Light response, 3=Baking
    uint32_t CloudTabIndex      = 0;   // [-] - 0=Overview, 1=Formation, 2=Rendering, 3=Weather
    uint32_t WindTabIndex       = 0;   // [-] - 0=Flow, 1=Variability, 2=Altitude, 3=Receivers
};

} // namespace Frontier
