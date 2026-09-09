//============================================================================================================================================
//                                                    VIEWPORTPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column.

#include "ViewportPanel.h"

#include "ControlPanel.h"
#include "EditorInstance.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kView   = IM_COL32(7, 9, 12, 255);
constexpr ImU32 kTile   = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kField  = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);
constexpr ImU32 kInset  = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kHover  = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kSeated = IM_COL32(42, 42, 42, 255);
constexpr ImU32 kOk     = IM_COL32(34, 197, 94, 255);
constexpr ImU32 kDanger = IM_COL32(239, 68, 68, 255);
constexpr ImU32 kAmber  = IM_COL32(245, 158, 11, 255);
constexpr ImU32 kHi     = IM_COL32(108, 119, 255, 255);

constexpr uint32_t kEdit      = 0u;
constexpr uint32_t kPlay      = 1u;
constexpr uint32_t kSimulate  = 2u;

const char* kViewNames[3] = { "Perspective", "Orthographic", "Top" };

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSPORT GLYPHS
//------------------------------------------------------------------------------------------------------------------------

// The run glyphs, traced from the reference icon sheet (24-space, stroked at 1.9): filled here, because
//    at thirteen pixels a filled mark reads where an outline ghosts.
ImVec2 GlyphDot(const ImVec2& Centre, float Size, float X, float Y) noexcept
{
    const float S = Size / 24.0f;
    return ImVec2(Centre.x + (X - 12.0f) * S, Centre.y + (Y - 12.0f) * S);
}

void PlayGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 6.0f, 4.0f), GlyphDot(Centre, Size, 6.0f, 20.0f),
        GlyphDot(Centre, Size, 20.0f, 12.0f), Tint);
}

void PauseGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    const float S = Size / 24.0f;
    Draw->AddRectFilled(GlyphDot(Centre, Size, 6.0f, 4.0f), GlyphDot(Centre, Size, 10.0f, 20.0f), Tint, S);
    Draw->AddRectFilled(GlyphDot(Centre, Size, 14.0f, 4.0f), GlyphDot(Centre, Size, 18.0f, 20.0f), Tint, S);
}

void StopGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddRectFilled(GlyphDot(Centre, Size, 6.0f, 6.0f), GlyphDot(Centre, Size, 18.0f, 18.0f),
        Tint, 2.0f * Size / 24.0f);
}

void StepGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 7.0f, 5.0f), GlyphDot(Centre, Size, 7.0f, 19.0f),
        GlyphDot(Centre, Size, 16.0f, 12.0f), Tint);
    Draw->AddRectFilled(GlyphDot(Centre, Size, 17.0f, 5.0f), GlyphDot(Centre, Size, 19.0f, 19.0f), Tint, 0.0f);
}

void SimGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    // The clock face with its arrow: the arc walks the long way round (east through south, west, north),
    //    leaving the north-east quadrant for the arrowhead.
    const float S = Size / 24.0f;
    Draw->PathArcTo(Centre, 9.0f * S, 0.0f, 4.71239f, 0);
    Draw->PathStroke(Tint, 1.9f * S, 0);
    Draw->AddLine(GlyphDot(Centre, Size, 12.0f, 12.0f), GlyphDot(Centre, Size, 12.0f, 7.0f), Tint, 1.9f * S);
    Draw->AddLine(GlyphDot(Centre, Size, 12.0f, 12.0f), GlyphDot(Centre, Size, 15.5f, 14.0f), Tint, 1.9f * S);
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 17.0f, 3.0f), GlyphDot(Centre, Size, 21.0f, 5.0f),
        GlyphDot(Centre, Size, 17.0f, 7.0f), Tint);
}

void CommandGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    // The console's loop, vertex for vertex from the sheet; the corners stand in for the sheet's arcs.
    constexpr float Loop[10][2] = { { 6.0f, 3.0f }, { 3.0f, 6.0f }, { 3.0f, 18.0f }, { 6.0f, 15.0f },
        { 18.0f, 15.0f }, { 21.0f, 18.0f }, { 21.0f, 6.0f }, { 18.0f, 9.0f }, { 6.0f, 9.0f }, { 6.0f, 3.0f } };
    for (uint32_t i = 0u; i < 10u; ++i)
    {
        Draw->PathLineTo(GlyphDot(Centre, Size, Loop[i][0], Loop[i][1]));
    }
    Draw->PathStroke(Tint, 1.9f * Size / 24.0f, ImDrawFlags_Closed);
}

void RunGlyph(uint32_t Icon, ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    if (Icon == 0u)      { PlayGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 1u) { SimGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 2u) { PauseGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 3u) { StepGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 4u) { StopGlyph(Draw, Centre, Size, Tint); }
    else                 { CommandGlyph(Draw, Centre, Size, Tint); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPACED CAPS
//------------------------------------------------------------------------------------------------------------------------

// Letterspaced caps, the footer labels and chips: the reference spaces its micro-caps by a pixel and change.
float SpacedCapsWidth(ImFont* Font, const char* Text, float Tracking) noexcept
{
    ImGui::PushFont(Font);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const char Upper[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(*P))), '\0' };
        Advance += Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, Upper).x + Tracking;
    }
    ImGui::PopFont();
    return Advance > 0.0f ? Advance - Tracking : 0.0f;
}

void SpacedCaps(ImDrawList* Draw, ImFont* Font, const char* Text, const ImVec2& At, ImU32 Tint,
                float Tracking) noexcept
{
    ImGui::PushFont(Font);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const char Upper[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(*P))), '\0' };
        Draw->AddText(ImVec2(At.x + Advance, At.y), Tint, Upper);
        Advance += Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, Upper).x + Tracking;
    }
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUICK COMMANDS
//------------------------------------------------------------------------------------------------------------------------

struct QuickCommand
{
    const char* Label;
    const char* Sub;
    uint32_t    Icon;   // play, sim, pause, step, stop, command — the run glyphs above
};

