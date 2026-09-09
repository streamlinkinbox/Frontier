//============================================================================================================================================
//                                                 CONTROLCENTREPANEL.CPP
//============================================================================================================================================
// 🧩 The dashboard port. One fullscreen window carries the sheet, the scrim, the card, the toast, and the handle;
//    only the handle shows while shut, and only the handle takes the pointer then, so the columns stay live below.
//    The carry honours the host's rules verbatim: tap travel and duration, single axis resolution, elastic bounds,
//    and the Notch release classification with the smoothed release velocity.

#include "ControlCentrePanel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui_internal.h>

#include "ControlPanel.h"

namespace Frontier {

namespace {

constexpr ImU32 kSheet      = IM_COL32(0x14, 0x14, 0x15, 255);   // reference Card
constexpr ImU32 kTileIdle   = IM_COL32(0x1C, 0x1C, 0x1E, 255);   // reference CardSub
constexpr ImU32 kTileHover  = IM_COL32(0x2C, 0x2C, 0x2E, 255);
constexpr ImU32 kAccent     = IM_COL32(0x3B, 0x82, 0xF6, 255);   // reference Accent
constexpr ImU32 kAccentHot  = IM_COL32(0x60, 0xA5, 0xFA, 255);
constexpr ImU32 kPillFill   = IM_COL32(0x63, 0x66, 0xF1, 230);   // #6366F1 at 90 %
constexpr ImU32 kTrack      = IM_COL32(0x00, 0x00, 0x00, 128);   // black halves
constexpr ImU32 kWhite50    = IM_COL32(255, 255, 255, 128);
constexpr ImU32 kWhite70    = IM_COL32(255, 255, 255, 178);
constexpr ImU32 kWhite      = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kStroke     = IM_COL32(255, 255, 255, 26);

constexpr float kDeg = 0.0174533f;

float Elastic(float Along, float Lo, float Hi, float Give) noexcept
{
    if (Along < Lo)
    {
        return Lo + (Along - Lo) * Give;
    }
    if (Along > Hi)
    {
        return Hi + (Along - Hi) * Give;
    }
    return Along;
}

float Clamp01(float V) noexcept
{
    if (V < 0.0f)
    {
        return 0.0f;
    }
    if (V > 1.0f)
    {
        return 1.0f;
    }
    return V;
}

} // namespace

void ControlCentrePanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

void ControlCentrePanel::AssignOpen(bool* Open) noexcept
{
    Open_ = Open;
}

void ControlCentrePanel::AssignProjectName(const char* Name) noexcept
{
    if (Name == nullptr || Name[0] == '\0')
    {
        return;
    }
    std::strncpy(ProjectName_, Name, sizeof(ProjectName_) - 1u);
    ProjectName_[sizeof(ProjectName_) - 1u] = '\0';
}

ImU32 ControlCentrePanel::FadeTint(ImU32 Tint, float Opacity) noexcept
{
    const float Clamped = Clamp01(Opacity);
    const uint32_t Alpha = static_cast<uint32_t>(static_cast<float>(Tint >> 24) * Clamped + 0.5f);
    return (Tint & 0x00FFFFFFu) | (Alpha << 24);
}

void ControlCentrePanel::Record() noexcept
{
    ImGuiViewport* Main = ImGui::GetMainViewport();
    const float Width  = Main->Size.x;
    const float Height = Main->Size.y;
    const float OpenTravel = Height - kNotchH;
    const float Bound = (Width - kNotchW) * 0.5f > 0.0f ? (Width - kNotchW) * 0.5f : 0.0f;

    if (!Grabbed_ && Open_ != nullptr)
    {
        Target_ = *Open_ ? OpenTravel : 0.0f;
    }
    const float Delta = ImGui::GetIO().DeltaTime;
    if (!Grabbed_)
    {
        if (Delta <= 0.0f)
        {
            ShadeY_ = Target_;
            NotchX_ = NotchAim_;
        }
        else
        {
            const float Ease = Delta * 10.0f < 1.0f ? Delta * 10.0f : 1.0f;
            ShadeY_ += (Target_ - ShadeY_) * Ease;
            if (ShadeY_ > Target_ - 0.5f && ShadeY_ < Target_ + 0.5f)
            {
                ShadeY_ = Target_;
            }
            const float Slide = Delta * 15.0f < 1.0f ? Delta * 15.0f : 1.0f;
            NotchX_ += (NotchAim_ - NotchX_) * Slide;
            if (NotchX_ > NotchAim_ - 0.5f && NotchX_ < NotchAim_ + 0.5f)
            {
                NotchX_ = NotchAim_;
            }
        }
    }

    ImGui::SetNextWindowPos(Main->Pos);
    // The window keeps the shade's footprint: the notch strip while shut, the viewport while open. A
    //    full-viewport window would sit above the dock columns and swallow their hovers and clicks.
    ImGui::SetNextWindowSize(ImVec2(Width, ShadeY_ > 0.5f ? Height : kNotchH));
    constexpr ImGuiWindowFlags kShadeFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    if (!ImGui::Begin("##controlcentre", nullptr, kShadeFlags))
    {
        ImGui::End();
        return;
    }
    // The shade always draws above the dock columns: the host records it last, and the bring-forward
    //    holds it there when a docked window takes focus.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    DrawScrim(ShadeY_, OpenTravel, Width, Height);
    DrawCard(ShadeY_, Width, Clamp01((ShadeY_ - 100.0f) / (Height * 0.5f - 100.0f)));
    DrawToast(ShadeY_, Width);
    DrawHandle(NotchX_, ShadeY_, Bound);

    ImGui::End();
}

void ControlCentrePanel::DrawScrim(float ShadeY, float OpenTravel, float Width, float Height) noexcept
{
    if (ShadeY <= 0.5f)
    {
        return;
    }
    const ImVec2 Origin = ImGui::GetMainViewport()->Pos;
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + ShadeY), kSheet);
    const float Top = Origin.y + ShadeY + kNotchH;
    const uint32_t Alpha = static_cast<uint32_t>(255.0f * kScrimMax * (ShadeY / OpenTravel) + 0.5f);
    if (Top < Origin.y + Height)
    {
        Draw->AddRectFilled(ImVec2(Origin.x, Top), ImVec2(Origin.x + Width, Origin.y + Height),
            IM_COL32(0, 0, 0, Alpha));
    }
    // A press on the scrim shuts the open shade; the handle grab owns the pointer mid-carry, so the two
    //    never meet.
    if (Target_ >= OpenTravel - 0.5f && ShadeY > Height * 0.5f && !Grabbed_ && Top < Origin.y + Height)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Top));
        ImGui::InvisibleButton("##ccscrim", ImVec2(Width, Origin.y + Height - Top));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        {
            Target_ = 0.0f;
            if (Open_ != nullptr)
            {
                *Open_ = false;
            }
        }
    }
}

