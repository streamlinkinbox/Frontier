//========================================================================================================================
// 🧩 ConfigurationInspector — content of the Control Centre's Input page ("Keybindings Setup") and Notifications page
//    ("Telemetry & Notifications"). Both are 1:1 ports of Notch OtherModals.tsx: InputSettingsModal keeps its own
//    fixed palette (#161415 sheet, #ececec / #868384 / #c4c4c4 inks, #0d0a0b fields, #2a2627 borders, #e254eb accent);
//    TelemetrySettingsModal is a GenericSettingsModal of FormFieldRow + FormToggle in the theme palette.
//    Each inspector holds an Applied and a Draft record; the footer's Save enables only while they differ.
//========================================================================================================================
#pragma once

#include "ControlKit.h"
#include "ConfigurationStructure.h"

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class InputInspector
{
public:
    InputInspector() noexcept;

    [[nodiscard]] const InputPreferences& QueryApplied() const noexcept { return Applied; }
    [[nodiscard]] const InputPreferences& QueryDraft()   const noexcept { return Draft; }
    [[nodiscard]] bool     IsDirty()      const noexcept { return !(Draft == Applied); }
    [[nodiscard]] bool     IsDefault()    const noexcept { return Draft == InputPreferences{}; }
    [[nodiscard]] uint32_t QueryRevision() const noexcept { return Revision; }

    void Apply()         noexcept { Applied = Draft; ++Revision; }
    void Seed(const InputPreferences& Persisted) noexcept { Applied = Persisted; Draft = Persisted; ++Revision; }
    void Discard()       noexcept { Draft = Applied; CloseMenus(); }
    void ResetDefaults() noexcept { Draft = InputPreferences{}; }
    void CloseMenus()    noexcept { ProfileOpen = false; DraggingSlider = false; }
    [[nodiscard]] bool HasOpenMenu() const noexcept { return ProfileOpen; }
    [[nodiscard]] bool IsDragging()  const noexcept { return DraggingSlider; }

    // Records the page body (already clipped by the caller) offset by ScrollY; returns the content height.
    float ConstructInputLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;
    // The open profile menu floats above the footer; the host calls this after the body clip is popped.
    void  ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept;

    [[nodiscard]] static const char* QueryProfileName(InputProfileCategory Profile) noexcept;

    // Notch InputSettingsModal palette (fixed, not themed — the reference hard-codes it).
    static constexpr ColorQuad InkStrong { 0xEC / 255.0f, 0xEC / 255.0f, 0xEC / 255.0f, 1.0f };   // #ececec
    static constexpr ColorQuad InkLabel  { 0xC4 / 255.0f, 0xC4 / 255.0f, 0xC4 / 255.0f, 1.0f };   // #c4c4c4
    static constexpr ColorQuad InkMuted  { 0x86 / 255.0f, 0x83 / 255.0f, 0x84 / 255.0f, 1.0f };   // #868384
    static constexpr ColorQuad InkDim    { 0x5A / 255.0f, 0x56 / 255.0f, 0x57 / 255.0f, 1.0f };   // #5a5657 (read-only field)
    static constexpr ColorQuad Field     { 0x0D / 255.0f, 0x0A / 255.0f, 0x0B / 255.0f, 1.0f };   // #0d0a0b
    static constexpr ColorQuad Border    { 0x2A / 255.0f, 0x26 / 255.0f, 0x27 / 255.0f, 1.0f };   // #2a2627
    static constexpr ColorQuad Accent    { 0xE2 / 255.0f, 0x54 / 255.0f, 0xEB / 255.0f, 1.0f };   // #e254eb
    static constexpr ColorQuad KnobTrack { 0x3B / 255.0f, 0x36 / 255.0f, 0x38 / 255.0f, 1.0f };   // #3b3638 (toggle off)

    // Proof-harness access: extents recorded on the last frame.
    [[nodiscard]] PlaneExtent QueryProfileDropdownExtent()  const noexcept { return ProfileExtent; }
    [[nodiscard]] PlaneExtent QuerySensitivitySliderExtent() const noexcept { return SensitivityExtent; }
    [[nodiscard]] PlaneExtent QueryCustomShortcutsSwitchExtent() const noexcept { return CustomSwitchExtent; }
    [[nodiscard]] PlaneExtent QueryAdvancedSwitchExtent()   const noexcept { return AdvancedSwitchExtent; }
    [[nodiscard]] PlaneExtent QueryInvertSwitchExtent()     const noexcept { return InvertSwitchExtent; }

private:
    // Notch toggle: w-11 h-6 p-[2px] (or w-10 h-5 for the inline one), knob w-5 (w-4) bg-white, accent when on.
    static ControlHit RecordToggle(PixelSpace& Surface, const PlaneExtent& Extent, bool On, const ControlPointer& Pointer, float Opacity) noexcept;
    static void       RecordField(PixelSpace& Surface, const PlaneExtent& Extent, float Opacity) noexcept;   // bg-[#0d0a0b] border-[#2a2627] rounded-lg

    InputPreferences Applied;
    InputPreferences Draft;
    uint32_t         Revision;
    bool             ProfileOpen;
    bool             DraggingSlider;
    int              ProfilePick;   // choice made in the floating layer, consumed next frame; -1 none

    PlaneExtent ProfileExtent, SensitivityExtent, CustomSwitchExtent, AdvancedSwitchExtent, InvertSwitchExtent;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                NOTIFICATION INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class NotificationInspector
{
public:
    NotificationInspector() noexcept;

    [[nodiscard]] const NotificationPreferences& QueryApplied() const noexcept { return Applied; }
    [[nodiscard]] const NotificationPreferences& QueryDraft()   const noexcept { return Draft; }
    [[nodiscard]] bool     IsDirty()      const noexcept { return !(Draft == Applied); }
    [[nodiscard]] bool     IsDefault()    const noexcept { return Draft == NotificationPreferences{}; }
    [[nodiscard]] uint32_t QueryRevision() const noexcept { return Revision; }

    void Apply()         noexcept { Applied = Draft; ++Revision; }
    void Seed(const NotificationPreferences& Persisted) noexcept { Applied = Persisted; Draft = Persisted; ++Revision; }
    void Discard()       noexcept { Draft = Applied; DraggingSlider = false; }
    void ResetDefaults() noexcept { Draft = NotificationPreferences{}; }
    // The dashboard "FPS Overlay" tile and this page edit the same flag: the host mirrors the tile into both copies.
    void MirrorFrameRateOverlay(bool On) noexcept { Applied.ShowFrameRateOverlay = On; Draft.ShowFrameRateOverlay = On; }
    [[nodiscard]] bool IsDragging() const noexcept { return DraggingSlider; }

    float ConstructNotificationLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;

    static constexpr uint32_t RowCount = 8u;   // 0 FPS  1 RAM  2 Scene  |  3 Baking  4 Render  5 Autosave  6 Frame drops  |  7 hold slider
    [[nodiscard]] PlaneExtent QueryToggleExtent(uint32_t Row) const noexcept { return Row < 7u ? ToggleExtents[Row] : PlaneExtent{}; }
    [[nodiscard]] PlaneExtent QueryHoldSliderExtent() const noexcept { return HoldSliderExtent; }

private:
    // Notch FormToggle: w-12 h-6 rounded-full, accentColor when on / white-10 off, knob w-5 h-5 bg-white top-0.5, x 2 → 26.
    static ControlHit RecordFormToggle(PixelSpace& Surface, const PlaneExtent& Extent, bool On, const ControlPointer& Pointer, float Opacity) noexcept;

    NotificationPreferences Applied;
    NotificationPreferences Draft;
    uint32_t                Revision;
    bool                    DraggingSlider;
    PlaneExtent             ToggleExtents[7];
    PlaneExtent             HoldSliderExtent;
};

} // namespace Frontier