constexpr QuickCommand kQuick[7] = {
    { "Play \xe2\x80\x94 run through a camera", "transport", 0u },
    { "Simulate \xe2\x80\x94 run the world", "transport", 1u },
    { "Pause / resume", "transport", 2u },
    { "Step one frame", "transport", 3u },
    { "Stop and restore", "transport", 4u },
    { "Realtime viewport", "viewport", 5u },
    { "Day cycle", "day", 1u },
};

bool InfixMatch(const char* Label, const char* Text) noexcept
{
    if (Text[0] == '\0')
    {
        return true;
    }
    for (const char* P = Label; *P != '\0'; ++P)
    {
        const char* A = P;
        const char* B = Text;
        while (*A != '\0' && *B != '\0'
            && std::tolower(static_cast<unsigned char>(*A)) == std::tolower(static_cast<unsigned char>(*B)))
        {
            ++A;
            ++B;
        }
        if (*B == '\0')
        {
            return true;
        }
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin("Viewport", nullptr))
    {
        ImGui::End();
        return;
    }

    if (DayCycle_ && Realtime_ && !Paused_)
    {
        // The day cycle walks the clock the way the reference loop does: one day-hour per 600 seconds at
        //    the Sun sheet's 1.00 rate.
        ClockHours_ += ImGui::GetIO().DeltaTime / 600.0f;
        if (ClockHours_ >= 24.0f)
        {
            ClockHours_ -= 24.0f;
        }
    }

    RecordBar();
    RecordView();
    RecordCommand();
    RecordFooter(Instances, InstanceCount);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::SetTransport(uint32_t Mode) noexcept
{
    // A run is always realtime; coming home unpauses. The world snapshot the reference keeps belongs to
    //    the engine wiring, so the UI keeps the mode machine and its two rules.
    Transport_ = Mode;
    if (Mode == kEdit)
    {
        Paused_ = false;
    }
    else
    {
        Realtime_ = true;
        Paused_   = false;
    }
}

void ViewportPanel::SetPaused(bool Paused) noexcept
{
    Paused_ = Paused;
}

void ViewportPanel::SetRealtime(bool Realtime) noexcept
{
    Realtime_ = Realtime;
}

void ViewportPanel::StepOnce() noexcept
{
    // One tick past the pause, and the daylight clock takes the reference's thirtieth-of-a-second stride.
    if (!Paused_)
    {
        Paused_ = true;
    }
    ClockHours_ += (1.0f / 30.0f) / 600.0f;
    if (ClockHours_ >= 24.0f)
    {
        ClockHours_ -= 24.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         HEADER BAR
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordBar() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 44.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();

    const ImVec2 TileMin(Cursor.x, Cursor.y + 8.0f);
    const ImVec2 TileMax(Cursor.x + 28.0f, Cursor.y + 36.0f);
    Draw->AddRectFilled(TileMin, TileMax, kTile, 8.0f);
    Draw->AddRect(TileMin, TileMax, kStroke, 8.0f);
    ImGui::PushFont(Ui);
    const ImVec2 FGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "F");
    Draw->AddText(ImVec2(Cursor.x + (28.0f - FGlyph.x) * 0.5f, Cursor.y + 8.0f + (28.0f - FGlyph.y) * 0.5f),
        kText, "F");
    ImGui::PopFont();

    // Degenerate stages hold the bar's height and rest: the bar needs ~560 px, and narrower columns keep
    //    the brand alone rather than tripping a cursor-boundary assert on the way past the right edge.
    if (RowWidth < 560.0f)
    {
        return;
    }

    float X = Cursor.x + 36.0f;
    const float BtnY = Cursor.y + 8.0f;

    bool* Docks[2] = { &DockLeft_, &DockRight_ };
    for (uint32_t i = 0u; i < 2u; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, BtnY));
        char DockId[10] = {};
        std::snprintf(DockId, sizeof(DockId), "##dk%u", i);
        ImGui::InvisibleButton(DockId, ImVec2(28.0f, 28.0f));
        const bool Hot = ImGui::IsItemHovered();
        if (Hot && ImGui::IsMouseClicked(0))
        {
            *Docks[i] = !*Docks[i];
        }
        // Decorative: the dock columns are fixed while the layout is under review.
        const ImVec2 Centre(X + 14.0f, BtnY + 14.0f);
        Draw->AddCircleFilled(Centre, 14.0f, Hot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
        Draw->AddRect(ImVec2(Centre.x - 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 6.0f, Centre.y + 5.0f),
            *Docks[i] ? kDim : kFaint, 2.0f, 0, 1.4f);
        const float BarX = (i == 0u) ? (Centre.x - 6.0f) : (Centre.x + 3.0f);
        Draw->AddRectFilled(ImVec2(BarX, Centre.y - 5.0f), ImVec2(BarX + 3.0f, Centre.y + 5.0f),
            *Docks[i] ? kText : kFaint);
        X += 36.0f;
    }

    ImGui::PushFont(Small);
    const ImVec2 MarkGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Markers");
    ImGui::PopFont();
    const float MarkW = MarkGlyph.x + 24.0f;
    ImGui::SetCursorScreenPos(ImVec2(X, BtnY + 3.0f));
    ImGui::InvisibleButton("##markers", ImVec2(MarkW, 22.0f));
    const bool MarkHot = ImGui::IsItemHovered();
    if (MarkHot && ImGui::IsMouseClicked(0))
    {
        MarkersOn_ = !MarkersOn_;
    }
    Draw->AddRectFilled(ImVec2(X, BtnY + 3.0f), ImVec2(X + MarkW, BtnY + 25.0f),
        MarkersOn_ ? IM_COL32(26, 26, 26, 255) : IM_COL32(255, 255, 255, 8), 11.0f);
    Draw->AddRect(ImVec2(X, BtnY + 3.0f), ImVec2(X + MarkW, BtnY + 25.0f),
        MarkersOn_ ? kStrong : kStroke, 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(X + 12.0f, BtnY + 3.0f + (22.0f - MarkGlyph.y) * 0.5f),
        MarkersOn_ ? kText : kDim, "Markers");
    ImGui::PopFont();
    X += MarkW + 8.0f;

    ImGui::PushFont(Small);
    const ImVec2 ViewGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, kViewNames[ViewPick_]);
    ImGui::PopFont();
    const float ViewW = ViewGlyph.x + 24.0f;
    ImGui::SetCursorScreenPos(ImVec2(X, BtnY + 3.0f));
    ImGui::InvisibleButton("##viewcycle", ImVec2(ViewW, 22.0f));
    const bool ViewHot = ImGui::IsItemHovered();
    if (ViewHot && ImGui::IsMouseClicked(0))
    {
        ViewPick_ = (ViewPick_ + 1u) % 3u;
    }
    Draw->AddRectFilled(ImVec2(X, BtnY + 3.0f), ImVec2(X + ViewW, BtnY + 25.0f),
        ViewHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12), 11.0f);
    Draw->AddRect(ImVec2(X, BtnY + 3.0f), ImVec2(X + ViewW, BtnY + 25.0f), kStroke, 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(X + 12.0f, BtnY + 3.0f + (22.0f - ViewGlyph.y) * 0.5f), kDim, kViewNames[ViewPick_]);
    ImGui::PopFont();

    // The transport strip: five runs in a seated pill, the realtime lamp, and the status chip. The
    //    reference's order — play, simulate, pause, step, stop — and its rules: step waits on pause, stop
    //    waits on a run, realtime rests while running.
    const bool Running = (Transport_ != kEdit);
    const char* ChipLabel = Paused_ ? "Paused" : (Transport_ == kPlay) ? "Play"
        : (Transport_ == kSimulate)   ? "Simulate"
        : (Realtime_ ? "Edit" : "Edit \xc2\xb7 static");

    struct RunButton
    {
        const char* Id;
        uint32_t    Icon;
        const char* Tip;
    };
    static constexpr RunButton kRuns[5] = {
        { "##tplay", 0u, "Play \xe2\x80\x94 run the world through a scene camera  (Alt P)" },
        { "##tsim", 1u, "Simulate \xe2\x80\x94 run the world, keep the editor camera  (Alt S)" },
        { "##tpause", 2u, "Pause / resume  (P)" },
        { "##tstep", 3u, "Advance one frame  (.)" },
        { "##tstop", 4u, "Stop \xe2\x80\x94 restore the editor  (Esc)" },
    };

    constexpr float kBtnW = 28.0f, kBtnH = 26.0f, kBtnGap = 2.0f, kStripH = 30.0f, kStripPad = 4.0f;
    const float ChipW = SpacedCapsWidth(Small, ChipLabel, 1.3f) + 20.0f;
    const float RtW   = SpacedCapsWidth(Small, "Realtime", 1.1f) + 32.0f;
    const float Inner = 5.0f * kBtnW + 4.0f * kBtnGap + kBtnGap + 11.0f + kBtnGap + RtW + kBtnGap + ChipW;
    const float StripW = Inner + 2.0f * kStripPad;
    const float StripX = Cursor.x + RowWidth - StripW;
    const float StripY = Cursor.y + 7.0f;
    Draw->AddRectFilled(ImVec2(StripX, StripY), ImVec2(StripX + StripW, StripY + kStripH), kInset, 15.0f);
    Draw->AddRect(ImVec2(StripX, StripY), ImVec2(StripX + StripW, StripY + kStripH), kStroke, 15.0f);

    float BX = StripX + kStripPad;
    const float TBtnY = StripY + 2.0f;
    for (uint32_t i = 0u; i < 5u; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(BX, TBtnY));
        ImGui::InvisibleButton(kRuns[i].Id, ImVec2(kBtnW, kBtnH));
        const bool Hot = ImGui::IsItemHovered();

        bool Disabled = false;
        bool On       = false;
        if (i == 0u)      { On = (Transport_ == kPlay); }
        else if (i == 1u) { On = (Transport_ == kSimulate); }
        else if (i == 2u) { On = Paused_; }
        else if (i == 3u) { Disabled = !Paused_; }
        else              { Disabled = !Running; }

        if (Hot && !Disabled)
        {
            ImGui::SetTooltip("%s", kRuns[i].Tip);
            if (ImGui::IsMouseClicked(0))
            {
                if (i == 0u)      { SetTransport(Transport_ == kPlay ? kEdit : kPlay); }
                else if (i == 1u) { SetTransport(Transport_ == kSimulate ? kEdit : kSimulate); }
                else if (i == 2u) { SetPaused(!Paused_); }
                else if (i == 3u) { StepOnce(); }
                else              { SetTransport(kEdit); }
            }
        }

        ImU32 Bg = IM_COL32(0, 0, 0, 0);
        ImU32 GlyphTint = kDim;
        if (Disabled)
        {
            GlyphTint = IM_COL32(136, 136, 136, 77);
        }
        else if (On)
        {
            if (i == 0u)      { Bg = kOk; GlyphTint = IM_COL32(4, 20, 10, 255); }
            else if (i == 1u) { Bg = kHi; GlyphTint = IM_COL32(255, 255, 255, 255); }
            else              { Bg = kAmber; GlyphTint = IM_COL32(26, 18, 4, 255); }
        }
        else if (Hot)
        {
            if (i == 0u)      { Bg = IM_COL32(34, 197, 94, 41); GlyphTint = IM_COL32(126, 231, 165, 255); }
            else if (i == 1u) { Bg = IM_COL32(108, 119, 255, 46); GlyphTint = IM_COL32(174, 180, 255, 255); }
            else if (i == 2u) { Bg = IM_COL32(245, 158, 11, 41); GlyphTint = IM_COL32(246, 198, 106, 255); }
            else if (i == 4u) { Bg = IM_COL32(239, 68, 68, 41); GlyphTint = IM_COL32(255, 155, 155, 255); }
            else              { Bg = kHover; GlyphTint = kText; }
        }
        if (Bg != IM_COL32(0, 0, 0, 0))
        {
            Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + kBtnW, TBtnY + kBtnH), Bg, 13.0f);
        }
        uint32_t Icon = kRuns[i].Icon;
        if (i == 2u && Paused_)
        {
            Icon = 0u;   // paused, the button offers the way back: the reference swaps in play
        }
        RunGlyph(Icon, Draw, ImVec2(BX + kBtnW * 0.5f, TBtnY + kBtnH * 0.5f), 13.0f, GlyphTint);
        BX += kBtnW + kBtnGap;
    }

    BX += kBtnGap;
    Draw->AddLine(ImVec2(BX + 5.0f, StripY + 7.0f), ImVec2(BX + 5.0f, StripY + 23.0f), kStroke);
    BX += 11.0f + kBtnGap;

    ImGui::SetCursorScreenPos(ImVec2(BX, TBtnY));
    ImGui::InvisibleButton("##trealtime", ImVec2(RtW, kBtnH));
    const bool RtHot = ImGui::IsItemHovered();
    const bool RtOff = Running;
    if (RtHot && !RtOff)
    {
        ImGui::SetTooltip("Realtime viewport \xe2\x80\x94 animate and redraw continuously  (Ctrl R)");
        if (ImGui::IsMouseClicked(0))
        {
            SetRealtime(!Realtime_);
        }
    }
    if (Realtime_)
    {
        Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + RtW, TBtnY + kBtnH),
            IM_COL32(255, 255, 255, 31), 13.0f);
    }
    else if (RtHot && !RtOff)
    {
        Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + RtW, TBtnY + kBtnH), kHover, 13.0f);
    }
    const ImVec2 Led(BX + 13.0f, TBtnY + 13.0f);
    if (Realtime_)
    {
        Draw->AddCircleFilled(Led, 6.0f, IM_COL32(34, 197, 94, 80));
        Draw->AddCircleFilled(Led, 3.0f, kOk);
    }
    else
    {
        Draw->AddCircleFilled(Led, 3.0f, IM_COL32(58, 58, 58, 255));
    }
    ImGui::PushFont(Small);
    const ImVec2 RtGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Realtime");
    ImGui::PopFont();
    const bool RtDim = RtOff && !Realtime_;
    SpacedCaps(Draw, Small, "Realtime", ImVec2(BX + 22.0f, TBtnY + (kBtnH - RtGlyph.y) * 0.5f),
        RtDim ? IM_COL32(136, 136, 136, 77) : (Realtime_ ? kText : kDim), 1.1f);
    BX += RtW + kBtnGap;

    const float ChipY = StripY + 4.0f;
    ImU32 ChipBg = IM_COL32(0, 0, 0, 0);
    ImU32 ChipTint = kFaint;
    if (Paused_)                       { ChipBg = IM_COL32(245, 158, 11, 51); ChipTint = IM_COL32(246, 198, 106, 255); }
    else if (Transport_ == kPlay)      { ChipBg = IM_COL32(34, 197, 94, 51); ChipTint = IM_COL32(126, 231, 165, 255); }
    else if (Transport_ == kSimulate)  { ChipBg = IM_COL32(108, 119, 255, 51); ChipTint = IM_COL32(174, 180, 255, 255); }
    if (ChipBg != IM_COL32(0, 0, 0, 0))
    {
        Draw->AddRectFilled(ImVec2(BX, ChipY), ImVec2(BX + ChipW, ChipY + 22.0f), ChipBg, 11.0f);
    }
    SpacedCaps(Draw, Small, ChipLabel, ImVec2(BX + 10.0f, ChipY + (22.0f - RtGlyph.y) * 0.5f), ChipTint, 1.3f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE VIEW
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordView() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float ViewH    = ImGui::GetContentRegionAvail().y - 40.0f - 8.0f - 40.0f - 8.0f;
    if (ViewH < 40.0f)
    {
        return;
    }

    ImGui::Dummy(ImVec2(RowWidth, ViewH));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    Draw->AddRectFilled(Min, Max, kView, 12.0f);
    Draw->AddRect(Min, Max, kStroke, 12.0f);

    ImGui::PushFont(Ui);
    const ImVec2 HintGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "The Cornell Box renders here");
    Draw->AddText(ImVec2(Min.x + (RowWidth - HintGlyph.x) * 0.5f, Min.y + ViewH * 0.5f - 22.0f),
        kDim, "The Cornell Box renders here");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 SubGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "in the engine build");
    Draw->AddText(ImVec2(Min.x + (RowWidth - SubGlyph.x) * 0.5f, Min.y + ViewH * 0.5f + 2.0f),
        kFaint, "in the engine build");
    ImGui::PopFont();

    const ImVec2 OrbCentre(Max.x - 52.0f, Max.y - 52.0f);
    Draw->AddCircleFilled(OrbCentre, 32.0f, IM_COL32(16, 16, 20, 255));
    Draw->AddCircle(OrbCentre, 32.0f, kStrong, 0, 1.2f);
    Draw->AddLine(ImVec2(OrbCentre.x - 20.0f, OrbCentre.y), ImVec2(OrbCentre.x + 20.0f, OrbCentre.y),
        IM_COL32(239, 83, 80, 140), 1.6f);
    Draw->AddLine(ImVec2(OrbCentre.x, OrbCentre.y - 20.0f), ImVec2(OrbCentre.x, OrbCentre.y + 20.0f),
        IM_COL32(105, 208, 109, 140), 1.6f);
    Draw->AddCircleFilled(ImVec2(OrbCentre.x + 20.0f, OrbCentre.y), 4.0f, IM_COL32(239, 83, 80, 255));
    Draw->AddCircleFilled(ImVec2(OrbCentre.x, OrbCentre.y - 20.0f), 4.0f, IM_COL32(105, 208, 109, 255));
    Draw->AddCircleFilled(OrbCentre, 4.0f, IM_COL32(91, 140, 255, 255));
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(OrbCentre.x + 24.0f, OrbCentre.y - 6.0f), IM_COL32(239, 83, 80, 255), "X");
    Draw->AddText(ImVec2(OrbCentre.x - 3.0f, OrbCentre.y - 32.0f), IM_COL32(105, 208, 109, 255), "Y");
    Draw->AddText(ImVec2(OrbCentre.x + 6.0f, OrbCentre.y + 4.0f), IM_COL32(91, 140, 255, 255), "Z");
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMMAND LINE
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordCommand() noexcept
{
    if ((ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && ImGui::IsKeyPressed(ImGuiKey_K, false))
    {
        FocusCommand_ = true;
        SugShut_      = false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 40.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    Draw->AddRectFilled(Min, Max, kWash);
    Draw->AddLine(ImVec2(Min.x, Min.y), ImVec2(Max.x, Min.y), kStroke);

    if (std::strcmp(CommandText_, LastPaint_) != 0)
    {
        SugShut_ = false;   // moved text reopens the stack a run had shut
        std::snprintf(LastPaint_, sizeof(LastPaint_), "%s", CommandText_);
    }

    const double Now    = ImGui::GetTime();
    const bool   EchoOn = (CommandEcho_[0] != '\0') && (Now < EchoUntil_);

    // The right cluster first, so the field takes the middle: run, kbd, echo.
    const ImVec2 RunMin(Max.x - 8.0f - 28.0f, Min.y + 6.0f);
    ImFont* MonoSmall = Controls_->QueryMonoSmall();
    ImGui::PushFont(MonoSmall);
    const ImVec2 CmdGlyph = MonoSmall->CalcTextSizeA(MonoSmall->LegacySize, FLT_MAX, 0.0f, "\xe2\x8c\x98");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 KbdGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "K");
    ImGui::PopFont();
    const ImVec2 KbdSize(CmdGlyph.x + KbdGlyph.x + 12.0f, KbdGlyph.y + 4.0f);
    const ImVec2 KbdMin(RunMin.x - 10.0f - KbdSize.x, Min.y + (40.0f - KbdSize.y) * 0.5f);
    float EchoW = 0.0f;
    if (EchoOn)
    {
        ImGui::PushFont(Small);
        EchoW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CommandEcho_).x;
        ImGui::PopFont();
        if (EchoW > RowWidth * 0.42f)
        {
            EchoW = RowWidth * 0.42f;
        }
    }
    const float FieldX0 = Min.x + 10.0f + 20.0f + 10.0f;
    const float FieldX1 = (EchoOn ? (KbdMin.x - 10.0f - EchoW) : KbdMin.x) - 10.0f;

    CommandGlyph(Draw, ImVec2(Min.x + 20.0f, Min.y + 20.0f), 14.0f, CommandFocus_ ? kHi : kFaint);

    ImGui::SetCursorScreenPos(ImVec2(FieldX0, Min.y + 9.0f));
    ImGui::PushItemWidth(FieldX1 - FieldX0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));
    ImGui::PushFont(Ui);
    if (FocusCommand_)
    {
        ImGui::SetKeyboardFocusHere();
        FocusCommand_ = false;
    }
    static constexpr char kPlaceholder[] = "Say what you want \xe2\x80\x94 \xe2\x80\x9crotate cube 40 degrees on Z\xe2\x80\x9d, " "\xe2\x80\x9c" "add sphere at x 3 y 2 z -1\xe2\x80\x9d";
    const bool Done = ImGui::InputTextWithHint("##cmd", kPlaceholder, CommandText_, sizeof(CommandText_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion
            | ImGuiInputTextFlags_CallbackHistory,
        &ViewportPanel::ConsoleCallback, this);
    const bool Focused = ImGui::IsItemFocused();
    if ((CommandFocus_ || Focused) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        // Clear and shut; the caret rests where it was, and the stack stays shut till the text moves.
        CommandText_[0] = '\0';
        Ghost_[0]       = '\0';
        SugShut_        = true;
        SugUntil_       = 0.0;
    }
    if (CommandFocus_ && !Focused)
    {
        SugUntil_ = Now + 0.12;
    }
    CommandFocus_ = Focused;
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();

    if (Done)
    {
        // Enter runs the standing row, even on an empty line — the reference's own quirk, kept.
        RunSugRow(SugIndex_);
    }

    if (Focused && Ghost_[0] != '\0')
    {
        ImGui::PushFont(Ui);
        const ImVec2 TypedW = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, CommandText_);
        Draw->AddText(ImVec2(FieldX0 + 2.0f + TypedW.x, Min.y + 9.0f + 4.0f),
            IM_COL32(92, 92, 92, 166), Ghost_);
        ImGui::PopFont();
    }

    if (EchoOn)
    {
        ImGui::PushFont(Small);
        const ImVec2 EchoGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CommandEcho_);
        Draw->PushClipRect(ImVec2(FieldX1 + 10.0f, Min.y), ImVec2(KbdMin.x - 10.0f, Max.y), true);
        Draw->AddText(ImVec2(FieldX1 + 10.0f, Min.y + (40.0f - EchoGlyph.y) * 0.5f), kDim, CommandEcho_);
        Draw->PopClipRect();
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(KbdMin);
    ImGui::InvisibleButton("##kcmd", KbdSize);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        FocusCommand_ = true;
        SugShut_      = false;
    }
    Draw->AddRectFilled(KbdMin, ImVec2(KbdMin.x + KbdSize.x, KbdMin.y + KbdSize.y), kField, 6.0f);
    Draw->AddRect(KbdMin, ImVec2(KbdMin.x + KbdSize.x, KbdMin.y + KbdSize.y), kStroke, 6.0f);
    ImGui::PushFont(Small);
    Draw->AddText(MonoSmall, MonoSmall->LegacySize, ImVec2(KbdMin.x + 6.0f, KbdMin.y + 2.0f), kFaint,
        "\xe2\x8c\x98");
    Draw->AddText(ImVec2(KbdMin.x + 6.0f + CmdGlyph.x, KbdMin.y + 2.0f), kFaint, "K");
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(RunMin);
    ImGui::InvisibleButton("##cmdrun", ImVec2(28.0f, 28.0f));
    const bool RunHot = ImGui::IsItemHovered();
    if (RunHot)
    {
        ImGui::SetTooltip("Run  (Enter)");
        if (ImGui::IsMouseClicked(0))
        {
            RunSugRow(SugIndex_);
        }
    }
    const ImVec2 RunCentre(RunMin.x + 14.0f, RunMin.y + 14.0f);
    if (RunHot)
    {
        Draw->AddCircleFilled(RunCentre, 14.0f, kHover);
    }
    PlayGlyph(Draw, RunCentre, 13.0f, RunHot ? kText : kDim);

    const bool SugOpen = (Focused && !SugShut_) || (Now < SugUntil_);
    if (SugOpen)
    {
        PaintSuggestions();

        // The stack rises out of the console instead of covering the viewport: header caps, then rows
        //    with their glyph, title, and right-hung sub.
        constexpr float kRowH = 36.0f, kHeadH = 24.0f, kPad = 6.0f;
        const float StackH  = kPad + kHeadH + static_cast<float>(SugCount_) * kRowH + kPad;
        const float StackX0 = Min.x + 8.0f;
        const float StackX1 = Max.x - 8.0f;
        const float StackY1 = Min.y - 6.0f;
        const float StackY0 = StackY1 - StackH;
        Draw->AddRectFilled(ImVec2(StackX0, StackY0), ImVec2(StackX1, StackY1),
            IM_COL32(12, 12, 12, 247), 18.0f);
        Draw->AddRect(ImVec2(StackX0, StackY0), ImVec2(StackX1, StackY1), kStrong, 18.0f);

        const char* Head = (SugCount_ == 0u) ? "Nothing matches \xe2\x80\x94 try \xe2\x80\x9chelp\xe2\x80\x9d"
            : (CommandText_[0] != '\0' ? "What this will do" : "Say something like");
        SpacedCaps(Draw, Small, Head, ImVec2(StackX0 + 16.0f, StackY0 + 10.0f), kFaint, 1.3f);

        const uint32_t Rows = SugCount_;
        for (uint32_t r = 0u; r < Rows; ++r)
        {
            const float RowY0 = StackY0 + kPad + kHeadH + static_cast<float>(r) * kRowH;
            const ImVec2 RowMin(StackX0 + kPad, RowY0);
            const ImVec2 RowMax(StackX1 - kPad, RowY0 + kRowH);
            ImGui::SetCursorScreenPos(RowMin);
            char SugId[10] = {};
            std::snprintf(SugId, sizeof(SugId), "##sug%u", r);
            ImGui::InvisibleButton(SugId, ImVec2(RowMax.x - RowMin.x, kRowH));
            const bool RowHot = ImGui::IsItemHovered();

            const uint32_t Cmd = SugRows_[r];
            if (RowHot)
            {
                Draw->AddRectFilled(RowMin, RowMax, IM_COL32(27, 27, 27, 255), 13.0f);
            }
            else if (r == SugIndex_)
            {
                Draw->AddRectFilled(RowMin, RowMax, kSeated, 13.0f);
            }
            uint32_t Icon = kQuick[Cmd].Icon;
            if (Cmd == 2u && Paused_)
            {
                Icon = 0u;
            }
            RunGlyph(Icon, Draw, ImVec2(RowMin.x + 19.0f, RowY0 + kRowH * 0.5f), 14.0f, kDim);
            ImGui::PushFont(Ui);
            const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, QuickLabels_[Cmd]);
            Draw->AddText(ImVec2(RowMin.x + 37.0f, RowY0 + (kRowH - TitleGlyph.y) * 0.5f), kText,
                QuickLabels_[Cmd]);
            ImGui::PopFont();
            ImGui::PushFont(Small);
            const ImVec2 SubGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, kQuick[Cmd].Sub);
            ImGui::PopFont();
            const float SubW = SpacedCapsWidth(Small, kQuick[Cmd].Sub, 0.7f);
            SpacedCaps(Draw, Small, kQuick[Cmd].Sub,
                ImVec2(RowMax.x - 12.0f - SubW, RowY0 + (kRowH - SubGlyph.y) * 0.5f), kFaint, 0.7f);

            if (RowHot)
            {
                SugIndex_ = r;
                if (ImGui::IsMouseClicked(0))
                {
                    RunSugRow(r);
                    break;   // the run repaints the rows; the rest redraw next tick, shut
                }
            }
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SUGGESTIONS
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::PaintSuggestions() noexcept
{
    if (std::strcmp(CommandText_, "help") == 0)
    {
        // The reference's help clears the line and shows the whole table.
        CommandText_[0] = '\0';
    }
    for (uint32_t i = 0u; i < 7u; ++i)
    {
        if (i == 5u)
        {
            std::snprintf(QuickLabels_[i], sizeof(QuickLabels_[i]), "Realtime viewport: turn %s",
                Realtime_ ? "off" : "on");
        }
        else if (i == 6u)
        {
            std::snprintf(QuickLabels_[i], sizeof(QuickLabels_[i]), "Day cycle: turn %s",
                DayCycle_ ? "off" : "on");
        }
        else
        {
            std::snprintf(QuickLabels_[i], sizeof(QuickLabels_[i]), "%s", kQuick[i].Label);
        }
    }

    SugCount_ = 0u;
    for (uint32_t i = 0u; i < 7u; ++i)
    {
        if (InfixMatch(QuickLabels_[i], CommandText_))
        {
            SugRows_[SugCount_] = i;
            ++SugCount_;
        }
    }
    if (SugCount_ > 0u && SugIndex_ >= SugCount_)
    {
        SugIndex_ = 0u;
    }

    Ghost_[0] = '\0';
    if (CommandText_[0] != '\0' && SugCount_ > 0u)
    {
        const char* Top    = QuickLabels_[SugRows_[0]];
        const size_t Eaten = std::strlen(CommandText_);
        bool Prefix = true;
        for (size_t i = 0u; i < Eaten; ++i)
        {
            if (Top[i] == '\0'
                || std::tolower(static_cast<unsigned char>(Top[i]))
                    != std::tolower(static_cast<unsigned char>(CommandText_[i])))
            {
                Prefix = false;
                break;
            }
        }
        if (Prefix)
        {
            std::snprintf(Ghost_, sizeof(Ghost_), "%s", Top + Eaten);
        }
    }
}

