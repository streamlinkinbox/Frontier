//============================================================================================================================================
//                                                        TYREGENERATORWINDOW.CPP
//============================================================================================================================================
// 📦 The generator's own three-column layout, widget for widget: header tabs, control column, viewport readouts and the pattern column.

#include "TyreGeneratorWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

#include "Generators/Tyre/TyreProfileSpecification.h"
#include "Generators/Tyre/TyrePresetLibrary.h"
#include "../../Editor/ControlPanel.h"

namespace Frontier {

namespace {

constexpr float π = 3.14159265358979323846f;

//------------------------------------------------------------------------------------------------------------------------
//                                                         TOKENS
//------------------------------------------------------------------------------------------------------------------------
// 📝 The generator's own :root, as bytes. Nothing here is invented — every value is the stylesheet's.

constexpr ImU32 BackdropTint = IM_COL32(0x0C, 0x0D, 0x0F, 255);
constexpr ImU32 CardTint     = IM_COL32(0x17, 0x18, 0x1B, 255);
constexpr ImU32 CardRaisedTint    = IM_COL32(0x1E, 0x20, 0x24, 255);
constexpr ImU32 CardHoverTint    = IM_COL32(0x25, 0x27, 0x2C, 255);
constexpr ImU32 HairlineTint     = IM_COL32(255, 255, 255, 15);
constexpr ImU32 TextTint     = IM_COL32(0xE6, 0xE7, 0xEA, 255);
constexpr ImU32 MutedTint    = IM_COL32(0x8A, 0x8D, 0x94, 255);
constexpr ImU32 DimTint      = IM_COL32(0x5C, 0x5F, 0x66, 255);
constexpr ImU32 WhiteTint    = IM_COL32(255, 255, 255, 255);
constexpr ImU32 GreenTint    = IM_COL32(0x38, 0xC4, 0x6D, 255);
constexpr ImU32 YellowTint   = IM_COL32(0xD6, 0xC9, 0x3A, 255);
constexpr ImU32 RedTint      = IM_COL32(0xE5, 0x42, 0x3F, 255);
constexpr ImU32 OrangeTint   = IM_COL32(0xF0, 0xA0, 0x4B, 255);
constexpr ImU32 TrackTint    = IM_COL32(0x2D, 0x30, 0x36, 255);
constexpr ImU32 ViewportTint = IM_COL32(0x0B, 0x0C, 0x0E, 255);
constexpr ImU32 CanvasTint   = IM_COL32(0x11, 0x11, 0x11, 255);

constexpr float CardCornerRadius  = 22.0f;
constexpr float CardSidePad    = 18.0f;
constexpr float CardHeadPad  = 18.0f;
constexpr float CardFootPad  = 14.0f;
constexpr float LabelColumnWidth  = 118.0f;
constexpr float FigureColumnWidth  = 54.0f;
constexpr float RowHeight   = 26.0f;
constexpr float RowGap      = 6.0f;
constexpr float ControlColumnWidth  = 320.0f;
constexpr float PatternColumnWidth = 340.0f;
constexpr float ColumnGutter      = 10.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                                       TEXT HELPERS
//------------------------------------------------------------------------------------------------------------------------

struct Faces
{
    ImFont* Ui    = nullptr;
    ImFont* Small = nullptr;
    ImFont* Mono  = nullptr;
};

Faces gFaces;

void Ink(ImDrawList* Draw, float Size, ImVec2 At, ImU32 Colour, const char* Text, ImFont* Face = nullptr) noexcept
{
    Draw->AddText(Face != nullptr ? Face : gFaces.Ui, Size, At, Colour, Text);
}

[[nodiscard]] ImVec2 Measure(float Size, const char* Text, ImFont* Face = nullptr) noexcept
{
    ImFont* F = Face != nullptr ? Face : gFaces.Ui;
    return F->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text);
}

void InkRight(ImDrawList* Draw, float Size, float RightEdge, float Y, ImU32 Colour, const char* Text,
              ImFont* Face = nullptr) noexcept
{
    Ink(Draw, Size, ImVec2(RightEdge - Measure(Size, Text, Face).x, Y), Colour, Text, Face);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         CARDS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One card: a 22-pixel rounded panel with a coloured bar beside its title. The background is drawn after
///    the content so the card can be exactly as tall as what went into it, which is what a flow layout in a
///    stylesheet does for free and an immediate-mode list has to arrange for.
class Card
{
public:
    void Begin(const char* Title, ImU32 Bar, const char* Right)
    {
        Draw_ = ImGui::GetWindowDrawList();
        Splitter_.Split(Draw_, 2);
        Splitter_.SetCurrentChannel(Draw_, 1);

        Min_   = ImGui::GetCursorScreenPos();
        Width_ = ImGui::GetContentRegionAvail().x;
        Bar_   = Bar;

        const float TitleY = Min_.y + CardHeadPad;
        Draw_->AddRectFilled(ImVec2(Min_.x + CardSidePad, TitleY + 1.0f),
                             ImVec2(Min_.x + CardSidePad + 3.0f, TitleY + 17.0f), Bar, 2.0f);
        Ink(Draw_, 15.0f, ImVec2(Min_.x + CardSidePad + 13.0f, TitleY), TextTint, Title);
        if (Right != nullptr && Right[0] != '\0')
        {
            InkRight(Draw_, 13.0f, Min_.x + Width_ - CardSidePad, TitleY + 2.0f, MutedTint, Right);
        }

        ImGui::SetCursorScreenPos(ImVec2(Min_.x + CardSidePad, TitleY + 18.0f + 12.0f));
        ImGui::PushClipRect(ImVec2(Min_.x, Min_.y), ImVec2(Min_.x + Width_, Min_.y + 4000.0f), true);
        ImGui::PushItemWidth(Width_ - 2.0f * CardSidePad);
    }

    void End()
    {
        ImGui::PopItemWidth();
        ImGui::PopClipRect();
        const float Bottom = ImGui::GetCursorScreenPos().y + CardFootPad;

        Splitter_.SetCurrentChannel(Draw_, 0);
        Draw_->AddRectFilled(Min_, ImVec2(Min_.x + Width_, Bottom), CardTint, CardCornerRadius);
        Splitter_.Merge(Draw_);

        ImGui::SetCursorScreenPos(Min_);
        ImGui::Dummy(ImVec2(Width_, Bottom - Min_.y));
        ImGui::Dummy(ImVec2(Width_, ColumnGutter - ImGui::GetStyle().ItemSpacing.y));
    }

    [[nodiscard]] float ContentWidth() const noexcept { return Width_ - 2.0f * CardSidePad; }

private:
    ImDrawListSplitter Splitter_;
    ImDrawList*        Draw_  = nullptr;
    ImVec2             Min_{};
    float              Width_ = 0.0f;
    ImU32              Bar_   = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The uppercase letterspaced divider the generator calls `.sec`.
void SectionLabel(const char* Text)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At = ImGui::GetCursorScreenPos();
    float X = At.x;
    for (const char* C = Text; *C != '\0'; ++C)
    {
        const char One[2] = { char(std::toupper(static_cast<unsigned char>(*C))), '\0' };
        Ink(Draw, 11.0f, ImVec2(X, At.y + 8.0f), DimTint, One, gFaces.Small);
        X += Measure(11.0f, One, gFaces.Small).x + 1.6f;
    }
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
}

/// 📦 A paragraph of guidance, wrapped. The generator calls it `.hint`.
void Hint(const char* Text, ImU32 Colour = MutedTint, float Size = 12.0f)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At    = ImGui::GetCursorScreenPos();
    const float  Wrap  = ImGui::GetContentRegionAvail().x;
    const float  Tall  = gFaces.Ui->CalcTextSizeA(Size, FLT_MAX, Wrap, Text).y;
    Draw->AddText(gFaces.Ui, Size, ImVec2(At.x, At.y + 2.0f), Colour, Text, nullptr, Wrap);
    ImGui::Dummy(ImVec2(Wrap, Tall + 8.0f));
}

/// 📦 The 118-pixel label column. The generator's stylesheet gives it `text-overflow: ellipsis`, and without
///    that a label like "Coloured tread stripes" runs straight under its own switch.
void RowLabel(ImDrawList* Draw, float X, float Y, const char* Text) noexcept
{
    if (Measure(13.0f, Text).x <= LabelColumnWidth)
    {
        Ink(Draw, 13.0f, ImVec2(X, Y), MutedTint, Text);
        return;
    }
    char Clipped[96];
    const size_t Length = std::min(sizeof(Clipped) - 4u, std::strlen(Text));
    size_t Keep = Length;
    while (Keep > 1u)
    {
        std::memcpy(Clipped, Text, Keep);
        std::memcpy(Clipped + Keep, "...", 4u);
        if (Measure(13.0f, Clipped).x <= LabelColumnWidth)
        {
            break;
        }
        --Keep;
    }
    Ink(Draw, 13.0f, ImVec2(X, Y), MutedTint, Clipped);
}

/// 📦 The label-track-figure row every scalar control in the generator is built on.
/// note  The figure sits on the right at a fixed 54 pixels so a column of sliders reads as a column of
///       numbers. Letting it follow the value would make the whole column jitter as one slider is dragged.
bool Slider(const char* Id, const char* Label, float* Value, float Minimum, float Maximum, float Step,
            int Decimals, const char* Prefix = "")
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At   = ImGui::GetCursorScreenPos();
    const float  Full = ImGui::GetContentRegionAvail().x;
    const float  Mid  = At.y + RowHeight * 0.5f;

    RowLabel(Draw, At.x, Mid - 8.0f, Label);

    const float TrackX0 = At.x + LabelColumnWidth + 8.0f;
    const float TrackX1 = At.x + Full - FigureColumnWidth - 8.0f;
    const float TrackW  = std::max(16.0f, TrackX1 - TrackX0);

    ImGui::SetCursorScreenPos(ImVec2(TrackX0, At.y));
    ImGui::InvisibleButton(Id, ImVec2(TrackW, RowHeight));
    bool Changed = false;
    if (ImGui::IsItemActive())
    {
        const float T = std::clamp((ImGui::GetIO().MousePos.x - TrackX0) / TrackW, 0.0f, 1.0f);
        float Next = Minimum + (Maximum - Minimum) * T;
        if (Step > 0.0f)
        {
            Next = Minimum + std::round((Next - Minimum) / Step) * Step;
        }
        Next = std::clamp(Next, Minimum, Maximum);
        if (Next != *Value)
        {
            *Value = Next;
            Changed = true;
        }
    }

    Draw->AddRectFilled(ImVec2(TrackX0, Mid - 1.0f), ImVec2(TrackX0 + TrackW, Mid + 1.0f), TrackTint, 1.0f);
    const float Fraction = (Maximum > Minimum) ? std::clamp((*Value - Minimum) / (Maximum - Minimum), 0.0f, 1.0f)
                                               : 0.0f;
    const float ThumbX = TrackX0 + TrackW * Fraction;
    Draw->AddCircleFilled(ImVec2(ThumbX, Mid), 9.0f, CardTint, 16);
    Draw->AddCircleFilled(ImVec2(ThumbX, Mid), 6.0f, WhiteTint, 16);

    char Figure[48];
    std::snprintf(Figure, sizeof(Figure), "%s%.*f", Prefix, Decimals, double(*Value));
    InkRight(Draw, 13.0f, At.x + Full, Mid - 8.0f, TextTint, Figure, gFaces.Mono);

    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + RowHeight + RowGap));
    return Changed;
}

