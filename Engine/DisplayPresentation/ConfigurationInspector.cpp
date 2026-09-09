//========================================================================================================================
// 🧩 ConfigurationInspector — Input ("Keybindings Setup") and Notifications ("Telemetry & Notifications") page bodies.
//    Geometry and colours are Notch OtherModals.tsx / SharedUI.tsx verbatim; see the header for the palette.
//========================================================================================================================
#include "ConfigurationInspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier {

namespace {

constexpr const char* ProfileNames[3] = { "Blender (Default)", "Maya / Unity", "Unreal Engine" };

// Notch InputSettingsModal figures
constexpr float LabelWidth  = 140.0f;   // w-[140px]
constexpr float LabelSize   = 13.0f;    // text-[13px]
constexpr float FieldPadX   = 16.0f;    // px-4
constexpr float FieldPadY   = 10.0f;    // py-2.5
constexpr float FieldRadius = 8.0f;     // rounded-lg
constexpr float FieldH      = FieldPadY * 2.0f + 20.0f;   // 13 px text → ~20 px line box → 40 px
constexpr float RowGap      = 12.0f;    // gap-3
constexpr float GroupGap    = 24.0f;    // gap-6 between groups
constexpr float TitleSize   = 14.5f;    // text-[14.5px]
constexpr float SubSize     = 12.0f;    // text-[12px]

// Notch GenericSettingsModal / FormFieldRow figures
constexpr float FormLabelCol = 160.0f;  // grid-cols-[160px_1fr]
constexpr float FormRowPadY  = 16.0f;   // py-4
constexpr float FormLabelSize= 14.0f;   // text-sm
constexpr float FormRowH     = FormRowPadY * 2.0f + 24.0f;   // toggle h-6
constexpr float AlertsHeadTop= 32.0f;   // mt-8
constexpr float AlertsHeadGap= 16.0f;   // mb-4

inline ColorQuad Faded(ColorQuad C, float A) noexcept { C.Alpha *= A; return C; }

} // namespace

//========================================================================================================================
//                                                   INPUT INSPECTOR
//========================================================================================================================

InputInspector::InputInspector() noexcept
    : Applied{}, Draft{}, Revision(0u), ProfileOpen(false), DraggingSlider(false), ProfilePick(-1)
    , ProfileExtent{}, SensitivityExtent{}, CustomSwitchExtent{}, AdvancedSwitchExtent{}, InvertSwitchExtent{}
{
}

const char* InputInspector::QueryProfileName(InputProfileCategory Profile) noexcept
{
    return ProfileNames[std::min<uint32_t>(static_cast<uint32_t>(Profile), 2u)];
}

ControlHit InputInspector::RecordToggle(PixelSpace& Surface, const PlaneExtent& Extent, bool On, const ControlPointer& Pointer, float Opacity) noexcept
{
    // rounded-full p-[2px]; knob = height − 4, bg-white shadow-sm; translate-x = width − height.
    ControlHit Hit{};
    Hit.Hovered = ControlKit::Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    const float H = Extent.Height(), R = H * 0.5f;
    Surface.FillRectangle(Extent, Faded(On ? Accent : KnobTrack, Opacity), R);
    const float Knob = H - 4.0f;
    const float X = Extent.MinimumX + 2.0f + (On ? Extent.Width() - H : 0.0f);
    ControlKit::FillCircle(Surface, X + Knob * 0.5f, Extent.MinimumY + 2.0f + Knob * 0.5f, Knob * 0.5f, Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }, Opacity));
    return Hit;
}

void InputInspector::RecordField(PixelSpace& Surface, const PlaneExtent& Extent, float Opacity) noexcept
{
    Surface.FillRectangle(Extent, Faded(Field, Opacity), FieldRadius);
    ControlKit::OutlineRounded(Surface, Extent, Faded(Border, Opacity), FieldRadius);
}