void ControlCentrePanel::DrawHandle(float NotchX, float ShadeY, float Bound) noexcept
{
    ImGuiViewport* Main = ImGui::GetMainViewport();
    const float Width  = Main->Size.x;
    const float Height = Main->Size.y;
    const float OpenTravel = Height - kNotchH;
    const float Hx = Main->Pos.x + Width * 0.5f - kNotchW * 0.5f + NotchX;
    const float Hy = Main->Pos.y + ShadeY;

    // The handle outline, tessellated from the host's SVG path once per tick (36 points, trivially cheap).
    ImVec2 Outline[40] = {};
    Outline[0] = ImVec2(Hx, Hy);
    int Points = 1;
    const auto Cubic = [&](float X0, float Y0, float X1, float Y1, float X2, float Y2, float X3, float Y3)
    {
        for (int S = 1; S <= 8; ++S)
        {
            const float T = static_cast<float>(S) / 8.0f;
            const float U = 1.0f - T;
            Outline[Points++] = ImVec2(
                Hx + U * U * U * X0 + 3.0f * U * U * T * X1 + 3.0f * U * T * T * X2 + T * T * T * X3,
                Hy + U * U * U * Y0 + 3.0f * U * U * T * Y1 + 3.0f * U * T * T * Y2 + T * T * T * Y3);
        }
    };
    Cubic(0.0f, 0.0f, 15.0f, 0.0f, 20.0f, 6.0f, 25.0f, 15.0f);
    Outline[Points++] = ImVec2(Hx + 35.0f, Hy + 28.0f);
    Cubic(35.0f, 28.0f, 40.0f, 34.0f, 45.0f, 36.0f, 52.0f, 36.0f);
    Outline[Points++] = ImVec2(Hx + 348.0f, Hy + 36.0f);
    Cubic(348.0f, 36.0f, 355.0f, 36.0f, 360.0f, 34.0f, 365.0f, 28.0f);
    Outline[Points++] = ImVec2(Hx + 375.0f, Hy + 15.0f);
    Cubic(375.0f, 15.0f, 380.0f, 6.0f, 385.0f, 0.0f, 400.0f, 0.0f);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddConvexPolyFilled(Outline, Points, kTileIdle);
    Draw->AddPolyline(Outline, Points, kStroke, ImDrawFlags_Closed, 1.2f);

    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 NameGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, ProjectName_);
    Draw->AddText(ImVec2(Hx + (kNotchW - NameGlyph.x) * 0.5f, Hy + (kNotchH - NameGlyph.y) * 0.5f),
        kWhite70, ProjectName_);
    ImGui::PopFont();

    NotchCX_ = Hx + kNotchW * 0.5f;
    NotchCY_ = Hy + kNotchH * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(Hx, Hy));
    ImGui::InvisibleButton("##notch", ImVec2(kNotchW, kNotchH));
    float Delta = ImGui::GetIO().DeltaTime;
    if (Delta <= 0.0f)
    {
        Delta = 1.0f / 60.0f;
    }
    const ImVec2 Mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated())
    {
        Grabbed_   = true;
        GrabX_     = Mouse.x;
        GrabY_     = Mouse.y;
        GrabShade_ = ShadeY_;
        GrabNotch_ = NotchX_;
        Travelled_ = 0.0f;
        Contact_   = 0.0f;
        AxisSet_   = false;
        YCarry_    = true;
        VelY_      = 0.0f;
        PrevY_     = Mouse.y;
        Target_    = ShadeY_;
    }
    if (Grabbed_ && ImGui::IsItemActive())
    {
        const float Dx = Mouse.x - GrabX_;
        const float Dy = Mouse.y - GrabY_;
        const float AbsX = Dx < 0.0f ? -Dx : Dx;
        const float AbsY = Dy < 0.0f ? -Dy : Dy;
        if (AbsX > Travelled_)
        {
            Travelled_ = AbsX;
        }
        if (AbsY > Travelled_)
        {
            Travelled_ = AbsY;
        }
        Contact_ += Delta;
        VelY_ = kRateKeep * VelY_ + (1.0f - kRateKeep) * ((Mouse.y - PrevY_) / Delta);
        PrevY_ = Mouse.y;
        if (!AxisSet_ && Travelled_ > kTapTravel)
        {
            AxisSet_ = true;
            YCarry_  = AbsY >= AbsX;
        }
        if (AxisSet_ && YCarry_)
        {
            Target_ = Elastic(GrabShade_ + Dy, 0.0f, OpenTravel, kElastic);
            ShadeY_ = Target_;
            if (Open_ != nullptr)
            {
                *Open_ = Target_ > Height * 0.5f;
            }
        }
        if (AxisSet_ && !YCarry_)
        {
            NotchX_ = Elastic(GrabNotch_ + Dx, -Bound, Bound, kElastic);
        }
    }
    if (Grabbed_ && ImGui::IsItemDeactivated())
    {
        Grabbed_ = false;
        if (Travelled_ <= kTapTravel && Contact_ <= kTapDuration)
        {
            Target_ = (Target_ > Height * 0.5f) ? 0.0f : OpenTravel;
        }
        else if (AxisSet_ && YCarry_)
        {
            const bool Past = Target_ > Height * 0.5f;
            const bool StaysOpen = Past
                ? !((VelY_ < -kSnapRate) || (GrabShade_ - Target_ > kSnapOffset))
                : ((VelY_ > kSnapRate) || (Target_ - GrabShade_ > kSnapOffset));
            Target_ = StaysOpen ? OpenTravel : 0.0f;
        }
        else if (AxisSet_ && !YCarry_)
        {
            NotchAim_ = NotchX_ < -Bound ? -Bound : (NotchX_ > Bound ? Bound : NotchX_);
        }
        if (Open_ != nullptr)
        {
            *Open_ = (Target_ == OpenTravel);
        }
    }
}

