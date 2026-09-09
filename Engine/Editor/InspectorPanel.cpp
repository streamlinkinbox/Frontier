//============================================================================================================================================
//                                                    INSPECTORPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor inspector — the picked instance as a property sheet.

#include "InspectorPanel.h"

#include "ControlPanel.h"
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kInset  = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);

float ProwHeight(EditorPropertyCategory Category) noexcept
{
    switch (Category)
    {
    case EditorPropertyCategory::Slider:   return 30.0f;
    case EditorPropertyCategory::Switch:   return 26.0f;
    case EditorPropertyCategory::AxisVec3: return 26.0f;
    case EditorPropertyCategory::Colour:   return 26.0f;
    case EditorPropertyCategory::Select:   return 32.0f;
    case EditorPropertyCategory::Readout:  return 18.0f;
    default:                           return 26.0f;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::Record(EditorInstance* Picked, uint32_t PickedIndex, EditorSheet* Sheet) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin("Inspector", nullptr))
    {
        ImGui::End();
        return;
    }

    if (PickedIndex != SheetFor_)
    {
        SheetFor_ = PickedIndex;
        NameFor_  = kNoEditorInstance;
        for (uint32_t i = 0u; i < 8u; ++i)
        {
            CardShut_[i] = false;
        }
    }

    if (Picked == nullptr || Sheet == nullptr)
    {
        RecordEmpty();
        const float Gap = ImGui::GetContentRegionAvail().y - 30.0f;
        if (Gap > 0.0f)
        {
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, Gap));
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8.0f);
        RecordFooter(nullptr);
        ImGui::End();
        return;
    }

    RecordIdent(Picked, PickedIndex);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##props", ImVec2(0.0f, -30.0f), false);
    for (uint32_t i = 0u; i < Sheet->GroupCount && i < kMaxEditorSheetGroups; ++i)
    {
        RecordCard(Sheet->Groups[i], i);
    }
    RecordStanding(Picked, PickedIndex);
    RecordNotes(Picked);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8.0f);
    RecordFooter(Picked);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LETTERSPACED CAPS
//------------------------------------------------------------------------------------------------------------------------

