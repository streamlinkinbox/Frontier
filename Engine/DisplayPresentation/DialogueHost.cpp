//========================================================================================================================
// 🧩 DialogueHost — see DialogueHost.h
//========================================================================================================================
#include "DialogueHost.h"

#include <algorithm>

namespace Frontier {

namespace {

constexpr DialoguePresetStructure Presets[static_cast<size_t>(DialoguePresetCategory::Count)] =
{
    { DialogueToneCategory::Info,    ControlCentreIconCategory::CircleInfo,    "",                  "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
    { DialogueToneCategory::Danger,  ControlCentreIconCategory::OctagonAlert,  "Discard changes?",  "This cannot be undone",             "Your unsaved edits on this page will be thrown away and the applied values restored.", "Cancel", nullptr, "Discard"      },
    { DialogueToneCategory::Success, ControlCentreIconCategory::CircleCheck,   "Apply changes?",    "Settings take effect immediately",  "The pending values will be committed and pushed to the renderer.",          "Cancel", nullptr,   "Apply"        },
    { DialogueToneCategory::Warning, ControlCentreIconCategory::TriangleAlert, "Unsaved changes",   "You are leaving this page",         "Apply your edits, keep editing, or discard them.",                          "Keep editing", "Discard", "Apply"  },
    { DialogueToneCategory::Warning, ControlCentreIconCategory::TriangleAlert, "Reset to defaults?", "All fields on this page",          "Every value will return to its factory default. You can still discard before applying.", "Cancel", nullptr, "Reset" },
    { DialogueToneCategory::Info,    ControlCentreIconCategory::CircleInfo,    "Information",       "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
    { DialogueToneCategory::Success, ControlCentreIconCategory::CircleCheck,   "Success",           "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
    { DialogueToneCategory::Warning, ControlCentreIconCategory::TriangleAlert, "Warning",           "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
    { DialogueToneCategory::Caution, ControlCentreIconCategory::OctagonAlert,  "Caution",           "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
    { DialogueToneCategory::Danger,  ControlCentreIconCategory::OctagonAlert,  "Error",             "",                                  "",                                                                          nullptr,  nullptr,   "OK"           },
};

} // namespace

DialogueHost::DialogueHost() noexcept
    : Active(DialoguePresetCategory::None)
    , Verdict(DialogueVerdictCategory::Pending)
    , Motion{}
    , EnterChannel(0u)
    , LastCancel{}, LastSecondary{}, LastPrimary{}
    , PressedInside(false)
    , PressedOnScrim(false)
{
    EnterChannel = Motion.Register(0.0);
    SpringChannel& Enter = Motion.Spring(EnterChannel);
    Enter.Stiffness = 986.0;   // ≈ (2π / 0.2)² : 200 ms, critically damped
    Enter.Damping   = 62.8;
}

const DialoguePresetStructure& DialogueHost::QueryPreset(DialoguePresetCategory Preset) noexcept
{
    return Presets[static_cast<size_t>(Preset) < static_cast<size_t>(DialoguePresetCategory::Count) ? static_cast<size_t>(Preset) : 0u];
}

ColorQuad DialogueHost::QueryToneColour(DialogueToneCategory Tone) noexcept
{
    switch (Tone)
    {
        case DialogueToneCategory::Danger:  return ControlKit::Palette().Danger;
        case DialogueToneCategory::Success: return ControlKit::Palette().Ok;
        case DialogueToneCategory::Info:    return ControlKit::Palette().Info;
        case DialogueToneCategory::Warning: return ControlKit::Palette().Warning;
        case DialogueToneCategory::Caution: return ControlKit::Palette().Caution;
    }
    return ControlKit::Palette().Accent;
}

void DialogueHost::Open(DialoguePresetCategory Preset) noexcept
{
    Active  = Preset;
    Verdict = DialogueVerdictCategory::Pending;
    PressedInside = false;
    PressedOnScrim = false;
    Motion.Spring(EnterChannel).Place(0.0);
    Motion.Spring(EnterChannel).Depart(1.0);
}

void DialogueHost::Close() noexcept
{
    Active = DialoguePresetCategory::None;
    Motion.Spring(EnterChannel).Depart(0.0);
}

DialogueVerdictCategory DialogueHost::TakeVerdict() noexcept
{
    const DialogueVerdictCategory V = Verdict;
    Verdict = DialogueVerdictCategory::Pending;
    return V;
}

void DialogueHost::Advance(float DeltaSeconds) noexcept
{
    if (DeltaSeconds > 0.0f) Motion.Advance(static_cast<double>(DeltaSeconds));
}

PlaneExtent DialogueHost::QueryButtonExtent(DialogueVerdictCategory Which) const noexcept
{
    switch (Which)
    {
        case DialogueVerdictCategory::Cancel:    return LastCancel;
        case DialogueVerdictCategory::Secondary: return LastSecondary;
        case DialogueVerdictCategory::Primary:   return LastPrimary;
        default: return {};
    }
}

void DialogueHost::ConstructDialogueLayout(PixelSpace& Surface, const PlaneExtent& Host, const ControlPointer& Pointer) noexcept
{
    const float Enter = static_cast<float>(std::clamp(Motion.Spring(EnterChannel).Current, 0.0, 1.0));
    if (!IsOpen() && Enter <= 0.001f) return;

    const DialoguePresetStructure& P = QueryPreset(Active == DialoguePresetCategory::None ? DialoguePresetCategory::Info : Active);
    const ColorQuad Tone = QueryToneColour(P.Tone);
    const ControlPointer Live = IsOpen() && Enter > 0.95f ? Pointer : ControlPointer{ Pointer.X, Pointer.Y, false, false, false, false };

    // Scrim over the host card (black/40).
    Surface.FillRectangle(Host, ColorQuad{ 0.0f, 0.0f, 0.0f, ScrimAlpha * Enter }, 32.0f);

    // Measure body height (single line wrap-free; long bodies wrap manually at ~52 chars).
    const float BodySize = 13.5f;
    const float BodyLine = 20.0f;
    // naive wrap
    char Lines[6][96]; int LineCount = 0;
    {
        const char* S = P.Body; const float MaxW = Width - BodyPadX * 2.0f;
        char Cur[96] = {}; int CurLen = 0;
        const char* WordStart = S;
        auto Flush = [&]{ if (LineCount < 6) { std::copy(Cur, Cur + CurLen + 1, Lines[LineCount++]); } CurLen = 0; Cur[0] = 0; };
        for (const char* C = S;; ++C)
        {
            if (*C == ' ' || *C == 0)
            {
                const int WordLen = static_cast<int>(C - WordStart);
                char Trial[96] = {};
                int TrialLen = CurLen;
                std::copy(Cur, Cur + CurLen, Trial);
                if (TrialLen > 0) Trial[TrialLen++] = ' ';
                std::copy(WordStart, WordStart + std::min(WordLen, 90 - TrialLen), Trial + TrialLen); TrialLen += std::min(WordLen, 90 - TrialLen); Trial[TrialLen] = 0;
                if (Surface.MeasureText(Trial, BodySize).X > MaxW && CurLen > 0) { Flush(); std::copy(WordStart, WordStart + WordLen, Cur); CurLen = WordLen; Cur[CurLen] = 0; }
                else { std::copy(Trial, Trial + TrialLen + 1, Cur); CurLen = TrialLen; }
                WordStart = C + 1;
                if (*C == 0) break;
            }
        }
        if (CurLen > 0 || LineCount == 0) Flush();
    }

    const bool HasSub = P.Subtitle && *P.Subtitle;
    const float HeaderH = HeaderPadY * 2.0f + std::max(HeaderIconDisc, 20.0f + (HasSub ? 15.0f : 0.0f));
    const float BodyH   = (P.Body && *P.Body) ? BodyPadY * 2.0f + LineCount * BodyLine : 0.0f;
    const float FooterH = FooterPadY * 2.0f + ControlKitTokens::ControlHeight;
    const float Height  = HeaderH + BodyH + FooterH;

    const float Cx = (Host.MinimumX + Host.MaximumX) * 0.5f, Cy = (Host.MinimumY + Host.MaximumY) * 0.5f;
    const PlaneExtent Box = Spanning(Cx - Width * 0.5f, Cy - Height * 0.5f, Width, Height);

    const uint32_t Mark = Surface.BeginGroup();

    Surface.FillRectangle(Box, ControlKit::Palette().Panel, ControlKitTokens::RadiusPanel);
    ControlKit::OutlineRounded(Surface, Box, ControlKit::Palette().Stroke, ControlKitTokens::RadiusPanel);

    // Header: rgba(255,255,255,.02) band, icon disc tinted by tone, title 14 semibold, sub 11.5 faint.
    Surface.FillRectangle(Spanning(Box.MinimumX, Box.MinimumY, Width, HeaderH), ColorQuad{ 1.0f, 1.0f, 1.0f, 0.02f }, ControlKitTokens::RadiusPanel);
    ControlKit::Divider(Surface, Box.MinimumX, Box.MinimumY + HeaderH, Width);
    const PlaneExtent Disc = Spanning(Box.MinimumX + HeaderPadX, Box.MinimumY + (HeaderH - HeaderIconDisc) * 0.5f, HeaderIconDisc, HeaderIconDisc);
    ColorQuad DiscFill = Tone; DiscFill.Alpha = 0.15f;
    Surface.FillRectangle(Disc, DiscFill, HeaderIconRadius);
    ColorQuad DiscEdge = Tone; DiscEdge.Alpha = 0.35f;
    ControlKit::OutlineRounded(Surface, Disc, DiscEdge, HeaderIconRadius);
    ControlKit::GlyphCentred(Surface, Disc, 16.0f, Tone, P.Icon);
    const float TextX = Disc.MaximumX + 10.0f;
    float TextY = Box.MinimumY + HeaderPadY;
    if (!HasSub) TextY = Disc.MinimumY + (HeaderIconDisc - 20.0f) * 0.5f;
    Surface.Text(TextX, TextY + 2.0f, ControlKit::Palette().Text, P.Title, 14.0f);
    if (HasSub) Surface.Text(TextX, TextY + 20.0f + 1.0f, ControlKit::Palette().TextFaint, P.Subtitle, 11.5f);

    // Body
    if (BodyH > 0.0f)
        for (int I = 0; I < LineCount; ++I)
            Surface.Text(Box.MinimumX + BodyPadX, Box.MinimumY + HeaderH + BodyPadY + I * BodyLine + 2.0f, ControlKit::Palette().TextDim, Lines[I], BodySize);

    // Footer: band + buttons right-aligned (Cancel ghost · Secondary · Primary tinted).
    const float FooterTop = Box.MaximumY - FooterH;
    Surface.FillRectangleBottomRounded(Spanning(Box.MinimumX, FooterTop, Width, FooterH), ColorQuad{ 1.0f, 1.0f, 1.0f, 0.02f }, ControlKitTokens::RadiusPanel);
    ControlKit::Divider(Surface, Box.MinimumX, FooterTop, Width);

    ButtonStructure Primary{};   Primary.Label = P.PrimaryLabel;     Primary.Tone = ButtonToneCategory::Tinted; Primary.Tint = Tone;
    if (P.Tone == DialogueToneCategory::Caution || P.Tone == DialogueToneCategory::Warning) { /* dark ink reads better on amber */ }
    ButtonStructure Secondary{}; Secondary.Label = P.SecondaryLabel ? P.SecondaryLabel : ""; Secondary.Tone = ButtonToneCategory::Danger;
    ButtonStructure Cancel{};    Cancel.Label = P.CancelLabel ? P.CancelLabel : "";           Cancel.Tone = ButtonToneCategory::Ghost;

    float X = Box.MaximumX - FooterPadX;
    const float BY = FooterTop + FooterPadY;
    LastPrimary = LastSecondary = LastCancel = PlaneExtent{};

    const float PW = ControlKit::ButtonWidth(Surface, Primary);
    LastPrimary = Spanning(X - PW, BY, PW, ControlKitTokens::ControlHeight); X -= PW + FooterGap;
    if (P.SecondaryLabel) { const float SW = ControlKit::ButtonWidth(Surface, Secondary); LastSecondary = Spanning(X - SW, BY, SW, ControlKitTokens::ControlHeight); X -= SW + FooterGap; }
    if (P.CancelLabel)    { const float CW = ControlKit::ButtonWidth(Surface, Cancel);    LastCancel    = Spanning(X - CW, BY, CW, ControlKitTokens::ControlHeight); }

    if (P.CancelLabel    && ControlKit::PillButton(Surface, LastCancel,    Cancel,    Live).Clicked) { Verdict = DialogueVerdictCategory::Cancel;    Close(); }
    if (P.SecondaryLabel && ControlKit::PillButton(Surface, LastSecondary, Secondary, Live).Clicked) { Verdict = DialogueVerdictCategory::Secondary; Close(); }
    if (ControlKit::PillButton(Surface, LastPrimary, Primary, Live).Clicked)                          { Verdict = DialogueVerdictCategory::Primary;   Close(); }

    // Scrim tap (outside the box) cancels, if the preset has a cancel path.
    if (Live.Pressed) { PressedInside = Box.Encloses(Live.X, Live.Y); PressedOnScrim = !PressedInside && Host.Encloses(Live.X, Live.Y); }
    if (Live.Released && PressedOnScrim && !Box.Encloses(Live.X, Live.Y) && IsOpen())
    {
        Verdict = DialogueVerdictCategory::Cancel;
        Close();
    }

    const float Scale = 0.95f + 0.05f * Enter;
    Surface.EndGroup(Mark, 0.0f, 0.0f, Scale, Cx, Cy, Enter);
}

} // namespace Frontier