void ControlCentrePanel::DrawCard(float ShadeY, float Width, float Opacity) noexcept
{
    if (ShadeY <= 1.0f)
    {
        return;
    }
    const bool Live = ShadeY > 100.0f;
    const ImVec2 Origin = ImGui::GetMainViewport()->Pos;
    const float CardX = Origin.x + Width * 0.5f - kCardW * 0.5f;
    const float CardY = Origin.y + ShadeY * 0.5f - kCardH * 0.5f;

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();

    // Header: the title left, wifi and the gear right.
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(CardX + 8.0f, CardY + 50.0f), FadeTint(kWhite50, Opacity), "Control Center");
    ImGui::PopFont();
    const float HeadCY  = CardY + 50.0f + 8.0f;
    const float GearCX  = CardX + kCardW - 8.0f - 8.0f;
    const float WifiCX  = GearCX - 32.0f;
    DrawWifi(Draw, WifiCX, HeadCY, 16.0f, FadeTint(kWhite70, Opacity), 1.5f);
    DrawGear(Draw, GearCX, HeadCY, 16.0f, FadeTint(kWhite70, Opacity), 1.5f);
    if (Live)
    {
        ImGui::SetCursorScreenPos(ImVec2(GearCX - 14.0f, HeadCY - 14.0f));
        ImGui::InvisibleButton("##ccgear", ImVec2(28.0f, 28.0f));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && NotifOn_)
        {
            std::snprintf(ToastTitle_, sizeof(ToastTitle_), "Settings hub");
            std::snprintf(ToastSub_, sizeof(ToastSub_), "The full hub opens in the game");
            ToastRaised_ = ImGui::GetTime();
            ToastUntil_  = ToastRaised_ + 3.2;
        }
    }
    // The 4 × 2 disc rows; slots past Quality draw nothing.
    const float GridY = CardY + 50.0f + 16.0f + kStackGap;
    for (uint32_t Slot = 0u; Slot < static_cast<uint32_t>(ControlCentreTile::Count); ++Slot)
    {
        const float CX = CardX + 58.0f + 32.0f + static_cast<float>(Slot % 4u) * (kDisc + kColGap);
        const float CY = GridY + static_cast<float>(Slot / 4u) * (91.0f + kRowGap) + 32.0f;
        DrawTile(Slot, CX, CY, Opacity, Live);
        if (Slot == static_cast<uint32_t>(ControlCentreTile::GlobalIllumination))
        {
            GiCX_ = CX;
            GiCY_ = CY;
        }
    }

    DrawPill(CardX + 24.0f, GridY + 91.0f + kRowGap + 91.0f + kStackGap, kCardW - 48.0f, Opacity, Live);
}

