//============================================================================================================================================
//                                                     OUTLINERPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor outliner — the instance roster as an outline. SolidArc's outliner, spoke in ImGui: the
//    glowing title, the census tiles, the filter menu and pills, the two-line rows, the census-bar footer.
//    Behaviour is unchanged (pick, rename, search, filters, toggles); only the look moved. Token values below
//    are SolidArc's :root palette as bytes, so the two outliners match by construction rather than by eye.

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

constexpr ImU32 kText    = IM_COL32(255, 255, 255, 240);   // --text .94
constexpr ImU32 kT2      = IM_COL32(255, 255, 255, 143);   // --t2 .56
constexpr ImU32 kT3      = IM_COL32(255, 255, 255, 82);    // --t3 .32
constexpr ImU32 kG2      = IM_COL32(255, 255, 255, 11);    // --g2 .045
constexpr ImU32 kG3      = IM_COL32(255, 255, 255, 20);    // --g3 .08
constexpr ImU32 kStroke  = IM_COL32(255, 255, 255, 18);    // --stroke .07
constexpr ImU32 kStroke2 = IM_COL32(255, 255, 255, 33);    // --stroke2 .13
constexpr ImU32 kSelBg   = IM_COL32(255, 255, 255, 23);    // .row.sel .09
constexpr ImU32 kMenuBg  = IM_COL32(10, 11, 13, 245);      // .dd-menu .96
constexpr ImU32 kFootBg  = IM_COL32(255, 255, 255, 5);     // .outliner-foot .02
constexpr ImU32 kRed     = IM_COL32(0xFF, 0x3B, 0x30, 255);
constexpr ImU32 kGreen   = IM_COL32(0x34, 0xC7, 0x59, 255);
constexpr ImU32 kYellow  = IM_COL32(0xE5, 0xD3, 0x3A, 255);
constexpr ImU32 kOrange  = IM_COL32(0xFF, 0xB4, 0x54, 255);
constexpr ImU32 kBlue    = IM_COL32(0x4D, 0xA3, 0xFF, 255);
constexpr ImU32 kViolet  = IM_COL32(0xB4, 0x8C, 0xFF, 255);
constexpr ImU32 kCyan    = IM_COL32(0x4F, 0xD8, 0xE0, 255);
constexpr ImU32 kHi      = IM_COL32(108, 119, 255, 255);   // ours-only: the set-solo gleam
constexpr ImU32 kWash    = IM_COL32(108, 119, 255, 110);   // ours-only: the search-hit wash

ImU32 WithAlpha(ImU32 Tint, uint8_t Alpha) noexcept
{
    return (Tint & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
}

ImU32 CategoryAccent(EditorInstanceCategory Category) noexcept
{
    switch (Category)
    {
    case EditorInstanceCategory::Geometry: return kBlue;
    case EditorInstanceCategory::Light:    return kYellow;
    case EditorInstanceCategory::Camera:   return kViolet;
    case EditorInstanceCategory::Folder:
    default:                               return kT2;   // folders carry their own tint; the menu dot stays neutral
    }
}

ImU32 RowTint(const EditorInstance& Row) noexcept
{
    return IM_COL32(static_cast<int>(Row.Tint[0] * 255.0f), static_cast<int>(Row.Tint[1] * 255.0f),
        static_cast<int>(Row.Tint[2] * 255.0f), 255);
}

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

void UpperCopy(char* Dst, uint32_t Cap, const char* Src) noexcept
{
    uint32_t i = 0u;
    for (; Src[i] != '\0' && i + 1u < Cap; ++i)
    {
        Dst[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Src[i])));
    }
    Dst[i] = '\0';
}

// Letter-spaced text — ImGui has no tracking, so the spaced headers walk codepoint by codepoint (UTF-8 aware:
//    the badge's middot is two bytes). Returns the advance, so callers right-align off it.
float MeasureSpaced(ImFont* Font, const char* Text, float Spacing) noexcept
{
    float W = 0.0f;
    uint32_t N = 0u;
    for (const char* P = Text; *P != '\0';)
    {
        unsigned int C = 0u;
        const int Len = ImTextCharFromUtf8(&C, P, nullptr);
        if (Len <= 0)
        {
            break;
        }
        W += Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, P, P + Len).x;
        P += Len;
        ++N;
    }
    return N > 0u ? W + Spacing * static_cast<float>(N - 1u) : 0.0f;
}

void DrawSpaced(ImDrawList* Draw, ImFont* Font, const ImVec2& Pos, ImU32 Tint, const char* Text,
                float Spacing) noexcept
{
    float X = Pos.x;
    for (const char* P = Text; *P != '\0';)
    {
        unsigned int C = 0u;
        const int Len = ImTextCharFromUtf8(&C, P, nullptr);
        if (Len <= 0)
        {
            break;
        }
        const ImVec2 G = Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, P, P + Len);
        Draw->AddText(Font, Font->LegacySize, ImVec2(X, Pos.y), Tint, P, P + Len);
        X += G.x + Spacing;
        P += Len;
    }
}

// A resigned glow dot: the halo SolidArc draws with box-shadow, as two faint discs under a solid core.
void DrawGlowDot(ImDrawList* Draw, const ImVec2& Centre, float Radius, ImU32 Tint) noexcept
{
    Draw->AddCircleFilled(Centre, Radius + 4.0f, WithAlpha(Tint, 26));
    Draw->AddCircleFilled(Centre, Radius + 1.5f, WithAlpha(Tint, 60));
    Draw->AddCircleFilled(Centre, Radius, Tint);
}

// The row glyphs, drawn rather than fonted so the headless proof rasterises the same pixels.
void DrawEyeGlyph(ImDrawList* Draw, const ImVec2& Centre, bool Visible, ImU32 Tint) noexcept
{
    Draw->AddBezierCubic(ImVec2(Centre.x - 6.0f, Centre.y),
        ImVec2(Centre.x - 2.5f, Centre.y - 4.5f), ImVec2(Centre.x + 2.5f, Centre.y - 4.5f),
        ImVec2(Centre.x + 6.0f, Centre.y), Tint, 1.6f);
    Draw->AddBezierCubic(ImVec2(Centre.x - 6.0f, Centre.y),
        ImVec2(Centre.x - 2.5f, Centre.y + 4.5f), ImVec2(Centre.x + 2.5f, Centre.y + 4.5f),
        ImVec2(Centre.x + 6.0f, Centre.y), Tint, 1.6f);
    if (Visible)
    {
        Draw->AddCircleFilled(Centre, 1.8f, Tint);
    }
    else
    {
        Draw->AddLine(ImVec2(Centre.x - 6.5f, Centre.y + 6.0f),
            ImVec2(Centre.x + 6.5f, Centre.y - 6.0f), Tint, 1.6f);
    }
}