/// 📦 The pill dropdown.
bool Choice(const char* Id, const char* Label, int* Picked, const std::vector<std::string>& Options)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At   = ImGui::GetCursorScreenPos();
    const float  Full = ImGui::GetContentRegionAvail().x;
    const float  Mid  = At.y + RowHeight * 0.5f;

    RowLabel(Draw, At.x, Mid - 8.0f, Label);

    const float PillX = At.x + LabelColumnWidth + 8.0f;
    const float PillW = std::max(40.0f, At.x + Full - PillX);
    ImGui::SetCursorScreenPos(ImVec2(PillX, Mid - 13.0f));
    ImGui::InvisibleButton(Id, ImVec2(PillW, 26.0f));
    const bool Hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup(Id);
    }

    Draw->AddRectFilled(ImVec2(PillX, Mid - 13.0f), ImVec2(PillX + PillW, Mid + 13.0f),
                        Hovered ? CardHoverTint : CardRaisedTint, 13.0f);
    const char* Shown = (*Picked >= 0 && *Picked < int(Options.size())) ? Options[size_t(*Picked)].c_str() : "—";
    Ink(Draw, 13.0f, ImVec2(PillX + 13.0f, Mid - 8.0f), TextTint, Shown);
    Draw->AddTriangleFilled(ImVec2(PillX + PillW - 18.0f, Mid - 2.0f),
                            ImVec2(PillX + PillW - 10.0f, Mid - 2.0f),
                            ImVec2(PillX + PillW - 14.0f, Mid + 3.0f), MutedTint);

    bool Changed = false;
    if (ImGui::BeginPopup(Id))
    {
        for (int I = 0; I < int(Options.size()); ++I)
        {
            if (ImGui::Selectable(Options[size_t(I)].c_str(), I == *Picked))
            {
                *Picked = I;
                Changed = true;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + RowHeight + RowGap));
    return Changed;
}

/// 📦 The 34×20 pill switch, green when on.
bool Check(const char* Id, const char* Label, bool* On)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At   = ImGui::GetCursorScreenPos();
    const float  Full = ImGui::GetContentRegionAvail().x;
    const float  Mid  = At.y + RowHeight * 0.5f;

    RowLabel(Draw, At.x, Mid - 8.0f, Label);

    const float SwitchX = At.x + LabelColumnWidth + 8.0f;
    ImGui::SetCursorScreenPos(ImVec2(SwitchX, Mid - 10.0f));
    ImGui::InvisibleButton(Id, ImVec2(34.0f, 20.0f));
    bool Changed = false;
    if (ImGui::IsItemClicked())
    {
        *On = !*On;
        Changed = true;
    }

    Draw->AddRectFilled(ImVec2(SwitchX, Mid - 10.0f), ImVec2(SwitchX + 34.0f, Mid + 10.0f),
                        *On ? GreenTint : TrackTint, 10.0f);
    const float KnobX = *On ? SwitchX + 24.0f : SwitchX + 10.0f;
    Draw->AddCircleFilled(ImVec2(KnobX, Mid), 7.0f, *On ? WhiteTint : IM_COL32(0x9A, 0x9D, 0xA4, 255), 16);

    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + RowHeight + RowGap));
    return Changed;
}

/// 📦 The round colour swatch with its picker.
bool Swatch(const char* Id, const char* Label, uint32_t* Argb)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At   = ImGui::GetCursorScreenPos();
    const float  Mid  = At.y + RowHeight * 0.5f;

    RowLabel(Draw, At.x, Mid - 8.0f, Label);

    const float ChipX = At.x + LabelColumnWidth + 8.0f;
    ImGui::SetCursorScreenPos(ImVec2(ChipX, Mid - 14.0f));
    ImGui::InvisibleButton(Id, ImVec2(28.0f, 28.0f));
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup(Id);
    }

    const ImU32 Shown = IM_COL32((*Argb >> 16) & 0xFFu, (*Argb >> 8) & 0xFFu, *Argb & 0xFFu, 255);
    Draw->AddCircleFilled(ImVec2(ChipX + 14.0f, Mid), 14.0f, TrackTint, 24);
    Draw->AddCircleFilled(ImVec2(ChipX + 14.0f, Mid), 13.0f, Shown, 24);

    char Hex[16];
    std::snprintf(Hex, sizeof(Hex), "#%06X", unsigned(*Argb & 0x00FFFFFFu));
    Ink(Draw, 13.0f, ImVec2(ChipX + 36.0f, Mid - 7.0f), DimTint, Hex, gFaces.Mono);

    bool Changed = false;
    if (ImGui::BeginPopup(Id))
    {
        float Rgb[3] = { float((*Argb >> 16) & 0xFFu) / 255.0f,
                         float((*Argb >> 8) & 0xFFu) / 255.0f,
                         float(*Argb & 0xFFu) / 255.0f };
        if (ImGui::ColorPicker3("##picker", Rgb, ImGuiColorEditFlags_NoSidePreview))
        {
            *Argb = 0xFF000000u | (uint32_t(Rgb[0] * 255.0f + 0.5f) << 16)
                                | (uint32_t(Rgb[1] * 255.0f + 0.5f) << 8)
                                |  uint32_t(Rgb[2] * 255.0f + 0.5f);
            Changed = true;
        }
        ImGui::EndPopup();
    }

    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + RowHeight + RowGap));
    return Changed;
}

/// 📦 The pill text field.
bool TextField(const char* Id, const char* Label, std::string* Value)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At   = ImGui::GetCursorScreenPos();
    const float  Full = ImGui::GetContentRegionAvail().x;
    const float  Mid  = At.y + RowHeight * 0.5f;

    RowLabel(Draw, At.x, Mid - 8.0f, Label);

    const float FieldX = At.x + LabelColumnWidth + 8.0f;
    const float FieldW = std::max(40.0f, At.x + Full - FieldX);

    char Buffer[128];
    std::snprintf(Buffer, sizeof(Buffer), "%s", Value->c_str());

    ImGui::SetCursorScreenPos(ImVec2(FieldX, Mid - 13.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, CardRaisedTint);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, CardHoverTint);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, CardHoverTint);
    ImGui::PushStyleColor(ImGuiCol_Text, TextTint);
    ImGui::SetNextItemWidth(FieldW);
    const bool Changed = ImGui::InputText(Id, Buffer, sizeof(Buffer));
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    if (Changed)
    {
        *Value = Buffer;
    }

    ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + RowHeight + RowGap));
    return Changed;
}

/// 📦 The pill button. Primary is the white one.
bool Button(const char* Label, bool Primary, float Width = 0.0f, float Height = 30.0f)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 At = ImGui::GetCursorScreenPos();
    const float  W  = (Width > 0.0f) ? Width : Measure(13.0f, Label).x + 28.0f;

    ImGui::InvisibleButton(Label, ImVec2(W, Height));
    const bool Hovered = ImGui::IsItemHovered();
    const bool Clicked = ImGui::IsItemClicked();

    const ImU32 Fill = Primary ? (Hovered ? IM_COL32(0xE8, 0xE8, 0xE8, 255) : WhiteTint)
                               : (Hovered ? CardHoverTint : CardRaisedTint);
    Draw->AddRectFilled(At, ImVec2(At.x + W, At.y + Height), Fill, Height * 0.5f);
    const ImVec2 Size = Measure(13.0f, Label);
    Ink(Draw, 13.0f, ImVec2(At.x + (W - Size.x) * 0.5f, At.y + (Height - Size.y) * 0.5f),
        Primary ? IM_COL32(0x11, 0x11, 0x11, 255) : TextTint, Label);
    return Clicked;
}