void ControlCentrePanel::DrawTile(uint32_t Slot, float DiscCX, float DiscCY, float Opacity, bool Live) noexcept
{
    bool On = true;
    const char* Label = "";
    switch (static_cast<ControlCentreTile>(Slot))
    {
    case ControlCentreTile::GlobalIllumination: On = GiOn_;    Label = "Global Illumination"; break;
    case ControlCentreTile::AntiAliasing:       On = AaOn_;    Label = "Anti-Aliasing";       break;
    case ControlCentreTile::FrameRateOverlay:   On = FpsOn_;   Label = "FPS Overlay";         break;
    case ControlCentreTile::Notifications:      On = NotifOn_; Label = "Notifications";       break;
    case ControlCentreTile::Quality:
        On = true;
        switch (Quality_)
        {
        case ControlCentreQuality::Minimal:   Label = "Minimal";   break;
        case ControlCentreQuality::Economy:   Label = "Economy";   break;
        case ControlCentreQuality::Standard:  Label = "Standard";  break;
        case ControlCentreQuality::Ultra:     Label = "Ultra";     break;
        case ControlCentreQuality::Reference: Label = "Reference"; break;
        }
        break;
    case ControlCentreTile::Count: break;
    }

    bool Hot = false;
    if (Live)
    {
        ImGui::SetCursorScreenPos(ImVec2(DiscCX - 32.0f, DiscCY - 32.0f));
        char TileId[12] = {};
        std::snprintf(TileId, sizeof(TileId), "##cct%u", Slot);
        ImGui::InvisibleButton(TileId, ImVec2(kDisc, kDisc));
        Hot = ImGui::IsItemHovered();
        if (Hot && ImGui::IsMouseClicked(0))
        {
            switch (static_cast<ControlCentreTile>(Slot))
            {
            case ControlCentreTile::GlobalIllumination: GiOn_ = !GiOn_; break;
            case ControlCentreTile::AntiAliasing:       AaOn_ = !AaOn_; break;
            case ControlCentreTile::FrameRateOverlay:   FpsOn_ = !FpsOn_; break;
            case ControlCentreTile::Notifications:      NotifOn_ = !NotifOn_; break;
            case ControlCentreTile::Quality:
                Quality_ = static_cast<ControlCentreQuality>(
                    (static_cast<uint32_t>(Quality_) + 1u) % 5u);
                break;
            case ControlCentreTile::Count: break;
            }
            ++Revision_;
            RaiseToast();
        }
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImU32 DiscTint = On ? (Hot ? kAccentHot : kAccent) : (Hot ? kTileHover : kTileIdle);
    Draw->AddCircleFilled(ImVec2(DiscCX, DiscCY), 32.0f, FadeTint(DiscTint, Opacity));
    const ImU32 Ink = FadeTint(kWhite, Opacity);
    const float Thick = On ? 2.0f : 1.5f;
    switch (static_cast<ControlCentreTile>(Slot))
    {
    case ControlCentreTile::GlobalIllumination: DrawSun(Draw, DiscCX, DiscCY, kGlyph, Ink, Thick); break;
    case ControlCentreTile::AntiAliasing:       DrawSparkles(Draw, DiscCX, DiscCY, kGlyph, Ink); break;
    case ControlCentreTile::FrameRateOverlay:   DrawGauge(Draw, DiscCX, DiscCY, kGlyph, Ink, Thick); break;
    case ControlCentreTile::Notifications:      DrawBell(Draw, DiscCX, DiscCY, kGlyph, Ink, Thick); break;
    case ControlCentreTile::Quality:            DrawSliders(Draw, DiscCX, DiscCY, kGlyph, Ink, Thick); break;
    case ControlCentreTile::Count: break;
    }

    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Label);
    Draw->AddText(ImVec2(DiscCX - LabelGlyph.x * 0.5f, DiscCY + 32.0f + kLabelGap),
        FadeTint(kWhite70, Opacity), Label);
    ImGui::PopFont();
}

