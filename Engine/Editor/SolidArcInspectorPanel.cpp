//============================================================================================================================================
//                                                         SOLIDARCINSPECTORPANEL.CPP
//============================================================================================================================================
// 📦 SolidArc inspector — the picked CAD object as flat dark cards: head pill, hero (measure + stats), presence cells, tab chips,
//    transform / bounds tables, parameter rows and the action tiles. Mirrors the web editor's inspector.
//    Drawn only for sheets the SolidArc adapter marks EditorSheetAppearance::SolidArc; the game inspector is untouched.

#include "SolidArcInspectorPanel.h"

#include "ControlPanel.h"
#include "EditorInstance.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                         TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kInk      = IM_COL32(236, 238, 242, 255);
constexpr ImU32 kDim      = IM_COL32(128, 133, 143, 255);
constexpr ImU32 kFaint    = IM_COL32(84, 88, 96, 255);
constexpr ImU32 kHair     = IM_COL32(255, 255, 255, 18);
constexpr ImU32 kTile     = IM_COL32(255, 255, 255, 11);
constexpr ImU32 kTileHot  = IM_COL32(255, 255, 255, 20);
constexpr ImU32 kCardFill = IM_COL32(23, 24, 27, 255);      // the flat card and tile fill (#17181b)
constexpr ImU32 kBlack    = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kCapTrack = IM_COL32(59, 59, 62, 255);      // slider capsule
constexpr ImU32 kCapFill  = IM_COL32(93, 93, 96, 255);
constexpr ImU32 kKnob     = IM_COL32(217, 217, 219, 255);
constexpr ImU32 kSeated[4] = { IM_COL32(52, 199, 89, 255), IM_COL32(245, 158, 11, 255), IM_COL32(180, 140, 255, 255), IM_COL32(229, 211, 58, 255) };
constexpr const char* kPresenceName[4] = { "Visible", "Locked", "Construction", "Dimensions" };
constexpr ImU32 kGreen    = IM_COL32(52, 199, 89, 255);
constexpr ImU32 kAxis[3]  = { IM_COL32(239, 83, 80, 255), IM_COL32(102, 214, 120, 255), IM_COL32(86, 156, 255, 255) };
constexpr const char* kLetter[3] = { "X", "Y", "Z" };

constexpr float kMargin = 10.0f;    // [px] between the panel edge and a card
constexpr float kPad    = 16.0f;    // [px] inside a card
constexpr float kGap    = 10.0f;    // [px] between cards
constexpr float kRadius = 22.0f;    // [px] card corner
constexpr float kCell   = 28.0f;    // [px] numeric field height
constexpr float kTileH  = 60.0f;    // [px] stat tile height

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRIMITIVE HELPERS
//------------------------------------------------------------------------------------------------------------------------

ImU32 WithAlpha(ImU32 Tint, uint32_t Alpha) noexcept
{
    return (Tint & 0x00FFFFFFu) | (Alpha << 24u);
}

ImU32 TintOf(const float Tint[3], uint32_t Alpha) noexcept
{
    return IM_COL32(static_cast<int>(Tint[0] * 255.0f), static_cast<int>(Tint[1] * 255.0f), static_cast<int>(Tint[2] * 255.0f),
                    static_cast<int>(Alpha));
}

// Right-aligned numeral: the whole part in ink, the decimals dimmed, the way the reference prints "78.3".
float DrawNumeral(ImDrawList* Draw, ImFont* Font, float Size, float RightX, float TopY, const char* Text,
                  ImU32 Ink, ImU32 Dim) noexcept
{
    const char* Point = std::strchr(Text, '.');
    char Whole[40] = {};
    std::snprintf(Whole, sizeof(Whole), "%.*s", Point != nullptr ? static_cast<int>(Point - Text) : static_cast<int>(std::strlen(Text)), Text);
    const float WholeW = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Whole).x;
    const float TailW  = Point != nullptr ? Font->CalcTextSizeA(Size * 0.74f, FLT_MAX, 0.0f, Point).x : 0.0f;
    const float X0     = RightX - WholeW - TailW;
    Draw->AddText(Font, Size, ImVec2(X0, TopY), Ink, Whole);
    if (Point != nullptr)
        Draw->AddText(Font, Size * 0.74f, ImVec2(X0 + WholeW, TopY + Size * 0.20f), Dim, Point);
    return WholeW + TailW;
}

float NumeralWidth(ImFont* Font, float Size, const char* Text) noexcept
{
    const char* Point = std::strchr(Text, '.');
    char Whole[40] = {};
    std::snprintf(Whole, sizeof(Whole), "%.*s", Point != nullptr ? static_cast<int>(Point - Text) : static_cast<int>(std::strlen(Text)), Text);
    return Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Whole).x + (Point != nullptr ? Font->CalcTextSizeA(Size * 0.74f, FLT_MAX, 0.0f, Point).x : 0.0f);
}