float InputInspector::ConstructInputLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    // Notch: px-8 lg:px-12 py-8, max-w-3xl (768 px) column, flex-col gap-6.
    const float X = Body.MinimumX + 16.0f;                              // lg:px-12 − the host's p-8
    const float W = std::min(Body.Width() - 32.0f, 768.0f);
    float Y = Body.MinimumY - ScrollY;
    ControlPointer Local = Pointer;
    if (ProfileOpen) Local.Enabled = false;                             // the floating menu owns the pointer

    // Label + field row: label w-[140px] text-[13px] #c4c4c4 (+ optional accent asterisk), control flex-1.
    auto LabelRow = [&](const char* Label, bool Required) -> PlaneExtent
    {
        const PlanePoint M = Surface.MeasureText(Label, LabelSize);
        Surface.Text(X, Y + (FieldH - M.Y) * 0.5f, Faded(InkLabel, Opacity), Label, LabelSize);
        if (Required) Surface.Text(X + M.X + 4.0f, Y + (FieldH - M.Y) * 0.5f, Faded(Accent, Opacity), "*", LabelSize);   // ml-1
        return Spanning(X + LabelWidth + 16.0f, Y, W - LabelWidth - 16.0f, FieldH);   // gap-4
    };
    auto Divider = [&]
    {
        Y += GroupGap;                                                   // group gap-6 …
        Surface.FillRectangle(Spanning(X, Y + 4.0f, W, 1.0f), Faded(Border, Opacity));   // h-px my-1
        Y += 1.0f + 8.0f + GroupGap;
    };

    // ── Input Profile ────────────────────────────────────────────────────────────────────────────────────────────────
    {
        PlaneExtent Ctl = LabelRow("Preset profile", true);
        if (ProfilePick >= 0) { Draft.Profile = static_cast<InputProfileCategory>(ProfilePick); ProfilePick = -1; }
        RecordField(Surface, Ctl, Opacity);
        if (ProfileOpen) ControlKit::OutlineRounded(Surface, Ctl, Faded(Accent, Opacity), FieldRadius);   // focus:border-[#e254eb]
        ControlKit::TextLeading(Surface, Ctl, FieldPadX, Faded(InkLabel, Opacity), QueryProfileName(Draft.Profile), LabelSize);
        ControlKit::GlyphCentred(Surface, Spanning(Ctl.MaximumX - 12.0f - 16.0f, Ctl.MinimumY + (FieldH - 16.0f) * 0.5f, 16.0f, 16.0f), 16.0f, Faded(InkMuted, Opacity), ControlCentreIconCategory::ChevronDown);
        if (ControlKit::Over(Ctl, Local) && Local.Released) ProfileOpen = true;
        ProfileExtent = Ctl;
        Y += FieldH + RowGap;

        Ctl = LabelRow("Mouse Sensitivity", true);
        // flex-1 range inside a field + w-16 text box showing "NN%".
        const float BoxW = 64.0f;
        const PlaneExtent Track = Spanning(Ctl.MinimumX, Ctl.MinimumY, Ctl.Width() - BoxW - 8.0f, FieldH);
        RecordField(Surface, Track, Opacity);
        SensitivityExtent = Spanning(Track.MinimumX + FieldPadX, Track.MinimumY, Track.Width() - FieldPadX * 2.0f, FieldH);
        {
            float V = Draft.MouseSensitivity;
            // Browser <input type=range accent-[#e254eb]>: thin track, accent fill and thumb.
            const float Cy = Track.MinimumY + FieldH * 0.5f;
            const PlaneExtent Rail = Spanning(SensitivityExtent.MinimumX, Cy - 2.0f, SensitivityExtent.Width(), 4.0f);
            const bool Dragging = DraggingSlider;
            const bool Over = ControlKit::Over(Spanning(SensitivityExtent.MinimumX, Track.MinimumY, SensitivityExtent.Width(), FieldH), Local);
            if (Over && Local.Pressed) DraggingSlider = true;
            if (DraggingSlider && !Local.Down) DraggingSlider = false;
            if (DraggingSlider || (Dragging && Local.Down))
                V = std::clamp((Local.X - Rail.MinimumX) / std::max(Rail.Width(), 1.0f), 0.0f, 1.0f) * 100.0f;
            Draft.MouseSensitivity = std::round(V);
            const float T = Draft.MouseSensitivity / 100.0f;
            Surface.FillRectangle(Rail, Faded(KnobTrack, Opacity), 2.0f);
            Surface.FillRectangle(Spanning(Rail.MinimumX, Rail.MinimumY, Rail.Width() * T, 4.0f), Faded(Accent, Opacity), 2.0f);
            ControlKit::FillCircle(Surface, Rail.MinimumX + Rail.Width() * T, Cy, 7.0f, Faded(Accent, Opacity));
        }
        const PlaneExtent Box = Spanning(Ctl.MaximumX - BoxW, Ctl.MinimumY, BoxW, FieldH);
        RecordField(Surface, Box, Opacity);
        char Pct[8]; std::snprintf(Pct, sizeof(Pct), "%d%%", static_cast<int>(Draft.MouseSensitivity));
        ControlKit::TextCentred(Surface, Box, Faded(InkLabel, Opacity), Pct, LabelSize);
        Y += FieldH;
    }
    Divider();

    // ── Custom Shortcuts ─────────────────────────────────────────────────────────────────────────────────────────────
    {
        const PlanePoint M = Surface.MeasureText("Custom Shortcuts", TitleSize);
        Surface.Text(X, Y + (24.0f - M.Y) * 0.5f, Faded(InkStrong, Opacity), "Custom Shortcuts", TitleSize);
        CustomSwitchExtent = Spanning(X + W - 44.0f, Y, 44.0f, 24.0f);   // w-11 h-6
        if (RecordToggle(Surface, CustomSwitchExtent, Draft.CustomShortcuts, Local, Opacity).Clicked) Draft.CustomShortcuts = !Draft.CustomShortcuts;
        Y += 24.0f + RowGap;

        const char* Labels[4] = { "Select Tool", "Translate Tool", "Rotate Tool", "Frame Selected" };
        const char* Values[4] = { Draft.SelectTool, Draft.TranslateTool, Draft.RotateTool, Draft.FrameSelected };
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const PlaneExtent Ctl = LabelRow(Labels[I], false);
            RecordField(Surface, Ctl, Opacity);
            // readOnly while custom shortcuts are off: text-[#5a5657] cursor-not-allowed. (Key capture is a later step.)
            ControlKit::TextLeading(Surface, Ctl, FieldPadX, Faded(Draft.CustomShortcuts ? InkLabel : InkDim, Opacity), Values[I], LabelSize);
            Y += FieldH + (I < 3u ? RowGap : 0.0f);
        }
    }
    Divider();

    // ── Advanced Controls ────────────────────────────────────────────────────────────────────────────────────────────
    {
        const PlanePoint M = Surface.MeasureText("Advanced Controls", TitleSize);
        Surface.Text(X, Y, Faded(InkStrong, Opacity), "Advanced Controls", TitleSize);
        Surface.Text(X, Y + M.Y + 2.0f, Faded(InkMuted, Opacity), "Enable axis inversion and modifiers", SubSize);   // mt-0.5
        const float BlockH = M.Y + 2.0f + 16.0f;
        AdvancedSwitchExtent = Spanning(X + W - 44.0f, Y + (BlockH - 24.0f) * 0.5f, 44.0f, 24.0f);
        if (RecordToggle(Surface, AdvancedSwitchExtent, Draft.AdvancedControls, Local, Opacity).Clicked) Draft.AdvancedControls = !Draft.AdvancedControls;
        Y += BlockH + RowGap;

        const PlaneExtent Ctl = LabelRow("Invert Y-Axis", false);
        RecordField(Surface, Ctl, Opacity);
        ControlKit::TextLeading(Surface, Ctl, FieldPadX, Faded(InkMuted, Opacity), "Flip vertical mouse look", LabelSize);
        InvertSwitchExtent = Spanning(Ctl.MaximumX - FieldPadX - 40.0f, Ctl.MinimumY + (FieldH - 20.0f) * 0.5f, 40.0f, 20.0f);   // w-10 h-5
        // Notch keeps the inline toggle live regardless of the Advanced switch; reproduced.
        if (RecordToggle(Surface, InvertSwitchExtent, Draft.InvertPitch, Local, Opacity).Clicked) Draft.InvertPitch = !Draft.InvertPitch;
        Y += FieldH;
    }

    // Buttons row (mt-2 pb-6) is drawn by the host below the body extent.
    return Y - (Body.MinimumY - ScrollY);
}

