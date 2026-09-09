//============================================================================================================================================
//                                                     OUTLINERPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor outliner — the record register as a tree.

#include "OutlinerPanel.h"

#include "EditorKit.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kTile   = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kHover  = IM_COL32(28, 28, 28, 255);
constexpr ImU32 kSeated = IM_COL32(42, 42, 42, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kHi     = IM_COL32(108, 119, 255, 255);
constexpr ImU32 kDynBg  = IM_COL32(108, 119, 255, 46);
constexpr ImU32 kDynTx  = IM_COL32(154, 162, 255, 255);
constexpr ImU32 kPillBg = IM_COL32(255, 255, 255, 15);

bool ContainsFolded(const char* Hay, const char* Needle) noexcept
{
    if (Needle[0] == '\0')
    {
        return true;
    }
    for (; *Hay != '\0'; ++Hay)
    {
        const char* H = Hay;
        const char* N = Needle;
        while (*N != '\0' && *H != '\0'
            && std::tolower(static_cast<unsigned char>(*H)) == std::tolower(static_cast<unsigned char>(*N)))
        {
            ++H;
            ++N;
        }
        if (*N == '\0')
        {
            return true;
        }
    }
    return false;
}

const char* FindFolded(const char* Hay, const char* Needle) noexcept
{
    if (Needle[0] == '\0')
    {
        return nullptr;
    }
    for (; *Hay != '\0'; ++Hay)
    {
        const char* H = Hay;
        const char* N = Needle;
        while (*N != '\0' && *H != '\0'
            && std::tolower(static_cast<unsigned char>(*H)) == std::tolower(static_cast<unsigned char>(*N)))
        {
            ++H;
            ++N;
        }
        if (*N == '\0')
        {
            return Hay;
        }
    }
    return nullptr;
}

const char* KindLabel(EditorRecordKind Kind) noexcept
{
    switch (Kind)
    {
    case EditorRecordKind::Folder:   return "Folder";
    case EditorRecordKind::Geometry: return "Geometry";
    case EditorRecordKind::Light:    return "Light";
    case EditorRecordKind::Camera:   return "Camera";
    case EditorRecordKind::Sky:      return "Sky";
    case EditorRecordKind::Sun:      return "Sun";
    case EditorRecordKind::Moon:     return "Moon";
    default:                         return "?";
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::AssignKit(EditorKit* Kit) noexcept
{
    Kit_ = Kit;
}

uint32_t OutlinerPanel::QueryPicked() const noexcept
{
    return PickedCount_ > 0u ? Picked_[0] : kNoEditorRecord;
}

uint32_t OutlinerPanel::QueryPickedCount() const noexcept
{
    return PickedCount_;
}

uint32_t OutlinerPanel::QueryPickedAt(uint32_t Slot) const noexcept
{
    return Slot < PickedCount_ ? Picked_[Slot] : kNoEditorRecord;
}

void OutlinerPanel::PickRecord(uint32_t Index) noexcept
{
    Picked_[0]   = Index;
    PickedCount_ = 1u;
    Anchor_      = Index;
}

bool OutlinerPanel::IsPicked(uint32_t Index) const noexcept
{
    for (uint32_t i = 0u; i < PickedCount_; ++i)
    {
        if (Picked_[i] == Index)
        {
            return true;
        }
    }
    return false;
}

void OutlinerPanel::AddPick(uint32_t Index) noexcept
{
    if (!IsPicked(Index) && PickedCount_ < kMaxEditorPicked)
    {
        Picked_[PickedCount_++] = Index;
    }
}

void OutlinerPanel::RemovePick(uint32_t Index) noexcept
{
    for (uint32_t i = 0u; i < PickedCount_; ++i)
    {
        if (Picked_[i] == Index)
        {
            for (uint32_t j = i; j + 1u < PickedCount_; ++j)
            {
                Picked_[j] = Picked_[j + 1u];
            }
            --PickedCount_;
            return;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::Record(EditorRecord* Records, uint32_t RecordCount) noexcept
{
    IM_ASSERT(Kit_ != nullptr);
    if (!ImGui::Begin("Outliner", nullptr))
    {
        ImGui::End();
        return;
    }
    if (RecordCount > kMaxEditorRecords)
    {
        RecordCount = kMaxEditorRecords;
    }

    uint32_t EntityCount = 0u;
    uint32_t GroupCount  = 0u;
    for (uint32_t i = 0u; i < RecordCount; ++i)
    {
        if (Records[i].Kind == EditorRecordKind::Folder)
        {
            ++GroupCount;
        }
        else
        {
            ++EntityCount;
        }
    }

    RecordHeader(Records, RecordCount, EntityCount, GroupCount);
    RecordSearch();
    RecordChips();
    const uint32_t Hits = RecordTree(Records, RecordCount);
    RecordFooter(Hits, RecordCount);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          HEADER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordHeader(EditorRecord* Records, uint32_t RecordCount,
                                 uint32_t EntityCount, uint32_t GroupCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 44.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Kit_->QueryUi();
    ImFont*     Small = Kit_->QuerySmall();

    const ImVec2 TileMax(Cursor.x + 28.0f, Cursor.y + 36.0f);
    Draw->AddRectFilled(ImVec2(Cursor.x, Cursor.y + 4.0f), TileMax, kTile, 8.0f);
    Draw->AddRect(ImVec2(Cursor.x, Cursor.y + 4.0f), TileMax, kStroke, 8.0f);
    for (uint32_t i = 0u; i < 3u; ++i)
    {
        const float LineY = Cursor.y + 13.0f + i * 6.0f;
        Draw->AddLine(ImVec2(Cursor.x + 8.0f, LineY), ImVec2(Cursor.x + 20.0f, LineY), kDim, 2.0f);
    }

    ImGui::PushFont(Ui);
    Draw->AddText(ImVec2(Cursor.x + 38.0f, Cursor.y + 2.0f), kText, "Outliner");
    ImGui::PopFont();

    char Sub[48] = {};
    std::snprintf(Sub, sizeof(Sub), "%u entities \xc2\xb7 %u groups", EntityCount, GroupCount);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(Cursor.x + 38.0f, Cursor.y + 21.0f), kDim, Sub);
    ImGui::PopFont();

    const float EndX = Cursor.x + RowWidth;
    bool AnyOpen = false;
    for (uint32_t i = 0u; i < RecordCount; ++i)
    {
        if (Records[i].Kind == EditorRecordKind::Folder && !FolderShut_[i])
        {
            AnyOpen = true;
            break;
        }
    }

    const ImVec2 FoldMin(EndX - 64.0f, Cursor.y + 8.0f);
    ImGui::SetCursorScreenPos(FoldMin);
    ImGui::InvisibleButton("##fold", ImVec2(28.0f, 28.0f));
    const bool FoldHot = ImGui::IsItemHovered();
    if (FoldHot && ImGui::IsMouseClicked(0))
    {
        for (uint32_t i = 0u; i < RecordCount; ++i)
        {
            if (Records[i].Kind == EditorRecordKind::Folder)
            {
                FolderShut_[i] = AnyOpen;
            }
        }
    }
    const ImVec2 FoldCentre(FoldMin.x + 14.0f, FoldMin.y + 14.0f);
    Draw->AddCircleFilled(FoldCentre, 14.0f, FoldHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
    const float ChevY = AnyOpen ? -2.5f : 2.5f;
    Draw->AddLine(ImVec2(FoldCentre.x - 5.0f, FoldCentre.y - ChevY),
        ImVec2(FoldCentre.x, FoldCentre.y + ChevY), kText, 1.8f);
    Draw->AddLine(ImVec2(FoldCentre.x, FoldCentre.y + ChevY),
        ImVec2(FoldCentre.x + 5.0f, FoldCentre.y - ChevY), kText, 1.8f);

    // The plus stays decorative: the feed owns the register, and creation lands with the project binding.
    const ImVec2 PlusMin(EndX - 28.0f, Cursor.y + 8.0f);
    const ImVec2 PlusCentre(PlusMin.x + 14.0f, PlusMin.y + 14.0f);
    Draw->AddCircleFilled(PlusCentre, 14.0f, IM_COL32(255, 255, 255, 8));
    Draw->AddLine(ImVec2(PlusCentre.x - 5.0f, PlusCentre.y), ImVec2(PlusCentre.x + 5.0f, PlusCentre.y), kFaint, 1.8f);
    Draw->AddLine(ImVec2(PlusCentre.x, PlusCentre.y - 5.0f), ImVec2(PlusCentre.x, PlusCentre.y + 5.0f), kFaint, 1.8f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SEARCH
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordSearch() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kDdWidth = 120.0f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.03f, 0.03f, 0.03f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
    ImGui::PushItemWidth(RowWidth - kDdWidth - 8.0f);
    ImGui::InputTextWithHint("##query", "Search records\xe2\x80\xa6", QueryText_, sizeof(QueryText_));
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 8.0f);
    const ImVec2 DdMin = ImGui::GetCursorScreenPos();
    const float  DdW   = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##kindmenu", ImVec2(DdW, 32.0f));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        ImGui::OpenPopup("##kinds");
    }

    uint32_t PickCount = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorRecordKind::Count); ++i)
    {
        if (KindPicked_[i])
        {
            ++PickCount;
        }
    }
    char DdLabel[24] = {};
    if (PickCount == 0u)
    {
        std::snprintf(DdLabel, sizeof(DdLabel), "All types");
    }
    else
    {
        std::snprintf(DdLabel, sizeof(DdLabel), "%u types", PickCount);
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 DdMax(DdMin.x + DdW, DdMin.y + 32.0f);
    Draw->AddRectFilled(DdMin, DdMax, IM_COL32(0, 0, 0, 255), 16.0f);
    Draw->AddRect(DdMin, DdMax, kStroke, 16.0f);
    ImFont* Ui = Kit_->QueryUi();
    ImGui::PushFont(Ui);
    const ImVec2 LabelGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, DdLabel);
    Draw->AddText(ImVec2(DdMin.x + 13.0f, DdMin.y + (32.0f - LabelGlyph.y) * 0.5f), kText, DdLabel);
    ImGui::PopFont();
    const ImVec2 Chev(DdMin.x + DdW - 16.0f, DdMin.y + 16.0f);
    Draw->AddLine(ImVec2(Chev.x - 4.0f, Chev.y - 1.5f), ImVec2(Chev.x, Chev.y + 2.5f), kDim, 1.6f);
    Draw->AddLine(ImVec2(Chev.x, Chev.y + 2.5f), ImVec2(Chev.x + 4.0f, Chev.y - 1.5f), kDim, 1.6f);

    ImGui::SetNextWindowSize(ImVec2(180.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.102f, 0.102f, 0.102f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.180f, 0.180f, 0.180f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    if (ImGui::BeginPopup("##kinds"))
    {
        ImGui::PushFont(Ui);
        const float MenuWidth = ImGui::GetContentRegionAvail().x;
        for (int32_t i = -1; i < static_cast<int32_t>(EditorRecordKind::Count); ++i)
        {
            const char* Label = (i < 0) ? "All types" : KindLabel(static_cast<EditorRecordKind>(i));
            const bool  Ticked = (i < 0) ? (PickCount == 0u) : KindPicked_[i];
            ImGui::Dummy(ImVec2(MenuWidth, 28.0f));
            const ImVec2 RowMin = ImGui::GetItemRectMin();
            const ImVec2 RowMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(RowMin);
            char RowId[12] = {};
            std::snprintf(RowId, sizeof(RowId), "##k%di", i);
            ImGui::InvisibleButton(RowId, ImVec2(MenuWidth, 28.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered)
            {
                Draw->AddRectFilled(RowMin, RowMax, IM_COL32(36, 36, 36, 255), 6.0f);
            }
            if (Hovered && ImGui::IsMouseClicked(0))
            {
                if (i < 0)
                {
                    for (uint32_t k = 0u; k < static_cast<uint32_t>(EditorRecordKind::Count); ++k)
                    {
                        KindPicked_[k] = false;
                    }
                }
                else
                {
                    KindPicked_[i] = !KindPicked_[i];
                }
            }
            const ImVec2 OptGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Label);
            Draw->AddText(ImVec2(RowMin.x + 30.0f, RowMin.y + (28.0f - OptGlyph.y) * 0.5f),
                Ticked ? kText : kDim, Label);
            if (Ticked)
            {
                const ImVec2 Tick(RowMin.x + 14.0f, RowMin.y + 14.0f);
                Draw->AddLine(ImVec2(Tick.x - 5.0f, Tick.y), ImVec2(Tick.x - 1.0f, Tick.y + 4.0f), kText, 2.0f);
                Draw->AddLine(ImVec2(Tick.x - 1.0f, Tick.y + 4.0f), ImVec2(Tick.x + 5.0f, Tick.y - 4.0f), kText, 2.0f);
            }
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           CHIPS
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordChips() noexcept
{
    uint32_t PickCount = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorRecordKind::Count); ++i)
    {
        if (KindPicked_[i])
        {
            ++PickCount;
        }
    }
    if (PickCount == 0u)
    {
        return;
    }

    // Flow layout: buttons place themselves and wrap by the tracked X, so no cursor jump ever extends
    //    the boundaries. After the last button the cursor already sits at the next line's start.
    ImFont*     Small = Kit_->QuerySmall();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    const float StartX = ImGui::GetCursorScreenPos().x;
    const float EndX   = StartX + ImGui::GetContentRegionAvail().x;
    float X = StartX;
    bool  FreshLine = true;

    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorRecordKind::Count); ++i)
    {
        if (!KindPicked_[i])
        {
            continue;
        }
        char Chip[32] = {};
        std::snprintf(Chip, sizeof(Chip), "%s \xc3\x97", KindLabel(static_cast<EditorRecordKind>(i)));
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Chip);
        ImGui::PopFont();
        const float ChipW = Glyph.x + 20.0f;
        if (!FreshLine && X + 6.0f + ChipW > EndX)
        {
            FreshLine = true;
        }
        if (!FreshLine)
        {
            ImGui::SameLine(0.0f, 6.0f);
        }
        char ChipId[12] = {};
        std::snprintf(ChipId, sizeof(ChipId), "##c%u", i);
        ImGui::InvisibleButton(ChipId, ImVec2(ChipW, 22.0f));
        const bool Hovered = ImGui::IsItemHovered();
        if (Hovered && ImGui::IsMouseClicked(0))
        {
            KindPicked_[i] = false;
        }
        const ImVec2 ChipMin = ImGui::GetItemRectMin();
        const ImVec2 ChipMax = ImGui::GetItemRectMax();
        Draw->AddRectFilled(ChipMin, ChipMax, Hovered ? IM_COL32(44, 44, 44, 255) : IM_COL32(36, 36, 36, 255), 11.0f);
        Draw->AddRect(ChipMin, ChipMax, kStroke, 11.0f);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(ChipMin.x + 10.0f, ChipMin.y + (22.0f - Glyph.y) * 0.5f), kText, Chip);
        ImGui::PopFont();
        if (FreshLine)
        {
            X = StartX + ChipW;
            FreshLine = false;
        }
        else
        {
            X += 6.0f + ChipW;
        }
    }

    ImGui::PushFont(Small);
    const ImVec2 ClearGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Clear");
    ImGui::PopFont();
    const float ClearW = ClearGlyph.x + 8.0f;
    if (!FreshLine && X + 6.0f + ClearW > EndX)
    {
        FreshLine = true;
    }
    if (!FreshLine)
    {
        ImGui::SameLine(0.0f, 6.0f);
    }
    ImGui::InvisibleButton("##clearchips", ImVec2(ClearW, 22.0f));
    const bool ClearHot = ImGui::IsItemHovered();
    if (ClearHot && ImGui::IsMouseClicked(0))
    {
        for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorRecordKind::Count); ++i)
        {
            KindPicked_[i] = false;
        }
    }
    const ImVec2 ClearMin = ImGui::GetItemRectMin();
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(ClearMin.x + 4.0f, ClearMin.y + (22.0f - ClearGlyph.y) * 0.5f),
        ClearHot ? kText : kDim, "Clear");
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           TREE
//------------------------------------------------------------------------------------------------------------------------

uint32_t OutlinerPanel::RecordTree(EditorRecord* Records, uint32_t RecordCount) noexcept
{
    bool SelfMatch[kMaxEditorRecords] = {};
    bool Shown[kMaxEditorRecords]     = {};

    const bool QueryOn = QueryText_[0] != '\0';
    bool AnyPick = false;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorRecordKind::Count); ++i)
    {
        AnyPick = AnyPick || KindPicked_[i];
    }
    const bool MatchOn = QueryOn || AnyPick;

    for (uint32_t i = 0u; i < RecordCount; ++i)
    {
        const bool NameHit = !QueryOn || ContainsFolded(Records[i].Label, QueryText_);
        const bool KindHit = !AnyPick || KindPicked_[static_cast<uint32_t>(Records[i].Kind)];
        SelfMatch[i] = NameHit && KindHit;
        Shown[i]     = SelfMatch[i];
    }
    for (uint32_t i = RecordCount; i-- > 0u;)
    {
        if (Records[i].Kind != EditorRecordKind::Folder)
        {
            continue;
        }
        for (uint32_t j = i + 1u; j < RecordCount && Records[j].Depth > Records[i].Depth; ++j)
        {
            if (Shown[j])
            {
                Shown[i] = true;
                break;
            }
        }
    }

    uint32_t HitCount = 0u;
    for (uint32_t i = 0u; i < RecordCount; ++i)
    {
        if (Shown[i])
        {
            ++HitCount;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##tree", ImVec2(0.0f, -30.0f), false);
    if (HitCount == 0u)
    {
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(Avail);
        const ImVec2 EmptyMin = ImGui::GetItemRectMin();
        ImFont* Small = Kit_->QuerySmall();
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Nothing matches");
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(EmptyMin.x + (Avail.x - Glyph.x) * 0.5f, EmptyMin.y + 24.0f), kDim, "Nothing matches");
        ImGui::PopFont();
    }
    else
    {
        for (uint32_t i = 0u; i < RecordCount;)
        {
            if (!Shown[i])
            {
                ++i;
                continue;
            }
            RecordRow(Records, RecordCount, i, MatchOn);
            if (Records[i].Kind == EditorRecordKind::Folder && FolderShut_[i] && !MatchOn)
            {
                const uint32_t ShutDepth = Records[i].Depth;
                do
                {
                    ++i;
                } while (i < RecordCount && Records[i].Depth > ShutDepth);
            }
            else
            {
                ++i;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    return HitCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           ROW
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordRow(EditorRecord* Records, uint32_t RecordCount, uint32_t Index, bool MatchOn) noexcept
{
    EditorRecord& Row = Records[Index];
    ImGui::PushID(static_cast<int>(Index));

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();
    const bool IsFolder = (Row.Kind == EditorRecordKind::Folder);

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##row", ImVec2(RowWidth, 30.0f));
    const bool RowHot = ImGui::IsItemHovered();

    const float Indent = static_cast<float>(Row.Depth) * 14.0f;
    float X = Min.x + 6.0f + Indent;

    bool TwirlHit = false;
    bool TwirlHot = false;
    if (IsFolder)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, Min.y));
        ImGui::InvisibleButton("##twirl", ImVec2(14.0f, 30.0f));
        TwirlHot = ImGui::IsItemHovered();
        TwirlHit = TwirlHot && ImGui::IsMouseClicked(0);
        X += 14.0f;
    }

    float BX = Max.x - 4.0f;
    BX -= 19.0f;
    const ImVec2 VisMin(BX, Min.y + 5.5f);
    ImGui::SetCursorScreenPos(VisMin);
    ImGui::InvisibleButton("##vis", ImVec2(19.0f, 19.0f));
    const bool VisHot = ImGui::IsItemHovered();
    const bool VisHit = VisHot && ImGui::IsMouseClicked(0);
    BX -= 4.0f;

    BX -= 19.0f;
    const ImVec2 LockMin(BX, Min.y + 5.5f);
    ImGui::SetCursorScreenPos(LockMin);
    ImGui::InvisibleButton("##lock", ImVec2(19.0f, 19.0f));
    const bool LockHot = ImGui::IsItemHovered();
    const bool LockHit = LockHot && ImGui::IsMouseClicked(0);
    BX -= 4.0f;

    BX -= 19.0f;
    const ImVec2 SoloMin(BX, Min.y + 5.5f);
    ImGui::SetCursorScreenPos(SoloMin);
    ImGui::InvisibleButton("##solo", ImVec2(19.0f, 19.0f));
    const bool SoloHot = ImGui::IsItemHovered();
    const bool SoloHit = SoloHot && ImGui::IsMouseClicked(0);
    BX -= 6.0f;

    const bool AnyHot = RowHot || TwirlHot || VisHot || LockHot || SoloHot;
    const bool Seated = IsPicked(Index);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    if (Seated)
    {
        Draw->AddRectFilled(Min, Max, kSeated, 9.0f);
        Draw->AddRectFilled(ImVec2(Min.x, Min.y + 5.0f), ImVec2(Min.x + 2.0f, Max.y - 5.0f),
            IM_COL32(255, 255, 255, 255), 1.0f);
    }
    else if (AnyHot)
    {
        Draw->AddRectFilled(Min, Max, kHover, 9.0f);
    }

    if (TwirlHit)
    {
        FolderShut_[Index] = !FolderShut_[Index];
    }
    else if (VisHit)
    {
        Row.Visible = !Row.Visible;
    }
    else if (LockHit)
    {
        Row.Locked = !Row.Locked;
    }
    else if (SoloHit)
    {
        Row.Solo = !Row.Solo;
    }
    else if (RowHot && ImGui::IsMouseClicked(0))
    {
        if (Renaming_ && RenameIndex_ != Index)
        {
            Renaming_ = false;
        }
        const bool Ctrl  = ImGui::GetIO().KeyCtrl;
        const bool Shift = ImGui::GetIO().KeyShift;
        if (Shift && Anchor_ != kNoEditorRecord && Anchor_ < RecordCount)
        {
            const uint32_t Lo = Anchor_ < Index ? Anchor_ : Index;
            const uint32_t Hi = Anchor_ < Index ? Index : Anchor_;
            for (uint32_t k = Lo; k <= Hi; ++k)
            {
                AddPick(k);
            }
        }
        else if (Ctrl)
        {
            if (IsPicked(Index))
            {
                RemovePick(Index);
            }
            else
            {
                AddPick(Index);
                Anchor_ = Index;
            }
        }
        else
        {
            Picked_[0]   = Index;
            PickedCount_ = 1u;
            Anchor_      = Index;
        }
        if (ImGui::IsMouseDoubleClicked(0))
        {
            Renaming_    = true;
            RenameIndex_ = Index;
            std::snprintf(RenameText_, sizeof(RenameText_), "%s", Row.Label);
            FocusRename_ = true;
        }
    }
    if (Seated)
    {
        if (ServedPick_ != Index)
        {
            ServedPick_ = Index;
            RevealTicks_ = 3;
        }
        if (RevealTicks_ > 0)
        {
            ImGui::SetScrollHereY(0.5f);
            --RevealTicks_;
        }
    }

    const float CentreY = Min.y + 15.0f;
    if (IsFolder)
    {
        const float Tx = Min.x + 6.0f + Indent;
        if (!FolderShut_[Index] || MatchOn)
        {
            Draw->AddTriangleFilled(ImVec2(Tx + 3.0f, CentreY - 3.5f), ImVec2(Tx + 11.0f, CentreY - 3.5f),
                ImVec2(Tx + 7.0f, CentreY + 3.5f), kDim);
        }
        else
        {
            Draw->AddTriangleFilled(ImVec2(Tx + 4.5f, CentreY - 4.0f), ImVec2(Tx + 4.5f, CentreY + 4.0f),
                ImVec2(Tx + 10.5f, CentreY), kDim);
        }
    }

    Draw->AddCircleFilled(ImVec2(X + 4.0f, CentreY), 4.0f,
        IM_COL32(static_cast<int>(Row.Tint[0] * 255.0f), static_cast<int>(Row.Tint[1] * 255.0f),
            static_cast<int>(Row.Tint[2] * 255.0f), 255));
    X += 12.0f;

    ImFont* Ui    = Kit_->QueryUi();
    ImFont* Small = Kit_->QuerySmall();
    ImGui::PushFont(Ui);
    const ImVec2 NameGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Row.Label);
    ImGui::PopFont();
    const float NameY = Min.y + (30.0f - NameGlyph.y) * 0.5f;

    if (Renaming_ && RenameIndex_ == Index)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, Min.y + 4.0f));
        ImGui::PushItemWidth(BX - X > 40.0f ? BX - X : 40.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushFont(Ui);
        if (FocusRename_)
        {
            ImGui::SetKeyboardFocusHere();
            FocusRename_ = false;
        }
        const bool Done = ImGui::InputText("##rename", RenameText_, sizeof(RenameText_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
        if (Done)
        {
            std::snprintf(Row.Label, sizeof(Row.Label), "%s", RenameText_);
            Renaming_ = false;
        }
        else if (ImGui::IsItemDeactivated())
        {
            Renaming_ = false;
        }
    }
    else
    {
        if (QueryText_[0] != '\0')
        {
            const char* At = FindFolded(Row.Label, QueryText_);
            if (At != nullptr)
            {
                ImGui::PushFont(Ui);
                const ImVec2 PreGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Row.Label, At);
                const ImVec2 HitGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, At, At + std::strlen(QueryText_));
                ImGui::PopFont();
                Draw->AddRectFilled(ImVec2(X + PreGlyph.x, NameY),
                    ImVec2(X + PreGlyph.x + HitGlyph.x, NameY + NameGlyph.y), IM_COL32(108, 119, 255, 110));
            }
        }
        ImGui::PushFont(Ui);
        Draw->AddText(ImVec2(X, NameY), Row.Visible ? kText : kFaint, Row.Label);
        ImGui::PopFont();
    }

    float BadgeX = X + NameGlyph.x + 8.0f;
    if (IsFolder)
    {
        char CountText[8] = {};
        std::snprintf(CountText, sizeof(CountText), "%u", Row.KidCount);
        ImGui::PushFont(Small);
        const ImVec2 CountGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CountText);
        ImGui::PopFont();
        const float BadgeW = CountGlyph.x + 12.0f;
        if (BadgeX + BadgeW < BX)
        {
            Draw->AddRectFilled(ImVec2(BadgeX, CentreY - 8.0f), ImVec2(BadgeX + BadgeW, CentreY + 8.0f), kPillBg, 4.0f);
            ImGui::PushFont(Small);
            Draw->AddText(ImVec2(BadgeX + 6.0f, CentreY - 8.0f + (16.0f - CountGlyph.y) * 0.5f), kDim, CountText);
            ImGui::PopFont();
            BadgeX += BadgeW + 6.0f;
        }
    }
    if (Row.Dynamic)
    {
        ImGui::PushFont(Small);
        const ImVec2 DynGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "DYN");
        ImGui::PopFont();
        const float BadgeW = DynGlyph.x + 12.0f;
        if (BadgeX + BadgeW < BX)
        {
            Draw->AddRectFilled(ImVec2(BadgeX, CentreY - 8.0f), ImVec2(BadgeX + BadgeW, CentreY + 8.0f), kDynBg, 4.0f);
            ImGui::PushFont(Small);
            Draw->AddText(ImVec2(BadgeX + 6.0f, CentreY - 8.0f + (16.0f - DynGlyph.y) * 0.5f), kDynTx, "DYN");
            ImGui::PopFont();
            BadgeX += BadgeW + 6.0f;
        }
    }
    if (Row.Physics)
    {
        ImGui::PushFont(Small);
        const ImVec2 PhysGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "PHYS");
        ImGui::PopFont();
        const float BadgeW = PhysGlyph.x + 12.0f;
        if (BadgeX + BadgeW < BX)
        {
            Draw->AddRectFilled(ImVec2(BadgeX, CentreY - 8.0f), ImVec2(BadgeX + BadgeW, CentreY + 8.0f), kPillBg, 4.0f);
            ImGui::PushFont(Small);
            Draw->AddText(ImVec2(BadgeX + 6.0f, CentreY - 8.0f + (16.0f - PhysGlyph.y) * 0.5f), kDim, "PHYS");
            ImGui::PopFont();
        }
    }

    const ImVec2 SoloCentre(SoloMin.x + 9.5f, SoloMin.y + 9.5f);
    const ImU32 SoloTint = Row.Solo ? kHi : (SoloHot ? kDim : kFaint);
    Draw->AddCircle(SoloCentre, 5.0f, SoloTint, 0, 1.6f);
    Draw->AddCircleFilled(SoloCentre, 1.6f, SoloTint);

    const ImVec2 LockCentre(LockMin.x + 9.5f, LockMin.y + 9.5f);
    const ImU32 LockTint = Row.Locked ? kText : (LockHot ? kDim : kFaint);
    Draw->AddCircle(ImVec2(LockCentre.x, LockCentre.y - 1.5f), 3.2f, LockTint, 0, 1.6f);
    Draw->AddRectFilled(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), kSeated, 2.0f);
    Draw->AddRect(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), LockTint, 2.0f, 0, 1.4f);

    const ImVec2 VisCentre(VisMin.x + 9.5f, VisMin.y + 9.5f);
    const ImU32 VisTint = Row.Visible ? (VisHot ? kText : kDim) : kFaint;
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y - 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y - 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y + 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y + 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    if (Row.Visible)
    {
        Draw->AddCircleFilled(VisCentre, 1.8f, VisTint);
    }
    else
    {
        Draw->AddLine(ImVec2(VisCentre.x - 6.5f, VisCentre.y + 6.0f),
            ImVec2(VisCentre.x + 6.5f, VisCentre.y - 6.0f), kFaint, 1.6f);
    }

    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FOOTER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordFooter(uint32_t HitCount, uint32_t TotalCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddLine(ImVec2(Cursor.x, Cursor.y), ImVec2(Cursor.x + RowWidth, Cursor.y), IM_COL32(34, 34, 40, 255));

    char Left[32] = {};
    if (PickedCount_ > 0u)
    {
        std::snprintf(Left, sizeof(Left), "%u selected", PickedCount_);
    }
    else
    {
        std::snprintf(Left, sizeof(Left), "Nothing selected");
    }
    char Right[32] = {};
    std::snprintf(Right, sizeof(Right), "%u of %u", HitCount, TotalCount);

    ImFont* Small = Kit_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 RightGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Right);
    Draw->AddText(ImVec2(Cursor.x, Cursor.y + (30.0f - RightGlyph.y) * 0.5f), kDim, Left);
    Draw->AddText(ImVec2(Cursor.x + RowWidth - RightGlyph.x, Cursor.y + (30.0f - RightGlyph.y) * 0.5f), kDim, Right);
    ImGui::PopFont();
}

} // namespace Frontier