/// 📦 The small coloured capsule the size card lines up under its headline figure.
void Capsule(ImDrawList* Draw, ImVec2 At, const char* Text, ImU32 Fill, ImU32 Ink_, float* Advance)
{
    const ImVec2 Size = Measure(13.0f, Text);
    const float  W    = Size.x + 22.0f;
    Draw->AddRectFilled(At, ImVec2(At.x + W, At.y + 23.0f), Fill, 11.0f);
    Ink(Draw, 13.0f, ImVec2(At.x + 11.0f, At.y + 4.0f), Ink_, Text);
    *Advance = W + 6.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAP DRAWING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws a material map into a rectangle by emitting one filled rectangle per run of equal colour along a
///    row.
/// note  ⚠️ Not one rectangle per texel. ImGui indexes vertices with sixteen bits, so a megapixel map drawn
///       naively overflows the draw list long before it finishes. A tread map is mostly flat rubber and
///       flat groove floor, so run-length encoding the rows collapses a four-hundred-texel row to a few
///       dozen rectangles and keeps every texel of detail in the circumferential axis.
void DrawMap(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, const TreadMaterialMap& Map, float SourceFraction,
             float Brighten)
{
    const float W = Max.x - Min.x;
    const float H = Max.y - Min.y;
    if (Map.Width == 0u || Map.Height == 0u || W < 2.0f || H < 2.0f)
    {
        Draw->AddRectFilled(Min, Max, CanvasTint, 10.0f);
        return;
    }

    const int Rows    = int(H);
    const int Columns = int(W);
    const uint32_t Span = std::max(1u, uint32_t(float(Map.Width) * std::clamp(SourceFraction, 0.01f, 1.0f)));

    const auto Lift = [&](uint32_t Texel) noexcept -> ImU32
    {
        const auto Channel = [&](uint32_t Shift) noexcept -> uint32_t
        {
            const float V = float((Texel >> Shift) & 0xFFu) / 255.0f * Brighten;
            return uint32_t(std::clamp(V, 0.0f, 1.0f) * 31.0f + 0.5f) * 8u;
        };
        return IM_COL32(Channel(16), Channel(8), Channel(0), 255);
    };

    for (int Y = 0; Y < Rows; ++Y)
    {
        const uint32_t SY = std::min(Map.Height - 1u, uint32_t(float(Y) / H * float(Map.Height)));
        const uint32_t* Row = &Map.Texels[size_t(SY) * Map.Width];

        int   RunStart = 0;
        ImU32 RunTint  = Lift(Row[0]);
        for (int X = 1; X <= Columns; ++X)
        {
            ImU32 Tint = RunTint;
            if (X < Columns)
            {
                const uint32_t SX = std::min(Map.Width - 1u, uint32_t(float(X) / W * float(Span)));
                Tint = Lift(Row[SX]);
            }
            if (X == Columns || Tint != RunTint)
            {
                Draw->AddRectFilled(ImVec2(Min.x + float(RunStart), Min.y + float(Y)),
                                    ImVec2(Min.x + float(X), Min.y + float(Y) + 1.0f), RunTint);
                RunStart = X;
                RunTint  = Tint;
            }
        }
    }
}

/// 📦 Draws a depth field as a grey preview, which is what a preset thumbnail is.
void DrawDepthThumbnail(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, const TreadDepthField& Field)
{
    const float W = Max.x - Min.x;
    const float H = Max.y - Min.y;
    if (Field.Width == 0u || Field.Height == 0u || W < 2.0f || H < 2.0f)
    {
        Draw->AddRectFilled(Min, Max, IM_COL32(0x22, 0x22, 0x22, 255), 8.0f);
        return;
    }
    // The generator's own thumbnail crop: a window of the circumference four times as long as it is wide.
    const uint32_t Span = std::min(Field.Width, Field.Height * 4u);

    for (int Y = 0; Y < int(H); ++Y)
    {
        const uint32_t SY = std::min(Field.Height - 1u, uint32_t(float(Y) / H * float(Field.Height)));
        int   RunStart = 0;
        int   RunTone  = -1;
        for (int X = 0; X <= int(W); ++X)
        {
            int Tone = RunTone;
            if (X < int(W))
            {
                const uint32_t SX = std::min(Field.Width - 1u, uint32_t(float(X) / W * float(Span)));
                const float V = 40.0f + (1.0f - Field.At(SX, SY)) * 60.0f;
                Tone = int(std::clamp(V, 0.0f, 255.0f)) & ~3;
            }
            if (RunTone < 0)
            {
                RunTone = Tone;
                continue;
            }
            if (X == int(W) || Tone != RunTone)
            {
                const ImU32 Grey = IM_COL32(RunTone, RunTone, RunTone, 255);
                Draw->AddRectFilled(ImVec2(Min.x + float(RunStart), Min.y + float(Y)),
                                    ImVec2(Min.x + float(X), Min.y + float(Y) + 1.0f), Grey);
                RunStart = X;
                RunTone  = Tone;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CROSS-SECTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One draggable handle on the cross-section.
struct ProfileHandle
{
    const char* Identity = "";
    float       Lateral  = 0.0f;   // [mm]
    float       Radius   = 0.0f;   // [mm]
    const char* Label    = "";
    ImU32       Colour   = 0u;
    bool        Mirrored = false;
};

/// 📦 The four parameters the cross-section exposes as handles, each one mirrored unless it sits on the axis.
[[nodiscard]] std::vector<ProfileHandle> ProfileHandles(const TreadSpecification& S,
                                                        const TreadDerivedValues& D)
{
    const float Sin45 = std::sin(π * 0.25f);
    const float Cos45 = std::cos(π * 0.25f);
    const ProfileHandle Seeds[4] = {
        { "tread",    D.TreadHalf,                        D.OuterRadius - S.Crown,
          "tread edge / crown", GreenTint,  false },
        { "shoulder", D.TreadHalf + D.ShoulderRadius * Sin45,
          D.OuterRadius - S.Crown - D.ShoulderRadius + D.ShoulderRadius * Cos45,
          "shoulder radius",    YellowTint, false },
        { "bulge",    D.HalfWidth * (0.6f + 0.4f * S.Bulge), D.RimRadius + D.SectionHeight * 0.52f,
          "sidewall bulge",     OrangeTint, false },
        { "depth",    0.0f,                               D.OuterRadius - S.TreadDepth,
          "tread depth",        RedTint,    false }
    };

    std::vector<ProfileHandle> Handles;
    Handles.reserve(7u);
    for (const ProfileHandle& H : Seeds)
    {
        Handles.push_back(H);
        if (H.Lateral != 0.0f)
        {
            ProfileHandle Mirror = H;
            Mirror.Lateral = -H.Lateral;
            Mirror.Mirrored = true;
            Handles.push_back(Mirror);
        }
    }
    return Handles;
}

/// 📦 Draws the lathe profile with its draggable handles, which is the generator's primary shape editor.
bool DrawCrossSection(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, TreadSpecification& S, const char* Id)
{
    const TreadDerivedValues D = DeriveTreadValues(S);
    Draw->AddRectFilled(Min, Max, CanvasTint, 14.0f);

    const float W = Max.x - Min.x;
    const float H = Max.y - Min.y;
    const float Scale = std::min((W - 44.0f) / (S.Width * 1.15f),
                                 (H - 40.0f) / (D.OuterRadius - D.RimRadius + 40.0f));
    const float OX = Min.x + W * 0.5f;
    const float OY = Max.y - 16.0f;
    const auto X = [&](float Millimetres) noexcept { return OX + Millimetres * Scale; };
    const auto Y = [&](float Radius) noexcept { return OY - (Radius - D.RimRadius + 20.0f) * Scale; };

    // ① the bead seat, dashed, with the rim bar sitting on it
    const float SeatY = Y(D.RimRadius);
    for (float Dash = X(-D.BeadHalf - 10.0f); Dash < X(D.BeadHalf + 10.0f); Dash += 6.0f)
    {
        Draw->AddLine(ImVec2(Dash, SeatY), ImVec2(std::min(Dash + 3.0f, X(D.BeadHalf + 10.0f)), SeatY),
                      IM_COL32(0x2B, 0x2F, 0x38, 255), 1.0f);
    }
    Draw->AddRectFilled(ImVec2(X(-D.BeadHalf), SeatY - 3.0f), ImVec2(X(D.BeadHalf), SeatY + 3.0f),
                        IM_COL32(0x9A, 0xA0, 0xAB, 255));

    // ② the carcass outline, moulded surface across the top and the sidewall bezier down each side
    const float Worn = S.Wear * S.TreadDepth;
    std::vector<ImVec2> Outline;
    constexpr int TreadSamples = 40;
    constexpr int SidewallSamples  = 22;
    const auto Push = [&](float Lateral, float Radius) { Outline.push_back(ImVec2(X(Lateral), Y(Radius))); };

    std::vector<ImVec2> Surface;   // the moulded line only, kept for the dashed floor below it
    for (int I = -TreadSamples; I <= TreadSamples; ++I)
    {
        const float Lateral = float(I) / float(TreadSamples) * D.AcrossHalf;
        const TyreProfileSample Sample = EvaluateTyreProfile(Lateral, S, D);
        Surface.push_back(ImVec2(X(Sample.Lateral), Y(Sample.Radius - Worn * Sample.ContactWeight)));
    }
    for (const ImVec2& P : Surface)
    {
        Outline.push_back(P);
    }
    // the sidewall, a cubic from the shoulder's last point down to the bead
    const TyreProfileSample Edge = EvaluateTyreProfile(D.AcrossHalf, S, D);
    const ImVec2 P0(Edge.Lateral, Edge.Radius);
    const ImVec2 P3(D.BeadHalf, D.RimRadius + 2.0f);
    const ImVec2 P1(P0.x + (D.HalfWidth - P0.x) * 1.35f * S.Bulge + 4.0f, P0.y - D.SectionHeight * 0.22f);
    const ImVec2 P2(D.BeadHalf + (D.HalfWidth - D.BeadHalf) * 0.55f * S.Bulge, D.RimRadius + D.SectionHeight * 0.2f);
    for (int Side = 0; Side < 2; ++Side)
    {
        const float Sign = (Side == 0) ? 1.0f : -1.0f;
        for (int I = 1; I <= SidewallSamples; ++I)
        {
            const float T = float(I) / float(SidewallSamples);
            const float M = 1.0f - T;
            const float Lateral = M * M * M * P0.x + 3.0f * M * M * T * P1.x + 3.0f * M * T * T * P2.x + T * T * T * P3.x;
            const float Radius  = M * M * M * P0.y + 3.0f * M * M * T * P1.y + 3.0f * M * T * T * P2.y + T * T * T * P3.y;
            if (Side == 0)
            {
                Push(Lateral, Radius);
            }
            else
            {
                Outline.insert(Outline.begin(), ImVec2(X(-Lateral), Y(Radius)));
            }
        }
        // the tuck under the rim flange, which closes the outline on each side
        if (Side == 0)
        {
            Push(D.BeadHalf - 3.0f, D.RimRadius - 1.0f);
        }
        else
        {
            Outline.insert(Outline.begin(), ImVec2(X(-(D.BeadHalf - 3.0f)), Y(D.RimRadius - 1.0f)));
        }
        (void)Sign;
    }
    Draw->AddConvexPolyFilled(Outline.data(), int(Outline.size()), CardRaisedTint);
    Draw->AddPolyline(Outline.data(), int(Outline.size()), TextTint, ImDrawFlags_Closed, 1.2f);

    // ③ the floor the pattern cuts to, dashed, under the moulded surface
    for (size_t I = 1; I < Surface.size(); I += 2u)
    {
        const float LateralA = (float(int(I) - 1 - TreadSamples) / float(TreadSamples)) * D.AcrossHalf;
        const float LateralB = (float(int(I) - TreadSamples) / float(TreadSamples)) * D.AcrossHalf;
        const TyreProfileSample A = EvaluateTyreProfile(LateralA, S, D);
        const TyreProfileSample B = EvaluateTyreProfile(LateralB, S, D);
        Draw->AddLine(ImVec2(X(A.Lateral), Y(A.Radius - (S.TreadDepth + Worn) * A.ContactWeight)),
                      ImVec2(X(B.Lateral), Y(B.Radius - (S.TreadDepth + Worn) * B.ContactWeight)),
                      YellowTint, 1.0f);
    }

    // ④ the handles, each one a direct grip on the parameter it names
    bool Changed = false;
    const std::vector<ProfileHandle> Handles = ProfileHandles(S, D);
    for (size_t I = 0; I < Handles.size(); ++I)
    {
        const ProfileHandle& Handle = Handles[I];
        const ImVec2 At(X(Handle.Lateral), Y(Handle.Radius));

        ImGui::SetCursorScreenPos(ImVec2(At.x - 8.0f, At.y - 8.0f));
        ImGui::PushID(int(I));
        ImGui::InvisibleButton(Id, ImVec2(16.0f, 16.0f));
        const bool Active = ImGui::IsItemActive();
        if (Active)
        {
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            const float  Sign  = Handle.Mirrored ? -1.0f : 1.0f;
            const float  Lateral = Sign * (Mouse.x - OX) / Scale;
            const float  Radius  = D.RimRadius - 20.0f + (OY - Mouse.y) / Scale;
            if (std::strcmp(Handle.Identity, "tread") == 0)
            {
                S.TreadFraction = std::clamp(Lateral / D.HalfWidth, 0.7f, 0.98f);
                S.Crown = std::clamp(D.OuterRadius - Radius, 0.0f, 8.0f);
            }
            else if (std::strcmp(Handle.Identity, "shoulder") == 0)
            {
                S.Shoulder = std::clamp((Lateral - D.TreadHalf) / std::sin(π * 0.25f), 4.0f, 40.0f);
            }
            else if (std::strcmp(Handle.Identity, "bulge") == 0)
            {
                S.Bulge = std::clamp((Lateral / D.HalfWidth - 0.6f) / 0.4f, 0.4f, 1.6f);
            }
            else if (std::strcmp(Handle.Identity, "depth") == 0)
            {
                S.TreadDepth = std::clamp(std::round((D.OuterRadius - Radius) * 2.0f) * 0.5f, 2.0f, 20.0f);
            }
            Changed = true;
        }
        ImGui::PopID();

        Draw->AddCircleFilled(At, Active ? 7.0f : 5.0f, IM_COL32(0x11, 0x11, 0x11, 255), 16);
        Draw->AddCircle(At, Active ? 7.0f : 5.0f, Handle.Colour, 16, 2.0f);
        if (!Handle.Mirrored)
        {
            const bool Below = std::strcmp(Handle.Identity, "depth") == 0;
            const ImVec2 Size = Measure(10.0f, Handle.Label);
            Ink(Draw, 10.0f, ImVec2(Below ? At.x - Size.x * 0.5f : At.x + 9.0f,
                                    Below ? At.y + 8.0f : At.y - 14.0f), Handle.Colour, Handle.Label);
        }
    }

    // ⑤ the dimension callouts
    char Figure[96];
    std::snprintf(Figure, sizeof(Figure), "%g mm", double(S.Width));
    const ImVec2 WidthSize = Measure(11.0f, Figure);
    Ink(Draw, 11.0f, ImVec2(OX - WidthSize.x * 0.5f, Y(D.OuterRadius) - 22.0f), MutedTint, Figure);
    std::snprintf(Figure, sizeof(Figure), "rim %g\" · %.0f mm", double(S.Rim), double(D.BeadHalf * 2.0f));
    const ImVec2 RimSize = Measure(11.0f, Figure);
    Ink(Draw, 11.0f, ImVec2(OX - RimSize.x * 0.5f, OY - 16.0f), MutedTint, Figure);
    std::snprintf(Figure, sizeof(Figure), "%.0f mm", double(D.SectionHeight));
    Ink(Draw, 11.0f, ImVec2(X(D.HalfWidth) + 10.0f, (Y(D.OuterRadius) + Y(D.RimRadius)) * 0.5f), MutedTint, Figure);
    std::snprintf(Figure, sizeof(Figure), "crown %.1f · R%.0f · depth %g",
                  double(S.Crown), double(D.ShoulderRadius), double(S.TreadDepth));
    InkRight(Draw, 11.0f, Max.x - 8.0f, Min.y + 6.0f, DimTint, Figure);

    ImGui::SetCursorScreenPos(Min);
    ImGui::Dummy(ImVec2(W, H));
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RIM SECTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws the rim's lathe profile with its bead-seat and drop-well datums.
void DrawRimSection(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, const RimSpecification& Rim,
                    const TreadDerivedValues& D)
{
    Draw->AddRectFilled(Min, Max, IM_COL32(0x10, 0x11, 0x14, 255), 14.0f);
    const std::vector<RimProfilePoint> Profile = RimProfilePoints(Rim, D);
    if (Profile.empty())
    {
        return;
    }

    float MinX = Profile[0].Lateral, MaxX = Profile[0].Lateral;
    float MinR = Profile[0].Radius,  MaxR = Profile[0].Radius;
    for (const RimProfilePoint& P : Profile)
    {
        MinX = std::min(MinX, P.Lateral);
        MaxX = std::max(MaxX, P.Lateral);
        MinR = std::min(MinR, P.Radius);
        MaxR = std::max(MaxR, P.Radius);
    }
    const float W = Max.x - Min.x;
    const float H = Max.y - Min.y;
    const float Scale = std::min((W - 30.0f) / std::max(1.0f, MaxX - MinX),
                                 (H - 48.0f) / std::max(1.0f, MaxR - MinR));
    const auto X = [&](float Lateral) noexcept { return Min.x + W * 0.5f + Lateral * Scale; };
    const auto Y = [&](float Radius) noexcept { return Max.y - 24.0f - (Radius - MinR) * Scale; };

    std::vector<ImVec2> Points;
    Points.reserve(Profile.size());
    for (const RimProfilePoint& P : Profile)
    {
        Points.push_back(ImVec2(X(P.Lateral), Y(P.Radius)));
    }
    const RimMaterial& Spec = RimMaterials()[std::min(size_t(Rim.Material), RimMaterials().size() - 1u)];
    const ImU32 Body = IM_COL32((Spec.Colour >> 16) & 0xFFu, (Spec.Colour >> 8) & 0xFFu, Spec.Colour & 0xFFu, 219);
    Draw->AddConvexPolyFilled(Points.data(), int(Points.size()), Body);
    Draw->AddPolyline(Points.data(), int(Points.size()), IM_COL32(0xF2, 0xF3, 0xF4, 255), ImDrawFlags_Closed, 1.5f);

    const float Seat = D.RimRadius + 2.0f;
    const float Well = Seat - Rim.DropWell;
    const auto Datum = [&](float Radius, ImU32 Colour)
    {
        for (float Dash = X(MinX - 4.0f); Dash < X(MaxX + 4.0f); Dash += 6.0f)
        {
            Draw->AddLine(ImVec2(Dash, Y(Radius)), ImVec2(std::min(Dash + 3.0f, X(MaxX + 4.0f)), Y(Radius)),
                          Colour, 1.0f);
        }
    };
    Datum(Seat, GreenTint);
    Datum(Well, RedTint);

    Ink(Draw, 11.0f, ImVec2(Min.x + 10.0f, Min.y + 6.0f),  YellowTint, "flange");
    Ink(Draw, 11.0f, ImVec2(Min.x + 10.0f, Min.y + 20.0f), GreenTint,  "bead seat");
    Ink(Draw, 11.0f, ImVec2(Min.x + 10.0f, Min.y + 34.0f), RedTint,    "drop well");

    ImGui::SetCursorScreenPos(Min);
    ImGui::Dummy(ImVec2(W, H));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RASTER CACHE
//------------------------------------------------------------------------------------------------------------------------

void RefreshRaster(TyreGeneratorState& State)
{
    const bool StaleField   = State.RasterRevision != State.Revision;
    const bool StaleChannel = State.MapChannel != State.TreadChannel;
    if (!StaleField && !StaleChannel && State.Field.Width != 0u)
    {
        return;
    }
    const TreadDerivedValues D = DeriveTreadValues(State.Document.Carcass);
    if (StaleField || State.Field.Width == 0u)
    {
        State.Field = RasterizeTreadPattern(State.Document.Pattern, State.Document.Carcass, D, 192u);
        State.RasterRevision = State.Revision;
    }
    State.Map = ReadTreadMap(State.Field, State.Document.Carcass, D, State.Document.Appearance,
                             State.TreadChannel);
    State.MapChannel = State.TreadChannel;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRESET THUMBNAILS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The depth field behind each preset's thumbnail, built once and kept.
/// note  Fourteen presets at sixty rows is under a megapixel in total, so the whole grid is cheaper than one
///       frame of the live tread and there is no reason to rebuild it.
[[nodiscard]] const std::vector<TreadDepthField>& PresetThumbnails()
{
    static const std::vector<TreadDepthField> Thumbnails = []
    {
        std::vector<TreadDepthField> Fields;
        Fields.reserve(TyrePresets().size());
        for (const TyrePreset& Preset : TyrePresets())
        {
            const TreadDerivedValues D = DeriveTreadValues(Preset.Carcass);
            Fields.push_back(RasterizeTreadPattern(Preset.Pattern, Preset.Carcass, D, 60u, true));
        }
        return Fields;
    }();
    return Thumbnails;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRESET SEATING
//------------------------------------------------------------------------------------------------------------------------

void ApplyTyrePreset(TyreGeneratorState& State, int Index)
{
    const std::vector<TyrePreset>& Presets = TyrePresets();
    if (Presets.empty())
    {
        return;
    }
    const size_t Pick = size_t(std::clamp(Index, 0, int(Presets.size()) - 1));
    const TyrePreset& Preset = Presets[Pick];

    State.Document.Name    = Preset.Name;
    State.Document.Kind    = Preset.Kind;
    State.Document.Carcass = Preset.Carcass;
    State.Document.Pattern = Preset.Pattern;
    State.Document.Carcass.Wear = 0.0f;
    State.PickedPreset = int(Pick);
    State.PickedLayer  = -1;
    State.Touch();
}

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE TYRE PAGE
//------------------------------------------------------------------------------------------------------------------------

void RecordTyrePage(TyreGeneratorState& State)
{
    TreadSpecification& S = State.Document.Carcass;
    const TreadDerivedValues D = DeriveTreadValues(S);
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const float Worn = S.Wear * S.TreadDepth;
    const float Left = S.TreadDepth - Worn;

    //----------------------------------------------------------------------------------------------------------
    // ① tyre size
    //----------------------------------------------------------------------------------------------------------
    {
        char Right[48];
        std::snprintf(Right, sizeof(Right), "%.0f mm rim", double(D.RimRadius * 2.0f));
        Card Panel;
        Panel.Begin("Tyre size", GreenTint, Right);

        // the marked size, set as one headline with the aspect dimmed the way a sidewall prints it
        const ImVec2 At = ImGui::GetCursorScreenPos();
        char Head[16];
        std::snprintf(Head, sizeof(Head), "%g", double(S.Width));
        float X = At.x;
        Ink(Draw, 42.0f, ImVec2(X, At.y), WhiteTint, Head);
        X += Measure(42.0f, Head).x;
        std::snprintf(Head, sizeof(Head), "/%g", double(S.Aspect));
        Ink(Draw, 42.0f, ImVec2(X, At.y), DimTint, Head);
        X += Measure(42.0f, Head).x;
        std::snprintf(Head, sizeof(Head), " R%g", double(S.Rim));
        Ink(Draw, 16.0f, ImVec2(X + 4.0f, At.y + 24.0f), DimTint, Head);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 54.0f));

        const ImVec2 Pills = ImGui::GetCursorScreenPos();
        float Advance = 0.0f;
        float PX = Pills.x;
        char Capsule1[48];
        std::snprintf(Capsule1, sizeof(Capsule1), "sidewall %.0f mm", double(D.SectionHeight));
        Capsule(Draw, ImVec2(PX, Pills.y), Capsule1, CardRaisedTint, MutedTint, &Advance);
        PX += Advance;
        std::snprintf(Capsule1, sizeof(Capsule1), "tread %.0f mm", double(D.TreadHalf * 2.0f));
        Capsule(Draw, ImVec2(PX, Pills.y), Capsule1, CardRaisedTint, MutedTint, &Advance);
        PX += Advance;
        if (S.Wear > 0.0f)
        {
            std::snprintf(Capsule1, sizeof(Capsule1), "-%.1f mm worn", double(Worn));
        }
        else
        {
            std::snprintf(Capsule1, sizeof(Capsule1), "new");
        }
        const bool Illegal = Left < 1.6f;
        Capsule(Draw, ImVec2(PX, Pills.y), Capsule1,
                Illegal ? IM_COL32(0xE5, 0x42, 0x3F, 36) : IM_COL32(0x38, 0xC4, 0x6D, 31),
                Illegal ? RedTint : GreenTint, &Advance);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 29.0f));

        SectionLabel("Size");
        if (Slider("##width",  "Section width (mm)", &S.Width,  135.0f, 405.0f, 5.0f, 0)) { State.Touch(); }
        if (Slider("##aspect", "Aspect ratio (%)",   &S.Aspect,  20.0f,  85.0f, 5.0f, 0)) { State.Touch(); }
        SectionLabel("Wear");
        if (Slider("##wearbias", "Wear bias (edge-centre)", &S.WearBias, -1.0f, 1.0f, 0.01f, 2)) { State.Touch(); }
        SectionLabel("Tread mesh");
        {
            const ImVec2 Row = ImGui::GetCursorScreenPos();
            Ink(Draw, 13.0f, ImVec2(Row.x, Row.y + 5.0f), MutedTint, "Mesh mode");
            Ink(Draw, 13.0f, ImVec2(Row.x + LabelColumnWidth + 8.0f, Row.y + 5.0f), TextTint, "Polygon topology");
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, RowHeight + RowGap));
        }
        if (Slider("##detail",  "Polygon detail (mm)", &S.PolygonDetail,   3.0f,   16.0f, 0.5f, 1)) { State.Touch(); }
        if (Slider("##around",  "Segments around",     &S.SegmentsAround, 180.0f, 1440.0f, 20.0f, 0)) { State.Touch(); }
        Panel.End();
    }

    //----------------------------------------------------------------------------------------------------------
    // ② tyre condition
    //----------------------------------------------------------------------------------------------------------
    {
        char Right[16];
        if (S.Wear <= 0.0f)        { std::snprintf(Right, sizeof(Right), "New"); }
        else if (S.Wear >= 1.0f)   { std::snprintf(Right, sizeof(Right), "Bald"); }
        else                       { std::snprintf(Right, sizeof(Right), "%.0f%%", double(S.Wear * 100.0f)); }

        Card Panel;
        Panel.Begin("Tyre condition", GreenTint, Right);

        const ImVec2 Head = ImGui::GetCursorScreenPos();
        Ink(Draw, 13.0f, ImVec2(Head.x, Head.y + 7.0f), MutedTint, "Tread wear");
        char Percent[16];
        std::snprintf(Percent, sizeof(Percent), "%.0f%%", double(S.Wear * 100.0f));
        InkRight(Draw, 20.0f, Head.x + ImGui::GetContentRegionAvail().x, Head.y, WhiteTint, Percent);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 26.0f));

        // the wear slider runs the full width, with no label column: it is the card's only control
        {
            const ImVec2 Row = ImGui::GetCursorScreenPos();
            const float  W   = ImGui::GetContentRegionAvail().x;
            const float  Mid = Row.y + 11.0f;
            ImGui::InvisibleButton("##wear", ImVec2(W, 22.0f));
            if (ImGui::IsItemActive())
            {
                S.Wear = std::clamp((ImGui::GetIO().MousePos.x - Row.x) / W, 0.0f, 1.0f);
                State.Touch();
            }
            Draw->AddRectFilled(ImVec2(Row.x, Mid - 1.0f), ImVec2(Row.x + W, Mid + 1.0f), TrackTint, 1.0f);
            const float ThumbX = Row.x + W * S.Wear;
            Draw->AddCircleFilled(ImVec2(ThumbX, Mid), 9.0f, CardTint, 16);
            Draw->AddCircleFilled(ImVec2(ThumbX, Mid), 6.0f, WhiteTint, 16);
            ImGui::SetCursorScreenPos(ImVec2(Row.x, Row.y + 24.0f));
        }

        char Note[80];
        std::snprintf(Note, sizeof(Note), "%.1f mm worn · %.1f mm remaining", double(Worn), double(Left));
        Hint(Note, DimTint, 11.0f);
        Panel.End();
    }

    //----------------------------------------------------------------------------------------------------------
    // ③ the cross-section, with the five scalars its handles do not reach
    //----------------------------------------------------------------------------------------------------------
    {
        Card Panel;
        Panel.Begin("Cross-section", YellowTint, "drag the handles");
        const ImVec2 At = ImGui::GetCursorScreenPos();
        const float  W  = ImGui::GetContentRegionAvail().x;
        if (DrawCrossSection(Draw, At, ImVec2(At.x + W, At.y + 210.0f), S, "##section"))
        {
            State.Touch();
        }
        ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + 218.0f));

        if (Slider("##depth",     "Tread depth (mm)",     &S.TreadDepth,     2.0f, 20.0f, 0.5f,  1)) { State.Touch(); }
        if (Slider("##treadfrac", "Tread width fraction", &S.TreadFraction,  0.7f,  0.98f, 0.01f, 2)) { State.Touch(); }
        if (Slider("##crown",     "Crown drop (mm)",      &S.Crown,          0.0f,  8.0f, 0.1f,  1)) { State.Touch(); }
        if (Slider("##shoulder",  "Shoulder radius (mm)", &S.Shoulder,       4.0f, 40.0f, 1.0f,  0)) { State.Touch(); }
        if (Slider("##bulge",     "Sidewall bulge",       &S.Bulge,          0.4f,  1.6f, 0.01f, 2)) { State.Touch(); }
        Panel.End();
    }

    //----------------------------------------------------------------------------------------------------------
    // ④ the tread materials, which is the pattern raster itself
    //----------------------------------------------------------------------------------------------------------
    {
        Card Panel;
        Panel.Begin("Tread materials", RedTint, "");

        // the four-way channel switch sits on the title row, right-aligned
        {
            const ImVec2 Row = ImGui::GetCursorScreenPos();
            const float  W   = ImGui::GetContentRegionAvail().x;
            const char*  Names[4] = { "Colour", "Depth", "Normal", "Rough" };
            float Widths[4];
            float Total = 6.0f;
            for (int I = 0; I < 4; ++I)
            {
                Widths[I] = Measure(11.0f, Names[I]).x + 18.0f;
                Total += Widths[I];
            }
            const float X0 = Row.x + W - Total;
            const float Y0 = Row.y - 42.0f;
            Draw->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + Total, Y0 + 26.0f), CardRaisedTint, 13.0f);
            float X = X0 + 3.0f;
            for (int I = 0; I < 4; ++I)
            {
                ImGui::SetCursorScreenPos(ImVec2(X, Y0 + 3.0f));
                ImGui::PushID(I);
                ImGui::InvisibleButton("##chan", ImVec2(Widths[I], 20.0f));
                if (ImGui::IsItemClicked())
                {
                    State.TreadChannel = TreadMapChannel(I);
                }
                ImGui::PopID();
                const bool On = int(State.TreadChannel) == I;
                if (On)
                {
                    Draw->AddRectFilled(ImVec2(X, Y0 + 3.0f), ImVec2(X + Widths[I], Y0 + 23.0f), CardHoverTint, 10.0f);
                }
                const ImVec2 Size = Measure(11.0f, Names[I]);
                Ink(Draw, 11.0f, ImVec2(X + (Widths[I] - Size.x) * 0.5f, Y0 + 7.0f), On ? WhiteTint : MutedTint, Names[I]);
                X += Widths[I];
            }
            ImGui::SetCursorScreenPos(Row);
        }

        RefreshRaster(State);
        const ImVec2 At = ImGui::GetCursorScreenPos();
        const float  W  = ImGui::GetContentRegionAvail().x;

        // the detail window shows a crop of the circumference at true aspect; the strip shows the whole map
        const float  Fraction = std::clamp((W / 150.0f) * float(State.Field.Height)
                                           / std::max(1.0f, float(State.Field.Width)), 0.02f, 1.0f);
        const float  Brighten = (State.TreadChannel == TreadMapChannel::Colour) ? 2.6f : 1.0f;
        Draw->PushClipRect(At, ImVec2(At.x + W, At.y + 150.0f), true);
        DrawMap(Draw, At, ImVec2(At.x + W, At.y + 150.0f), State.Map, Fraction, Brighten);
        Draw->PopClipRect();
        ImGui::Dummy(ImVec2(W, 156.0f));

        const ImVec2 Strip = ImGui::GetCursorScreenPos();
        Draw->PushClipRect(Strip, ImVec2(Strip.x + W, Strip.y + 26.0f), true);
        DrawMap(Draw, Strip, ImVec2(Strip.x + W, Strip.y + 26.0f), State.Map, 1.0f, Brighten);
        Draw->PopClipRect();
        Draw->AddRect(ImVec2(Strip.x + 1.0f, Strip.y + 1.0f),
                      ImVec2(Strip.x + W * Fraction - 1.0f, Strip.y + 25.0f), WhiteTint, 0.0f, 0, 2.0f);
        ImGui::Dummy(ImVec2(W, 32.0f));

        char Dims[160];
        std::snprintf(Dims, sizeof(Dims),
                      "Showing %.0f mm of %.0f mm circumference · map %u×%u @ %.2f px/mm",
                      double(float(State.Field.Width) * Fraction / std::max(0.0001f, State.Field.Pixels)),
                      double(D.Circumference), State.Field.Width, State.Field.Height, double(State.Field.Pixels));
        Hint(Dims, MutedTint, 12.0f);
        Panel.End();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE RIM PAGE