bool IsNumber(const char* Text) noexcept
{
    if (Text == nullptr || Text[0] == '\0')
        return false;
    char* End = nullptr;
    (void)std::strtod(Text, &End);
    return End != nullptr && *End == '\0';
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         GLYPHS
//------------------------------------------------------------------------------------------------------------------------

enum class Glyph : uint32_t { Face, Edge, Vertex, Volume, Area, Dots, Line, Profile, Body, Surface, Construction, Dimension, Constraint };

Glyph GlyphForLabel(const char* Label) noexcept
{
    if (std::strcmp(Label, "Faces") == 0)    return Glyph::Face;
    if (std::strcmp(Label, "Edges") == 0)    return Glyph::Edge;
    if (std::strcmp(Label, "Vertices") == 0) return Glyph::Vertex;
    if (std::strcmp(Label, "Volume") == 0)   return Glyph::Volume;
    if (std::strcmp(Label, "Area") == 0)     return Glyph::Area;
    if (std::strcmp(Label, "Bodies") == 0)       return Glyph::Body;
    if (std::strcmp(Label, "Sketches") == 0)     return Glyph::Line;
    if (std::strcmp(Label, "Surfaces") == 0)     return Glyph::Surface;
    if (std::strcmp(Label, "Construction") == 0) return Glyph::Construction;
    if (std::strcmp(Label, "Dimensions") == 0)   return Glyph::Dimension;
    if (std::strcmp(Label, "Constraints") == 0)  return Glyph::Constraint;
    return Glyph::Dots;
}

ImU32 GlyphTint(Glyph Shape) noexcept
{
    switch (Shape)
    {
    case Glyph::Face:   return IM_COL32(77, 163, 255, 255);
    case Glyph::Edge:   return IM_COL32(79, 216, 224, 255);
    case Glyph::Vertex: return IM_COL32(255, 180, 84, 255);
    case Glyph::Volume: return IM_COL32(180, 140, 255, 255);
    case Glyph::Area:   return IM_COL32(255, 107, 138, 255);
    case Glyph::Line:         return IM_COL32(79, 216, 224, 255);
    case Glyph::Body:         return IM_COL32(255, 180, 84, 255);
    case Glyph::Surface:      return IM_COL32(77, 163, 255, 255);
    case Glyph::Construction: return IM_COL32(180, 140, 255, 255);
    case Glyph::Dimension:    return IM_COL32(229, 211, 58, 255);
    case Glyph::Constraint:   return IM_COL32(255, 107, 138, 255);
    default:            return IM_COL32(150, 156, 168, 255);
    }
}

void DrawGlyph(ImDrawList* Draw, Glyph Shape, const ImVec2& C, float H, ImU32 Ink) noexcept
{
    const float T = 1.5f;
    switch (Shape)
    {
    case Glyph::Face:
        Draw->AddRect(ImVec2(C.x - H, C.y - H), ImVec2(C.x + H, C.y + H), Ink, 2.0f, 0, T);
        Draw->AddRectFilled(ImVec2(C.x - H * 0.45f, C.y - H * 0.45f), ImVec2(C.x + H * 0.45f, C.y + H * 0.45f), WithAlpha(Ink, 120), 1.0f);
        break;
    case Glyph::Edge:
        Draw->AddLine(ImVec2(C.x - H, C.y + H), ImVec2(C.x + H, C.y - H), Ink, T + 0.5f);
        Draw->AddCircleFilled(ImVec2(C.x - H, C.y + H), 1.8f, Ink);
        Draw->AddCircleFilled(ImVec2(C.x + H, C.y - H), 1.8f, Ink);
        break;
    case Glyph::Vertex:
        Draw->AddCircleFilled(C, H * 0.55f, Ink);
        Draw->AddCircle(C, H, WithAlpha(Ink, 130), 20, T);
        break;
    case Glyph::Volume:
    {
        const ImVec2 Top[4] = { ImVec2(C.x, C.y - H), ImVec2(C.x + H, C.y - H * 0.45f), ImVec2(C.x, C.y + H * 0.1f), ImVec2(C.x - H, C.y - H * 0.45f) };
        Draw->AddConvexPolyFilled(Top, 4, WithAlpha(Ink, 130));
        Draw->AddPolyline(Top, 4, Ink, ImDrawFlags_Closed, T);
        Draw->AddLine(ImVec2(C.x - H, C.y - H * 0.45f), ImVec2(C.x - H, C.y + H * 0.55f), Ink, T);
        Draw->AddLine(ImVec2(C.x + H, C.y - H * 0.45f), ImVec2(C.x + H, C.y + H * 0.55f), Ink, T);
        Draw->AddLine(ImVec2(C.x, C.y + H * 0.1f), ImVec2(C.x, C.y + H), Ink, T);
        Draw->AddLine(ImVec2(C.x - H, C.y + H * 0.55f), ImVec2(C.x, C.y + H), Ink, T);
        Draw->AddLine(ImVec2(C.x + H, C.y + H * 0.55f), ImVec2(C.x, C.y + H), Ink, T);
        break;
    }
    case Glyph::Area:
    {
        const ImVec2 Quad[4] = { ImVec2(C.x - H, C.y + H * 0.6f), ImVec2(C.x - H * 0.35f, C.y - H * 0.7f), ImVec2(C.x + H, C.y - H * 0.6f), ImVec2(C.x + H * 0.35f, C.y + H * 0.7f) };
        Draw->AddConvexPolyFilled(Quad, 4, WithAlpha(Ink, 110));
        Draw->AddPolyline(Quad, 4, Ink, ImDrawFlags_Closed, T);
        break;
    }
    case Glyph::Line:
        Draw->AddLine(ImVec2(C.x - H, C.y + H), ImVec2(C.x + H, C.y - H), Ink, 2.0f);
        break;
    case Glyph::Profile:
        Draw->AddCircle(C, H, Ink, 24, 2.0f);
        break;
    case Glyph::Body:
        DrawGlyph(Draw, Glyph::Volume, C, H, Ink);
        break;
    case Glyph::Surface:
        DrawGlyph(Draw, Glyph::Area, C, H, Ink);
        break;
    case Glyph::Construction:
        Draw->AddRect(ImVec2(C.x - H, C.y - H * 0.7f), ImVec2(C.x + H, C.y + H * 0.7f), WithAlpha(Ink, 200), 1.5f, 0, 1.2f);
        break;
    case Glyph::Dimension:
        Draw->AddLine(ImVec2(C.x - H, C.y), ImVec2(C.x + H, C.y), Ink, T);
        Draw->AddLine(ImVec2(C.x - H, C.y - H * 0.6f), ImVec2(C.x - H, C.y + H * 0.6f), Ink, T);
        Draw->AddLine(ImVec2(C.x + H, C.y - H * 0.6f), ImVec2(C.x + H, C.y + H * 0.6f), Ink, T);
        break;
    case Glyph::Constraint:
        Draw->AddCircle(ImVec2(C.x - H * 0.45f, C.y), H * 0.65f, Ink, 16, T);
        Draw->AddCircle(ImVec2(C.x + H * 0.45f, C.y), H * 0.65f, Ink, 16, T);
        break;
    default:
        for (int I = -1; I <= 1; ++I)
            Draw->AddCircleFilled(ImVec2(C.x + static_cast<float>(I) * H * 0.7f, C.y), 1.9f, Ink);
        break;
    }
}

Glyph GlyphForMark(EditorSymbol Mark) noexcept
{
    switch (Mark)
    {
    case EditorSymbol::Line:         return Glyph::Line;
    case EditorSymbol::Profile:      return Glyph::Profile;
    case EditorSymbol::Body:         return Glyph::Body;
    case EditorSymbol::Surface:      return Glyph::Surface;
    case EditorSymbol::Construction: return Glyph::Construction;
    case EditorSymbol::Dimension:    return Glyph::Dimension;
    case EditorSymbol::Constraint:   return Glyph::Constraint;
    default:                           return Glyph::Dots;
    }
}

// The arrow-up-right at a card's top-right; a shut card turns it down-right.
void DrawArrow(ImDrawList* Draw, const ImVec2& C, bool Shut, bool Hot) noexcept
{
    const float S = Shut ? -1.0f : 1.0f;
    Draw->AddCircle(C, 13.0f, Hot ? IM_COL32(255, 255, 255, 60) : kHair, 28, 1.0f);
    const ImU32 Ink = Hot ? kInk : kDim;
    Draw->AddLine(ImVec2(C.x - 4.0f, C.y + 4.0f * S), ImVec2(C.x + 4.0f, C.y - 4.0f * S), Ink, 1.4f);
    Draw->AddLine(ImVec2(C.x - 0.5f, C.y - 4.0f * S), ImVec2(C.x + 4.0f, C.y - 4.0f * S), Ink, 1.4f);
    Draw->AddLine(ImVec2(C.x + 4.0f, C.y - 4.0f * S), ImVec2(C.x + 4.0f, C.y - 4.0f * S + 4.5f * S), Ink, 1.4f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TYPE-IN STATE
//------------------------------------------------------------------------------------------------------------------------

struct TypeIn
{
    ImGuiID Id      = 0u;
    char    Text[32] = {};
    bool    Focus   = false;
    bool    Dragged = false;
};

TypeIn gTypeIn;
char   gNameText[48] = {};
uint64_t gNameFor = ~0ull;

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------

struct Layout
{
    ControlPanel* Controls = nullptr;
    ImDrawList*   Draw     = nullptr;
    ImFont*       Ui       = nullptr;
    float         X        = 0.0f;     // card left
    float         W        = 0.0f;     // card width
    float         Y        = 0.0f;     // running top
    float         CardTop  = 0.0f;

    float InnerX() const noexcept { return X + kPad; }
    float InnerW() const noexcept { return W - 2.0f * kPad; }
};

void Text(Layout& L, float Size, const ImVec2& At, ImU32 Tint, const char* Value) noexcept
{
    L.Draw->AddText(L.Ui, Size, At, Tint, Value);
}

float TextWidth(Layout& L, float Size, const char* Value) noexcept
{
    return L.Ui->CalcTextSizeA(Size, FLT_MAX, 0.0f, Value).x;
}

// Text trimmed with an ellipsis to fit MaxW.
void FitText(Layout& L, float Size, const ImVec2& At, ImU32 Tint, const char* Value, float MaxW) noexcept
{
    char Buffer[96] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%s", Value);
    size_t Length = std::strlen(Buffer);
    while (Length > 3u && TextWidth(L, Size, Buffer) > MaxW)
    {
        Length -= 1u;
        Buffer[Length] = '\0';
        if (Length >= 2u)
        {
            Buffer[Length - 1u] = '.';
            Buffer[Length - 2u] = '.';
        }
    }
    Text(L, Size, At, Tint, Buffer);
}

// The UTF-8 sequence starting at P: its byte length.
size_t Utf8Length(const char* P) noexcept
{
    const unsigned char C = static_cast<unsigned char>(*P);
    return C < 0x80u ? 1u : (C >= 0xF0u ? 4u : (C >= 0xE0u ? 3u : 2u));
}

// Letter-spaced small caps, the label voice of the cards. Multi-byte marks (the middle dot) pass through whole.
void Caps(Layout& L, const ImVec2& At, ImU32 Tint, const char* Value) noexcept
{
    float X = At.x;
    for (const char* P = Value; *P != '\0'; )
    {
        char One[5] = {};
        const size_t Length = Utf8Length(P);
        for (size_t I = 0u; I < Length && P[I] != '\0'; ++I)
            One[I] = Length == 1u ? static_cast<char>(std::toupper(static_cast<unsigned char>(P[I]))) : P[I];
        L.Draw->AddText(L.Ui, 10.0f, ImVec2(X, At.y), Tint, One);
        X += L.Ui->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, One).x + 1.2f;
        P += Length;
    }
}

float gAnchor[kMaxEditorSheetGroups] = {};      // content-space tops of last frame's cards, for the tab chips

float ContentY(float ScreenY) noexcept
{
    return ScreenY - ImGui::GetWindowPos().y + ImGui::GetScrollY();
}

// Opens a card: the title with its dim caption beside it, and the fold arrow. The flat fill is painted behind the content at
//    CardEnd, once the height is known.
bool CardBegin(Layout& L, const char* Id, const char* Title, const char* Subtitle, bool* Shut) noexcept
{
    L.CardTop = L.Y;
    L.Draw->ChannelsSplit(2);
    L.Draw->ChannelsSetCurrent(1);
    ImGui::PushID(Id);

    const float TextW = L.InnerW() - (Shut != nullptr ? 34.0f : 0.0f);
    const float TitleW = std::min(TextWidth(L, 17.0f, Title), TextW);
    FitText(L, 17.0f, ImVec2(L.InnerX(), L.Y + 14.0f), kInk, Title, TextW);
    if (Subtitle != nullptr && Subtitle[0] != '\0' && TextW - TitleW > 40.0f)
        FitText(L, 11.0f, ImVec2(L.InnerX() + TitleW + 10.0f, L.Y + 20.0f), kFaint, Subtitle, TextW - TitleW - 10.0f);

    if (Shut == nullptr)
    {
        L.Y += 52.0f;
        return true;
    }
    const ImVec2 ArrowC(L.X + L.W - kPad - 13.0f, L.Y + 24.0f);
    ImGui::SetCursorScreenPos(ImVec2(ArrowC.x - 14.0f, ArrowC.y - 14.0f));
    ImGui::InvisibleButton("##arrow", ImVec2(28.0f, 28.0f));
    if (ImGui::IsItemClicked())
        *Shut = !*Shut;
    DrawArrow(L.Draw, ArrowC, *Shut, ImGui::IsItemHovered());

    L.Y += 52.0f;
    return !*Shut;
}

void CardEnd(Layout& L, bool Shut) noexcept
{
    L.Y += Shut ? 0.0f : kPad - 2.0f;
    L.Draw->ChannelsSetCurrent(0);
    const ImVec2 Min(L.X, L.CardTop), Max(L.X + L.W, L.Y);
    L.Draw->AddRectFilled(Min, Max, kCardFill, kRadius);
    L.Draw->AddRect(Min, Max, kHair, kRadius, 0, 1.0f);
    L.Draw->ChannelsMerge();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, kGap));
    L.Y += kGap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NUMERIC CELL + TRACK
//------------------------------------------------------------------------------------------------------------------------

// One numeric field: a rounded well, the numeral right-aligned. A drag on it scrubs; a click types in.
bool NumberCell(Layout& L, const char* Id, const ImVec2& Min, float Width, float* Value, float Step, int Decimals, int Axis,
                bool Editable, ImU32 Fill = 0u, bool Centred = true) noexcept
{
    bool Changed = false;
    const ImVec2 Max(Min.x + Width, Min.y + kCell);
    const ImGuiID CellId = ImGui::GetID(Id);
    const bool Typing = gTypeIn.Id == CellId;

    ImGui::SetCursorScreenPos(Min);
    bool Hot = false;
    if (!Typing)
    {
        ImGui::InvisibleButton(Id, ImVec2(Width, kCell));
        Hot = ImGui::IsItemHovered() && Editable;
        if (Editable)
        {
            if (ImGui::IsItemActivated())
                gTypeIn.Dragged = false;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 2.0f))
            {
                *Value += ImGui::GetIO().MouseDelta.x * Step;
                gTypeIn.Dragged = true;
                Changed = true;
            }
            if (ImGui::IsItemDeactivated() && !gTypeIn.Dragged && Hot)
            {
                std::snprintf(gTypeIn.Text, sizeof(gTypeIn.Text), "%.*f", Decimals, static_cast<double>(*Value));
                gTypeIn.Id    = CellId;
                gTypeIn.Focus = true;
            }
            if (Hot || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
    }

    if (Fill != 0u)
        L.Draw->AddRectFilled(Min, Max, Fill, kCell * 0.5f);
    else
    {
        L.Draw->AddRectFilled(Min, Max, Hot ? kTileHot : kTile, kCell * 0.5f);
        L.Draw->AddRect(Min, Max, Typing ? WithAlpha(kAxis[Axis], 170) : kHair, kCell * 0.5f, 0, 1.0f);
    }

    if (Typing)
    {
        ImGui::SetCursorScreenPos(ImVec2(Min.x + 4.0f, Min.y + 3.0f));
        ImGui::PushItemWidth(Width - 8.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
        ImGui::PushFont(L.Ui, 13.0f);
        if (gTypeIn.Focus)
        {
            ImGui::SetKeyboardFocusHere();
            gTypeIn.Focus = false;
        }
        const bool Done = ImGui::InputText("##typein", gTypeIn.Text, sizeof(gTypeIn.Text),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsDecimal);
        const bool Left = ImGui::IsItemDeactivated();
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();
        if (Done || (Left && ImGui::IsItemDeactivatedAfterEdit()))
        {
            *Value  = static_cast<float>(std::atof(gTypeIn.Text));
            Changed = true;
        }
        if (Done || Left)
            gTypeIn.Id = 0u;
    }
    else
    {
        char Shown[32] = {};
        std::snprintf(Shown, sizeof(Shown), "%.*f", Decimals, static_cast<double>(*Value));
        const float Right = Centred ? Min.x + Width * 0.5f + NumeralWidth(L.Ui, 13.0f, Shown) * 0.5f : Max.x - 12.0f;
        DrawNumeral(L.Draw, L.Ui, 13.0f, Right, Min.y + 7.0f, Shown, Editable ? kInk : IM_COL32(190, 194, 202, 255), kDim);
    }
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROPERTY ROWS
//------------------------------------------------------------------------------------------------------------------------

void RowSwitch(Layout& L, EditorProperty& Property) noexcept
{
    ImGui::PushID(Property.Label);
    Text(L, 12.5f, ImVec2(L.InnerX(), L.Y + 6.0f), kInk, Property.Label);
    ImGui::SetCursorScreenPos(ImVec2(L.InnerX() + L.InnerW() - 46.0f, L.Y));
    ImGui::BeginChild("##switch", ImVec2(46.0f, 26.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    L.Controls->Switch("##s", &Property.On);
    ImGui::EndChild();
    ImGui::PopID();
    L.Y += 34.0f;
}

void RowSelect(Layout& L, EditorProperty& Property) noexcept
{
    ImGui::PushID(Property.Label);
    Caps(L, ImVec2(L.InnerX(), L.Y), kDim, Property.Label);
    ImGui::SetCursorScreenPos(ImVec2(L.InnerX(), L.Y + 18.0f));
    ImGui::BeginChild("##select", ImVec2(L.InnerW(), 32.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    L.Controls->DropDown("##d", &Property.Picked, Property.Options, Property.OptionCount);
    ImGui::EndChild();
    ImGui::PopID();
    L.Y += 58.0f;
}

void RowColour(Layout& L, EditorProperty& Property) noexcept
{
    ImGui::PushID(Property.Label);
    Caps(L, ImVec2(L.InnerX(), L.Y), kDim, Property.Label);
    ImGui::SetCursorScreenPos(ImVec2(L.InnerX(), L.Y + 18.0f));
    ImGui::BeginChild("##colour", ImVec2(L.InnerW(), 28.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (Property.Swatches)
        L.Controls->SwatchRow("##sw", Property.ColourTint);
    else
        L.Controls->ColourChip("##chip", Property.ColourTint);
    ImGui::EndChild();
    ImGui::PopID();
    L.Y += 54.0f;
}

void RowReadout(Layout& L, const EditorProperty& Property) noexcept
{
    Text(L, 12.0f, ImVec2(L.InnerX(), L.Y), kDim, Property.Label);
    const float LabelW = TextWidth(L, 12.0f, Property.Label) + 10.0f;
    const float W = std::min(TextWidth(L, 12.5f, Property.Text), L.InnerW() - LabelW);
    FitText(L, 12.5f, ImVec2(L.InnerX() + L.InnerW() - W, L.Y), kInk, Property.Text, W);
    L.Y += 26.0f;
    L.Draw->AddLine(ImVec2(L.InnerX(), L.Y - 7.0f), ImVec2(L.InnerX() + L.InnerW(), L.Y - 7.0f), kHair, 1.0f);
}

// Numeric readouts become stat tiles, two to a row; the rest stay readout rows beneath them.
void TilesAndReadouts(Layout& L, EditorPropertyGroup& Group) noexcept
{
    constexpr float Gap = 8.0f;
    // Roomy docks pair the tiles two to a row; a narrow dock stacks them one to a row, so no label or numeral is cropped.
    const bool  Paired = L.InnerW() >= 240.0f;
    const float TileW  = Paired ? (L.InnerW() - Gap) * 0.5f : L.InnerW();
    const float TileHt = Paired ? kTileH : 46.0f;
    uint32_t Count = 0u;
    for (uint32_t I = 0u; I < Group.PropertyCount; ++I)
    {
        const EditorProperty& Property = Group.Properties[I];
        if (Property.Category != EditorPropertyCategory::Readout || !IsNumber(Property.Text))
            continue;
        const float X = L.InnerX() + (Paired ? static_cast<float>(Count & 1u) * (TileW + Gap) : 0.0f);
        const ImVec2 Min(X, L.Y), Max(X + TileW, L.Y + TileHt);
        L.Draw->AddRectFilled(Min, Max, kTile, 16.0f);
        L.Draw->AddRect(Min, Max, kHair, 16.0f);
        const Glyph Shape  = GlyphForLabel(Property.Label);
        const bool  Whole = std::strchr(Property.Text, '.') == nullptr;
        if (Paired)
        {
            // Icon badge top-left, label under it, numeral right.
            const ImVec2 Badge(Min.x + 14.0f + 11.0f, Min.y + 14.0f + 11.0f);
            L.Draw->AddCircleFilled(Badge, 12.0f, WithAlpha(GlyphTint(Shape), 38));
            DrawGlyph(L.Draw, Shape, Badge, 5.0f, GlyphTint(Shape));
            Text(L, 11.0f, ImVec2(Min.x + 14.0f, Max.y - 20.0f), kDim, Property.Label);
            DrawNumeral(L.Draw, L.Ui, Whole ? 28.0f : 23.0f, Max.x - 14.0f, Min.y + (Whole ? 12.0f : 17.0f), Property.Text, kInk, kDim);
        }
        else
        {
            // Row: icon badge, label, numeral right-aligned.
            const ImVec2 Badge(Min.x + 14.0f + 10.0f, Min.y + TileHt * 0.5f);
            L.Draw->AddCircleFilled(Badge, 11.0f, WithAlpha(GlyphTint(Shape), 38));
            DrawGlyph(L.Draw, Shape, Badge, 4.6f, GlyphTint(Shape));
            Text(L, 11.5f, ImVec2(Min.x + 14.0f + 28.0f, Min.y + TileHt * 0.5f - 7.0f), kDim, Property.Label);
            DrawNumeral(L.Draw, L.Ui, 22.0f, Max.x - 14.0f, Min.y + 10.0f, Property.Text, kInk, kDim);
        }
        if (!Paired || (Count & 1u) == 1u)
            L.Y += TileHt + Gap;
        ++Count;
    }
    if (Paired && (Count & 1u) == 1u)
        L.Y += TileHt + Gap;
    if (Count > 0u)
        L.Y += 4.0f;
    for (uint32_t I = 0u; I < Group.PropertyCount; ++I)
    {
        const EditorProperty& Property = Group.Properties[I];
        if (Property.Category == EditorPropertyCategory::Readout && !IsNumber(Property.Text))
            RowReadout(L, Property);
    }
}

bool GroupHasTiles(const EditorPropertyGroup& Group) noexcept
{
    if (std::strcmp(Group.Title, "Topology") != 0 && std::strcmp(Group.Title, "Contents") != 0)
        return false;
    uint32_t Numeric = 0u;
    for (uint32_t I = 0u; I < Group.PropertyCount; ++I)
        if (Group.Properties[I].Category == EditorPropertyCategory::Readout && IsNumber(Group.Properties[I].Text))
            ++Numeric;
    return Numeric >= 2u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     AXIS TABLES + SLIDERS
//------------------------------------------------------------------------------------------------------------------------

struct AxisUnit
{
    const char* Label;
    const char* Unit;
    float       Step;
    int         Decimals;
};

AxisUnit UnitFor(const char* Label) noexcept
{
    if (std::strcmp(Label, "Position") == 0) return AxisUnit{ Label, "m", 0.01f, 3 };
    if (std::strcmp(Label, "Rotation") == 0) return AxisUnit{ Label, "deg", 0.40f, 1 };
    if (std::strcmp(Label, "Scale") == 0)    return AxisUnit{ Label, "x", 0.005f, 3 };
    return AxisUnit{ Label, "m", 0.1f, 3 };
}

constexpr float kLabelCol = 62.0f;      // [px] the row-label column of an axis table

// The X / Y / Z header, then one row of three value pills per property. Editable rows scrub and type in.
void AxisTable(Layout& L, EditorProperty* const* Rows, uint32_t Count) noexcept
{
    const float Gap     = 6.0f;
    const float LabelW  = std::min(kLabelCol, L.InnerW() * 0.26f);
    const float PillW   = (L.InnerW() - LabelW - 2.0f * Gap) / 3.0f;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Cx = L.InnerX() + LabelW + static_cast<float>(Axis) * (PillW + Gap) + PillW * 0.5f;
        Text(L, 10.0f, ImVec2(Cx - TextWidth(L, 10.0f, kLetter[Axis]) * 0.5f, L.Y), kAxis[Axis], kLetter[Axis]);
    }
    L.Y += 18.0f;
    for (uint32_t Row = 0u; Row < Count; ++Row)
    {
        EditorProperty& Property = *Rows[Row];
        const AxisUnit Unit = UnitFor(Property.Label);
        ImGui::PushID(Property.Label);
        Text(L, 12.0f, ImVec2(L.InnerX(), L.Y + 1.0f), kDim, Property.Label);
        if (Property.Editable)
            Text(L, 9.5f, ImVec2(L.InnerX(), L.Y + 15.0f), kFaint, Unit.Unit);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            char Id[16] = {};
            std::snprintf(Id, sizeof(Id), "##c%d", Axis);
            const float X = L.InnerX() + LabelW + static_cast<float>(Axis) * (PillW + Gap);
            NumberCell(L, Id, ImVec2(X, L.Y), PillW, &Property.Axes[Axis], Unit.Step, Unit.Decimals, Axis, Property.Editable, 0u, true);
        }
        ImGui::PopID();
        L.Y += kCell + 8.0f;
    }
}

// The engine capsule: a black value pill with its unit cell, then a long grey capsule with a big light knob.
bool CapsuleRow(Layout& L, const char* Id, float* Value, float Low, float High, int Decimals, const char* Unit) noexcept
{
    ImGui::PushID(Id);
    const float PillW = 58.0f, UnitW = 30.0f;
    NumberCell(L, "##v", ImVec2(L.InnerX(), L.Y), PillW, Value, (High - Low) / 400.0f, Decimals, 0, true, kBlack, true);
    const float UnitX = L.InnerX() + PillW - kCell * 0.5f;
    L.Draw->AddRectFilled(ImVec2(UnitX + kCell * 0.5f, L.Y), ImVec2(UnitX + kCell * 0.5f + UnitW, L.Y + kCell), IM_COL32(8, 8, 9, 255), kCell * 0.5f);
    Text(L, 11.0f, ImVec2(UnitX + kCell * 0.5f + 8.0f, L.Y + 8.0f), kDim, Unit);

    const float TrackX = L.InnerX() + PillW + UnitW + 8.0f;
    const float TrackW = L.InnerX() + L.InnerW() - TrackX;
    bool Changed = false;
    if (TrackW > 40.0f)
    {
        ImGui::SetCursorScreenPos(ImVec2(TrackX, L.Y));
        ImGui::InvisibleButton("##cap", ImVec2(TrackW, kCell));
        if (ImGui::IsItemActive())
        {
            const float T = std::clamp((ImGui::GetIO().MousePos.x - TrackX - 14.0f) / std::max(1.0f, TrackW - 28.0f), 0.0f, 1.0f);
            const float V = Low + (High - Low) * T;
            if (V != *Value)
            {
                *Value  = V;
                Changed = true;
            }
        }
        const float T  = std::clamp((*Value - Low) / (High - Low), 0.0f, 1.0f);
        const float Kx = TrackX + 14.0f + (TrackW - 28.0f) * T;
        const ImVec2 Min(TrackX, L.Y + 3.0f), Max(TrackX + TrackW, L.Y + kCell - 3.0f);
        L.Draw->AddRectFilled(Min, Max, kCapTrack, 11.0f);
        L.Draw->AddRectFilled(Min, ImVec2(Kx + 4.0f, Max.y), kCapFill, 11.0f);
        L.Draw->AddCircleFilled(ImVec2(Kx, L.Y + kCell * 0.5f), 12.0f, kKnob);
    }
    ImGui::PopID();
    L.Y += kCell + 8.0f;
    return Changed;
}

EditorProperty* FindProperty(EditorPropertyGroup& Group, const char* Label) noexcept
{
    for (uint32_t I = 0u; I < Group.PropertyCount; ++I)
        if (std::strcmp(Group.Properties[I].Label, Label) == 0)
            return &Group.Properties[I];
    return nullptr;
}

// A small chip with the reset glyph, for the transform card's pos / rot / scale resets.
bool ResetChip(Layout& L, const char* Id, const char* Label, float Right, float Y) noexcept
{
    const float W = TextWidth(L, 11.0f, Label) + 38.0f;
    const ImVec2 Min(Right - W, Y), Max(Right, Y + 26.0f);
    ImGui::SetCursorScreenPos(Min);
    const bool Hit = ImGui::InvisibleButton(Id, ImVec2(W, 26.0f));
    L.Draw->AddRectFilled(Min, Max, ImGui::IsItemHovered() ? kTileHot : kTile, 13.0f);
    L.Draw->AddRect(Min, Max, kHair, 13.0f);
    const ImVec2 C(Min.x + 16.0f, Min.y + 13.0f);
    L.Draw->PathArcTo(C, 4.2f, 0.6f, 5.6f, 14);
    L.Draw->PathStroke(kDim, 0, 1.3f);
    Text(L, 11.0f, ImVec2(Min.x + 27.0f, Min.y + 6.0f), kInk, Label);
    return Hit;
}

void RecordTransform(Layout& L, EditorPropertyGroup& Group) noexcept
{
    EditorProperty* Rows[3] = {};
    uint32_t Count = 0u;
    for (const char* Name : { "Position", "Rotation", "Scale" })
        if (EditorProperty* Property = FindProperty(Group, Name))
            Rows[Count++] = Property;
    if (Count > 0u)
        AxisTable(L, Rows, Count);
    if (EditorProperty* Uniform = FindProperty(Group, "Uniform scale"))
    {
        L.Y += 2.0f;
        Text(L, 12.5f, ImVec2(L.InnerX(), L.Y), kInk, Uniform->Label);
        L.Y += 22.0f;
        if (CapsuleRow(L, Uniform->Label, &Uniform->Figure, Uniform->Minimum, Uniform->Maximum, static_cast<int>(Uniform->Decimals), Uniform->Unit))
            Uniform->Figure = std::clamp(Uniform->Figure, Uniform->Minimum, Uniform->Maximum);
    }
    if (Count > 0u)
    {
        // The reset chips hang from the right edge, wrapping to fit a narrow dock.
        const char* Names[3] = { "pos", "rot", "scale" };
        const float Defaults[3] = { 0.0f, 0.0f, 1.0f };
        float Right = L.InnerX() + L.InnerW();
        for (int I = 2; I >= 0; --I)
        {
            const float W = TextWidth(L, 11.0f, Names[I]) + 38.0f;
            if (Right - W < L.InnerX())
            {
                Right = L.InnerX() + L.InnerW();
                L.Y += 32.0f;
            }
            char Id[16] = {};
            std::snprintf(Id, sizeof(Id), "##reset%d", I);
            if (ResetChip(L, Id, Names[I], Right, L.Y) && Rows[I] != nullptr)
                for (float& Axis : Rows[I]->Axes)
                    Axis = Defaults[I];
            Right -= W + 6.0f;
        }
        L.Y += 30.0f;
    }
}

void RecordBounds(Layout& L, EditorPropertyGroup& Group) noexcept
{
    EditorProperty* Size = FindProperty(Group, "Size");
    if (Size != nullptr)
    {
        // The extent as one big line: 30.0 × 22.0 × 14.0 — the decimals dim, the multiply signs fainter.
        float Fontsize = 26.0f;
        char Part[3][24] = {};
        for (int I = 0; I < 3; ++I)
            std::snprintf(Part[I], sizeof(Part[I]), "%.2f", static_cast<double>(Size->Axes[I]));
        float Total = 0.0f;
        for (;;)
        {
            Total = 0.0f;
            for (int I = 0; I < 3; ++I)
                Total += NumeralWidth(L.Ui, Fontsize, Part[I]) + (I < 2 ? Fontsize * 0.9f : 0.0f);
            if (Total <= L.InnerW() || Fontsize <= 14.0f)
                break;
            Fontsize -= 1.0f;
        }
        float X = L.InnerX();
        for (int I = 0; I < 3; ++I)
        {
            X += DrawNumeral(L.Draw, L.Ui, Fontsize, X + NumeralWidth(L.Ui, Fontsize, Part[I]), L.Y, Part[I], kInk, IM_COL32(255, 255, 255, 80));
            if (I < 2)
            {
                Text(L, Fontsize * 0.6f, ImVec2(X + Fontsize * 0.22f, L.Y + Fontsize * 0.3f), kFaint, "\xc3\x97");
                X += Fontsize * 0.9f;
            }
        }
        L.Y += Fontsize + 14.0f;
    }
    EditorProperty* Rows[3] = {};
    uint32_t Count = 0u;
    for (const char* Name : { "Min", "Max", "Centre" })
        if (EditorProperty* Property = FindProperty(Group, Name))
            Rows[Count++] = Property;
    if (Count > 0u)
        AxisTable(L, Rows, Count);
}

void RecordGroup(Layout& L, EditorPropertyGroup& Group, uint32_t Index, bool* Shut) noexcept
{
    if (Index < kMaxEditorSheetGroups)
        gAnchor[Index] = ContentY(L.Y);
    if (!CardBegin(L, Group.Title, Group.Title, Group.Caption, Shut))
    {
        CardEnd(L, true);
        return;
    }
    if (std::strcmp(Group.Title, "Transform") == 0)
    {
        RecordTransform(L, Group);
    }
    else if (std::strcmp(Group.Title, "Bounds") == 0)
    {
        RecordBounds(L, Group);
    }
    else if (GroupHasTiles(Group))
    {
        TilesAndReadouts(L, Group);
    }
    else
    {
        for (uint32_t I = 0u; I < Group.PropertyCount; ++I)
        {
            EditorProperty& Property = Group.Properties[I];
            switch (Property.Category)
            {
            case EditorPropertyCategory::Switch:   RowSwitch(L, Property);   break;
            case EditorPropertyCategory::Colour:   RowColour(L, Property);   break;
            case EditorPropertyCategory::Select:   RowSelect(L, Property);   break;
            case EditorPropertyCategory::Readout:  RowReadout(L, Property);  break;
            default: break;
            }
        }
    }
    CardEnd(L, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              HEAD, HERO, PRESENCE, CHIPS, ACTIONS
//------------------------------------------------------------------------------------------------------------------------

float CapsWidth(Layout& L, const char* Value) noexcept
{
    float W = 0.0f;
    for (const char* P = Value; *P != '\0'; )
    {
        char One[5] = {};
        const size_t Length = Utf8Length(P);
        for (size_t I = 0u; I < Length && P[I] != '\0'; ++I)
            One[I] = P[I];
        W += L.Ui->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, One).x + 1.2f;
        P += Length;
    }
    return W;
}

// The head: the accent dot, "Inspector", and the subject pill on the right ("BODY · #8").
void HeadBar(Layout& L, const EditorSheetHero& Hero, ImU32 Accent) noexcept
{
    const float Cy = L.Y + 22.0f;
    L.Draw->AddCircleFilled(ImVec2(L.X + 10.0f, Cy), 4.5f, Accent);
    L.Draw->AddCircle(ImVec2(L.X + 10.0f, Cy), 8.0f, WithAlpha(Accent, 60), 20, 1.5f);
    Text(L, 17.0f, ImVec2(L.X + 26.0f, Cy - 10.0f), kInk, "Inspector");

    char Pill[40] = {};
    if (Hero.Identity != 0u)
        std::snprintf(Pill, sizeof(Pill), "%s \xc2\xb7 #%u", Hero.Subject, Hero.Identity);
    else
        std::snprintf(Pill, sizeof(Pill), "%s", Hero.Subject);
    float W = CapsWidth(L, Pill) + 22.0f;
    if (L.X + L.W - W < L.X + 26.0f + TextWidth(L, 17.0f, "Inspector") + 10.0f && Hero.Identity != 0u)
    {
        std::snprintf(Pill, sizeof(Pill), "#%u", Hero.Identity);     // a narrow dock keeps the number only
        W = CapsWidth(L, Pill) + 22.0f;
    }
    const ImVec2 Min(L.X + L.W - W, Cy - 11.0f), Max(L.X + L.W, Cy + 11.0f);
    L.Draw->AddRect(Min, Max, IM_COL32(255, 255, 255, 30), 11.0f);
    Caps(L, ImVec2(Min.x + 11.0f, Min.y + 6.0f), kDim, Pill);
    L.Y += 46.0f;
}

void HeroCard(Layout& L, EditorInstance& Picked, uint32_t PickedIndex, EditorSheet& Sheet) noexcept
{
    const EditorSheetHero& Hero = Sheet.Hero;
    constexpr float TileS = 44.0f;
    const bool HasMeasure = Hero.Measure[0] != '\0';
    const uint32_t Stats  = Hero.StatCount;

    // The numeral shrinks until the measure, its unit and its caption share one line.
    float NumSize = 46.0f;
    const float UnitW    = Hero.Unit[0] != '\0' ? TextWidth(L, 14.0f, Hero.Unit) + 8.0f : 0.0f;
    const float CaptionW = Hero.Caption[0] != '\0' ? TextWidth(L, 11.0f, Hero.Caption) + 12.0f : 0.0f;
    while (HasMeasure && NumSize > 24.0f && NumeralWidth(L.Ui, NumSize, Hero.Measure) + UnitW + CaptionW > L.InnerW())
        NumSize -= 2.0f;

    float H = kPad + TileS;
    if (HasMeasure)
        H += 12.0f + NumSize;
    if (Stats > 0u)
        H += 14.0f + 56.0f;
    H += kPad;

    const ImVec2 Min(L.X, L.Y), Max(L.X + L.W, L.Y + H);
    L.Draw->AddRectFilled(Min, Max, kCardFill, kRadius);
    L.Draw->AddRect(Min, Max, kHair, kRadius);
    ImGui::PushID("##hero");

    const ImVec2 Tile(L.InnerX(), L.Y + kPad);
    L.Draw->AddRectFilled(Tile, ImVec2(Tile.x + TileS, Tile.y + TileS), TintOf(Picked.Tint, 36), 15.0f);
    L.Draw->AddRect(Tile, ImVec2(Tile.x + TileS, Tile.y + TileS), TintOf(Picked.Tint, 115), 15.0f);
    DrawGlyph(L.Draw, GlyphForMark(Picked.Symbol), ImVec2(Tile.x + TileS * 0.5f, Tile.y + TileS * 0.5f), 8.5f, TintOf(Picked.Tint, 255));

    const float NameX = Tile.x + TileS + 12.0f;
    const float NameW = L.InnerX() + L.InnerW() - NameX;
    if (Hero.Renameable)
    {
        if (gNameFor != PickedIndex)
        {
            std::snprintf(gNameText, sizeof(gNameText), "%s", Picked.Label);
            gNameFor = PickedIndex;
        }
        ImGui::SetCursorScreenPos(ImVec2(NameX - 4.0f, Tile.y - 3.0f));
        ImGui::PushItemWidth(NameW + 4.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kInk));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
        ImGui::PushFont(L.Ui, 22.0f);
        const bool Done   = ImGui::InputText("##name", gNameText, sizeof(gNameText), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool Edited = ImGui::IsItemDeactivatedAfterEdit();
        const bool Focus  = ImGui::IsItemFocused();
        const ImVec2 NameMin = ImGui::GetItemRectMin(), NameMax = ImGui::GetItemRectMax();
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
        if (Focus)
            L.Draw->AddRect(NameMin, NameMax, IM_COL32(255, 255, 255, 46), 8.0f);
        if (Done || Edited)
            std::snprintf(Picked.Label, sizeof(Picked.Label), "%.*s", int(sizeof(Picked.Label) - 1u), gNameText);
    }
    else
    {
        FitText(L, 22.0f, ImVec2(NameX, Tile.y - 1.0f), kInk, Picked.Label, NameW);
    }
    FitText(L, 11.5f, ImVec2(NameX, Tile.y + 28.0f), kDim, Hero.Subtitle, NameW);

    float Y = Tile.y + TileS;
    if (HasMeasure)
    {
        Y += 12.0f;
        const float W = DrawNumeral(L.Draw, L.Ui, NumSize, L.InnerX() + NumeralWidth(L.Ui, NumSize, Hero.Measure), Y, Hero.Measure, kInk,
                                    IM_COL32(255, 255, 255, 80));
        if (Hero.Unit[0] != '\0')
            Text(L, 14.0f, ImVec2(L.InnerX() + W + 6.0f, Y + NumSize * 0.52f), kDim, Hero.Unit);
        if (Hero.Caption[0] != '\0')
            Text(L, 11.0f, ImVec2(L.InnerX() + L.InnerW() - CaptionW + 12.0f, Y + NumSize * 0.62f), kFaint, Hero.Caption);
        Y += NumSize;
    }
    if (Stats > 0u)
    {
        Y += 14.0f;
        const float Gap   = 8.0f;
        const float TileW = (L.InnerW() - Gap * static_cast<float>(Stats - 1u)) / static_cast<float>(Stats);
        for (uint32_t I = 0u; I < Stats; ++I)
        {
            const float X = L.InnerX() + static_cast<float>(I) * (TileW + Gap);
            const ImVec2 SMin(X, Y), SMax(X + TileW, Y + 56.0f);
            L.Draw->AddRectFilled(SMin, SMax, kCardFill, 16.0f);
            L.Draw->AddRect(SMin, SMax, IM_COL32(255, 255, 255, 24), 16.0f);
            FitText(L, 11.5f, ImVec2(X + 11.0f, Y + 9.0f), kDim, Hero.StatLabel[I], TileW - 20.0f);
            float Size = 18.0f;
            while (Size > 11.0f && TextWidth(L, Size, Hero.StatText[I]) > TileW - 22.0f)
                Size -= 1.0f;
            const float W = std::min(TextWidth(L, Size, Hero.StatText[I]), TileW - 22.0f);
            FitText(L, Size, ImVec2(X + TileW - 11.0f - W, Y + 56.0f - 11.0f - Size), kInk, Hero.StatText[I], TileW - 22.0f);
        }
    }
    ImGui::PopID();
    L.Y += H;
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, kGap));
    L.Y += kGap;
}

// The glyphs in the accent circles: black strokes.
void PresenceIcon(ImDrawList* Draw, uint32_t Cell, const ImVec2& C, ImU32 Ink) noexcept
{
    const float T = 1.7f;
    switch (Cell)
    {
    case 0u:
    {
        ImVec2 Up[13], Down[13];
        for (int I = 0; I < 13; ++I)
        {
            const float U = static_cast<float>(I) / 6.0f - 1.0f;
            const float Dy = 5.0f * (1.0f - U * U);
            Up[I]   = ImVec2(C.x + U * 8.0f, C.y - Dy);
            Down[I] = ImVec2(C.x + U * 8.0f, C.y + Dy);
        }
        Draw->AddPolyline(Up, 13, Ink, 0, T);
        Draw->AddPolyline(Down, 13, Ink, 0, T);
        Draw->AddCircleFilled(C, 2.4f, Ink);
        break;
    }
    case 1u:
        Draw->AddRectFilled(ImVec2(C.x - 5.5f, C.y - 1.0f), ImVec2(C.x + 5.5f, C.y + 6.5f), Ink, 1.8f);
        Draw->PathArcTo(ImVec2(C.x, C.y - 1.0f), 3.6f, 3.14159f, 6.28318f, 12);
        Draw->PathStroke(Ink, 0, T);
        Draw->AddLine(ImVec2(C.x - 3.6f, C.y - 1.0f), ImVec2(C.x - 3.6f, C.y), Ink, T);
        Draw->AddLine(ImVec2(C.x + 3.6f, C.y - 1.0f), ImVec2(C.x + 3.6f, C.y), Ink, T);
        break;
    case 2u:
    {
        const ImVec2 Quad[4] = { ImVec2(C.x - 8.0f, C.y + 4.5f), ImVec2(C.x - 4.0f, C.y - 4.5f), ImVec2(C.x + 8.0f, C.y - 4.5f), ImVec2(C.x + 4.0f, C.y + 4.5f) };
        Draw->AddPolyline(Quad, 4, Ink, ImDrawFlags_Closed, T);
        break;
    }
    default:
        Draw->AddLine(ImVec2(C.x - 8.0f, C.y), ImVec2(C.x + 8.0f, C.y), Ink, T);
        Draw->AddLine(ImVec2(C.x - 8.0f, C.y - 5.0f), ImVec2(C.x - 8.0f, C.y + 5.0f), Ink, T);
        Draw->AddLine(ImVec2(C.x + 8.0f, C.y - 5.0f), ImVec2(C.x + 8.0f, C.y + 5.0f), Ink, T);
        Draw->AddLine(ImVec2(C.x - 8.0f, C.y), ImVec2(C.x - 4.5f, C.y - 2.5f), Ink, T);
        Draw->AddLine(ImVec2(C.x - 8.0f, C.y), ImVec2(C.x - 4.5f, C.y + 2.5f), Ink, T);
        Draw->AddLine(ImVec2(C.x + 8.0f, C.y), ImVec2(C.x + 4.5f, C.y - 2.5f), Ink, T);
        Draw->AddLine(ImVec2(C.x + 8.0f, C.y), ImVec2(C.x + 4.5f, C.y + 2.5f), Ink, T);
        break;
    }
}

// The four presence cells: square flat tiles, an accent circle with a black glyph, the name, and SEATED / LIFTED.
void PresenceBlock(Layout& L, EditorInstance& Picked, EditorSheet& Sheet) noexcept
{
    bool Any = false;
    for (bool Offered : Sheet.PresenceOffered)
        Any = Any || Offered;
    if (!Any)
        return;
    Caps(L, ImVec2(L.X + 4.0f, L.Y), kFaint, "Presence \xc2\xb7 tap a cell to seat / lift");
    L.Y += 20.0f;

    const bool  FourUp = L.W >= 320.0f;
    const float Gap    = 8.0f;
    const uint32_t Cols = FourUp ? 4u : 2u;
    const float TileW  = (L.W - Gap * static_cast<float>(Cols - 1u)) / static_cast<float>(Cols);
    const float TileH  = FourUp ? 92.0f : 54.0f;
    ImGui::PushID("##presence");
    for (uint32_t Cell = 0u; Cell < 4u; ++Cell)
    {
        const float X = L.X + static_cast<float>(Cell % Cols) * (TileW + Gap);
        const float Y = L.Y + static_cast<float>(Cell / Cols) * (TileH + Gap);
        const ImVec2 Min(X, Y), Max(X + TileW, Y + TileH);
        const bool Offered = Sheet.PresenceOffered[Cell];
        const bool On      = Sheet.Presence[Cell];
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(static_cast<int>(Cell));
        const bool Hit = ImGui::InvisibleButton("##cell", ImVec2(TileW, TileH)) && Offered;
        const bool Hot = ImGui::IsItemHovered() && Offered;
        ImGui::PopID();
        const uint32_t Fade = Offered ? 255u : 70u;
        L.Draw->AddRectFilled(Min, Max, Hot ? IM_COL32(32, 33, 37, 255) : kCardFill, 20.0f);
        L.Draw->AddRect(Min, Max, WithAlpha(IM_COL32(255, 255, 255, 255), Offered ? 24u : 10u), 20.0f);
        const ImU32 Disc = On && Offered ? kSeated[Cell] : IM_COL32(86, 88, 94, Fade);
        const ImU32 Ink  = On && Offered ? kBlack : IM_COL32(12, 12, 14, Offered ? 255u : 120u);
        const ImU32 Name = On && Offered ? kInk : (Offered ? kDim : kFaint);
        ImVec2 Centre;
        float TextX, NameY, StateY;
        if (FourUp)
        {
            Centre = ImVec2(X + TileW * 0.5f, Y + 30.0f);
            L.Draw->AddCircleFilled(Centre, 19.0f, Disc);
            PresenceIcon(L.Draw, Cell, Centre, Ink);
            const float W = TextWidth(L, 11.0f, kPresenceName[Cell]);
            TextX = X + (TileW - W) * 0.5f;
            NameY = Y + 57.0f;
            StateY = Y + 73.0f;
        }
        else
        {
            Centre = ImVec2(X + 14.0f + 15.0f, Y + TileH * 0.5f);
            L.Draw->AddCircleFilled(Centre, 15.0f, Disc);
            PresenceIcon(L.Draw, Cell, Centre, Ink);
            TextX = X + 14.0f + 30.0f + 9.0f;
            NameY = Y + 12.0f;
            StateY = Y + 30.0f;
        }
        FitText(L, FourUp ? 10.5f : 11.0f, ImVec2(TextX, NameY), Name, kPresenceName[Cell], X + TileW - TextX - 4.0f);
        if (Offered)
        {
            const char* State = On ? "SEATED" : "LIFTED";
            float StateX = TextX;
            if (FourUp)
            {
                float W = 0.0f;
                for (const char* P = State; *P != '\0'; ++P)
                    W += 6.0f + 1.0f;
                StateX = X + (TileW - W) * 0.5f;
            }
            float Cx = StateX;
            for (const char* P = State; *P != '\0'; ++P)
            {
                const char One[2] = { *P, '\0' };
                L.Draw->AddText(L.Ui, 8.5f, ImVec2(Cx, StateY), On ? kSeated[Cell] : kFaint, One);
                Cx += L.Ui->CalcTextSizeA(8.5f, FLT_MAX, 0.0f, One).x + 1.0f;
            }
        }
        if (Hit)
        {
            Sheet.Presence[Cell] = !Sheet.Presence[Cell];
            if (Cell == 0u)
                Picked.Visible = Sheet.Presence[0];
            if (Cell == 1u)
                Picked.Locked = Sheet.Presence[1];
        }
    }
    ImGui::PopID();
    const uint32_t Rows = (4u + Cols - 1u) / Cols;
    L.Y += static_cast<float>(Rows) * (TileH + Gap);
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, 2.0f));
}

// The tab chips: Object, then one per card. A chip scrolls the card into view; the chip of the card at the top lights.
void TabChips(Layout& L, const EditorSheet& Sheet) noexcept
{
    const float Scroll = ImGui::GetScrollY();
    int Active = 0;
    for (uint32_t I = 0u; I < Sheet.GroupCount && I < kMaxEditorSheetGroups; ++I)
        if (gAnchor[I] <= Scroll + 70.0f && gAnchor[I] > 0.0f)
            Active = static_cast<int>(I) + 1;

    const ImVec2 RowMin(L.X, L.Y), RowMax(L.X + L.W, L.Y + 30.0f);
    ImGui::PushClipRect(RowMin, RowMax, true);
    ImGui::PushID("##chips");
    float X = L.X;
    for (uint32_t I = 0u; I <= Sheet.GroupCount && I <= kMaxEditorSheetGroups; ++I)
    {
        const char* Label = I == 0u ? "Object" : Sheet.Groups[I - 1u].Title;
        const float W = TextWidth(L, 12.0f, Label) + 28.0f;
        const ImVec2 Min(X, L.Y), Max(X + W, L.Y + 30.0f);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(static_cast<int>(I));
        if (ImGui::InvisibleButton("##chip", ImVec2(W, 30.0f)))
            ImGui::SetScrollY(I == 0u ? 0.0f : std::max(0.0f, gAnchor[I - 1u] - 10.0f));
        const bool Hot = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool On = static_cast<int>(I) == Active;
        L.Draw->AddRectFilled(Min, Max, On ? IM_COL32(255, 255, 255, 30) : (Hot ? kTile : kCardFill), 15.0f);
        L.Draw->AddRect(Min, Max, On ? IM_COL32(255, 255, 255, 50) : IM_COL32(255, 255, 255, 24), 15.0f);
        Text(L, 12.0f, ImVec2(X + 14.0f, L.Y + 8.0f), On ? kInk : kDim, Label);
        X += W + 6.0f;
    }
    ImGui::PopID();
    ImGui::PopClipRect();
    L.Y += 30.0f + kGap + 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, 2.0f));
}

void ActionIcon(ImDrawList* Draw, uint32_t Action, const ImVec2& C, ImU32 Ink) noexcept
{
    const float T = 1.8f;
    if (Action == 0u)
    {
        Draw->AddRect(ImVec2(C.x - 2.0f, C.y - 8.0f), ImVec2(C.x + 8.0f, C.y + 2.0f), Ink, 2.0f, 0, T);
        Draw->AddRect(ImVec2(C.x - 8.0f, C.y - 2.0f), ImVec2(C.x + 2.0f, C.y + 8.0f), Ink, 2.0f, 0, T);
    }
    else if (Action == 1u)
    {
        const float S = 8.0f, A = 4.0f;
        for (int Sx = -1; Sx <= 1; Sx += 2)
            for (int Sy = -1; Sy <= 1; Sy += 2)
            {
                const ImVec2 Corner(C.x + static_cast<float>(Sx) * S, C.y + static_cast<float>(Sy) * S);
                Draw->AddLine(Corner, ImVec2(Corner.x - static_cast<float>(Sx) * A, Corner.y), Ink, T);
                Draw->AddLine(Corner, ImVec2(Corner.x, Corner.y - static_cast<float>(Sy) * A), Ink, T);
            }
        Draw->AddCircle(C, 2.4f, Ink, 12, T);
    }
    else
    {
        Draw->AddLine(ImVec2(C.x - 8.0f, C.y - 5.0f), ImVec2(C.x + 8.0f, C.y - 5.0f), Ink, T);
        Draw->AddLine(ImVec2(C.x - 3.0f, C.y - 5.0f), ImVec2(C.x - 3.0f, C.y - 8.0f), Ink, T);
        Draw->AddLine(ImVec2(C.x + 3.0f, C.y - 5.0f), ImVec2(C.x + 3.0f, C.y - 8.0f), Ink, T);
        Draw->AddLine(ImVec2(C.x - 3.0f, C.y - 8.0f), ImVec2(C.x + 3.0f, C.y - 8.0f), Ink, T);
        const ImVec2 Body[4] = { ImVec2(C.x - 6.0f, C.y - 3.0f), ImVec2(C.x - 5.0f, C.y + 8.0f), ImVec2(C.x + 5.0f, C.y + 8.0f), ImVec2(C.x + 6.0f, C.y - 3.0f) };
        Draw->AddPolyline(Body, 4, Ink, 0, T);
        Draw->AddLine(ImVec2(C.x, C.y), ImVec2(C.x, C.y + 5.0f), Ink, T);
    }
}

// Duplicate / Isolate / Delete: flat tiles, a filled accent circle, a black glyph, the name.
void ActionTiles(Layout& L, EditorSheet& Sheet) noexcept
{
    if (!Sheet.ActionsOffered)
        return;
    static const char* const Names[3] = { "Duplicate", "Isolate", "Delete" };
    static const ImU32 Tints[3] = { IM_COL32(79, 216, 224, 255), IM_COL32(77, 163, 255, 255), IM_COL32(255, 59, 48, 255) };
    const float Gap   = 8.0f;
    const float TileW = (L.W - 2.0f * Gap) / 3.0f;
    const float TileH = 80.0f;
    ImGui::PushID("##actions");
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const float X = L.X + static_cast<float>(I) * (TileW + Gap);
        const ImVec2 Min(X, L.Y), Max(X + TileW, L.Y + TileH);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(static_cast<int>(I));
        const bool Hit = ImGui::InvisibleButton("##act", ImVec2(TileW, TileH));
        const bool Hot = ImGui::IsItemHovered();
        ImGui::PopID();
        L.Draw->AddRectFilled(Min, Max, Hot ? IM_COL32(32, 33, 37, 255) : kCardFill, 20.0f);
        L.Draw->AddRect(Min, Max, IM_COL32(255, 255, 255, 24), 20.0f);
        const ImVec2 C(X + TileW * 0.5f, L.Y + 30.0f);
        L.Draw->AddCircleFilled(C, 19.0f, Tints[I]);
        ActionIcon(L.Draw, I, C, kBlack);
        Text(L, 12.0f, ImVec2(X + (TileW - TextWidth(L, 12.0f, Names[I])) * 0.5f, L.Y + 57.0f), kInk, Names[I]);
        if (Hit)
            Sheet.Action = I == 0u ? EditorSheetAction::Duplicate : (I == 1u ? EditorSheetAction::Isolate : EditorSheetAction::Delete);
    }
    ImGui::PopID();
    L.Y += TileH + kGap;
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, 2.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         ENTRY
//------------------------------------------------------------------------------------------------------------------------

void RecordSolidArcEmpty(ControlPanel& Controls) noexcept
{
    Layout L;
    L.Controls = &Controls;
    L.Draw     = ImGui::GetWindowDrawList();
    L.Ui       = Controls.QueryUi();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    L.X = Origin.x + kMargin;
    L.W = ImGui::GetContentRegionAvail().x - 2.0f * kMargin;
    L.Y = Origin.y + kMargin;
    (void)CardBegin(L, "##empty", "Nothing selected", "Pick an object in the outliner", nullptr);
    CardEnd(L, false);
}

void RecordSolidArcInspector(ControlPanel& Controls, EditorInstance& Picked, uint32_t PickedIndex, EditorSheet& Sheet, bool* Shut) noexcept
{
    Layout L;
    L.Controls = &Controls;
    L.Draw     = ImGui::GetWindowDrawList();
    L.Ui       = Controls.QueryUi();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    L.X = Origin.x + kMargin;
    L.W = ImGui::GetContentRegionAvail().x - 2.0f * kMargin;
    L.Y = Origin.y + kMargin;

    if (Sheet.Hero.Active)
    {
        HeadBar(L, Sheet.Hero, TintOf(Picked.Tint, 255));
        HeroCard(L, Picked, PickedIndex, Sheet);
        PresenceBlock(L, Picked, Sheet);
        TabChips(L, Sheet);
    }
    for (uint32_t I = 0u; I < Sheet.GroupCount && I < kMaxEditorSheetGroups; ++I)
        RecordGroup(L, Sheet.Groups[I], I, &Shut[I]);
    ActionTiles(L, Sheet);
    ImGui::SetCursorScreenPos(ImVec2(L.X, L.Y));
    ImGui::Dummy(ImVec2(L.W, 6.0f));
}

} // namespace Frontier