void InputInspector::ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept
{
    if (!ProfileOpen) return;
    // Native <select> popup rendered in the field palette: rows of FieldH, selected row tinted, accent hover ring.
    const uint32_t Count = 3u;
    const PlaneExtent Menu = Spanning(ProfileExtent.MinimumX, ProfileExtent.MaximumY + 6.0f, ProfileExtent.Width(), 8.0f + Count * FieldH);
    Surface.FillRectangle(Menu, Faded(Field, Opacity), FieldRadius);
    ControlKit::OutlineRounded(Surface, Menu, Faded(Border, Opacity), FieldRadius);
    bool Any = false;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + 4.0f, Menu.MinimumY + 4.0f + I * FieldH, Menu.Width() - 8.0f, FieldH);
        const bool Hover = ControlKit::Over(Row, Pointer);
        const bool Selected = static_cast<uint32_t>(Draft.Profile) == I;
        if (Selected || Hover) Surface.FillRectangle(Row, Faded(Selected ? Border : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.04f }, Opacity), 6.0f);
        ControlKit::TextLeading(Surface, Row, FieldPadX - 4.0f, Faded(Hover || Selected ? InkStrong : InkLabel, Opacity), ProfileNames[I], LabelSize);
        if (Hover && Pointer.Released) { ProfilePick = static_cast<int>(I); ProfileOpen = false; return; }
        Any = Any || Hover;
    }
    if (Pointer.Released && !Any && !ProfileExtent.Encloses(Pointer.X, Pointer.Y)) ProfileOpen = false;
}

