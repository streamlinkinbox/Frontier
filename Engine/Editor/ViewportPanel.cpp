//============================================================================================================================================
//                                                    VIEWPORTPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column.

#include "ViewportPanel.h"

#include "ControlPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cstdio>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kView   = IM_COL32(7, 9, 12, 255);
constexpr ImU32 kTile   = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);
constexpr ImU32 kFill   = IM_COL32(122, 122, 122, 255);
constexpr ImU32 kThumb  = IM_COL32(224, 224, 224, 255);
constexpr ImU32 kGreen  = IM_COL32(105, 208, 109, 255);
constexpr ImU32 kAmber  = IM_COL32(245, 158, 11, 255);

const char* kViewNames[3] = { "Perspective", "Orthographic", "Top" };

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

void ViewportPanel::Record(uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin("Viewport", nullptr))
    {
        ImGui::End();
        return;
    }

    RecordBar();
    RecordView();
    RecordCommand();
    RecordFooter(InstanceCount);
    ImGui::End();
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

    float BX = Cursor.x + RowWidth;
    const char* ChipText = "STOPPED";
    ImU32 ChipTint = kFaint;
    if (Playing_ && !Paused_)
    {
        ChipText = "REALTIME";
        ChipTint = kGreen;
    }
    else if (Playing_ && Paused_)
    {
        ChipText = "PAUSED";
        ChipTint = kAmber;
    }
    ImGui::PushFont(Small);
    const ImVec2 ChipGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, ChipText);
    ImGui::PopFont();
    const float ChipW = ChipGlyph.x + 20.0f;
    BX -= ChipW;
    Draw->AddRectFilled(ImVec2(BX, BtnY + 3.0f), ImVec2(BX + ChipW, BtnY + 25.0f),
        IM_COL32(255, 255, 255, 12), 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(BX + 10.0f, BtnY + 3.0f + (22.0f - ChipGlyph.y) * 0.5f), ChipTint, ChipText);
    ImGui::PopFont();
    BX -= 8.0f;

    for (int i = 3; i >= 0; --i)
    {
        BX -= 28.0f;
        const ImVec2 BtnMin(BX, BtnY);
        ImGui::SetCursorScreenPos(BtnMin);
        char TransId[10] = {};
        std::snprintf(TransId, sizeof(TransId), "##t%d", i);
        ImGui::InvisibleButton(TransId, ImVec2(28.0f, 28.0f));
        const bool Hot = ImGui::IsItemHovered();
        if (Hot && ImGui::IsMouseClicked(0))
        {
            if (i == 0)
            {
                SimOn_ = !SimOn_;
            }
            else if (i == 1)
            {
                Playing_ = true;
                Paused_  = false;
            }
            else if (i == 2)
            {
                if (Playing_)
                {
                    Paused_ = true;
                }
            }
            else
            {
                Playing_ = false;
                Paused_  = false;
            }
        }
        const bool Active = (i == 0) ? SimOn_ : (i == 1) ? (Playing_ && !Paused_)
            : (i == 2)   ? (Playing_ && Paused_)
                         : (!Playing_ && !Paused_);
        const ImVec2 Centre(BX + 14.0f, BtnY + 14.0f);
        Draw->AddCircleFilled(Centre, 14.0f,
            Active ? IM_COL32(26, 26, 26, 255) : (Hot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12)));
        if (Active)
        {
            Draw->AddCircle(Centre, 14.0f, kStrong, 0, 1.2f);
        }
        const ImU32 GlyphTint = Active ? kText : kDim;
        if (i == 0)
        {
            Draw->AddBezierCubic(ImVec2(Centre.x - 6.0f, Centre.y + 1.0f),
                ImVec2(Centre.x - 3.0f, Centre.y - 5.0f), ImVec2(Centre.x, Centre.y + 5.0f),
                ImVec2(Centre.x + 3.0f, Centre.y - 1.0f), GlyphTint, 1.6f);
            Draw->AddBezierCubic(ImVec2(Centre.x + 3.0f, Centre.y - 1.0f),
                ImVec2(Centre.x + 4.0f, Centre.y + 1.0f), ImVec2(Centre.x + 5.0f, Centre.y + 1.0f),
                ImVec2(Centre.x + 6.0f, Centre.y - 1.0f), GlyphTint, 1.6f);
        }
        else if (i == 1)
        {
            Draw->AddTriangleFilled(ImVec2(Centre.x - 3.5f, Centre.y - 5.0f),
                ImVec2(Centre.x - 3.5f, Centre.y + 5.0f), ImVec2(Centre.x + 4.5f, Centre.y), GlyphTint);
        }
        else if (i == 2)
        {
            Draw->AddRectFilled(ImVec2(Centre.x - 4.5f, Centre.y - 5.0f),
                ImVec2(Centre.x - 1.5f, Centre.y + 5.0f), GlyphTint, 1.0f);
            Draw->AddRectFilled(ImVec2(Centre.x + 1.5f, Centre.y - 5.0f),
                ImVec2(Centre.x + 4.5f, Centre.y + 5.0f), GlyphTint, 1.0f);
        }
        else
        {
            Draw->AddRectFilled(ImVec2(Centre.x - 4.5f, Centre.y - 4.5f),
                ImVec2(Centre.x + 4.5f, Centre.y + 4.5f), GlyphTint, 1.5f);
        }
        BX -= 8.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE VIEW
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordView() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float ViewH    = ImGui::GetContentRegionAvail().y - 40.0f - 8.0f - 30.0f - 8.0f;
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
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 40.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    Draw->AddRectFilled(Min, Max, IM_COL32(0, 0, 0, 255), 10.0f);
    Draw->AddRect(Min, Max, CommandFocus_ ? kStrong : kStroke, 10.0f);

    ImGui::PushFont(Ui);
    const ImVec2 PromptGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, ">");
    Draw->AddText(ImVec2(Min.x + 14.0f, Min.y + (40.0f - PromptGlyph.y) * 0.5f), kDim, ">");
    ImGui::PopFont();

    ImGui::PushFont(Small);
    const ImVec2 KbdGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Cmd+K");
    ImGui::PopFont();
    const float KbdW = KbdGlyph.x + 14.0f;
    const ImVec2 KbdMin(Max.x - 12.0f - KbdW, Min.y + 10.0f);
    const ImVec2 KbdMax(Max.x - 12.0f, Min.y + 30.0f);
    Draw->AddRectFilled(KbdMin, KbdMax, IM_COL32(255, 255, 255, 12), 4.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(KbdMin.x + 7.0f, KbdMin.y + (20.0f - KbdGlyph.y) * 0.5f), kFaint, "Cmd+K");
    ImGui::PopFont();

    const float FieldX = Min.x + 32.0f;
    ImGui::SetCursorScreenPos(ImVec2(FieldX, Min.y + 6.0f));
    ImGui::PushItemWidth(KbdMin.x - 8.0f - FieldX);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));
    ImGui::PushFont(Ui);
    if (FocusCommand_)
    {
        ImGui::SetKeyboardFocusHere();
        FocusCommand_ = false;
    }
    const bool Done = ImGui::InputTextWithHint("##cmd", "Type a command\xe2\x80\xa6",
        CommandText_, sizeof(CommandText_), ImGuiInputTextFlags_EnterReturnsTrue);
    CommandFocus_ = ImGui::IsItemFocused();
    if (ImGui::IsItemActivated())
    {
        CommandEcho_[0] = '\0';
    }
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();

    if (Done && CommandText_[0] != '\0')
    {
        std::snprintf(CommandEcho_, sizeof(CommandEcho_), "'%s' does nothing yet", CommandText_);
        CommandText_[0] = '\0';
    }
    if (CommandEcho_[0] != '\0' && CommandText_[0] == '\0')
    {
        ImGui::PushFont(Small);
        const ImVec2 EchoGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CommandEcho_);
        Draw->AddText(ImVec2(FieldX + 2.0f, Min.y + (40.0f - EchoGlyph.y) * 0.5f), kFaint, CommandEcho_);
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           FOOTER
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordFooter(uint32_t InstanceCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Mono = Controls_->QueryMonoSmall();

    Draw->AddRectFilled(Cursor, ImVec2(Cursor.x + RowWidth, Cursor.y + 30.0f), kWash);
    Draw->AddLine(ImVec2(Cursor.x, Cursor.y), ImVec2(Cursor.x + RowWidth, Cursor.y), kStroke);

    const float Fps = ImGui::GetIO().Framerate;
    char Stats[64] = {};
    std::snprintf(Stats, sizeof(Stats), "%.0f FPS \xc2\xb7 %.1f MS \xc2\xb7 %u INSTANCES",
        static_cast<double>(Fps), static_cast<double>(Fps > 0.0f ? 1000.0f / Fps : 0.0f), InstanceCount);
    ImGui::PushFont(Mono);
    const ImVec2 StatsGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Stats);
    Draw->AddText(ImVec2(Cursor.x, Cursor.y + (30.0f - StatsGlyph.y) * 0.5f), kDim, Stats);
    ImGui::PopFont();

    char Clock[8] = {};
    const int Hours   = static_cast<int>(ClockHours_);
    const int Minutes = static_cast<int>((ClockHours_ - static_cast<float>(Hours)) * 60.0f);
    std::snprintf(Clock, sizeof(Clock), "%02d:%02d", Hours, Minutes);
    ImGui::PushFont(Mono);
    const ImVec2 ClockGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Clock);
    ImGui::PopFont();
    const float ClockW = ClockGlyph.x + 8.0f;
    constexpr float kTrackW = 120.0f;
    const float TrackX1 = Cursor.x + RowWidth - ClockW;
    const float TrackX0 = TrackX1 - kTrackW;
    const float TrackY  = Cursor.y + 15.0f;

    ImGui::SetCursorScreenPos(ImVec2(TrackX0, Cursor.y + 2.0f));
    ImGui::InvisibleButton("##clock", ImVec2(kTrackW, 26.0f));
    if (ImGui::IsItemActive())
    {
        const float MouseX = ImGui::GetIO().MousePos.x;
        float Next = (MouseX - TrackX0 - 9.0f) / (kTrackW - 18.0f) * 24.0f;
        ClockHours_ = Next < 0.0f ? 0.0f : (Next > 24.0f ? 24.0f : Next);
    }

    const float Travel0 = TrackX0 + 9.0f;
    const float Travel1 = TrackX1 - 9.0f;
    const float KnobX = Travel0 + (ClockHours_ / 24.0f) * (Travel1 - Travel0);
    Draw->AddRectFilled(ImVec2(TrackX0, TrackY - 5.0f), ImVec2(TrackX1, TrackY + 5.0f), IM_COL32(34, 34, 34, 255), 5.0f);
    Draw->AddRectFilled(ImVec2(TrackX0, TrackY - 5.0f), ImVec2(KnobX, TrackY + 5.0f), kFill, 5.0f);
    Draw->AddCircleFilled(ImVec2(KnobX, TrackY), 9.0f, kThumb);
    ImGui::PushFont(Mono);
    Draw->AddText(ImVec2(TrackX1 + 8.0f, Cursor.y + (30.0f - ClockGlyph.y) * 0.5f), kDim, Clock);
    ImGui::PopFont();
}

} // namespace Frontier
