//============================================================================================================================================
//                                                 CONTROLCENTREPANEL.CPP
//============================================================================================================================================
// 🧩 The Control Centre shade. The strip owns its height: the drag walks the target between shut and open,
//    a tap toggles, and the sheet eases toward the target every tick while the window clips the reveal.

#include "ControlCentrePanel.h"

#include <cstdio>

#include <imgui_internal.h>

#include "ControlPanel.h"

namespace Frontier {

namespace {

constexpr ImU32 kShade  = IM_COL32(22, 22, 25, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 26);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kText   = IM_COL32(229, 229, 229, 255);
constexpr ImU32 kOk     = IM_COL32(34, 197, 94, 255);
constexpr ImU32 kOff    = IM_COL32(82, 82, 82, 255);

} // namespace

void ControlCentrePanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

void ControlCentrePanel::AssignOpen(bool* Open) noexcept
{
    Open_ = Open;
}

void ControlCentrePanel::Record() noexcept
{
    ImGuiViewport* Main = ImGui::GetMainViewport();

    // The sheet eases toward its target; the drag below owns the target while held, the open figure
    //    otherwise, so the gear and the drag never fight.
    if (!Dragging_ && Open_ != nullptr)
    {
        Target_ = *Open_ ? kExpandedH : kCollapsedH;
    }
    const float Delta = ImGui::GetIO().DeltaTime;
    if (Delta <= 0.0f)
    {
        Height_ = Target_;
    }
    else
    {
        Height_ += (Target_ - Height_) * ((Delta * 10.0f < 1.0f) ? (Delta * 10.0f) : 1.0f);
        if (Height_ > Target_ - 0.5f && Height_ < Target_ + 0.5f)
        {
            Height_ = Target_;
        }
    }

    ImGui::SetNextWindowPos(Main->Pos);
    ImGui::SetNextWindowSize(ImVec2(Main->Size.x, Height_));
    constexpr ImGuiWindowFlags kShadeFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoNavFocus;
    if (!ImGui::Begin("##controlcentre", nullptr, kShadeFlags))
    {
        ImGui::End();
        return;
    }
    // The shade always draws above the dock columns: the host records it last, and the bring-forward
    //    holds it there when a docked window takes focus.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImDrawList* Draw     = ImGui::GetWindowDrawList();
    ImFont*     Small    = Controls_->QuerySmall();
    const ImVec2 Pos     = ImGui::GetWindowPos();
    const float  Width   = Main->Size.x;
    const float  MidY    = kCollapsedH * 0.5f;

    Draw->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + Height_), kShade, 12.0f,
        ImDrawFlags_RoundCornersBottom);
    Draw->AddLine(ImVec2(Pos.x + 12.0f, Pos.y + Height_ - 0.5f),
        ImVec2(Pos.x + Width - 12.0f, Pos.y + Height_ - 0.5f), kStroke);

    // The strip: the title left, the notch pill centred, the path summary right. The whole strip is the
    //    drag handle; the rows below only show once the sheet slides open.
    ImGui::SetCursorScreenPos(Pos);
    ImGui::InvisibleButton("##shadedrag", ImVec2(Width, kCollapsedH));
    if (ImGui::IsItemActivated())
    {
        Dragging_        = true;
        DragStartY_      = ImGui::GetIO().MousePos.y;
        DragStartTarget_ = Target_;
        DragMoved_       = 0.0f;
    }
    if (Dragging_ && ImGui::IsItemActive())
    {
        const float Pull = ImGui::GetIO().MousePos.y - DragStartY_;
        DragMoved_ = (Pull > DragMoved_) ? Pull : ((Pull < -DragMoved_) ? -Pull : DragMoved_);
        Target_ = DragStartTarget_ + Pull;
        Target_ = (Target_ < kCollapsedH) ? kCollapsedH : ((Target_ > kExpandedH) ? kExpandedH : Target_);
        if (Open_ != nullptr)
        {
            *Open_ = Target_ > (kCollapsedH + kExpandedH) * 0.5f;
        }
    }
    if (Dragging_ && ImGui::IsItemDeactivated())
    {
        Dragging_ = false;
        if (DragMoved_ < 6.0f)
        {
            // A tap toggles; a drag snaps to whichever half it reached.
            Target_ = (Target_ > (kCollapsedH + kExpandedH) * 0.5f) ? kCollapsedH : kExpandedH;
        }
        else
        {
            Target_ = (Target_ > (kCollapsedH + kExpandedH) * 0.5f) ? kExpandedH : kCollapsedH;
        }
        if (Open_ != nullptr)
        {
            *Open_ = (Target_ == kExpandedH);
        }
    }
    NotchX_ = Pos.x + Width * 0.5f;
    NotchY_ = Pos.y + MidY;

    ImGui::PushFont(Small);
    const ImVec2 TitleGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Control");
    Draw->AddText(ImVec2(Pos.x + 14.0f, Pos.y + MidY - TitleGlyph.y * 0.5f), kDim, "Control");
    char ExpoText[16] = {};
    std::snprintf(ExpoText, sizeof(ExpoText), "%+.1f ev", static_cast<double>(Exposure_));
    const ImVec2 ExpoGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, ExpoText);
    const ImVec2 GiGlyph   = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "GI");
    Draw->AddText(ImVec2(Pos.x + Width - 14.0f - ExpoGlyph.x, Pos.y + MidY - ExpoGlyph.y * 0.5f), kDim,
        ExpoText);
    Draw->AddText(
        ImVec2(Pos.x + Width - 14.0f - ExpoGlyph.x - 12.0f - GiGlyph.x, Pos.y + MidY - GiGlyph.y * 0.5f),
        GiOn_ ? kText : kDim, "GI");
    ImGui::PopFont();
    Draw->AddCircleFilled(
        ImVec2(Pos.x + Width - 14.0f - ExpoGlyph.x - 12.0f - GiGlyph.x - 10.0f, Pos.y + MidY), 3.0f,
        GiOn_ ? kOk : kOff);
    Draw->AddRectFilled(ImVec2(NotchX_ - 22.0f, Pos.y + MidY - 2.5f),
        ImVec2(NotchX_ + 22.0f, Pos.y + MidY + 2.5f), kStroke, 2.5f);

    // The sheet rows draw only once the sheet slides: while shut they would sit invisible over the dock
    //    host, clickable through the strip. GI seats the render path with the path it seated as the
    //    subtitle, and exposure trims the view for lookdev.
    if (Height_ <= kCollapsedH + 1.0f)
    {
        ImGui::End();
        return;
    }
    float Y = Pos.y + kCollapsedH + 12.0f;
    ImGui::PushFont(Small);
    const ImVec2 GiRowGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Global illumination");
    Draw->AddText(ImVec2(Pos.x + 16.0f, Y), kText, "Global illumination");
    Draw->AddText(ImVec2(Pos.x + 16.0f, Y + GiRowGlyph.y + 2.0f), kDim,
        GiOn_ ? "Full ReSTIR" : "Visibility raster");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(Pos.x + Width - 16.0f - 46.0f, Y + 2.0f));
    Controls_->Switch("##ccgi", &GiOn_);
    GiX_ = (ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f;
    GiY_ = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
    Y += 52.0f;

    ImGui::PushFont(Small);
    const ImVec2 ExpoRowGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Exposure");
    Draw->AddText(ImVec2(Pos.x + 16.0f, Y + (27.0f - ExpoRowGlyph.y) * 0.5f), kText, "Exposure");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(Pos.x + 150.0f, Y));
    ImGui::BeginChild("##ccexpozone", ImVec2(Width - 150.0f - 16.0f, 27.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Controls_->SliderPill("##ccexpo", &Exposure_, -3.0f, +3.0f, 2u, "ev", true, false, true);
    ImGui::EndChild();
    Y += 44.0f;

    Draw->AddRectFilled(ImVec2(NotchX_ - 22.0f, Y + 2.0f), ImVec2(NotchX_ + 22.0f, Y + 7.0f), kStroke,
        2.5f);

    ImGui::End();
}

} // namespace Frontier