void DrawLockGlyph(ImDrawList* Draw, const ImVec2& Centre, ImU32 Tint) noexcept
{
    Draw->PathArcTo(ImVec2(Centre.x, Centre.y - 1.0f), 3.4f, 3.14159265f, 6.28318530f);
    Draw->PathStroke(Tint, 0, 1.5f);
    Draw->AddRect(ImVec2(Centre.x - 4.5f, Centre.y - 1.0f), ImVec2(Centre.x + 4.5f, Centre.y + 5.5f),
        Tint, 2.0f, 0, 1.5f);
}

void DrawSoloGlyph(ImDrawList* Draw, const ImVec2& Centre, ImU32 Tint) noexcept
{
    Draw->AddCircle(Centre, 5.0f, Tint, 0, 1.6f);
    Draw->AddCircleFilled(Centre, 1.6f, Tint);
}

// The icon chip's glyph: one minimal mark per category, in the row's own tint.
void DrawChipGlyph(ImDrawList* Draw, const ImVec2& Centre, ImU32 Tint, EditorInstanceCategory Category) noexcept
{
    switch (Category)
    {
    case EditorInstanceCategory::Light:
        Draw->AddCircle(Centre, 3.0f, Tint, 0, 1.6f);
        Draw->AddLine(ImVec2(Centre.x, Centre.y - 6.5f), ImVec2(Centre.x, Centre.y - 4.5f), Tint, 1.6f);
        Draw->AddLine(ImVec2(Centre.x, Centre.y + 4.5f), ImVec2(Centre.x, Centre.y + 6.5f), Tint, 1.6f);
        Draw->AddLine(ImVec2(Centre.x - 6.5f, Centre.y), ImVec2(Centre.x - 4.5f, Centre.y), Tint, 1.6f);
        Draw->AddLine(ImVec2(Centre.x + 4.5f, Centre.y), ImVec2(Centre.x + 6.5f, Centre.y), Tint, 1.6f);
        break;
    case EditorInstanceCategory::Camera:
        Draw->AddRect(ImVec2(Centre.x - 6.5f, Centre.y - 4.5f), ImVec2(Centre.x + 6.5f, Centre.y + 4.5f),
            Tint, 2.0f, 0, 1.6f);
        Draw->AddCircleFilled(Centre, 1.8f, Tint);
        break;
    case EditorInstanceCategory::Geometry:
    case EditorInstanceCategory::Folder:
    default:
        Draw->AddRect(ImVec2(Centre.x - 6.0f, Centre.y - 6.0f), ImVec2(Centre.x + 6.0f, Centre.y + 6.0f),
            Tint, 1.5f, 0, 1.6f);
        break;
    }
}

// Centred line for the empty tree: the caller measures, this places.
void DrawTextCentred(const ImVec2& ZoneMin, float ZoneW, float Y, ImFont* Font, const ImVec2& Glyph,
                     const char* Text, ImU32 Tint) noexcept
{
    ImGui::GetWindowDrawList()->AddText(Font, Font->LegacySize,
        ImVec2(ZoneMin.x + (ZoneW - Glyph.x) * 0.5f, ZoneMin.y + Y), Tint, Text);
}

// The filter test both the tree walk and the footer census share.
bool PassesFilter(const EditorInstance& Row, const char* Query, const bool* CategoryPicked) noexcept
{
    bool AnyPick = false;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        AnyPick = AnyPick || CategoryPicked[i];
    }
    const bool NameHit = Query[0] == '\0' || ContainsFolded(Row.Label, Query);
    return NameHit && (!AnyPick || CategoryPicked[static_cast<uint32_t>(Row.Category)]);
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