void ViewportPanel::RunSugRow(uint32_t Row) noexcept
{
    if (Row >= SugCount_)
    {
        return;
    }
    const uint32_t Cmd = SugRows_[Row];
    if (Cmd == 0u)      { SetTransport(kPlay); }
    else if (Cmd == 1u) { SetTransport(kSimulate); }
    else if (Cmd == 2u) { SetPaused(!Paused_); }
    else if (Cmd == 3u) { StepOnce(); }
    else if (Cmd == 4u) { SetTransport(kEdit); }
    else if (Cmd == 5u) { SetRealtime(!Realtime_); }
    else
    {
        DayCycle_ = !DayCycle_;
        if (DayCycle_ && !Realtime_)
        {
            SetRealtime(true);
        }
    }

    if (CommandText_[0] != '\0'
        && (PastCount_ == 0u || std::strcmp(CommandText_, CommandPast_[PastCount_ - 1u]) != 0))
    {
        if (PastCount_ == 8u)
        {
            for (uint32_t i = 0u; i < 7u; ++i)
            {
                std::snprintf(CommandPast_[i], sizeof(CommandPast_[i]), "%s", CommandPast_[i + 1u]);
            }
            --PastCount_;
        }
        std::snprintf(CommandPast_[PastCount_], sizeof(CommandPast_[PastCount_]), "%s", CommandText_);
        ++PastCount_;
    }
    PastAt_ = -1;
    std::snprintf(CommandEcho_, sizeof(CommandEcho_), "%s", QuickLabels_[Cmd]);
    EchoUntil_ = ImGui::GetTime() + 3.2;
    CommandText_[0] = '\0';
    Ghost_[0]       = '\0';
    SugShut_        = true;
    PaintSuggestions();
}

