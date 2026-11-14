/*============================================================================================================================================
                                                        PARTICLEOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Native Frontier Particle Studio Outliner pane with census tiles, search, 14 active category filters (zero Quarantine), and C++ ForceFields rows

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <cstdint>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleOutlinerFilterMode : uint8_t
{
    AllEntries   = 0u, // [-] - Display both coupled ForceFields environment and Particle Effects
    EffectsOnly  = 1u, // [-] - Display only Particle Effects across active categories
    ForcesOnly   = 2u, // [-] - Display only coupled C++ ForceFields environment rows
    SingleDomain = 3u, // [-] - Display only Particle Effects matching FilteredCategory
};

enum class ConstructSheetPage : uint8_t
{
    ParticlePresets = 0u, // [-] - Browse and construct from the 93 non-quarantined particle presets
    ForceFieldModels = 1u, // [-] - Browse and construct from Frontier's 19 C++ ForceModel entries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLEOUTLINERPANEL
//------------------------------------------------------------------------------------------------------------------------

class ParticleOutlinerPanel
{
public:
    ParticleOutlinerPanel() noexcept = default;
    ~ParticleOutlinerPanel() noexcept;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;

    void ConfigureFilterAll() noexcept;
    void ConfigureFilterEffects() noexcept;
    void ConfigureFilterForces() noexcept;
    void ConfigureFilterCategory(ParticleEffectCategory Category) noexcept;
    void ConfigureSearchQuery(std::string_view Query) noexcept;
    void ConfigureCompactMode(bool Enabled) noexcept;
    void ConfigureScrollOffset(float OffsetY) noexcept;
    void OpenConstructSheet(ConstructSheetPage Page = ConstructSheetPage::ParticlePresets) noexcept;
    void CloseConstructSheet() noexcept;

    [[nodiscard]] ParticleOutlinerFilterMode QueryFilterMode() const noexcept;
    [[nodiscard]] ParticleEffectCategory     QueryFilteredCategory() const noexcept;
    [[nodiscard]] bool                       QueryCompactMode() const noexcept;
    [[nodiscard]] bool                       QueryConstructSheetOpen() const noexcept;
    [[nodiscard]] float                      QueryScrollOffset() const noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void RenderConstructOverlay(
        PixelSpace&             Surface,
        Rectangle2D             PaneBounds,
        Rectangle2D             AnchorButton,
        const PaneInputReading& Input);

    ParticleWorkspaceSession*  Session               = nullptr;                                       // [-]  - Attached authoring session
    TextEditBuffer             SearchBuffer          {};                                              // [-]  - Live Outliner search filter
    ParticleOutlinerFilterMode FilterMode            = ParticleOutlinerFilterMode::AllEntries;        // [-]  - Active top filter scope
    ParticleEffectCategory     FilteredCategory      = ParticleEffectCategory::FerrofluidFields;   // [-]  - Active category when SingleDomain
    bool                       CompactMode           = false;                                         // [-]  - Compact single-line row toggle
    bool                       EnvironmentExpanded   = true;                                          // [-]  - Environment group disclosure
    bool                       EffectsExpanded       = true;                                          // [-]  - Particle Effects group disclosure
    bool                       ConstructSheetOpen    = false;                                         // [-]  - "+" construct dropdown visibility
    ConstructSheetPage         ActiveConstructPage   = ConstructSheetPage::ParticlePresets;           // [-]  - Active construct sheet page
    ParticleEffectCategory     ConstructCategory     = ParticleEffectCategory::CurlFilaments;           // [-]  - Selected preset shelf in sheet
    float                      ScrollOffsetY         = 0.0f;                                          // [px] - Vertical tree scroll offset
    float                      LastTreeContentH      = 0.0f;                                          // [px] - Measured scroll content extent
    float                      SheetScrollOffsetY    = 0.0f;                                          // [px] - Construct sheet scroll offset
};

} // namespace Frontier::Workspaces