void OutlinerPanel::HandleRowClick(EditorInstance& Row, uint32_t Index, uint32_t InstanceCount) noexcept
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

    RecordHeader();
    RecordTiles(Instances, InstanceCount);
    RecordSearch(Instances, InstanceCount);
    RecordChips(Instances, InstanceCount);
    const uint32_t Hits = RecordOutline(Instances, InstanceCount);
    RecordFooter(Instances, InstanceCount, Hits);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          HEADER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordHeader() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 44.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Title = Controls_->QueryTitle();
    ImFont*     Small = Controls_->QuerySmall();

    const float Cy = Cursor.y + 22.0f;
    DrawGlowDot(Draw, ImVec2(Cursor.x + 5.0f, Cy), 5.0f, kCyan);

    ImGui::PushFont(Title);
    const ImVec2 TitleGlyph = Title->CalcTextSizeA(Title->LegacySize, FLT_MAX, 0.0f, "Outliner");
    Draw->AddText(ImVec2(Cursor.x + 20.0f, Cy - TitleGlyph.y * 0.5f), kText, "Outliner");
    ImGui::PopFont();

    // The badge names the host the way SolidArc's names its own: same pill, our document.
    const char* Badge = "FRONTIER \xc2\xb7 SCENE";
    const float BadgeW = MeasureSpaced(Small, Badge, 0.9f) + 16.0f;
    const ImVec2 BadgeMin(Cursor.x + RowWidth - BadgeW, Cy - 8.0f);
    const ImVec2 BadgeMax(Cursor.x + RowWidth, Cy + 8.0f);
    Draw->AddRect(BadgeMin, BadgeMax, kStroke, 8.0f);
    DrawSpaced(Draw, Small, ImVec2(BadgeMin.x + 8.0f, BadgeMin.y + (16.0f - 11.0f) * 0.5f), kT3, Badge, 0.9f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          TILES
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    uint32_t Leaves = 0u;
    uint32_t Visible = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Category == EditorInstanceCategory::Folder)
        {
            continue;
        }
        ++Leaves;
        if (Instances[i].Visible)
        {
            ++Visible;
        }
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kGap = 8.0f;
    constexpr float kTileH = 46.0f;
    const float TileW = (RowWidth - kGap) * 0.5f;
    ImGui::Dummy(ImVec2(RowWidth, kTileH + 10.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    ImFont*     Small   = Controls_->QuerySmall();
    ImFont*     Display = Controls_->QueryDisplay();

    char Big[16] = {};
    char Sub[64] = {};
    for (uint32_t T = 0u; T < 2u; ++T)
    {
        const ImVec2 Min(Cursor.x + static_cast<float>(T) * (TileW + kGap), Cursor.y);
        const ImVec2 Max(Min.x + TileW, Min.y + kTileH);
        Draw->AddRectFilled(Min, Max, IM_COL32(255, 255, 255, 9), 15.0f);
        Draw->AddRect(Min, Max, kStroke, 15.0f);

        const bool  Lit = (T == 0u) ? (Leaves > 0u && Visible > 0u) : (PickedCount_ > 0u);
        const char* Tag = (T == 0u) ? "Instances" : "Selected";
        if (T == 0u)
        {
            std::snprintf(Big, sizeof(Big), "%u", Leaves);
            std::snprintf(Sub, sizeof(Sub), "%u visible \xc2\xb7 %u hidden", Visible, Leaves - Visible);
        }
        else if (PickedCount_ == 0u)
        {
            std::snprintf(Big, sizeof(Big), "0");
            std::snprintf(Sub, sizeof(Sub), "none");
        }
        else if (PickedCount_ == 1u && Picked_[0] < InstanceCount)
        {
            std::snprintf(Big, sizeof(Big), "1");
            std::snprintf(Sub, sizeof(Sub), "%s", Instances[Picked_[0]].Label);
        }
        else
        {
            std::snprintf(Big, sizeof(Big), "%u", PickedCount_);
            const char* First  = (PickedCount_ > 0u && Picked_[0] < InstanceCount) ? Instances[Picked_[0]].Label : "?";
            const char* Second = (PickedCount_ > 1u && Picked_[1] < InstanceCount) ? Instances[Picked_[1]].Label : "?";
            if (PickedCount_ > 2u)
            {
                std::snprintf(Sub, sizeof(Sub), "%s, %s \xe2\x80\xa6", First, Second);
            }
            else
            {
                std::snprintf(Sub, sizeof(Sub), "%s, %s", First, Second);
            }
        }

        ImGui::PushFont(Display);
        const ImVec2 BigGlyph = Display->CalcTextSizeA(Display->LegacySize, FLT_MAX, 0.0f, Big);
        ImGui::PopFont();
        ImGui::PushFont(Small);
        const ImVec2 TagGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Tag);
        const ImVec2 SubGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Sub);
        Draw->AddText(ImVec2(Min.x + 26.0f, Min.y + 9.0f), kT2, Tag);
        ImGui::PopFont();

        const ImVec2 DotC(Min.x + 16.0f, Min.y + 9.0f + TagGlyph.y * 0.5f);
        if (Lit)
        {
            DrawGlowDot(Draw, DotC, 4.0f, kGreen);
        }
        else
        {
            Draw->AddCircleFilled(DotC, 4.0f, kT3);
        }

        // The numeral sits bottom-right across both text rows; the sub clips short of it, ellipsis-style.
        const ImVec2 BigMin(Max.x - 12.0f - BigGlyph.x, Max.y - 8.0f - BigGlyph.y);
        ImGui::PushFont(Display);
        Draw->AddText(BigMin, kText, Big);
        ImGui::PopFont();
        ImGui::PushClipRect(ImVec2(Min.x + 12.0f, Min.y + 26.0f), ImVec2(BigMin.x - 6.0f, Max.y - 8.0f), true);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(Min.x + 12.0f, Min.y + 26.0f), kT3, Sub);
        ImGui::PopFont();
        ImGui::PopClipRect();
        // The reference ellipsizes overflow; the dots sit at the clip edge like CSS would.
        if (SubGlyph.x > (BigMin.x - 6.0f) - (Min.x + 12.0f))
        {
            const ImVec2 EGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "\xe2\x80\xa6");
            ImGui::PushFont(Small);
            Draw->AddText(ImVec2(BigMin.x - 6.0f - EGlyph.x, Min.y + 26.0f), kT3, "\xe2\x80\xa6");
            ImGui::PopFont();
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SEARCH
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordSearch(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    uint32_t CatCount[static_cast<uint32_t>(EditorInstanceCategory::Count)] = {};
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        ++CatCount[static_cast<uint32_t>(Instances[i].Category)];
    }
    uint32_t PickCount = 0u;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        if (CategoryPicked_[i])
        {
            ++PickCount;
        }
    }

    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    ImFont*     Mono  = Controls_->QueryMonoSmall();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const float RowWidth = ImGui::GetContentRegionAvail().x;

    ImGui::PushFont(Ui);
    const ImVec2 FilterGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Filter");
    const ImVec2 FieldProbe  = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Ag");
    ImGui::PopFont();
    float BtnW = 12.0f + FilterGlyph.x + 8.0f + 26.0f + 6.0f;
    char CntText[8] = {};
    float CntW = 0.0f;
    if (PickCount > 0u)
    {
        std::snprintf(CntText, sizeof(CntText), "%u", PickCount);
        ImGui::PushFont(Mono);
        CntW = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, CntText).x;
        ImGui::PopFont();
        BtnW += 8.0f + CntW;
    }

    const float PadY = (34.0f - FieldProbe.y) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.045f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.045f));
    ImGui::PushStyleColor(ImGuiCol_Border, SearchFocus_
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f) : ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.0f, 1.0f, 1.0f, 0.32f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 17.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(36.0f, PadY));
    ImGui::PushItemWidth(RowWidth - 6.0f - BtnW);
    ImGui::PushFont(Ui);
    ImGui::InputTextWithHint("##query", "Search instances\xe2\x80\xa6", QueryText_, sizeof(QueryText_));
    ImGui::PopFont();
    SearchFocus_ = ImGui::IsItemFocused();
    const ImVec2 FieldMin = ImGui::GetItemRectMin();
    const ImVec2 FieldMax = ImGui::GetItemRectMax();
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);

    const float GlassY = (FieldMin.y + FieldMax.y) * 0.5f;
    Draw->AddCircle(ImVec2(FieldMin.x + 15.5f, GlassY - 1.5f), 5.5f, kT3, 0, 1.6f);
    Draw->AddLine(ImVec2(FieldMin.x + 19.5f, GlassY + 2.5f), ImVec2(FieldMin.x + 24.0f, GlassY + 7.0f), kT3, 1.6f);

    ImGui::SameLine(0.0f, 6.0f);
    const ImVec2 DdMin = ImGui::GetCursorScreenPos();
    const ImVec2 DdMax(DdMin.x + BtnW, DdMin.y + 34.0f);
    ImGui::InvisibleButton("##filtermenu", ImVec2(BtnW, 34.0f));
    const bool DdHot = ImGui::IsItemHovered();
    if (DdHot && ImGui::IsMouseClicked(0))
    {
        ImGui::OpenPopup("##filtermenu");
    }

    Draw->AddRectFilled(DdMin, DdMax, kG2, 17.0f);
    Draw->AddRect(DdMin, DdMax, (DdHot || CategoryMenuWasOpen_) ? kStroke2 : kStroke, 17.0f);
    ImGui::PushFont(Ui);
    Draw->AddText(ImVec2(DdMin.x + 12.0f, DdMin.y + (34.0f - FilterGlyph.y) * 0.5f),
        (DdHot || CategoryMenuWasOpen_) ? kText : kT2, "Filter");
    ImGui::PopFont();
    if (PickCount > 0u)
    {
        ImGui::PushFont(Mono);
        const ImVec2 CntGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, CntText);
        Draw->AddText(ImVec2(DdMin.x + 20.0f + FilterGlyph.x, DdMin.y + (34.0f - CntGlyph.y) * 0.5f),
            kT3, CntText);
        ImGui::PopFont();
    }

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
    const float Turn  = CategoryChevronAnim_ * CategoryChevronAnim_ * (3.0f - 2.0f * CategoryChevronAnim_);
    const ImVec2 CaretC(DdMax.x - 6.0f - 13.0f, DdMin.y + 17.0f);
    Draw->AddCircleFilled(CaretC, 13.0f, CategoryMenuWasOpen_ ? kG3 : kG2);
    const float TipY   = CaretC.y - 2.5f + Turn * 5.0f;
    const float ElbowY = CaretC.y + 2.5f - Turn * 5.0f;
    const ImU32 ChevTint = CategoryMenuWasOpen_ ? kText : kT3;
    Draw->AddLine(ImVec2(CaretC.x - 6.0f, TipY), ImVec2(CaretC.x, ElbowY), ChevTint, 1.8f);
    Draw->AddLine(ImVec2(CaretC.x, ElbowY), ImVec2(CaretC.x + 6.0f, TipY), ChevTint, 1.8f);

    // The menu ADDS filters; the pills below remove them. Inactive kinds list with their census; active
    //    ones leave the menu for the pill row, and Clear filters resets the narrowing.
    static const EditorInstanceCategory kMenuOrder[] = {
        EditorInstanceCategory::Geometry, EditorInstanceCategory::Light,
        EditorInstanceCategory::Camera, EditorInstanceCategory::Folder,
    };
    const double MenuNow = ImGui::GetTime();
    float MenuFade = 1.0f;
    if (CategoryMenuWasOpen_)
    {
        float T = static_cast<float>((MenuNow - CategoryMenuOpenedAt_) / 0.16);
        T        = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
        MenuFade = T * T * (3.0f - 2.0f * T);
    }
    const float MenuW = RowWidth < 210.0f ? RowWidth : 210.0f;
    ImGui::SetNextWindowPos(ImVec2(DdMax.x - MenuW, DdMax.y + 6.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.039f, 0.043f, 0.051f, 0.96f * MenuFade));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.13f * MenuFade));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    const bool MenuOpen = ImGui::BeginPopup("##filtermenu");
    if (MenuOpen && !CategoryMenuWasOpen_)
    {
        CategoryMenuOpenedAt_ = MenuNow;
        MenuFade              = 0.0f;
    }
    CategoryMenuWasOpen_ = MenuOpen;
    if (MenuOpen)
    {
        ImDrawList* MenuDraw = ImGui::GetWindowDrawList();
        const float MenuInner = MenuW - 12.0f;
        uint32_t Avail = 0u;
        for (uint32_t o = 0u; o < 4u; ++o)
        {
            const uint32_t i = static_cast<uint32_t>(kMenuOrder[o]);
            if (!CategoryPicked_[i])
            {
                ++Avail;
            }
        }
        ImGui::PushFont(Ui);
        for (uint32_t o = 0u; o < 4u; ++o)
        {
            const uint32_t i = static_cast<uint32_t>(kMenuOrder[o]);
            if (CategoryPicked_[i])
            {
                continue;
            }
            const char* Label = EditorInstanceLabel(static_cast<EditorInstanceCategory>(i));
            const ImU32 Acc = CategoryAccent(static_cast<EditorInstanceCategory>(i));
            ImGui::Dummy(ImVec2(MenuInner, 32.0f));
            const ImVec2 RowMin = ImGui::GetItemRectMin();
            const ImVec2 RowMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(RowMin);
            char RowId[12] = {};
            std::snprintf(RowId, sizeof(RowId), "##fm%u", i);
            ImGui::InvisibleButton(RowId, ImVec2(MenuInner, 32.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered && ImGui::IsMouseClicked(0))
            {
                CategoryPicked_[i] = true;
                if (Avail == 1u)
                {
                    ImGui::CloseCurrentPopup();
                }
            }
            if (Hovered)
            {
                MenuDraw->AddRectFilled(RowMin, RowMax, ControlPanel::FadeTint(kG2, MenuFade), 16.0f);
            }
            const ImVec2 DotC(RowMin.x + 14.0f, RowMin.y + 16.0f);
            MenuDraw->AddCircleFilled(DotC, 7.0f, ControlPanel::FadeTint(WithAlpha(Acc, 40), MenuFade));
            MenuDraw->AddCircleFilled(DotC, 4.0f, ControlPanel::FadeTint(Acc, MenuFade));
            const ImVec2 OptGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Label);
            MenuDraw->AddText(ImVec2(RowMin.x + 27.0f, RowMin.y + (32.0f - OptGlyph.y) * 0.5f),
                ControlPanel::FadeTint(Hovered ? kText : kT2, MenuFade), Label);
            char N[8] = {};
            std::snprintf(N, sizeof(N), "%u", CatCount[i]);
            const ImVec2 NGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, N);
            MenuDraw->AddText(Mono, Mono->LegacySize,
                ImVec2(RowMax.x - 8.0f - NGlyph.x, RowMin.y + (32.0f - NGlyph.y) * 0.5f),
                ControlPanel::FadeTint(kT3, MenuFade), N);
        }
        ImGui::PopFont();
        if (Avail == 0u)
        {
            ImGui::PushFont(Small);
            ImGui::Dummy(ImVec2(MenuInner, 28.0f));
            const ImVec2 AllMin = ImGui::GetItemRectMin();
            const ImVec2 AllGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "all types filtered");
            MenuDraw->AddText(
                ImVec2(AllMin.x + (MenuInner - AllGlyph.x) * 0.5f, AllMin.y + (28.0f - AllGlyph.y) * 0.5f),
                ControlPanel::FadeTint(kT3, MenuFade), "all types filtered");
            ImGui::PopFont();
        }
        else if (PickCount > 0u)
        {
            ImGui::Dummy(ImVec2(MenuInner, 9.0f));
            const ImVec2 SepMin = ImGui::GetItemRectMin();
            MenuDraw->AddLine(ImVec2(SepMin.x + 8.0f, SepMin.y + 4.5f),
                ImVec2(SepMin.x + MenuInner - 8.0f, SepMin.y + 4.5f),
                ControlPanel::FadeTint(kStroke, MenuFade));
            ImGui::PushFont(Small);
            ImGui::Dummy(ImVec2(MenuInner, 28.0f));
            const ImVec2 ActMin = ImGui::GetItemRectMin();
            const ImVec2 ActMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(ActMin);
            ImGui::InvisibleButton("##fmclear", ImVec2(MenuInner, 28.0f));
            const bool ActHot = ImGui::IsItemHovered();
            if (ActHot && ImGui::IsMouseClicked(0))
            {
                for (uint32_t k = 0u; k < static_cast<uint32_t>(EditorInstanceCategory::Count); ++k)
                {
                    CategoryPicked_[k] = false;
                }
                ImGui::CloseCurrentPopup();
            }
            const ImVec2 ActGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Clear filters");
            MenuDraw->AddText(
                ImVec2(ActMin.x + (MenuInner - ActGlyph.x) * 0.5f, ActMin.y + (28.0f - ActGlyph.y) * 0.5f),
                ControlPanel::FadeTint(ActHot ? kText : kT3, MenuFade), "Clear filters");
            (void)ActMax;
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(RowWidth, 6.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           PILLS
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordChips(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    uint32_t CatCount[static_cast<uint32_t>(EditorInstanceCategory::Count)] = {};
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        ++CatCount[static_cast<uint32_t>(Instances[i].Category)];
    }
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

    ImFont*     Small = Controls_->QuerySmall();
    ImFont*     Mono  = Controls_->QueryMonoSmall();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    const float StartX = ImGui::GetCursorScreenPos().x;
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float EndX   = StartX + RowWidth;
    float X = StartX;
    bool  FreshLine = true;

    static const EditorInstanceCategory kPillOrder[] = {
        EditorInstanceCategory::Geometry, EditorInstanceCategory::Light,
        EditorInstanceCategory::Camera, EditorInstanceCategory::Folder,
    };
    for (uint32_t o = 0u; o < 4u; ++o)
    {
        const uint32_t i = static_cast<uint32_t>(kPillOrder[o]);
        if (!CategoryPicked_[i])
        {
            continue;
        }
        const char* Label = EditorInstanceLabel(static_cast<EditorInstanceCategory>(i));
        const ImU32 Acc = CategoryAccent(static_cast<EditorInstanceCategory>(i));
        char N[8] = {};
        std::snprintf(N, sizeof(N), "%u", CatCount[i]);
        ImGui::PushFont(Small);
        const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Label);
        ImGui::PopFont();
        ImGui::PushFont(Mono);
        const ImVec2 NGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, N);
        ImGui::PopFont();
        const float PillW = 10.0f + 7.0f + 6.0f + LabelGlyph.x + 6.0f + NGlyph.x + 6.0f + 18.0f + 4.0f;
        if (!FreshLine && X + 6.0f + PillW > EndX)
        {
            FreshLine = true;
        }
        if (!FreshLine)
        {
            ImGui::SameLine(0.0f, 6.0f);
        }
        ImGui::Dummy(ImVec2(PillW, 26.0f));
        const ImVec2 PillMin = ImGui::GetItemRectMin();
        const ImVec2 PillMax = ImGui::GetItemRectMax();
        // The × hit box ends at the pill's edge, so the wrap tracking below stays exact.
        ImGui::SetCursorScreenPos(ImVec2(PillMax.x - 22.0f, PillMin.y));
        char XId[12] = {};
        std::snprintf(XId, sizeof(XId), "##px%u", i);
        ImGui::InvisibleButton(XId, ImVec2(22.0f, 26.0f));
        const bool XHot = ImGui::IsItemHovered();
        if (XHot && ImGui::IsMouseClicked(0))
        {
            CategoryPicked_[i] = false;
        }

        Draw->AddRectFilled(PillMin, PillMax, WithAlpha(Acc, 36), 13.0f);
        Draw->AddRect(PillMin, PillMax, WithAlpha(Acc, 89), 13.0f);
        const float Cy = PillMin.y + 13.0f;
        const ImVec2 DotC(PillMin.x + 13.5f, Cy);
        Draw->AddCircleFilled(DotC, 6.0f, WithAlpha(Acc, 40));
        Draw->AddCircleFilled(DotC, 3.5f, Acc);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(PillMin.x + 23.0f, Cy - LabelGlyph.y * 0.5f), kText, Label);
        ImGui::PopFont();
        ImGui::PushFont(Mono);
        Draw->AddText(ImVec2(PillMin.x + 29.0f + LabelGlyph.x, Cy - NGlyph.y * 0.5f), kT2, N);
        ImGui::PopFont();
        const ImVec2 XC(PillMax.x - 13.0f, Cy);
        if (XHot)
        {
            Draw->AddCircleFilled(XC, 9.0f, IM_COL32(255, 255, 255, 38));
        }
        const ImU32 XTint = XHot ? IM_COL32(255, 255, 255, 255) : kT2;
        Draw->AddLine(ImVec2(XC.x - 3.5f, XC.y - 3.5f), ImVec2(XC.x + 3.5f, XC.y + 3.5f), XTint, 2.2f);
        Draw->AddLine(ImVec2(XC.x - 3.5f, XC.y + 3.5f), ImVec2(XC.x + 3.5f, XC.y - 3.5f), XTint, 2.2f);

        X = FreshLine ? StartX + PillW : X + 6.0f + PillW;
        FreshLine = false;
    }

    if (PickCount > 1u)
    {
        ImGui::PushFont(Small);
        const ImVec2 ClearGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "clear all");
        ImGui::PopFont();
        const float ClearW = 20.0f + ClearGlyph.x;
        if (!FreshLine && X + 6.0f + ClearW > EndX)
        {
            FreshLine = true;
        }
        if (!FreshLine)
        {
            ImGui::SameLine(0.0f, 6.0f);
        }
        ImGui::Dummy(ImVec2(ClearW, 26.0f));
        const ImVec2 ClearMin = ImGui::GetItemRectMin();
        const ImVec2 ClearMax = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(ClearMin);
        ImGui::InvisibleButton("##clearall", ImVec2(ClearW, 26.0f));
        const bool ClearHot = ImGui::IsItemHovered();
        if (ClearHot && ImGui::IsMouseClicked(0))
        {
            for (uint32_t k = 0u; k < static_cast<uint32_t>(EditorInstanceCategory::Count); ++k)
            {
                CategoryPicked_[k] = false;
            }
        }
        Draw->AddRect(ClearMin, ClearMax, ClearHot ? kStroke2 : kStroke, 13.0f);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(ClearMin.x + 10.0f, ClearMin.y + (26.0f - ClearGlyph.y) * 0.5f),
            ClearHot ? kText : kT3, "clear all");
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(RowWidth, 6.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           TREE
//------------------------------------------------------------------------------------------------------------------------

uint32_t OutlinerPanel::RecordOutline(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    bool Shown[kMaxEditorInstances] = {};

    const bool QueryOn = QueryText_[0] != '\0';
    bool AnyPick = false;
    for (uint32_t i = 0u; i < static_cast<uint32_t>(EditorInstanceCategory::Count); ++i)
    {
        AnyPick = AnyPick || CategoryPicked_[i];
    }
    const bool MatchOn = QueryOn || AnyPick;

    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        Shown[i] = PassesFilter(Instances[i], QueryText_, CategoryPicked_);
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::BeginChild("##outline", ImVec2(0.0f, -49.0f), false);
    if (HitCount == 0u)
    {
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(Avail);
        const ImVec2 EmptyMin = ImGui::GetItemRectMin();
        ImFont* Ui    = Controls_->QueryUi();
        ImFont* Small = Controls_->QuerySmall();
        ImGui::PushFont(Ui);
        const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Nothing matches");
        DrawTextCentred(EmptyMin, Avail.x, 24.0f, Ui, TitleGlyph, "Nothing matches", kT2);
        ImGui::PopFont();
        ImGui::PushFont(Small);
        const ImVec2 HintGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f,
            "Loosen the search or clear a filter");
        DrawTextCentred(EmptyMin, Avail.x, 24.0f + TitleGlyph.y + 6.0f, Small, HintGlyph,
            "Loosen the search or clear a filter", kT3);
        ImGui::PopFont();
        (void)TitleGlyph;
    }
    else
    {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 2.0f));
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
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    return HitCount;
}