//------------------------------------------------------------------------------------------------------------------------

void RecordRimPage(TyreGeneratorState& State)
{
    TreadSpecification& S = State.Document.Carcass;
    RimSpecification&   R = State.Document.Rim;
    const TreadDerivedValues D = DeriveTreadValues(S);
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    {
        Card Panel;
        Panel.Begin("Rim generator", OrangeTint, "");

        SectionLabel("Tyre fit");
        if (Slider("##rimdia", "Rim diameter (in)",   &S.Rim,              13.0f, 24.0f, 1.0f,  0)) { State.Touch(); }
        if (Slider("##beadfr", "Bead width fraction", &S.RimWidthFraction,  0.6f, 0.92f, 0.01f, 2)) { State.Touch(); }
        Hint("These shared dimensions keep the rim seated correctly inside the selected tyre.");

        SectionLabel("Barrel profile");
        if (Slider("##flange", "Flange height (mm)", &R.Flange,   4.0f, 14.0f, 0.5f, 1)) { State.Touch(); }
        if (Slider("##well",   "Drop well (mm)",     &R.DropWell, 3.0f, 18.0f, 0.5f, 1)) { State.Touch(); }

        SectionLabel("Material");
        {
            std::vector<std::string> Names;
            Names.reserve(RimMaterials().size());
            for (const RimMaterial& M : RimMaterials())
            {
                Names.emplace_back(M.Name);
            }
            int Picked = int(R.Material);
            if (Choice("##rimmat", "Rim material", &Picked, Names))
            {
                R.Material = uint32_t(Picked);
                State.Touch();
            }
        }
        if (Swatch("##rimtint", "Rim tint", &R.Tint)) { State.Touch(); }

        SectionLabel("Centre construction");
        {
            const std::vector<std::string> Layouts = { "Blank barrel", "Straight spokes", "Split spokes", "Y spokes" };
            int Picked = int(R.Layout);
            if (Choice("##layout", "Centre design", &Picked, Layouts))
            {
                R.Layout = RimCentreLayout(Picked);
                State.Touch();
            }
        }
        if (Slider("##pieces",    "Piece count",      &R.PieceCount,   3.0f, 18.0f, 1.0f,  0)) { State.Touch(); }
        if (Slider("##piecew",    "Piece width (mm)", &R.PieceWidth,   8.0f, 34.0f, 1.0f,  0)) { State.Touch(); }
        if (Slider("##pieced",    "Piece depth (mm)", &R.PieceDepth,   4.0f, 16.0f, 1.0f,  0)) { State.Touch(); }
        if (Slider("##hub",       "Hub size",         &R.HubFraction, 0.16f, 0.46f, 0.01f, 2)) { State.Touch(); }
        Hint("Pieces are procedural beams. Blank barrel keeps the centre fully open; split and Y designs "
             "branch from each hub piece.");

        SectionLabel("Air valve");
        if (Check("##valve",  "Inflation valve", &R.Valve))                                  { State.Touch(); }
        if (Slider("##vangle", "Valve angle (°)", &R.ValveAngle, 0.0f, 359.0f, 1.0f, 0))     { State.Touch(); }
        Panel.End();
    }

    {
        Card Panel;
        Panel.Begin("Rim cross-section", YellowTint, "procedural profile");
        const ImVec2 At = ImGui::GetCursorScreenPos();
        const float  W  = ImGui::GetContentRegionAvail().x;
        DrawRimSection(Draw, At, ImVec2(At.x + W, At.y + 190.0f), R, D);
        ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + 198.0f));

        char Info[160];
        std::snprintf(Info, sizeof(Info), "%g in rim · %.0f mm bead width · %.1f mm flange · %.1f mm drop well",
                      double(S.Rim), double(D.BeadHalf * 2.0f), double(R.Flange), double(R.DropWell));
        Hint(Info);
        Panel.End();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LOOK PAGE
