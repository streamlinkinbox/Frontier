//============================================================================================================================================
//                                                      EDITORKIT.CPP
//============================================================================================================================================
// 🧩 Development editor kit — the hand-drawn controls of the property sheet.

#include "EditorKit.h"
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kField  = IM_COL32(0, 0, 0, 255);        // the pill shell
constexpr ImU32 kInset  = IM_COL32(26, 26, 26, 255);     // the card seat
constexpr ImU32 kHover  = IM_COL32(28, 28, 28, 255);     // the row hover
constexpr ImU32 kSeated = IM_COL32(42, 42, 42, 255);     // the seated row
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);     // the strong stroke
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);   // rgba(255,255,255,.05)
constexpr ImU32 kTrack  = IM_COL32(255, 255, 255, 36);   // rgba(255,255,255,.14)
constexpr ImU32 kHi     = IM_COL32(108, 119, 255, 255);  // the periwinkle fill
constexpr ImU32 kCellBg = IM_COL32(255, 255, 255, 11);   // rgba(255,255,255,.045)
constexpr ImU32 kAxX    = IM_COL32(239, 83, 80, 255);
constexpr ImU32 kAxY    = IM_COL32(105, 208, 109, 255);
constexpr ImU32 kAxZ    = IM_COL32(91, 140, 255, 255);

constexpr float kTintDots[8][3] =
{
    { 1.000f, 1.000f, 1.000f },   // white
    { 0.937f, 0.325f, 0.314f },   // red
    { 1.000f, 0.694f, 0.294f },   // amber
    { 0.961f, 0.827f, 0.294f },   // yellow
    { 0.412f, 0.816f, 0.427f },   // green
    { 0.357f, 0.549f, 1.000f },   // blue
    { 0.604f, 0.482f, 1.000f },   // violet
    { 0.310f, 0.820f, 0.773f },   // teal
};

float Clamp01(float V) noexcept
{
    return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);
}