//========================================================================================================================
//                                                NOTIFICATION INSPECTOR
//========================================================================================================================

NotificationInspector::NotificationInspector() noexcept
    : Applied{}, Draft{}, Revision(0u), DraggingSlider(false), ToggleExtents{}, HoldSliderExtent{}
{
}

ControlHit NotificationInspector::RecordFormToggle(PixelSpace& Surface, const PlaneExtent& Extent, bool On, const ControlPointer& Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = ControlKit::Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    const ControlKitPalette& K = ControlKit::Palette();
    const ColorQuad Off = K.LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.10f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.10f };   // bg-white/10
    Surface.FillRectangle(Extent, Faded(On ? K.Accent : Off, Opacity), 12.0f);                 // w-12 h-6 rounded-full
    const float X = Extent.MinimumX + (On ? 26.0f : 2.0f);                                       // animate x 2 → 26
    ControlKit::FillCircle(Surface, X + 10.0f, Extent.MinimumY + 2.0f + 10.0f, 10.0f, Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }, Opacity));   // w-5 h-5 top-0.5 bg-white
    return Hit;
}

float NotificationInspector::ConstructNotificationLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    // GenericSettingsModal: px-8 lg:px-12 py-8, max-w-3xl column of FormFieldRow (grid 160px | 1fr, py-4, border-b last:0).
    const ControlKitPalette& K = ControlKit::Palette();
    const float X = Body.MinimumX + 16.0f;
    const float W = std::min(Body.Width() - 32.0f, 768.0f);
    float Y = Body.MinimumY - ScrollY;

    auto Row = [&](uint32_t Index, const char* Label, bool& Flag, bool Last)
    {
        const PlanePoint M = Surface.MeasureText(Label, FormLabelSize);
        Surface.Text(X, Y + (FormRowH - M.Y) * 0.5f, Faded(K.Text, Opacity), Label, FormLabelSize);   // text-sm tracking-tight colors.text
        ToggleExtents[Index] = Spanning(X + W - 48.0f, Y + FormRowPadY, 48.0f, 24.0f);                // flex justify-end w-full
        if (RecordFormToggle(Surface, ToggleExtents[Index], Flag, Pointer, Opacity).Clicked) Flag = !Flag;
        Y += FormRowH;
        if (!Last) Surface.FillRectangle(Spanning(X, Y - 0.5f, W, 1.0f), Faded(K.Divider, Opacity));  // border-b colors.divider
    };

    Row(0u, "Show FPS Overlay", Draft.ShowFrameRateOverlay, false);
    Row(1u, "Show RAM Usage",   Draft.ShowMemoryUsage,      false);
    Row(2u, "Scene Metadata",   Draft.ShowSceneMetadata,    true);

    // <h3 class="mt-8 mb-4 text-sm font-medium">Alerts</h3>
    Y += AlertsHeadTop;
    Surface.Text(X, Y, Faded(K.Text, Opacity), "Alerts", FormLabelSize);
    Y += Surface.MeasureText("Alerts", FormLabelSize).Y + AlertsHeadGap;

    Row(3u, "Baking Complete",  Draft.BakingComplete, false);
    Row(4u, "Render Finished",  Draft.RenderFinished, false);
    Row(5u, "Autosave Errors",  Draft.AutosaveErrors, false);
    Row(6u, "Frame-rate Drops", Draft.FrameRateDrops, true);   // engine addition (flagged)

    // Engine addition (flagged): toast dwell. Same FormFieldRow grid, thin kit slider + "N.N s" readout.
    {
        Y += AlertsHeadTop;
        Surface.Text(X, Y, Faded(K.Text, Opacity), "Toasts", FormLabelSize);
        Y += Surface.MeasureText("Toasts", FormLabelSize).Y + AlertsHeadGap;
        const PlanePoint M = Surface.MeasureText("Hold Duration", FormLabelSize);
        Surface.Text(X, Y + (FormRowH - M.Y) * 0.5f, Faded(K.Text, Opacity), "Hold Duration", FormLabelSize);
        char Readout[16]; std::snprintf(Readout, sizeof(Readout), "%.1f s", static_cast<double>(Draft.HoldSeconds));
        const PlanePoint RM = Surface.MeasureText(Readout, 12.0f);
        Surface.Text(X + W - RM.X, Y + (FormRowH - RM.Y) * 0.5f, Faded(K.TextDim, Opacity), Readout, 12.0f);
        HoldSliderExtent = Spanning(X + FormLabelCol, Y + (FormRowH - ControlKitTokens::ControlHeight) * 0.5f, W - FormLabelCol - RM.X - 24.0f, ControlKitTokens::ControlHeight);
        float V = Draft.HoldSeconds;
        const ControlHit Hit = ControlKit::Slider(Surface, HoldSliderExtent, 1.0f, 10.0f, V, DraggingSlider, Pointer, V, true, false, Opacity);
        if (Hit.Pressed) DraggingSlider = true;
        if (DraggingSlider && !Pointer.Down) DraggingSlider = false;
        if (DraggingSlider) Draft.HoldSeconds = std::round(V * 2.0f) * 0.5f;
        Y += FormRowH;
    }

    return Y - (Body.MinimumY - ScrollY);
}

} // namespace Frontier
