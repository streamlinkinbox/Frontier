//============================================================================================================================================
//                                                     OUTLINERPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor outliner — the instance roster as an outline.

#include "OutlinerPanel.h"

#include "ControlPanel.h"

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

constexpr ImU32 kTile   = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kHover  = IM_COL32(34, 34, 34, 255);
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
constexpr ImU32 kMenuHover = IM_COL32(20, 20, 20, 255);
constexpr ImU32 kMenuSel   = IM_COL32(24, 24, 24, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);
constexpr ImU32 kGuide  = IM_COL32(255, 255, 255, 15);

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


} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

uint32_t OutlinerPanel::QueryPicked() const noexcept
{
    return PickedCount_ > 0u ? Picked_[0] : kNoEditorInstance;
}

uint32_t OutlinerPanel::QueryPickedCount() const noexcept
{
    return PickedCount_;
}

uint32_t OutlinerPanel::QueryPickedAt(uint32_t Slot) const noexcept
{
    return Slot < PickedCount_ ? Picked_[Slot] : kNoEditorInstance;
}

void OutlinerPanel::PickInstance(uint32_t Index) noexcept
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

void OutlinerPanel::Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin("Outliner", nullptr))
    {
        ImGui::End();
        return;
    }
    if (InstanceCount > kMaxEditorInstances)
    {
        InstanceCount = kMaxEditorInstances;
    }

    uint32_t LeafCount = 0u;
    uint32_t GroupCount  = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Category == EditorInstanceCategory::Folder)
        {
            ++GroupCount;
        }
        else
        {
            ++LeafCount;
        }
    }

    RecordHeader(Instances, InstanceCount, LeafCount, GroupCount);
    RecordSearch();
    RecordChips();
    const uint32_t Hits = RecordOutline(Instances, InstanceCount);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8.0f);
    RecordFooter(Instances, InstanceCount, Hits);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          HEADER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordHeader(EditorInstance* Instances, uint32_t InstanceCount,
                                 uint32_t LeafCount, uint32_t GroupCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 44.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();

    const ImVec2 HeadPos  = ImGui::GetWindowPos();
    const float  HeadX0   = HeadPos.x;
    const float  HeadX1   = HeadPos.x + ImGui::GetWindowSize().x;
    Draw->AddRectFilled(ImVec2(HeadX0, Cursor.y), ImVec2(HeadX1, Cursor.y + 44.0f), kWash);
    Draw->AddLine(ImVec2(HeadX0, Cursor.y + 44.0f), ImVec2(HeadX1, Cursor.y + 44.0f), kStroke);

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
    std::snprintf(Sub, sizeof(Sub), "%u instances \xc2\xb7 %u groups", LeafCount, GroupCount);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(Cursor.x + 38.0f, Cursor.y + 21.0f), kDim, Sub);
    ImGui::PopFont();

    const float EndX = Cursor.x + RowWidth;
    bool AnyOpen = false;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Category == EditorInstanceCategory::Folder && !FolderShut_[i])
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
        for (uint32_t i = 0u; i < InstanceCount; ++i)
        {
            if (Instances[i].Category == EditorInstanceCategory::Folder)
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

    // The plus stays decorative: the feed owns the roster, and creation lands with the project write-back.
    const ImVec2 PlusMin(EndX - 28.0f, Cursor.y + 8.0f);
    const ImVec2 PlusCentre(PlusMin.x + 14.0f, PlusMin.y + 14.0f);
    Draw->AddCircleFilled(PlusCentre, 14.0f, IM_COL32(255, 255, 255, 8));
    Draw->AddLine(ImVec2(PlusCentre.x - 5.0f, PlusCentre.y), ImVec2(PlusCentre.x + 5.0f, PlusCentre.y), kFaint, 1.8f);
    Draw->AddLine(ImVec2(PlusCentre.x, PlusCentre.y - 5.0f), ImVec2(PlusCentre.x, PlusCentre.y + 5.0f), kFaint, 1.8f);
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + 44.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SEARCH
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordSearch() noexcept
{
    const ImVec2 At = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + 8.0f));
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kDdWidth = 120.0f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.03f, 0.03f, 0.03f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
        SearchFocus_ ? ImVec4(0.180f, 0.180f, 0.180f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(28.0f, 8.0f));
    ImGui::PushItemWidth(RowWidth - kDdWidth - 8.0f);
    ImGui::InputTextWithHint("##query", "Search instances\xe2\x80\xa6", QueryText_, sizeof(QueryText_));
    SearchFocus_ = ImGui::IsItemFocused();
    const ImVec2 FieldMin = ImGui::GetItemRectMin();
    const ImVec2 FieldMax = ImGui::GetItemRectMax();
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 8.0f);
    const ImVec2 DdMin = ImGui::GetCursorScreenPos();
    const float  DdW   = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##categorymenu", ImVec2(DdW, 32.0f));
    const bool DdHot = ImGui::IsItemHovered();
    if (DdHot && ImGui::IsMouseClicked(0))
    {
        ImGui::OpenPopup("##categories");
    }

    uint32_t PickCount = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        if (CategoryPicked_[i])
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
    const float GlassY = (FieldMin.y + FieldMax.y) * 0.5f;
    Draw->AddCircle(ImVec2(FieldMin.x + 13.0f, GlassY - 1.0f), 5.0f, kFaint, 0, 1.6f);
    Draw->AddLine(ImVec2(FieldMin.x + 17.0f, GlassY + 3.0f), ImVec2(FieldMin.x + 21.0f, GlassY + 7.0f), kFaint, 1.6f);

    const ImVec2 DdMax(DdMin.x + DdW, DdMin.y + 32.0f);
    const float CaretX = DdMax.x - 36.0f;
    Draw->AddRectFilled(DdMin, ImVec2(CaretX, DdMax.y), IM_COL32(0, 0, 0, 255), 16.0f, ImDrawFlags_RoundCornersLeft);
    Draw->AddRectFilled(ImVec2(CaretX, DdMin.y), DdMax, DdHot ? kHover : kTile, 16.0f,
        ImDrawFlags_RoundCornersRight);
    Draw->AddRect(DdMin, DdMax, kStroke, 16.0f);
    Draw->AddLine(ImVec2(CaretX, DdMin.y + 5.0f), ImVec2(CaretX, DdMax.y - 5.0f), kStroke);
    ImFont* Ui = Controls_->QueryUi();
    ImGui::PushFont(Ui);
    const ImVec2 LabelGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, DdLabel);
    Draw->AddText(ImVec2(DdMin.x + 16.0f, DdMin.y + (32.0f - LabelGlyph.y) * 0.5f), kText, DdLabel);
    ImGui::PopFont();

    const float TurnTarget = CategoryMenuWasOpen_ ? 1.0f : 0.0f;
    const float TurnStep   = ImGui::GetIO().DeltaTime / 0.2f;
    if (CategoryChevronAnim_ < TurnTarget)
    {
        CategoryChevronAnim_ += TurnStep;
        if (CategoryChevronAnim_ > TurnTarget)
        {
            CategoryChevronAnim_ = TurnTarget;
        }
    }
    else if (CategoryChevronAnim_ > TurnTarget)
    {
        CategoryChevronAnim_ -= TurnStep;
        if (CategoryChevronAnim_ < TurnTarget)
        {
            CategoryChevronAnim_ = TurnTarget;
        }
    }
    const float Turn   = CategoryChevronAnim_ * CategoryChevronAnim_ * (3.0f - 2.0f * CategoryChevronAnim_);
    const float ChevX  = CaretX + 18.0f;
    const float ChevY  = DdMin.y + 16.0f;
    const float TipY   = ChevY - 2.0f + Turn * 4.0f;
    const float ElbowY = ChevY + 3.0f - Turn * 6.0f;
    Draw->AddLine(ImVec2(ChevX - 5.0f, TipY), ImVec2(ChevX, ElbowY), kDim, 2.0f);
    Draw->AddLine(ImVec2(ChevX, ElbowY), ImVec2(ChevX + 5.0f, TipY), kDim, 2.0f);

    const double MenuNow = ImGui::GetTime();
    float MenuFade = 1.0f;
    if (CategoryMenuWasOpen_)
    {
        float T = static_cast<float>((MenuNow - CategoryMenuOpenedAt_) / 0.14);
        T        = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
        MenuFade = T * T * (3.0f - 2.0f * T);
    }
    ImGui::SetNextWindowPos(ImVec2(DdMin.x, DdMax.y + 8.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(DdW, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, MenuFade));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.180f, 0.180f, 0.180f, MenuFade));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    const bool MenuOpen = ImGui::BeginPopup("##categories");
    if (MenuOpen && !CategoryMenuWasOpen_)
    {
        CategoryMenuOpenedAt_ = MenuNow;
        MenuFade              = 0.0f;
    }
    CategoryMenuWasOpen_ = MenuOpen;
    if (MenuOpen)
    {
        ImDrawList* MenuDraw = ImGui::GetWindowDrawList();
        ImGui::PushFont(Ui);
        const float MenuWidth = ImGui::GetContentRegionAvail().x;
        for (int32_t i = -1; i < static_cast<int32_t>(EditorInstanceCategory::Count); ++i)
        {
            const char* Label = (i < 0) ? "All types" : EditorInstanceLabel(static_cast<EditorInstanceCategory>(i));
            const bool  Ticked = (i < 0) ? (PickCount == 0u) : CategoryPicked_[i];
            ImGui::Dummy(ImVec2(MenuWidth, 28.0f));
            const ImVec2 RowMin = ImGui::GetItemRectMin();
            const ImVec2 RowMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(RowMin);
            char RowId[12] = {};
            std::snprintf(RowId, sizeof(RowId), "##k%di", i);
            ImGui::InvisibleButton(RowId, ImVec2(MenuWidth, 28.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered && ImGui::IsMouseClicked(0))
            {
                if (i < 0)
                {
                    for (uint32_t k = 0u; k < static_cast<uint32_t>(EditorInstanceCategory::Count); ++k)
                    {
                        CategoryPicked_[k] = false;
                    }
                }
                else
                {
                    CategoryPicked_[i] = !CategoryPicked_[i];
                }
            }
            if (Ticked)
            {
                MenuDraw->AddRectFilled(RowMin, RowMax, ControlPanel::FadeTint(kMenuSel, MenuFade), 14.0f);
            }
            else if (Hovered)
            {
                MenuDraw->AddRectFilled(RowMin, RowMax, ControlPanel::FadeTint(kMenuHover, MenuFade), 14.0f);
            }
            const ImVec2 OptGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Label);
            MenuDraw->AddText(ImVec2(RowMin.x + 14.0f, RowMin.y + (28.0f - OptGlyph.y) * 0.5f),
                ControlPanel::FadeTint(Ticked || Hovered ? kText : kDim, MenuFade), Label);
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           CHIPS
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordChips() noexcept
{
    uint32_t PickCount = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        if (CategoryPicked_[i])
        {
            ++PickCount;
        }
    }
    if (PickCount == 0u)
    {
        return;
    }

    // Wrap layout: buttons place themselves and wrap by the tracked X, so no cursor jump ever extends
    //    the boundaries. After the last button the cursor already sits at the next line's start.
    ImFont*     Small = Controls_->QuerySmall();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    const float StartX = ImGui::GetCursorScreenPos().x;
    const float EndX   = StartX + ImGui::GetContentRegionAvail().x;
    float X = StartX;
    bool  FreshLine = true;

    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        if (!CategoryPicked_[i])
        {
            continue;
        }
        char Chip[32] = {};
        std::snprintf(Chip, sizeof(Chip), "%s \xc3\x97", EditorInstanceLabel(static_cast<EditorInstanceCategory>(i)));
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
            CategoryPicked_[i] = false;
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
        for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
        {
            CategoryPicked_[i] = false;
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

uint32_t OutlinerPanel::RecordOutline(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    bool SelfMatch[kMaxEditorInstances] = {};
    bool Shown[kMaxEditorInstances]     = {};

    const bool QueryOn = QueryText_[0] != '\0';
    bool AnyPick = false;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        AnyPick = AnyPick || CategoryPicked_[i];
    }
    const bool MatchOn = QueryOn || AnyPick;

    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        const bool NameHit = !QueryOn || ContainsFolded(Instances[i].Label, QueryText_);
        const bool CategoryHit = !AnyPick || CategoryPicked_[static_cast<uint32_t>(Instances[i].Category)];
        SelfMatch[i] = NameHit && CategoryHit;
        Shown[i]     = SelfMatch[i];
    }
    for (uint32_t i = InstanceCount; i-- > 0u;)
    {
        if (Instances[i].Category != EditorInstanceCategory::Folder)
        {
            continue;
        }
        for (uint32_t j = i + 1u; j < InstanceCount && Instances[j].Depth > Instances[i].Depth; ++j)
        {
            if (Shown[j])
            {
                Shown[i] = true;
                break;
            }
        }
    }

    uint32_t HitCount = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Shown[i])
        {
            ++HitCount;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##outline", ImVec2(0.0f, -30.0f), false);
    if (HitCount == 0u)
    {
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(Avail);
        const ImVec2 EmptyMin = ImGui::GetItemRectMin();
        ImFont* Small = Controls_->QuerySmall();
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Nothing matches");
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(EmptyMin.x + (Avail.x - Glyph.x) * 0.5f, EmptyMin.y + 24.0f), kDim, "Nothing matches");
        ImGui::PopFont();
    }
    else
    {
        for (uint32_t i = 0u; i < InstanceCount;)
        {
            if (!Shown[i])
            {
                ++i;
                continue;
            }
            RecordRow(Instances, InstanceCount, i, MatchOn);
            if (Instances[i].Category == EditorInstanceCategory::Folder && FolderShut_[i] && !MatchOn)
            {
                const uint32_t ShutDepth = Instances[i].Depth;
                do
                {
                    ++i;
                } while (i < InstanceCount && Instances[i].Depth > ShutDepth);
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

void OutlinerPanel::RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool MatchOn) noexcept
{
    EditorInstance& Row = Instances[Index];
    ImGui::PushID(static_cast<int>(Index));

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();
    const bool IsFolder = (Row.Category == EditorInstanceCategory::Folder);

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##row", ImVec2(RowWidth, 30.0f));
    const bool RowHot = ImGui::IsItemHovered();

    const float Indent = static_cast<float>(Row.Depth) * 14.0f;
    float X = Min.x + 6.0f + Indent;

    bool TwistyHit = false;
    bool TwistyHot = false;
    if (IsFolder)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, Min.y));
        ImGui::InvisibleButton("##twisty", ImVec2(14.0f, 30.0f));
        TwistyHot = ImGui::IsItemHovered();
        TwistyHit = TwistyHot && ImGui::IsMouseClicked(0);
        X += 14.0f;
    }

    float BX = Max.x - 4.0f;
    BX -= 19.0f;
    const ImVec2 VisMin(BX, Min.y + 5.5f);
    BX -= 4.0f;
    BX -= 19.0f;
    const ImVec2 LockMin(BX, Min.y + 5.5f);
    BX -= 4.0f;
    BX -= 19.0f;
    const ImVec2 SoloMin(BX, Min.y + 5.5f);
    BX -= 6.0f;

    // The row buttons rest hidden: a hover over the row wakes them, and a set button stays lit.
    const bool VisShow  = RowHot || !Row.Visible;
    const bool LockShow = RowHot || Row.Locked;
    const bool SoloShow = RowHot || Row.Solo;
    bool VisHot   = false;
    bool VisHit   = false;
    bool LockHot  = false;
    bool LockHit  = false;
    bool SoloHot  = false;
    bool SoloHit  = false;
    if (VisShow)
    {
        ImGui::SetCursorScreenPos(VisMin);
        ImGui::InvisibleButton("##vis", ImVec2(19.0f, 19.0f));
        VisHot = ImGui::IsItemHovered();
        VisHit = VisHot && ImGui::IsMouseClicked(0);
    }
    if (LockShow)
    {
        ImGui::SetCursorScreenPos(LockMin);
        ImGui::InvisibleButton("##lock", ImVec2(19.0f, 19.0f));
        LockHot = ImGui::IsItemHovered();
        LockHit = LockHot && ImGui::IsMouseClicked(0);
    }
    if (SoloShow)
    {
        ImGui::SetCursorScreenPos(SoloMin);
        ImGui::InvisibleButton("##solo", ImVec2(19.0f, 19.0f));
        SoloHot = ImGui::IsItemHovered();
        SoloHit = SoloHot && ImGui::IsMouseClicked(0);
    }

    const bool AnyHot = RowHot || TwistyHot || VisHot || LockHot || SoloHot;
    const bool Seated = IsPicked(Index);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    if (Seated)
    {
        Draw->AddRectFilled(Min, Max, kSeated, 10.0f);
        Draw->AddRectFilled(ImVec2(Min.x, Min.y + 6.0f), ImVec2(Min.x + 3.0f, Max.y - 6.0f),
            IM_COL32(255, 255, 255, 255), 1.5f);
    }
    else if (AnyHot)
    {
        Draw->AddRectFilled(Min, Max, kHover, 10.0f);
    }
    for (uint32_t d = 1u; d <= Row.Depth; ++d)
    {
        const float GuideX = Min.x + static_cast<float>(d - 1u) * 14.0f + 11.0f;
        Draw->AddLine(ImVec2(GuideX, Min.y), ImVec2(GuideX, Max.y), kGuide);
    }

    if (TwistyHit)
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
        if (Shift && Anchor_ != kNoEditorInstance && Anchor_ < InstanceCount)
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

    ImFont* Ui    = Controls_->QueryUi();
    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Ui);
    const ImVec2 NameGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Row.Label);
    ImGui::PopFont();
    const float NameY = Min.y + (30.0f - NameGlyph.y) * 0.5f;

    if (Renaming_ && RenameIndex_ == Index)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, Min.y + 4.0f));
        ImGui::PushItemWidth(BX - X > 40.0f ? BX - X : 40.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, RenameFocus_
            ? ImVec4(0.180f, 0.180f, 0.180f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
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
        RenameFocus_ = ImGui::IsItemFocused();
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
        if (Done)
        {
            std::snprintf(Row.Label, sizeof(Row.Label), "%s", RenameText_);
            Renaming_    = false;
            RenameFocus_ = false;
        }
        else if (ImGui::IsItemDeactivated())
        {
            Renaming_    = false;
            RenameFocus_ = false;
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

    if (SoloShow)
    {
        const ImVec2 SoloCentre(SoloMin.x + 9.5f, SoloMin.y + 9.5f);
        const ImU32 SoloTint = Row.Solo ? kHi : (SoloHot ? kDim : kFaint);
        Draw->AddCircle(SoloCentre, 5.0f, SoloTint, 0, 1.6f);
        Draw->AddCircleFilled(SoloCentre, 1.6f, SoloTint);
    }

    if (LockShow)
    {
        const ImVec2 LockCentre(LockMin.x + 9.5f, LockMin.y + 9.5f);
        const ImU32 LockTint = Row.Locked ? kText : (LockHot ? kDim : kFaint);
        Draw->AddCircle(ImVec2(LockCentre.x, LockCentre.y - 1.5f), 3.2f, LockTint, 0, 1.6f);
        Draw->AddRectFilled(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
            ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), kSeated, 2.0f);
        Draw->AddRect(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
            ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), LockTint, 2.0f, 0, 1.4f);
    }

    if (VisShow)
    {
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
    }

    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FOOTER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordFooter(EditorInstance* Instances, uint32_t InstanceCount, uint32_t HitCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 30.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 FootPos  = ImGui::GetWindowPos();
    const ImVec2 FootSize = ImGui::GetWindowSize();
    const float  FootX0   = FootPos.x;
    const float  FootX1   = FootPos.x + FootSize.x;
    const float  FootH    = FootPos.y + FootSize.y - Cursor.y;
    Draw->AddRectFilled(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y + FootH), kWash);
    Draw->AddLine(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y), kStroke);

    // One pick names itself; the hit count only shows while a query or a chip narrows the tree.
    char Left[64] = {};
    if (PickedCount_ == 0u)
    {
        std::snprintf(Left, sizeof(Left), "Nothing selected");
    }
    else if (PickedCount_ == 1u && Picked_[0] < InstanceCount)
    {
        std::snprintf(Left, sizeof(Left), "%s selected", Instances[Picked_[0]].Label);
    }
    else
    {
        std::snprintf(Left, sizeof(Left), "%u selected", PickedCount_);
    }
    bool Narrowed = (QueryText_[0] != '\0');
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        Narrowed = Narrowed || CategoryPicked_[i];
    }
    char Right[32] = {};
    if (Narrowed)
    {
        std::snprintf(Right, sizeof(Right), "%u of %u", HitCount, InstanceCount);
    }

    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 RightGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Right);
    Draw->AddText(ImVec2(FootX0 + 14.0f, Cursor.y + (FootH - RightGlyph.y) * 0.5f), kFaint, Left);
    if (Right[0] != '\0')
    {
        Draw->AddText(ImVec2(FootX1 - 14.0f - RightGlyph.x,
            Cursor.y + (FootH - RightGlyph.y) * 0.5f), kFaint, Right);
    }
    ImGui::PopFont();
}

} // namespace Frontier