bool NearTint(const float A[3], const float B[3]) noexcept
{
    return std::fabs(A[0] - B[0]) + std::fabs(A[1] - B[1]) + std::fabs(A[2] - B[2]) < 0.03f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           FACES
//------------------------------------------------------------------------------------------------------------------------

void EditorKit::AssignFonts(ImFont* Ui, ImFont* Small, ImFont* Mono, ImFont* MonoSmall) noexcept
{
    Ui_        = Ui;
    Small_     = Small;
    Mono_      = Mono;
    MonoSmall_ = MonoSmall;
}

ImFont* EditorKit::QueryUi() const noexcept
{
    return Ui_ != nullptr ? Ui_ : ImGui::GetFont();
}

ImFont* EditorKit::QuerySmall() const noexcept
{
    return Small_ != nullptr ? Small_ : ImGui::GetFont();
}

ImFont* EditorKit::QueryMono() const noexcept
{
    return Mono_ != nullptr ? Mono_ : ImGui::GetFont();
}

ImFont* EditorKit::QueryMonoSmall() const noexcept
{
    return MonoSmall_ != nullptr ? MonoSmall_ : ImGui::GetFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CLICK-TO-TYPE
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::TypeInCell(const char* Id, const ImVec2& CellMin, const ImVec2& CellSize,
                            float Current, uint32_t Decimals, float* Committed) noexcept
{
    const ImGuiID CellId = ImGui::GetID(Id);
    ImDrawList*   Draw   = ImGui::GetWindowDrawList();
    ImFont*       Mono   = QueryMono();

    if (TypeInId_ == CellId)
    {
        ImGui::SetCursorScreenPos(ImVec2(CellMin.x + 2.0f, CellMin.y + 1.0f));
        ImGui::PushItemWidth(CellSize.x - 4.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
        ImGui::PushFont(Mono);
        if (TypeInFocus_)
        {
            ImGui::SetKeyboardFocusHere();
            TypeInFocus_ = false;
        }
        const bool Done = ImGui::InputText("##typein", TypeIn_, sizeof(TypeIn_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        if (Done)
        {
            *Committed = static_cast<float>(std::atof(TypeIn_));
            TypeInId_  = 0u;
            return true;
        }
        if (ImGui::IsItemDeactivated())
        {
            TypeInId_ = 0u;
        }
        return false;
    }

    ImGui::SetCursorScreenPos(CellMin);
    ImGui::InvisibleButton(Id, CellSize);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        std::snprintf(TypeIn_, sizeof(TypeIn_), "%.*f", Decimals, static_cast<double>(Current));
        TypeInId_    = CellId;
        TypeInFocus_ = true;
    }

    char Shown[32] = {};
    std::snprintf(Shown, sizeof(Shown), "%.*f", Decimals, static_cast<double>(Current));
    const ImVec2 Glyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Shown);
    ImGui::PushFont(Mono);
    Draw->AddText(ImVec2(CellMin.x + (CellSize.x - Glyph.x) * 0.5f, CellMin.y + (CellSize.y - Glyph.y) * 0.5f),
        kText, Shown);
    ImGui::PopFont();
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SLIDER PILL
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::SliderPill(const char* Id, float* Figure, float Minimum, float Maximum,
                            uint32_t Decimals, const char* Unit, bool Hi) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    if (RowWidth < 1.0f)
    {
        return false;
    }

    constexpr float kPillWidth = 92.0f;
    constexpr float kGap       = 10.0f;
    constexpr float kHeight    = 26.0f;

    ImGui::Dummy(ImVec2(RowWidth, kHeight));
    const ImVec2 Cursor  = ImGui::GetItemRectMin();
    const ImVec2 PillMax = ImVec2(Cursor.x + kPillWidth, Cursor.y + kHeight);
    const float  TrackX0 = Cursor.x + kPillWidth + kGap;
    const float  TrackX1 = Cursor.x + RowWidth;

    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    ImFont*     Mono    = QueryMono();
    bool        Changed = false;

    Draw->AddRectFilled(Cursor, PillMax, kField, 13.0f);
    Draw->AddRect(Cursor, PillMax, kStroke, 13.0f);

    ImGui::PushFont(Mono);
    const ImVec2 UnitGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Unit);
    ImGui::PopFont();
    const float UnitCell = UnitGlyph.x + 18.0f;
    const float SplitX   = Cursor.x + kPillWidth - 4.0f - UnitCell;
    const ImVec2 NumMin(Cursor.x + 4.0f, Cursor.y + 4.0f);
    const ImVec2 NumMax(SplitX - 2.0f, Cursor.y + kHeight - 4.0f);
    const ImVec2 UnitMin(SplitX + 2.0f, Cursor.y + 4.0f);
    const ImVec2 UnitMax(Cursor.x + kPillWidth - 4.0f, Cursor.y + kHeight - 4.0f);
    Draw->AddRectFilled(NumMin, NumMax, kCellBg, 9.0f);
    Draw->AddRectFilled(UnitMin, UnitMax, kCellBg, 9.0f);

    if (TypeInCell(Id, NumMin, ImVec2(NumMax.x - NumMin.x, NumMax.y - NumMin.y), *Figure, Decimals, Figure))
    {
        Changed = true;
    }

    ImGui::PushFont(Mono);
    Draw->AddText(ImVec2(UnitMin.x + (UnitMax.x - UnitMin.x - UnitGlyph.x) * 0.5f,
        UnitMin.y + (UnitMax.y - UnitMin.y - UnitGlyph.y) * 0.5f), kDim, Unit);
    ImGui::PopFont();

    const float Span = TrackX1 - TrackX0;
    if (Span > 4.0f)
    {
        const float TrackY = Cursor.y + kHeight * 0.5f;
        float Fraction = (Maximum > Minimum) ? ((*Figure - Minimum) / (Maximum - Minimum)) : 0.0f;
        Fraction       = Clamp01(Fraction);
        const float KnobX = TrackX0 + Fraction * Span;

        ImGui::SetCursorScreenPos(ImVec2(TrackX0, Cursor.y));
        ImGui::InvisibleButton("##track", ImVec2(Span, kHeight));
        if (ImGui::IsItemActive())
        {
            const float MouseX = ImGui::GetIO().MousePos.x;
            float Next = Minimum + (MouseX - TrackX0) / Span * (Maximum - Minimum);
            Next       = Next < Minimum ? Minimum : (Next > Maximum ? Maximum : Next);
            if (Next != *Figure)
            {
                *Figure = Next;
                Changed = true;
            }
        }

        Draw->AddLine(ImVec2(TrackX0, TrackY), ImVec2(TrackX1, TrackY), kTrack, 2.0f);
        Draw->AddLine(ImVec2(TrackX0, TrackY), ImVec2(KnobX, TrackY), Hi ? kHi : IM_COL32(255, 255, 255, 255), 2.0f);
        Draw->AddCircleFilled(ImVec2(KnobX, TrackY), 10.0f, IM_COL32(255, 255, 255, 30));
        Draw->AddCircleFilled(ImVec2(KnobX, TrackY), 6.0f, IM_COL32(255, 255, 255, 255));
    }

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SWITCH
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::Switch(const char* Id, bool* On) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    ImGui::Dummy(ImVec2(33.0f, 19.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton(Id, ImVec2(33.0f, 19.0f));
    bool Changed = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        *On     = !*On;
        Changed = true;
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, *On ? IM_COL32(232, 232, 232, 255) : IM_COL32(36, 36, 36, 255), 9.5f);
    Draw->AddRect(Min, Max, kStroke, 9.5f);
    const float KnobX = *On ? (Max.x - 9.5f) : (Min.x + 9.5f);
    Draw->AddCircleFilled(ImVec2(KnobX, Min.y + 9.5f), 6.5f, IM_COL32(245, 245, 245, 255));
    Draw->AddCircle(ImVec2(KnobX, Min.y + 9.5f), 6.5f, IM_COL32(0, 0, 0, 60));

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         AXIS VEC3
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::AxisVec3(const char* Id, float Axes[3], float Step, bool Editable) noexcept
{
    (void)Id;
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    if (RowWidth < 1.0f)
    {
        return false;
    }

    constexpr float kGap    = 8.0f;
    constexpr float kHeight = 26.0f;
    const float CellWidth   = (RowWidth - 2.0f * kGap) / 3.0f;

    ImGui::Dummy(ImVec2(RowWidth, kHeight));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    static const char*  kLetters[3] = { "X", "Y", "Z" };
    static const ImU32  kAxisTint[3] = { kAxX, kAxY, kAxZ };

    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    ImFont*     Small   = QuerySmall();
    bool        Changed = false;

    for (uint32_t i = 0u; i < 3u; ++i)
    {
        const ImVec2 CellMin(Cursor.x + i * (CellWidth + kGap), Cursor.y);
        const ImVec2 CellMax(CellMin.x + CellWidth, Cursor.y + kHeight);
        Draw->AddRectFilled(CellMin, CellMax, kField, 8.0f);
        Draw->AddRect(CellMin, CellMax, kStroke, 8.0f);

        const ImVec2 LetterMax(CellMin.x + 22.0f, CellMax.y);
        char LetterId[8] = {};
        std::snprintf(LetterId, sizeof(LetterId), "##ax%u", i);
        ImGui::SetCursorScreenPos(CellMin);
        ImGui::InvisibleButton(LetterId, ImVec2(22.0f, kHeight));
        if (Editable && ImGui::IsItemActive())
        {
            Axes[i] += ImGui::GetIO().MouseDelta.x * Step * 0.5f;
            Changed  = true;
        }

        ImGui::PushFont(Small);
        const ImVec2 LetterGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, kLetters[i]);
        Draw->AddText(ImVec2(CellMin.x + (22.0f - LetterGlyph.x) * 0.5f,
            CellMin.y + (kHeight - LetterGlyph.y) * 0.5f), Editable ? kAxisTint[i] : kFaint, kLetters[i]);
        ImGui::PopFont();

        const ImVec2 NumMin(LetterMax.x + 2.0f, CellMin.y + 4.0f);
        const ImVec2 NumMax(CellMax.x - 8.0f, CellMax.y - 4.0f);
        if (Editable)
        {
            char NumId[8] = {};
            std::snprintf(NumId, sizeof(NumId), "##n%u", i);
            if (TypeInCell(NumId, NumMin, ImVec2(NumMax.x - NumMin.x, NumMax.y - NumMin.y), Axes[i], 2u, &Axes[i]))
            {
                Changed = true;
            }
        }
        else
        {
            char Shown[32] = {};
            std::snprintf(Shown, sizeof(Shown), "%.2f", static_cast<double>(Axes[i]));
            ImFont* Mono = QueryMono();
            ImGui::PushFont(Mono);
            const ImVec2 Glyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Shown);
            Draw->AddText(ImVec2(NumMin.x + (NumMax.x - NumMin.x - Glyph.x) * 0.5f,
                NumMin.y + (NumMax.y - NumMin.y - Glyph.y) * 0.5f), kFaint, Shown);
            ImGui::PopFont();
        }
    }

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        COLOUR CHIP
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::ColourChip(const char* Id, float Tint[3]) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    if (RowWidth < 1.0f)
    {
        return false;
    }

    constexpr float kHeight = 26.0f;
    ImGui::Dummy(ImVec2(RowWidth, kHeight));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImGui::SetCursorScreenPos(Cursor);
    ImGui::InvisibleButton(Id, ImVec2(52.0f, kHeight));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        ImGui::OpenPopup("##chipmenu");
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 ChipMax(Cursor.x + 52.0f, Cursor.y + kHeight);
    Draw->AddRectFilled(Cursor, ChipMax,
        IM_COL32(static_cast<int>(Clamp01(Tint[0]) * 255.0f), static_cast<int>(Clamp01(Tint[1]) * 255.0f),
            static_cast<int>(Clamp01(Tint[2]) * 255.0f), 255), 8.0f);
    Draw->AddRect(Cursor, ChipMax, kStrong, 8.0f);

    char Hex[10] = {};
    std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X",
        static_cast<int>(Clamp01(Tint[0]) * 255.0f),
        static_cast<int>(Clamp01(Tint[1]) * 255.0f),
        static_cast<int>(Clamp01(Tint[2]) * 255.0f));
    ImFont* Mono = QueryMono();
    ImGui::PushFont(Mono);
    const ImVec2 HexGlyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Hex);
    Draw->AddText(ImVec2(Cursor.x + 62.0f, Cursor.y + (kHeight - HexGlyph.y) * 0.5f), kDim, Hex);
    ImGui::PopFont();

    bool Changed = false;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.102f, 0.102f, 0.102f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.180f, 0.180f, 0.180f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    if (ImGui::BeginPopup("##chipmenu"))
    {
        ImGui::PushItemWidth(220.0f);
        Changed = ImGui::ColorPicker3("##picker", Tint,
            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoAlpha);
        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SWATCH ROW
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::SwatchRow(const char* Id, float Tint[3]) noexcept
{
    (void)Id;
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    constexpr float kDot  = 18.0f;
    constexpr float kGap  = 8.0f;
    constexpr float kSpan = 8.0f * kDot + 7.0f * kGap;

    ImGui::Dummy(ImVec2(kSpan, 20.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    bool        Changed = false;

    for (uint32_t i = 0u; i < 8u; ++i)
    {
        const ImVec2 DotMin(Cursor.x + i * (kDot + kGap), Cursor.y + 1.0f);
        const ImVec2 Centre(DotMin.x + kDot * 0.5f, DotMin.y + kDot * 0.5f);
        char DotId[8] = {};
        std::snprintf(DotId, sizeof(DotId), "##sw%u", i);
        ImGui::SetCursorScreenPos(DotMin);
        ImGui::InvisibleButton(DotId, ImVec2(kDot, kDot));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        {
            Tint[0] = kTintDots[i][0];
            Tint[1] = kTintDots[i][1];
            Tint[2] = kTintDots[i][2];
            Changed = true;
        }

        Draw->AddCircleFilled(Centre, 9.0f,
            IM_COL32(static_cast<int>(kTintDots[i][0] * 255.0f), static_cast<int>(kTintDots[i][1] * 255.0f),
                static_cast<int>(kTintDots[i][2] * 255.0f), 255));
        Draw->AddCircle(Centre, 9.0f, kStrong);
        if (NearTint(Tint, kTintDots[i]))
        {
            Draw->AddCircle(Centre, 11.5f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
        }
    }

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         DROP-DOWN
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::DropDown(const char* Id, uint32_t* Picked,
                          const char Options[kMaxEditorOptions][kMaxEditorOptionChars],
                          uint32_t OptionCount) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    if (RowWidth < 1.0f)
    {
        return false;
    }

    constexpr float kHeight = 30.0f;
    ImGui::Dummy(ImVec2(RowWidth, kHeight));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    const ImVec2 End(Cursor.x + RowWidth, Cursor.y + kHeight);

    ImGui::SetCursorScreenPos(Cursor);
    ImGui::InvisibleButton(Id, ImVec2(RowWidth, kHeight));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        ImGui::OpenPopup("##ddmenu");
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Cursor, End, kField, 15.0f);
    Draw->AddRect(Cursor, End, kStroke, 15.0f);

    const uint32_t Shown = (*Picked < OptionCount) ? *Picked : 0u;
    ImFont* Ui = QueryUi();
    ImGui::PushFont(Ui);
    const ImVec2 LabelGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Options[Shown]);
    Draw->AddText(ImVec2(Cursor.x + 13.0f, Cursor.y + (kHeight - LabelGlyph.y) * 0.5f), kText, Options[Shown]);
    ImGui::PopFont();

    const ImVec2 Chev(Cursor.x + RowWidth - 16.0f, Cursor.y + kHeight * 0.5f);
    Draw->AddLine(ImVec2(Chev.x - 4.0f, Chev.y - 1.5f), ImVec2(Chev.x, Chev.y + 2.5f), kDim, 1.6f);
    Draw->AddLine(ImVec2(Chev.x, Chev.y + 2.5f), ImVec2(Chev.x + 4.0f, Chev.y - 1.5f), kDim, 1.6f);

    bool Changed = false;
    ImGui::SetNextWindowSize(ImVec2(RowWidth < 160.0f ? 160.0f : RowWidth, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.102f, 0.102f, 0.102f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.180f, 0.180f, 0.180f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    if (ImGui::BeginPopup("##ddmenu"))
    {
        ImGui::PushFont(Ui);
        for (uint32_t i = 0u; i < OptionCount; ++i)
        {
            const float MenuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(MenuWidth, 28.0f));
            const ImVec2 RowMin = ImGui::GetItemRectMin();
            const ImVec2 RowMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(RowMin);
            char RowId[12] = {};
            std::snprintf(RowId, sizeof(RowId), "##dd%u", i);
            ImGui::InvisibleButton(RowId, ImVec2(MenuWidth, 28.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered && ImGui::IsMouseClicked(0))
            {
                *Picked = i;
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (Hovered)
            {
                Draw->AddRectFilled(RowMin, RowMax, IM_COL32(36, 36, 36, 255), 6.0f);
            }
            const ImVec2 OptGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Options[i]);
            Draw->AddText(ImVec2(RowMin.x + 30.0f, RowMin.y + (28.0f - OptGlyph.y) * 0.5f),
                *Picked == i ? kText : kDim, Options[i]);
            if (*Picked == i)
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
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PILL TOGGLE
//------------------------------------------------------------------------------------------------------------------------

bool EditorKit::PillToggle(const char* Label, bool* On) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return false;
    }

    ImFont* Small = QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Label);
    ImGui::PopFont();

    const ImVec2 Size(LabelGlyph.x + 20.0f, 22.0f);
    ImGui::Dummy(Size);
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton(Label, Size);
    bool Changed = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        *On     = !*On;
        Changed = true;
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, *On ? kInset : IM_COL32(255, 255, 255, 8), 11.0f);
    Draw->AddRect(Min, Max, *On ? kStrong : kStroke, 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(Min.x + (Size.x - LabelGlyph.x) * 0.5f, Min.y + (Size.y - LabelGlyph.y) * 0.5f),
        *On ? kText : kDim, Label);
    ImGui::PopFont();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          READOUT
//------------------------------------------------------------------------------------------------------------------------

void EditorKit::Readout(const char* Text) noexcept
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems)
    {
        return;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    if (RowWidth < 1.0f)
    {
        return;
    }

    ImGui::Dummy(ImVec2(RowWidth, 18.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImFont* Mono = QueryMonoSmall();
    ImGui::PushFont(Mono);
    const ImVec2 Glyph = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, Text);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(Cursor.x + RowWidth - Glyph.x, Cursor.y + (18.0f - Glyph.y) * 0.5f), kDim, Text);
    ImGui::PopFont();
}

} // namespace Frontier
