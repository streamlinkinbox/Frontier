//============================================================================================================================================
//                                                                CURVEPANEL.H
//============================================================================================================================================
// 📦 Bounded sampled curves with mouse and keyboard domain probes; no sample-array storage.

#pragma once

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier {

struct CurvePanel
{
    ImVec2 Origin;       // [px] - graph rectangle in screen coordinates
    ImVec2 Extent;       // [px] - rectangle including axis labels
    ImVec2 Minimum;      // [-]  - domain and ordinate lower bounds
    ImVec2 Maximum;      // [-]  - domain and ordinate upper bounds
    float  Probe = 0;    // [-]  - inspected domain coordinate, including hover
    bool   Hover = false;// [-]  - pointer inside this graph

    ImVec2 Project(float Domain, float Value) const
    {
        const float Horizontal = (Domain - Minimum.x) / (Maximum.x - Minimum.x);
        const float Vertical   = (Value - Minimum.y) / (Maximum.y - Minimum.y);
        return {Origin.x + 34 + Horizontal * (Extent.x - 44),
                Origin.y + 8 + (1 - std::clamp(Vertical, 0.0f, 1.0f)) * (Extent.y - 32)};
    }

    bool RecordProbe(
        const char* Identity,
        float&      Selection,
        float       Step,
        const char* Unit)
    {
        const ImVec2 Start = Project(Minimum.x, Maximum.y);
        const ImVec2 End   = Project(Maximum.x, Minimum.y);
        ImGui::SetCursorScreenPos(Start);
        ImGui::InvisibleButton(Identity, {End.x - Start.x, End.y - Start.y}, ImGuiButtonFlags_EnableNav);
        Hover = ImGui::IsItemHovered();
        const float Previous = Selection;
        if (!std::isfinite(Selection)) Selection = Minimum.x;
        if (ImGui::IsItemActivated())
        {
            ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
        }
        auto Pointer = [&]()
        {
            return std::clamp(Minimum.x + (ImGui::GetIO().MousePos.x - Start.x) /
                (End.x - Start.x) * (Maximum.x - Minimum.x), Minimum.x, Maximum.x);
        };
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) Selection = Pointer();
        if (ImGui::IsItemFocused())
        {
            for (const auto Key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End})
                ImGui::SetKeyOwner(Key, ImGui::GetItemID());
            const float Increment = ImGui::GetIO().KeyShift ? Step * 0.1f : Step;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Selection -= Increment;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Selection += Increment;
            if (ImGui::IsKeyPressed(ImGuiKey_Home)) Selection = Minimum.x;
            if (ImGui::IsKeyPressed(ImGuiKey_End)) Selection = Maximum.x;
        }
        Selection = std::clamp(Selection, Minimum.x, Maximum.x);
        Probe = Hover ? Pointer() : Selection;
        if (Selection != Previous) ImGui::MarkItemEdited(ImGui::GetItemID());
        auto* Commands = ImGui::GetWindowDrawList();
        Commands->AddRectFilled(Origin, {Origin.x + Extent.x, Origin.y + Extent.y}, IM_COL32(26, 29, 31, 255), 10);
        Commands->AddRect(Origin, {Origin.x + Extent.x, Origin.y + Extent.y},
            ImGui::IsItemFocused() ? IM_COL32(168, 189, 196, 255) : IM_COL32(54, 57, 59, 255), 10);
        char Text[48];
        for (int Index = 0; Index <= 2; ++Index)
        {
            const float Value = Minimum.y + (Maximum.y - Minimum.y) * Index / 2;
            const auto Position = Project(Minimum.x, Value);
            Commands->AddLine(Position, Project(Maximum.x, Value), IM_COL32(150, 170, 180, 35));
            std::snprintf(Text, sizeof(Text), "%.2g", double(Value));
            Commands->AddText(ImGui::GetFont(), 9, {Origin.x + 4, Position.y - 4}, IM_COL32(151, 157, 161, 255), Text);
            const float Domain = Minimum.x + (Maximum.x - Minimum.x) * Index / 2;
            std::snprintf(Text, sizeof(Text), "%.0f%s", double(Domain), Unit);
            const float Width = ImGui::GetFont()->CalcTextSizeA(9, 10000, 0, Text).x;
            const float PositionX = std::clamp(Project(Domain, Minimum.y).x - Width / 2, Origin.x + 4,
                Origin.x + Extent.x - Width - 4);
            Commands->AddText(ImGui::GetFont(), 9, {PositionX, End.y + 7}, IM_COL32(151, 157, 161, 255), Text);
        }
        Commands->AddLine(Project(Probe, Minimum.y), Project(Probe, Maximum.y), IM_COL32(216, 217, 201, 85));
        return Selection != Previous;
    }

    template<class Function>
    float RecordCurve(Function Sample, ImU32 Tint) const
    {
        auto* Commands = ImGui::GetWindowDrawList();
        ImVec2 Previous = Project(Minimum.x, Sample(Minimum.x));
        for (int Index = 1; Index <= 96; ++Index)
        {
            const float Domain = Minimum.x + (Maximum.x - Minimum.x) * Index / 96;
            const ImVec2 Position = Project(Domain, Sample(Domain));
            Commands->AddLine(Previous, Position, Tint, 1.5f);
            Previous = Position;
        }
        const float Value = Sample(Probe);
        Commands->AddCircleFilled(Project(Probe, Value), 3, Tint);
        return Value;
    }
};

}