//------------------------------------------------------------------------------------------------------------------------

void RecordLookPage(TyreGeneratorState& State)
{
    TyreAppearanceSpecification& A = State.Document.Appearance;
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    {
        Card Panel;
        Panel.Begin("Compound & look", YellowTint, "");
        if (Swatch("##rubber", "Rubber colour", &A.Rubber))              { State.Touch(); }
        if (Check("##letter",  "Show sidewall decals", &A.Lettering))    { State.Touch(); }
        if (TextField("##brand", "Brand", &A.Brand))                     { State.Touch(); }

        SectionLabel("Factory tread paint");
        if (Check("##stripes", "Coloured tread stripes", &A.FactoryStripes)) { State.Touch(); }
        {
            const std::vector<std::string> Patterns = { "Solid zig-zag", "Dotted zig-zag", "Dashed zig-zag",
                                                        "Stitch marks" };
            int Picked = int(A.Pattern);
            if (Choice("##stripepat", "Stripe pattern", &Picked, Patterns))
            {
                A.Pattern = StripePattern(Picked);
                State.Touch();
            }
        }
        if (Swatch("##red",  "Red stripe",  &A.StripeRed))  { State.Touch(); }
        if (Swatch("##blue", "Blue stripe", &A.StripeBlue)) { State.Touch(); }
        if (Slider("##soff",  "Lateral position (mm)", &A.StripeOffset, -70.0f, 70.0f, 0.5f, 1)) { State.Touch(); }
        if (Slider("##swid",  "Stripe width (mm)",     &A.StripeWidth,    0.5f,  4.0f, 0.1f, 1)) { State.Touch(); }
        if (Slider("##sgap",  "Stripe gap (mm)",       &A.StripeGap,      1.5f, 10.0f, 0.1f, 1)) { State.Touch(); }
        if (Slider("##swav",  "Stripe weave (mm)",     &A.StripeWave,     0.0f,  4.0f, 0.1f, 1)) { State.Touch(); }
        if (Slider("##spit",  "Pattern pitch (mm)",    &A.StripePitch,    4.0f, 24.0f, 0.5f, 1)) { State.Touch(); }
        Panel.End();
    }

    {
        char Right[32];
        std::snprintf(Right, sizeof(Right), "%zu layers", A.Decals.size());
        Card Panel;
        Panel.Begin("Sidewall decals", RedTint, Right);
        Hint("Build the sidewall lettering as editable text layers. Select a layer to change its copy, "
             "typeface, size, colour and placement.");
        if (Button("Add text decal", true))
        {
            SidewallDecalSpecification Fresh;
            Fresh.Name = "New text decal";
            Fresh.Repeat = false;
            char Identity[32];
            std::snprintf(Identity, sizeof(Identity), "decal-%zu", A.Decals.size() + 1u);
            Fresh.Identity = Identity;
            A.Decals.push_back(Fresh);
            State.PickedDecal = int(A.Decals.size()) - 1;
            State.Touch();
        }
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 8.0f));

        const float W = ImGui::GetContentRegionAvail().x;
        for (int I = 0; I < int(A.Decals.size()); ++I)
        {
            SidewallDecalSpecification& Decal = A.Decals[size_t(I)];
            const ImVec2 Row = ImGui::GetCursorScreenPos();
            ImGui::PushID(I);
            ImGui::InvisibleButton("##decal", ImVec2(W, 40.0f));
            if (ImGui::IsItemClicked())
            {
                State.PickedDecal = (State.PickedDecal == I) ? -1 : I;
            }
            const bool Picked = (State.PickedDecal == I);
            const float Fade = Decal.Enabled ? 1.0f : 0.48f;

            Draw->AddRectFilled(Row, ImVec2(Row.x + W, Row.y + 36.0f), CardRaisedTint, 12.0f);
            if (Picked)
            {
                Draw->AddRect(Row, ImVec2(Row.x + W, Row.y + 36.0f), WhiteTint, 13.0f, 0, 1.0f);
            }
            const ImU32 Chip = IM_COL32((Decal.Colour >> 16) & 0xFFu, (Decal.Colour >> 8) & 0xFFu,
                                        Decal.Colour & 0xFFu, ImU8(255.0f * Fade));
            Draw->AddRectFilled(ImVec2(Row.x + 13.0f, Row.y + 9.0f), ImVec2(Row.x + 16.0f, Row.y + 27.0f), Chip, 2.0f);
            Ink(Draw, 13.0f, ImVec2(Row.x + 24.0f, Row.y + 5.0f),
                IM_COL32(0xE6, 0xE7, 0xEA, ImU8(255.0f * Fade)), Decal.Name.c_str());
            Ink(Draw, 11.0f, ImVec2(Row.x + 24.0f, Row.y + 20.0f),
                IM_COL32(0x8A, 0x8D, 0x94, ImU8(255.0f * Fade)), Decal.Text.c_str());
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(Row.x, Row.y + 40.0f));
        }

        ImGui::Dummy(ImVec2(W, 10.0f));
        Draw->AddLine(ImGui::GetCursorScreenPos(),
                      ImVec2(ImGui::GetCursorScreenPos().x + W, ImGui::GetCursorScreenPos().y), HairlineTint, 1.0f);
        ImGui::Dummy(ImVec2(W, 10.0f));

        if (State.PickedDecal >= 0 && State.PickedDecal < int(A.Decals.size()))
        {
            SidewallDecalSpecification& Decal = A.Decals[size_t(State.PickedDecal)];
            if (TextField("##dname", "Layer name", &Decal.Name))  { State.Touch(); }
            if (TextField("##dtext", "Text",       &Decal.Text))  { State.Touch(); }
            {
                const std::vector<std::string> Faces_ = { "Outfit", "Impact / heavy", "Clean sans",
                                                          "Classic serif", "Technical mono", "Script" };
                int Picked = int(Decal.Face);
                if (Choice("##dface", "Typeface", &Picked, Faces_))
                {
                    Decal.Face = DecalFace(Picked);
                    State.Touch();
                }
            }
            if (Slider("##dsize", "Size",    &Decal.Size,    8.0f, 120.0f, 1.0f, 0)) { State.Touch(); }
            if (Swatch("##dcol",  "Colour",  &Decal.Colour))                         { State.Touch(); }
            if (Slider("##dx",    "Across (%)", &Decal.Across, 0.0f, 100.0f, 0.5f, 1)) { State.Touch(); }
            if (Slider("##dy",    "Along (%)",  &Decal.Along,  0.0f, 100.0f, 0.5f, 1)) { State.Touch(); }
            {
                const std::vector<std::string> Alignments = { "Left", "Centre", "Right" };
                int Picked = int(Decal.Alignment);
                if (Choice("##dalign", "Alignment", &Picked, Alignments))
                {
                    Decal.Alignment = DecalAlignment(Picked);
                    State.Touch();
                }
            }
            if (Slider("##dweight", "Weight",  &Decal.Weight,  100.0f, 900.0f, 100.0f, 0)) { State.Touch(); }
            if (Slider("##dopac",   "Opacity", &Decal.Opacity,   0.0f,   1.0f,   0.01f, 2)) { State.Touch(); }
            if (Check("##ditalic",  "Italic",   &Decal.Italic))   { State.Touch(); }
            if (Check("##demboss",  "Embossed", &Decal.Embossed)) { State.Touch(); }
            if (Check("##drepeat",  "Repeat around", &Decal.Repeat)) { State.Touch(); }
            if (Check("##denable",  "Enabled",  &Decal.Enabled))  { State.Touch(); }
            Hint("Tokens: {brand} {name} {size} {type} {depth} {serial}. They resolve against the document "
                 "when the sidewall is rasterised, so a size line never goes stale.", DimTint, 11.0f);
        }
        else
        {
            Hint("Select a decal layer to edit it.");
        }
        Panel.End();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EXPORT PAGE
