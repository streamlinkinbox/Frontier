//============================================================================================================================================
//                                                      CONTROLPANEL.H
//============================================================================================================================================
// 🧩 Development editor controls — the hand-drawn widgets of the property sheet. Every widget here is an ImDrawList
//    drawing over an invisible button, so the theme is exact pixels rather than style affection. Callers bracket
//    repeated rows with PushID/PopID; the ids below only need to be unique within one row.

#pragma once

#include "EditorInstance.h"

#include <imgui.h>

namespace Frontier {

class ControlPanel final
{
public:
    // The four faces the host loads. Until assigned, every widget falls back to the current font.
    void AssignFonts(ImFont* Ui, ImFont* Small, ImFont* Mono, ImFont* MonoSmall) noexcept;

    [[nodiscard]] ImFont* QueryUi() const noexcept;
    [[nodiscard]] ImFont* QuerySmall() const noexcept;
    [[nodiscard]] ImFont* QueryMono() const noexcept;
    [[nodiscard]] ImFont* QueryMonoSmall() const noexcept;

    // The reference thin slider: a 100-pixel split pill (figure + unit) beside the 10-pixel track. 30 tall.
    bool SliderPill(const char* Id, float* Figure, float Minimum, float Maximum,
                    uint32_t Decimals, const char* Unit, bool Hi) noexcept;

    // The reference 46×26 switch. Draws at the cursor; the caller aligns it.
    bool Switch(const char* Id, bool* On) noexcept;

    // The three axis cells. A drag on a letter scrubs; a click on a figure types in.
    bool AxisVec3(const char* Id, float Axes[3], float Step, bool Editable) noexcept;

    // The colour chip with its hex readout; a click opens the picker menu.
    bool ColourChip(const char* Id, float Tint[3]) noexcept;

    // The eight tint dots; a click seats the tint.
    bool SwatchRow(const char* Id, float Tint[3]) noexcept;

    // The split pill with its fading menu; the caret turns while the menu stands open. One dropdown
    //    animates at a time — the Quality row is the only caller, and the outliner carries its own fade.
    bool DropDown(const char* Id, uint32_t* Picked,
                  const char Options[kMaxEditorOptions][kMaxEditorOptionChars], uint32_t OptionCount) noexcept;

    // The standing pill. Draws at the cursor, sized to its label; the caller places it.
    bool PillToggle(const char* Label, bool* On) noexcept;

    // Right-aligned dim tabular text across the row's remainder. No interaction.
    void Readout(const char* Text) noexcept;

    // One tint with its alpha scaled — the menu fade's brush. Pure arithmetic, nothing retained.
    static ImU32 FadeTint(ImU32 Tint, float Fade) noexcept;

private:
    // One click-to-type cell: renders the figure, opens an InputText on click, commits on Enter.
    bool TypeInCell(const char* Id, const ImVec2& CellMin, const ImVec2& CellSize,
                    float Current, uint32_t Decimals, float* Committed) noexcept;

    ImFont*  Ui_        = nullptr;
    ImFont*  Small_     = nullptr;
    ImFont*  Mono_      = nullptr;
    ImFont*  MonoSmall_ = nullptr;

    char     TypeIn_[32]   = {};
    ImGuiID  TypeInId_     = 0u;
    bool     TypeInFocus_  = false;

    bool     MenuWasOpen_  = false;   // the fade's rising-edge latch
    double   MenuOpenedAt_ = 0.0;     // the tick-clock instant the menu appeared
    float    ChevronAnim_  = 0.0f;    // the caret turn, 0 = down, 1 = up
};

} // namespace Frontier