void ControlCentrePanel::DrawPill(float PillX, float PillY, float PillW, float Opacity, bool Live) noexcept
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(ImVec2(PillX, PillY), ImVec2(PillX + PillW, PillY + 64.0f),
        FadeTint(kTileIdle, Opacity), 32.0f);
    DrawVideo(Draw, PillX + 8.0f + 24.0f, PillY + 32.0f, 20.0f, FadeTint(kWhite70, Opacity), 1.5f);

    const float TrackX0 = PillX + 72.0f;
    const float TrackX1 = PillX + PillW - 72.0f;
    const float TrackCY = PillY + 32.0f;
    Draw->AddRectFilled(ImVec2(TrackX0, TrackCY - 4.0f), ImVec2(TrackX1, TrackCY + 4.0f),
        FadeTint(kTrack, Opacity), 4.0f);
    const float U = (RenderScale_ - kScaleMin) / (1.0f - kScaleMin);
    Draw->AddRectFilled(ImVec2(TrackX0, TrackCY - 4.0f), ImVec2(TrackX0 + U * (TrackX1 - TrackX0), TrackCY + 4.0f),
        FadeTint(kPillFill, Opacity), 4.0f);

    char Figure[8] = {};
    std::snprintf(Figure, sizeof(Figure), "%d%%", static_cast<int>(RenderScale_ * 100.0f + 0.5f));
    ImFont* Mono = Controls_->QueryMonoSmall();
    ImGui::PushFont(Mono);
    const ImVec2 FigureGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Figure);
    Draw->AddText(ImVec2(PillX + PillW - 56.0f + (48.0f - FigureGlyph.x) * 0.5f,
            PillY + 8.0f + (48.0f - FigureGlyph.y) * 0.5f),
        FadeTint(kWhite70, Opacity), Figure);
    ImGui::PopFont();

    PillX0_ = TrackX0;
    PillX1_ = TrackX1;
    PillY_  = TrackCY;

    if (Live)
    {
        ImGui::SetCursorScreenPos(ImVec2(TrackX0, PillY + 8.0f));
        ImGui::InvisibleButton("##ccscale", ImVec2(TrackX1 - TrackX0, 48.0f));
        const auto Follow = [&]()
        {
            float T = (ImGui::GetIO().MousePos.x - TrackX0) / (TrackX1 - TrackX0);
            T = Clamp01(T);
            RenderScale_ = kScaleMin + T * (1.0f - kScaleMin);
        };
        if (ImGui::IsItemActivated())
        {
            PillHeld_ = true;
            Follow();
        }
        if (PillHeld_ && ImGui::IsItemActive())
        {
            Follow();
        }
        if (PillHeld_ && ImGui::IsItemDeactivated())
        {
            PillHeld_ = false;
            ++Revision_;
            RaiseToast();
        }
    }
}

