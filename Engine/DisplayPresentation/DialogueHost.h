//========================================================================================================================
// 🧩 DialogueHost — modal confirmation / notice dialogues drawn above a page.
//    One component, nine presets, five tones. Layout follows the Slate kit .panel (header · body · footer);
//    tone colours the header icon disc and the primary button.
//    ⚠️ Derived from References/UIComponents.html tokens — the dedicated dialogue reference was not in the
//       repository at the time of writing; re-skin here once it lands.
//========================================================================================================================
#pragma once

#include "ControlKit.h"
#include "MotionIntegrator.h"

#include <cstdint>

namespace Frontier {

enum class DialogueToneCategory : uint32_t { Danger = 0, Success = 1, Info = 2, Warning = 3, Caution = 4 };

enum class DialoguePresetCategory : uint32_t
{
    None            = 0,
    ConfirmDiscard  = 1,   // red     "Discard changes?"            Cancel · Discard
    ConfirmApply    = 2,   // green   "Apply changes?"              Cancel · Apply
    UnsavedChanges  = 3,   // orange  "Unsaved changes"             Discard · Keep editing · Apply
    ResetDefaults   = 4,   // orange  "Reset to defaults?"          Cancel · Reset
    Info            = 5,   // blue    notice                        OK
    Success         = 6,   // green   notice                        OK
    Warning         = 7,   // orange  notice                        OK
    Caution         = 8,   // amber   notice                        OK
    Error           = 9,   // red     notice                        OK
    Count           = 10
};

// What the user chose when a dialogue closed. Hosts poll TakeVerdict() once per frame.
enum class DialogueVerdictCategory : uint32_t { Pending = 0, Cancel = 1, Primary = 2, Secondary = 3 };

struct DialoguePresetStructure
{
    DialogueToneCategory       Tone;
    ControlCentreIconCategory  Icon;
    const char*                Title;
    const char*                Subtitle;        // header second line (11.5 px faint)
    const char*                Body;            // 13.5 px text
    const char*                CancelLabel;     // nullptr → no cancel button
    const char*                SecondaryLabel;  // nullptr → none (UnsavedChanges: "Discard")
    const char*                PrimaryLabel;
};

class DialogueHost
{
public:
    DialogueHost() noexcept;

    void  Open(DialoguePresetCategory Preset) noexcept;
    void  Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return Active != DialoguePresetCategory::None; }
    [[nodiscard]] bool IsVisible() const noexcept { return IsOpen() || !Motion.Spring(EnterChannel).Settled; }
    [[nodiscard]] DialoguePresetCategory QueryActive() const noexcept { return Active; }

    // Returns the verdict recorded since the last call and clears it.
    [[nodiscard]] DialogueVerdictCategory TakeVerdict() noexcept;

    void  Advance(float DeltaSeconds) noexcept;

    // Records the scrim + dialogue centred in Host (the page card). Consumes pointer input while visible.
    void  ConstructDialogueLayout(PixelSpace& Surface, const PlaneExtent& Host, const ControlPointer& Pointer) noexcept;

    [[nodiscard]] static const DialoguePresetStructure& QueryPreset(DialoguePresetCategory Preset) noexcept;
    [[nodiscard]] static ColorQuad QueryToneColour(DialogueToneCategory Tone) noexcept;

    // Geometry (kit .panel): width 420, header 14/20 pad, footer 12/20 pad, body 18/20.
    static constexpr float Width         = 420.0f;
    static constexpr float HeaderPadY    = 14.0f, HeaderPadX = 20.0f, HeaderIconDisc = 28.0f, HeaderIconRadius = 9.0f;
    static constexpr float BodyPadY      = 18.0f, BodyPadX = 20.0f;
    static constexpr float FooterPadY    = 12.0f, FooterPadX = 20.0f, FooterGap = 8.0f;
    static constexpr float ScrimAlpha    = 0.4f;
    static constexpr float EnterDuration = 0.2f;

    // Extents for hit-testing from proof harnesses (valid after the last ConstructDialogueLayout).
    [[nodiscard]] PlaneExtent QueryButtonExtent(DialogueVerdictCategory Which) const noexcept;

private:
    DialoguePresetCategory  Active;
    DialogueVerdictCategory Verdict;
    MotionIntegrator        Motion;
    uint32_t                EnterChannel;      // 0 → 1 while opening (scale .95 → 1, alpha 0 → 1)
    PlaneExtent             LastCancel, LastSecondary, LastPrimary;
    bool                    PressedInside;     // pointer went down on the dialogue (not the scrim)
    bool                    PressedOnScrim;    // pointer went down on the scrim while the dialogue was open
};

} // namespace Frontier
