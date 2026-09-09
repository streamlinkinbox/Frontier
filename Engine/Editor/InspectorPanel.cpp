//============================================================================================================================================
//                                                    INSPECTORPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor inspector — the picked record as a property sheet.

#include "InspectorPanel.h"

#include "EditorKit.h"
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

float ProwHeight(EditorPropertyKind Kind) noexcept
{
    switch (Kind)
    {
    case EditorPropertyKind::Slider:   return 26.0f;
    case EditorPropertyKind::Switch:   return 26.0f;
    case EditorPropertyKind::AxisVec3: return 26.0f;
    case EditorPropertyKind::Colour:   return 26.0f;
    case EditorPropertyKind::Select:   return 30.0f;
    case EditorPropertyKind::Readout:  return 18.0f;
    default:                           return 26.0f;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::AssignKit(EditorKit* Kit) noexcept
{
    Kit_ = Kit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::Record(EditorRecord* Picked, uint32_t PickedIndex, EditorSheet* Sheet) noexcept
{
    IM_ASSERT(Kit_ != nullptr);
    if (!ImGui::Begin("Inspector", nullptr))
    {
        ImGui::End();
        return;
    }

    if (PickedIndex != SheetFor_)
    {
        SheetFor_ = PickedIndex;
        NameFor_  = kNoEditorRecord;
        for (uint32_t i = 0u; i < 8u; ++i)
        {
            CardShut_[i] = false;
        }
    }

    if (Picked == nullptr || Sheet == nullptr)
    {
        RecordEmpty();
        ImGui::End();
        return;
    }

    RecordIdent(Picked, PickedIndex);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##props", ImVec2(0.0f, 0.0f), false);
    for (uint32_t i = 0u; i < Sheet->GroupCount && i < kMaxEditorSheetGroups; ++i)
    {
        RecordCard(Sheet->Groups[i], i);
    }
    RecordStanding(Picked, PickedIndex);
    RecordNotes(Picked);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LETTERSPACED CAPS
//------------------------------------------------------------------------------------------------------------------------

float InspectorPanel::RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept
{
    ImFont*     Small = Kit_->QuerySmall();
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

    ImFont* Ui    = Kit_->QueryUi();
    ImFont* Small = Kit_->QuerySmall();
    ImGui::PushFont(Ui);
    const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Nothing selected");
    Draw->AddText(ImVec2(Min.x + (RowWidth - TitleGlyph.x) * 0.5f, Min.y + 38.0f), kDim, "Nothing selected");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 HintGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Pick a record in the outliner.");
    Draw->AddText(ImVec2(Min.x + (RowWidth - HintGlyph.x) * 0.5f, Min.y + 62.0f), kFaint,
        "Pick a record in the outliner.");
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           IDENT
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordIdent(EditorRecord* Picked, uint32_t PickedIndex) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 56.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Kit_->QueryUi();
    ImFont*     Small = Kit_->QuerySmall();

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
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    if (NameDone || NameEdited)
    {
        std::snprintf(Picked->Label, sizeof(Picked->Label), "%s", NameText_);
    }

    char KindUpper[24] = {};
    const char* KindName = EditorKindLabel(Picked->Kind);
    for (uint32_t i = 0u; i < sizeof(KindUpper) - 1u && KindName[i] != '\0'; ++i)
    {
        KindUpper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(KindName[i])));
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
    const float KindAdvance = RecordCaps(KindUpper, ImVec2(NameX + 2.0f, Cursor.y + 30.0f), kDim);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(NameX + 2.0f + KindAdvance, Cursor.y + 30.0f), kFaint, Suffix);
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
            H += ProwHeight(Group.Properties[i].Kind) + 8.0f;
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
        ImFont* Small = Kit_->QuerySmall();
        const float BodyX = Min.x + 14.0f;
        const float BodyW = RowWidth - 28.0f;
        const float ZoneX = BodyX + 78.0f;
        const float ZoneW = BodyW - 78.0f;
        float Y = Min.y + 12.0f + 24.0f + 8.0f;

        for (uint32_t i = 0u; i < Group.PropertyCount; ++i)
        {
            EditorProperty& Prop = Group.Properties[i];
            const float ProwH = ProwHeight(Prop.Kind);

            ImGui::PushFont(Small);
            const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Prop.Label);
            Draw->AddText(ImVec2(BodyX, Y + (ProwH - LabelGlyph.y) * 0.5f), kDim, Prop.Label);
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
            char ProwId[12] = {};
            std::snprintf(ProwId, sizeof(ProwId), "##p%u", i);
            ImGui::BeginChild(ProwId, ImVec2(ZoneW, ProwH), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            switch (Prop.Kind)
            {
            case EditorPropertyKind::Slider:
                Kit_->SliderPill("##s", &Prop.Figure, Prop.Minimum, Prop.Maximum, Prop.Decimals, Prop.Unit, Prop.Hi);
                break;
            case EditorPropertyKind::Switch:
                ImGui::SetCursorScreenPos(ImVec2(ZoneX + ZoneW - 33.0f, Y + 3.5f));
                Kit_->Switch("##w", &Prop.On);
                break;
            case EditorPropertyKind::AxisVec3:
                Kit_->AxisVec3("##v", Prop.Axes, Prop.AxisStep, Prop.Editable);
                break;
            case EditorPropertyKind::Colour:
                if (Prop.Swatches)
                {
                    Kit_->SwatchRow("##t", Prop.ColourTint);
                }
                else
                {
                    Kit_->ColourChip("##c", Prop.ColourTint);
                }
                break;
            case EditorPropertyKind::Select:
                Kit_->DropDown("##d", &Prop.Picked, Prop.Options, Prop.OptionCount);
                break;
            case EditorPropertyKind::Readout:
                Kit_->Readout(Prop.Text);
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

void InspectorPanel::RecordStanding(EditorRecord* Picked, uint32_t PickedIndex) noexcept
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

    RecordCaps("Record", ImVec2(Min.x + 14.0f, Min.y + 14.0f), kDim);

    ImFont* Small = Kit_->QuerySmall();
    const char* Pills[4] = { "VISIBLE", "LOCKED", "DYNAMIC", "PHYSICS" };
    bool* Flags[4] = { &Picked->Visible, &Picked->Locked, &Picked->Dynamic, &Picked->Physics };
    float PX = Min.x + 14.0f;
    const float PY = Min.y + 40.0f;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Pills[i]);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(PX, PY));
        ImGui::PushID(static_cast<int>(10 + i));
        Kit_->PillToggle(Pills[i], Flags[i]);
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
    Kit_->Readout(EditorKindLabel(Picked->Kind));
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
    Kit_->Readout(IdText);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          NOTES
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordNotes(EditorRecord* Picked) noexcept
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
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
        ImGui::PushFont(Kit_->QueryUi());
        ImGui::InputTextMultiline("##notes", Picked->Notes, sizeof(Picked->Notes), ImVec2(BodyW, 64.0f));
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

} // namespace Frontier