void OutlinerPanel::RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index,
                              bool MatchOn) noexcept
{
    ImGui::PushID(static_cast<int>(Index));
    if (Instances[Index].Category == EditorInstanceCategory::Folder)
    {
        RecordFolderRow(Instances, InstanceCount, Index, MatchOn);
    }
    else
    {
        RecordLeafRow(Instances, InstanceCount, Index);
    }
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           GROUP
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordFolderRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index,
                                    bool MatchOn) noexcept
{
    EditorInstance& Row = Instances[Index];
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 28.0f));
    const ImVec2 PadMin = ImGui::GetItemRectMin();
    const ImVec2 PadMax = ImGui::GetItemRectMax();
    const ImVec2 Min(PadMin.x, PadMin.y + 4.0f);
    const ImVec2 Max(PadMax.x, PadMax.y);

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##head", ImVec2(RowWidth, 24.0f));
    const bool HeadHot = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##caret", ImVec2(24.0f, 24.0f));
    const bool CaretHot = ImGui::IsItemHovered();

    const bool RowHot   = HeadHot || CaretHot;
    const bool EyeShow  = RowHot || !Row.Visible;
    const bool SoloShow = RowHot || Row.Solo;
    const bool LockShow = RowHot || Row.Locked;
    float BX = Max.x;
    bool EyeHot = false;
    bool EyeHit = false;
    bool SoloHot = false;
    bool SoloHit = false;
    bool LockHot = false;
    bool LockHit = false;
    if (EyeShow)
    {
        BX -= 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y));
        ImGui::InvisibleButton("##eye", ImVec2(28.0f, 24.0f));
        EyeHot = ImGui::IsItemHovered();
        EyeHit = EyeHot && ImGui::IsMouseClicked(0);
    }
    if (SoloShow)
    {
        BX -= 2.0f + 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y + 1.0f));
        ImGui::InvisibleButton("##solo", ImVec2(22.0f, 22.0f));
        SoloHot = ImGui::IsItemHovered();
        SoloHit = SoloHot && ImGui::IsMouseClicked(0);
    }
    if (LockShow)
    {
        BX -= 2.0f + 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y + 1.0f));
        ImGui::InvisibleButton("##lock", ImVec2(22.0f, 22.0f));
        LockHot = ImGui::IsItemHovered();
        LockHit = LockHot && ImGui::IsMouseClicked(0);
    }

    if (CaretHot && ImGui::IsMouseClicked(0))
    {
        FolderShut_[Index] = !FolderShut_[Index];
    }
    else if (EyeHit)
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
    else if (HeadHot && ImGui::IsMouseClicked(0))
    {
        HandleRowClick(Row, Index, InstanceCount);
    }
    const bool Seated = IsPicked(Index);
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

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();
    ImFont*     Mono  = Controls_->QueryMonoSmall();
    const float Cy = Min.y + 12.0f;
    // SolidArc draws no picked header; the label lifts one step so the pick still reads.
    const ImU32 Dim = !Row.Visible ? ControlPanel::FadeTint(kT3, 0.45f) : (Seated ? kT2 : kT3);

    if (!FolderShut_[Index] || MatchOn)
    {
        Draw->AddLine(ImVec2(Min.x + 6.0f, Cy - 3.0f), ImVec2(Min.x + 12.0f, Cy + 3.0f), Dim, 1.8f);
        Draw->AddLine(ImVec2(Min.x + 12.0f, Cy + 3.0f), ImVec2(Min.x + 18.0f, Cy - 3.0f), Dim, 1.8f);
    }
    else
    {
        Draw->AddLine(ImVec2(Min.x + 9.0f, Cy - 5.0f), ImVec2(Min.x + 15.0f, Cy), Dim, 1.8f);
        Draw->AddLine(ImVec2(Min.x + 15.0f, Cy), ImVec2(Min.x + 9.0f, Cy + 5.0f), Dim, 1.8f);
    }

    char Upper[44] = {};
    UpperCopy(Upper, static_cast<uint32_t>(sizeof(Upper)), Row.Label);
    ImGui::PushFont(Small);
    const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Upper);
    ImGui::PopFont();

    if (Renaming_ && RenameIndex_ == Index)
    {
        const float EditW = BX - 8.0f - (Min.x + 26.0f);
        ImGui::SetCursorScreenPos(ImVec2(Min.x + 26.0f, Min.y + 3.0f));
        ImGui::PushItemWidth(EditW > 40.0f ? EditW : 40.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, RenameFocus_
            ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f) : ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushFont(Small);
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
        DrawSpaced(Draw, Small, ImVec2(Min.x + 26.0f, Cy - LabelGlyph.y * 0.5f), Dim, Upper, 1.1f);
    }

    char N[8] = {};
    std::snprintf(N, sizeof(N), "%u", Row.KidCount);
    ImGui::PushFont(Mono);
    const ImVec2 NGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, N);
    Draw->AddText(ImVec2(BX - 8.0f - NGlyph.x, Cy - NGlyph.y * 0.5f), Dim, N);
    ImGui::PopFont();

    float GX = Max.x;
    if (EyeShow)
    {
        GX -= 28.0f;
        DrawEyeGlyph(Draw, ImVec2(GX + 14.0f, Cy), Row.Visible, kT3);
    }
    if (SoloShow)
    {
        GX -= 2.0f + 22.0f;
        DrawSoloGlyph(Draw, ImVec2(GX + 11.0f, Cy), Row.Solo ? kHi : (SoloHot ? kT2 : kT3));
    }
    if (LockShow)
    {
        GX -= 2.0f + 22.0f;
        DrawLockGlyph(Draw, ImVec2(GX + 11.0f, Cy), Row.Locked ? kT2 : kT3);
    }
    (void)EyeHot;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           LEAF
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordLeafRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index) noexcept
{
    EditorInstance& Row = Instances[Index];
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 34.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();
    const float Cy = Min.y + 17.0f;
    const float Indent = Row.Depth >= 2u ? static_cast<float>(Row.Depth - 1u) * 16.0f : 0.0f;

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##row", ImVec2(RowWidth, 34.0f));
    const bool RowHot = ImGui::IsItemHovered();

    const bool EyeShow  = RowHot || !Row.Visible;
    const bool SoloShow = RowHot || Row.Solo;
    const bool LockShow = RowHot || Row.Locked;
    float BX = Max.x - 4.0f;
    bool EyeHot  = false;
    bool EyeHit  = false;
    bool SoloHot = false;
    bool SoloHit = false;
    bool LockHot = false;
    bool LockHit = false;
    if (EyeShow)
    {
        BX -= 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y + 5.0f));
        ImGui::InvisibleButton("##eye", ImVec2(28.0f, 24.0f));
        EyeHot = ImGui::IsItemHovered();
        EyeHit = EyeHot && ImGui::IsMouseClicked(0);
    }
    if (SoloShow)
    {
        BX -= 2.0f + 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y + 6.0f));
        ImGui::InvisibleButton("##solo", ImVec2(22.0f, 22.0f));
        SoloHot = ImGui::IsItemHovered();
        SoloHit = SoloHot && ImGui::IsMouseClicked(0);
    }
    if (LockShow)
    {
        BX -= 2.0f + 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(BX, Min.y + 6.0f));
        ImGui::InvisibleButton("##lock", ImVec2(22.0f, 22.0f));
        LockHot = ImGui::IsItemHovered();
        LockHit = LockHot && ImGui::IsMouseClicked(0);
    }

    if (EyeHit)
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
        HandleRowClick(Row, Index, InstanceCount);
    }
    const bool Seated = IsPicked(Index);
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

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Mono  = Controls_->QueryMonoSmall();
    const bool  AnyHot = RowHot || EyeHot || SoloHot || LockHot;
    const float DimFade = Row.Visible ? 1.0f : 0.45f;

    if (Seated)
    {
        Draw->AddRectFilled(Min, Max, kSelBg, 12.0f);
        Draw->AddRect(ImVec2(Min.x + 0.5f, Min.y + 0.5f), ImVec2(Max.x - 0.5f, Max.y - 0.5f),
            kStroke2, 12.0f);
    }
    else if (AnyHot)
    {
        Draw->AddRectFilled(Min, Max, kG2, 12.0f);
    }

    for (uint32_t l = 1u; l < Row.Depth; ++l)
    {
        const float GX = Min.x + static_cast<float>(l - 1u) * 16.0f + 6.0f;
        Draw->AddLine(ImVec2(GX, Min.y + 4.0f), ImVec2(GX, Max.y - 4.0f), kStroke);
    }

    const ImU32 Tint = RowTint(Row);
    const ImVec2 ChipMin(Min.x + 7.0f + Indent, Cy - 11.0f);
    const ImVec2 ChipMax(ChipMin.x + 22.0f, ChipMin.y + 26.0f);
    Draw->AddRectFilled(ChipMin, ChipMax, ControlPanel::FadeTint(WithAlpha(Tint, 36), DimFade), 8.0f);
    DrawChipGlyph(Draw, ImVec2(ChipMin.x + 13.0f, Cy), ControlPanel::FadeTint(Tint, DimFade), Row.Category);

    const float TextX = ChipMin.x + 30.0f;
    const float ClipR = (BX - 8.0f > TextX + 12.0f) ? (BX - 8.0f) : (TextX + 12.0f);
    const ImU32 NameTint = ControlPanel::FadeTint((Seated || AnyHot) ? kText : kT2, DimFade);

    if (Renaming_ && RenameIndex_ == Index)
    {
        const float EditW = ClipR - TextX;
        ImGui::SetCursorScreenPos(ImVec2(TextX, Min.y + 6.0f));
        ImGui::PushItemWidth(EditW > 40.0f ? EditW : 40.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, RenameFocus_
            ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f) : ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
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
        ImGui::PushFont(Ui);
        const ImVec2 NameGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Row.Label);
        Draw->AddText(ImVec2(TextX, Min.y + 2.0f), NameTint, Row.Label);
        ImGui::PopFont();
        if (QueryText_[0] != '\0')
        {
            const char* Hit = FindFolded(Row.Label, QueryText_);
            if (Hit != nullptr)
            {
                ImGui::PushFont(Ui);
                const float PreW = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Row.Label, Hit).x;
                const float HitW = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f,
                    Hit, Hit + std::strlen(QueryText_)).x;
                ImGui::PopFont();
                Draw->AddRectFilled(ImVec2(TextX + PreW, Min.y + 2.0f),
                    ImVec2(TextX + PreW + HitW, Min.y + 2.0f + NameGlyph.y),
                    ControlPanel::FadeTint(kWash, DimFade), 2.0f);
            }
        }
        (void)NameGlyph;

        char Meta[64] = {};
        std::snprintf(Meta, sizeof(Meta), "%s%s%s", EditorInstanceLabel(Row.Category),
            Row.Dynamic ? " \xc2\xb7 dynamic" : "", Row.Physics ? " \xc2\xb7 physics" : "");
        ImGui::PushFont(Mono);
        const ImVec2 MetaGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Meta);
        ImGui::PushClipRect(ImVec2(TextX, Min.y), ImVec2(ClipR, Max.y), true);
        Draw->AddText(ImVec2(TextX, Min.y + 3.0f + NameGlyph.y),
            ControlPanel::FadeTint(kT3, DimFade), Meta);
        ImGui::PopClipRect();
        ImGui::PopFont();
        (void)MetaGlyph;
    }

    float GX = Max.x - 4.0f;
    if (EyeShow)
    {
        GX -= 28.0f;
        DrawEyeGlyph(Draw, ImVec2(GX + 14.0f, Cy), Row.Visible, kT3);
    }
    if (SoloShow)
    {
        GX -= 2.0f + 22.0f;
        DrawSoloGlyph(Draw, ImVec2(GX + 11.0f, Cy), Row.Solo ? kHi : (SoloHot ? kT2 : kT3));
    }
    if (LockShow)
    {
        GX -= 2.0f + 22.0f;
        DrawLockGlyph(Draw, ImVec2(GX + 11.0f, Cy), Row.Locked ? kT2 : kT3);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FOOTER
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::RecordFooter(EditorInstance* Instances, uint32_t InstanceCount, uint32_t HitCount) noexcept
{
    (void)HitCount;
    uint32_t LeafTotal = 0u;
    uint32_t LeafShown = 0u;
    uint32_t Hidden = 0u;
    uint32_t Locked = 0u;
    uint32_t GeoN = 0u;
    uint32_t LightN = 0u;
    uint32_t CamN = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Locked)
        {
            ++Locked;
        }
        const uint32_t c = static_cast<uint32_t>(Instances[i].Category);
        if (Instances[i].Category == EditorInstanceCategory::Folder)
        {
            continue;
        }
        ++LeafTotal;
        switch (Instances[i].Category)
        {
        case EditorInstanceCategory::Geometry: ++GeoN; break;
        case EditorInstanceCategory::Light:    ++LightN; break;
        case EditorInstanceCategory::Camera:   ++CamN; break;
        default: break;
        }
        if (!Instances[i].Visible)
        {
            ++Hidden;
        }
        // Like SolidArc's shown count, the category narrowing alone decides; the query narrows the
        //    tree, not the census.
        bool AnyPick = false;
        for (uint32_t k = 0u; k < static_cast<uint32_t>(EditorInstanceCategory::Count); ++k)
        {
            AnyPick = AnyPick || CategoryPicked_[k];
        }
        if (!AnyPick || CategoryPicked_[c])
        {
            ++LeafShown;
        }
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 FootMin = ImGui::GetCursorScreenPos();
    const ImVec2 WinMin = ImGui::GetWindowPos();
    const ImVec2 WinMax(WinMin.x + ImGui::GetWindowSize().x, WinMin.y + ImGui::GetWindowSize().y);
    const float FootY0 = FootMin.y;

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();
    ImFont*     Mono  = Controls_->QueryMonoSmall();
    Draw->AddRectFilled(ImVec2(WinMin.x, FootY0), WinMax, kFootBg);
    Draw->AddLine(ImVec2(WinMin.x, FootY0 + 0.5f), ImVec2(WinMax.x, FootY0 + 0.5f), kStroke);

    // The census bar: one segment per kind, proportional, with the 3px gaps of the reference.
    const float BarX0 = FootMin.x;
    const float BarX1 = FootMin.x + RowWidth;
    const float BarY = FootY0 + 8.0f;
    Draw->AddRectFilled(ImVec2(BarX0, BarY), ImVec2(BarX1, BarY + 6.0f),
        IM_COL32(255, 255, 255, 13), 3.0f);
    struct Seg { ImU32 Acc; uint32_t N; };
    Seg Segs[3] = { { kBlue, GeoN }, { kYellow, LightN }, { kViolet, CamN } };
    uint32_t SegN = 0u;
    for (uint32_t s = 0u; s < 3u; ++s)
    {
        if (Segs[s].N > 0u)
        {
            ++SegN;
        }
    }
    if (LeafTotal > 0u && SegN > 0u)
    {
        const float Unit = (RowWidth - static_cast<float>(SegN - 1u) * 3.0f) / static_cast<float>(LeafTotal);
        float X = BarX0;
        uint32_t Drawn = 0u;
        for (uint32_t s = 0u; s < 3u; ++s)
        {
            if (Segs[s].N == 0u)
            {
                continue;
            }
            const float W = static_cast<float>(Segs[s].N) * Unit;
            const bool First = (Drawn == 0u);
            const bool Last = (Drawn + 1u == SegN);
            const ImDrawFlags Round = (First && Last) ? ImDrawFlags_RoundCornersAll
                : First ? ImDrawFlags_RoundCornersLeft : Last ? ImDrawFlags_RoundCornersRight
                : ImDrawFlags_RoundCornersNone;
            Draw->AddRectFilled(ImVec2(X, BarY), ImVec2(X + W, BarY + 6.0f), Segs[s].Acc, 3.0f, Round);
            X += W + 3.0f;
            ++Drawn;
        }
    }

    const float RowY = BarY + 6.0f + 7.0f;
    const float RowCy = RowY + 9.0f;
    float KX = BarX0;

    char ShownN[16] = {};
    char TotalN[16] = {};
    std::snprintf(ShownN, sizeof(ShownN), "%u", LeafShown);
    std::snprintf(TotalN, sizeof(TotalN), "%u", LeafTotal);
    ImGui::PushFont(Mono);
    const ImVec2 ShownGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, ShownN);
    const ImVec2 TotalGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, TotalN);
    const ImVec2 SlashGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, "/");
    Draw->AddRect(ImVec2(KX + 3.5f, RowCy - 5.0f), ImVec2(KX + 10.5f, RowCy + 2.0f), kT3, 1.0f, 0, 1.3f);
    Draw->AddRect(ImVec2(KX, RowCy - 2.0f), ImVec2(KX + 7.0f, RowCy + 5.0f), kT3, 1.0f, 0, 1.3f);
    float TX = KX + 12.0f + 5.0f;
    Draw->AddText(ImVec2(TX, RowCy - ShownGlyph.y * 0.5f), kT2, ShownN);
    TX += ShownGlyph.x;
    Draw->AddText(ImVec2(TX, RowCy - SlashGlyph.y * 0.5f), kT3, "/");
    TX += SlashGlyph.x;
    Draw->AddText(ImVec2(TX, RowCy - TotalGlyph.y * 0.5f), kT3, TotalN);
    TX += TotalGlyph.x;
    KX = TX + 8.0f;

    char HidN[8] = {};
    char LokN[8] = {};
    std::snprintf(HidN, sizeof(HidN), "%u", Hidden);
    std::snprintf(LokN, sizeof(LokN), "%u", Locked);
    const ImVec2 HidGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, HidN);
    Draw->AddBezierCubic(ImVec2(KX, RowCy),
        ImVec2(KX + 1.8f, RowCy - 2.8f), ImVec2(KX + 6.2f, RowCy - 2.8f),
        ImVec2(KX + 8.0f, RowCy), kT3, 1.3f);
    Draw->AddBezierCubic(ImVec2(KX, RowCy),
        ImVec2(KX + 1.8f, RowCy + 2.8f), ImVec2(KX + 6.2f, RowCy + 2.8f),
        ImVec2(KX + 8.0f, RowCy), kT3, 1.3f);
    Draw->AddLine(ImVec2(KX - 0.5f, RowCy + 4.5f), ImVec2(KX + 8.5f, RowCy - 4.5f), kT3, 1.3f);
    Draw->AddText(ImVec2(KX + 12.0f, RowCy - HidGlyph.y * 0.5f), kT2, HidN);
    KX += 12.0f + HidGlyph.x + 8.0f;

    const ImVec2 LokGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, LokN);
    Draw->PathArcTo(ImVec2(KX + 4.0f, RowCy), 2.6f, 3.14159265f, 6.28318530f);
    Draw->PathStroke(kT3, 0, 1.3f);
    Draw->AddRect(ImVec2(KX + 0.5f, RowCy), ImVec2(KX + 7.5f, RowCy + 5.5f), kT3, 1.0f, 0, 1.3f);
    Draw->AddText(ImVec2(KX + 12.0f, RowCy - LokGlyph.y * 0.5f), kT2, LokN);
    ImGui::PopFont();

    char SelN[16] = {};
    if (PickedCount_ == 0u)
    {
        std::snprintf(SelN, sizeof(SelN), "Nothing selected");
    }
    else if (PickedCount_ == 1u)
    {
        std::snprintf(SelN, sizeof(SelN), "1 selected");
    }
    else
    {
        std::snprintf(SelN, sizeof(SelN), "%u selected", PickedCount_);
    }
    ImGui::PushFont(Small);
    const ImVec2 SelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, SelN);
    Draw->AddText(ImVec2(BarX1 - SelGlyph.x, RowCy - SelGlyph.y * 0.5f), kT2, SelN);
    ImGui::PopFont();
}

} // namespace Frontier