//------------------------------------------------------------------------------------------------------------------------

void RecordExportPage(TyreGeneratorState& State)
{
    Card Panel;
    Panel.Begin("Export", RedTint, "");
    Hint("Textures are exported at the current map resolution. GLB is in metres, Y-up, tyre axis along X.");

    // 📝 A wrapping button row needs its own cursor. Reading the live cursor inside the loop reads the
    //    position the previous button left behind, and the row walks diagonally down the card.
    const float  W   = ImGui::GetContentRegionAvail().x;
    const ImVec2 Row = ImGui::GetCursorScreenPos();
    const char*  Labels[4] = { "Pattern JSON", "Height PNG", "Normal PNG", "GLB (3D)" };
    float X = Row.x;
    float Y = Row.y;
    for (int I = 0; I < 4; ++I)
    {
        const float Width = Measure(13.0f, Labels[I]).x + 28.0f;
        if (X > Row.x && X + Width > Row.x + W)
        {
            X = Row.x;
            Y += 36.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (Button(Labels[I], I == 3))
        {
            State.Notice = Labels[I];
            State.NoticeFade = 1.0f;
        }
        X += Width + 6.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(Row.x, Y + 42.0f));
    Hint("Import a pattern JSON to replace the layer sequence without touching the carcass.");
    Panel.End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The centre column: an empty three-dimensional viewport with the generator's floating readouts over it.
void RecordViewport(TyreGeneratorState& State, ImVec2 Min, ImVec2 Max)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const TreadSpecification& S = State.Document.Carcass;
    const TreadDerivedValues  D = DeriveTreadValues(S);
    const float Worn = S.Wear * S.TreadDepth;
    const float Left = S.TreadDepth - Worn;

    Draw->AddRectFilled(Min, Max, ViewportTint, CardCornerRadius);

    const float Room = Max.x - Min.x;
    // 📝 The generator's overlays are laid out for a wide centre column. Below that the two blocks would
    //    collide, so the tiles fall to one column and the badge gives up its width rather than overprint.
    const bool  Narrow = Room < 560.0f;

    // ① the badge, top left
    {
        const ImVec2 At(Min.x + 16.0f, Min.y + 16.0f);
        const float  NameSize = Narrow ? 26.0f : 34.0f;
        const float  W = std::max(Narrow ? 196.0f : 230.0f,
                                  Measure(NameSize, State.Document.Name.c_str()).x + 40.0f);
        Draw->AddRectFilled(At, ImVec2(At.x + W, At.y + 108.0f), IM_COL32(0x17, 0x18, 0x1B, 199), 20.0f);
        Draw->AddRectFilled(ImVec2(At.x + 18.0f, At.y + 17.0f), ImVec2(At.x + 21.0f, At.y + 31.0f), GreenTint, 1.5f);
        Ink(Draw, 13.0f, ImVec2(At.x + 29.0f, At.y + 16.0f), MutedTint, State.Document.Kind.c_str());
        Draw->PushClipRect(At, ImVec2(At.x + W, At.y + 108.0f), true);
        Ink(Draw, NameSize, ImVec2(At.x + 18.0f, At.y + 38.0f), WhiteTint, State.Document.Name.c_str());
        Draw->PopClipRect();
        char Marked[48];
        std::snprintf(Marked, sizeof(Marked), "%g/%g R%g", double(S.Width), double(S.Aspect), double(S.Rim));
        Ink(Draw, 15.0f, ImVec2(At.x + 18.0f, At.y + 80.0f), MutedTint, Marked);
    }

    // ② the four readout tiles, top right, in two columns
    {
        const int   Columns = Narrow ? 1 : 2;
        const float TileW = 128.0f;
        const float TileH = 68.0f;
        const float X0 = Max.x - 16.0f - TileW * float(Columns) - 8.0f * float(Columns - 1);
        const float Y0 = Min.y + 16.0f;
        const float Diameter = (D.OuterRadius - Worn) * 2.0f;
        const ImU32 Health = (Left < 1.6f) ? RedTint : (Left < 3.0f) ? YellowTint : GreenTint;

        char Values[4][24];
        char Units[4][24];
        std::snprintf(Values[0], sizeof(Values[0]), "%.0f", double(Diameter));
        std::snprintf(Units[0],  sizeof(Units[0]),  "mm");
        std::snprintf(Values[1], sizeof(Values[1]), "%.1f", double(Left));
        std::snprintf(Units[1],  sizeof(Units[1]),  "/ %g mm", double(S.TreadDepth));
        std::snprintf(Values[2], sizeof(Values[2]), "%.2f", double(π * Diameter / 1000.0f));
        std::snprintf(Units[2],  sizeof(Units[2]),  "m");
        std::snprintf(Values[3], sizeof(Values[3]), "%.0f",
                      double(S.SegmentsAround * S.SegmentsAcross * 2.0f / 1000.0f));
        std::snprintf(Units[3],  sizeof(Units[3]),  "k tris");

        const char* Labels[4] = { "Overall Ø", "Tread left", "Circumference", "Tread mesh" };
        const ImU32 Dots[4]   = { YellowTint, Health, YellowTint, GreenTint };

        for (int I = 0; I < 4; ++I)
        {
            const float X = X0 + float(I % Columns) * (TileW + 8.0f);
            const float Y = Y0 + float(I / Columns) * (TileH + 8.0f);
            Draw->AddRectFilled(ImVec2(X, Y), ImVec2(X + TileW, Y + TileH), IM_COL32(0x17, 0x18, 0x1B, 209), 18.0f);
            Draw->PushClipRect(ImVec2(X, Y), ImVec2(X + TileW, Y + TileH), true);
            Draw->AddCircleFilled(ImVec2(X + 17.0f, Y + 18.0f), 3.5f, Dots[I], 10);
            Ink(Draw, 11.0f, ImVec2(X + 27.0f, Y + 12.0f), MutedTint, Labels[I], gFaces.Small);
            Ink(Draw, 26.0f, ImVec2(X + 14.0f, Y + 30.0f), WhiteTint, Values[I]);
            const float Advance = Measure(26.0f, Values[I]).x;
            Ink(Draw, 11.0f, ImVec2(X + 17.0f + Advance, Y + 44.0f), DimTint, Units[I], gFaces.Small);
            Draw->PopClipRect();
        }
    }

    // ③ the view bar, bottom left
    {
        const char* Labels[6] = { "Iso", "Side", "Tread", "Front", "Spin", "X-ray cords" };
        float Widths[6];
        float Total = 12.0f;
        for (int I = 0; I < 6; ++I)
        {
            Widths[I] = Measure(13.0f, Labels[I]).x + 22.0f;
            Total += Widths[I] + 4.0f;
        }
        const float X0 = Min.x + 16.0f;
        const float Y0 = Max.y - 16.0f - 38.0f;
        Draw->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + Total, Y0 + 38.0f), IM_COL32(0x17, 0x18, 0x1B, 199), 20.0f);
        float X = X0 + 6.0f;
        for (int I = 0; I < 6; ++I)
        {
            ImGui::SetCursorScreenPos(ImVec2(X, Y0 + 6.0f));
            ImGui::PushID(100 + I);
            ImGui::InvisibleButton("##view", ImVec2(Widths[I], 26.0f));
            const bool Clicked = ImGui::IsItemClicked();
            ImGui::PopID();
            bool On = false;
            if (I < 4)
            {
                On = int(State.View) == I;
                if (Clicked) { State.View = TyreGeneratorView(I); }
            }
            else if (I == 4)
            {
                On = State.Spin;
                if (Clicked) { State.Spin = !State.Spin; }
            }
            else
            {
                On = State.XrayCords;
                if (Clicked) { State.XrayCords = !State.XrayCords; }
            }
            if (On)
            {
                Draw->AddRectFilled(ImVec2(X, Y0 + 6.0f), ImVec2(X + Widths[I], Y0 + 32.0f), CardRaisedTint, 13.0f);
            }
            const ImVec2 Size = Measure(13.0f, Labels[I]);
            Ink(Draw, 13.0f, ImVec2(X + (Widths[I] - Size.x) * 0.5f, Y0 + 12.0f), On ? WhiteTint : MutedTint, Labels[I]);
            X += Widths[I] + 4.0f;
        }
    }

    // ④ the notice strip, which the export buttons raise
    if (State.NoticeFade > 0.0f && !State.Notice.empty())
    {
        const ImVec2 Size = Measure(13.0f, State.Notice.c_str());
        const float  W = Size.x + 32.0f;
        const float  X = (Min.x + Max.x) * 0.5f - W * 0.5f;
        const float  Y = Max.y - 70.0f;
        const ImU8   Alpha = ImU8(std::clamp(State.NoticeFade, 0.0f, 1.0f) * 255.0f);
        Draw->AddRectFilled(ImVec2(X, Y), ImVec2(X + W, Y + 32.0f), IM_COL32(255, 255, 255, Alpha), 16.0f);
        Ink(Draw, 13.0f, ImVec2(X + 16.0f, Y + 8.0f), IM_COL32(0x11, 0x11, 0x11, Alpha), State.Notice.c_str());
        State.NoticeFade -= ImGui::GetIO().DeltaTime * 0.5f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PATTERN COLUMN
//------------------------------------------------------------------------------------------------------------------------

void RecordPatternColumn(TyreGeneratorState& State)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    TreadPatternSpecification& P = State.Document.Pattern;

    //----------------------------------------------------------------------------------------------------------
    // ① the preset grid
    //----------------------------------------------------------------------------------------------------------
    {
        char Right[24];
        std::snprintf(Right, sizeof(Right), "%zu designs", TyrePresets().size());
        Card Panel;
        Panel.Begin("Tread presets", GreenTint, Right);

        const float W = ImGui::GetContentRegionAvail().x;
        const float CellW = (W - 8.0f) * 0.5f;
        const ImVec2 Grid = ImGui::GetCursorScreenPos();
        const std::vector<TyrePreset>& Presets = TyrePresets();
        const std::vector<TreadDepthField>& Thumbnails = PresetThumbnails();

        for (int I = 0; I < int(Presets.size()); ++I)
        {
            const float X = Grid.x + float(I % 2) * (CellW + 8.0f);
            const float Y = Grid.y + float(I / 2) * 88.0f;

            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            ImGui::PushID(200 + I);
            ImGui::InvisibleButton("##preset", ImVec2(CellW, 80.0f));
            if (ImGui::IsItemClicked())
            {
                ApplyTyrePreset(State, I);
            }
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const bool Active = (Presets[size_t(I)].Name == State.Document.Name);
            Draw->AddRectFilled(ImVec2(X, Y), ImVec2(X + CellW, Y + 80.0f), Hovered ? CardHoverTint : CardRaisedTint, 16.0f);
            if (Active)
            {
                Draw->AddRect(ImVec2(X, Y), ImVec2(X + CellW, Y + 80.0f), WhiteTint, 16.0f, 0, 1.0f);
            }
            Draw->PushClipRect(ImVec2(X + 8.0f, Y + 8.0f), ImVec2(X + CellW - 8.0f, Y + 46.0f), true);
            DrawDepthThumbnail(Draw, ImVec2(X + 8.0f, Y + 8.0f), ImVec2(X + CellW - 8.0f, Y + 46.0f),
                               Thumbnails[size_t(I)]);
            Draw->PopClipRect();
            Ink(Draw, 13.0f, ImVec2(X + 8.0f, Y + 50.0f), WhiteTint, Presets[size_t(I)].Name.c_str());
            // the category, letterspaced small caps the way the generator sets it
            float TX = X + 8.0f;
            for (const char* C = Presets[size_t(I)].Kind.c_str(); *C != '\0'; ++C)
            {
                const char One[2] = { char(std::toupper(static_cast<unsigned char>(*C))), '\0' };
                Ink(Draw, 11.0f, ImVec2(TX, Y + 65.0f), MutedTint, One);
                TX += Measure(11.0f, One).x + 1.0f;
            }
        }
        const int Rows = (int(Presets.size()) + 1) / 2;
        ImGui::SetCursorScreenPos(ImVec2(Grid.x, Grid.y + float(Rows) * 88.0f));
        ImGui::Dummy(ImVec2(W, 2.0f));

        const ImVec2 Buttons = ImGui::GetCursorScreenPos();
        if (Button("Random tyre", true))
        {
            RandomiseTyreDesign(State.Document.Carcass, State.Document.Pattern, State.Document.Kind,
                                State.Document.Name, uint32_t(ImGui::GetFrameCount()));
            State.PickedLayer = -1;
            State.Touch();
        }
        ImGui::SetCursorScreenPos(ImVec2(Buttons.x + Measure(13.0f, "Random tyre").x + 34.0f, Buttons.y));
        if (Button("New name", false))
        {
            State.Document.Name = RandomTyreName(uint32_t(ImGui::GetFrameCount()) * 7919u);
        }
        ImGui::SetCursorScreenPos(ImVec2(Buttons.x, Buttons.y + 34.0f));
        Panel.End();
    }

    //----------------------------------------------------------------------------------------------------------
    // ② the tread editor: identity, then the layer sequence
    //----------------------------------------------------------------------------------------------------------
    static int sAddKind = 0;
    {
        Card Panel;
        Panel.Begin("Tread editor", YellowTint, "");
        if (TextField("##pname", "Name", &State.Document.Name)) { State.Touch(); }
        {
            int Picked = 0;
            const std::vector<std::string>& Kinds = TyreKinds();
            for (int I = 0; I < int(Kinds.size()); ++I)
            {
                if (Kinds[size_t(I)] == State.Document.Kind) { Picked = I; }
            }
            if (Choice("##ptype", "Type", &Picked, Kinds))
            {
                State.Document.Kind = Kinds[size_t(Picked)];
                State.Touch();
            }
        }
        {
            std::vector<std::string> Names;
            Names.reserve(TreadLayerCatalogue().size());
            for (const TreadLayerCatalogueEntry& Entry : TreadLayerCatalogue())
            {
                Names.emplace_back(Entry.Label);
            }
            const ImVec2 Row = ImGui::GetCursorScreenPos();
            const float  W   = ImGui::GetContentRegionAvail().x;
            ImGui::PushClipRect(Row, ImVec2(Row.x + W - 58.0f, Row.y + RowHeight), true);
            Choice("##addlayer", "Add layer", &sAddKind, Names);
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(Row.x + W - 54.0f, Row.y));
            if (Button("Add", false, 54.0f, 26.0f))
            {
                P.Layers.push_back(TreadLayerCatalogue()[size_t(sAddKind)].Default);
                State.PickedLayer = int(P.Layers.size()) - 1;
                State.Touch();
            }
            ImGui::SetCursorScreenPos(ImVec2(Row.x, Row.y + RowHeight + RowGap));
        }

        const float W = ImGui::GetContentRegionAvail().x;
        int Duplicate = -1;
        int MoveUp    = -1;
        int MoveDown  = -1;
        int Remove    = -1;
        for (int I = 0; I < int(P.Layers.size()); ++I)
        {
            const TreadLayerSpecification& Layer = P.Layers[size_t(I)];
            const TreadLayerCatalogueEntry& Entry = DescribeLayerKind(Layer.Kind);
            const ImVec2 Row = ImGui::GetCursorScreenPos();

            ImGui::PushID(300 + I);
            ImGui::InvisibleButton("##layer", ImVec2(W - 92.0f, 32.0f));
            if (ImGui::IsItemClicked())
            {
                State.PickedLayer = I;
            }
            const bool Picked = (State.PickedLayer == I);
            Draw->AddRectFilled(Row, ImVec2(Row.x + W, Row.y + 32.0f), CardRaisedTint, 12.0f);
            if (Picked)
            {
                Draw->AddRect(Row, ImVec2(Row.x + W, Row.y + 32.0f), WhiteTint, 13.0f, 0, 1.0f);
            }
            const ImU32 Chip = IM_COL32((Entry.Colour >> 16) & 0xFFu, (Entry.Colour >> 8) & 0xFFu,
                                        Entry.Colour & 0xFFu, 255);
            Draw->AddRectFilled(ImVec2(Row.x + 13.0f, Row.y + 9.0f), ImVec2(Row.x + 15.0f, Row.y + 23.0f), Chip, 1.5f);

            Draw->PushClipRect(Row, ImVec2(Row.x + W - 92.0f, Row.y + 32.0f), true);
            Ink(Draw, 13.0f, ImVec2(Row.x + 23.0f, Row.y + 8.0f), TextTint, Entry.Label);
            const std::string Summary = SummariseLayer(Layer);
            Ink(Draw, 11.0f, ImVec2(Row.x + 27.0f + Measure(13.0f, Entry.Label).x, Row.y + 9.0f),
                MutedTint, Summary.c_str());
            Draw->PopClipRect();

            // the four row actions, as glyph-free vector marks
            const char* Marks[4] = { "copy", "up", "down", "cross" };
            for (int B = 0; B < 4; ++B)
            {
                const float BX = Row.x + W - 88.0f + float(B) * 22.0f;
                ImGui::SetCursorScreenPos(ImVec2(BX, Row.y + 6.0f));
                ImGui::PushID(B);
                ImGui::InvisibleButton("##act", ImVec2(20.0f, 20.0f));
                const bool Hit = ImGui::IsItemClicked();
                const bool Over = ImGui::IsItemHovered();
                ImGui::PopID();
                if (Hit)
                {
                    if (B == 0) { Duplicate = I; }
                    if (B == 1) { MoveUp = I; }
                    if (B == 2) { MoveDown = I; }
                    if (B == 3) { Remove = I; }
                }
                if (Over)
                {
                    Draw->AddRectFilled(ImVec2(BX, Row.y + 6.0f), ImVec2(BX + 20.0f, Row.y + 26.0f), CardHoverTint, 6.0f);
                }
                const ImU32 Mark = Over ? WhiteTint : MutedTint;
                const float CX = BX + 10.0f;
                const float CY = Row.y + 16.0f;
                if (std::strcmp(Marks[B], "copy") == 0)
                {
                    Draw->AddRect(ImVec2(CX - 5.0f, CY - 5.0f), ImVec2(CX + 2.0f, CY + 2.0f), Mark, 1.0f, 0, 1.2f);
                    Draw->AddRect(ImVec2(CX - 2.0f, CY - 2.0f), ImVec2(CX + 5.0f, CY + 5.0f), Mark, 1.0f, 0, 1.2f);
                }
                else if (std::strcmp(Marks[B], "up") == 0)
                {
                    Draw->AddLine(ImVec2(CX, CY + 5.0f), ImVec2(CX, CY - 5.0f), Mark, 1.2f);
                    Draw->AddLine(ImVec2(CX - 4.0f, CY - 1.0f), ImVec2(CX, CY - 5.0f), Mark, 1.2f);
                    Draw->AddLine(ImVec2(CX + 4.0f, CY - 1.0f), ImVec2(CX, CY - 5.0f), Mark, 1.2f);
                }
                else if (std::strcmp(Marks[B], "down") == 0)
                {
                    Draw->AddLine(ImVec2(CX, CY - 5.0f), ImVec2(CX, CY + 5.0f), Mark, 1.2f);
                    Draw->AddLine(ImVec2(CX - 4.0f, CY + 1.0f), ImVec2(CX, CY + 5.0f), Mark, 1.2f);
                    Draw->AddLine(ImVec2(CX + 4.0f, CY + 1.0f), ImVec2(CX, CY + 5.0f), Mark, 1.2f);
                }
                else
                {
                    Draw->AddLine(ImVec2(CX - 4.0f, CY - 4.0f), ImVec2(CX + 4.0f, CY + 4.0f), Mark, 1.2f);
                    Draw->AddLine(ImVec2(CX + 4.0f, CY - 4.0f), ImVec2(CX - 4.0f, CY + 4.0f), Mark, 1.2f);
                }
            }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(Row.x, Row.y + 36.0f));
        }

        if (Duplicate >= 0)
        {
            P.Layers.insert(P.Layers.begin() + Duplicate + 1, P.Layers[size_t(Duplicate)]);
            State.PickedLayer = Duplicate + 1;
            State.Touch();
        }
        if (MoveUp > 0)
        {
            std::swap(P.Layers[size_t(MoveUp)], P.Layers[size_t(MoveUp - 1)]);
            State.PickedLayer = MoveUp - 1;
            State.Touch();
        }
        if (MoveDown >= 0 && MoveDown + 1 < int(P.Layers.size()))
        {
            std::swap(P.Layers[size_t(MoveDown)], P.Layers[size_t(MoveDown + 1)]);
            State.PickedLayer = MoveDown + 1;
            State.Touch();
        }
        if (Remove >= 0)
        {
            P.Layers.erase(P.Layers.begin() + Remove);
            State.PickedLayer = -1;
            State.Touch();
        }
        Panel.End();
    }

    //----------------------------------------------------------------------------------------------------------
    // ③ the picked layer's properties, generated from the catalogue
    //----------------------------------------------------------------------------------------------------------
    {
        Card Panel;
        Panel.Begin("Layer properties", RedTint, "");
        if (State.PickedLayer < 0 || State.PickedLayer >= int(P.Layers.size()))
        {
            Hint("Select a layer to edit it. Layers are drawn in order; deeper cuts win. "
                 "Across-position -1 to 1 is the tread edges, ±1.3 reaches the shoulder.");
        }
        else
        {
            TreadLayerSpecification& Layer = P.Layers[size_t(State.PickedLayer)];
            const TreadLayerCatalogueEntry& Entry = DescribeLayerKind(Layer.Kind);
            const ImVec2 At = ImGui::GetCursorScreenPos();
            Ink(Draw, 13.0f, At, IM_COL32((Entry.Colour >> 16) & 0xFFu, (Entry.Colour >> 8) & 0xFFu,
                                          Entry.Colour & 0xFFu, 255), Entry.Label);
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));

            const std::vector<TreadLayerFieldDescriptor> Fields = DescribeLayerFields(Layer.Kind);
            for (size_t I = 0; I < Fields.size(); ++I)
            {
                const TreadLayerFieldDescriptor& Descriptor = Fields[I];
                char Id[24];
                std::snprintf(Id, sizeof(Id), "##field%zu", I);
                if (Descriptor.Flag)
                {
                    bool On = ReadLayerField(Layer, Descriptor.Field) != 0.0f;
                    if (Check(Id, Descriptor.Label, &On))
                    {
                        WriteLayerField(Layer, Descriptor.Field, On ? 1.0f : 0.0f);
                        State.Touch();
                    }
                }
                else
                {
                    float Value = ReadLayerField(Layer, Descriptor.Field);
                    const int Decimals = (Descriptor.Step < 1.0f) ? 2 : 0;
                    if (Slider(Id, Descriptor.Label, &Value, Descriptor.Minimum, Descriptor.Maximum,
                               Descriptor.Step, Decimals))
                    {
                        WriteLayerField(Layer, Descriptor.Field, Value);
                        State.Touch();
                    }
                }
            }

            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 6.0f));
            if (Button("Mirror layer across centre", false))
            {
                P.Layers.insert(P.Layers.begin() + State.PickedLayer + 1, MirrorLayer(Layer));
                ++State.PickedLayer;
                State.Touch();
            }
        }
        Panel.End();
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE WINDOW
//------------------------------------------------------------------------------------------------------------------------