int ViewportPanel::ConsoleCallback(ImGuiInputTextCallbackData* Edit) noexcept
{
    auto* Self = static_cast<ViewportPanel*>(Edit->UserData);
    if (Edit->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        if (Self->Ghost_[0] != '\0')
        {
            Edit->InsertChars(Edit->BufTextLen, Self->Ghost_);
        }
    }
    else if (Edit->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (Self->CommandText_[0] == '\0' && Self->PastCount_ > 0u)
        {
            // An empty line walks the earlier lines, newest first.
            if (Edit->EventKey == ImGuiKey_UpArrow)
            {
                if (Self->PastAt_ + 1 < static_cast<int32_t>(Self->PastCount_))
                {
                    ++Self->PastAt_;
                }
            }
            else if (Self->PastAt_ > -1)
            {
                --Self->PastAt_;
            }
            const char* Line = (Self->PastAt_ < 0)
                ? ""
                : Self->CommandPast_[Self->PastCount_ - 1u - static_cast<uint32_t>(Self->PastAt_)];
            Edit->DeleteChars(0, Edit->BufTextLen);
            Edit->InsertChars(0, Line);
        }
        else
        {
            if (Edit->EventKey == ImGuiKey_UpArrow)
            {
                if (Self->SugIndex_ > 0u)
                {
                    --Self->SugIndex_;
                }
            }
            else if (Self->SugIndex_ + 1u < Self->SugCount_)
            {
                ++Self->SugIndex_;
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           FOOTER
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordFooter(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 40.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();
    Draw->AddRectFilled(Cursor, ImVec2(Cursor.x + RowWidth, Cursor.y + 40.0f), kWash);
    Draw->AddLine(ImVec2(Cursor.x, Cursor.y), ImVec2(Cursor.x + RowWidth, Cursor.y), kStroke);

    // The counters. Tris and camera wait on engine counters, so they keep the reference's own pre-paint
    //    dash; physics and isolation hide at zero, under the reference's show rule.
    const char* Dash = "\xe2\x80\x94";
    char PerfText[16] = {}, PerfSub[16] = {}, EntsText[16] = {}, EntsSub[32] = {};
    char DayText[16] = {}, DaySub[16] = {};
    const float Fps = ImGui::GetIO().Framerate;
    std::snprintf(PerfText, sizeof(PerfText), "%.0f", static_cast<double>(Fps));
    std::snprintf(PerfSub, sizeof(PerfSub), "%.1f ms",
        static_cast<double>(Fps > 0.0f ? 1000.0f / Fps : 0.0f));
    uint32_t Shown = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Visible)
        {
            ++Shown;
        }
    }
    std::snprintf(EntsText, sizeof(EntsText), "%u", InstanceCount);
    std::snprintf(EntsSub, sizeof(EntsSub), "%u visible", Shown);
    const float DayT = (ClockHours_ - 6.0f) / 12.0f * 3.14159265f;
    const float Elev = 62.0f * std::sin(DayT);
    float DayFactor = (std::sin(Elev * 3.14159265f / 180.0f) + 0.10f) / 0.34f;
    DayFactor = DayFactor < 0.0f ? 0.0f : (DayFactor > 1.0f ? 1.0f : DayFactor);
    std::snprintf(DayText, sizeof(DayText), "%d%%", static_cast<int>(std::round(DayFactor * 100.0f)));
    std::snprintf(DaySub, sizeof(DaySub), "%d\xc2\xb0", static_cast<int>(std::round(Elev)));

    struct Counter
    {
        const char* Label;
        const char* Text;
        const char* Sub;
        bool        Warn;
        bool        Dim;
        bool        Opt;
    };
    const Counter Counters[5] = {
        { "FPS", PerfText, PerfSub, Fps > 0.0f && Fps < 24.0f, false, false },
        { "TRIS", Dash, "", false, false, false },
        { "INSTANCES", EntsText, EntsSub, false, false, false },
        { "DAYLIGHT", DayText, DaySub, false, false, false },
        { "CAMERA", Dash, "", false, true, true },
    };

    ImGui::PushFont(Small);
    const float DotW  = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, " \xc2\xb7 ").x;
    const float TextH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Ag").y;
    ImGui::PopFont();
    const auto CounterWidth = [&](const Counter& Cell, bool WithSub) -> float
    {
        float W = SpacedCapsWidth(Small, Cell.Label, 1.2f) + 6.0f;
        ImGui::PushFont(Small);
        W += Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Cell.Text).x;
        if (WithSub && Cell.Sub[0] != '\0')
        {
            W += DotW + Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Cell.Sub).x;
        }
        ImGui::PopFont();
        return W;
    };

    // fitStats: the camera cell steps aside first, then the secondary halves.
    uint32_t Mode = 2u;
    float TodW = 132.0f;
    for (uint32_t Try = 0u; Try < 3u; ++Try)
    {
        const bool KeepCam = (Try == 0u);
        const bool KeepSub = (Try < 2u);
        float W = 10.0f;
        for (uint32_t i = 0u; i < 5u; ++i)
        {
            if (Counters[i].Opt && !KeepCam)
            {
                continue;
            }
            W += CounterWidth(Counters[i], KeepSub) + 20.0f;
        }
        W -= 10.0f;   // the last cell keeps its left pad only; the tod gap follows
        float Tod = RowWidth - W - 10.0f;
        Tod = Tod < 132.0f ? 132.0f : (Tod > 236.0f ? 236.0f : Tod);
        if (W + 10.0f + Tod <= RowWidth || Try == 2u)
        {
            Mode = Try;
            TodW = Tod;
            break;
        }
    }

    const bool  KeepCam = (Mode == 0u);
    const bool  KeepSub = (Mode < 2u);
    const float TextY   = Cursor.y + (40.0f - TextH) * 0.5f;
    const float MidY    = Cursor.y + 20.0f;
    float X = Cursor.x + 10.0f;
    bool First = true;
    for (uint32_t i = 0u; i < 5u; ++i)
    {
        if (Counters[i].Opt && !KeepCam)
        {
            continue;
        }
        if (!First)
        {
            Draw->AddLine(ImVec2(X - 10.0f, MidY - 6.0f), ImVec2(X - 10.0f, MidY + 6.0f), kStroke);
        }
        First = false;
        const float LabelW = SpacedCapsWidth(Small, Counters[i].Label, 1.2f);
        SpacedCaps(Draw, Small, Counters[i].Label, ImVec2(X, TextY), kFaint, 1.2f);
        const ImU32 TextTint = Counters[i].Warn ? kAmber : (Counters[i].Dim ? kDim : kText);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(X + LabelW + 6.0f, TextY), TextTint, Counters[i].Text);
        const float TextW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Counters[i].Text).x;
        if (KeepSub && Counters[i].Sub[0] != '\0')
        {
            char Joined[48] = {};
            std::snprintf(Joined, sizeof(Joined), " \xc2\xb7 %s", Counters[i].Sub);
            Draw->AddText(ImVec2(X + LabelW + 6.0f + TextW, TextY), kFaint, Joined);
        }
        ImGui::PopFont();
        X += CounterWidth(Counters[i], KeepSub) + 20.0f;
    }

    const float TodX1 = Cursor.x + RowWidth - 10.0f;
    const float TodX0 = TodX1 - TodW;
    const float TodY  = Cursor.y + 6.0f;
    Draw->AddRectFilled(ImVec2(TodX0, TodY), ImVec2(TodX1, TodY + 28.0f), kInset, 14.0f);
    Draw->AddRect(ImVec2(TodX0, TodY), ImVec2(TodX1, TodY + 28.0f), kStroke, 14.0f);

    char Clock[8] = {};
    const int Hours   = static_cast<int>(ClockHours_);
    const int Minutes = static_cast<int>((ClockHours_ - static_cast<float>(Hours)) * 60.0f);
    std::snprintf(Clock, sizeof(Clock), "%02d:%02d", Hours, Minutes);
    ImGui::PushFont(Small);
    const ImVec2 ClockGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Clock);
    Draw->AddText(ImVec2(TodX0 + 12.0f, TodY + (28.0f - ClockGlyph.y) * 0.5f), kText, Clock);
    ImGui::PopFont();
    const float ClockW = ClockGlyph.x > 48.0f ? ClockGlyph.x : 48.0f;

    const float SlotX0 = TodX0 + 12.0f + ClockW + 10.0f;
    const float SlotX1 = TodX1 - 6.0f - 28.0f - 10.0f;
    if (SlotX1 - SlotX0 > 20.0f)
    {
        ImGui::SetCursorScreenPos(ImVec2(SlotX0, TodY - 1.0f));
        ImGui::BeginChild("##todslot", ImVec2(SlotX1 - SlotX0, 28.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Controls_->SliderPill("##tod", &ClockHours_, 0.0f, 24.0f, 2u, "h", false, true, false);
        ImGui::EndChild();
    }

    const ImVec2 TodPlayMin(TodX1 - 6.0f - 28.0f, TodY);
    ImGui::SetCursorScreenPos(TodPlayMin);
    ImGui::InvisibleButton("##todplay", ImVec2(28.0f, 28.0f));
    const bool TodHot = ImGui::IsItemHovered();
    if (TodHot)
    {
        ImGui::SetTooltip("Run the day cycle");
        if (ImGui::IsMouseClicked(0))
        {
            DayCycle_ = !DayCycle_;
            if (DayCycle_ && !Realtime_)
            {
                SetRealtime(true);
            }
        }
    }
    const ImVec2 TodCentre(TodPlayMin.x + 14.0f, TodPlayMin.y + 14.0f);
    if (DayCycle_)
    {
        Draw->AddCircleFilled(TodCentre, 14.0f, IM_COL32(255, 255, 255, 255));
    }
    else if (TodHot)
    {
        Draw->AddCircleFilled(TodCentre, 14.0f, kHover);
    }
    if (DayCycle_)
    {
        PauseGlyph(Draw, TodCentre, 12.0f, IM_COL32(0, 0, 0, 255));
    }
    else
    {
        PlayGlyph(Draw, TodCentre, 12.0f, TodHot ? kText : kDim);
    }
}

} // namespace Frontier