void ControlCentrePanel::RaiseToast() noexcept
{
    if (!NotifOn_)
    {
        return;
    }
    const char* Grade = "Standard";
    switch (Quality_)
    {
    case ControlCentreQuality::Minimal:   Grade = "Minimal";   break;
    case ControlCentreQuality::Economy:   Grade = "Economy";   break;
    case ControlCentreQuality::Standard:  Grade = "Standard";  break;
    case ControlCentreQuality::Ultra:     Grade = "Ultra";     break;
    case ControlCentreQuality::Reference: Grade = "Reference"; break;
    }
    std::snprintf(ToastTitle_, sizeof(ToastTitle_), "Render settings applied");
    std::snprintf(ToastSub_, sizeof(ToastSub_), "%s | GI %s, AA %s, scale %d%%", Grade,
        GiOn_ ? "on" : "off", AaOn_ ? "on" : "off", static_cast<int>(RenderScale_ * 100.0f + 0.5f));
    ToastRaised_ = ImGui::GetTime();
    ToastUntil_  = ToastRaised_ + 3.2;
}

void ControlCentrePanel::DrawToast(float ShadeY, float Width) noexcept
{
    const double Now = ImGui::GetTime();
    if (ToastUntil_ < 0.0 || Now >= ToastUntil_ || ShadeY <= 200.0f)
    {
        return;
    }
    const float FadeIn = static_cast<float>((Now - ToastRaised_) / 0.25);
    const float FadeOut = static_cast<float>((ToastUntil_ - Now) / 0.5);
    const float Opacity = FadeIn < FadeOut ? Clamp01(FadeIn) : Clamp01(FadeOut);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 Origin = ImGui::GetMainViewport()->Pos;
    const float ToastX = Origin.x + Width - 16.0f - 334.0f;
    const float ToastY = Origin.y + 48.0f;
    Draw->AddRectFilled(ImVec2(ToastX, ToastY), ImVec2(ToastX + 334.0f, ToastY + 64.0f),
        FadeTint(kTileIdle, Opacity), 12.0f);
    Draw->AddRect(ImVec2(ToastX, ToastY), ImVec2(ToastX + 334.0f, ToastY + 64.0f),
        FadeTint(kStroke, Opacity), 12.0f);

    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(ToastX + 14.0f, ToastY + 10.0f), FadeTint(kWhite, Opacity), ToastTitle_);
    Draw->AddText(ImVec2(ToastX + 14.0f, ToastY + 32.0f), FadeTint(kWhite70, Opacity), ToastSub_);
    ImGui::PopFont();
}

void ControlCentrePanel::DrawSun(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 24.0f;
    Draw->AddCircle(ImVec2(CX, CY), 7.0f * S, Tint, 24, Thick);
    for (uint32_t Ray = 0u; Ray < 8u; ++Ray)
    {
        const float A = static_cast<float>(Ray) * 0.7853982f;
        Draw->AddLine(ImVec2(CX + 9.5f * S * std::cos(A), CY + 9.5f * S * std::sin(A)),
            ImVec2(CX + 12.0f * S * std::cos(A), CY + 12.0f * S * std::sin(A)), Tint, Thick);
    }
}

void ControlCentrePanel::DrawSparkles(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint) noexcept
{
    const float S = Size / 24.0f;
    const auto Star = [&](float SX, float SY, float R)
    {
        const float W = R * 0.22f;
        Draw->AddTriangleFilled(ImVec2(SX, SY - R), ImVec2(SX - W, SY), ImVec2(SX + W, SY), Tint);
        Draw->AddTriangleFilled(ImVec2(SX + R, SY), ImVec2(SX, SY - W), ImVec2(SX, SY + W), Tint);
        Draw->AddTriangleFilled(ImVec2(SX, SY + R), ImVec2(SX + W, SY), ImVec2(SX - W, SY), Tint);
        Draw->AddTriangleFilled(ImVec2(SX - R, SY), ImVec2(SX, SY + W), ImVec2(SX, SY - W), Tint);
    };
    Star(CX - 3.0f * S, CY - 2.0f * S, 6.5f * S);
    Star(CX + 7.0f * S, CY - 6.0f * S, 3.0f * S);
    Star(CX + 6.0f * S, CY + 7.0f * S, 4.0f * S);
}