float InspectorPanel::RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept
{
    ImFont*     Small = Controls_->QuerySmall();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImGui::PushFont(Small);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const char Upper[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(*P))), '\0' };
        Draw->AddText(ImVec2(At.x + Advance, At.y), Tint, Upper);
        Advance += Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Upper).x + 1.4f;
    }
    ImGui::PopFont();
    return Advance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           EMPTY
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordEmpty() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 120.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    ImFont* Ui    = Controls_->QueryUi();
    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Ui);
    const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Nothing selected");
    Draw->AddText(ImVec2(Min.x + (RowWidth - TitleGlyph.x) * 0.5f, Min.y + 38.0f), kDim, "Nothing selected");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 HintGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Pick an instance in the outliner.");
    Draw->AddText(ImVec2(Min.x + (RowWidth - HintGlyph.x) * 0.5f, Min.y + 62.0f), kFaint,
        "Pick an instance in the outliner.");
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           IDENT
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordIdent(EditorInstance* Picked, uint32_t PickedIndex) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 56.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();

    const ImVec2 HeadPos = ImGui::GetWindowPos();
    const float  HeadX0  = HeadPos.x;
    const float  HeadX1  = HeadPos.x + ImGui::GetWindowSize().x;
    Draw->AddRectFilled(ImVec2(HeadX0, Cursor.y), ImVec2(HeadX1, Cursor.y + 56.0f), kWash);
    Draw->AddLine(ImVec2(HeadX0, Cursor.y + 56.0f), ImVec2(HeadX1, Cursor.y + 56.0f), kStroke);

    const int R = static_cast<int>(Picked->Tint[0] * 255.0f);
    const int G = static_cast<int>(Picked->Tint[1] * 255.0f);
    const int B = static_cast<int>(Picked->Tint[2] * 255.0f);
    const ImVec2 TileMin(Cursor.x, Cursor.y + 10.0f);
    const ImVec2 TileMax(Cursor.x + 36.0f, Cursor.y + 46.0f);
    Draw->AddRectFilled(TileMin, TileMax, IM_COL32(R, G, B, 36), 10.0f);
    Draw->AddRect(TileMin, TileMax, IM_COL32(R, G, B, 110), 10.0f);
    Draw->AddCircleFilled(ImVec2(Cursor.x + 18.0f, Cursor.y + 28.0f), 5.0f, IM_COL32(R, G, B, 255));

    if (NameFor_ != PickedIndex)
    {
        std::snprintf(NameText_, sizeof(NameText_), "%s", Picked->Label);
        NameFor_ = PickedIndex;
    }

    const float NameX  = Cursor.x + 46.0f;
    const float BtnX   = Cursor.x + RowWidth - 64.0f;
    const float NameW  = BtnX - 8.0f - NameX;
    ImGui::SetCursorScreenPos(ImVec2(NameX, Cursor.y + 4.0f));
    ImGui::PushItemWidth(NameW > 40.0f ? NameW : 40.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    ImGui::PushFont(Ui);
    const bool NameDone = ImGui::InputText("##pickname", NameText_, sizeof(NameText_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool NameEdited = ImGui::IsItemDeactivatedAfterEdit();
    const ImVec2 NameMin = ImGui::GetItemRectMin();
    const ImVec2 NameMax = ImGui::GetItemRectMax();
    const bool NameHot = ImGui::IsItemFocused();
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    if (NameHot)
    {
        Draw->AddRect(NameMin, NameMax, kStrong, 6.0f);
    }
    if (NameDone || NameEdited)
    {
        std::snprintf(Picked->Label, sizeof(Picked->Label), "%s", NameText_);
    }

    char CategoryUpper[24] = {};
    const char* CategoryName = EditorInstanceLabel(Picked->Category);
    for (uint32_t i = 0u; i < sizeof(CategoryUpper) - 1u && CategoryName[i] != '\0'; ++i)
    {
        CategoryUpper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(CategoryName[i])));
    }
    char Suffix[32] = {};
    if (Picked->Locked && !Picked->Visible)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 locked \xc2\xb7 hidden");
    }
    else if (Picked->Locked)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 locked");
    }
    else if (!Picked->Visible)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 hidden");
    }
    const float CategoryAdvance = RecordCaps(CategoryUpper, ImVec2(NameX + 2.0f, Cursor.y + 30.0f), kDim);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(NameX + 2.0f + CategoryAdvance, Cursor.y + 30.0f), kFaint, Suffix);
    ImGui::PopFont();

    const ImVec2 LockMin(BtnX, Cursor.y + 14.0f);
    ImGui::SetCursorScreenPos(LockMin);
    ImGui::InvisibleButton("##identlock", ImVec2(28.0f, 28.0f));
    const bool LockHot = ImGui::IsItemHovered();
    if (LockHot && ImGui::IsMouseClicked(0))
    {
        Picked->Locked = !Picked->Locked;
    }
    const ImVec2 VisMin(BtnX + 36.0f, Cursor.y + 14.0f);
    ImGui::SetCursorScreenPos(VisMin);
    ImGui::InvisibleButton("##identvis", ImVec2(28.0f, 28.0f));
    const bool VisHot = ImGui::IsItemHovered();
    if (VisHot && ImGui::IsMouseClicked(0))
    {
        Picked->Visible = !Picked->Visible;
    }

    const ImVec2 LockCentre(LockMin.x + 14.0f, LockMin.y + 14.0f);
    Draw->AddCircleFilled(LockCentre, 14.0f, LockHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
    const ImU32 LockTint = Picked->Locked ? kText : kFaint;
    Draw->AddCircle(ImVec2(LockCentre.x, LockCentre.y - 1.5f), 3.2f, LockTint, 0, 1.6f);
    Draw->AddRectFilled(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), IM_COL32(30, 30, 30, 255), 2.0f);
    Draw->AddRect(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), LockTint, 2.0f, 0, 1.4f);

    const ImVec2 VisCentre(VisMin.x + 14.0f, VisMin.y + 14.0f);
    Draw->AddCircleFilled(VisCentre, 14.0f, VisHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
    const ImU32 VisTint = Picked->Visible ? kDim : kFaint;
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y - 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y - 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y + 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y + 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    if (Picked->Visible)
    {
        Draw->AddCircleFilled(VisCentre, 1.8f, VisTint);
    }
    else
    {
        Draw->AddLine(ImVec2(VisCentre.x - 6.5f, VisCentre.y + 6.0f),
            ImVec2(VisCentre.x + 6.5f, VisCentre.y - 6.0f), kFaint, 1.6f);
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + 56.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SCHEMA CARDS
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordCard(EditorPropertyGroup& Group, uint32_t Card) noexcept
{
    ImGui::PushID(static_cast<int>(Card));

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    float H = 12.0f + 24.0f + 10.0f;
    if (!CardShut_[Card])
    {
        H = 12.0f + 24.0f + 8.0f;
        for (uint32_t i = 0u; i < Group.PropertyCount; ++i)
        {
            H += ProwHeight(Group.Properties[i].Category) + 8.0f;
        }
        H += 14.0f - 8.0f;
    }

    ImGui::Dummy(ImVec2(RowWidth, H));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    const ImVec2 HeadMin(Min.x + 14.0f, Min.y + 12.0f);
    ImGui::SetCursorScreenPos(HeadMin);
    ImGui::InvisibleButton("##h4", ImVec2(RowWidth - 28.0f, 24.0f));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        CardShut_[Card] = !CardShut_[Card];
    }

    const float ChevY = HeadMin.y + 12.0f;
    if (CardShut_[Card])
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x + 1.0f, ChevY - 4.0f), ImVec2(HeadMin.x + 1.0f, ChevY + 4.0f),
            ImVec2(HeadMin.x + 7.0f, ChevY), kDim);
    }
    else
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x - 1.0f, ChevY - 2.5f), ImVec2(HeadMin.x + 9.0f, ChevY - 2.5f),
            ImVec2(HeadMin.x + 4.0f, ChevY + 3.5f), kDim);
    }
    RecordCaps(Group.Title, ImVec2(HeadMin.x + 14.0f, HeadMin.y + 5.0f), kDim);

    if (!CardShut_[Card])
    {
        ImFont* Small = Controls_->QuerySmall();
        const float BodyX = Min.x + 14.0f;
        const float BodyW = RowWidth - 28.0f;
        const float ZoneX = BodyX + 78.0f;
        const float ZoneW = BodyW - 78.0f;
        float Y = Min.y + 12.0f + 24.0f + 8.0f;

        for (uint32_t i = 0u; i < Group.PropertyCount; ++i)
        {
            EditorProperty& Prop = Group.Properties[i];
            const float ProwH = ProwHeight(Prop.Category);

            ImGui::PushFont(Small);
            const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Prop.Label);
            Draw->AddText(ImVec2(BodyX, Y + (ProwH - LabelGlyph.y) * 0.5f), kDim, Prop.Label);
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
            char ProwId[12] = {};
            std::snprintf(ProwId, sizeof(ProwId), "##p%u", i);
            ImGui::BeginChild(ProwId, ImVec2(ZoneW, ProwH), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            switch (Prop.Category)
            {
            case EditorPropertyCategory::Slider:
                Controls_->SliderPill("##s", &Prop.Figure, Prop.Minimum, Prop.Maximum, Prop.Decimals, Prop.Unit, Prop.Hi, false, true);
                break;
            case EditorPropertyCategory::Switch:
                ImGui::SetCursorScreenPos(ImVec2(ZoneX + ZoneW - 44.0f, Y + 3.5f));
                Controls_->Switch("##w", &Prop.On);
                break;
            case EditorPropertyCategory::AxisVec3:
                Controls_->AxisVec3("##v", Prop.Axes, Prop.AxisStep, Prop.Editable);
                break;
            case EditorPropertyCategory::Colour:
                if (Prop.Swatches)
                {
                    Controls_->SwatchRow("##t", Prop.ColourTint);
                }
                else
                {
                    Controls_->ColourChip("##c", Prop.ColourTint);
                }
                break;
            case EditorPropertyCategory::Select:
                Controls_->DropDown("##d", &Prop.Picked, Prop.Options, Prop.OptionCount);
                break;
            case EditorPropertyCategory::Readout:
                Controls_->Readout(Prop.Text);
                break;
            default:
                break;
            }
            ImGui::EndChild();

            Y += ProwH + 8.0f;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD STANDING
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordStanding(EditorInstance* Picked, uint32_t PickedIndex) noexcept
{
    ImGui::PushID(6);

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kH = 130.0f;
    ImGui::Dummy(ImVec2(RowWidth, kH));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    RecordCaps("Instance", ImVec2(Min.x + 14.0f, Min.y + 14.0f), kDim);

    ImFont* Small = Controls_->QuerySmall();
    const char* Pills[4] = { "VISIBLE", "LOCKED", "DYNAMIC", "PHYSICS" };
    bool* Standing[4] = { &Picked->Visible, &Picked->Locked, &Picked->Dynamic, &Picked->Physics };
    float PX = Min.x + 14.0f;
    const float PY = Min.y + 40.0f;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Pills[i]);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(PX, PY));
        ImGui::PushID(static_cast<int>(10 + i));
        Controls_->PillToggle(Pills[i], Standing[i]);
        ImGui::PopID();
        PX += Glyph.x + 20.0f + 8.0f;
    }

    const float BodyX = Min.x + 14.0f;
    const float ZoneX = BodyX + 78.0f;
    const float ZoneW = RowWidth - 28.0f - 78.0f;
    float Y = Min.y + 72.0f;

    ImGui::PushFont(Small);
    const ImVec2 TypeGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "TYPE");
    Draw->AddText(ImVec2(BodyX, Y), kDim, "TYPE");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
    ImGui::BeginChild("##spectype", ImVec2(ZoneW, 18.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Controls_->Readout(EditorInstanceLabel(Picked->Category));
    ImGui::EndChild();
    Y += 24.0f;

    char IdText[8] = {};
    std::snprintf(IdText, sizeof(IdText), "#%03u", PickedIndex);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(BodyX, Y), kDim, "ID");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
    ImGui::BeginChild("##specid", ImVec2(ZoneW, 18.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Controls_->Readout(IdText);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          NOTES
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordNotes(EditorInstance* Picked) noexcept
{
    ImGui::PushID(7);

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float H = CardShut_[7] ? (12.0f + 24.0f + 10.0f) : (12.0f + 24.0f + 8.0f + 64.0f + 14.0f);
    ImGui::Dummy(ImVec2(RowWidth, H));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    const ImVec2 HeadMin(Min.x + 14.0f, Min.y + 12.0f);
    ImGui::SetCursorScreenPos(HeadMin);
    ImGui::InvisibleButton("##h4n", ImVec2(RowWidth - 28.0f, 24.0f));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        CardShut_[7] = !CardShut_[7];
    }
    const float ChevY = HeadMin.y + 12.0f;
    if (CardShut_[7])
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x + 1.0f, ChevY - 4.0f), ImVec2(HeadMin.x + 1.0f, ChevY + 4.0f),
            ImVec2(HeadMin.x + 7.0f, ChevY), kDim);
    }
    else
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x - 1.0f, ChevY - 2.5f), ImVec2(HeadMin.x + 9.0f, ChevY - 2.5f),
            ImVec2(HeadMin.x + 4.0f, ChevY + 3.5f), kDim);
    }
    RecordCaps("Notes", ImVec2(HeadMin.x + 14.0f, HeadMin.y + 5.0f), kDim);

    if (!CardShut_[7])
    {
        const float BodyX = Min.x + 14.0f;
        const float BodyW = RowWidth - 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(BodyX, Min.y + 12.0f + 24.0f + 8.0f));
        ImGui::PushItemWidth(BodyW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, NotesFocus_
            ? ImVec4(0.180f, 0.180f, 0.180f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
        ImGui::PushFont(Controls_->QueryUi());
        ImGui::InputTextMultiline("##notes", Picked->Notes, sizeof(Picked->Notes), ImVec2(BodyW, 64.0f));
        NotesFocus_ = ImGui::IsItemFocused();
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           FOOTER
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordFooter(EditorInstance* Picked) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 FootPos  = ImGui::GetWindowPos();
    const ImVec2 FootSize = ImGui::GetWindowSize();
    const float  FootX0   = FootPos.x;
    const float  FootH    = FootPos.y + FootSize.y - Cursor.y;
    Draw->AddRectFilled(ImVec2(FootX0, Cursor.y), ImVec2(FootX0 + FootSize.x, Cursor.y + FootH), kWash);
    Draw->AddLine(ImVec2(FootX0, Cursor.y), ImVec2(FootX0 + FootSize.x, Cursor.y), kStroke);

    char Foot[64] = {};
    if (Picked == nullptr)
    {
        std::snprintf(Foot, sizeof(Foot), "\xe2\x80\x94");
    }
    else if (Picked->Locked)
    {
        std::snprintf(Foot, sizeof(Foot), "%s \xc2\xb7 %s \xc2\xb7 locked",
            EditorInstanceLabel(Picked->Category), Picked->Dynamic ? "dynamic" : "static");
    }
    else
    {
        std::snprintf(Foot, sizeof(Foot), "%s \xc2\xb7 %s",
            EditorInstanceLabel(Picked->Category), Picked->Dynamic ? "dynamic" : "static");
    }
    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 FootGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Foot);
    Draw->AddText(ImVec2(FootX0 + 14.0f, Cursor.y + (FootH - FootGlyph.y) * 0.5f), kFaint, Foot);
    ImGui::PopFont();
}

} // namespace Frontier