void RecordTyreGeneratorWindow(ControlPanel& Controls, TyreGeneratorState& State, bool* Open)
{
    gFaces.Ui    = Controls.QueryUi()    != nullptr ? Controls.QueryUi()    : ImGui::GetFont();
    gFaces.Small = Controls.QuerySmall() != nullptr ? Controls.QuerySmall() : gFaces.Ui;
    gFaces.Mono  = Controls.QueryMono()  != nullptr ? Controls.QueryMono()  : gFaces.Ui;

    ImGui::SetNextWindowSize(ImVec2(1520.0f, 850.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(900.0f, 520.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, BackdropTint);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (!ImGui::Begin("Tyre Generator", Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        return;
    }

    RefreshRaster(State);
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    //----------------------------------------------------------------------------------------------------------
    // ① the header: the wheel mark, then the four page tabs
    //----------------------------------------------------------------------------------------------------------
    {
        const ImVec2 At = ImGui::GetCursorScreenPos();
        const ImVec2 Centre(At.x + 23.0f, At.y + 30.0f);
        for (int I = 0; I < 8; ++I)
        {
            const float A0 = float(I) * π * 0.25f + π * 7.0f / 6.0f;
            const float A1 = A0 + π * 0.25f * 0.5f;
            Draw->PathArcTo(Centre, 14.0f, A0, A1, 6);
            Draw->PathArcTo(Centre, 9.0f, A1, A0, 6);
            Draw->PathFillConvex(IM_COL32(0xE6, 0xE7, 0xEA, 230));
        }

        const char* Tabs[4] = { "Tyre", "Rim", "Look", "Export" };
        float X = At.x + 52.0f;
        for (int I = 0; I < 4; ++I)
        {
            const float W = Measure(13.0f, Tabs[I]).x + 40.0f;
            ImGui::SetCursorScreenPos(ImVec2(X, At.y + 12.0f));
            ImGui::PushID(400 + I);
            ImGui::InvisibleButton("##tab", ImVec2(W, 36.0f));
            if (ImGui::IsItemClicked())
            {
                State.Tab = TyreGeneratorTab(I);
            }
            const bool Over = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool On = int(State.Tab) == I;
            if (On)
            {
                Draw->AddRectFilled(ImVec2(X, At.y + 12.0f), ImVec2(X + W, At.y + 48.0f), CardRaisedTint, 18.0f);
            }
            const ImVec2 Size = Measure(13.0f, Tabs[I]);
            Ink(Draw, 13.0f, ImVec2(X + (W - Size.x) * 0.5f, At.y + 22.0f),
                On ? TextTint : (Over ? TextTint : MutedTint), Tabs[I]);
            X += W + 4.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(At.x, At.y + 60.0f));
    }

    //----------------------------------------------------------------------------------------------------------
    // ② the three columns
    //----------------------------------------------------------------------------------------------------------
    const float BodyHeight = ImGui::GetContentRegionAvail().y;
    const float TotalWidth = ImGui::GetContentRegionAvail().x;
    const float CentreWidth = std::max(240.0f, TotalWidth - ControlColumnWidth - PatternColumnWidth - 2.0f * ColumnGutter);

    ImGui::BeginChild("left", ImVec2(ControlColumnWidth, BodyHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    switch (State.Tab)
    {
        case TyreGeneratorTab::Tyre:   RecordTyrePage(State);   break;
        case TyreGeneratorTab::Rim:    RecordRimPage(State);    break;
        case TyreGeneratorTab::Look:   RecordLookPage(State);   break;
        case TyreGeneratorTab::Export: RecordExportPage(State); break;
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, ColumnGutter);
    {
        const ImVec2 At = ImGui::GetCursorScreenPos();
        ImGui::BeginChild("viewport", ImVec2(CentreWidth, BodyHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        RecordViewport(State, At, ImVec2(At.x + CentreWidth, At.y + BodyHeight));
        ImGui::SetCursorScreenPos(At);
        ImGui::Dummy(ImVec2(CentreWidth, BodyHeight));
        ImGui::EndChild();
    }

    ImGui::SameLine(0.0f, ColumnGutter);
    ImGui::BeginChild("right", ImVec2(PatternColumnWidth, BodyHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    RecordPatternColumn(State);
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

}   // namespace Frontier