void ControlCentrePanel::DrawGauge(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 24.0f;
    Draw->PathArcTo(ImVec2(CX, CY), 8.0f * S, 210.0f * kDeg, 330.0f * kDeg, 24);
    Draw->PathStroke(Tint, 0, Thick);
    Draw->AddLine(ImVec2(CX, CY), ImVec2(CX + 4.2f * S, CY - 4.2f * S), Tint, Thick);
    Draw->AddCircleFilled(ImVec2(CX, CY), 1.5f * S, Tint);
}

void ControlCentrePanel::DrawBell(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 24.0f;
    Draw->PathArcTo(ImVec2(CX, CY + 1.0f * S), 7.0f * S, 200.0f * kDeg, 340.0f * kDeg, 24);
    Draw->PathStroke(Tint, 0, Thick);
    const float Ex = 7.0f * S * 0.9397f;
    const float Ey = 1.0f * S - 7.0f * S * 0.3420f;
    Draw->AddLine(ImVec2(CX - Ex, CY + Ey), ImVec2(CX + Ex, CY + Ey), Tint, Thick);
    Draw->AddLine(ImVec2(CX, CY + 2.0f * S), ImVec2(CX, CY + 4.0f * S), Tint, Thick);
    Draw->AddCircleFilled(ImVec2(CX, CY + 5.5f * S), 1.8f * S, Tint);
}

void ControlCentrePanel::DrawSliders(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 24.0f;
    constexpr float kKnobX[3] = { -3.0f, 4.0f, -1.0f };
    for (uint32_t Row = 0u; Row < 3u; ++Row)
    {
        const float LY = CY + (static_cast<float>(Row) - 1.0f) * 6.0f * S;
        Draw->AddLine(ImVec2(CX - 9.0f * S, LY), ImVec2(CX + 9.0f * S, LY), Tint, Thick);
        Draw->AddCircleFilled(ImVec2(CX + kKnobX[Row] * S, LY), 2.2f * S, Tint);
    }
}

void ControlCentrePanel::DrawWifi(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 16.0f;
    Draw->PathArcTo(ImVec2(CX, CY + 8.0f * S), 5.0f * S, 235.0f * kDeg, 305.0f * kDeg, 16);
    Draw->PathStroke(Tint, 0, Thick);
    Draw->PathArcTo(ImVec2(CX, CY + 8.0f * S), 8.5f * S, 235.0f * kDeg, 305.0f * kDeg, 20);
    Draw->PathStroke(Tint, 0, Thick);
    Draw->AddCircleFilled(ImVec2(CX, CY + 6.5f * S), 1.5f * S, Tint);
}

void ControlCentrePanel::DrawGear(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    (void)Thick;
    const float R = Size * 0.20f;
    Draw->AddCircle(ImVec2(CX, CY), R, Tint, 20, 1.2f);
    for (uint32_t Tooth = 0u; Tooth < 8u; ++Tooth)
    {
        const float A = static_cast<float>(Tooth) * 0.7853982f;
        Draw->AddLine(ImVec2(CX + R * 1.15f * std::cos(A), CY + R * 1.15f * std::sin(A)),
            ImVec2(CX + R * 1.70f * std::cos(A), CY + R * 1.70f * std::sin(A)), Tint, 1.4f);
    }
    Draw->AddCircleFilled(ImVec2(CX, CY), R * 0.35f, Tint);
}

void ControlCentrePanel::DrawVideo(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept
{
    const float S = Size / 20.0f;
    Draw->AddRect(ImVec2(CX - 8.0f * S, CY - 5.5f * S), ImVec2(CX + 4.0f * S, CY + 5.5f * S), Tint, 2.0f * S,
        0, Thick);
    Draw->AddTriangle(ImVec2(CX + 4.0f * S, CY - 3.5f * S), ImVec2(CX + 10.0f * S, CY),
        ImVec2(CX + 4.0f * S, CY + 3.5f * S), Tint, Thick);
}

} // namespace Frontier
