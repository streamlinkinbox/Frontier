/*==========================================================================================================================================
                                                        MATERIALLIBRARYPANEL.CPP
==========================================================================================================================================*/
// 🧩 Native material discovery, selection, edit/apply/revert and explicit disk operations; no simulated renderer thumbnails.

#include "MaterialLibraryPanel.h"
#include "PanelChrome.h"
#include "WorkspaceViewportDrawList.h"
#include "Engine/ContentInterchange/MaterialRecipeProfiles.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <tuple>
#include <imgui.h>
#include <imgui_internal.h>
#include <set>
#include <optional>

namespace Frontier
{
// Shared panel UI primitives (PanelChrome.h) - one implementation for all five workspaces. This
// panel's ConstructMaterialCaption/Pill stay local: their text metrics (current-font CalcTextSize,
// baseline offset, style-alpha ink) deliberately differ from the other panels' Caption grid.
using PanelChrome::Ink255;
using PanelChrome::SpectrumQuad;
using PanelChrome::ThemeQuad;
namespace
{
// 📝 Rounded, padded tooltip card shared by every material hover hint.
struct MaterialTooltipScope
{
    explicit MaterialTooltipScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 10 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * Scale, 5 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.098f, .098f, .106f, .97f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320 * Scale);
    }
    ~MaterialTooltipScope()
    {
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }
};
// 📝 Rounded, padded modal chrome shared by every blocking dialog (library switch, unsaved-edit
// prompt, create/duplicate destination). ConstructInterface zeroes WindowRounding/WindowPadding for
// the whole docked workspace, so these overlays need their own scope or they inherit flush, square
// edges instead of looking like deliberate dialogs.
struct MaterialModalScope
{
    explicit MaterialModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~MaterialModalScope()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};
void PresentMaterialTooltip(float Scale, const char *Title, const char *Body)
{
    MaterialTooltipScope Scope(Scale);
    if (Title && Title[0]) ImGui::TextUnformatted(Title);
    if (Body && Body[0]) ImGui::TextDisabled("%s", Body);
}
void ConstructMaterialCaption(float X, float Y, float Size, uint32_t Rgb, const char *Caption, ImFont *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? Typeface : ImGui::GetFont(), Size * 1.302f, ImVec2(X, Y - Size * .15f), Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha)), Caption);
}
bool ConstructMaterialPill(const char *Caption, ImVec2 Position, ImVec2 Extent, bool Selected = false)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y),
        Ink255(Selected ? 0x282825 : ImGui::IsItemHovered() ? 0x2e2e2b : 0x181818), Extent.y * .5f);
    Ink->AddRect(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y), Ink255(0xffffff, Selected ? 24 : 8), Extent.y * .5f);
    const char *CaptionEnd = ImGui::FindRenderedTextEnd(Caption);
    const auto CaptionExtent = ImGui::CalcTextSize(Caption, CaptionEnd);
    Ink->AddText(ImVec2(Position.x + (Extent.x - CaptionExtent.x) * .5f, Position.y + (Extent.y - CaptionExtent.y) * .5f),
        Ink255(Selected ? 0xcccdbb : 0x858577), Caption, CaptionEnd);
    return Pressed;
}
float *QueryPaintCoefficient(AutomotivePaint &Paint, std::string_view Key)
{
    if (Key == "flakes") return &Paint.Distribution[0];
    if (Key == "flakeSize") return &Paint.Distribution[1];
    if (Key == "flakeScale") return &Paint.Distribution[2];
    if (Key == "depth") return &Paint.Distribution[3];
    if (Key == "flakeRoughnessMin") return &Paint.Finish[0];
    if (Key == "flakeRoughnessMax") return &Paint.Finish[1];
    if (Key == "flakeMetalnessMin") return &Paint.Finish[2];
    if (Key == "flakeMetalnessMax") return &Paint.Finish[3];
    if (Key == "flakeTilt") return &Paint.Relief[0];
    if (Key == "flakeLayerDepth") return &Paint.Relief[1];
    if (Key == "orangePeel") return &Paint.Relief[2];
    if (Key == "orangePeelScale") return &Paint.Relief[3];
    return nullptr;
}
std::optional<MaterialPoolLocation> QueryRecipePoolLocation(MaterialDefinition &Definition, std::string_view Key)
{
    // 📝 Shared-pool-first: procedural params shadow stratum scalars, so one gesture drives both the recipe
    // sliders and the rebaked channels. Direction aliases collapse onto the stored angle.
    if (auto Pool = QueryProceduralPool(Definition, Key)) return Pool;
    for (const auto &Op : QueryProceduralOps())
        if (const auto Canonical = ResolveProceduralParamKey(Op, Key); Canonical != Key)
            if (auto Pool = QueryProceduralPool(Definition, Canonical)) return Pool;
    return {};
}

std::optional<float> QueryRecipeCoefficient(MaterialDefinition &Definition, std::string_view Key)
{
    if (const auto Pool = QueryRecipePoolLocation(Definition, Key)) return float(Pool->Value);
    const uint32_t Index = Key.starts_with("coat") ? 1 : 0;
    if (Index >= Definition.Scattering.Illumination.Configuration[0]) return {};
    auto &Stratum = Definition.Scattering.Illumination.Strata[Index];
    if (Key == "roughness" || Key == "coatRoughness") return Stratum.AlbedoRoughness[3];
    if (Key == "metalness") return Stratum.Optics[0];
    if (Key == "ior" || Key == "coatIor") return Stratum.Optics[1];
    if (Key == "coat") return Stratum.Optics[2];
    if (Key == "anisotropy") return Definition.Scattering.DirectionalOptics[Index][0];
    if (Key == "specular") return Definition.Scattering.DirectionalOptics[Index][3];
    if (Key == "heightStrength") return Definition.Scattering.StratumNormal[Index][1];
    if (Key == "normalStrength") return Definition.Scattering.StratumNormal[Index][0];
    if (Definition.Scattering.Paint.Configuration[3])
    {
        if (Key == "flakeLayers") return float(Definition.Scattering.Paint.Configuration[1]);
        if (const auto *Coefficient = QueryPaintCoefficient(Definition.Scattering.Paint, Key)) return *Coefficient;
    }
    else if (Key == "depth" && Definition.Scattering.Illumination.Configuration[0] > 1 &&
             Definition.Scattering.Illumination.Strata[1].Classification[0] == 1)
    {
        return Definition.Scattering.Illumination.Strata[1].AbsorptionThickness[3];
    }
    // 📝 Emission strength is the peak of the authored Radiance spectrum (LED recipes).
    if (Key == "emissionStrength")
    {
        const auto &Radiance = Definition.Scattering.RadianceNormal;
        return std::max({Radiance[0], Radiance[1], Radiance[2]});
    }
    return {};
}
const MaterialCatalogueEntry *QuerySelectedMaterial(const MaterialLibrary &Library)
{
    for (const auto &Entry : Library.QueryEntries())
        if (Entry.RelativePath == Library.QuerySelection()) return &Entry;
    return nullptr;
}
bool ConstructMaterialSelection(const char *Caption, const char *Preview, const char *const *Options,
    uint32_t Count, int &Selection, float Scale)
{
    bool Changed = false;
    ImGui::PushID(Caption);
    if (DropdownControlSurface::BeginSelection("##MaterialSelection", Preview, Scale))
    {
        for (uint32_t Index = 0; Index < Count; ++Index)
            if (DropdownControlSurface::Option(Options[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
        DropdownControlSurface::EndSelection();
    }
    ImGui::PopID();
    return Changed;
}
bool ConstructMaterialToggle(const char *Caption, bool &Enabled, float Scale)
{
    // 📝 HTML .scratch-switch: 12px label left, 32×18 accent switch (#b3c2a7) pinned right.
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const ImVec2 Extent(Width, 18 * Scale);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    if (Pressed) Enabled = !Enabled;
    const auto Minimum = ImVec2(Origin.x + Width - 32 * Scale, Origin.y);
    const auto Maximum = ImVec2(Minimum.x + 32 * Scale, Minimum.y + 18 * Scale);
    auto *Ink = ImGui::GetWindowDrawList();
    ConstructMaterialCaption(Origin.x, Origin.y + 2 * Scale, 12 * Scale, 0xd8d8ce, Caption);
    Ink->AddRectFilled(Minimum, Maximum, Ink255(Enabled ? 0xb3c2a7 : 0x383a38), 9 * Scale);
    const float Centre = Enabled ? Maximum.x - 9 * Scale : Minimum.x + 9 * Scale;
    Ink->AddCircleFilled(ImVec2(Centre, (Minimum.y + Maximum.y) * .5f), 7 * Scale,
        Enabled ? Ink255(0x1f241e) : Ink255(0xcfcfc4));
    return Pressed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR (HTML) PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------
// 📝 Pixel-matched building blocks for the Alloy material inspector port: colour maths, wrapped text
// with CSS leading, line glyphs for the code points DM Sans lacks, and the .mw-panel widget chrome.

float SrgbToLinearChannel(float Value)
{
    return Value <= .04045f ? Value / 12.92f : std::pow((Value + .055f) / 1.055f, 2.4f);
}
float LinearToSrgbChannel(float Value)
{
    Value = std::max(Value, 0.f);
    return Value <= .0031308f ? 12.92f * Value : 1.055f * std::pow(Value, 1.f / 2.4f) - .055f;
}
bool ParseHexColour(std::string_view Text, std::array<float, 3> &Srgb)
{
    if (Text.size() != 7 || Text[0] != '#') return false;
    for (size_t Channel = 0; Channel < 3; ++Channel)
    {
        unsigned Value = 0;
        for (size_t Digit = 0; Digit < 2; ++Digit)
        {
            const char Letter = Text[1 + Channel * 2 + Digit];
            Value <<= 4;
            if (Letter >= '0' && Letter <= '9') Value |= unsigned(Letter - '0');
            else if (Letter >= 'a' && Letter <= 'f') Value |= unsigned(Letter - 'a' + 10);
            else if (Letter >= 'A' && Letter <= 'F') Value |= unsigned(Letter - 'A' + 10);
            else return false;
        }
        Srgb[Channel] = float(Value) / 255.f;
    }
    return true;
}
std::string EncodeHexColour(const std::array<float, 3> &Srgb)
{
    char Text[8];
    std::snprintf(Text, sizeof Text, "#%02x%02x%02x",
        unsigned(std::lround(std::clamp(Srgb[0], 0.f, 1.f) * 255.f)),
        unsigned(std::lround(std::clamp(Srgb[1], 0.f, 1.f) * 255.f)),
        unsigned(std::lround(std::clamp(Srgb[2], 0.f, 1.f) * 255.f)));
    return Text;
}
ImU32 HexColourInk(std::string_view Hex, unsigned Alpha = 255)
{
    std::array<float, 3> Srgb{1, 1, 1};
    (void)ParseHexColour(Hex, Srgb);
    return IM_COL32(unsigned(std::lround(Srgb[0] * 255.f)), unsigned(std::lround(Srgb[1] * 255.f)),
        unsigned(std::lround(Srgb[2] * 255.f)), Alpha);
}
std::string CaptionFromKey(std::string_view Key)
{
    // 📝 JS Caption(): camelCase → spaced words, first letter upper-cased.
    std::string Caption;
    for (size_t Index = 0; Index < Key.size(); ++Index)
    {
        const char Letter = Key[Index];
        if (Index && Letter >= 'A' && Letter <= 'Z' && Key[Index - 1] >= 'a' && Key[Index - 1] <= 'z') Caption += ' ';
        Caption += Letter;
    }
    if (!Caption.empty() && Caption[0] >= 'a' && Caption[0] <= 'z') Caption[0] = char(Caption[0] - 'a' + 'A');
    return Caption;
}
std::string TrimQuantity(double Value)
{
    char Text[48];
    std::snprintf(Text, sizeof Text, "%.6g", Value);
    return Text;
}

// 📝 Word-wrapped block with CSS line-height; returns the block height in screen pixels.
float ConstructInspectorTextBlock(ImVec2 Position, float Width, float Size, float LineHeight, uint32_t Rgb,
    const char *Text, float Scale, ImFont *Typeface = nullptr)
{
    ImFont *Font = Typeface ? Typeface : ImGui::GetFont();
    const float FontSize = Size * 1.302f * Scale;
    const float Step = Size * LineHeight * Scale;
    auto *Ink = ImGui::GetWindowDrawList();
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    std::string Remaining(Text ? Text : "");
    float Y = Position.y;
    size_t Cursor = 0;
    while (Cursor < Remaining.size())
    {
        while (Cursor < Remaining.size() && Remaining[Cursor] == ' ') ++Cursor;
        if (Cursor >= Remaining.size()) break;
        size_t LineEnd = Cursor, Probe = Cursor;
        while (Probe < Remaining.size())
        {
            const size_t WordEnd = Remaining.find(' ', Probe);
            const size_t End = WordEnd == std::string::npos ? Remaining.size() : WordEnd;
            const float Trial = Font->CalcTextSizeA(FontSize, FLT_MAX, 0,
                Remaining.c_str() + Cursor, Remaining.c_str() + End).x;
            if (Trial > Width && LineEnd > Cursor) break;
            LineEnd = End;
            if (WordEnd == std::string::npos) { Probe = Remaining.size(); break; }
            Probe = WordEnd + 1;
        }
        Ink->AddText(Font, FontSize, ImVec2(Position.x, Y - Size * .15f * Scale), Colour,
            Remaining.c_str() + Cursor, Remaining.c_str() + LineEnd);
        Y += Step;
        Cursor = LineEnd;
    }
    return Y - Position.y;
}
float MeasureInspectorTextBlock(float Width, float Size, float LineHeight, const char *Text, float Scale,
    ImFont *Typeface = nullptr)
{
    ImFont *Font = Typeface ? Typeface : ImGui::GetFont();
    const float FontSize = Size * 1.302f * Scale;
    std::string Remaining(Text ? Text : "");
    size_t Cursor = 0;
    float Lines = 0;
    while (Cursor < Remaining.size())
    {
        while (Cursor < Remaining.size() && Remaining[Cursor] == ' ') ++Cursor;
        if (Cursor >= Remaining.size()) break;
        size_t LineEnd = Cursor, Probe = Cursor;
        while (Probe < Remaining.size())
        {
            const size_t WordEnd = Remaining.find(' ', Probe);
            const size_t End = WordEnd == std::string::npos ? Remaining.size() : WordEnd;
            const float Trial = Font->CalcTextSizeA(FontSize, FLT_MAX, 0,
                Remaining.c_str() + Cursor, Remaining.c_str() + End).x;
            if (Trial > Width && LineEnd > Cursor) break;
            LineEnd = End;
            if (WordEnd == std::string::npos) { Probe = Remaining.size(); break; }
            Probe = WordEnd + 1;
        }
        ++Lines;
        Cursor = LineEnd;
    }
    return Lines * Size * LineHeight * Scale;
}
float InspectorTextWidth(float Size, const char *Text, float Scale, ImFont *Typeface = nullptr)
{
    ImFont *Font = Typeface ? Typeface : ImGui::GetFont();
    return Font->CalcTextSizeA(Size * 1.302f * Scale, FLT_MAX, 0, Text).x;
}

void DrawInspectorXGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    Ink->AddLine(ImVec2(Centre.x - Half, Centre.y - Half), ImVec2(Centre.x + Half, Centre.y + Half), Colour, 1.4f * Scale);
    Ink->AddLine(ImVec2(Centre.x - Half, Centre.y + Half), ImVec2(Centre.x + Half, Centre.y - Half), Colour, 1.4f * Scale);
}
void DrawInspectorPlusGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    Ink->AddLine(ImVec2(Centre.x - Half, Centre.y), ImVec2(Centre.x + Half, Centre.y), Colour, 1.4f * Scale);
    Ink->AddLine(ImVec2(Centre.x, Centre.y - Half), ImVec2(Centre.x, Centre.y + Half), Colour, 1.4f * Scale);
}
void DrawInspectorDiamondGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    const std::array<ImVec2, 4> Points{ImVec2(Centre.x, Centre.y - Half), ImVec2(Centre.x + Half, Centre.y),
        ImVec2(Centre.x, Centre.y + Half), ImVec2(Centre.x - Half, Centre.y)};
    Ink->AddPolyline(Points.data(), 4, Colour, ImDrawFlags_Closed, 1.3f * Scale);
}
void DrawInspectorChevronGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale, bool PointDown)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale, Drop = Size * .28f * Scale * (PointDown ? 1.f : -1.f);
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    Ink->AddLine(ImVec2(Centre.x - Half, Centre.y - Drop), ImVec2(Centre.x, Centre.y + Drop), Colour, 1.4f * Scale);
    Ink->AddLine(ImVec2(Centre.x, Centre.y + Drop), ImVec2(Centre.x + Half, Centre.y - Drop), Colour, 1.4f * Scale);
}
void DrawInspectorArrowGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale, bool Up)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale, Dir = Up ? -1.f : 1.f;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    Ink->AddLine(ImVec2(Centre.x, Centre.y - Dir * Half), ImVec2(Centre.x, Centre.y + Dir * Half), Colour, 1.4f * Scale);
    Ink->AddLine(ImVec2(Centre.x, Centre.y + Dir * Half), ImVec2(Centre.x - Half * .62f, Centre.y + Dir * Half * .28f), Colour, 1.4f * Scale);
    Ink->AddLine(ImVec2(Centre.x, Centre.y + Dir * Half), ImVec2(Centre.x + Half * .62f, Centre.y + Dir * Half * .28f), Colour, 1.4f * Scale);
}
void DrawInspectorRightArrowGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float Half = Size * .5f * Scale;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    Ink->AddLine(ImVec2(Centre.x - Half, Centre.y), ImVec2(Centre.x + Half, Centre.y), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(Centre.x + Half, Centre.y), ImVec2(Centre.x + Half * .3f, Centre.y - Half * .66f), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(Centre.x + Half, Centre.y), ImVec2(Centre.x + Half * .3f, Centre.y + Half * .66f), Colour, 1.3f * Scale);
}
void DrawInspectorTrashGlyph(ImVec2 Centre, float Size, uint32_t Rgb, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    const float W = Size * Scale, H = Size * Scale;
    const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
    const float X0 = Centre.x - W * .38f, X1 = Centre.x + W * .38f, Y0 = Centre.y - H * .3f, Y1 = Centre.y + H * .46f;
    Ink->AddLine(ImVec2(Centre.x - W * .5f, Y0), ImVec2(Centre.x + W * .5f, Y0), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(Centre.x - W * .18f, Y0), ImVec2(Centre.x - W * .18f, Centre.y - H * .48f), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(Centre.x - W * .18f, Centre.y - H * .48f), ImVec2(Centre.x + W * .18f, Centre.y - H * .48f), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(Centre.x + W * .18f, Centre.y - H * .48f), ImVec2(Centre.x + W * .18f, Y0), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(X0, Y0), ImVec2(X0 + W * .07f, Y1), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(X0 + W * .07f, Y1), ImVec2(X1 - W * .07f, Y1), Colour, 1.3f * Scale);
    Ink->AddLine(ImVec2(X1 - W * .07f, Y1), ImVec2(X1, Y0), Colour, 1.3f * Scale);
}
void DrawInspectorSpectrumDot(ImVec2 Centre, float Scale)
{
    // 📝 CSS conic-gradient(#b7b9bd, #a6a8ab, #a08bc4, #789eca, #b7b9bd) at 10px: four soft quadrants.
    auto *Ink = ImGui::GetWindowDrawList();
    const float Radius = 5 * Scale;
    static constexpr std::array<uint32_t, 4> Stops{0xb7b9bd, 0xa08bc4, 0x789eca, 0xa6a8ab};
    for (size_t Quadrant = 0; Quadrant < 4; ++Quadrant)
    {
        Ink->PathClear();
        Ink->PathLineTo(Centre);
        Ink->PathArcTo(Centre, Radius, float(Quadrant) * 1.5707963f - 1.5707963f, float(Quadrant + 1) * 1.5707963f - 1.5707963f, 8);
        Ink->PathFillConvex(Ink255(Stops[Quadrant], unsigned(255 * ImGui::GetStyle().Alpha)));
    }
    Ink->AddCircle(Centre, Radius + 1.5f * Scale, Ink255(0xffffff, 8), 24, 2 * Scale);
}

// 📝 Horizontal multi-stop gradient (CSS linear-gradient 90deg) with optional corner radius.
void DrawInspectorGradient(ImVec2 Minimum, ImVec2 Maximum, const std::vector<uint32_t> &Stops, float Radius,
    float Scale, unsigned Alpha = 255, uint32_t BackdropRgb = 0x121212)
{
    if (Stops.empty()) return;
    auto *Ink = ImGui::GetWindowDrawList();
    const unsigned Faded = unsigned(Alpha * ImGui::GetStyle().Alpha);
    if (Stops.size() == 1)
    {
        Ink->AddRectFilled(Minimum, Maximum, Ink255(Stops[0], Faded), Radius * Scale);
        return;
    }
    constexpr int Bands = 32;
    Ink->PushClipRect(Minimum, Maximum, true);
    for (int Band = 0; Band < Bands; ++Band)
    {
        const float T = (float(Band) + .5f) / float(Bands);
        const float Span = T * float(Stops.size() - 1);
        const size_t Index = std::min(size_t(Span), Stops.size() - 2);
        const float Local = Span - float(Index);
        const auto Mix = [&](size_t Shift)
        {
            const float A = float((Stops[Index] >> Shift) & 0xff), B = float((Stops[Index + 1] >> Shift) & 0xff);
            return unsigned(std::lround(A + (B - A) * Local));
        };
        const float X0 = Minimum.x + (Maximum.x - Minimum.x) * float(Band) / float(Bands);
        const float X1 = Minimum.x + (Maximum.x - Minimum.x) * float(Band + 1) / float(Bands) + 1;
        Ink->AddRectFilled(ImVec2(X0, Minimum.y), ImVec2(X1, Maximum.y), IM_COL32(Mix(16), Mix(8), Mix(0), Faded));
    }
    if (Radius > 0)
    {
        // 📝 Round the silhouette by redrawing the corner outsides in the caller's backdrop tone.
        const ImU32 Backdrop = Ink255(BackdropRgb);
        const float R = Radius * Scale;
        const auto Punch = [&](ImVec2 Corner, ImVec2 Centre, float From, float To, ImVec2 Via)
        {
            Ink->PathClear();
            Ink->PathLineTo(Corner);
            Ink->PathLineTo(Via);
            Ink->PathArcTo(Centre, R, From, To, 6);
            Ink->PathFillConvex(Backdrop);
        };
        Punch(Minimum, ImVec2(Minimum.x + R, Minimum.y + R), 3.14159265f, 4.71238898f,
            ImVec2(Minimum.x, Minimum.y + R));
        Punch(ImVec2(Maximum.x, Minimum.y), ImVec2(Maximum.x - R, Minimum.y + R), 4.71238898f, 6.28318531f,
            ImVec2(Maximum.x - R, Minimum.y));
        Punch(ImVec2(Minimum.x, Maximum.y), ImVec2(Minimum.x + R, Maximum.y - R), 1.57079633f, 3.14159265f,
            ImVec2(Minimum.x + R, Maximum.y));
        Punch(Maximum, ImVec2(Maximum.x - R, Maximum.y - R), 0.f, 1.57079633f,
            ImVec2(Maximum.x, Maximum.y - R));
    }
    Ink->PopClipRect();
}
void DrawInspectorCheckerboard(ImVec2 Minimum, ImVec2 Maximum, float Cell, uint32_t A, uint32_t B, float Scale)
{
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->PushClipRect(Minimum, Maximum, true);
    Ink->AddRectFilled(Minimum, Maximum, Ink255(A));
    const float Step = Cell * Scale;
    bool Row = false;
    for (float Y = Minimum.y; Y < Maximum.y; Y += Step, Row = !Row)
        for (float X = Minimum.x + (Row ? Step : 0); X < Maximum.x; X += Step * 2)
            Ink->AddRectFilled(ImVec2(X, Y), ImVec2(X + Step, Y + Step), Ink255(B));
    Ink->PopClipRect();
}

// 📝 .icon-button hit area + hover wash; the caller paints the glyph at the returned centre.
bool ConstructInspectorIconButton(const char *Caption, ImVec2 Position, ImVec2 Extent, float Scale, ImVec2 &Centre)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    Centre = ImVec2(Position.x + Extent.x * .5f, Position.y + Extent.y * .5f);
    if (ImGui::IsItemHovered())
        ImGui::GetWindowDrawList()->AddRectFilled(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y),
            Ink255(0x34383c, unsigned(255 * ImGui::GetStyle().Alpha)), 5 * Scale);
    return Pressed;
}
// 📝 .studio-themed button under the .mw-panel radius override (r9, #222 face, white 5% border).
bool ConstructInspectorButton(const char *Caption, ImVec2 Position, ImVec2 Extent, float Scale, float FontSize = 10,
    uint32_t TextRgb = 0xcecec2)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    auto *Ink = ImGui::GetWindowDrawList();
    const unsigned Alpha = unsigned(255 * ImGui::GetStyle().Alpha);
    Ink->AddRectFilled(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y),
        Ink255(ImGui::IsItemHovered() ? 0x2b2b2b : 0x222222, Alpha), 9 * Scale);
    Ink->AddRect(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y), Ink255(0xffffff, unsigned(13 * ImGui::GetStyle().Alpha)), 9 * Scale);
    const char *End = ImGui::FindRenderedTextEnd(Caption);
    const float Width = InspectorTextWidth(FontSize, std::string(Caption, End).c_str(), Scale);
    Ink->AddText(ImGui::GetFont(), FontSize * 1.302f * Scale,
        ImVec2(Position.x + (Extent.x - Width) * .5f, Position.y + (Extent.y - FontSize * 1.302f * Scale) * .5f),
        Ink255(TextRgb, Alpha), Caption, End);
    return Pressed;
}
// 📝 .mw-panel checkbox: 12px box, accent #a9bd9c when set.
bool ConstructInspectorCheckboxAt(const char *Caption, ImVec2 Position, bool &Value, float Scale)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, ImVec2(12 * Scale, 12 * Scale));
    if (Pressed) Value = !Value;
    auto *Ink = ImGui::GetWindowDrawList();
    const unsigned Alpha = unsigned(255 * ImGui::GetStyle().Alpha);
    const auto Maximum = ImVec2(Position.x + 12 * Scale, Position.y + 12 * Scale);
    if (Value)
    {
        Ink->AddRectFilled(Position, Maximum, Ink255(0xa9bd9c, Alpha), 2.5f * Scale);
        const ImU32 Tick = Ink255(0x1c201a, Alpha);
        Ink->AddLine(ImVec2(Position.x + 2.6f * Scale, Position.y + 6.2f * Scale),
            ImVec2(Position.x + 5.f * Scale, Position.y + 8.8f * Scale), Tick, 1.6f * Scale);
        Ink->AddLine(ImVec2(Position.x + 5.f * Scale, Position.y + 8.8f * Scale),
            ImVec2(Position.x + 9.4f * Scale, Position.y + 3.2f * Scale), Tick, 1.6f * Scale);
    }
    else
    {
        Ink->AddRectFilled(Position, Maximum, Ink255(0x1a1a1a, Alpha), 2.5f * Scale);
        Ink->AddRect(Position, Maximum, Ink255(0x6a6f6a, Alpha), 2.5f * Scale);
    }
    return Pressed;
}

void PushInspectorInputStyle(float Scale, ImVec2 Padding)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Padding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Ink255(0x161616)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(Ink255(0x1a1a1a)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(Ink255(0x1a1a1a)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 11.f / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Ink255(0xdddddd)));
}
void PopInspectorInputStyle()
{
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
}
bool ConstructInspectorTextInput(const char *Caption, ImVec2 Position, float Width, char *Buffer, size_t Capacity,
    float Scale, ImVec2 Padding = ImVec2(7, 7))
{
    ImGui::SetCursorScreenPos(Position);
    ImGui::SetNextItemWidth(Width);
    PushInspectorInputStyle(Scale, ImVec2(Padding.x * Scale, Padding.y * Scale));
    const bool Changed = ImGui::InputText(Caption, Buffer, Capacity);
    PopInspectorInputStyle();
    return Changed;
}
bool ConstructInspectorNumberInput(const char *Caption, ImVec2 Position, float Width, double &Value, float Scale)
{
    ImGui::SetCursorScreenPos(Position);
    ImGui::SetNextItemWidth(Width);
    PushInspectorInputStyle(Scale, ImVec2(7 * Scale, 7 * Scale));
    const bool Changed = ImGui::InputDouble(Caption, &Value, 0, 0, "%.6g");
    PopInspectorInputStyle();
    return Changed && std::isfinite(Value);
}

struct InspectorSelectOption
{
    std::string Value, Label;
    bool GroupHeader = false;
};
// 📝 HTML <select>: .mw-panel face (#161616, r7, 27px) or the recipe face (#1c2024, r8, 33px).
bool ConstructInspectorSelect(const char *Caption, ImVec2 Position, float Width,
    const std::vector<InspectorSelectOption> &Options, int &Selection, float Scale, bool RecipeStyle = false)
{
    const float Height = (RecipeStyle ? 33.f : 27.f) * Scale;
    bool Changed = false;
    ImGui::PushID(Caption);
    ImGui::SetCursorScreenPos(Position);
    if (ImGui::InvisibleButton("##SelectFace", ImVec2(Width, Height))) ImGui::OpenPopup("##SelectPopup");
    auto *Ink = ImGui::GetWindowDrawList();
    const unsigned Alpha = unsigned(255 * ImGui::GetStyle().Alpha);
    const auto Maximum = ImVec2(Position.x + Width, Position.y + Height);
    Ink->AddRectFilled(Position, Maximum, Ink255(RecipeStyle ? 0x1c2024 : 0x161616, Alpha), (RecipeStyle ? 8.f : 7.f) * Scale);
    Ink->AddRect(Position, Maximum, RecipeStyle ? Ink255(0x3c454d, Alpha) : Ink255(0xffffff, unsigned(11 * ImGui::GetStyle().Alpha)),
        (RecipeStyle ? 8.f : 7.f) * Scale);
    const std::string Preview = Selection >= 0 && Selection < int(Options.size()) ? Options[size_t(Selection)].Label : "";
    Ink->PushClipRect(Position, ImVec2(Maximum.x - 27 * Scale, Maximum.y), true);
    Ink->AddText(ImGui::GetFont(), 10 * 1.302f * Scale,
        ImVec2(Position.x + (RecipeStyle ? 10.f : 7.f) * Scale, Position.y + (Height - 13.02f * Scale) * .5f),
        Ink255(RecipeStyle ? 0xcbd7db : 0xdddddd, Alpha), Preview.c_str());
    Ink->PopClipRect();
    DrawInspectorChevronGlyph(ImVec2(Maximum.x - 15.5f * Scale, Position.y + Height * .5f), 11, RecipeStyle ? 0x98a7af : 0x9aa0a4, Scale, true);
    ImGui::SetNextWindowPos(ImVec2(Position.x, Maximum.y + 2 * Scale));
    ImGui::SetNextWindowSize(ImVec2(Width, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * Scale, 4 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * Scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(Ink255(0x1c1c1a)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 16.f / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(Ink255(0x2c2c28)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(Ink255(0x33332e)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(Ink255(0x33332e)));
    if (ImGui::BeginPopup("##SelectPopup"))
    {
        ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
        for (size_t Index = 0; Index < Options.size(); ++Index)
        {
            const auto &Option = Options[Index];
            if (Option.GroupHeader)
            {
                const auto Row = ImGui::GetCursorScreenPos();
                ConstructMaterialCaption(Row.x + 6 * Scale, Row.y + 5 * Scale, 8 * Scale, 0x8b977f, Option.Label.c_str());
                ImGui::Dummy(ImVec2(Width - 8 * Scale, 19 * Scale));
                continue;
            }
            ImGui::PushID(int(Index));
            if (ImGui::Selectable("##Option", Selection == int(Index), 0, ImVec2(Width - 8 * Scale, 22 * Scale)))
            {
                Selection = int(Index);
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
            const auto RowMin = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 10 * 1.302f * Scale,
                ImVec2(RowMin.x + 6 * Scale, RowMin.y + 4.5f * Scale),
                Ink255(Selection == int(Index) ? 0xe6ead9 : 0xc9c9bd), Option.Label.c_str());
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return Changed;
}

// 📝 Shared sRGB picker popup (swatch click in the HTML opens the OS colour dialog).
bool ConstructInspectorColourPopup(const char *Caption, std::array<float, 3> &Srgb, float Scale)
{
    bool Changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 12 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * Scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(Ink255(0x1e1e1c)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 18.f / 255.f));
    if (ImGui::BeginPopup(Caption))
    {
        float Colour[3]{Srgb[0], Srgb[1], Srgb[2]};
        if (ImGui::ColorPicker3("##InspectorPicker", Colour,
                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview))
        {
            Srgb = {Colour[0], Colour[1], Colour[2]};
            Changed = true;
        }
        static std::array<char, 16> HexBuffer{};
        static ImGuiID HexOwnerId = 0;
        const ImGuiID Owner = ImGui::GetID(Caption);
        if (HexOwnerId != Owner || Changed)
        {
            HexOwnerId = Owner;
            std::snprintf(HexBuffer.data(), HexBuffer.size(), "%s", EncodeHexColour(Srgb).c_str());
        }
        ImGui::SetNextItemWidth(120 * Scale);
        PushInspectorInputStyle(Scale, ImVec2(7 * Scale, 5 * Scale));
        const bool Committed = ImGui::InputText("##InspectorHex", HexBuffer.data(), HexBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        PopInspectorInputStyle();
        if (Committed)
        {
            std::array<float, 3> Parsed{};
            if (ParseHexColour(HexBuffer.data(), Parsed)) { Srgb = Parsed; Changed = true; }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return Changed;
}
bool ConstructInspectorColourSwatch(const char *Caption, ImVec2 Position, ImVec2 Extent, std::array<float, 3> &Srgb,
    float Radius, float Scale)
{
    ImGui::PushID(Caption);
    ImGui::SetCursorScreenPos(Position);
    if (ImGui::InvisibleButton("##Swatch", Extent)) ImGui::OpenPopup("##ColourPopup");
    auto *Ink = ImGui::GetWindowDrawList();
    const auto Maximum = ImVec2(Position.x + Extent.x, Position.y + Extent.y);
    Ink->AddRectFilled(Position, Maximum, HexColourInk(EncodeHexColour(Srgb), unsigned(255 * ImGui::GetStyle().Alpha)), Radius * Scale);
    Ink->AddRect(Position, Maximum, Ink255(0xffffff, unsigned(26 * ImGui::GetStyle().Alpha)), Radius * Scale);
    const bool Changed = ConstructInspectorColourPopup("##ColourPopup", Srgb, Scale);
    ImGui::PopID();
    return Changed;
}

MaterialAuthoringQuantity DefaultChannelQuantity(const MaterialPbrChannel &Channel)
{
    // 📝 JS PbrChannels defaults (metallic/thickness/transmission/sheen family fall back to Minimum).
    if (std::string_view(Channel.Category) == "colour") return std::string("#ffffff");
    if (std::string_view(Channel.Category) == "vector") return std::array<double, 3>{0, 0, 1};
    const std::string_view Identity(Channel.Identity);
    if (Identity == "roughness") return 0.5;
    if (Identity == "ior") return 1.5;
    if (Identity == "specular" || Identity == "coverage") return 1.0;
    return Channel.Minimum;
}

struct InspectorInputSpec
{
    std::string Address, Caption, Category, Unit;
    double Minimum = 0, Maximum = 1, Step = .01;
};
// 📝 Port of MaterialPanel.jsx GeneratorInputs() over the native recipe control list.
std::vector<InspectorInputSpec> BuildGeneratorInputs(const MaterialAuthoringField &Field,
    const std::vector<MaterialRecipeControl> &Controls)
{
    std::vector<InspectorInputSpec> Specs;
    if (Field.Coded)
    {
        static constexpr std::array<const char *, 4> PaletteCaptions{"Sand colour", "Olive colour", "Earth colour", "Shadow colour"};
        for (const auto &Address : Field.Inputs)
        {
            const size_t Split = Address.rfind(':');
            const std::string Key = Split == std::string::npos ? Address : Address.substr(Split + 1);
            if (Key.starts_with("colour") && Key.size() > 6 &&
                std::all_of(Key.begin() + 6, Key.end(), [](char Letter) { return Letter >= '0' && Letter <= '9'; }))
            {
                const int Ordinal = std::atoi(Key.c_str() + 6);
                Specs.push_back({Address,
                    Ordinal < 4 ? PaletteCaptions[size_t(Ordinal)] : "Colour " + std::to_string(Ordinal + 1),
                    "colour", "", 0, 1, .01});
                continue;
            }
            if ((Key == "softness" && Field.Procedure == "camouflage") ||
                (Key == "pixelSize" && Field.Procedure == "camouflage-smooth")) continue;
            double Minimum = 0, Maximum = 1, Step = .01;
            if (Key == "scale") { Minimum = 1; Maximum = 200; Step = 1; }
            else if (Key == "seed") { Minimum = 0; Maximum = 9999; Step = 1; }
            else if (Key == "angle") { Minimum = -180; Maximum = 180; Step = 1; }
            else if (Key == "amount") { Minimum = 0; Maximum = 1; Step = .01; }
            else if (Key == "softness") { Minimum = .001; Maximum = .1; Step = .001; }
            else if (Key == "pixelSize") { Minimum = 1; Maximum = 16; Step = 1; }
            Specs.push_back({Address,
                Key == "amount" && Field.Procedure.starts_with("camouflage") ? "Patch contrast" : CaptionFromKey(Key),
                "scalar", "", Minimum, Maximum, Step});
        }
        return Specs;
    }
    for (const auto &Address : Field.Inputs)
    {
        InspectorInputSpec Spec{Address, CaptionFromKey(Address), "scalar", "", 0, 1, .001};
        const MaterialRecipeControl *Direct = nullptr;
        const MaterialRecipeTarget *Target = nullptr;
        for (const auto &Control : Controls)
        {
            if (Control.Direct && Control.Key == Address) Direct = &Control;
            for (const auto &Entry : Control.Targets)
                if (Entry.Key == Address) Target = &Entry;
        }
        if (Direct)
        {
            Spec.Caption = Direct->Caption.empty() ? Spec.Caption : Direct->Caption;
            Spec.Minimum = Direct->Minimum;
            Spec.Maximum = Direct->Maximum;
            Spec.Step = Direct->Step > 0 ? Direct->Step : (Target && Target->Integer ? 1 : .001);
            Spec.Unit = Direct->Unit;
        }
        else if (Target)
        {
            Spec.Minimum = std::min(Target->Minimum, Target->Maximum);
            Spec.Maximum = std::max(Target->Minimum, Target->Maximum);
            Spec.Step = Target->Integer ? 1 : .001;
        }
        Specs.push_back(std::move(Spec));
    }
    return Specs;
}
const std::array<std::pair<const char *, const char *>, 8> &CodedGeneratorOptions()
{
    static constexpr std::array<std::pair<const char *, const char *>, 8> Options{{
        {"camouflage-smooth", "Military camouflage · Smooth"},
        {"camouflage", "Military camouflage · Pixelated"},
        {"noise", "Fractal noise"},
        {"stripes", "Directional grain"},
        {"checker", "Checker tiles"},
        {"cells", "Cellular / Voronoi"},
        {"ridged", "Ridged fractal"},
        {"rings", "Growth rings"}}};
    return Options;
}
const std::array<std::tuple<const char *, const char *, const char *>, 9> &WeaveOptions()
{
    static constexpr std::array<std::tuple<const char *, const char *, const char *>, 9> Options{{
        {"plain", "Plain · 1 over / 1 under", "The fundamental balanced weave."},
        {"twill", "Twill · 2 over / 2 under", "Continuous diagonal ribs."},
        {"satin", "Satin · 5-harness float", "Long floats for a smooth, lustrous face."},
        {"basket", "Basket · 2 × 2", "Pairs of yarns woven as groups."},
        {"rib", "Rib · warp rib", "Paired floats emphasize crosswise ribs."},
        {"herringbone", "Herringbone · broken twill", "Reversed diagonals form a chevron."},
        {"oxford", "Oxford · 2 × 1 basket", "Paired warp yarns crossing single weft yarns."},
        {"houndstooth", "Houndstooth · color twill", "Contrasting warp and weft bands break into checked teeth."},
        {"denim", "Denim · indigo warp twill", "Warp-faced 3/1 twill with a pale weft underside."}}};
    return Options;
}
const std::array<std::pair<const char *, std::array<const char *, 5>>, 4> &FlakePalettes()
{
    static constexpr std::array<std::pair<const char *, std::array<const char *, 5>>, 4> Palettes{{
        {"Spectrum", {"#c6db89", "#59bea3", "#ab8bc9", "#d17b83", "#82a3d5"}},
        {"Vivid spectrum", {"#ec5667", "#50c38c", "#ae67e5", "#5999ee", "#edc25c"}},
        {"Warm metallic", {"#f0cb8a", "#d99a5d", "#b97046", "#e6bf83", "#e7d6a9"}},
        {"Cool metallic", {"#93bbdc", "#a49cdc", "#6ed0c5", "#c0cedd", "#789cab"}}}};
    return Palettes;
}

std::string EncodeBase64(const std::vector<unsigned char> &Bytes)
{
    static constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Encoded;
    Encoded.reserve((Bytes.size() + 2) / 3 * 4);
    for (size_t Index = 0; Index < Bytes.size(); Index += 3)
    {
        const uint32_t A = Bytes[Index];
        const uint32_t B = Index + 1 < Bytes.size() ? Bytes[Index + 1] : 0;
        const uint32_t C = Index + 2 < Bytes.size() ? Bytes[Index + 2] : 0;
        Encoded += Alphabet[A >> 2];
        Encoded += Alphabet[((A & 3) << 4) | (B >> 4)];
        Encoded += Index + 1 < Bytes.size() ? Alphabet[((B & 15) << 2) | (C >> 6)] : '=';
        Encoded += Index + 2 < Bytes.size() ? Alphabet[C & 63] : '=';
    }
    return Encoded;
}
bool LoadInspectorTextureFile(const std::filesystem::path &Path, MaterialAuthoringTexture &Texture, std::string &Error)
{
    // 📝 Mirrors the HTML import gate + engine TextureValid: PNG only, ≤256px, ≤300 kB data URL.
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File) { Error = "Texture file could not be opened."; return false; }
    const std::streamoff Length = File.tellg();
    if (Length <= 0 || Length > 220000) { Error = "Texture must stay under the 300 kB embedded limit."; return false; }
    std::vector<unsigned char> Bytes;
    Bytes.resize(size_t(Length));
    File.seekg(0);
    if (!File.read(reinterpret_cast<char *>(Bytes.data()), Length)) { Error = "Texture file could not be read."; return false; }
    static constexpr std::array<unsigned char, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (Bytes.size() < 24 || !std::equal(Signature.begin(), Signature.end(), Bytes.begin()) ||
        Bytes[12] != 'I' || Bytes[13] != 'H' || Bytes[14] != 'D' || Bytes[15] != 'R')
    {
        Error = "Only PNG textures can be embedded.";
        return false;
    }
    const auto Extent = [&Bytes](size_t Offset)
    {
        return (uint32_t(Bytes[Offset]) << 24) | (uint32_t(Bytes[Offset + 1]) << 16) |
            (uint32_t(Bytes[Offset + 2]) << 8) | uint32_t(Bytes[Offset + 3]);
    };
    Texture.Width = Extent(16);
    Texture.Height = Extent(20);
    if (!Texture.Width || !Texture.Height || Texture.Width > 256 || Texture.Height > 256)
    {
        Error = "Texture edges must be between 1 and 256 pixels.";
        return false;
    }
    Texture.Name = Path.filename().generic_string();
    Texture.DataUrl = "data:image/png;base64," + EncodeBase64(Bytes);
    Texture.Origin.clear();
    return true;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

MaterialLibraryPanel::MaterialLibraryPanel() : SourceText(1048577, '\0')
{
    if (ImGui::GetCurrentContext()) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Material panels");
    (void)Panels.Register({"MaterialLibrary", "Material library", [this]() { ConstructCatalogue(); }, true, {}, {}});
    (void)Panels.Register({"MaterialViewport", "Viewport", [this]() { ConstructViewport(); }, true, {}, {}});
    (void)Panels.Register({"MaterialInspector", "Inspector", [this]() { ConstructInspector(); }, true, {}, {}});
    Panels.ConfigureWorkspaceInsets(79, 0);
    Panels.ConfigureTabExtent(140, 26);
    (void)Panels.ConfigureColumns({{"MaterialLibrary", .26f}, {"MaterialViewport", .48f}, {"MaterialInspector", .26f}});
    std::snprintf(NameText.data(), NameText.size(), "%s", "Untitled material");
    // 📝 HTML useState defaults: detail + flake-colours sections start folded, the create-generator
    // sheet is pre-filled with "Fractal breakup" / "Surface fields".
    FoldedInspectorCards = {"section:detail", "section:colors"};
    std::snprintf(FieldNameText.data(), FieldNameText.size(), "%s", "Fractal breakup");
    std::snprintf(FieldCategoryText.data(), FieldCategoryText.size(), "%s", "Surface fields");
    std::snprintf(DestinationText.data(), DestinationText.size(), "%s", "Untitled.material");
}

void MaterialLibraryPanel::ConfigureInterfaceScale(float Scale) noexcept
{
    if (std::isfinite(Scale)) InterfaceScale = std::clamp(Scale, .75f, 1.5f);
}
float MaterialLibraryPanel::QueryInterfaceScale() const noexcept
{
    return InterfaceScale * ImGui::GetStyle().FontScaleDpi;
}

bool MaterialLibraryPanel::ConfigureDirectory(const std::filesystem::path &Directory)
{
    if (Library.QueryModified() || Library.QueryParkedEdits() || !Library.ConfigureDirectory(Directory)) return false;
    std::snprintf(DirectoryText.data(), DirectoryText.size(), "%s", EncodeContentPath(Library.QueryDirectory()).c_str());
    SynchronizeSource();
    return true;
}

bool MaterialLibraryPanel::SelectMaterial(const std::filesystem::path &RelativePath)
{
    if (!Library.OpenMaterial(RelativePath)) return false;
    // 📝 Restored parked drafts compile into the live preview; disk still needs Save or Revert.
    if (Library.QueryUnapplied()) (void)Library.ApplySource();
    PendingLivePreviewApply = false;
    ProceduralDirty = false;
    // 📝 A new selection restarts the inspector at the top; layer-stack scroll offsets never leak across materials.
    if (const auto *Context = ImGui::GetCurrentContext())
        for (auto *Candidate : Context->Windows)
        {
            const std::string Name(Candidate->Name);
            const size_t Child = Name.rfind("/Material properties_");
            if (Child != std::string::npos && Name.find('/', Child + 1) == std::string::npos) ImGui::SetScrollY(Candidate, 0);
        }
    SynchronizeSource();
    return true;
}

void MaterialLibraryPanel::ReplaceSource(std::string Source)
{
    Library.ReplaceSource(std::move(Source));
    SynchronizeSource();
}

bool MaterialLibraryPanel::RequestClose() noexcept
{
    if (!Library.QueryModified() && !Library.QueryParkedEdits()) return true;
    ClosingRequested = true;
    return false;
}

bool MaterialLibraryPanel::ConsumeClose() noexcept
{
    const bool Authorized = CloseAuthorized;
    CloseAuthorized = false;
    return Authorized;
}

void MaterialLibraryPanel::SynchronizeSource()
{
    std::snprintf(SourceText.data(), SourceText.size(), "%s", Library.QuerySource().c_str());
}

void MaterialLibraryPanel::FinalizeContent() noexcept
{
    ActivatePattern = {};
    ActivateStamp = {};
    ActivateBaking = {};
    Library = MaterialLibrary{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIBRARY AND SOURCE EDITOR
//------------------------------------------------------------------------------------------------------------------------

void MaterialLibraryPanel::ConstructCatalogue()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    if (ConstructMaterialPill("Workspace", At(14, 10), ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), !CollectionVisible)) CollectionVisible = false;
    const auto CollectionCaption = "My collection  " + std::to_string(Favourites.size());
    if (ConstructMaterialPill(CollectionCaption.c_str(), ImVec2(Origin.x + Width * .5f + 3 * Scale, Origin.y + 10 * Scale),
        ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), CollectionVisible)) CollectionVisible = true;
    ConstructMaterialCaption(At(20, 56).x, At(20, 56).y, 18 * Scale, 0xd3d3c8, "Material library", static_cast<ImFont *>(HeadingTypeface));
    {
        auto *TitleFont = static_cast<ImFont *>(HeadingTypeface) ? static_cast<ImFont *>(HeadingTypeface) : ImGui::GetFont();
        const float TitleWidth = TitleFont->CalcTextSizeA(18 * 1.302f * Scale, FLT_MAX, 0, "Material library").x;
        ConstructMaterialCaption(At(20, 56).x + TitleWidth + 9 * Scale, At(0, 61).y, 8 * Scale, 0x777766,
            std::to_string(Library.QueryEntries().size()).c_str());
    }
    if (ConstructMaterialPill("##New material", ImVec2(Origin.x + Width - 40 * Scale, At(0, 52).y), ImVec2(24 * Scale, 24 * Scale))) Creating = true;
    ConstructIcon(IconSymbol::EditorAdd, Origin.x + Width - 35 * Scale, At(0, 57).y, 14 * Scale);
    ImGui::SetCursorScreenPos(At(14, 96));
    ImGui::SetNextItemWidth(Width - 28 * Scale);
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(25.f / 255, 25.f / 255, 25.f / 255, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * Scale, 11.5f * Scale));
    ImGui::InputTextWithHint("##Search", "Search materials...", SearchText.data(), SearchText.size());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ConstructIcon(IconSymbol::EditorSearch, At(27, 108).x, At(27, 108).y, 15 * Scale);
    Library.ConfigureFilter(SearchText.data());
    const char *Categories[]{"All materials", "Paint", "Metal", "Ceramic", "Fabric", "Rubber", "Glass", "Plastic", "Leather", "Clay", "Wax", "Skin", "Paper", "Technical", "Stone", "Wall", "Nature"};
    float X = 14, Y = 144;
    ImGui::PushFont(nullptr, 8 * 1.302f * Scale);
    for (const char *Category : Categories)
    {
        const float Edge = ImGui::CalcTextSize(Category).x / Scale + 16;
        if (X + Edge > Width / Scale - 14) { X = 14; Y += 29; }
        if (ConstructMaterialPill(Category, At(X, Y), ImVec2(Edge * Scale, 24 * Scale), CategoryFilter == Category)) CategoryFilter = Category;
        X += Edge + 5;
    }
    Y += 24 + 8;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddLine(At(0, Y), At(Width / Scale, Y), Ink255(0xffffff, 13));
    ConstructMaterialCaption(At(17, Y + 12).x, At(17, Y + 12).y, 8 * Scale, 0x7e7e70, "THE ESSENTIAL COLLECTION");
    std::vector<size_t> Visible;
    for (size_t Index : Library.QueryVisible())
    {
        const auto &Entry = Library.QueryEntries()[Index];
        if (CategoryFilter != "All materials" && Entry.Category != CategoryFilter) continue;
        if (CollectionVisible && !Favourites.contains(EncodeContentPath(Entry.RelativePath))) continue;
        Visible.push_back(Index);
    }
    ConstructMaterialCaption(Origin.x + Width - 30 * Scale, At(0, Y + 12).y, 8 * Scale, 0x7e7e70, std::to_string(Visible.size()).c_str());
    ImGui::PopFont();
    const float FooterY = Bottom - 71 * Scale;
    ImGui::SetCursorScreenPos(At(16, Y + 31));
    ImGui::BeginChild("Material grid", ImVec2(Width - 32 * Scale, std::max(1.f, FooterY - At(0, Y + 31).y)), ImGuiChildFlags_None);
    const float Gap = 9 * Scale;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - 20 * Scale - Gap) * .5f);
    const float CardHeight = 125 * Scale;
    const auto GridOrigin = ImGui::GetCursorScreenPos();
    const float ClipTop = ImGui::GetWindowPos().y, ClipBottom = ClipTop + ImGui::GetWindowSize().y;
    std::vector<std::filesystem::path> Requests;
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float Top = GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap);
        if (Top + CardHeight >= ClipTop && Top < ClipBottom) Requests.push_back(Library.QueryEntries()[Visible[Ordinal]].RelativePath);
    }
    // 📝 Keep the inspector thumbnail resident even when its selected card scrolls out of view.
    if (!Library.QuerySelection().empty() && std::find(Requests.begin(), Requests.end(), Library.QuerySelection()) == Requests.end())
        Requests.push_back(Library.QuerySelection());
    if (RequestThumbnails) RequestThumbnails(Requests);
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const auto &Entry = Library.QueryEntries()[Visible[Ordinal]];
        const ImVec2 Position(GridOrigin.x + 12 * Scale + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        if (Position.y + CardHeight < ClipTop || Position.y > ClipBottom) continue;
        ImGui::SetCursorScreenPos(Position);
        ImGui::PushID(int(Visible[Ordinal]));
        if (ImGui::InvisibleButton("Material card", ImVec2(CardWidth, CardHeight))) SelectMaterial(Entry.RelativePath);
        const bool Hovered = ImGui::IsItemHovered();
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale);
        const PlaneExtent Card{Position.x / Scale, Position.y / Scale, (Position.x + CardWidth) / Scale, (Position.y + CardHeight) / Scale};
        (void)ControlKit::SectionCard(Surface, Card, 15);
        auto *CardInk = ImGui::GetWindowDrawList();
        if (Hovered) CardInk->AddRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), Ink255(0x707173), 15 * Scale);
        const auto Image = QueryThumbnail ? QueryThumbnail(Entry.RelativePath) : MaterialPreviewImage{};
        CardInk->PushClipRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), true);
        if (Image.Texture)
            CardInk->AddImage(ImTextureRef(Image.Texture), ImVec2(Position.x + (CardWidth - 85 * Scale) * .5f, Position.y),
                ImVec2(Position.x + (CardWidth + 85 * Scale) * .5f, Position.y + 85 * Scale),
                ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 89 * Scale, 9 * Scale, 0xc8c8b6, Entry.Name.c_str());
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 105 * Scale, 7 * Scale, 0x767665, Entry.Category.empty() ? "Layered material" : Entry.Category.c_str());
        if (Library.QuerySelection() == Entry.RelativePath)
        {
            PanelChrome::CheckDisc(CardInk, Position, Scale);
        }
        if (Library.QueryParked(Entry.RelativePath) || (Library.QuerySelection() == Entry.RelativePath && Library.QueryModified()))
            CardInk->AddCircleFilled(ImVec2(Position.x + CardWidth - 14 * Scale, Position.y + 14 * Scale), 5 * Scale, Ink255(0xd8b25c));
        CardInk->PopClipRect();
        if (Hovered) PresentMaterialTooltip(Scale, Entry.Name.c_str(), Entry.Description.c_str());
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + ((Visible.size() + 1) / 2) * (CardHeight + Gap)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    Ink->AddRectFilled(ImVec2(Origin.x, FooterY), ImVec2(Origin.x + Width, Bottom), Ink255(0x151515));
    ConstructIcon(IconSymbol::Stars, Origin.x + 19 * Scale, FooterY + 20 * Scale, 19 * Scale);
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 20 * Scale, 9 * Scale, 0xaeae9b, "Beautiful by calculation.");
    const auto Footer = std::to_string(Library.QueryEntries().size()) + " presets. Vector + procedural surfaces.";
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 37 * Scale, 7 * Scale, 0x6b6b60, Footer.c_str());
    ImGui::PopFont();
}

void MaterialLibraryPanel::SynchronizePreviewCamera()
{
    if (!SmoothedInspectionReady)
    {
        SmoothedInspection = Inspection;
        SmoothedInspectionReady = true;
    }
    else
    {
        SmoothedInspection.Shape = Inspection.Shape;
        SmoothedInspection.Environment = Inspection.Environment;
        SmoothedInspection.Exposure = Inspection.Exposure;
        SmoothedInspection.LightIntensity = Inspection.LightIntensity;
        const float Blend = 1 - std::exp(-std::min(ImGui::GetIO().DeltaTime, .1f) * 8);
        SmoothedInspection.Yaw = std::remainder(SmoothedInspection.Yaw +
                std::remainder(Inspection.Yaw - SmoothedInspection.Yaw, 6.2831853f) * Blend,
            6.2831853f);
        SmoothedInspection.Pitch += (Inspection.Pitch - SmoothedInspection.Pitch) * Blend;
        SmoothedInspection.Zoom += (Inspection.Zoom - SmoothedInspection.Zoom) * Blend;
        SmoothedInspection.PanX += (Inspection.PanX - SmoothedInspection.PanX) * Blend;
        SmoothedInspection.PanY += (Inspection.PanY - SmoothedInspection.PanY) * Blend;
        // 📝 Snap once the lag closes so the preview stops dirtying and settles exactly on the target.
        if (std::abs(std::remainder(Inspection.Yaw - SmoothedInspection.Yaw, 6.2831853f)) < .0002f &&
            std::abs(Inspection.Pitch - SmoothedInspection.Pitch) < .0002f &&
            std::abs(Inspection.Zoom - SmoothedInspection.Zoom) < .0002f &&
            std::abs(Inspection.PanX - SmoothedInspection.PanX) < .0002f &&
            std::abs(Inspection.PanY - SmoothedInspection.PanY) < .0002f)
            SmoothedInspection = Inspection;
    }
    if (!SentInspectionReady || SmoothedInspection != SentInspection)
    {
        SentInspection = SmoothedInspection;
        SentInspectionReady = true;
        if (ConfigurePreview) ConfigurePreview(SmoothedInspection);
    }
}

void MaterialLibraryPanel::ConstructViewport()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const auto Available = ImGui::GetContentRegionAvail();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilledMultiColor(Origin, ImVec2(Origin.x + Available.x, Origin.y + Available.y),
        Ink255(0x202222), Ink255(0x202222), Ink255(0x303235), Ink255(0x303235));
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    // 📝 Full-bleed cover fit between the header and footer bars; transparent margins crop invisibly.
    const float ViewTop = Origin.y + 42 * Scale, ViewBottom = Origin.y + Available.y - 45 * Scale;
    const float ViewWidth = Available.x, ViewHeight = std::max(1.f, ViewBottom - ViewTop);
    const float Edge = std::max(1.f, std::max(ViewWidth, ViewHeight));
    // 📝 Display-size preview: follow the cover-fit edge in framebuffer pixels (snapped, so a live window
    // resize settles instead of reallocating every frame) rather than upscaling the 512 default.
    const float Pixels = Edge * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t Snapped = std::clamp((uint32_t(Pixels) + 63) / 64 * 64, 512u, 2048u);
    if (ConfigurePreviewExtent && Snapped != SentPreviewExtent)
    {
        SentPreviewExtent = Snapped;
        ConfigurePreviewExtent(Snapped);
    }
    const ImVec2 CoverMinimum(Origin.x + (ViewWidth - Edge) * .5f, ViewTop + (ViewHeight - Edge) * .5f + 22 * Scale);
    if (Image.Texture)
    {
        Ink->PushClipRect(ImVec2(Origin.x, ViewTop), ImVec2(Origin.x + ViewWidth, ViewBottom), true);
        Ink->AddImage(ImTextureRef(Image.Texture), CoverMinimum, ImVec2(CoverMinimum.x + Edge, CoverMinimum.y + Edge),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        Ink->PopClipRect();
    }
    else if (!QueryPreview)
    {
        if (const auto *Applied = Library.QueryApplied())
        {
            static const MaterialInspectionGeometry InspectionGeo = ConstructMaterialInspectionGeometryWithRanges();
            const uint32_t CpuExtent = std::clamp(uint32_t(Edge * .85f), 256u, 512u);
            const uint64_t CurrentRevision = Library.QueryRevision();
            if (CachedCpuPreviewRgba.empty() || CachedCpuPreviewRevision != CurrentRevision ||
                CachedCpuPreviewExtent != CpuExtent || CachedCpuPreviewInspection != SmoothedInspection)
            {
                const size_t ShapeSlot = std::min<size_t>(size_t(SmoothedInspection.Shape), InspectionGeo.ShapeIndexRanges.size() - 1);
                WorkspaceSurfaceViewportRequest Request{};
                Request.Geometry = &InspectionGeo.Geometry;
                Request.FirstIndex = InspectionGeo.ShapeIndexRanges[ShapeSlot][0];
                Request.IndexCount = InspectionGeo.ShapeIndexRanges[ShapeSlot][1];
                Request.Material = Applied;
                Request.Camera = ConstructMaterialPreviewProjection(SmoothedInspection);
                Request.LightDirection = ConstructMaterialPreviewLightDirection(SmoothedInspection, {.3906f, .7330f, .5569f});
                Request.LightIntensity = 2.25f * SmoothedInspection.LightIntensity;
                Request.EnvironmentStrength = 1.f * SmoothedInspection.LightIntensity;
                Request.EnvironmentUp = ConstructMaterialPreviewEnvironmentUp(SmoothedInspection);
                Request.Exposure = SmoothedInspection.Exposure;
                Request.PreviewShapeOrdinal = uint32_t(ShapeSlot) + 1u;
                auto Rendered = RenderWorkspaceSurfaceViewportCpu(Request, CpuExtent, CpuExtent);
                CachedCpuPreviewRgba = std::move(Rendered.Rgba);
                CachedCpuPreviewRevision = CurrentRevision;
                CachedCpuPreviewExtent = CpuExtent;
                CachedCpuPreviewInspection = SmoothedInspection;
            }
            BlitWorkspaceViewportCpuFrame(Ink, {CachedCpuPreviewExtent, CachedCpuPreviewExtent, CachedCpuPreviewRgba},
                CoverMinimum, ImVec2(CoverMinimum.x + Edge, CoverMinimum.y + Edge),
                ImVec2(Origin.x, ViewTop), ImVec2(Origin.x + ViewWidth, ViewBottom));
        }
    }
    Ink->AddRectFilledMultiColor(ImVec2(Origin.x, ViewTop), ImVec2(Origin.x + ViewWidth, ViewTop + 118 * Scale),
        Ink255(0x151515), Ink255(0x151515), Ink255(0x151515, 0), Ink255(0x151515, 0));
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Available.x, Origin.y + 42 * Scale), Ink255(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Origin.y + 15 * Scale, 7 * Scale, 0x898a78, "LIVE PREVIEW   PBR");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + std::max(4.f, Available.x - 92 * Scale), Origin.y + 7 * Scale));
    if (ImGui::Button("Fullscreen##MaterialPreview", ImVec2(84 * Scale, 27 * Scale)))
    {
        FullscreenPreview = true;
        ResetMaterialPreviewView(Inspection);
        SynchronizePreviewCamera();
    }
    const auto FullscreenMinimum = ImGui::GetItemRectMin(), FullscreenMaximum = ImGui::GetItemRectMax();
    FullscreenPreviewExtent = {FullscreenMinimum.x, FullscreenMinimum.y, FullscreenMaximum.x, FullscreenMaximum.y};
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Fullscreen preview", "Fitted full-work-area preview. Escape to close.");
    const auto *Entry = QuerySelectedMaterial(Library);
    const bool CompactHeight = Available.y < 700 * Scale;
    const float HeadingSize = CompactHeight ? 24 * Scale : 28 * Scale;
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 56 * Scale, 8 * Scale, 0x92917c,
        Entry && !Entry->Category.empty() ? ("01  /  " + Entry->Category + " COLLECTION").c_str() : "MATERIAL COLLECTION");
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 76 * Scale, HeadingSize, 0xdcdccd, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    if (Entry)
    {
        (void)ConstructInspectorTextBlock(ImVec2(Origin.x + 22 * Scale, Origin.y + (CompactHeight ? 110.f : 116.f) * Scale),
            std::max(80.f, Available.x - 44 * Scale), 9, 1.50f, 0x7b7c6d, Entry->Description.c_str(), Scale);
    }
    const float Bottom = Origin.y + Available.y;
    const bool CompactToolbar = Available.x < 610 * Scale;
    const float TopToolbarRowY = CompactToolbar ? (Bottom - 120 * Scale) : (Bottom - 88 * Scale);
    const float BottomToolbarRowY = CompactToolbar ? (Bottom - 84 * Scale) : (Bottom - 88 * Scale);
    Ink->AddRectFilled(ImVec2(Origin.x, Bottom - 45 * Scale), ImVec2(Origin.x + Available.x, Bottom), Ink255(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "Drag orbit  /  Shift-MMB pan  /  Wheel zoom");
    if (Available.x > 420 * Scale) ConstructMaterialCaption(Origin.x + Available.x - 75 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "VULKAN");

    const auto ConfigureExtent = [&](size_t Index)
    {
        const auto Minimum = ImGui::GetItemRectMin();
        const auto Maximum = ImGui::GetItemRectMax();
        InspectionExtents[Index] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    ImGui::PushID("MaterialInspection");
    ImGui::BeginDisabled(!ConfigurePreview);
    // 📝 One canvas owns gestures until release; widgets and popup menus cannot also drive the camera.
    InspectionExtents[7] = {};
    const ImVec2 CanvasMinimum(Origin.x, Origin.y + 148 * Scale);
    const ImVec2 CanvasMaximum(Origin.x + Available.x, TopToolbarRowY - 8 * Scale);
    if (CanvasMaximum.y > CanvasMinimum.y)
    {
        ImGui::SetCursorScreenPos(CanvasMinimum);
        ImGui::InvisibleButton("InspectionCanvas", ImVec2(Available.x, CanvasMaximum.y - CanvasMinimum.y),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        ConfigureExtent(7);
        const auto &Input = ImGui::GetIO();
        const bool Hovered = ImGui::IsItemHovered();
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
        if (ImGui::IsItemActive())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            {
                Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / Edge, -2.f, 2.f);
                Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / Edge, -2.f, 2.f);
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 5.f);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
                Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
            }
        }
        if (Hovered && !Input.WantTextInput)
        {
            if (Input.MouseWheel != 0) Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 5.f);
            if (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsMouseDoubleClicked(0))
                ResetMaterialPreviewView(Inspection);
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Inspection.PanX = std::max(-2.f, Inspection.PanX - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Inspection.PanX = std::min(2.f, Inspection.PanX + .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Inspection.PanY = std::max(-2.f, Inspection.PanY - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Inspection.PanY = std::min(2.f, Inspection.PanY + .05f);
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Scale, 5 * Scale));
    const float ShapeWidth = CompactToolbar
        ? std::min(148 * Scale, std::max(96 * Scale, (Available.x - 44 * Scale) * .46f))
        : std::min(144 * Scale, Available.x * .26f);
    const float EnvironmentButtonWidth = CompactToolbar
        ? std::min(176 * Scale, std::max(108 * Scale, Available.x - 44 * Scale - ShapeWidth))
        : std::min(164 * Scale, Available.x * .30f);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 18 * Scale, TopToolbarRowY));
    ImGui::SetNextItemWidth(ShapeWidth);
    const char *ShapeOptions[]{"Shader ball", "Sphere", "Plane", "Cube", "Draped cloth"};
    int Shape = int(Inspection.Shape);
    ImGui::SetNextItemWidth(ShapeWidth);
    if (ConstructMaterialSelection("Preview shape", ShapeOptions[Shape], ShapeOptions, 5, Shape, Scale))
        Inspection.Shape = MaterialPreviewShape(Shape);
    ConfigureExtent(3);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Preview shape", "Change preview object; catalogue thumbnails remain unchanged.");
    ImGui::SameLine(0, 8 * Scale);
    // 📝 Environment card (Blender-style): sphere previews of every scanned studio archive over
    // exposure and light intensity, with a folder rescan for importing equirect PNGs. The old
    // standalone exposure card and the tinted analytic presets are retired.
    const auto EnvironmentNames = QueryEnvironmentNames ? QueryEnvironmentNames() : std::vector<std::string>{};
    if (!EnvironmentNames.empty())
        Inspection.Environment = std::min<uint32_t>(Inspection.Environment, uint32_t(EnvironmentNames.size() - 1));
    const std::string EnvironmentCaption = EnvironmentNames.empty() ? std::string("Environment") :
        EnvironmentNames[Inspection.Environment];
    if (ImGui::Button((EnvironmentCaption + "##PreviewEnvironment").c_str(), ImVec2(EnvironmentButtonWidth, 0)))
        ImGui::OpenPopup("##EnvironmentCard");
    ConfigureExtent(4);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Environment", "Studio lighting, exposure and light intensity.");
    ImGui::SetNextWindowSize(ImVec2(342 * Scale, 0));
    if (DropdownControlSurface::BeginMenu("##EnvironmentCard", Scale))
    {
        EnvironmentPreviewExtents.clear();
        if (EnvironmentNames.empty()) ImGui::TextDisabled("No environment archives found.");
        const float TileExtent = 148 * Scale;
        const float CellHeight = TileExtent + 20 * Scale;
        for (size_t Index = 0; Index < EnvironmentNames.size(); ++Index)
        {
            if (Index % 2) ImGui::SameLine();
            const auto CellOrigin = ImGui::GetCursorScreenPos();
            ImGui::PushID(int(Index));
            const bool Pressed = ImGui::InvisibleButton("##EnvironmentPreview", ImVec2(TileExtent, CellHeight));
            ImGui::PopID();
            EnvironmentPreviewExtents.push_back({CellOrigin.x, CellOrigin.y,
                CellOrigin.x + TileExtent, CellOrigin.y + TileExtent});
            const auto Preview = QueryEnvironmentPreview ? QueryEnvironmentPreview(Index) : MaterialPreviewImage{};
            auto *CardInk = ImGui::GetWindowDrawList();
            const ImVec2 TileMinimum = CellOrigin, TileMaximum(CellOrigin.x + TileExtent, CellOrigin.y + TileExtent);
            if (Preview.Texture)
                CardInk->AddImageRounded(ImTextureRef(Preview.Texture), TileMinimum, TileMaximum,
                    ImVec2(Preview.MinimumU, Preview.MinimumV), ImVec2(Preview.MaximumU, Preview.MaximumV),
                    Ink255(0xffffff), 10 * Scale);
            else
                CardInk->AddRectFilled(TileMinimum, TileMaximum, Ink255(0x232629), 10 * Scale);
            if (uint32_t(Index) == Inspection.Environment)
                CardInk->AddRect(TileMinimum, TileMaximum, Ink255(0x8fd0a8, 255), 10 * Scale, 0, 2 * Scale);
            else if (ImGui::IsItemHovered())
                CardInk->AddRect(TileMinimum, TileMaximum, Ink255(0xffffff, 70), 10 * Scale, 0, 1 * Scale);
            const auto &Name = EnvironmentNames[Index];
            const float CaptionWidth = ImGui::CalcTextSize(Name.c_str()).x;
            ConstructMaterialCaption(CellOrigin.x + (TileExtent - CaptionWidth) * .5f,
                CellOrigin.y + TileExtent + 3 * Scale, 10 * Scale,
                uint32_t(Index) == Inspection.Environment ? 0xe8e4d8 : 0xa3a393, Name.c_str());
            if (Pressed)
            {
                Inspection.Environment = uint32_t(Index);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        const auto ExposureOrigin = ImGui::GetCursorScreenPos();
        const float ExposureWidth = std::max(28.f, ImGui::GetContentRegionAvail().x);
        (void)ConstructProperty("Exposure", .25f, 4.f, Inspection.Exposure, "×", true, .01f);
        InspectionExtents[5] = {ExposureOrigin.x, ExposureOrigin.y + 42 * Scale,
            ExposureOrigin.x + ExposureWidth, ExposureOrigin.y + 66 * Scale};
        (void)ConstructProperty("Light intensity", 0.f, 3.f, Inspection.LightIntensity, "×", false, .01f);
        ImGui::Separator();
        if (ImGui::Button("Import environment…", ImVec2(-1, 0)) && RescanEnvironments) RescanEnvironments();
        ImGui::TextDisabled("Drop equirect PNGs into EngineContent/EnvironmentArchives, then import.");
        DropdownControlSurface::EndMenu();
    }
    const float ZoomGroupX = CompactToolbar
        ? (Origin.x + 18 * Scale)
        : (Origin.x + std::max(18.f * Scale, Available.x - 256 * Scale));
    ImGui::SetCursorScreenPos(ImVec2(ZoomGroupX, BottomToolbarRowY));
    const auto ZoomOrigin = ImGui::GetCursorScreenPos();
    Ink->AddRectFilled(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        Ink255(0x181818), 14 * Scale);
    Ink->AddRect(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        Ink255(0xffffff, 20), 14 * Scale);
    const ImVec2 ResetPillMin(ZoomOrigin.x + 148 * Scale, ZoomOrigin.y);
    const ImVec2 ResetPillMax(ResetPillMin.x + 90 * Scale, ResetPillMin.y + 28 * Scale);
    Ink->AddRectFilled(ResetPillMin, ResetPillMax, Ink255(0x181818), 14 * Scale);
    Ink->AddRect(ResetPillMin, ResetPillMax, Ink255(0xffffff, 20), 14 * Scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::BeginGroup();
    if (ImGui::Button("-##ZoomOut", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
    ConfigureExtent(0);
    ImGui::SameLine(0, 0);
    const auto ZoomCaption = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##ZoomReset";
    if (ImGui::Button(ZoomCaption.c_str(), ImVec2(76 * Scale, 28 * Scale))) Inspection.Zoom = 1;
    ConfigureExtent(1);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Reset zoom", "Reset zoom to 100%.");
    ImGui::SameLine(0, 0);
    if (ImGui::Button("+##ZoomIn", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
    ConfigureExtent(2);
    ImGui::SameLine(0, 8 * Scale);
    if (ImGui::Button("Reset view", ImVec2(90 * Scale, 28 * Scale))) ResetMaterialPreviewView(Inspection);
    ConfigureExtent(6);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Reset view", "Centre / fit and reset orbit. F or Home over preview.");
    ImGui::EndGroup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    SynchronizePreviewCamera();
    ImGui::PopID();
}

void MaterialLibraryPanel::ConstructFullscreenPreview()
{
    if (!FullscreenPreview) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::SetNextWindowViewport(Viewport->ID);
    ImGui::SetNextWindowBgAlpha(1);
    constexpr ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Fullscreen material preview###MaterialPreviewFullscreen", nullptr, Flags))
    {
        ImGui::End();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) FullscreenPreview = false;
    const ImVec2 Origin = ImGui::GetWindowPos();
    const ImVec2 Size = ImGui::GetWindowSize();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), Ink255(0x17191a));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 28, Origin.y + 20));
    ImGui::TextUnformatted(QuerySelectedMaterial(Library) ? QuerySelectedMaterial(Library)->Name.c_str() : "Material preview");
    ImGui::SameLine();
    ImGui::TextDisabled("PBR · fullscreen fit · Escape to close");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Size.x - 150, Origin.y + 14));
    if (ImGui::Button("Close preview  ×", ImVec2(132, 32))) FullscreenPreview = false;
    const float Edge = std::max(1.f, std::min(Size.x - 64.f, Size.y - 150.f));
    const uint32_t FullscreenSnapped =
        std::clamp((uint32_t(Edge * ImGui::GetIO().DisplayFramebufferScale.x) + 63) / 64 * 64, 512u, 2048u);
    if (ConfigurePreviewExtent && FullscreenSnapped != SentPreviewExtent)
    {
        SentPreviewExtent = FullscreenSnapped;
        ConfigurePreviewExtent(FullscreenSnapped);
    }
    const ImVec2 ImagePosition(Origin.x + (Size.x - Edge) * .5f, Origin.y + (Size.y - Edge) * .5f + 8.f);
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    ImGui::SetCursorScreenPos(ImagePosition);
    ImGui::InvisibleButton("Fullscreen inspection canvas", ImVec2(Edge, Edge),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
    const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
    if (ImGui::IsItemActive())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        {
            Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / Edge, -2.f, 2.f);
            Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / Edge, -2.f, 2.f);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 5.f);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
            Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
        }
    }
    if (Hovered && Input.MouseWheel != 0)
    {
        Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 5.f);
    }
    Ink->PushClipRect(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), true);
    if (Image.Texture)
        Ink->AddImage(ImTextureRef(Image.Texture), ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    else if (const auto *Applied = Library.QueryApplied())
    {
        Ink->AddRectFilled(ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge), Ink255(0x202222));
        static const MaterialInspectionGeometry InspectionGeo = ConstructMaterialInspectionGeometryWithRanges();
        const uint32_t CpuExtent = std::clamp(uint32_t(Edge * .85f), 256u, 512u);
        const uint64_t CurrentRevision = Library.QueryRevision();
        if (CachedCpuPreviewRgba.empty() || CachedCpuPreviewRevision != CurrentRevision ||
            CachedCpuPreviewExtent != CpuExtent || CachedCpuPreviewInspection != SmoothedInspection)
        {
            const size_t ShapeSlot = std::min<size_t>(size_t(SmoothedInspection.Shape), InspectionGeo.ShapeIndexRanges.size() - 1);
            WorkspaceSurfaceViewportRequest Request{};
            Request.Geometry = &InspectionGeo.Geometry;
            Request.FirstIndex = InspectionGeo.ShapeIndexRanges[ShapeSlot][0];
            Request.IndexCount = InspectionGeo.ShapeIndexRanges[ShapeSlot][1];
            Request.Material = Applied;
            Request.Camera = ConstructMaterialPreviewProjection(SmoothedInspection);
            Request.LightDirection = ConstructMaterialPreviewLightDirection(SmoothedInspection, {.3906f, .7330f, .5569f});
            Request.LightIntensity = 2.25f * SmoothedInspection.LightIntensity;
            Request.EnvironmentStrength = 1.f * SmoothedInspection.LightIntensity;
            Request.EnvironmentUp = ConstructMaterialPreviewEnvironmentUp(SmoothedInspection);
            Request.Exposure = SmoothedInspection.Exposure;
            Request.PreviewShapeOrdinal = uint32_t(ShapeSlot) + 1u;
            auto Rendered = RenderWorkspaceSurfaceViewportCpu(Request, CpuExtent, CpuExtent);
            CachedCpuPreviewRgba = std::move(Rendered.Rgba);
            CachedCpuPreviewRevision = CurrentRevision;
            CachedCpuPreviewExtent = CpuExtent;
            CachedCpuPreviewInspection = SmoothedInspection;
        }
        BlitWorkspaceViewportCpuFrame(Ink, {CachedCpuPreviewExtent, CachedCpuPreviewExtent, CachedCpuPreviewRgba},
            ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge),
            ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge));
    }
    else Ink->AddRectFilled(ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge), Ink255(0x202222));
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + (Size.x - 300) * .5f, Origin.y + Size.y - 58));
    if (ImGui::Button("−##FullscreenZoomOut", ImVec2(42, 30))) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
    ImGui::SameLine();
    const std::string ZoomLabel = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##FullscreenZoomReset";
    if (ImGui::Button(ZoomLabel.c_str(), ImVec2(80, 30))) Inspection.Zoom = 1;
    ImGui::SameLine();
    if (ImGui::Button("+##FullscreenZoomIn", ImVec2(42, 30))) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
    ImGui::SameLine();
    if (ImGui::Button("Fit object", ImVec2(92, 30))) ResetMaterialPreviewView(Inspection);
    SynchronizePreviewCamera();
    ImGui::End();
}

void MaterialLibraryPanel::ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb)
{
    if (PresentIcon) (void)PresentIcon(Symbol, {X,Y}, Size, SpectrumQuad(Rgb));
}

bool MaterialLibraryPanel::ConstructProperty(const char *Caption, float Minimum, float Maximum, float &Position,
    const char *Unit, bool Logarithmic, float Step, const char *Unavailable, float RowWidth, bool RecipeForm)
{
    // 📝 HTML .slider-field: 10px label + 25px value pill right, 24px capsule track at +42, optional
    // range-label row on logarithmic fields. Block = 71px (90 with the LOG row); callers own margins.
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(28.f, RowWidth >= 0.f ? RowWidth : ImGui::GetContentRegionAvail().x);
    const bool Enabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
    const bool LogarithmicRange = Logarithmic && Minimum > 0 && Maximum > Minimum;
    auto *Ink = ImGui::GetWindowDrawList();
    ConstructMaterialCaption(Origin.x, Origin.y + 7 * Scale, 10 * Scale, 0xa7acb5, Caption);
    const bool Percentage = std::string_view(Unit) == "%";
    const float Multiplier = Percentage ? 100.f : 1.f;
    const bool ShowUnit = Percentage || (Unit && Unit[0]);
    const float UnitWidth = ShowUnit ? ImGui::GetFont()->CalcTextSizeA(7 * 1.302f * Scale, FLT_MAX, 0, Unit).x : 0.f;
    const float InputWidth = (RecipeForm ? 40.f : 42.f) * Scale;
    const float PillWidth = std::max((RecipeForm ? 66.f : 63.f) * Scale,
        12 * Scale + InputWidth + (ShowUnit ? 5 * Scale + UnitWidth : 0.f));
    const auto PillMin = ImVec2(Origin.x + Width - PillWidth, Origin.y);
    const auto PillMax = ImVec2(PillMin.x + PillWidth, Origin.y + 25 * Scale);
    Ink->AddRectFilled(PillMin, PillMax, Ink255(0x090909), 12.5f * Scale);
    Ink->AddRect(PillMin, PillMax, Ink255(0xffffff, 13), 12.5f * Scale);
    if (ShowUnit)
        Ink->AddText(ImGui::GetFont(), 7 * 1.302f * Scale,
            ImVec2(PillMax.x - 6 * Scale - UnitWidth, Origin.y + 8 * Scale), Ink255(0x7f8f9a), Unit);
    ImGui::PushID(Caption);
    ImGui::SetCursorScreenPos(ImVec2(PillMin.x + 6 * Scale, Origin.y));
    ImGui::SetNextItemWidth(InputWidth);
    ImGui::PushFont(static_cast<ImFont *>(HeadingTypeface), 10 * 1.302f * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 6 * Scale));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Ink255(0xcbd3dc)));
    const char *Format = Percentage || Step >= 1 ? "%.0f" : "%.6g";
    float Display = Position * Multiplier;
    bool Changed = ImGui::InputFloat("##Position", &Display, 0, 0, Format);
    if (Changed && std::isfinite(Display)) Position = std::clamp(Display / Multiplier, Minimum, Maximum);
    else Changed = false;
    bool Hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 42 * Scale));
    ImGui::InvisibleButton("Property slider", ImVec2(Width, 24 * Scale));
    const bool Active = Enabled && ImGui::IsItemActive();
    Hovered = Hovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const auto &IO = ImGui::GetIO();
    ControlPointer Pointer{IO.MousePos.x / Scale, IO.MousePos.y / Scale, Enabled && IO.MouseDown[0],
        Enabled && ImGui::IsItemActivated(), Enabled && ImGui::IsMouseReleased(0), Enabled && (Active || ImGui::IsItemHovered()), IO.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const PlaneExtent Extent{Origin.x / Scale, Origin.y / Scale + 42, (Origin.x + Width) / Scale, Origin.y / Scale + 66};
    const float Fraction = LogarithmicRange ? std::clamp(std::log(std::max(Position, Minimum) / Minimum) / std::log(Maximum / Minimum), 0.f, 1.f) : Position;
    float Replacement = Fraction;
    const auto SavedPalette = ControlKit::Palette();
    auto SliderPalette = SavedPalette;
    SliderPalette.SliderTrack = SpectrumQuad(0x171717);
    SliderPalette.SliderFill = SpectrumQuad(0x454545);
    SliderPalette.SliderThumb = SpectrumQuad(0xd7d7ce);
    ControlKit::ConfigurePalette(SliderPalette);
    (void)ControlKit::Slider(Surface, Extent, LogarithmicRange ? 0.f : Minimum, LogarithmicRange ? 1.f : Maximum,
        Fraction, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 24, 22);
    ControlKit::ConfigurePalette(SavedPalette);
    if (InspectorMode == 0 && !RoughnessRecorded)
    {
        RoughnessExtent = {Origin.x, Origin.y + 42 * Scale, Origin.x + Width, Origin.y + 66 * Scale};
        RoughnessRecorded = true;
    }
    if (InspectorMode == 0 && !CoatRoughnessRecorded && InspectorLayerContext == 1)
    {
        CoatRoughnessExtent = {Origin.x, Origin.y + 42 * Scale, Origin.x + Width, Origin.y + 66 * Scale};
        CoatRoughnessRecorded = true;
    }
    if (Active && Replacement != Fraction)
    {
        Position = LogarithmicRange ? Minimum * std::pow(Maximum / Minimum, Replacement) : Replacement;
        Changed = true;
    }
    if (Changed && Step >= 1) Position = std::clamp(std::round(Position / Step) * Step, Minimum, Maximum);
    float Block = 71 * Scale;
    if (LogarithmicRange)
    {
        // 📝 .range-labels renders only for logarithmic fields (or explicit low/high hints, unused here).
        const auto Trim = [](double Value)
        {
            char Text[40];
            std::snprintf(Text, sizeof Text, "%.6g", Value);
            return std::string(Text);
        };
        const std::string Low = Trim(Minimum) + " " + (Unit ? Unit : ""), High = Trim(Maximum) + " " + (Unit ? Unit : "");
        const float LabelSize = 7 * 1.302f * Scale;
        Ink->AddText(ImGui::GetFont(), LabelSize, ImVec2(Origin.x, Origin.y + 76 * Scale), Ink255(0x747e89), Low.c_str());
        const float HighWidth = ImGui::GetFont()->CalcTextSizeA(LabelSize, FLT_MAX, 0, High.c_str()).x;
        const float ChipTextWidth = ImGui::GetFont()->CalcTextSizeA(5 * 1.302f * Scale, FLT_MAX, 0, "LOG").x;
        const float ChipWidth = ChipTextWidth + 8 * Scale;
        const auto ChipMin = ImVec2(Origin.x + Width - ChipWidth, Origin.y + 75 * Scale);
        Ink->AddRectFilled(ChipMin, ImVec2(ChipMin.x + ChipWidth, ChipMin.y + 11 * Scale), Ink255(0x000000, 0), 3 * Scale);
        Ink->AddRect(ChipMin, ImVec2(ChipMin.x + ChipWidth, ChipMin.y + 11 * Scale), Ink255(0x465249), 3 * Scale);
        Ink->AddText(ImGui::GetFont(), 5 * 1.302f * Scale, ImVec2(ChipMin.x + 4 * Scale, ChipMin.y + 2 * Scale),
            Ink255(0x94a49a), "LOG");
        Ink->AddText(ImGui::GetFont(), LabelSize,
            ImVec2(ChipMin.x - 6 * Scale - HighWidth, Origin.y + 76 * Scale), Ink255(0x747e89), High.c_str());
        Block = 90 * Scale;
    }
    if (Unavailable && Hovered) PresentMaterialTooltip(Scale, "Unavailable", Unavailable);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Block));
    ImGui::Dummy(ImVec2(1, 0));
    return Changed && Enabled;
}

void MaterialLibraryPanel::ConstructActions()
{
    const auto Extent = []()
    {
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        return PlaneExtent{Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    ImGui::BeginDisabled(!Library.QueryUnapplied());
    if (ImGui::Button("Apply")) (void)Library.ApplySource();
    ImGui::EndDisabled();
    ApplyExtent = Extent();
    ImGui::SameLine();
    if (ImGui::Button("Revert") && Library.RevertSource())
    {
        PendingLivePreviewApply = false;
        ProceduralDirty = false;
        SynchronizeSource();
    }
    RevertExtent = Extent();
    ImGui::SameLine();
    // 📝 Saving is the only edit-path event that re-renders a catalogue tile (baked atlas contract).
    if (ImGui::Button("Save") && Library.SaveMaterial() && InvalidateThumbnail)
        InvalidateThumbnail(Library.QuerySelection());
    SaveExtent = Extent();
    if (ImGui::Button("Duplicate")) Duplicating = true;
    if (Library.QueryModified()) ImGui::TextWrapped("Unsaved edits. Save or Revert; drafts survive switching materials.");
    else if (Library.QueryParkedEdits()) ImGui::TextWrapped("%u other material(s) hold parked edits.", (unsigned)Library.QueryParkedCount());
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
}

void MaterialLibraryPanel::ConstructInspector()
{
    // 📝 The inspector is the Alloy shell (MaterialInspectorPanel.jsx) over MaterialPanel: a
    // 59px panel title, the identity strip, segmented Properties/Layers tabs, one scrolling
    // body, the pinned variables dock and the save-preset footer. Properties renders
    // RecipeInspector's sections over the same write paths as before; Layers renders the
    // authoring document (layers, channels, sources, generators) projected through
    // MaterialAuthoring, declaring the document from the recipe when the material has none.
    if (Library.QuerySelection().empty()) { ImGui::TextUnformatted("Select a material"); return; }
    if (SourceVisible) { ConstructSourceEditor(); return; }
    RoughnessRecorded = false;
    CoatRoughnessRecorded = false;
    InspectorLayerContext = 0;
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    const auto *Entry = QuerySelectedMaterial(Library);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Bottom), Ink255(0x121212));

    // 📝 Decode first: authoring sync, the tab count and the dock height all need the live
    // definition before the shell bands can be laid out.
    MaterialDefinition Definition;
    std::string LoadFailure;
    try { Definition = DecodeSurfaceMaterial(Library.QuerySource()); }
    catch (const std::exception &Error) { LoadFailure = Error.what(); }
    auto *Profile = Entry && LoadFailure.empty() ? QueryMaterialRecipeProfile(Entry->Recipe.RecipeId) : nullptr;
    bool ScratchesEnabled = false;
    if (LoadFailure.empty())
        for (const auto &StratumChannels : Definition.StratumChannels)
            for (const auto &Channel : StratumChannels)
                if (!Channel.Op.empty())
                    if (const auto Density = Channel.Params.find("scratchDensity"); Density != Channel.Params.end())
                        ScratchesEnabled = ScratchesEnabled || Density->second > 0;
    const auto Controls = Entry && LoadFailure.empty() ? QueryMaterialRecipeControls(*Entry, ScratchesEnabled)
        : std::vector<MaterialRecipeControl>{};

    // ── Authoring sync ───────────────────────────────────────────────────────────────────────
    // The persisted document is truth; without one, declare the document Alloy derives from the
    // recipe (DeclareMaterialAuthoring over the profile's own facts). A source hash in the sync
    // key makes external edits (revert, source editor) reseed while our own persists update it.
    const auto SourceKey = [&]()
    {
        return EncodeContentPath(Library.QuerySelection()) + "@" +
            std::to_string(std::hash<std::string>{}(Library.QuerySource()));
    };
    if (LoadFailure.empty() && Entry && AuthoringSelectionKey != SourceKey())
    {
        AuthoringSelectionKey = SourceKey();
        AuthoringStore.clear();
        InspectorNotice.clear();
        EditingVariableId.clear();
        RenamingVariableId.clear();
        InspectorNameKey.clear();
        InspectorCategoryKey.clear();
        InspectorHexKey.clear();
        TexturePathKey.clear();
        OpenedInspectorDetails.clear();
        FoldedInspectorCards.clear();
        FoldedInspectorCards.insert("section:detail");
        FoldedInspectorCards.insert("section:colors");
        if (Definition.Authoring) AuthoringDocument = *Definition.Authoring;
        else
        {
            try
            {
                MaterialAuthoringDeclaration Declaration;
                Declaration.Type = Profile ? int64_t(Profile->Type) : -1;
                Declaration.Coat = QueryRecipeCoefficient(Definition, "coat").value_or(0.f);
                Declaration.Sheen = QueryRecipeCoefficient(Definition, "sheen").value_or(0.f);
                Declaration.Anisotropy = QueryRecipeCoefficient(Definition, "anisotropy").value_or(0.f);
                Declaration.Translucency = QueryRecipeCoefficient(Definition, "translucency").value_or(0.f);
                Declaration.MetalScratches = Profile && Profile->SupportsMetalScratches;
                Declaration.Woven = Profile && Profile->Woven;
                Declaration.Flakes = Definition.Scattering.Paint.Configuration[3] > 0;
                Declaration.RecipeIdentity = Entry->Recipe.RecipeId;
                Declaration.RecipeCaption = Profile ? Profile->Caption : Entry->RecipeCaption;
                for (const auto &Control : Controls)
                {
                    MaterialAuthoringControl Mapped;
                    Mapped.Key = Control.Key;
                    Mapped.Direct = Control.Direct;
                    Mapped.Minimum = Control.Minimum;
                    Mapped.Maximum = Control.Maximum;
                    Mapped.Step = Control.Step;
                    for (const auto &Target : Control.Targets)
                        Mapped.Targets.push_back({Target.Key, Target.Minimum, Target.Maximum, Target.Integer});
                    Declaration.Controls.push_back(std::move(Mapped));
                }
                AuthoringDocument = DeclareMaterialAuthoring(Declaration);
            }
            catch (const std::exception &Error)
            {
                InspectorNotice = Error.what();
                AuthoringDocument = MaterialAuthoringDocument{};
            }
        }
        VariableDockOpen = AuthoringDocument && !AuthoringDocument->Variables.empty();
    }
    if (!LoadFailure.empty()) AuthoringDocument.reset();
    if (!AuthoringDocument) AuthoringDocument = MaterialAuthoringDocument{};
    MaterialAuthoringDocument &Document = *AuthoringDocument;

    // 📝 Reseed the flat store from the live definition every frame (recipe scalars/texts,
    // routed coefficients, colour homes as hex); values only the session knows survive from
    // the previous store, so reverts and external edits win everywhere a readback exists.
    if (LoadFailure.empty() && Entry)
    {
        MaterialAuthoringStore Next = AuthoringStore;
        for (const auto &[Key, Value] : Entry->Recipe.Scalars) Next[Key] = Value;
        for (const auto &[Key, Value] : Entry->Recipe.Texts) Next[Key] = Value;
        const auto SeedCoefficient = [&](const std::string &Key)
        {
            if (const auto Value = QueryRecipeCoefficient(Definition, Key)) Next[Key] = double(*Value);
        };
        for (const auto &Control : Controls)
        {
            if (Control.Direct && !Control.Key.empty()) SeedCoefficient(Control.Key);
            for (const auto &Target : Control.Targets) SeedCoefficient(Target.Key);
        }
        for (const char *Key : {"roughness", "metalness", "ior", "specular", "anisotropy", "coat", "coatRoughness", "coatIor",
                 "sheen", "sheenRoughness", "translucency", "coverage", "normalStrength", "heightStrength", "emissionStrength"})
            SeedCoefficient(Key);
        const auto LinearToHex = [&](const float *Linear)
        {
            return EncodeHexColour({LinearToSrgbChannel(Linear[0]), LinearToSrgbChannel(Linear[1]), LinearToSrgbChannel(Linear[2])});
        };
        const uint32_t StratumTotal = Definition.Scattering.Illumination.Configuration[0];
        if (StratumTotal > 0)
        {
            if (const auto SandBase = Library.QuerySandColour("color")) Next["color"] = *SandBase;
            else Next["color"] = LinearToHex(Definition.Scattering.Illumination.Strata[0].AlbedoRoughness.data());
            if (const auto SandPale = Library.QuerySandColour("secondaryColor")) Next["secondaryColor"] = *SandPale;
            Next["sheenColor"] = LinearToHex(Definition.Scattering.Illumination.Strata[0].Sheen.data());
        }
        {
            const auto &Rad = Definition.Scattering.RadianceNormal;
            const float Peak = std::max({Rad[0], Rad[1], Rad[2]});
            if (Peak > 0.f)
            {
                const float Norm[3]{Rad[0] / Peak, Rad[1] / Peak, Rad[2] / Peak};
                Next["emissionColor"] = LinearToHex(Norm);
            }
        }
        // 📝 Procedural text colours (LED/fruit tints, sheet colour) read back from their
        // owning channel's Texts so shared reads agree with the finish swatches.
        for (uint32_t Stratum = 0; Stratum < StratumTotal && Stratum < 8; ++Stratum)
            for (const auto &Channel : Definition.StratumChannels[Stratum])
                if (!Channel.Op.empty())
                    for (const auto &[Key, Text] : Channel.Texts)
                        if (Text.size() == 7 && Text[0] == '#') Next[Key] = Text;
        if (!Next.contains("normal")) Next["normal"] = std::array<double, 3>{0, 0, 1};
        if (!Next.contains("microNormal")) Next["microNormal"] = std::array<double, 3>{0, 0, 1};
        AuthoringStore = std::move(Next);
    }

    // ── Title ──────────────────────────────────────────────────────────────────────────────────
    ConstructMaterialCaption(At(20, 19).x, At(20, 19).y, 18 * Scale, 0xd3d3c8, "Material inspector", static_cast<ImFont *>(HeadingTypeface));
    {
        ImVec2 Centre;
        if (ConstructInspectorIconButton("##InspectorReset", ImVec2(Origin.x + Width - 47 * Scale, At(0, 15.5f).y), ImVec2(28 * Scale, 28 * Scale), Scale, Centre))
        {
            if (Library.RevertSource())
            {
                PendingLivePreviewApply = false;
                InspectorNotice = "Material properties reset.";
            }
        }
        if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Reset material to preset", "Discard staged edits and rebuild this material from its stored source.");
        ConstructIcon(IconSymbol::EditorReset, Centre.x - 7.5f * Scale, Centre.y - 7.5f * Scale, 15 * Scale, 0x737365);
    }

    // ── Identity ───────────────────────────────────────────────────────────────────────────────
    {
        const auto Image = QueryThumbnail ? QueryThumbnail(Library.QuerySelection()) : MaterialPreviewImage{};
        Ink->AddRectFilled(At(20, 65), At(69, 114), Ink255(0x22221d), 16 * Scale);
        if (Image.Texture) Ink->AddImage(ImTextureRef(Image.Texture), At(22, 67), At(67, 112),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        else
        {
            std::array<float, 3> Srgb{1, 1, 1};
            if (const auto Found = AuthoringStore.find("color"); Found != AuthoringStore.end())
                if (const auto *Hex = std::get_if<std::string>(&Found->second)) (void)ParseHexColour(*Hex, Srgb);
            Ink->AddCircle(At(44.5f, 89.5f), 12 * Scale, IM_COL32(unsigned(Srgb[0] * 255), unsigned(Srgb[1] * 255), unsigned(Srgb[2] * 255), 255), 32, 1.5f * Scale);
        }
        Ink->AddRect(At(20, 65), At(69, 114), Ink255(0xffffff, 8), 16 * Scale);
        {
            const char *TitleText = Entry ? Entry->Name.c_str() : "Material";
            const float TitleSize = (Entry && Entry->Name.size() > 22 ? 15.f : 18.f) * Scale;
            Ink->PushClipRect(At(80, 65), ImVec2(Origin.x + Width - 46 * Scale, At(0, 102).y), true);
            ConstructMaterialCaption(At(80, 71).x, At(80, 71).y, TitleSize, 0xdadacc, TitleText, static_cast<ImFont *>(HeadingTypeface));
            Ink->PopClipRect();
        }
        Ink->AddCircleFilled(At(81.5f, 102.5f), 1.5f * Scale, Ink255(0x8fad99));
        ConstructMaterialCaption(At(88, 100).x, At(88, 100).y, 7 * Scale, 0x777766, Entry ? Entry->Label.c_str() : "LAYERED MATERIAL");
        ImGui::PushFont(nullptr, 11 * 1.302f * Scale);
        const auto Selection = EncodeContentPath(Library.QuerySelection());
        if (ConstructMaterialPill("##Favourite", ImVec2(Origin.x + Width - 40 * Scale, At(0, 76).y), ImVec2(27 * Scale, 27 * Scale)))
        {
            if (!Favourites.erase(Selection)) Favourites.insert(Selection);
        }
        if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Save current material", "Mark this material as a favourite in the library.");
        ConstructIcon(IconSymbol::EditorFavourite, Origin.x + Width - 34 * Scale, At(0, 82).y, 15 * Scale, Favourites.contains(Selection) ? 0xc5d2aa : 0x858577);
        ImGui::PopFont();
    }

    // ── Tabs ─────────────────────────────────────────────────────────────────────────────────────
    {
        const ImVec2 BarMin = At(18, 131), BarMax = ImVec2(Origin.x + Width - 18 * Scale, At(0, 165).y);
        Ink->AddRectFilled(BarMin, BarMax, Ink255(0x0f0f0d), 999.f);
        Ink->AddRect(BarMin, BarMax, Ink255(0xffffff, 7), 999.f);
        const float TabW = (BarMax.x - BarMin.x - 6 * Scale) * .5f;
        const auto Tab = [&](int Mode, IconSymbol Symbol, float IconSize, const char *Caption, const std::string &Chip)
        {
            const ImVec2 Min(BarMin.x + 3 * Scale + Mode * TabW, BarMin.y + 3 * Scale);
            const ImVec2 Max(Min.x + TabW, Min.y + 28 * Scale);
            const bool Active = InspectorMode == uint32_t(Mode);
            ImGui::SetCursorScreenPos(Min);
            if (ImGui::InvisibleButton(Mode == 0 ? "##TabProperties" : "##TabLayers", ImVec2(TabW, 28 * Scale)))
            {
                InspectorMode = uint32_t(Mode);
                InspectorNotice.clear();
            }
            if (Active) Ink->AddRectFilled(Min, Max, Ink255(0x2a2a25), 999.f);
            const uint32_t Colour = Active ? 0xcccdb6 : 0x757567;
            float ContentW = IconSize * Scale + 6 * Scale + InspectorTextWidth(9, Caption, Scale);
            float ChipW = 0;
            if (!Chip.empty()) ChipW = InspectorTextWidth(7, Chip.c_str(), Scale) + 8 * Scale;
            if (ChipW > 0) ContentW += 3 * Scale + ChipW;
            float X = Min.x + (TabW - ContentW) * .5f;
            const float MidY = Min.y + 14 * Scale;
            ConstructIcon(Symbol, X, MidY - IconSize * Scale * .5f, IconSize * Scale, Colour);
            X += IconSize * Scale + 6 * Scale;
            Ink->AddText(ImGui::GetFont(), 9 * 1.302f * Scale, ImVec2(X, MidY - 9 * 1.302f * Scale * .5f), Ink255(Colour), Caption);
            X += InspectorTextWidth(9, Caption, Scale);
            if (ChipW > 0)
            {
                X += 3 * Scale;
                const ImVec2 ChipMin(X, MidY - 6 * Scale), ChipMax(X + ChipW, MidY + 6 * Scale);
                Ink->AddRectFilled(ChipMin, ChipMax, Ink255(0x30362f), 3 * Scale);
                Ink->AddText(ImGui::GetFont(), 7 * 1.302f * Scale, ImVec2(X + 4 * Scale, MidY - 7 * 1.302f * Scale * .5f), Ink255(0x96a392), Chip.c_str());
            }
        };
        Tab(0, IconSymbol::EditorSliders, 13, "Properties", "");
        Tab(1, IconSymbol::EditorLayers, 14, "Layers", std::to_string(Document.Layers.size()));
    }

    // ── Bottom bands measurement ───────────────────────────────────────────────────────────────
    // The variables dock is flex 0 1 auto with max-height 45% and min-height 65; the native
    // commit band is pinned directly above it (HTML has no staged-apply concept).
    const float FooterHeight = 75 * Scale;
    float DockHeight = 0;
    {
        float Content = 0;
        if (VariableDockOpen)
        {
            const float RowW = Width - 28 * Scale;
            if (Document.Variables.empty())
                Content += 11 * Scale + MeasureInspectorTextBlock(RowW, 9, 1.7f, "Use ◇ on a channel or generator input. Expose selected variables to Properties.", Scale) + 11 * Scale;
            for (const auto &Variable : Document.Variables)
            {
                Content += 11 * Scale + 20 * Scale + 4 * Scale + 10 * Scale + 11 * Scale;
                if (EditingVariableId == Variable.Identity)
                {
                    Content += Variable.Category == "colour" ? 64 * Scale : Variable.Category == "vector" ? 48 * Scale : 71 * Scale;
                    Content += 39 * Scale;
                    if (Variable.Exposure) Content += 150 * Scale;
                }
            }
            Content += 12 * Scale;
        }
        const float PanelHeight = Bottom - Origin.y;
        DockHeight = VariableDockOpen ? std::min(std::max(44 * Scale + Content, 65 * Scale), PanelHeight * .42f) : 44 * Scale;
        DockHeight = std::min(DockHeight, std::max(PanelHeight - 177 * Scale - FooterHeight - 120 * Scale, 44 * Scale));
    }
    if (!LoadFailure.empty()) DockHeight = 0;
    const bool ShowActions = Library.QueryModified() || !Library.QueryDiagnostic().empty();
    float ActionsHeight = 0;
    if (ShowActions)
    {
        // 📝 Constant-height reservation (two button rows + three note lines) so the Apply /
        // Revert / Save buttons never move when a status or diagnostic note appears or clears.
        const float FrameH = ImGui::GetFrameHeight();
        const float LineH = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), Width - 34 * Scale, 0, "Wg").y + 4 * Scale;
        ActionsHeight = std::min(10 * Scale + FrameH + 4 * Scale + FrameH + 4 * Scale + 3 * LineH + 8 * Scale, 170 * Scale);
    }
    const float ReservedBottom = FooterHeight + DockHeight + ActionsHeight;
    const float ScrollTop = At(0, 177).y;
    const float ScrollHeight = std::max(Bottom - ReservedBottom - ScrollTop, 24 * Scale);

    // ── Scrolling body ─────────────────────────────────────────────────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, ScrollTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15 * Scale, 15 * Scale));
    ImGui::BeginChild("Material properties", ImVec2(Width, ScrollHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    bool Changed = false, PaintChanged = false, ChildOpen = true;
    std::function<void()> ConstructDockRows;
    const float StartX = ImGui::GetCursorScreenPos().x;
    const float ContentW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
    if (!LoadFailure.empty())
    {
        ImGui::TextWrapped("%s", LoadFailure.c_str());
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        ChildOpen = false;
        // 📝 A broken draft keeps the commit actions reachable: Apply surfaces the parse
        // diagnostic and Revert is the only way back to the applied source.
        if (ShowActions)
        {
            const float ActionsTop = Bottom - FooterHeight - ActionsHeight;
            Ink->AddRectFilled(ImVec2(Origin.x, ActionsTop), ImVec2(Origin.x + Width, ActionsTop + ActionsHeight), Ink255(0x161616));
            Ink->AddLine(ImVec2(Origin.x, ActionsTop), ImVec2(Origin.x + Width, ActionsTop), Ink255(0xffffff, 10));
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + 17 * Scale, ActionsTop + 10 * Scale));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width - 34 * Scale);
            ConstructActions();
            ImGui::PopTextWrapPos();
        }
    }
    else
    {
    try
    {
        const auto Gap = [&](float Height) { ImGui::Dummy(ImVec2(1, Height)); };
        const auto AnchorX = [&](float X) { ImGui::SetCursorScreenPos(ImVec2(X, ImGui::GetCursorScreenPos().y)); };
        // 📝 Single-buffer text field: the focused field owns its member buffer, every other
        // field renders from a local copy so many inputs can share the panel safely.
        const auto TextField = [&](const std::string &Id, char *Member, size_t Capacity, std::string &Key,
            ImVec2 Position, float FieldW, const std::string &Current,
            const std::function<void(const std::string &)> &Commit)
        {
            const bool Mine = Key == Id;
            std::array<char, 512> Local{};
            char *Buffer = Member;
            size_t BufferCapacity = Capacity;
            if (!Mine)
            {
                std::snprintf(Local.data(), Local.size(), "%s", Current.c_str());
                Buffer = Local.data();
                BufferCapacity = Local.size();
            }
            ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
            const bool Edited = ConstructInspectorTextInput(("##" + Id).c_str(), Position, FieldW, Buffer, BufferCapacity, Scale);
            const bool Activated = ImGui::IsItemActivated(), Deactivated = ImGui::IsItemDeactivated();
            ImGui::PopFont();
            if (Edited)
            {
                if (!Mine) { Key = Id; std::snprintf(Member, Capacity, "%s", Local.data()); }
                Commit(Buffer);
            }
            else if (!Mine && Activated) { Key = Id; std::snprintf(Member, Capacity, "%s", Local.data()); }
            if (Mine && Deactivated) Key.clear();
        };
        const auto SelectField = [&](const std::string &Id, ImVec2 Position, float FieldW,
            const std::vector<InspectorSelectOption> &Options, const std::string &Current, bool RecipeStyle,
            const std::function<void(const std::string &)> &Commit)
        {
            int Selection = 0;
            for (size_t Index = 0; Index < Options.size(); ++Index)
                if (!Options[Index].GroupHeader && Options[Index].Value == Current) Selection = int(Index);
            if (ConstructInspectorSelect(("##" + Id).c_str(), Position, FieldW, Options, Selection, Scale, RecipeStyle))
                Commit(Options[size_t(Selection)].Value);
        };
        // 📝 The recipe coefficient write path, salvaged verbatim from the previous inspector:
        // emission, the paint block, shared procedural pools (with the ORM ceiling lift and the
        // live sheen mirror), then stratum scalars. Sets Changed/ProceduralDirty like before.
        const auto QueryLiveCoefficient = [&](std::string_view Key) -> std::optional<float>
        {
            if (const auto Coeff = QueryRecipeCoefficient(Definition, Key)) return Coeff;
            if (const auto Sand = Library.QuerySandParameter(Key)) return float(*Sand);
            return std::nullopt;
        };
        const auto ApplyCoefficient = [&](const std::string &Key, float Coefficient)
        {
            if (Key == "emissionStrength")
            {
                Changed = Library.ConfigureEmissionStrength(Coefficient) || Changed;
                return;
            }
            if (Definition.Scattering.Paint.Configuration[3] && Key == "flakeLayers")
            {
                Definition.Scattering.Paint.Configuration[1] = uint32_t(std::clamp(std::round(Coefficient), 1.f, 4.f));
                PaintChanged = true;
                return;
            }
            if (Definition.Scattering.Paint.Configuration[3] && QueryPaintCoefficient(Definition.Scattering.Paint, Key))
            {
                *QueryPaintCoefficient(Definition.Scattering.Paint, Key) = Coefficient;
                PaintChanged = true;
                return;
            }
            if (Library.QuerySandParameter(Key))
            {
                if (Library.ConfigureSandParameter(Key, Coefficient))
                {
                    Changed = true;
                    ProceduralDirty = true;
                }
                return;
            }
            if (const auto Pool = QueryRecipePoolLocation(Definition, Key))
            {
                Changed = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, Key, Coefficient) || Changed;
                if (Key == "roughness")
                {
                    float Lift = .3f;
                    for (const auto &PoolEntry : Definition.StratumChannels[Pool->Stratum])
                        if (PoolEntry.Name == Pool->Channel) Lift = QueryProceduralOpCeilingLift(PoolEntry.Op);
                    Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "Roughness", std::min(1.f, Coefficient + Lift)) || Changed;
                }
                if (Key == "sheenRoughness")
                    Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "SheenRoughness", Coefficient) || Changed;
                if (Key == "sheen")
                    Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "SheenIntensity", Coefficient) || Changed;
                ProceduralDirty = true;
                return;
            }
            if (Key == "depth" && Definition.Scattering.Illumination.Configuration[0] > 1 &&
                Definition.Scattering.Illumination.Strata[1].Classification[0] == 1)
            {
                Changed = Library.ConfigureStratumCoefficient(1, "Thickness", std::clamp(Coefficient, 0.f, 1.f)) || Changed;
                return;
            }
            const uint32_t Index = Key.starts_with("coat") ? 1 : 0;
            const char *Field = Key == "roughness" || Key == "coatRoughness" ? "Roughness" :
                Key == "metalness" ? "Metallic" : Key == "ior" || Key == "coatIor" ? "Ior" :
                Key == "coat" ? "Coverage" : Key == "specular" ? "Specular" :
                Key == "heightStrength" ? "HeightStrength" :
                Key == "normalStrength" ? "NormalStrength" : "Anisotropy";
            Changed = Library.ConfigureStratumCoefficient(Index, Field,
                std::string_view(Field) == "Roughness" ? std::max(.045f, Coefficient) : Coefficient) || Changed;
        };
        // 📝 Colour homes, also salvaged: live sheen tint, emission radiance, sand colours, a
        // procedural text that owns the key, and the stratum albedo for everything else.
        const auto ResolveTextKey = [&](const std::string &Key) -> std::string
        {
            for (const auto &StratumChannels : Definition.StratumChannels)
                for (const auto &Channel : StratumChannels)
                    if (!Channel.Op.empty() && Channel.Texts.contains(Key)) return Key;
            const std::string Alias = Key == "warpColor" ? "colour0" : Key == "weftColor" ? "colour1" : "";
            if (!Alias.empty())
                for (const auto &StratumChannels : Definition.StratumChannels)
                    for (const auto &Channel : StratumChannels)
                        if (!Channel.Op.empty() && Channel.Texts.contains(Alias)) return Alias;
            return Key;
        };
        const auto FindTextOwner = [&](const std::string &Key) -> const MaterialProceduralChannel *
        {
            const std::string Target = ResolveTextKey(Key);
            for (const auto &StratumChannels : Definition.StratumChannels)
                for (const auto &Channel : StratumChannels)
                    if (!Channel.Op.empty() && Channel.Texts.contains(Target)) return &Channel;
            return nullptr;
        };
        const auto ReadColourHome = [&](const std::string &Key) -> std::string
        {
            if (Key == "emissionColor")
            {
                const auto &Rad = Definition.Scattering.RadianceNormal;
                const float Peak = std::max({Rad[0], Rad[1], Rad[2]});
                if (Peak > 0.f)
                    return EncodeHexColour({LinearToSrgbChannel(Rad[0] / Peak), LinearToSrgbChannel(Rad[1] / Peak), LinearToSrgbChannel(Rad[2] / Peak)});
                return "#ffffff";
            }
            if (const auto SandHex = Library.QuerySandColour(Key)) return *SandHex;
            if (Key == "sheenColor" && Definition.Scattering.Illumination.Configuration[0] > 0)
            {
                const auto &Sheen = Definition.Scattering.Illumination.Strata[std::min(InspectorLayerContext, Definition.Scattering.Illumination.Configuration[0] - 1)].Sheen;
                return EncodeHexColour({LinearToSrgbChannel(Sheen[0]), LinearToSrgbChannel(Sheen[1]), LinearToSrgbChannel(Sheen[2])});
            }
            const std::string Target = ResolveTextKey(Key);
            if (const auto *Owner = FindTextOwner(Key)) return Owner->Texts.at(Target);
            if (Definition.Scattering.Illumination.Configuration[0] > 0)
            {
                const auto &Albedo = Definition.Scattering.Illumination.Strata[0].AlbedoRoughness;
                return EncodeHexColour({LinearToSrgbChannel(Albedo[0]), LinearToSrgbChannel(Albedo[1]), LinearToSrgbChannel(Albedo[2])});
            }
            return "#ffffff";
        };
        const auto WriteColourHome = [&](const std::string &Key, const std::string &Hex)
        {
            std::array<float, 3> Srgb{1, 1, 1};
            if (!ParseHexColour(Hex, Srgb)) return;
            std::array<float, 3> Linear{};
            for (size_t Channel = 0; Channel < 3; ++Channel) Linear[Channel] = SrgbToLinearChannel(Srgb[Channel]);
            bool Written = false;
            const std::string Target = ResolveTextKey(Key);
            if (Key == "emissionColor") Written = Library.ConfigureEmissionColour(Linear);
            else if (Library.QuerySandColour(Key))
            {
                Written = Library.ConfigureSandColour(Key, Hex);
                if (Written) ProceduralDirty = true;
            }
            else if (Key == "sheenColor") Written = Library.ConfigureStratumColour(InspectorLayerContext, Linear, "SheenColor");
            else if (FindTextOwner(Key))
            {
                for (uint32_t Stratum = 0; Stratum < Definition.Scattering.Illumination.Configuration[0] && !Written; ++Stratum)
                    for (const auto &Channel : Definition.StratumChannels[Stratum])
                        if (!Channel.Op.empty() && Channel.Texts.contains(Target))
                            Written = Library.ConfigureProceduralText(Stratum, Channel.Name, Target, Hex);
                if (Written) ProceduralDirty = true;
            }
            else Written = Library.ConfigureStratumColour(0, Linear);
            Changed = Written || Changed;
        };
        const auto ReadQuantity = [&](const std::string &Address) -> std::optional<MaterialAuthoringQuantity>
        {
            try { return ReadMaterialParameter(Document, AuthoringStore, Address); }
            catch (const std::exception &) { return std::nullopt; }
        };
        // 📝 Linked values live at their variable's home address; after a value-bearing edit,
        // push the resolved quantity into every routable home so the preview follows.
        const auto PropagateLinks = [&]()
        {
            const auto Route = [&](const std::string &Address)
            {
                const auto Quantity = ReadQuantity(Address);
                if (!Quantity) return;
                if (const auto *Scalar = std::get_if<double>(&*Quantity))
                {
                    if (QueryLiveCoefficient(Address)) ApplyCoefficient(Address, float(*Scalar));
                }
                else if (const auto *Text = std::get_if<std::string>(&*Quantity))
                {
                    if (Text->size() == 7 && (*Text)[0] == '#' &&
                        (Address == "color" || Address == "sheenColor" || Address == "emissionColor" || Library.QuerySandColour(Address) || FindTextOwner(Address)))
                        WriteColourHome(Address, *Text);
                }
            };
            for (const auto &Variable : Document.Variables) Route(Variable.Address);
            for (const auto &[Address, Reference] : Document.Links) Route(Address);
        };
        // 📝 Every document mutation normalizes, persists through the library, and restores the
        // previous document on rejection — exactly Alloy's Change() gate.
        const auto EditAuthoring = [&](const std::function<void(MaterialAuthoringDocument &)> &Mutate, bool Propagate = false)
        {
            const MaterialAuthoringDocument Backup = Document;
            try
            {
                Mutate(Document);
                NormalizeMaterialAuthoring(Document);
            }
            catch (const std::exception &Error)
            {
                Document = Backup;
                InspectorNotice = Error.what();
                return;
            }
            if (!Library.ConfigureAuthoring(Document))
            {
                Document = Backup;
                InspectorNotice = Library.QueryDiagnostic().empty() ? std::string("Those authoring values are not supported by this format yet.") : Library.QueryDiagnostic();
                return;
            }
            AuthoringSelectionKey = SourceKey();
            Changed = true;
            if (Propagate) PropagateLinks();
        };
        // 📝 The single quantity write path (Alloy's WriteMaterialParameter): document
        // addresses and linked addresses persist the document; flat values route into the
        // live definition where a home exists and always land in the store.
        const auto WriteQuantity = [&](const std::string &Address, const MaterialAuthoringQuantity &Quantity)
        {
            std::string Resolved = Address;
            try { Resolved = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
            if (Resolved.find(':') != std::string::npos || Document.Links.contains(Address))
            {
                EditAuthoring([&](MaterialAuthoringDocument &Doc) { WriteMaterialParameter(Doc, AuthoringStore, Address, Quantity); }, true);
                return;
            }
            if (const auto *Scalar = std::get_if<double>(&Quantity))
            {
                if (QueryLiveCoefficient(Address)) ApplyCoefficient(Address, float(*Scalar));
                AuthoringStore[Address] = Quantity;
            }
            else if (const auto *Text = std::get_if<std::string>(&Quantity))
            {
                if (Text->size() == 7 && (*Text)[0] == '#')
                {
                    bool ColourKey = Address == "color" || Address == "sheenColor" || Address == "emissionColor" ||
                        Library.QuerySandColour(Address).has_value() || FindTextOwner(Address) != nullptr;
                    if (!ColourKey && Profile)
                        for (const auto &[Key, Label] : Profile->Colours) ColourKey = ColourKey || Key == Address;
                    if (ColourKey) WriteColourHome(Address, *Text);
                }
                else if (FindTextOwner(Address))
                {
                    bool Written = false;
                    for (uint32_t Stratum = 0; Stratum < Definition.Scattering.Illumination.Configuration[0] && !Written; ++Stratum)
                        for (const auto &Channel : Definition.StratumChannels[Stratum])
                            if (!Channel.Op.empty() && Channel.Texts.contains(Address))
                                Written = Library.ConfigureProceduralText(Stratum, Channel.Name, Address, *Text);
                    if (Written) { Changed = true; ProceduralDirty = true; }
                }
                AuthoringStore[Address] = Quantity;
            }
            else AuthoringStore[Address] = Quantity;
        };

        // ── Recipe sections ────────────────────────────────────────────────────────────────────
        const float FieldX = StartX + 17 * Scale;
        const float FieldW = ContentW - 34 * Scale;
        auto BeginSection = [&](const std::string &SectionId, IconSymbol Symbol, const char *Title, const std::string &Tag, bool &Closed) -> float
        {
            const std::string FoldKey = "section:" + SectionId;
            Closed = FoldedInspectorCards.contains(FoldKey);
            auto *Body = ImGui::GetWindowDrawList();
            Body->ChannelsSplit(2);
            Body->ChannelsSetCurrent(1);
            ImGui::PushID(SectionId.c_str());
            AnchorX(StartX);
            const float Top = ImGui::GetCursorScreenPos().y;
            ImGui::SetCursorScreenPos(ImVec2(StartX, Top));
            if (ImGui::InvisibleButton("##SectionTitle", ImVec2(ContentW, 32 * Scale)))
            {
                if (!FoldedInspectorCards.erase(FoldKey)) FoldedInspectorCards.insert(FoldKey);
                Closed = !Closed;
            }
            ConstructIcon(Symbol, FieldX, Top + 17 * Scale, 13 * Scale, 0x8e9b83);
            ConstructMaterialCaption(FieldX + 20 * Scale, Top + 18 * Scale, 11 * Scale, 0xcacabd, Title);
            float RightX = StartX + ContentW - 17 * Scale - 13 * Scale;
            ConstructIcon(Closed ? IconSymbol::EditorChevronRight : IconSymbol::EditorChevronDown, RightX, Top + 17 * Scale, 13 * Scale, 0x858577);
            if (!Tag.empty())
            {
                const float ChipW = InspectorTextWidth(6, Tag.c_str(), Scale) + 8 * Scale;
                RightX -= 3 * Scale + ChipW;
                const ImVec2 ChipMin(RightX, Top + 17.5f * Scale), ChipMax(RightX + ChipW, ChipMin.y + 12 * Scale);
                Body->AddRectFilled(ChipMin, ChipMax, Ink255(0x2c3034), 3 * Scale);
                Body->AddRect(ChipMin, ChipMax, Ink255(0x3c4448), 3 * Scale);
                Body->AddText(ImGui::GetFont(), 6 * 1.302f * Scale, ImVec2(ChipMin.x + 4 * Scale, ChipMin.y + 6 * Scale - 6 * 1.302f * Scale * .5f), Ink255(0x92a0a6), Tag.c_str());
            }
            ImGui::SetCursorScreenPos(ImVec2(FieldX, Top + 32 * Scale));
            if (!Closed) Gap(12 * Scale);
            return Top;
        };
        auto EndSection = [&](float Top)
        {
            auto *Body = ImGui::GetWindowDrawList();
            Gap(17 * Scale);
            const float EndY = ImGui::GetCursorScreenPos().y;
            Body->ChannelsSetCurrent(0);
            DrawInspectorGradient(ImVec2(StartX, Top), ImVec2(StartX + ContentW, EndY), {0x242424, 0x1f1f1f}, 23, Scale);
            Body->AddRect(ImVec2(StartX, Top), ImVec2(StartX + ContentW, EndY), Ink255(0xffffff, 13), 23 * Scale);
            Body->ChannelsMerge();
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(StartX, EndY));
            Gap(15 * Scale);
        };
        const auto HelpText = [&](const char *Text, float X, float W)
        {
            const float Height = ConstructInspectorTextBlock(ImVec2(X, ImGui::GetCursorScreenPos().y), W, 8, 1.8f, 0x7b7b6b, Text, Scale);
            Gap(Height);
        };
        const auto RenderColourField = [&](const std::string &Key, const std::string &Label)
        {
            ImGui::PushID(Key.c_str());
            AnchorX(FieldX);
            const float TopY = ImGui::GetCursorScreenPos().y;
            ConstructMaterialCaption(FieldX, TopY + 3 * Scale, 10 * Scale, 0xa7acb5, Label.c_str());
            const float SrgbW = InspectorTextWidth(7, "sRGB", Scale);
            auto *Body = ImGui::GetWindowDrawList();
            Body->AddText(ImGui::GetFont(), 7 * 1.302f * Scale, ImVec2(FieldX + FieldW - SrgbW, TopY + 4 * Scale), Ink255(0x727e88), "sRGB");
            std::string Hex = ReadColourHome(Key);
            if (const auto Quantity = ReadQuantity(Key); Quantity && std::holds_alternative<std::string>(*Quantity))
                Hex = std::get<std::string>(*Quantity);
            std::array<float, 3> Srgb{1, 1, 1};
            (void)ParseHexColour(Hex, Srgb);
            const ImVec2 BarMin(FieldX, TopY + 22 * Scale), BarMax(BarMin.x + FieldW, BarMin.y + 36 * Scale);
            Body->AddRectFilled(BarMin, BarMax, Ink255(0x1b1d20), 8 * Scale);
            Body->AddRect(BarMin, BarMax, Ink255(0x373b42), 8 * Scale);
            if (ConstructInspectorColourSwatch("##ColourSwatch", ImVec2(BarMin.x + 6 * Scale, BarMin.y + 6.5f * Scale), ImVec2(25 * Scale, 23 * Scale), Srgb, 5, Scale))
                WriteQuantity(Key, EncodeHexColour(Srgb));
            // 📝 The hex is a borderless text input inside the bar (Alloy's ColorField), sharing
            // the single hex buffer keyed by this field's identity.
            {
                const std::string HexId = "colourhex:" + Key;
                const bool Mine = InspectorHexKey == HexId;
                std::array<char, 32> Local{};
                char *Buffer = InspectorHexText.data();
                if (!Mine)
                {
                    std::string Upper = Hex;
                    for (auto &Letter : Upper) Letter = static_cast<char>(std::toupper(static_cast<unsigned char>(Letter)));
                    std::snprintf(Local.data(), Local.size(), "%s", Upper.c_str());
                    Buffer = Local.data();
                }
                ImGui::PushFont(static_cast<ImFont *>(HeadingTypeface), 10 * 1.302f * Scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Ink255(0xc7ced5)));
                ImGui::SetCursorScreenPos(ImVec2(BarMin.x + 41 * Scale, BarMin.y + 11 * Scale));
                ImGui::SetNextItemWidth(FieldW - 41 * Scale - 30 * Scale);
                const bool Edited = ImGui::InputText("##ColourHex", Buffer, Mine ? InspectorHexText.size() : Local.size());
                const bool Activated = ImGui::IsItemActivated(), Deactivated = ImGui::IsItemDeactivated();
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar(2);
                ImGui::PopFont();
                if (Edited)
                {
                    if (!Mine) { InspectorHexKey = HexId; std::snprintf(InspectorHexText.data(), InspectorHexText.size(), "%s", Local.data()); }
                    std::array<float, 3> Parsed{};
                    if (ParseHexColour(Buffer, Parsed)) WriteQuantity(Key, EncodeHexColour(Parsed));
                }
                else if (!Mine && Activated) { InspectorHexKey = HexId; std::snprintf(InspectorHexText.data(), InspectorHexText.size(), "%s", Local.data()); }
                if (Mine && Deactivated) InspectorHexKey.clear();
            }
            ConstructIcon(IconSymbol::SlateColorPicker, BarMax.x - 8 * Scale - 13 * Scale, BarMin.y + 11.5f * Scale, 13 * Scale, 0x777f89);
            ImGui::SetCursorScreenPos(ImVec2(FieldX, TopY + 58 * Scale));
            Gap(16 * Scale);
            ImGui::PopID();
        };
        // 📝 DM Sans has no ◇ (U+25C7) or → (U+2192) glyphs, so the two Alloy sentences that
        // use them render through this word-wrap with the vector glyph drawn in place.
        const auto GlyphTextBlock = [&](ImVec2 Position, float W, float Size, float LineHeight, uint32_t Rgb, const std::string &Text) -> float
        {
            const size_t Dia = Text.find("◇");
            const size_t Arrow = Text.find("→");
            const size_t Token = Dia != std::string::npos ? Dia : Arrow;
            if (Token == std::string::npos)
                return ConstructInspectorTextBlock(Position, W, Size, LineHeight, Rgb, Text.c_str(), Scale);
            const bool IsArrow = Dia == std::string::npos;
            const std::string Marked = Text.substr(0, Token) + "\x01" + Text.substr(Token + 3);
            auto *Body = ImGui::GetWindowDrawList();
            ImFont *Font = ImGui::GetFont();
            const float FontSize = Size * 1.302f * Scale;
            const float Step = Size * LineHeight * Scale;
            const float TokenW = Size * 1.1f * Scale;
            const float SpaceW = Font->CalcTextSizeA(FontSize, FLT_MAX, 0, " ").x;
            const ImU32 Colour = Ink255(Rgb, unsigned(255 * ImGui::GetStyle().Alpha));
            const auto SegmentWidth = [&](size_t Begin, size_t End) -> float
            {
                float Width = 0;
                size_t Run = Begin;
                for (size_t Index = Begin; Index <= End; ++Index)
                    if (Index == End || Marked[Index] == '\x01')
                    {
                        if (Index > Run) Width += Font->CalcTextSizeA(FontSize, FLT_MAX, 0, Marked.c_str() + Run, Marked.c_str() + Index).x;
                        if (Index < End) Width += TokenW;
                        Run = Index + 1;
                    }
                return Width;
            };
            float PenX = Position.x, PenY = Position.y;
            size_t Cursor = 0;
            while (Cursor < Marked.size())
            {
                while (Cursor < Marked.size() && Marked[Cursor] == ' ') ++Cursor;
                if (Cursor >= Marked.size()) break;
                size_t End = Marked.find(' ', Cursor);
                if (End == std::string::npos) End = Marked.size();
                const float WordW = SegmentWidth(Cursor, End);
                if (PenX > Position.x && PenX + WordW > Position.x + W) { PenX = Position.x; PenY += Step; }
                size_t Run = Cursor;
                for (size_t Index = Cursor; Index <= End; ++Index)
                    if (Index == End || Marked[Index] == '\x01')
                    {
                        if (Index > Run)
                        {
                            Body->AddText(Font, FontSize, ImVec2(PenX, PenY - Size * .15f * Scale), Colour, Marked.c_str() + Run, Marked.c_str() + Index);
                            PenX += Font->CalcTextSizeA(FontSize, FLT_MAX, 0, Marked.c_str() + Run, Marked.c_str() + Index).x;
                        }
                        if (Index < End)
                        {
                            const ImVec2 Centre(PenX + TokenW * .5f, PenY + Size * .42f * Scale);
                            if (IsArrow) DrawInspectorRightArrowGlyph(Centre, Size * .95f, Rgb, Scale);
                            else DrawInspectorDiamondGlyph(Centre, Size * .8f, Rgb, Scale);
                            PenX += TokenW;
                        }
                        Run = Index + 1;
                    }
                PenX += SpaceW;
                Cursor = End;
            }
            return (PenY - Position.y) + Step;
        };
        // 📝 Finish colours come from the profile; any recipe bundling a "sheen" target also
        // gets the live sheen tint (salvaged rule).
        std::vector<std::pair<std::string, std::string>> Colours;
        if (Profile) Colours = Profile->Colours;
        {
            const bool SupportsSheen = std::any_of(Controls.begin(), Controls.end(), [](const MaterialRecipeControl &Control)
            {
                return Control.Key == "sheen" || std::any_of(Control.Targets.begin(), Control.Targets.end(),
                    [](const MaterialRecipeTarget &Target) { return Target.Key == "sheen"; });
            });
            if (SupportsSheen && std::none_of(Colours.begin(), Colours.end(),
                [](const auto &Colour) { return Colour.first == "sheenColor"; }))
                Colours.emplace_back("sheenColor", "Sheen colour");
        }
        // 📝 Each control's stratum (shared pool, then paint, then the coat prefix) drives the
        // extent recording the headless harness reads, exactly like the layered cards did.
        const auto QueryKeyStratum = [&](const std::string &Key) -> uint32_t
        {
            if (const auto Pool = QueryRecipePoolLocation(Definition, Key)) return Pool->Stratum;
            if (Definition.Scattering.Paint.Configuration[3] &&
                (Key == "flakeLayers" || QueryPaintCoefficient(Definition.Scattering.Paint, Key))) return 0;
            return Key.starts_with("coat") ? 1u : 0u;
        };
        const auto RenderControl = [&](const MaterialRecipeControl &Control)
        {
            uint32_t Stratum = 0;
            if (Control.Direct) Stratum = QueryKeyStratum(Control.Key);
            for (const auto &Target : Control.Targets) Stratum = std::max(Stratum, QueryKeyStratum(Target.Key));
            AnchorX(FieldX);
            InspectorLayerContext = Stratum;
            float Position = Control.Position;
            bool Supported = Control.Direct
                ? QueryLiveCoefficient(Control.Key).has_value()
                : !Control.Targets.empty() || QueryLiveCoefficient(Control.Key).has_value();
            for (const auto &Target : Control.Targets) Supported = QueryLiveCoefficient(Target.Key).has_value() && Supported;
            // 📝 A linked address stays editable even without a coefficient home: the value
            // lives at the variable's address in the store.
            if (!Supported && !Control.Key.empty() && ReadQuantity(Control.Key)) Supported = true;
            if (Supported && Control.Direct)
                Position = QueryLiveCoefficient(Control.Key).value_or(
                    [&]() { const auto Q = ReadQuantity(Control.Key); return Q && std::holds_alternative<double>(*Q) ? float(std::get<double>(*Q)) : Control.Position; }());
            else if (Supported && !Control.Targets.empty())
            {
                const auto &Target = Control.Targets.front();
                const float Coefficient = *QueryLiveCoefficient(Target.Key);
                Position = std::abs(Target.Maximum - Target.Minimum) > .000001f ?
                    std::clamp((Coefficient - Target.Minimum) / (Target.Maximum - Target.Minimum), 0.f, 1.f) : Control.Position;
            }
            else if (Control.Direct) Position = QueryLiveCoefficient(Control.Key).value_or(Control.Position);
            float NativeMinimum = Control.Minimum, NativeMaximum = Control.Maximum;
            if (Control.Extend)
            {
                const float Held = QueryLiveCoefficient(Control.Key).value_or(Control.Minimum);
                NativeMaximum = std::max(NativeMaximum, Held);
                if (Held > 0) NativeMinimum = std::min(NativeMinimum, Held);
            }
            bool RoughnessLimited = false;
            for (const auto &Target : Control.Targets)
                if (Target.Key == "roughness" || Target.Key == "coatRoughness")
                {
                    if (Control.Direct) NativeMinimum = std::max(NativeMinimum, .045f);
                    else if (std::abs(Target.Maximum - Target.Minimum) > .000001f)
                    {
                        const float Boundary = (.045f - Target.Minimum) / (Target.Maximum - Target.Minimum);
                        if (Target.Maximum > Target.Minimum) NativeMinimum = std::max(NativeMinimum, Boundary);
                        else NativeMaximum = std::min(NativeMaximum, Boundary);
                    }
                    RoughnessLimited = NativeMinimum != Control.Minimum || NativeMaximum != Control.Maximum;
                }
            Supported = Supported && NativeMinimum <= NativeMaximum;
            ImGui::BeginDisabled(!Supported);
            const char *Unavailable = !Supported ? "This recipe parameter is retained, but no native parameter is bound to it yet." :
                RoughnessLimited ? "The native preview supports roughness of 0.045 or higher; this control is bounded to valid physical values." : nullptr;
            if (ConstructProperty(Control.Caption.c_str(), NativeMinimum, NativeMaximum, Position,
                Control.Direct ? (Control.Unit.empty() ? "" : Control.Unit.c_str()) : "%", Control.Logarithmic, Control.Step, Unavailable, FieldW, true))
            {
                if (Control.Direct)
                {
                    WriteQuantity(Control.Key, double(Position));
                }
                else if (!Control.Key.empty() && Document.Links.contains(Control.Key))
                {
                    WriteQuantity(Control.Key, double(Position));
                }
                else
                    for (const auto &Target : Control.Targets)
                    {
                        const float Coefficient = Target.Minimum + Position * (Target.Maximum - Target.Minimum);
                        WriteQuantity(Target.Key, double(Target.Integer ? std::round(Coefficient) : Coefficient));
                    }
                if (PaintChanged) { Changed = Library.ConfigurePaint(Definition.Scattering.Paint) || Changed; PaintChanged = false; }
            }
            ImGui::EndDisabled();
            InspectorLayerContext = 0;
            if (!Control.Hint.empty() && Supported)
            {
                Gap(14 * Scale);
                HelpText(Control.Hint.c_str(), FieldX, FieldW);
            }
        };
        const auto RenderGroup = [&](const char *Group)
        {
            bool First = true;
            for (const auto &Control : Controls)
                if (Control.Group == Group)
                {
                    if (!First) Gap(21 * Scale);
                    First = false;
                    RenderControl(Control);
                }
            return !First;
        };
        const auto HasGroup = [&](const char *Group)
        {
            return std::any_of(Controls.begin(), Controls.end(),
                [&](const MaterialRecipeControl &Control) { return Control.Group == Group; });
        };
        // 📝 The flake colour ramp (Alloy ColorRamp over AutomotivePaint::Palette): mode
        // segmented control, ramp preview with stop chips, the swatch grid, the selected-stop
        // row, a position slider in ramp mode and the palette presets.
        const auto RenderFlakeRamp = [&](float X, float W, bool RecipeForm)
        {
            auto &Paint = Definition.Scattering.Paint;
            if (!Paint.Configuration[3]) return;
            const uint32_t PaletteCount = std::clamp(Paint.Configuration[3], 1u, uint32_t(Paint.Palette.size()));
            FlakeColourSelection = std::min(FlakeColourSelection, PaletteCount - 1);
            const bool RampMode = Paint.Configuration[2] == 1;
            auto *Body = ImGui::GetWindowDrawList();
            AnchorX(X);
            const float TopY = ImGui::GetCursorScreenPos().y;
            ConstructMaterialCaption(X, TopY + 3 * Scale, 10 * Scale, 0xa7acb5, "Flake color distribution");
            char CountLabel[16];
            std::snprintf(CountLabel, sizeof CountLabel, "%u/12", PaletteCount);
            const float CountW = InspectorTextWidth(8, CountLabel, Scale);
            Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(X + W - CountW, TopY + 3 * Scale), Ink255(0x819091), CountLabel);
            // Segmented mode control.
            const ImVec2 SegMin(X, TopY + 24 * Scale), SegMax(X + W, SegMin.y + 28 * Scale);
            Body->AddRectFilled(SegMin, SegMax, Ink255(0x191c1f), 8 * Scale);
            Body->AddRect(SegMin, SegMax, Ink255(0x383d44), 8 * Scale);
            static constexpr std::array<std::pair<const char *, uint32_t>, 3> Modes{{{"Palette", 0}, {"Color ramp", 1}, {"Single", 2}}};
            const float SegW = (W - 6 * Scale - 6 * Scale) / 3;
            for (size_t Index = 0; Index < Modes.size(); ++Index)
            {
                const ImVec2 Min(SegMin.x + 3 * Scale + Index * (SegW + 3 * Scale), SegMin.y + 3 * Scale);
                const ImVec2 Max(Min.x + SegW, Min.y + 22 * Scale);
                const bool Active = Paint.Configuration[2] == Modes[Index].second;
                ImGui::SetCursorScreenPos(Min);
                ImGui::PushID(int(Index) + 700);
                if (ImGui::InvisibleButton("##FlakeMode", ImVec2(SegW, 22 * Scale)) && !Active)
                {
                    Paint.Configuration[2] = Modes[Index].second;
                    Changed = Library.ConfigurePaint(Paint) || Changed;
                }
                ImGui::PopID();
                if (Active) Body->AddRectFilled(Min, Max, Ink255(0x3b4544), 5 * Scale);
                const float TextW = InspectorTextWidth(8, Modes[Index].first, Scale);
                Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(Min.x + (SegW - TextW) * .5f, Min.y + 11 * Scale - 8 * 1.302f * Scale * .5f),
                    Ink255(Active ? 0xdeebe4 : 0x808e9b), Modes[Index].first);
            }
            float RowY = SegMax.y;
            const auto StopSrgb = [&](uint32_t Index) -> std::array<float, 3>
            {
                const auto &Stop = Paint.Palette[Index];
                return {LinearToSrgbChannel(Stop[0]), LinearToSrgbChannel(Stop[1]), LinearToSrgbChannel(Stop[2])};
            };
            if (RampMode)
            {
                RowY += 18 * Scale;
                const ImVec2 BarMin(X + 6 * Scale, RowY), BarMax(X + W - 6 * Scale, RowY + 21 * Scale);
                std::vector<uint32_t> Stops;
                for (uint32_t Index = 0; Index < PaletteCount; ++Index)
                {
                    const auto Srgb = StopSrgb(Index);
                    Stops.push_back((uint32_t(std::lround(Srgb[0] * 255)) << 16) | (uint32_t(std::lround(Srgb[1] * 255)) << 8) | uint32_t(std::lround(Srgb[2] * 255)));
                }
                DrawInspectorGradient(BarMin, BarMax, Stops, 5, Scale, 255, 0x1f1f1f);
                Body->AddRect(BarMin, BarMax, Ink255(0xffffff, 36), 5 * Scale);
                for (uint32_t Index = 0; Index < PaletteCount; ++Index)
                {
                    const float Cx = BarMin.x + Paint.Palette[Index][3] * (BarMax.x - BarMin.x);
                    const ImVec2 ChipMin(Cx - 5 * Scale, BarMax.y - 5 * Scale), ChipMax(Cx + 5 * Scale, ChipMin.y + 13 * Scale);
                    ImGui::SetCursorScreenPos(ChipMin);
                    ImGui::PushID(int(Index) + 800);
                    if (ImGui::InvisibleButton("##RampStop", ImVec2(10 * Scale, 13 * Scale))) FlakeColourSelection = Index;
                    ImGui::PopID();
                    const auto Srgb = StopSrgb(Index);
                    Body->AddRectFilled(ChipMin, ChipMax, IM_COL32(unsigned(Srgb[0] * 255), unsigned(Srgb[1] * 255), unsigned(Srgb[2] * 255), 255), 3 * Scale);
                    Body->AddRect(ChipMin, ChipMax, Ink255(Index == FlakeColourSelection ? 0xfafcf8 : 0x8e9aa2), 3 * Scale, 0, 2 * Scale);
                }
                RowY = BarMax.y + 24 * Scale;
            }
            else RowY += 12 * Scale;
            // Swatch grid: six per row, plus the dashed add tile.
            const float TileW = (W - 35 * Scale) / 6, TileH = 24 * Scale, PitchX = TileW + 7 * Scale, PitchY = TileH + 7 * Scale;
            const uint32_t Tiles = PaletteCount + (PaletteCount < 12 ? 1 : 0);
            for (uint32_t Index = 0; Index < Tiles; ++Index)
            {
                const ImVec2 Pos(X + (Index % 6) * PitchX, RowY + (Index / 6) * PitchY);
                if (Index < PaletteCount)
                {
                    auto Srgb = StopSrgb(Index);
                    ImGui::PushID(int(Index) + 900);
                    if (ConstructInspectorColourSwatch("##FlakeSwatch", Pos, ImVec2(TileW, TileH), Srgb, 5, Scale))
                    {
                        FlakeColourSelection = Index;
                        for (size_t Channel = 0; Channel < 3; ++Channel) Paint.Palette[Index][Channel] = SrgbToLinearChannel(Srgb[Channel]);
                        Changed = Library.ConfigurePaint(Paint) || Changed;
                    }
                    if (ImGui::IsItemClicked() || ImGui::IsItemActivated()) FlakeColourSelection = Index;
                    ImGui::PopID();
                    if (Index == FlakeColourSelection)
                        Body->AddRect(ImVec2(Pos.x - 2 * Scale, Pos.y - 2 * Scale), ImVec2(Pos.x + TileW + 2 * Scale, Pos.y + TileH + 2 * Scale), Ink255(0xc2d2bd), 5 * Scale, 0, 2 * Scale);
                }
                else
                {
                    ImGui::SetCursorScreenPos(Pos);
                    if (ImGui::InvisibleButton("##AddFlakeColour", ImVec2(TileW, TileH)))
                    {
                        float Previous = 0.f, BestGap = 0.f, BestPosition = 1.f;
                        for (uint32_t Probe = 0; Probe <= PaletteCount; ++Probe)
                        {
                            const float Current = Probe < PaletteCount ? Paint.Palette[Probe][3] : 1.f;
                            if (Current - Previous > BestGap) { BestGap = Current - Previous; BestPosition = (Current + Previous) * .5f; }
                            Previous = Current;
                        }
                        uint32_t InsertAt = PaletteCount;
                        for (uint32_t Probe = 0; Probe < PaletteCount; ++Probe)
                            if (Paint.Palette[Probe][3] > BestPosition) { InsertAt = Probe; break; }
                        for (uint32_t Probe = PaletteCount; Probe > InsertAt; --Probe) Paint.Palette[Probe] = Paint.Palette[Probe - 1];
                        std::array<float, 3> Added{1, 1, 1};
                        (void)ParseHexColour("#8ca6e0", Added);
                        Paint.Palette[InsertAt] = {SrgbToLinearChannel(Added[0]), SrgbToLinearChannel(Added[1]), SrgbToLinearChannel(Added[2]), BestPosition};
                        Paint.Configuration[3] = PaletteCount + 1;
                        FlakeColourSelection = InsertAt;
                        Changed = Library.ConfigurePaint(Paint) || Changed;
                    }
                    Body->AddRectFilled(Pos, ImVec2(Pos.x + TileW, Pos.y + TileH), Ink255(0x2c3338), 5 * Scale);
                    Body->AddRect(Pos, ImVec2(Pos.x + TileW, Pos.y + TileH), Ink255(0x737f86), 5 * Scale);
                    DrawInspectorPlusGlyph(ImVec2(Pos.x + TileW * .5f, Pos.y + TileH * .5f), 13, 0xb2c1bf, Scale);
                }
            }
            RowY += ((Tiles + 5) / 6) * PitchY + 8 * Scale;
            // Selected-stop row.
            {
                const uint32_t Selected = FlakeColourSelection;
                char Label[24];
                std::snprintf(Label, sizeof Label, "Color %u", Selected + 1);
                ConstructMaterialCaption(X, RowY + 6 * Scale, 8 * Scale, 0x94a2af, Label);
                const float HexW = 85 * Scale;
                const ImVec2 HexPos(X + W - HexW - 28 * Scale, RowY);
                const std::string CurrentHex = EncodeHexColour(StopSrgb(Selected));
                std::string Upper = CurrentHex;
                for (auto &Letter : Upper) Letter = static_cast<char>(std::toupper(static_cast<unsigned char>(Letter)));
                TextField("flakehex:" + std::to_string(Selected), InspectorHexText.data(), InspectorHexText.size(), InspectorHexKey,
                    HexPos, HexW, Upper, [&](const std::string &Text)
                    {
                        std::array<float, 3> Parsed{};
                        if (ParseHexColour(Text, Parsed))
                        {
                            for (size_t Channel = 0; Channel < 3; ++Channel) Paint.Palette[Selected][Channel] = SrgbToLinearChannel(Parsed[Channel]);
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        }
                    });
                const ImVec2 RemoveMin(X + W - 21 * Scale, RowY + 2 * Scale);
                ImGui::SetCursorScreenPos(RemoveMin);
                ImGui::BeginDisabled(PaletteCount <= 1);
                if (ImGui::InvisibleButton("##RemoveFlakeColour", ImVec2(21 * Scale, 21 * Scale)))
                {
                    const uint32_t Remove = std::min(FlakeColourSelection, PaletteCount - 1);
                    for (uint32_t Probe = Remove; Probe + 1 < PaletteCount; ++Probe) Paint.Palette[Probe] = Paint.Palette[Probe + 1];
                    Paint.Palette[PaletteCount - 1] = {};
                    Paint.Configuration[3] = PaletteCount - 1;
                    FlakeColourSelection = Remove > 0 ? Remove - 1 : 0;
                    Changed = Library.ConfigurePaint(Paint) || Changed;
                }
                ImGui::EndDisabled();
                DrawInspectorXGlyph(ImVec2(RemoveMin.x + 10.5f * Scale, RemoveMin.y + 10.5f * Scale), 13, 0x889ca9, Scale);
                RowY += 27 * Scale;
            }
            ImGui::SetCursorScreenPos(ImVec2(X, RowY));
            if (RampMode)
            {
                const uint32_t Selected = std::min(FlakeColourSelection, PaletteCount - 1);
                const float Minimum = Selected > 0 ? Paint.Palette[Selected - 1][3] : 0.f;
                const float Maximum = Selected + 1 < PaletteCount ? Paint.Palette[Selected + 1][3] : 1.f;
                float Position = Paint.Palette[Selected][3];
                Gap(15 * Scale);
                AnchorX(X);
                if (ConstructProperty("Stop position", Minimum, Maximum, Position, "%", false, 0, nullptr, W, RecipeForm))
                {
                    Paint.Palette[Selected][3] = Position;
                    Changed = Library.ConfigurePaint(Paint) || Changed;
                }
            }
            // Palette presets.
            {
                Gap(15 * Scale);
                AnchorX(X);
                const float PresetY = ImGui::GetCursorScreenPos().y;
                const auto &Palettes = FlakePalettes();
                FlakePalettePreset = std::min(FlakePalettePreset, uint32_t(Palettes.size() - 1));
                std::vector<InspectorSelectOption> Options;
                for (const auto &[Name, Hexes] : Palettes) Options.push_back({Name, Name, false});
                SelectField("flake-palette", ImVec2(X, PresetY), W, Options, Palettes[FlakePalettePreset].first, true,
                    [&](const std::string &Value)
                    {
                        for (size_t Index = 0; Index < Palettes.size(); ++Index)
                            if (Palettes[Index].first == Value)
                            {
                                FlakePalettePreset = uint32_t(Index);
                                const auto &Hexes = Palettes[Index].second;
                                for (size_t Stop = 0; Stop < Hexes.size(); ++Stop)
                                {
                                    std::array<float, 3> Srgb{};
                                    (void)ParseHexColour(Hexes[Stop], Srgb);
                                    Paint.Palette[Stop] = {SrgbToLinearChannel(Srgb[0]), SrgbToLinearChannel(Srgb[1]), SrgbToLinearChannel(Srgb[2]),
                                        float(Stop) / float(Hexes.size() - 1)};
                                }
                                Paint.Configuration[3] = uint32_t(Hexes.size());
                                FlakeColourSelection = 0;
                                Changed = Library.ConfigurePaint(Paint) || Changed;
                            }
                    });
                DrawInspectorSpectrumDot(ImVec2(X + W - 40 * Scale, PresetY + 16.5f * Scale), Scale);
                ImGui::SetCursorScreenPos(ImVec2(X, PresetY + 33 * Scale));
            }
            Gap(14 * Scale);
            HelpText(Paint.Configuration[2] == 1 ? "Each flake samples the ramp. Select a stop to edit its position." :
                Paint.Configuration[2] == 2 ? "The first color is used for every flake." :
                "Random, individual flake colors. Click a swatch to open the color picker.", X, W);
        };
        // ── Shared parameter controls (MaterialPanel's ParameterControl / VariableActions) ─────
        const auto DefaultFor = [&](const std::string &Category, double Minimum) -> MaterialAuthoringQuantity
        {
            if (Category == "colour") return std::string("#ffffff");
            if (Category == "vector") return std::array<double, 3>{0, 0, 1};
            return Minimum;
        };
        const auto RenderParameterControl = [&](const std::string &Address, const std::string &Caption, const std::string &Category,
            double Minimum, double Maximum, double Step, const std::string &Unit, float X, float W, bool RecipeForm = false)
        {
            ImGui::PushID(Address.c_str());
            const auto Quantity = ReadQuantity(Address);
            if (Category == "colour")
            {
                AnchorX(X);
                const float TopY = ImGui::GetCursorScreenPos().y;
                ConstructMaterialCaption(X, TopY + 3 * Scale, 9 * Scale, 0x999999, Caption.c_str());
                std::string Hex = "#ffffff";
                if (Quantity && std::holds_alternative<std::string>(*Quantity)) Hex = std::get<std::string>(*Quantity);
                std::array<float, 3> Srgb{1, 1, 1};
                (void)ParseHexColour(Hex, Srgb);
                if (ConstructInspectorColourSwatch("##ParamColour", ImVec2(X, TopY + 18 * Scale), ImVec2(W, 33 * Scale), Srgb, 7, Scale))
                    WriteQuantity(Address, EncodeHexColour(Srgb));
                ConstructMaterialCaption(X, TopY + 55 * Scale, 8 * Scale, 0x888888, Hex.c_str());
                ImGui::SetCursorScreenPos(ImVec2(X, TopY + 64 * Scale));
            }
            else if (Category == "vector")
            {
                std::array<double, 3> Vector{0, 0, 1};
                if (Quantity && std::holds_alternative<std::array<double, 3>>(*Quantity)) Vector = std::get<std::array<double, 3>>(*Quantity);
                AnchorX(X);
                const float TopY = ImGui::GetCursorScreenPos().y;
                const float ColW = (W - 12 * Scale) / 3;
                static constexpr const char *Axes[3] = {"X", "Y", "Z"};
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const float ColX = X + Axis * (ColW + 6 * Scale);
                    ConstructMaterialCaption(ColX, TopY + 3 * Scale, 9 * Scale, 0x999999, Axes[Axis]);
                    double Component = Vector[Axis];
                    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
                    const bool Edited = ConstructInspectorNumberInput(("##Vec" + std::to_string(Axis)).c_str(), ImVec2(ColX, TopY + 16 * Scale), ColW, Component, Scale);
                    ImGui::PopFont();
                    if (Edited)
                    {
                        Vector[Axis] = std::clamp(Component, -1., 1.);
                        WriteQuantity(Address, Vector);
                    }
                }
                ImGui::SetCursorScreenPos(ImVec2(X, TopY + 43 * Scale));
            }
            else
            {
                AnchorX(X);
                float Value = float(Minimum);
                if (Quantity && std::holds_alternative<double>(*Quantity)) Value = float(std::get<double>(*Quantity));
                if (ConstructProperty(Caption.c_str(), float(Minimum), float(Maximum), Value, Unit.empty() ? "" : Unit.c_str(),
                    false, float(Step), nullptr, W, RecipeForm))
                    WriteQuantity(Address, double(Value));
            }
            ImGui::PopID();
        };
        const auto FindVariableByAddress = [&](const std::string &Resolved) -> const MaterialAuthoringVariable *
        {
            for (const auto &Variable : Document.Variables)
                if (Variable.Address == Resolved) return &Variable;
            return nullptr;
        };
        const auto ShareParameter = [&](const std::string &Address, const std::string &Category, double Minimum, double Maximum, const std::string &Name)
        {
            std::string Root = Address;
            try { Root = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
            if (const auto *Existing = FindVariableByAddress(Root))
            {
                EditingVariableId = Existing->Identity;
                VariableDockOpen = true;
                return;
            }
            if (!ReadQuantity(Address)) WriteQuantity(Address, DefaultFor(Category, Minimum));
            std::string Base = Name.substr(0, 54);
            std::string Unique = Base;
            for (int Ordinal = 2; std::any_of(Document.Variables.begin(), Document.Variables.end(),
                [&](const MaterialAuthoringVariable &Variable) { return Variable.Name == Unique; }); ++Ordinal)
                Unique = Base + " " + std::to_string(Ordinal);
            std::string NewIdentity;
            EditAuthoring([&](MaterialAuthoringDocument &Doc)
            {
                MaterialAuthoringVariable Variable;
                Variable.Identity = IssueMaterialAuthoringIdentity();
                NewIdentity = Variable.Identity;
                Variable.Name = Unique;
                Variable.Address = Root;
                Variable.Category = Category;
                Variable.Minimum = Minimum;
                Variable.Maximum = Maximum;
                Variable.Bounded = Category == "scalar";
                Doc.Variables.push_back(std::move(Variable));
            });
            if (!NewIdentity.empty()) { EditingVariableId = NewIdentity; VariableDockOpen = true; }
        };
        const auto ToggleExposure = [&](const MaterialAuthoringVariable &Variable)
        {
            EditAuthoring([&](MaterialAuthoringDocument &Doc)
            {
                for (auto &EntryVariable : Doc.Variables)
                    if (EntryVariable.Identity == Variable.Identity)
                    {
                        if (EntryVariable.Exposure) EntryVariable.Exposure.reset();
                        else EntryVariable.Exposure = MaterialAuthoringExposure{EntryVariable.Name, EntryVariable.Minimum, EntryVariable.Maximum, EntryVariable.Category == "scalar"};
                    }
            });
        };
        // 📝 .mw-exposure row: ◇ Make/Edit variable + Expose-to-Properties checkbox.
        const auto RenderVariableActions = [&](const std::string &Address, const std::string &Category, double Minimum, double Maximum, const std::string &Name, float X, float W)
        {
            (void)W;
            std::string Root = Address;
            try { Root = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
            const MaterialAuthoringVariable *Variable = FindVariableByAddress(Root);
            Gap(9 * Scale);
            AnchorX(X);
            const float RowY = ImGui::GetCursorScreenPos().y;
            const char *Caption = Variable ? "Edit variable" : "Make variable";
            const float ButtonW = InspectorTextWidth(8, Caption, Scale) + 30 * Scale;
            auto *Body = ImGui::GetWindowDrawList();
            ImGui::SetCursorScreenPos(ImVec2(X, RowY));
            ImGui::PushID(("share:" + Address).c_str());
            const bool Pressed = ImGui::InvisibleButton("##ShareButton", ImVec2(ButtonW, 20 * Scale));
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            Body->AddRectFilled(ImVec2(X, RowY), ImVec2(X + ButtonW, RowY + 20 * Scale), Ink255(Hovered ? 0x2b2b2b : 0x222222), 9 * Scale);
            Body->AddRect(ImVec2(X, RowY), ImVec2(X + ButtonW, RowY + 20 * Scale), Ink255(0xffffff, 13), 9 * Scale);
            DrawInspectorDiamondGlyph(ImVec2(X + 11 * Scale, RowY + 10 * Scale), 8, 0xa8b59d, Scale);
            Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(X + 19 * Scale, RowY + 10 * Scale - 8 * 1.302f * Scale * .5f), Ink255(0xa8b59d), Caption);
            if (Pressed) ShareParameter(Address, Category, Minimum, Maximum, Name);
            if (Variable)
            {
                const float CheckX = X + ButtonW + 8 * Scale;
                bool Exposed = Variable->Exposure.has_value();
                ImGui::PushID(("expose:" + Variable->Identity).c_str());
                if (ConstructInspectorCheckboxAt("##ExposeCheck", ImVec2(CheckX, RowY + 4 * Scale), Exposed, Scale)) ToggleExposure(*Variable);
                ImGui::PopID();
                Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(CheckX + 17 * Scale, RowY + 10 * Scale - 8 * 1.302f * Scale * .5f), Ink255(0xa8b59d), "Expose to Properties");
            }
            ImGui::SetCursorScreenPos(ImVec2(X, RowY + 20 * Scale));
            Gap(9 * Scale);
        };
        // 📝 "Shared parameter" / "Shared input" link select (Independent + matching variables).
        const auto RenderLinkSelect = [&](const std::string &Address, const std::string &Category, double Minimum, double Maximum,
            const char *LabelText, float LabelSize, uint32_t LabelColour, float X, float W)
        {
            AnchorX(X);
            const float TopY = ImGui::GetCursorScreenPos().y;
            ConstructMaterialCaption(X, TopY + 3 * Scale, LabelSize * Scale, LabelColour, LabelText);
            std::vector<InspectorSelectOption> Options{{"", "Independent", false}};
            for (const auto &Variable : Document.Variables)
            {
                if (Variable.Address == Address) continue;
                const bool Match = Variable.Category == Category &&
                    (Category != "scalar" || (Variable.Minimum >= Minimum && Variable.Maximum <= Maximum));
                if (Match) Options.push_back({Variable.Identity, Variable.Name, false});
            }
            std::string Current;
            if (const auto Found = Document.Links.find(Address); Found != Document.Links.end()) Current = Found->second;
            SelectField("link:" + Address, ImVec2(X, TopY + 16 * Scale), W, Options, Current, false,
                [&](const std::string &Value)
                {
                    EditAuthoring([&](MaterialAuthoringDocument &Doc)
                    {
                        if (Value.empty()) Doc.Links.erase(Address);
                        else Doc.Links[Address] = Value;
                    }, true);
                });
            ImGui::SetCursorScreenPos(ImVec2(X, TopY + 43 * Scale));
        };

        const auto RenderIntro = [&](const char *Eyebrow, const char *Heading, const char *Paragraph)
        {
            AnchorX(StartX + 4 * Scale);
            const float TopY = ImGui::GetCursorScreenPos().y + 5 * Scale;
            auto *Body = ImGui::GetWindowDrawList();
            Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(StartX + 4 * Scale, TopY), Ink255(0x8e9e82), Eyebrow);
            ConstructMaterialCaption(StartX + 4 * Scale, TopY + 20 * Scale, 22 * Scale, 0xdcdcd2, Heading, static_cast<ImFont *>(HeadingTypeface));
            const float Height = GlyphTextBlock(ImVec2(StartX + 4 * Scale, TopY + 49 * Scale), ContentW - 8 * Scale, 10, 1.7f, 0x888888, Paragraph);
            ImGui::SetCursorScreenPos(ImVec2(StartX, TopY + 49 * Scale + Height + 16 * Scale));
        };

        // 📝 Dock rows are rendered after the scroll child closes but reuse the body's write
        // paths, so they are captured here (while those lambdas are in scope) and invoked by
        // the pinned dock band at the end of the function.
        ConstructDockRows = [&]()
        {
            const float RowX = ImGui::GetCursorScreenPos().x;
            const float RowW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
            auto *Body = ImGui::GetWindowDrawList();
            if (Document.Variables.empty())
            {
                Gap(11 * Scale);
                const float Height = GlyphTextBlock(ImVec2(RowX, ImGui::GetCursorScreenPos().y), RowW, 9, 1.7f, 0x888888,
                    "Use ◇ on a channel or generator input. Expose selected variables to Properties.");
                Gap(Height + 11 * Scale);
                return;
            }
            for (const auto &Variable : Document.Variables)
            {
                ImGui::PushID(Variable.Identity.c_str());
                AnchorX(RowX);
                const float RowTop = ImGui::GetCursorScreenPos().y;
                Body->AddLine(ImVec2(RowX, RowTop), ImVec2(RowX + RowW, RowTop), Ink255(0xffffff, 14));
                Gap(11 * Scale);
                AnchorX(RowX);
                const float HeaderY = ImGui::GetCursorScreenPos().y;
                const bool Editing = EditingVariableId == Variable.Identity;
                const float ButtonsW = 52 * Scale + 7 * Scale + 62 * Scale;
                Body->PushClipRect(ImVec2(RowX, HeaderY), ImVec2(RowX + RowW - ButtonsW - 7 * Scale, HeaderY + 20 * Scale), true);
                ConstructMaterialCaption(RowX, HeaderY + 4 * Scale, 10 * Scale, 0xc2d1b5, Variable.Name.c_str());
                Body->PopClipRect();
                float ButtonX = RowX + RowW - ButtonsW;
                if (ConstructInspectorButton(Editing ? "Close##DockEdit" : "Edit##DockEdit", ImVec2(ButtonX, HeaderY), ImVec2(52 * Scale, 20 * Scale), Scale, 8))
                    EditingVariableId = Editing ? std::string{} : Variable.Identity;
                ButtonX += 59 * Scale;
                if (ConstructInspectorButton("Rename##DockRename", ImVec2(ButtonX, HeaderY), ImVec2(62 * Scale, 20 * Scale), Scale, 8))
                {
                    RenamingVariableId = Variable.Identity;
                    InspectorNameKey = "rename";
                    std::snprintf(InspectorNameText.data(), InspectorNameText.size(), "%s", Variable.Name.c_str());
                }
                ImGui::SetCursorScreenPos(ImVec2(RowX, HeaderY + 20 * Scale));
                size_t Uses = 1;
                for (const auto &[Address, Reference] : Document.Links)
                    if (Reference == Variable.Identity) ++Uses;
                const std::string Small = Variable.Address + " · " + std::to_string(Uses) + " uses";
                Gap(4 * Scale);
                Body->PushClipRect(ImVec2(RowX, ImGui::GetCursorScreenPos().y), ImVec2(RowX + RowW, ImGui::GetCursorScreenPos().y + 12 * Scale), true);
                Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(RowX, ImGui::GetCursorScreenPos().y), Ink255(0x8a947f), Small.c_str());
                Body->PopClipRect();
                Gap(10 * Scale);
                if (Editing)
                {
                    RenderParameterControl(Variable.Address, Variable.Name, Variable.Category, Variable.Minimum, Variable.Maximum, .01, "", RowX, RowW);
                    Gap(12 * Scale);
                    AnchorX(RowX);
                    const float ToggleY = ImGui::GetCursorScreenPos().y;
                    bool Exposed = Variable.Exposure.has_value();
                    if (ConstructInspectorCheckboxAt("##DockExpose", ImVec2(RowX, ToggleY), Exposed, Scale)) ToggleExposure(Variable);
                    ConstructMaterialCaption(RowX + 19 * Scale, ToggleY + 2 * Scale, 9 * Scale, 0xa7b29d, "Expose to Properties");
                    ImGui::SetCursorScreenPos(ImVec2(RowX, ToggleY + 12 * Scale));
                    Gap(12 * Scale);
                    if (Variable.Exposure)
                    {
                        AnchorX(RowX);
                        const float SetTop = ImGui::GetCursorScreenPos().y;
                        Body->ChannelsSplit(2);
                        Body->ChannelsSetCurrent(1);
                        ImGui::SetCursorScreenPos(ImVec2(RowX + 10 * Scale, SetTop + 10 * Scale));
                        ConstructMaterialCaption(RowX + 10 * Scale, SetTop + 13 * Scale, 8 * Scale, 0xacbf9d, "Designer projection");
                        Gap(12 * Scale);
                        AnchorX(RowX + 10 * Scale);
                        const float LabelY = ImGui::GetCursorScreenPos().y;
                        ConstructMaterialCaption(RowX + 10 * Scale, LabelY + 3 * Scale, 8 * Scale, 0x999999, "Property label");
                        TextField("exposurename:" + Variable.Identity, InspectorNameText.data(), InspectorNameText.size(), InspectorNameKey,
                            ImVec2(RowX + 10 * Scale, LabelY + 15 * Scale), RowW - 20 * Scale, Variable.Exposure->Name,
                            [&](const std::string &Text)
                            {
                                EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                {
                                    for (auto &Candidate : Doc.Variables)
                                        if (Candidate.Identity == Variable.Identity && Candidate.Exposure)
                                            Candidate.Exposure->Name = Text.substr(0, 80);
                                });
                            });
                        ImGui::SetCursorScreenPos(ImVec2(RowX + 10 * Scale, LabelY + 42 * Scale));
                        if (Variable.Category == "scalar")
                        {
                            Gap(9 * Scale);
                            const float RangeY = ImGui::GetCursorScreenPos().y;
                            const float ColW = (RowW - 20 * Scale - 8 * Scale) * .5f;
                            const std::array<std::pair<const char *, double>, 2> Bounds{{
                                {"Minimum", Variable.Exposure->Minimum}, {"Maximum", Variable.Exposure->Maximum}}};
                            for (size_t Bound = 0; Bound < Bounds.size(); ++Bound)
                            {
                                const float ColX = RowX + 10 * Scale + Bound * (ColW + 8 * Scale);
                                ConstructMaterialCaption(ColX, RangeY + 3 * Scale, 8 * Scale, 0x999999, Bounds[Bound].first);
                                double Value = Bounds[Bound].second;
                                ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
                                const bool Edited = ConstructInspectorNumberInput(
                                    Bound == 0 ? "##ExposureMinimum" : "##ExposureMaximum", ImVec2(ColX, RangeY + 15 * Scale), ColW, Value, Scale);
                                ImGui::PopFont();
                                if (Edited)
                                {
                                    EditAuthoring([&, Bound](MaterialAuthoringDocument &Doc)
                                    {
                                        for (auto &Candidate : Doc.Variables)
                                            if (Candidate.Identity == Variable.Identity && Candidate.Exposure)
                                            {
                                                if (Bound == 0) Candidate.Exposure->Minimum = Value;
                                                else Candidate.Exposure->Maximum = Value;
                                                Candidate.Exposure->Bounded = true;
                                            }
                                    });
                                }
                            }
                            ImGui::SetCursorScreenPos(ImVec2(RowX + 10 * Scale, RangeY + 42 * Scale));
                        }
                        const float SmallHeight = ConstructInspectorTextBlock(ImVec2(RowX + 10 * Scale, ImGui::GetCursorScreenPos().y + 7 * Scale),
                            RowW - 20 * Scale, 8, 1.6f, 0x879579,
                            "Only the Properties control is restricted. The PBR/source editor retains the full physical range.", Scale);
                        Gap(7 * Scale + SmallHeight + 10 * Scale);
                        const float SetBottom = ImGui::GetCursorScreenPos().y;
                        Body->ChannelsSetCurrent(0);
                        Body->AddRect(ImVec2(RowX, SetTop), ImVec2(RowX + RowW, SetBottom), Ink255(0xb5c5a7, 51), 9 * Scale);
                        Body->ChannelsMerge();
                        ImGui::SetCursorScreenPos(ImVec2(RowX, SetBottom));
                        Gap(10 * Scale);
                    }
                }
                Gap(11 * Scale);
                ImGui::PopID();
            }
        };


        if (InspectorMode == 0)
        {
            RenderIntro("DESIGNER VIEW", "Properties", "Renamed, range-limited projections into layer channels and generator inputs. These are not another set of material values.");
            if (Profile)
            {
                AnchorX(StartX + 3 * Scale);
                const float TopY = ImGui::GetCursorScreenPos().y + 8 * Scale;
                ConstructIcon(IconSymbol::EditorCheck, StartX + 3 * Scale, TopY, 11 * Scale, 0x8b8d79);
                auto *Body = ImGui::GetWindowDrawList();
                Body->AddText(ImGui::GetFont(), 7 * 1.302f * Scale, ImVec2(StartX + 18 * Scale, TopY + 1 * Scale), Ink255(0x8b8d79), "DESIGNER CONTROLS");
                ConstructMaterialCaption(StartX + 3 * Scale, TopY + 23 * Scale, 14 * Scale, 0xdcdcd2, Profile->Title.c_str());
                const float Height = ConstructInspectorTextBlock(ImVec2(StartX + 3 * Scale, TopY + 43 * Scale), ContentW - 6 * Scale, 9, 1.6f, 0x868676, Profile->Caption.c_str(), Scale);
                ImGui::SetCursorScreenPos(ImVec2(StartX, TopY + 43 * Scale + Height + 13 * Scale));
            }

            // ── Finish ─────────────────────────────────────────────────────────────────────────
            {
                bool Closed = false;
                const float Top = BeginSection("finish", IconSymbol::EditorLayers, Profile && Profile->Woven ? "Yarn color & feel" : "Color & finish", "", Closed);
                if (!Closed)
                {
                    if (Profile)
                    {
                        Gap(2 * Scale);
                        for (const auto &[Key, Label] : Colours) RenderColourField(Key, Label);
                        const bool Drew = RenderGroup("finish");
                        if (HasGroup("coat")) { if (Drew) Gap(21 * Scale); RenderGroup("coat"); }
                        // Range note.
                        Gap(14 * Scale);
                        AnchorX(FieldX);
                        const float NoteY = ImGui::GetCursorScreenPos().y;
                        ConstructIcon(IconSymbol::EditorCheck, FieldX, NoteY, 12 * Scale, 0x9cac98);
                        const float Height = ConstructInspectorTextBlock(ImVec2(FieldX + 17 * Scale, NoteY), FieldW - 17 * Scale, 8, 1.8f, 0x7b7b6b,
                            "Material-specific ranges. Physical values stay in balance.", Scale);
                        Gap(Height);
                    }
                    else if (Definition.Scattering.Illumination.Configuration[0] > 0)
                    {
                        // 📝 No ported recipe profile: the generic structured scalars stay
                        // editable on the surface stratum (salvaged fallback).
                        auto &Stratum = Definition.Scattering.Illumination.Strata[0];
                        const auto Generic = [&](const char *Caption, float Minimum, float Maximum, float &Target, const char *Field)
                        {
                            AnchorX(FieldX);
                            if (ConstructProperty(Caption, Minimum, Maximum, Target, "%", false, 0, nullptr, FieldW, true))
                                Changed = Library.ConfigureStratumCoefficient(0, Field, Target) || Changed;
                            Gap(21 * Scale);
                        };
                        Generic("Roughness", .045f, 1.f, Stratum.AlbedoRoughness[3], "Roughness");
                        Generic("Metallic", 0.f, 1.f, Stratum.Optics[0], "Metallic");
                        Generic("Anisotropy", -.9f, .9f, Definition.Scattering.DirectionalOptics[0][0], "Anisotropy");
                        Generic("Specular", 0.f, 1.f, Definition.Scattering.DirectionalOptics[0][3], "Specular");
                        Generic("Normal strength", 0.f, 4.f, Definition.Scattering.StratumNormal[0][0], "NormalStrength");
                        AnchorX(FieldX);
                        if (ConstructProperty("Height strength", 0.f, 4.f, Definition.Scattering.StratumNormal[0][1], "%", false, 0, nullptr, FieldW, true))
                            Changed = Library.ConfigureStratumCoefficient(0, "HeightStrength", Definition.Scattering.StratumNormal[0][1]) || Changed;
                    }
                }
                EndSection(Top);
            }
            // ── Scratches ──────────────────────────────────────────────────────────────────────
            if (Profile && Profile->SupportsMetalScratches)
            {
                bool Closed = false;
                const float Top = BeginSection("scratches", IconSymbol::EditorLayers, "Metal scratches", ScratchesEnabled ? "ON" : "OFF", Closed);
                if (!Closed)
                {
                    bool Enabled = ScratchesEnabled;
                    AnchorX(FieldX);
                    const auto ToggleOrigin = ImGui::GetCursorScreenPos();
                    ScratchExtent = {ToggleOrigin.x, ToggleOrigin.y, ToggleOrigin.x + 132 * Scale, ToggleOrigin.y + 30 * Scale};
                    if (ConstructMaterialToggle("Enable scratches", Enabled, Scale))
                    {
                        ScratchesEnabled = Enabled;
                        bool Written = false;
                        if (Enabled)
                        {
                            const bool HasField = std::any_of(Definition.StratumChannels[0].begin(), Definition.StratumChannels[0].end(),
                                [](const auto &Channel) { return !Channel.Op.empty() && Channel.Name != "Albedo"; });
                            if (!HasField)
                            {
                                Written = Library.ConfigureStratumChannel(0, "Normal", "scratchedMetal") || Written;
                                Written = Library.ConfigureStratumChannel(0, "Orm", "scratchedMetal") || Written;
                                Definition = DecodeSurfaceMaterial(Library.QuerySource());
                            }
                            const auto Pool = QueryRecipePoolLocation(Definition, "scratchDensity");
                            if (Pool)
                            {
                                const auto Staged = Entry ? Entry->Recipe.Scalars.find("scratchDensity") : Entry->Recipe.Scalars.end();
                                Written = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, "scratchDensity",
                                    Entry && Staged != Entry->Recipe.Scalars.end() ? Staged->second : 6.0) || Written;
                            }
                        }
                        else if (const auto Pool = QueryRecipePoolLocation(Definition, "scratchDensity"))
                            Written = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, "scratchDensity", 0.0) || Written;
                        Changed = Written || Changed;
                        if (Written) ProceduralDirty = true;
                    }
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, ToggleOrigin.y + 18 * Scale));
                    Gap(14 * Scale);
                    HelpText("Optional surface cuts. The base finish stays unchanged when off. Direction is independent of other surface grain directions.", FieldX, FieldW);
                    if (ScratchesEnabled) { Gap(21 * Scale); RenderGroup("scratches"); }
                }
                EndSection(Top);
            }
            // ── Iridescence ────────────────────────────────────────────────────────────────────
            if (Profile && HasGroup("iridescence"))
            {
                bool Closed = false;
                const float Top = BeginSection("iridescence", IconSymbol::Stars, "Iridescent finish", "THIN FILM", Closed);
                if (!Closed)
                {
                    RenderGroup("iridescence");
                    Gap(22 * Scale);
                    AnchorX(FieldX);
                    const float FilmY = ImGui::GetCursorScreenPos().y;
                    DrawInspectorGradient(ImVec2(FieldX, FilmY), ImVec2(FieldX + FieldW, FilmY + 4 * Scale),
                        {0xab70aa, 0x7977c4, 0x779bbc, 0x80bcb0, 0xbbbf8c, 0xd1b288, 0xb7769e}, 4, Scale, 178);
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, FilmY + 4 * Scale));
                    Gap(14 * Scale);
                    HelpText("Orbit the surface to see the shift. Thin-film interference changes the reflection color with viewing angle—not just a painted-on gradient.", FieldX, FieldW);
                }
                EndSection(Top);
            }
            // ── Construction ───────────────────────────────────────────────────────────────────
            const bool SheetMaterial = Profile && Profile->Type == 21 &&
                [&]() { const auto Q = ReadQuantity("sheetFinish"); return Q && std::holds_alternative<double>(*Q) && std::get<double>(*Q) > 0; }();
            if (Profile && Profile->Woven)
            {
                bool Closed = false;
                const float Top = BeginSection("construction", IconSymbol::EditorLayers, "Weave construction", "9 WEAVES", Closed);
                if (!Closed)
                {
                    AnchorX(FieldX);
                    const float LabelY = ImGui::GetCursorScreenPos().y;
                    ConstructMaterialCaption(FieldX, LabelY + 3 * Scale, 10 * Scale, 0xa7acb5, "Construction");
                    const auto &Weaves = WeaveOptions();
                    std::string Current = std::get<0>(Weaves[1]);
                    if (const auto Q = ReadQuantity("weavePattern"); Q && std::holds_alternative<std::string>(*Q)) Current = std::get<std::string>(*Q);
                    std::vector<InspectorSelectOption> Options;
                    size_t CurrentIndex = 1;
                    for (size_t Index = 0; Index < Weaves.size(); ++Index)
                    {
                        Options.push_back({std::get<0>(Weaves[Index]), std::get<1>(Weaves[Index]), false});
                        if (Current == std::get<0>(Weaves[Index])) CurrentIndex = Index;
                    }
                    SelectField("weave", ImVec2(FieldX, LabelY + 22 * Scale), FieldW, Options, Current, true,
                        [&](const std::string &Value) { WriteQuantity("weavePattern", Value); });
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, LabelY + 55 * Scale));
                    Gap(10 * Scale);
                    HelpText(std::get<2>(Weaves[CurrentIndex]), FieldX, FieldW);
                    if (HasGroup("construction")) { Gap(21 * Scale); RenderGroup("construction"); }
                    Gap(14 * Scale);
                    HelpText("Yarn colors follow the over/under pattern. On the frozen cloth, the weave follows every fold.", FieldX, FieldW);
                }
                EndSection(Top);
            }
            else if (SheetMaterial)
            {
                bool Closed = false;
                const float Top = BeginSection("construction", IconSymbol::EditorLayers, "Sheet corrugation", "", Closed);
                if (!Closed)
                {
                    AnchorX(FieldX);
                    const float LabelY = ImGui::GetCursorScreenPos().y;
                    ConstructMaterialCaption(FieldX, LabelY + 3 * Scale, 10 * Scale, 0xa7acb5, "Profile");
                    std::string Current = "0";
                    if (const auto Q = ReadQuantity("ribProfile"); Q && std::holds_alternative<double>(*Q))
                        Current = std::to_string(int(std::lround(std::get<double>(*Q))));
                    SelectField("ribprofile", ImVec2(FieldX, LabelY + 22 * Scale), FieldW,
                        {{"0", "Rounded corrugation", false}, {"1", "Box / IBR ribs", false}, {"2", "Roller-door ribs", false}},
                        Current, true, [&](const std::string &Value) { WriteQuantity("ribProfile", double(std::atoi(Value.c_str()))); });
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, LabelY + 55 * Scale));
                    if (HasGroup("construction")) { Gap(21 * Scale); RenderGroup("construction"); }
                    Gap(14 * Scale);
                    HelpText("Rib relief changes shading and exported normals/height, not sheet thickness, silhouette or structural geometry.", FieldX, FieldW);
                }
                EndSection(Top);
            }
            else if (Profile && HasGroup("construction"))
            {
                bool Closed = false;
                const float Top = BeginSection("construction", IconSymbol::EditorLayers, "Construction", "", Closed);
                if (!Closed) RenderGroup("construction");
                EndSection(Top);
            }
            // ── Sand grains / beds / strata ──────────────────────────────────────────────────────
            if (Profile && Profile->Type == 37)
            {
                bool Closed = false;
                const float Top = BeginSection("grains", IconSymbol::EditorLayers, "Sand grains", "DIELECTRIC", Closed);
                if (!Closed)
                {
                    RenderGroup("grains");
                    Gap(14 * Scale);
                    HelpText("Sizes are nominal mineral-grain diameters at repetition 1 (100 scene units per metre). Grain identity seeds size, roughness, specular and facet direction independently. Use Macro to see individual grains; unresolved grains are filtered.", FieldX, FieldW);
                }
                EndSection(Top);
            }
            std::vector<MaterialSandBed> SandBeds;
            std::string SandArrangement = "beds";
            const bool HasSand = Profile && Profile->Type == 38 && Library.QuerySandBeds(SandBeds, SandArrangement);
            if (HasSand)
            {
                bool Closed = false;
                const float Top = BeginSection("sandBeds", IconSymbol::EditorLayers, "Sand layer beds", std::to_string(SandBeds.size()) + " / 4", Closed);
                if (!Closed)
                {
                    const auto CommitBeds = [&]()
                    {
                        if (!Library.ConfigureSandBeds(SandBeds, SandArrangement))
                        {
                            InspectorNotice = Library.QueryDiagnostic().empty() ? std::string("Those sand bed values are not supported.") : Library.QueryDiagnostic();
                            (void)Library.QuerySandBeds(SandBeds, SandArrangement);
                        }
                        else { InspectorNotice.clear(); Changed = true; ProceduralDirty = true; }
                    };
                    AnchorX(FieldX);
                    const float LabelY = ImGui::GetCursorScreenPos().y;
                    ConstructMaterialCaption(FieldX, LabelY + 3 * Scale, 10 * Scale, 0xa7acb5, "Layer arrangement");
                    SelectField("sand-arrangement", ImVec2(FieldX, LabelY + 22 * Scale), FieldW,
                        {{"beds", "Stacked grain beds", false}, {"bands", "Sediment bands", false}}, SandArrangement, true,
                        [&](const std::string &Value) { SandArrangement = Value; CommitBeds(); });
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, LabelY + 55 * Scale));
                    Gap(14 * Scale);
                    HelpText("Top bed occludes the lower grains where it covers them. Bands expose each bed's own properties in wavy strata. These are surface shaders, not loose particles or simulated erosion.", FieldX, FieldW);
                    // Bed list.
                    Gap(12 * Scale);
                    if (std::none_of(SandBeds.begin(), SandBeds.end(), [&](const MaterialSandBed &Bed) { return Bed.Id == SelectedSandBedId; }) && !SandBeds.empty())
                        SelectedSandBedId = SandBeds[0].Id;
                    size_t BedIndex = 0;
                    for (size_t Index = 0; Index < SandBeds.size(); ++Index)
                        if (SandBeds[Index].Id == SelectedSandBedId) BedIndex = Index;
                    auto *Body = ImGui::GetWindowDrawList();
                    for (size_t Index = 0; Index < SandBeds.size(); ++Index)
                    {
                        auto &Bed = SandBeds[Index];
                        if (Index) Gap(5 * Scale);
                        AnchorX(FieldX);
                        const float RowTop = ImGui::GetCursorScreenPos().y;
                        const float ButtonW = FieldW - 19 * Scale;
                        const bool Selected = Index == BedIndex;
                        ImGui::SetCursorScreenPos(ImVec2(FieldX, RowTop));
                        ImGui::PushID(int(Index) + 1100);
                        if (ImGui::InvisibleButton("##SandBedRow", ImVec2(ButtonW, 47 * Scale))) SelectedSandBedId = Bed.Id;
                        ImGui::PopID();
                        Body->AddRectFilled(ImVec2(FieldX, RowTop), ImVec2(FieldX + ButtonW, RowTop + 47 * Scale), Ink255(Selected ? 0x303329 : 0x1b1c19), 13 * Scale);
                        Body->AddRect(ImVec2(FieldX, RowTop), ImVec2(FieldX + ButtonW, RowTop + 47 * Scale), Selected ? Ink255(0x7c8863) : Ink255(0xffffff, 10), 13 * Scale);
                        std::array<float, 3> BedSrgb{1, 1, 1};
                        (void)ParseHexColour(Bed.Color, BedSrgb);
                        Body->AddRectFilled(ImVec2(FieldX + 10 * Scale, RowTop + 10 * Scale), ImVec2(FieldX + 32 * Scale, RowTop + 37 * Scale),
                            IM_COL32(unsigned(BedSrgb[0] * 255), unsigned(BedSrgb[1] * 255), unsigned(BedSrgb[2] * 255), 255), 6 * Scale);
                        Body->AddRect(ImVec2(FieldX + 10 * Scale, RowTop + 10 * Scale), ImVec2(FieldX + 32 * Scale, RowTop + 37 * Scale), Ink255(0xffffff, 32), 6 * Scale);
                        ConstructMaterialCaption(FieldX + 41 * Scale, RowTop + 12 * Scale, 11 * Scale, 0xd5d4bd, Bed.Name.c_str());
                        const std::string Small = TrimQuantity(Bed.Size) + " mm · rough " + TrimQuantity(Bed.Roughness) + " · spec " + TrimQuantity(Bed.Specular);
                        Body->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(FieldX + 41 * Scale, RowTop + 28 * Scale), Ink255(0x969581), Small.c_str());
                        bool Enabled = Bed.Enabled;
                        ImGui::PushID(int(Index) + 1200);
                        if (ConstructInspectorCheckboxAt("##SandBedEnabled", ImVec2(FieldX + FieldW - 12 * Scale, RowTop + 17.5f * Scale), Enabled, Scale))
                        {
                            Bed.Enabled = Enabled;
                            CommitBeds();
                        }
                        ImGui::PopID();
                        ImGui::SetCursorScreenPos(ImVec2(FieldX, RowTop + 47 * Scale));
                    }
                    // Actions.
                    if (!SandBeds.empty())
                    {
                        Gap(12 * Scale);
                        AnchorX(FieldX);
                        const float ActionsY = ImGui::GetCursorScreenPos().y;
                        float ActionX = FieldX;
                        ImGui::BeginDisabled(SandBeds.size() >= 4);
                        if (ConstructInspectorButton("+ Add bed##AddSandBed", ImVec2(ActionX, ActionsY), ImVec2(78 * Scale, 26 * Scale), Scale, 9))
                        {
                            MaterialSandBed Next;
                            size_t Serial = SandBeds.size() + 1;
                            const auto IdTaken = [&](const std::string &Id)
                            {
                                return std::any_of(SandBeds.begin(), SandBeds.end(), [&](const MaterialSandBed &Bed) { return Bed.Id == Id; });
                            };
                            while (IdTaken("sand-bed-" + std::to_string(Serial))) ++Serial;
                            Next.Id = "sand-bed-" + std::to_string(Serial);
                            Next.Name = "Sand bed " + std::to_string(Serial);
                            SandBeds.push_back(Next);
                            SelectedSandBedId = Next.Id;
                            CommitBeds();
                        }
                        ImGui::EndDisabled();
                        ActionX += 83 * Scale;
                        const auto IconAction = [&](const char *Id, bool Up, bool Trash, bool Disabled, const std::function<void()> &Action)
                        {
                            ImGui::BeginDisabled(Disabled);
                            ImGui::SetCursorScreenPos(ImVec2(ActionX, ActionsY));
                            const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(28 * Scale, 26 * Scale));
                            Body->AddRectFilled(ImVec2(ActionX, ActionsY), ImVec2(ActionX + 28 * Scale, ActionsY + 26 * Scale), Ink255(0x222222), 9 * Scale);
                            Body->AddRect(ImVec2(ActionX, ActionsY), ImVec2(ActionX + 28 * Scale, ActionsY + 26 * Scale), Ink255(0xffffff, 13), 9 * Scale);
                            if (Trash) DrawInspectorTrashGlyph(ImVec2(ActionX + 14 * Scale, ActionsY + 13 * Scale), 13, 0xcecec2, Scale);
                            else DrawInspectorArrowGlyph(ImVec2(ActionX + 14 * Scale, ActionsY + 13 * Scale), 13, 0xcecec2, Scale, Up);
                            ImGui::EndDisabled();
                            if (Pressed && !Disabled) Action();
                            ActionX += 33 * Scale;
                        };
                        IconAction("##SandBedUp", true, false, BedIndex == 0, [&]() { std::swap(SandBeds[BedIndex - 1], SandBeds[BedIndex]); CommitBeds(); });
                        IconAction("##SandBedDown", false, false, BedIndex + 1 >= SandBeds.size(), [&]() { std::swap(SandBeds[BedIndex + 1], SandBeds[BedIndex]); CommitBeds(); });
                        IconAction("##SandBedRemove", false, true, SandBeds.size() <= 1, [&]()
                        {
                            SandBeds.erase(SandBeds.begin() + std::ptrdiff_t(BedIndex));
                            SelectedSandBedId = SandBeds[std::max<size_t>(BedIndex, 1) - 1].Id;
                            CommitBeds();
                        });
                        ImGui::SetCursorScreenPos(ImVec2(FieldX, ActionsY + 26 * Scale));
                        // Selected bed editor.
                        auto &Bed = SandBeds[std::min(BedIndex, SandBeds.size() - 1)];
                        Gap(15 * Scale);
                        AnchorX(FieldX);
                        const float NameY = ImGui::GetCursorScreenPos().y;
                        ConstructMaterialCaption(FieldX, NameY + 3 * Scale, 9 * Scale, 0x9b9b84, "Bed name");
                        TextField("sand-bed-name:" + Bed.Id, InspectorNameText.data(), InspectorNameText.size(), InspectorNameKey,
                            ImVec2(FieldX, NameY + 17 * Scale), FieldW, Bed.Name,
                            [&](const std::string &Text) { Bed.Name = Text.substr(0, 60); CommitBeds(); });
                        ImGui::SetCursorScreenPos(ImVec2(FieldX, NameY + 44 * Scale));
                        const auto BedColour = [&](const char *Label, std::string &Target, const std::string &IdSuffix)
                        {
                            Gap(16 * Scale);
                            AnchorX(FieldX);
                            const float TopY = ImGui::GetCursorScreenPos().y;
                            ConstructMaterialCaption(FieldX, TopY + 3 * Scale, 10 * Scale, 0xa7acb5, Label);
                            const float SrgbW = InspectorTextWidth(7, "sRGB", Scale);
                            Body->AddText(ImGui::GetFont(), 7 * 1.302f * Scale, ImVec2(FieldX + FieldW - SrgbW, TopY + 4 * Scale), Ink255(0x727e88), "sRGB");
                            std::array<float, 3> Srgb{1, 1, 1};
                            (void)ParseHexColour(Target, Srgb);
                            const ImVec2 BarMin(FieldX, TopY + 22 * Scale), BarMax(BarMin.x + FieldW, BarMin.y + 36 * Scale);
                            Body->AddRectFilled(BarMin, BarMax, Ink255(0x1b1d20), 8 * Scale);
                            Body->AddRect(BarMin, BarMax, Ink255(0x373b42), 8 * Scale);
                            ImGui::PushID(IdSuffix.c_str());
                            if (ConstructInspectorColourSwatch("##BedSwatch", ImVec2(BarMin.x + 6 * Scale, BarMin.y + 6.5f * Scale), ImVec2(25 * Scale, 23 * Scale), Srgb, 5, Scale))
                            {
                                Target = EncodeHexColour(Srgb);
                                CommitBeds();
                            }
                            ImGui::PopID();
                            std::string Upper = Target;
                            for (auto &Letter : Upper) Letter = static_cast<char>(std::toupper(static_cast<unsigned char>(Letter)));
                            Body->AddText(static_cast<ImFont *>(HeadingTypeface), 10 * 1.302f * Scale, ImVec2(BarMin.x + 41 * Scale, BarMin.y + 11 * Scale), Ink255(0xc7ced5), Upper.c_str());
                            ConstructIcon(IconSymbol::SlateColorPicker, BarMax.x - 8 * Scale - 13 * Scale, BarMin.y + 11.5f * Scale, 13 * Scale, 0x777f89);
                            ImGui::SetCursorScreenPos(ImVec2(FieldX, TopY + 58 * Scale));
                        };
                        BedColour("Bed mineral color", Bed.Color, "bedcolour");
                        BedColour("Bed pale mineral color", Bed.SecondaryColor, "bedpale");
                        const auto BedSlider = [&](const char *Caption, double &Target, float Minimum, float Maximum, const char *Unit = "", bool Log = false)
                        {
                            Gap(16 * Scale);
                            AnchorX(FieldX);
                            float Value = float(Target);
                            if (ConstructProperty(Caption, Minimum, Maximum, Value, Unit, Log, .01f, nullptr, FieldW, true))
                            {
                                Target = Value;
                                CommitBeds();
                            }
                        };
                        BedSlider("Bed grain size", Bed.Size, .0625f, 2.f, "mm", true);
                        BedSlider("Bed size variation", Bed.SizeVariation, 0.f, 1.f);
                        BedSlider("Bed roughness", Bed.Roughness, .04f, 1.f);
                        BedSlider("Bed roughness variation", Bed.RoughnessVariation, 0.f, 1.f);
                        BedSlider("Bed specular", Bed.Specular, 0.f, 1.f);
                        BedSlider("Bed specular variation", Bed.SpecularVariation, 0.f, 1.f);
                        BedSlider("Bed mineral IOR", Bed.Ior, 1.3f, 2.2f);
                        BedSlider("Bed grain roundness", Bed.Roundness, 0.f, 1.f);
                        BedSlider("Bed facet tilt", Bed.Tilt, 0.f, 1.f);
                        BedSlider("Bed grain relief", Bed.Relief, 0.f, 1.f);
                        BedSlider("Bed coverage", Bed.Coverage, 0.f, 1.f);
                        if (SandArrangement == "bands") BedSlider("Bed band thickness", Bed.Thickness, .1f, 4.f, "relative");
                    }
                }
                EndSection(Top);
            }
            if (HasSand && SandArrangement == "bands")
            {
                bool Closed = false;
                const float Top = BeginSection("strata", IconSymbol::EditorLayers, "Sediment bands", "", Closed);
                if (!Closed) RenderGroup("strata");
                EndSection(Top);
            }
            // ── Flakes ─────────────────────────────────────────────────────────────────────────
            const bool HasFlakeModel = (Profile && Profile->Type == 0) || Definition.Scattering.Paint.Configuration[3] > 0;
            if (HasFlakeModel)
            {
                const bool AutomotiveRecipe = Profile && Profile->Type == 0;
                bool Closed = false;
                const float Top = BeginSection("flakes", IconSymbol::Stars,
                    AutomotiveRecipe ? "Flake character" : "Mineral & crystal flakes", "CELLULAR", Closed);
                if (!Closed)
                {
                    if (AutomotiveRecipe || HasGroup("flakes") || HasGroup("paint"))
                    {
                        const bool DrewPaint = RenderGroup("paint");
                        if (DrewPaint && HasGroup("flakes")) Gap(21 * Scale);
                        RenderGroup("flakes");
                    }
                    else
                    {
                        auto &Paint = Definition.Scattering.Paint;
                        const auto FlakeSlider = [&](const char *Label, float &Target, float MinV, float MaxV,
                            const char *Unit = "", bool Log = false, float StepV = .01f)
                        {
                            AnchorX(FieldX);
                            float Val = Target;
                            if (ConstructProperty(Label, MinV, MaxV, Val, Unit, Log, StepV, nullptr, FieldW, true))
                            {
                                Target = Val;
                                Paint.Finish[1] = std::max(Paint.Finish[0], Paint.Finish[1]);
                                Paint.Finish[3] = std::max(Paint.Finish[2], Paint.Finish[3]);
                                Changed = Library.ConfigurePaint(Paint) || Changed;
                            }
                            Gap(18 * Scale);
                        };
                        FlakeSlider("Flake coverage", Paint.Distribution[0], 0.f, 1.f, "%");
                        FlakeSlider("Crystal / grain size", Paint.Distribution[1], 40.f, 2400.f, "um", true, 1.f);
                        FlakeSlider("Flake frequency", Paint.Distribution[2], .2f, 4.f, "x");
                        FlakeSlider("Facet tilt", Paint.Relief[0], 0.f, 1.f, "%");
                        FlakeSlider("Burial depth", Paint.Relief[1], 0.f, 1.f, "%");
                        FlakeSlider("Facet roughness min", Paint.Finish[0], .045f, 1.f, "%");
                        FlakeSlider("Facet roughness max", Paint.Finish[1], .045f, 1.f, "%");
                        FlakeSlider("Facet luster min", Paint.Finish[2], 0.f, 1.f, "%");
                        FlakeSlider("Facet luster max", Paint.Finish[3], 0.f, 1.f, "%");
                        AnchorX(FieldX);
                        float LayersVal = float(Paint.Configuration[1]);
                        if (ConstructProperty("Flake strata", 1.f, 4.f, LayersVal, "", false, 1.f, nullptr, FieldW, true))
                        {
                            Paint.Configuration[1] = uint32_t(std::clamp(std::lround(LayersVal), 1l, 4l));
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        }
                    }
                    Gap(14 * Scale);
                    HelpText(AutomotiveRecipe ?
                        "Sparkle tunes micro-roughness and orientation; reflectivity independently bounds the metallic response. Cellular particles remain layered beneath the coat." :
                        "Object-space 3D Voronoi mineral facets (quartz, mica, feldspar, olivine) glint with viewing angle over the surface substrate.",
                        FieldX, FieldW);
                }
                EndSection(Top);
            }
            if (HasFlakeModel)
            {
                bool Closed = false;
                const float Top = BeginSection("colors", IconSymbol::EditorLayers,
                    (Profile && Profile->Type == 0) ? "Flake colors" : "Mineral flake palette", "", Closed);
                if (!Closed) RenderFlakeRamp(FieldX, FieldW, true);
                EndSection(Top);
            }
            // ── Detail + leftover groups ─────────────────────────────────────────────────────────
            if (Profile && HasGroup("detail"))
            {
                bool Closed = false;
                const float Top = BeginSection("detail", IconSymbol::EditorLayers, Profile->Woven ? "Thread detail" : "Surface detail", "", Closed);
                if (!Closed)
                {
                    RenderGroup("detail");
                    if (Profile->Identity == "wornPlastic")
                    {
                        Gap(14 * Scale);
                        HelpText("Abrasion flattens the raised grain. The recessed surface keeps its texture.", FieldX, FieldW);
                    }
                }
                EndSection(Top);
            }
            if (Profile)
            {
                static constexpr const char *Handled[] = {"finish", "coat", "scratches", "iridescence", "construction", "grains", "strata", "flakes", "paint", "detail"};
                std::vector<std::string> Extra;
                for (const auto &Control : Controls)
                {
                    bool Known = false;
                    for (const char *Group : Handled) Known = Known || Control.Group == Group;
                    if (!Known && std::find(Extra.begin(), Extra.end(), Control.Group) == Extra.end()) Extra.push_back(Control.Group);
                }
                for (const auto &Group : Extra)
                {
                    bool Closed = false;
                    const std::string Title = CaptionFromKey(Group);
                    const float Top = BeginSection("group:" + Group, IconSymbol::EditorLayers, Title.c_str(), "", Closed);
                    if (!Closed) RenderGroup(Group.c_str());
                    EndSection(Top);
                }
            }
            // ── Exposed variables ────────────────────────────────────────────────────────────────
            {
                AnchorX(StartX);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.133f, .133f, .133f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .047f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16 * Scale);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 14 * Scale));
                if (ImGui::BeginChild("##ExposedProperties", ImVec2(ContentW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    const float InnerX = ImGui::GetCursorScreenPos().x;
                    const float InnerW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                    ConstructMaterialCaption(InnerX, ImGui::GetCursorScreenPos().y + 4 * Scale, 13 * Scale, 0xdcdcd2, "Exposed variables");
                    Gap(17 * Scale);
                    std::vector<const MaterialAuthoringVariable *> Exposed;
                    for (const auto &Variable : Document.Variables)
                        if (Variable.Exposure) Exposed.push_back(&Variable);
                    if (Exposed.empty())
                    {
                        const float Height = ConstructInspectorTextBlock(ImVec2(InnerX, ImGui::GetCursorScreenPos().y + 11 * Scale), InnerW, 9, 1.7f, 0x888888,
                            "Enable “Expose to Properties” on a variable in the bottom list.", Scale);
                        Gap(11 * Scale + Height + 11 * Scale);
                    }
                    for (const auto *Variable : Exposed)
                    {
                        Gap(12 * Scale);
                        ImGui::PushID(Variable->Identity.c_str());
                        AnchorX(InnerX);
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.133f, .133f, .133f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .047f));
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16 * Scale);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 14 * Scale));
                        if (ImGui::BeginChild("##ExposedProperty", ImVec2(InnerW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                        {
                            const float PropertyX = ImGui::GetCursorScreenPos().x;
                            const float PropertyW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                            const double Minimum = Variable->Category == "scalar" && Variable->Exposure->Bounded ? Variable->Exposure->Minimum : Variable->Minimum;
                            const double Maximum = Variable->Category == "scalar" && Variable->Exposure->Bounded ? Variable->Exposure->Maximum : Variable->Maximum;
                            RenderParameterControl(Variable->Address, Variable->Exposure->Name, Variable->Category, Minimum, Maximum, .01, "", PropertyX, PropertyW);
                            const std::string Linked = "Linked to " + Variable->Name;
                            ConstructMaterialCaption(PropertyX, ImGui::GetCursorScreenPos().y + 5 * Scale, 8 * Scale, 0x808d75, Linked.c_str());
                            Gap(10 * Scale);
                            ImGui::Dummy(ImVec2(1, 1));
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                    }
                    ImGui::Dummy(ImVec2(1, 1));
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
                Gap(12 * Scale);
            }
        }
        else
        {
            RenderIntro("PHYSICAL VIEW", "PBR layer stack", "Outside → inside. Every field accepts a value, texture or generator. Its selected input and editable controls live directly under that field.");

            // 📝 One labelled MW row (caption 9px #999, control 5px below), shared by the
            // generator sheet and source controls. Returns the control's top Y.
            const auto MwLabel = [&](const char *Caption, float X) -> float
            {
                AnchorX(X);
                const float TopY = ImGui::GetCursorScreenPos().y;
                ConstructMaterialCaption(X, TopY + 3 * Scale, 9 * Scale, 0x999999, Caption);
                return TopY + 16 * Scale;
            };
            const auto MwHelp = [&](const char *Text, float X, float W)
            {
                Gap(11 * Scale);
                const float Height = ConstructInspectorTextBlock(ImVec2(X, ImGui::GetCursorScreenPos().y), W, 9, 1.7f, 0x888888, Text, Scale);
                Gap(Height + 11 * Scale);
            };

            std::function<void(const MaterialAuthoringField &, float, float, bool)> RenderGeneratorSheet =
                [&](const MaterialAuthoringField &Field, float X, float W, bool Inline)
            {
                const std::string FoldKey = "generator:" + Field.Identity;
                const bool Closed = FoldedInspectorCards.contains(FoldKey);
                const float Pad = (Inline ? 10 : 14) * Scale;
                ImGui::PushID(Field.Identity.c_str());
                AnchorX(X);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.133f, .133f, .133f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .047f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, (Inline ? 10 : 16) * Scale);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
                if (ImGui::BeginChild("##GeneratorSheet", ImVec2(W, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    const float InnerX = ImGui::GetCursorScreenPos().x;
                    const float InnerW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                    // Summary: name + category, click folds the sheet.
                    const float SummaryY = ImGui::GetCursorScreenPos().y;
                    ImGui::SetCursorScreenPos(ImVec2(InnerX, SummaryY));
                    if (ImGui::InvisibleButton("##GeneratorSummary", ImVec2(InnerW, 16 * Scale)))
                    {
                        if (!FoldedInspectorCards.erase(FoldKey)) FoldedInspectorCards.insert(FoldKey);
                    }
                    ConstructMaterialCaption(InnerX, SummaryY + 3 * Scale, (Inline ? 11 : 13) * Scale, 0xc2d3b6, Field.Name.c_str());
                    const float CategoryW = std::min(InspectorTextWidth(8, Field.Category.c_str(), Scale), 110 * Scale);
                    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 8 * 1.302f * Scale,
                        ImVec2(InnerX + InnerW - CategoryW, SummaryY + 4 * Scale), Ink255(0x869778), Field.Category.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(InnerX, SummaryY + 16 * Scale));
                    if (!Closed)
                    {
                        const auto EditField = [&](const std::function<void(MaterialAuthoringField &)> &Mutate, bool Propagate = false)
                        {
                            EditAuthoring([&](MaterialAuthoringDocument &Doc)
                            {
                                for (auto &Candidate : Doc.Generators)
                                    if (Candidate.Identity == Field.Identity) Mutate(Candidate);
                            }, Propagate);
                        };
                        Gap(12 * Scale);
                        float ControlY = MwLabel("Name", InnerX);
                        TextField("genname:" + Field.Identity, InspectorNameText.data(), InspectorNameText.size(), InspectorNameKey,
                            ImVec2(InnerX, ControlY), InnerW, Field.Name,
                            [&](const std::string &Text) { EditField([&](MaterialAuthoringField &Target) { Target.Name = Text.substr(0, 80); }); });
                        ImGui::SetCursorScreenPos(ImVec2(InnerX, ControlY + 27 * Scale));
                        Gap(12 * Scale);
                        ControlY = MwLabel("Category", InnerX);
                        TextField("gencat:" + Field.Identity, InspectorCategoryText.data(), InspectorCategoryText.size(), InspectorCategoryKey,
                            ImVec2(InnerX, ControlY), InnerW, Field.Category,
                            [&](const std::string &Text) { EditField([&](MaterialAuthoringField &Target) { Target.Category = Text.substr(0, 60); }); });
                        ImGui::SetCursorScreenPos(ImVec2(InnerX, ControlY + 27 * Scale));
                        // Enabled toggle (.mw-toggle).
                        Gap(12 * Scale);
                        AnchorX(InnerX);
                        const float ToggleY = ImGui::GetCursorScreenPos().y;
                        bool Enabled = Field.Enabled;
                        if (ConstructInspectorCheckboxAt("##GeneratorEnabled", ImVec2(InnerX, ToggleY), Enabled, Scale))
                            EditField([&](MaterialAuthoringField &Target) { Target.Enabled = Enabled; });
                        ConstructMaterialCaption(InnerX + 19 * Scale, ToggleY + 2 * Scale, 9 * Scale, 0xa7b29d, "Enabled");
                        ImGui::SetCursorScreenPos(ImVec2(InnerX, ToggleY + 12 * Scale));
                        std::string Outputs;
                        for (size_t Index = 0; Index < Field.Outputs.size(); ++Index) Outputs += (Index ? " · " : "") + Field.Outputs[Index];
                        Outputs += " outputs";
                        MwHelp(Outputs.c_str(), InnerX, InnerW);
                        const auto Specs = BuildGeneratorInputs(Field, Controls);
                        const bool Camouflage = Field.Procedure.starts_with("camouflage");
                        if (Camouflage)
                        {
                            ControlY = MwLabel("Edge style", InnerX);
                            SelectField("camo-edge:" + Field.Identity, ImVec2(InnerX, ControlY), InnerW,
                                {{"camouflage", "Pixelated", false}, {"camouflage-smooth", "Smooth / organic", false}}, Field.Procedure, false,
                                [&](const std::string &Value)
                                {
                                    EditField([&](MaterialAuthoringField &Target) { Target.Procedure = Value; });
                                    EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                    {
                                        for (auto &Candidate : Doc.Generators)
                                            if (Candidate.Identity == Field.Identity)
                                                for (const auto &[Key, Quantity] : std::array<std::pair<const char *, double>, 2>{{{"softness", .012}, {"pixelSize", 4.}}})
                                                {
                                                    const std::string Address = "field:" + Candidate.Identity + ":" + Key;
                                                    if (std::find(Candidate.Inputs.begin(), Candidate.Inputs.end(), Address) == Candidate.Inputs.end())
                                                        Candidate.Inputs.push_back(Address);
                                                    if (!Doc.Inputs.contains(Address)) Doc.Inputs[Address] = Quantity;
                                                }
                                    });
                                });
                            ImGui::SetCursorScreenPos(ImVec2(InnerX, ControlY + 27 * Scale));
                            // Colour array fieldset.
                            Gap(12 * Scale);
                            AnchorX(InnerX);
                            const float SetTop = ImGui::GetCursorScreenPos().y;
                            auto *Body = ImGui::GetWindowDrawList();
                            Body->ChannelsSplit(2);
                            Body->ChannelsSetCurrent(1);
                            ImGui::SetCursorScreenPos(ImVec2(InnerX + 10 * Scale, SetTop + 10 * Scale));
                            ConstructMaterialCaption(InnerX + 10 * Scale, SetTop + 13 * Scale, 10 * Scale, 0xb9c7ad, "Colour array / ramp");
                            Gap(14 * Scale);
                            std::vector<InspectorInputSpec> Palette;
                            for (const auto &Spec : Specs)
                                if (Spec.Category == "colour") Palette.push_back(Spec);
                            std::vector<uint32_t> PreviewStops;
                            for (const auto &Spec : Palette)
                            {
                                std::string Hex = "#ffffff";
                                if (const auto Q = ReadQuantity(Spec.Address); Q && std::holds_alternative<std::string>(*Q)) Hex = std::get<std::string>(*Q);
                                std::array<float, 3> Srgb{};
                                (void)ParseHexColour(Hex, Srgb);
                                PreviewStops.push_back((uint32_t(std::lround(Srgb[0] * 255)) << 16) | (uint32_t(std::lround(Srgb[1] * 255)) << 8) | uint32_t(std::lround(Srgb[2] * 255)));
                            }
                            AnchorX(InnerX + 10 * Scale);
                            const float PreviewY = ImGui::GetCursorScreenPos().y;
                            DrawInspectorGradient(ImVec2(InnerX + 10 * Scale, PreviewY), ImVec2(InnerX + InnerW - 10 * Scale, PreviewY + 24 * Scale), PreviewStops, 5, Scale, 255, 0x222222);
                            ImGui::SetCursorScreenPos(ImVec2(InnerX + 10 * Scale, PreviewY + 24 * Scale));
                            Gap(10 * Scale);
                            for (const auto &Spec : Palette)
                            {
                                RenderParameterControl(Spec.Address, Spec.Caption, Spec.Category, Spec.Minimum, Spec.Maximum, Spec.Step, Spec.Unit, InnerX + 10 * Scale, InnerW - 20 * Scale);
                                RenderVariableActions(Spec.Address, Spec.Category, Spec.Minimum, Spec.Maximum, Field.Name + " " + Spec.Caption, InnerX + 10 * Scale, InnerW - 20 * Scale);
                            }
                            Gap(10 * Scale);
                            AnchorX(InnerX + 10 * Scale);
                            const float PaletteActionsY = ImGui::GetCursorScreenPos().y;
                            ImGui::BeginDisabled(Palette.size() >= 8);
                            if (ConstructInspectorButton("+ Add colour##CamoAdd", ImVec2(InnerX + 10 * Scale, PaletteActionsY), ImVec2(96 * Scale, 24 * Scale), Scale, 9))
                            {
                                int Highest = -1;
                                for (const auto &Spec : Palette)
                                {
                                    const size_t Split = Spec.Address.rfind(':');
                                    Highest = std::max(Highest, std::atoi(Spec.Address.substr(Split + 7).c_str()));
                                }
                                const std::string Address = "field:" + Field.Identity + ":colour" + std::to_string(Highest + 1);
                                EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                {
                                    for (auto &Candidate : Doc.Generators)
                                        if (Candidate.Identity == Field.Identity) Candidate.Inputs.push_back(Address);
                                    Doc.Inputs[Address] = std::string("#827957");
                                });
                            }
                            ImGui::EndDisabled();
                            ImGui::BeginDisabled(Palette.size() <= 2);
                            if (!Palette.empty() && ConstructInspectorButton("Remove last colour##CamoRemove", ImVec2(InnerX + 112 * Scale, PaletteActionsY), ImVec2(128 * Scale, 24 * Scale), Scale, 9))
                            {
                                const std::string Address = Palette.back().Address;
                                EditField([&](MaterialAuthoringField &Target)
                                {
                                    Target.Inputs.erase(std::remove(Target.Inputs.begin(), Target.Inputs.end(), Address), Target.Inputs.end());
                                });
                            }
                            ImGui::EndDisabled();
                            ImGui::SetCursorScreenPos(ImVec2(InnerX + 10 * Scale, PaletteActionsY + 24 * Scale));
                            Gap(10 * Scale);
                            const float SetBottom = ImGui::GetCursorScreenPos().y;
                            Body->ChannelsSetCurrent(0);
                            Body->AddRect(ImVec2(InnerX, SetTop), ImVec2(InnerX + InnerW, SetBottom), Ink255(0xffffff, 24), 8 * Scale);
                            Body->ChannelsMerge();
                            ImGui::SetCursorScreenPos(ImVec2(InnerX, SetBottom));
                            Gap(12 * Scale);
                        }
                        for (const auto &Spec : Specs)
                        {
                            if (Camouflage && Spec.Category == "colour") continue;
                            auto *Body = ImGui::GetWindowDrawList();
                            AnchorX(InnerX);
                            const float DividerY = ImGui::GetCursorScreenPos().y;
                            Body->AddLine(ImVec2(InnerX, DividerY), ImVec2(InnerX + InnerW, DividerY), Ink255(0xffffff, 8));
                            Gap(11 * Scale);
                            RenderParameterControl(Spec.Address, Spec.Caption, Spec.Category, Spec.Minimum, Spec.Maximum, Spec.Step, Spec.Unit, InnerX, InnerW);
                            RenderLinkSelect(Spec.Address, Spec.Category, Spec.Minimum, Spec.Maximum, "Shared input", 8, 0x8e9b85, InnerX, InnerW);
                            RenderVariableActions(Spec.Address, Spec.Category, Spec.Minimum, Spec.Maximum, Field.Name + " " + Spec.Caption, InnerX, InnerW);
                        }
                        if (Field.Procedure == "flakes") RenderFlakeRamp(InnerX, InnerW, false);
                        const float SmallHeight = GlyphTextBlock(ImVec2(InnerX, ImGui::GetCursorScreenPos().y), InnerW, 8, 1.6f, 0x8c987e,
                            "Shared instance: edits here affect every channel using it. Expose inputs to Properties with ◇.");
                        Gap(SmallHeight);
                    }
                    ImGui::Dummy(ImVec2(1, 1));
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            };

            const auto RenderSourceControls = [&](const MaterialAuthoringLayer &Layer, const std::string &Channel, float X, float W, bool Expanded)
            {
                const auto *Specification = QueryMaterialPbrChannel(Channel);
                if (!Specification) return;
                const std::string Address = MaterialChannelAddress(Layer, Channel);
                std::string Root = Address;
                try { Root = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
                MaterialAuthoringSource Source;
                Source.Mode = "value";
                Source.Stops = {"#000000", "#ffffff"};
                if (const auto Found = Document.Sources.find(Root); Found != Document.Sources.end()) Source = Found->second;
                if (Source.Stops.empty()) Source.Stops = {"#000000", "#ffffff"};
                std::vector<const MaterialAuthoringField *> NativeGenerators;
                if (Layer.Model == "flakes" && Channel != "coverage")
                {
                    for (const auto &Field : Document.Generators)
                        if (Field.Identity == Layer.Generator) NativeGenerators.push_back(&Field);
                }
                else if (Layer.Model == "surface")
                {
                    for (const auto &Field : Document.Generators)
                        if (!Field.Coded && Field.Procedure != "flakes" &&
                            std::find(Field.Outputs.begin(), Field.Outputs.end(), Channel) != Field.Outputs.end())
                            NativeGenerators.push_back(&Field);
                }
                const bool NativeField = !NativeGenerators.empty();
                const auto AmendSource = [&](const std::function<void(MaterialAuthoringSource &)> &Patch)
                {
                    EditAuthoring([&](MaterialAuthoringDocument &Doc)
                    {
                        MaterialAuthoringSource Next = Source;
                        if (const auto Found = Doc.Sources.find(Root); Found != Doc.Sources.end()) Next = Found->second;
                        Patch(Next);
                        Doc.Sources[Root] = Next;
                    });
                };
                const float ControlY = MwLabel("Input type", X);
                std::vector<InspectorSelectOption> ModeOptions;
                ModeOptions.push_back({"value", NativeField ? "Generator · native material" :
                    std::string(Specification->Category) == "colour" ? "Value · solid colour" : "Value · single value", false});
                if (NativeField) ModeOptions.push_back({"solid", "Value · single value", false});
                ModeOptions.push_back({"texture", "Texture / image", false});
                ModeOptions.push_back({"", "Generators", true});
                ModeOptions.push_back({"linear", "Linear gradient", false});
                ModeOptions.push_back({"radial", "Radial gradient", false});
                ModeOptions.push_back({"conic", "Conic gradient", false});
                ModeOptions.push_back({"ramp", "Colour ramp", false});
                ModeOptions.push_back({"field", "Procedural generator…", false});
                if (Source.Mode == "pattern") ModeOptions.push_back({"pattern", "Applied pattern", false});
                SelectField("source:" + Root, ImVec2(X, ControlY), W, ModeOptions, Source.Mode, false,
                    [&](const std::string &Value)
                    {
                        AmendSource([&](MaterialAuthoringSource &Next) { Next.Mode = Value; Next.Direct = true; });
                        FoldedInspectorCards.erase("channel:" + Layer.Identity + ":" + Channel);
                    });
                ImGui::SetCursorScreenPos(ImVec2(X, ControlY + 27 * Scale));
                if (!Expanded) return;
                Gap(10 * Scale);
                const bool ValueMode = Source.Mode == "value" || Source.Mode == "solid";
                if (ValueMode && !(NativeField && Source.Mode == "value"))
                {
                    RenderParameterControl(Address, Specification->Caption, Specification->Category,
                        Specification->Minimum, Specification->Maximum, .01, "", X, W);
                    Gap(10 * Scale);
                }
                if (NativeField && Source.Mode == "value")
                {
                    auto *Body = ImGui::GetWindowDrawList();
                    AnchorX(X);
                    const float InlineTop = ImGui::GetCursorScreenPos().y;
                    for (const auto *Field : NativeGenerators)
                    {
                        Gap(12 * Scale);
                        RenderGeneratorSheet(*Field, X + 11 * Scale, W - 11 * Scale, true);
                    }
                    const float InlineBottom = ImGui::GetCursorScreenPos().y;
                    Body->AddLine(ImVec2(X, InlineTop), ImVec2(X, InlineBottom), Ink255(0xa9bd9c, 68), 2 * Scale);
                    Gap(10 * Scale);
                }
                if (!ValueMode && Source.Direct != true)
                {
                    const std::string AdjustKey = "adjust:" + Root;
                    const bool AdjustOpen = OpenedInspectorDetails.contains(AdjustKey);
                    AnchorX(X);
                    const float SummaryY = ImGui::GetCursorScreenPos().y;
                    ImGui::SetCursorScreenPos(ImVec2(X, SummaryY));
                    ImGui::PushID(AdjustKey.c_str());
                    if (ImGui::InvisibleButton("##AdjustSummary", ImVec2(W, 15 * Scale)))
                    {
                        if (!OpenedInspectorDetails.erase(AdjustKey)) OpenedInspectorDetails.insert(AdjustKey);
                    }
                    ImGui::PopID();
                    ConstructMaterialCaption(X, SummaryY + 3 * Scale, 10 * Scale, 0xb6c9a6, "Existing source adjustment");
                    ImGui::SetCursorScreenPos(ImVec2(X, SummaryY + 15 * Scale));
                    if (AdjustOpen)
                    {
                        Gap(10 * Scale);
                        const std::string Category = Specification->Category;
                        const std::string Label = std::string(Specification->Caption) + (Category == "scalar" ? " map maximum" : Category == "colour" ? " tint" : " offset");
                        RenderParameterControl(Address, Label, Category, Specification->Minimum, Specification->Maximum, .01, "", X, W);
                        const float SmallHeight = ConstructInspectorTextBlock(ImVec2(X, ImGui::GetCursorScreenPos().y + 5 * Scale), W, 8, 1.7f, 0x888888,
                            "Retained for previously authored maps. Reselect the input type to use its output directly.", Scale);
                        Gap(5 * Scale + SmallHeight + 5 * Scale);
                    }
                    Gap(10 * Scale);
                }
                if (Source.Mode == "linear" || Source.Mode == "radial" || Source.Mode == "conic")
                {
                    const float ColW = (W - 7 * Scale) * .5f;
                    AnchorX(X);
                    const float RowTop = ImGui::GetCursorScreenPos().y;
                    for (int Index = 0; Index < 2; ++Index)
                    {
                        std::array<float, 3> Srgb{1, 1, 1};
                        (void)ParseHexColour(Source.Stops[size_t(Index)], Srgb);
                        ImGui::PushID(Index + 1300);
                        if (ConstructInspectorColourSwatch("##GradientStop", ImVec2(X + Index * (ColW + 7 * Scale), RowTop), ImVec2(ColW, 33 * Scale), Srgb, 7, Scale))
                        {
                            const std::string Hex = EncodeHexColour(Srgb);
                            AmendSource([&, Index](MaterialAuthoringSource &Next)
                            {
                                if (Next.Stops.size() < 2) Next.Stops = {"#000000", "#ffffff"};
                                Next.Stops[size_t(Index)] = Hex;
                            });
                        }
                        ImGui::PopID();
                    }
                    std::vector<uint32_t> Stops;
                    for (const auto &Hex : Source.Stops)
                    {
                        std::array<float, 3> Srgb{};
                        (void)ParseHexColour(Hex, Srgb);
                        Stops.push_back((uint32_t(std::lround(Srgb[0] * 255)) << 16) | (uint32_t(std::lround(Srgb[1] * 255)) << 8) | uint32_t(std::lround(Srgb[2] * 255)));
                    }
                    DrawInspectorGradient(ImVec2(X, RowTop + 40 * Scale), ImVec2(X + W, RowTop + 64 * Scale), Stops, 7, Scale, 255, 0x171717);
                    ImGui::SetCursorScreenPos(ImVec2(X, RowTop + 64 * Scale));
                    Gap(10 * Scale);
                }
                else if (Source.Mode == "ramp")
                {
                    const float ColW = (W - 6 * Scale) * .5f;
                    AnchorX(X);
                    float RowTop = ImGui::GetCursorScreenPos().y;
                    for (size_t Index = 0; Index < Source.Stops.size(); ++Index)
                    {
                        const float ColX = X + (Index % 2) * (ColW + 6 * Scale);
                        const float CellY = RowTop + (Index / 2) * 56 * Scale;
                        const std::string Label = "Stop " + std::to_string(Index + 1);
                        ConstructMaterialCaption(ColX, CellY + 3 * Scale, 8 * Scale, 0xaaaaaa, Label.c_str());
                        std::array<float, 3> Srgb{1, 1, 1};
                        (void)ParseHexColour(Source.Stops[Index], Srgb);
                        ImGui::PushID(int(Index) + 1400);
                        if (ConstructInspectorColourSwatch("##RampStop", ImVec2(ColX, CellY + 14 * Scale), ImVec2(ColW, 33 * Scale), Srgb, 7, Scale))
                        {
                            const std::string Hex = EncodeHexColour(Srgb);
                            AmendSource([&, Index](MaterialAuthoringSource &Next)
                            {
                                if (Index < Next.Stops.size()) Next.Stops[Index] = Hex;
                            });
                        }
                        ImGui::PopID();
                    }
                    RowTop += ((Source.Stops.size() + 1) / 2) * 56 * Scale;
                    ImGui::SetCursorScreenPos(ImVec2(X, RowTop));
                    ImGui::BeginDisabled(Source.Stops.size() >= 8);
                    if (ConstructInspectorButton("+ Ramp stop##AddRampStop", ImVec2(X, RowTop), ImVec2(W, 26 * Scale), Scale, 9))
                        AmendSource([](MaterialAuthoringSource &Next) { Next.Stops.push_back("#808080"); });
                    ImGui::EndDisabled();
                    ImGui::SetCursorScreenPos(ImVec2(X, RowTop + 26 * Scale));
                    Gap(10 * Scale);
                }
                else if (Source.Mode == "texture")
                {
                    Gap(2 * Scale);
                    AnchorX(X);
                    const float PreviewTop = ImGui::GetCursorScreenPos().y;
                    const float PreviewSize = std::min(W, 180 * Scale);
                    const float PreviewX = X + (W - PreviewSize) * .5f;
                    auto *Body = ImGui::GetWindowDrawList();
                    DrawInspectorCheckerboard(ImVec2(PreviewX, PreviewTop), ImVec2(PreviewX + PreviewSize, PreviewTop + PreviewSize), 16, 0x292929, 0x202020, Scale);
                    Body->AddRect(ImVec2(PreviewX, PreviewTop), ImVec2(PreviewX + PreviewSize, PreviewTop + PreviewSize), Ink255(0xffffff, 26), 8 * Scale);
                    const char *PreviewCaption = Source.Texture ? Source.Texture->Name.c_str() : "No texture selected";
                    const float CaptionW = InspectorTextWidth(10, PreviewCaption, Scale) + 16 * Scale;
                    const ImVec2 CaptionMin(PreviewX + (PreviewSize - CaptionW) * .5f, PreviewTop + (PreviewSize - 27 * Scale) * .5f);
                    Body->AddRectFilled(CaptionMin, ImVec2(CaptionMin.x + CaptionW, CaptionMin.y + 27 * Scale), Ink255(0x161616, 192), 5 * Scale);
                    Body->AddText(ImGui::GetFont(), 10 * 1.302f * Scale, ImVec2(CaptionMin.x + 8 * Scale, CaptionMin.y + 13.5f * Scale - 10 * 1.302f * Scale * .5f), Ink255(0xa0a094), PreviewCaption);
                    ImGui::SetCursorScreenPos(ImVec2(X, PreviewTop + PreviewSize));
                    Gap(12 * Scale);
                    AnchorX(X);
                    const float LoadY = ImGui::GetCursorScreenPos().y;
                    ConstructMaterialCaption(X, LoadY + 3 * Scale, 10 * Scale, 0xb9b9a9, "Load from file");
                    const float InputY = LoadY + 20 * Scale;
                    TextField("texturepath:" + Root, TexturePathText.data(), TexturePathText.size(), TexturePathKey,
                        ImVec2(X, InputY), W - 62 * Scale, std::string(TexturePathText.data()), [](const std::string &) {});
                    if (ConstructInspectorButton("Load##LoadTexture", ImVec2(X + W - 56 * Scale, InputY), ImVec2(56 * Scale, 27 * Scale), Scale, 9))
                    {
                        MaterialAuthoringTexture Texture;
                        std::string Error;
                        if (LoadInspectorTextureFile(TexturePathText.data(), Texture, Error))
                            AmendSource([&](MaterialAuthoringSource &Next) { Next.Texture = Texture; });
                        else InspectorNotice = Error;
                    }
                    ImGui::SetCursorScreenPos(ImVec2(X, InputY + 27 * Scale));
                    if (Source.Texture)
                    {
                        const std::string Small = Source.Texture->Name + " · " + std::to_string(Source.Texture->Width) + " × " + std::to_string(Source.Texture->Height);
                        const float Height = ConstructInspectorTextBlock(ImVec2(X, ImGui::GetCursorScreenPos().y + 8 * Scale), W, 9, 1.7f, 0x909080, Small.c_str(), Scale);
                        Gap(8 * Scale + Height);
                    }
                    Gap(12 * Scale);
                }
                else if (Source.Mode == "field")
                {
                    const float SelectY = MwLabel("Shared generator", X);
                    std::vector<InspectorSelectOption> Options{{"", "Choose a field instance…", false}};
                    for (const auto &Field : Document.Generators)
                        if (Field.Coded) Options.push_back({Field.Identity, Field.Name + " · " + Field.Category, false});
                    SelectField("fieldgen:" + Root, ImVec2(X, SelectY), W, Options, Source.Generator, false,
                        [&](const std::string &Value) { AmendSource([&](MaterialAuthoringSource &Next) { Next.Generator = Value; }); });
                    ImGui::SetCursorScreenPos(ImVec2(X, SelectY + 27 * Scale));
                    const float SmallHeight = ConstructInspectorTextBlock(ImVec2(X, ImGui::GetCursorScreenPos().y + 6 * Scale), W, 8, 1.7f, 0x888888,
                        "Reuse a named instance, or create one below. Its inputs are edited here in the channel.", Scale);
                    Gap(6 * Scale + SmallHeight);
                    // Inline generator section (left rule).
                    auto *Body = ImGui::GetWindowDrawList();
                    AnchorX(X);
                    const float InlineTop = ImGui::GetCursorScreenPos().y + 12 * Scale;
                    ImGui::SetCursorScreenPos(ImVec2(X, InlineTop));
                    const float InnerX = X + 11 * Scale, InnerW = W - 11 * Scale;
                    // Create-generator details.
                    const std::string NewKey = "newfield:" + Root;
                    const bool NewOpen = OpenedInspectorDetails.contains(NewKey) ||
                        (!FoldedInspectorCards.contains(NewKey) && Source.Generator.empty());
                    ImGui::PushID(NewKey.c_str());
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.133f, .133f, .133f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .047f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10 * Scale);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * Scale, 10 * Scale));
                    AnchorX(InnerX);
                    if (ImGui::BeginChild("##NewField", ImVec2(InnerW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                    {
                        const float FormX = ImGui::GetCursorScreenPos().x;
                        const float FormW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                        const float SummaryY = ImGui::GetCursorScreenPos().y;
                        ImGui::SetCursorScreenPos(ImVec2(FormX, SummaryY));
                        if (ImGui::InvisibleButton("##NewFieldSummary", ImVec2(FormW, 15 * Scale)))
                        {
                            if (NewOpen) { OpenedInspectorDetails.erase(NewKey); FoldedInspectorCards.insert(NewKey); }
                            else { FoldedInspectorCards.erase(NewKey); OpenedInspectorDetails.insert(NewKey); }
                        }
                        ConstructMaterialCaption(FormX, SummaryY + 3 * Scale, 10 * Scale, 0xb6c9a6, "Create generator in this channel");
                        ImGui::SetCursorScreenPos(ImVec2(FormX, SummaryY + 15 * Scale));
                        if (NewOpen)
                        {
                            Gap(12 * Scale);
                            float FormControlY = MwLabel("Instance name", FormX);
                            ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
                            (void)ConstructInspectorTextInput("##NewFieldName", ImVec2(FormX, FormControlY), FormW, FieldNameText.data(), FieldNameText.size(), Scale);
                            ImGui::PopFont();
                            ImGui::SetCursorScreenPos(ImVec2(FormX, FormControlY + 27 * Scale));
                            Gap(12 * Scale);
                            FormControlY = MwLabel("Category", FormX);
                            ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
                            (void)ConstructInspectorTextInput("##NewFieldCategory", ImVec2(FormX, FormControlY), FormW, FieldCategoryText.data(), FieldCategoryText.size(), Scale);
                            ImGui::PopFont();
                            ImGui::SetCursorScreenPos(ImVec2(FormX, FormControlY + 27 * Scale));
                            Gap(12 * Scale);
                            FormControlY = MwLabel("Procedure", FormX);
                            std::vector<InspectorSelectOption> Procedures{{"", "Choose procedure…", false}};
                            for (const auto &[Identity, Label] : CodedGeneratorOptions()) Procedures.push_back({Identity, Label, false});
                            SelectField("newprocedure", ImVec2(FormX, FormControlY), FormW, Procedures, "", false,
                                [&](const std::string &Procedure)
                                {
                                    if (Procedure.empty()) return;
                                    std::string Name = FieldNameText.data();
                                    std::string Category = FieldCategoryText.data();
                                    const auto Trimmed = [](std::string Text)
                                    {
                                        const size_t Begin = Text.find_first_not_of(" \t");
                                        const size_t End = Text.find_last_not_of(" \t");
                                        return Begin == std::string::npos ? std::string{} : Text.substr(Begin, End - Begin + 1);
                                    };
                                    Name = Trimmed(Name);
                                    Category = Trimmed(Category);
                                    if (Name.empty()) Name = "Surface field";
                                    if (Category.empty()) Category = "Surface fields";
                                    EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                    {
                                        const std::string Identity = DeclareCodedField(Doc, Procedure, Name, Category);
                                        MaterialAuthoringSource Next = Source;
                                        if (const auto Found = Doc.Sources.find(Root); Found != Doc.Sources.end()) Next = Found->second;
                                        Next.Mode = "field";
                                        Next.Generator = Identity;
                                        Doc.Sources[Root] = Next;
                                    });
                                });
                            ImGui::SetCursorScreenPos(ImVec2(FormX, FormControlY + 27 * Scale));
                        }
                        ImGui::Dummy(ImVec2(1, 1));
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                    for (const auto &Field : Document.Generators)
                        if (Field.Identity == Source.Generator)
                        {
                            Gap(12 * Scale);
                            RenderGeneratorSheet(Field, InnerX, InnerW, true);
                        }
                    const float InlineBottom = ImGui::GetCursorScreenPos().y;
                    Body->AddLine(ImVec2(X, InlineTop), ImVec2(X, InlineBottom), Ink255(0xa9bd9c, 68), 2 * Scale);
                    Gap(10 * Scale);
                }
                else if (Source.Mode == "pattern")
                {
                    const std::string Help = std::string(Specification->Caption) + " output from the applied pattern.";
                    MwHelp(Help.c_str(), X, W);
                    AnchorX(X);
                    const float ButtonY = ImGui::GetCursorScreenPos().y;
                    if (ConstructInspectorButton("Edit pattern artwork##EditPattern", ImVec2(X, ButtonY), ImVec2(W, 26 * Scale), Scale, 9))
                    {
                        if (ActivatePattern) ActivatePattern();
                    }
                    ImGui::SetCursorScreenPos(ImVec2(X, ButtonY + 26 * Scale));
                    Gap(10 * Scale);
                }
                if (!NativeField || Source.Mode != "value")
                {
                    RenderLinkSelect(Address, Specification->Category, Specification->Minimum, Specification->Maximum, "Shared parameter", 9, 0x999999, X, W);
                    RenderVariableActions(Address, Specification->Category, Specification->Minimum, Specification->Maximum,
                        Layer.Name + " " + Specification->Caption, X, W);
                }
            };

            const std::vector<MaterialAuthoringLayer> LayersSnapshot = Document.Layers;
            for (size_t LayerIndex = 0; LayerIndex < LayersSnapshot.size(); ++LayerIndex)
            {
                const MaterialAuthoringLayer Layer = LayersSnapshot[LayerIndex];
                bool RemovedLayer = false;
                ImGui::PushID(Layer.Identity.c_str());
                AnchorX(StartX);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.125f, .125f, .125f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .039f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 19 * Scale);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 14 * Scale));
                if (ImGui::BeginChild("##LayerCard", ImVec2(ContentW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    auto *Body = ImGui::GetWindowDrawList();
                    const float InnerX = ImGui::GetCursorScreenPos().x;
                    const float InnerW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                    const float HeaderY = ImGui::GetCursorScreenPos().y;
                    // Ordinal disc.
                    Body->AddCircleFilled(ImVec2(InnerX + 13 * Scale, HeaderY + 13 * Scale), 13 * Scale, Ink255(0xafc79f, 18));
                    char Ordinal[8];
                    std::snprintf(Ordinal, sizeof Ordinal, "%02u", unsigned(LayerIndex + 1));
                    const float OrdinalW = InspectorTextWidth(9, Ordinal, Scale);
                    Body->AddText(ImGui::GetFont(), 9 * 1.302f * Scale, ImVec2(InnerX + 13 * Scale - OrdinalW * .5f, HeaderY + 13 * Scale - 9 * 1.302f * Scale * .5f), Ink255(0xa5bd95), Ordinal);
                    // Borderless name input.
                    const float NameX = InnerX + 33 * Scale;
                    const float NameW = InnerW - 33 * Scale - 19 * Scale - (Layer.Model == "detail" ? 27 * Scale : 0);
                    {
                        const std::string NameId = "layername:" + Layer.Identity;
                        const bool Mine = InspectorNameKey == NameId;
                        std::array<char, 96> Local{};
                        char *Buffer = InspectorNameText.data();
                        if (!Mine) { std::snprintf(Local.data(), Local.size(), "%s", Layer.Name.c_str()); Buffer = Local.data(); }
                        ImGui::PushFont(nullptr, 14 * 1.302f * Scale);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * Scale, 2 * Scale));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Ink255(0xdcdcd2)));
                        ImGui::SetCursorScreenPos(ImVec2(NameX, HeaderY + 2 * Scale));
                        ImGui::SetNextItemWidth(NameW);
                        const bool Edited = ImGui::InputText("##LayerName", Buffer, Mine ? InspectorNameText.size() : Local.size());
                        const bool Activated = ImGui::IsItemActivated(), Deactivated = ImGui::IsItemDeactivated();
                        ImGui::PopStyleColor(4);
                        ImGui::PopStyleVar(2);
                        ImGui::PopFont();
                        if (Edited)
                        {
                            if (!Mine) { InspectorNameKey = NameId; std::snprintf(InspectorNameText.data(), InspectorNameText.size(), "%s", Local.data()); }
                            const std::string Text(Buffer);
                            EditAuthoring([&](MaterialAuthoringDocument &Doc)
                            {
                                for (auto &Candidate : Doc.Layers)
                                    if (Candidate.Identity == Layer.Identity) Candidate.Name = Text.substr(0, 80);
                            });
                        }
                        else if (!Mine && Activated) { InspectorNameKey = NameId; std::snprintf(InspectorNameText.data(), InspectorNameText.size(), "%s", Local.data()); }
                        if (Mine && Deactivated) InspectorNameKey.clear();
                    }
                    bool LayerEnabled = Layer.Enabled;
                    if (ConstructInspectorCheckboxAt("##LayerEnabled", ImVec2(InnerX + InnerW - 12 * Scale - (Layer.Model == "detail" ? 27 * Scale : 0), HeaderY + 7 * Scale), LayerEnabled, Scale))
                    {
                        EditAuthoring([&](MaterialAuthoringDocument &Doc)
                        {
                            for (auto &Candidate : Doc.Layers)
                                if (Candidate.Identity == Layer.Identity) Candidate.Enabled = LayerEnabled;
                        });
                    }
                    if (Layer.Model == "detail")
                    {
                        ImVec2 Centre;
                        if (ConstructInspectorIconButton("##LayerRemove", ImVec2(InnerX + InnerW - 24 * Scale, HeaderY + 1 * Scale), ImVec2(24 * Scale, 24 * Scale), Scale, Centre))
                        {
                            EditAuthoring([&](MaterialAuthoringDocument &Doc)
                            {
                                Doc.Layers.erase(std::remove_if(Doc.Layers.begin(), Doc.Layers.end(),
                                    [&](const MaterialAuthoringLayer &Candidate) { return Candidate.Identity == Layer.Identity; }), Doc.Layers.end());
                            });
                            RemovedLayer = true;
                        }
                        DrawInspectorXGlyph(Centre, 12, 0x858577, Scale);
                    }
                    ImGui::SetCursorScreenPos(ImVec2(InnerX, HeaderY + 26 * Scale));
                    if (!RemovedLayer)
                    {
                        Gap(9 * Scale);
                        ConstructMaterialCaption(InnerX, ImGui::GetCursorScreenPos().y + 3 * Scale, 8 * Scale, 0x818779,
                            Layer.Model == "coat" ? "Dielectric coat" : Layer.Model == "flakes" ? "PBR contribution from particle field" :
                            Layer.Model == "surface" ? "Surface scattering body" : "PBR channel overlay");
                        Gap(11 * Scale + 9 * Scale);
                        // Add/remove channels menu.
                        {
                            const std::string MenuKey = "channelmenu:" + Layer.Identity;
                            const bool MenuOpen = OpenedInspectorDetails.contains(MenuKey);
                            AnchorX(InnerX);
                            ImGui::PushID(MenuKey.c_str());
                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.125f, .153f, .114f, .376f));
                            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.71f, .773f, .655f, .2f));
                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10 * Scale);
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                            if (ImGui::BeginChild("##ChannelMenu", ImVec2(InnerW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                            {
                                const float MenuX = ImGui::GetCursorScreenPos().x;
                                const float MenuY = ImGui::GetCursorScreenPos().y;
                                ImGui::SetCursorScreenPos(ImVec2(MenuX, MenuY));
                                if (ImGui::InvisibleButton("##ChannelMenuSummary", ImVec2(InnerW, 33 * Scale)))
                                {
                                    if (!OpenedInspectorDetails.erase(MenuKey)) OpenedInspectorDetails.insert(MenuKey);
                                }
                                DrawInspectorPlusGlyph(ImVec2(MenuX + 16 * Scale, MenuY + 16.5f * Scale), 12, 0xc2ceb9, Scale);
                                ConstructMaterialCaption(MenuX + 30 * Scale, MenuY + 13 * Scale, 10 * Scale, 0xc2ceb9, "Add / remove channels");
                                const std::string Count = std::to_string(Layer.Channels.size());
                                const float CountW = InspectorTextWidth(8, Count.c_str(), Scale);
                                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 8 * 1.302f * Scale,
                                    ImVec2(MenuX + InnerW - 10 * Scale - CountW, MenuY + 12 * Scale), Ink255(0x8b977f), Count.c_str());
                                ImGui::SetCursorScreenPos(ImVec2(MenuX, MenuY + 33 * Scale));
                                if (MenuOpen)
                                {
                                    auto *MenuBody = ImGui::GetWindowDrawList();
                                    MenuBody->AddLine(ImVec2(MenuX, MenuY + 33 * Scale), ImVec2(MenuX + InnerW, MenuY + 33 * Scale), Ink255(0xffffff, 16));
                                    std::vector<const MaterialPbrChannel *> Available;
                                    for (const auto &ChannelSpec : QueryMaterialPbrChannels())
                                    {
                                        if (Layer.Model == "coat" && ChannelSpec.Identity != std::string("coverage") &&
                                            ChannelSpec.Identity != std::string("roughness") && ChannelSpec.Identity != std::string("ior") &&
                                            ChannelSpec.Identity != std::string("normal")) continue;
                                        Available.push_back(&ChannelSpec);
                                    }
                                    const float ColW = (InnerW - 20 * Scale - 7 * Scale) * .5f;
                                    const float GridTop = MenuY + 39 * Scale;
                                    for (size_t Index = 0; Index < Available.size(); ++Index)
                                    {
                                        const auto *ChannelSpec = Available[Index];
                                        const float CellX = MenuX + 10 * Scale + (Index % 2) * (ColW + 7 * Scale);
                                        const float CellY = GridTop + (Index / 2) * 19 * Scale;
                                        bool Present = std::find(Layer.Channels.begin(), Layer.Channels.end(), ChannelSpec->Identity) != Layer.Channels.end();
                                        ImGui::PushID(int(Index) + 1500);
                                        if (ConstructInspectorCheckboxAt("##ChannelToggle", ImVec2(CellX, CellY), Present, Scale))
                                        {
                                            const std::string ChannelName = ChannelSpec->Identity;
                                            EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                            {
                                                for (auto &Candidate : Doc.Layers)
                                                    if (Candidate.Identity == Layer.Identity)
                                                    {
                                                        if (Present) Candidate.Channels.push_back(ChannelName);
                                                        else Candidate.Channels.erase(std::remove(Candidate.Channels.begin(), Candidate.Channels.end(), ChannelName), Candidate.Channels.end());
                                                    }
                                            });
                                        }
                                        ImGui::PopID();
                                        ConstructMaterialCaption(CellX + 20 * Scale, CellY + 2 * Scale, 9 * Scale, 0xaaaaaa, ChannelSpec->Caption);
                                    }
                                    ImGui::SetCursorScreenPos(ImVec2(MenuX, GridTop + ((Available.size() + 1) / 2) * 19 * Scale));
                                    Gap(6 * Scale);
                                }
                                ImGui::Dummy(ImVec2(1, 1));
                            }
                            ImGui::EndChild();
                            ImGui::PopStyleVar(2);
                            ImGui::PopStyleColor(2);
                            ImGui::PopID();
                        }
                        // Channel cards.
                        for (const auto &Channel : Layer.Channels)
                        {
                            const auto *Specification = QueryMaterialPbrChannel(Channel);
                            if (!Specification) continue;
                            const std::string Address = MaterialChannelAddress(Layer, Channel);
                            std::string Root = Address;
                            try { Root = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
                            const auto SourceFound = Document.Sources.find(Root);
                            const std::string ChannelFold = "channel:" + Layer.Identity + ":" + Channel;
                            const bool Open = !FoldedInspectorCards.contains(ChannelFold);
                            Gap(10 * Scale);
                            ImGui::PushID(ChannelFold.c_str());
                            AnchorX(InnerX);
                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.09f, .09f, .09f, 1.f));
                            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .035f));
                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * Scale);
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 12 * Scale));
                            bool RemovedChannel = false;
                            if (ImGui::BeginChild("##ChannelCard", ImVec2(InnerW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                            {
                                auto *ChannelBody = ImGui::GetWindowDrawList();
                                const float ChannelX = ImGui::GetCursorScreenPos().x;
                                const float ChannelW = std::max(ImGui::GetContentRegionAvail().x, 40 * Scale);
                                const float ChannelY = ImGui::GetCursorScreenPos().y;
                                ImGui::SetCursorScreenPos(ImVec2(ChannelX, ChannelY));
                                if (ImGui::InvisibleButton("##ChannelDisclosure", ImVec2(ChannelW - 56 * Scale, 15 * Scale)))
                                {
                                    if (!FoldedInspectorCards.erase(ChannelFold)) FoldedInspectorCards.insert(ChannelFold);
                                }
                                DrawInspectorChevronGlyph(ImVec2(ChannelX + 6 * Scale, ChannelY + 7.5f * Scale), 12, 0xdcdcd2, Scale, !FoldedInspectorCards.contains(ChannelFold));
                                ConstructMaterialCaption(ChannelX + 18 * Scale, ChannelY + 3 * Scale, 11 * Scale, 0xdcdcd2, Specification->Caption);
                                if (!Address.starts_with("flake-output:") || Channel == "coverage")
                                {
                                    ImVec2 Centre;
                                    if (ConstructInspectorIconButton("##ChannelShare", ImVec2(ChannelX + ChannelW - 52 * Scale, ChannelY - 2 * Scale), ImVec2(24 * Scale, 20 * Scale), Scale, Centre))
                                        ShareParameter(Address, Specification->Category, Specification->Minimum, Specification->Maximum, Layer.Name + " " + Specification->Caption);
                                    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Make or edit shared variable", "Share this channel as a variable, or open its variable in the dock.");
                                    DrawInspectorDiamondGlyph(Centre, 11, 0x858577, Scale);
                                }
                                {
                                    ImVec2 Centre;
                                    if (ConstructInspectorIconButton("##ChannelRemove", ImVec2(ChannelX + ChannelW - 24 * Scale, ChannelY - 2 * Scale), ImVec2(24 * Scale, 20 * Scale), Scale, Centre))
                                    {
                                        EditAuthoring([&](MaterialAuthoringDocument &Doc)
                                        {
                                            for (auto &Candidate : Doc.Layers)
                                                if (Candidate.Identity == Layer.Identity)
                                                    Candidate.Channels.erase(std::remove(Candidate.Channels.begin(), Candidate.Channels.end(), Channel), Candidate.Channels.end());
                                        });
                                        RemovedChannel = true;
                                    }
                                    DrawInspectorXGlyph(Centre, 11, 0x858577, Scale);
                                }
                                ImGui::SetCursorScreenPos(ImVec2(ChannelX, ChannelY + 15 * Scale));
                                Gap(9 * Scale);
                                if (!RemovedChannel && SourceFound != Document.Sources.end() &&
                                    (SourceFound->second.Mode == "field" || SourceFound->second.Mode == "linear" || SourceFound->second.Mode == "radial" ||
                                     SourceFound->second.Mode == "conic" || SourceFound->second.Mode == "ramp" || SourceFound->second.Mode == "pattern"))
                                {
                                    AnchorX(ChannelX);
                                    const float OutputY = ImGui::GetCursorScreenPos().y;
                                    std::vector<uint32_t> Stops;
                                    for (const auto &Hex : SourceFound->second.Stops)
                                    {
                                        std::array<float, 3> Srgb{};
                                        (void)ParseHexColour(Hex, Srgb);
                                        Stops.push_back((uint32_t(std::lround(Srgb[0] * 255)) << 16) | (uint32_t(std::lround(Srgb[1] * 255)) << 8) | uint32_t(std::lround(Srgb[2] * 255)));
                                    }
                                    if (Stops.size() >= 2 && SourceFound->second.Mode != "field" && SourceFound->second.Mode != "pattern")
                                        DrawInspectorGradient(ImVec2(ChannelX, OutputY), ImVec2(ChannelX + ChannelW, OutputY + 43 * Scale), Stops, 7, Scale, 255, 0x171717);
                                    else
                                        ChannelBody->AddRectFilled(ImVec2(ChannelX, OutputY), ImVec2(ChannelX + ChannelW, OutputY + 43 * Scale), Ink255(0x20271d), 7 * Scale);
                                    std::string Chip = CaptionFromKey(SourceFound->second.Mode);
                                    if (SourceFound->second.Mode == "field")
                                    {
                                        Chip = "Choose generator";
                                        for (const auto &Field : Document.Generators)
                                            if (Field.Identity == SourceFound->second.Generator) Chip = Field.Name;
                                    }
                                    const float ChipW = InspectorTextWidth(9, Chip.c_str(), Scale) + 18 * Scale;
                                    ChannelBody->AddRectFilled(ImVec2(ChannelX + 6 * Scale, OutputY + 8 * Scale), ImVec2(ChannelX + 6 * Scale + ChipW, OutputY + 35 * Scale), Ink255(0x111111, 170), 7 * Scale);
                                    ChannelBody->AddText(ImGui::GetFont(), 9 * 1.302f * Scale, ImVec2(ChannelX + 15 * Scale, OutputY + 21.5f * Scale - 9 * 1.302f * Scale * .5f), Ink255(0xdddddd), Chip.c_str());
                                    ImGui::SetCursorScreenPos(ImVec2(ChannelX, OutputY + 43 * Scale));
                                    Gap(10 * Scale);
                                }
                                if (!RemovedChannel) RenderSourceControls(Layer, Channel, ChannelX, ChannelW, Open);
                                ImGui::Dummy(ImVec2(1, 1));
                            }
                            ImGui::EndChild();
                            ImGui::PopStyleVar(2);
                            ImGui::PopStyleColor(2);
                            ImGui::PopID();
                            if (RemovedChannel) break;
                        }
                    }
                    ImGui::Dummy(ImVec2(1, 1));
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                Gap(12 * Scale);
                if (RemovedLayer) break;
            }
            AnchorX(StartX);
            if (ConstructInspectorButton("+ Add PBR detail layer##AddLayer", ImGui::GetCursorScreenPos(), ImVec2(ContentW, 30 * Scale), Scale, 10))
            {
                EditAuthoring([](MaterialAuthoringDocument &Doc)
                {
                    if (Doc.Layers.size() >= 16) throw std::runtime_error("Maximum 16 layers.");
                    const size_t Position = !Doc.Layers.empty() && Doc.Layers[0].Model == "coat" ? 1 : 0;
                    MaterialAuthoringLayer Layer;
                    Layer.Identity = IssueMaterialAuthoringIdentity();
                    Layer.Name = "PBR detail layer";
                    Layer.Model = "detail";
                    Layer.Enabled = true;
                    Layer.Channels = {"colour", "roughness", "coverage"};
                    Doc.Inputs[MaterialChannelAddress(Layer, "colour")] = std::string("#b8ae9e");
                    Doc.Inputs[MaterialChannelAddress(Layer, "roughness")] = .42;
                    Doc.Inputs[MaterialChannelAddress(Layer, "coverage")] = .65;
                    Doc.Layers.insert(Doc.Layers.begin() + std::ptrdiff_t(Position), std::move(Layer));
                });
            }
            const bool HasFlakeLayer = std::any_of(Document.Layers.begin(), Document.Layers.end(),
                [](const MaterialAuthoringLayer &Candidate) { return Candidate.Model == "flakes"; });
            const bool HasCoatLayer = std::any_of(Document.Layers.begin(), Document.Layers.end(),
                [](const MaterialAuthoringLayer &Candidate) { return Candidate.Model == "coat"; });
            if (!HasFlakeLayer || !HasCoatLayer)
            {
                Gap(8 * Scale);
                AnchorX(StartX);
                const float RowY = ImGui::GetCursorScreenPos().y;
                const float HalfW = (!HasFlakeLayer && !HasCoatLayer) ? (ContentW - 8 * Scale) * .5f : ContentW;
                float CurX = StartX;
                if (!HasFlakeLayer)
                {
                    if (ConstructInspectorButton("+ Add Flakes layer##AddFlakeLayer", ImVec2(CurX, RowY), ImVec2(HalfW, 28 * Scale), Scale, 9))
                    {
                        if (Definition.Scattering.Paint.Configuration[3] == 0)
                        {
                            Definition.Scattering.Paint.Configuration = {1, 2, 0, 2};
                            Definition.Scattering.Paint.Distribution = {.48f, 220.f, 1.f, .35f};
                            Definition.Scattering.Paint.Finish = {.12f, .28f, .15f, .65f};
                            Definition.Scattering.Paint.Relief = {.24f, .45f, 0.f, 24.f};
                            Definition.Scattering.Paint.Palette[0] = {.92f, .90f, .86f, 0.f};
                            Definition.Scattering.Paint.Palette[1] = {.68f, .64f, .58f, 1.f};
                            Changed = Library.ConfigurePaint(Definition.Scattering.Paint) || Changed;
                        }
                        EditAuthoring([](MaterialAuthoringDocument &Doc)
                        {
                            if (Doc.Layers.size() >= 16) throw std::runtime_error("Maximum 16 layers.");
                            std::string GenId;
                            for (const auto &Field : Doc.Generators)
                                if (Field.Procedure == "flakes") GenId = Field.Identity;
                            if (GenId.empty())
                            {
                                MaterialAuthoringField Flakes;
                                Flakes.Identity = IssueMaterialAuthoringIdentity();
                                Flakes.Name = "Mineral flake distribution";
                                Flakes.Category = "Particle fields";
                                Flakes.Procedure = "flakes";
                                Flakes.Inputs = {"flakes", "flakeSize", "flakeScale", "flakeLayers", "flakeLayerDepth", "flakeTilt",
                                    "flakeRoughnessMin", "flakeRoughnessMax", "flakeMetalnessMin", "flakeMetalnessMax"};
                                Flakes.Outputs = {"colour", "roughness", "metallic", "normal", "coverage"};
                                GenId = Flakes.Identity;
                                Doc.Generators.push_back(std::move(Flakes));
                            }
                            const size_t Position = !Doc.Layers.empty() && Doc.Layers[0].Model == "coat" ? 1 : 0;
                            MaterialAuthoringLayer Layer;
                            Layer.Identity = IssueMaterialAuthoringIdentity();
                            Layer.Name = "Mineral flakes";
                            Layer.Model = "flakes";
                            Layer.Generator = GenId;
                            Layer.Enabled = true;
                            Layer.Channels = {"colour", "roughness", "metallic", "normal", "coverage"};
                            Doc.Layers.insert(Doc.Layers.begin() + std::ptrdiff_t(Position), std::move(Layer));
                        });
                    }
                    CurX += HalfW + 8 * Scale;
                }
                if (!HasCoatLayer)
                {
                    if (ConstructInspectorButton("+ Add Clearcoat layer##AddCoatLayer", ImVec2(CurX, RowY), ImVec2(HalfW, 28 * Scale), Scale, 9))
                    {
                        if (Definition.Scattering.Illumination.Configuration[0] == 1)
                            Changed = Library.InsertStratum(1) || Changed;
                        EditAuthoring([](MaterialAuthoringDocument &Doc)
                        {
                            if (Doc.Layers.size() >= 16) throw std::runtime_error("Maximum 16 layers.");
                            MaterialAuthoringLayer Coat;
                            Coat.Identity = IssueMaterialAuthoringIdentity();
                            Coat.Name = "Clearcoat";
                            Coat.Model = "coat";
                            Coat.Enabled = true;
                            Coat.Channels = {"coverage", "roughness", "ior"};
                            Doc.Layers.insert(Doc.Layers.begin(), std::move(Coat));
                        });
                    }
                }
                ImGui::SetCursorScreenPos(ImVec2(StartX, RowY + 28 * Scale));
            }
            Gap(30 * Scale);
        }
        InspectorLayerContext = 0;
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        ChildOpen = false;

        // ── Commit actions (native band above the variables dock) ───────────────────────────
        if (ShowActions)
        {
            const float ActionsTop = Bottom - FooterHeight - DockHeight - ActionsHeight;
            Ink->AddRectFilled(ImVec2(Origin.x, ActionsTop), ImVec2(Origin.x + Width, ActionsTop + ActionsHeight), Ink255(0x161616));
            Ink->AddLine(ImVec2(Origin.x, ActionsTop), ImVec2(Origin.x + Width, ActionsTop), Ink255(0xffffff, 10));
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + 17 * Scale, ActionsTop + 10 * Scale));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width - 34 * Scale);
            ConstructActions();
            ImGui::SameLine();
            if (ImGui::Button("Source")) SourceVisible = true;
            ImGui::PopTextWrapPos();
        }

        // ── Shared variables dock ────────────────────────────────────────────────────────────
        {
            const float DockTop = Bottom - FooterHeight - DockHeight;
            Ink->AddRectFilled(ImVec2(Origin.x, DockTop), ImVec2(Origin.x + Width, Bottom - FooterHeight), Ink255(0x24271f));
            Ink->AddLine(ImVec2(Origin.x, DockTop), ImVec2(Origin.x + Width, DockTop), Ink255(0xb5c5a7, 42));
            ConstructMaterialCaption(Origin.x + 14 * Scale, DockTop + 17 * Scale, 13 * Scale, 0xdcdcd2, "Variables");
            const float TitleW = InspectorTextWidth(13, "Variables", Scale);
            const std::string DockCount = std::to_string(Document.Variables.size());
            Ink->AddText(ImGui::GetFont(), 13 * 1.302f * Scale, ImVec2(Origin.x + 14 * Scale + TitleW + 9 * Scale, DockTop + 14 * Scale), Ink255(0x819075), DockCount.c_str());
            {
                ImVec2 Centre;
                if (ConstructInspectorIconButton("##VariablesToggle", ImVec2(Origin.x + Width - 42 * Scale, DockTop + 8 * Scale), ImVec2(28 * Scale, 28 * Scale), Scale, Centre))
                    VariableDockOpen = !VariableDockOpen;
                if (VariableDockOpen)
                    Ink->AddLine(ImVec2(Centre.x - 6 * Scale, Centre.y), ImVec2(Centre.x + 6 * Scale, Centre.y), Ink255(0xdcdcd2), 1.4f * Scale);
                else
                    DrawInspectorPlusGlyph(Centre, 12, 0xdcdcd2, Scale);
            }
            if (VariableDockOpen && DockHeight > 48 * Scale)
            {
                ImGui::SetCursorScreenPos(ImVec2(Origin.x + 14 * Scale, DockTop + 44 * Scale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                if (ImGui::BeginChild("##VariableList", ImVec2(Width - 28 * Scale, DockHeight - 44 * Scale)))
                {
                    ChildOpen = true;
                    if (ConstructDockRows) ConstructDockRows();
                    ImGui::Dummy(ImVec2(1, 12 * Scale));
                }
                ImGui::EndChild();
                ChildOpen = false;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
        }

        // ── Shared tail (salvaged): source sync, idle rebake, idle apply. Runs after the ─────
        // dock so variable edits commit through the same path as every other edit.
        bool Rebaked = false;
        if (PaintChanged)
        {
            Changed = Library.ConfigurePaint(Definition.Scattering.Paint) || Changed;
            PaintChanged = false;
        }
        if (Changed)
        {
            SynchronizeSource();
            PendingLivePreviewApply = true;
        }
        if (ProceduralDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ProceduralDirty = false;
            if (Library.RebakeProcedurals(512))
            {
                PendingLivePreviewApply = false;
                Rebaked = true;
            }
        }
        if (PendingLivePreviewApply && !Rebaked && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (Library.ApplySource()) PendingLivePreviewApply = false;
        }
    }
    catch (const std::exception &Failure)
    {
        if (ChildOpen) { ImGui::EndChild(); ChildOpen = false; }
        InspectorNotice = Failure.what();
    }
    }

    // ── Footer ───────────────────────────────────────────────────────────────────────────────
    {
        const float FooterTop = Bottom - FooterHeight;
        Ink->AddRectFilled(ImVec2(Origin.x, FooterTop), ImVec2(Origin.x + Width, Bottom), Ink255(0x151515));
        Ink->AddLine(ImVec2(Origin.x, FooterTop), ImVec2(Origin.x + Width, FooterTop), Ink255(0xffffff, 6));
        const ImVec2 PillMin(Origin.x + 17 * Scale, FooterTop + 12 * Scale);
        const ImVec2 PillMax(Origin.x + Width - 17 * Scale, PillMin.y + 34 * Scale);
        ImGui::SetCursorScreenPos(PillMin);
        if (ImGui::InvisibleButton("##SaveAsPreset", ImVec2(PillMax.x - PillMin.x, 34 * Scale))) Duplicating = true;
        const bool PillHover = ImGui::IsItemHovered();
        Ink->AddRectFilled(PillMin, PillMax, Ink255(PillHover ? 0x444547 : 0x292f2d), 15 * Scale);
        Ink->AddRect(PillMin, PillMax, Ink255(PillHover ? 0x848588 : 0x46534a), 15 * Scale);
        const char *PresetLabel = "Save as preset";
        const float PresetW = InspectorTextWidth(10, PresetLabel, Scale);
        const float GroupW = 14 * Scale + 7 * Scale + PresetW;
        const float GroupX = PillMin.x + (PillMax.x - PillMin.x - GroupW) * .5f;
        const float PillMidY = PillMin.y + 17 * Scale;
        DrawInspectorPlusGlyph(ImVec2(GroupX + 7 * Scale, PillMidY), 14, 0xc2d2c4, Scale);
        Ink->AddText(ImGui::GetFont(), 10 * 1.302f * Scale, ImVec2(GroupX + 21 * Scale, PillMidY - 10 * 1.302f * Scale * .5f), Ink255(0xc2d2c4), PresetLabel);
        // ⌘ (U+2318) is absent from DM Sans: draw the looped square + "S".
        {
            const ImVec2 CmdCentre(PillMax.x - 26 * Scale, PillMidY);
            const float R = 2.2f * Scale, S = 2.6f * Scale;
            const ImU32 CmdColour = Ink255(0x848688);
            Ink->AddRect(ImVec2(CmdCentre.x - S, CmdCentre.y - S), ImVec2(CmdCentre.x + S, CmdCentre.y + S), CmdColour, 0, 0, 1.2f * Scale);
            for (const float Sx : {-1.f, 1.f})
                for (const float Sy : {-1.f, 1.f})
                    Ink->AddCircle(ImVec2(CmdCentre.x + Sx * S, CmdCentre.y + Sy * S), R, CmdColour, 16, 1.2f * Scale);
            Ink->AddText(ImGui::GetFont(), 8 * 1.302f * Scale, ImVec2(CmdCentre.x + 8 * Scale, PillMidY - 8 * 1.302f * Scale * .5f), CmdColour, "S");
        }
        const char *Tagline = "Made to be made your own.";
        const float TaglineW = InspectorTextWidth(6.5f, Tagline, Scale);
        Ink->AddText(ImGui::GetFont(), 6.5f * 1.302f * Scale, ImVec2(Origin.x + (Width - TaglineW) * .5f, PillMax.y + 9 * Scale), Ink255(0x6c7a7c), Tagline);
    }

    // ── Notice toast ─────────────────────────────────────────────────────────────────────────
    if (!InspectorNotice.empty())
    {
        const float TextW = InspectorTextWidth(9, InspectorNotice.c_str(), Scale);
        const float ToastW = std::min(TextW + 26 * Scale, Width - 24 * Scale);
        const ImVec2 ToastMin(Origin.x + (Width - ToastW) * .5f, Bottom - ReservedBottom - 36 * Scale);
        const ImVec2 ToastMax(ToastMin.x + ToastW, ToastMin.y + 26 * Scale);
        ImGui::SetCursorScreenPos(ToastMin);
        if (ImGui::InvisibleButton("##InspectorNotice", ImVec2(ToastW, 26 * Scale))) InspectorNotice.clear();
        Ink->AddRectFilled(ToastMin, ToastMax, Ink255(0x2a2a25), 9 * Scale);
        Ink->AddRect(ToastMin, ToastMax, Ink255(0xffffff, 20), 9 * Scale);
        Ink->PushClipRect(ToastMin, ToastMax, true);
        Ink->AddText(ImGui::GetFont(), 9 * 1.302f * Scale, ImVec2(ToastMin.x + 13 * Scale, ToastMin.y + 13 * Scale - 9 * 1.302f * Scale * .5f), Ink255(0xe2e2d8), InspectorNotice.c_str());
        Ink->PopClipRect();
    }

    // ── Rename variable dialog ───────────────────────────────────────────────────────────────
    if (!RenamingVariableId.empty())
    {
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Bottom), Ink255(0x000000, 153));
        const float CardW = std::min(360 * Scale, Width - 36 * Scale);
        const float CardH = 152 * Scale;
        const ImVec2 CardMin(Origin.x + (Width - CardW) * .5f, Origin.y + (Bottom - Origin.y - CardH) * .4f);
        const ImVec2 CardMax(CardMin.x + CardW, CardMin.y + CardH);
        Ink->AddRectFilled(CardMin, CardMax, Ink255(0x252525), 22 * Scale);
        Ink->AddRect(CardMin, CardMax, Ink255(0xffffff, 21), 22 * Scale);
        ConstructMaterialCaption(CardMin.x + 25 * Scale, CardMin.y + 28 * Scale, 13 * Scale, 0xdcdcd2, "Rename variable");
        ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
        (void)ConstructInspectorTextInput("##RenameVariable", ImVec2(CardMin.x + 25 * Scale, CardMin.y + 53 * Scale), CardW - 50 * Scale,
            InspectorNameText.data(), InspectorNameText.size(), Scale);
        ImGui::PopFont();
        const float ButtonsY = CardMin.y + 94 * Scale;
        const auto CloseRename = [&]() { RenamingVariableId.clear(); InspectorNameKey.clear(); };
        if (ConstructInspectorButton("Save name##RenameSave", ImVec2(CardMax.x - 25 * Scale - 84 * Scale, ButtonsY), ImVec2(84 * Scale, 30 * Scale), Scale, 9))
        {
            std::string Name = InspectorNameText.data();
            const size_t NameBegin = Name.find_first_not_of(" \t");
            const size_t NameEnd = Name.find_last_not_of(" \t");
            Name = NameBegin == std::string::npos ? std::string{} : Name.substr(NameBegin, NameEnd - NameBegin + 1);
            const bool Duplicate = std::any_of(Document.Variables.begin(), Document.Variables.end(),
                [&](const MaterialAuthoringVariable &Variable) { return Variable.Identity != RenamingVariableId && Variable.Name == Name; });
            if (Name.empty() || Duplicate || Name.size() > 60) InspectorNotice = "Choose a unique variable name.";
            else
            {
                if (LoadFailure.empty() && Entry)
                {
                    const MaterialAuthoringDocument Backup = Document;
                    try
                    {
                        for (auto &Variable : Document.Variables)
                            if (Variable.Identity == RenamingVariableId) Variable.Name = Name;
                        NormalizeMaterialAuthoring(Document);
                        if (Library.ConfigureAuthoring(Document))
                        {
                            AuthoringSelectionKey = EncodeContentPath(Library.QuerySelection()) + "@" +
                                std::to_string(std::hash<std::string>{}(Library.QuerySource()));
                            SynchronizeSource();
                            PendingLivePreviewApply = true;
                        }
                        else Document = Backup;
                    }
                    catch (const std::exception &Error) { Document = Backup; InspectorNotice = Error.what(); }
                }
                CloseRename();
            }
        }
        if (ConstructInspectorButton("Cancel##RenameCancel", ImVec2(CardMax.x - 25 * Scale - 84 * Scale - 8 * Scale - 72 * Scale, ButtonsY), ImVec2(72 * Scale, 30 * Scale), Scale, 9))
            CloseRename();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CloseRename();
    }
}

void MaterialLibraryPanel::ConstructSourceEditor()
{
    if (ImGui::Button("Properties")) SourceVisible = false;
    if (Library.QuerySelection().empty())
    {
        ImGui::TextUnformatted("Select a material");
        ImGui::TextWrapped(".material files contain versioned TOML. Texture references use the selected library directory as ContentDirectory.");
        return;
    }
    ImGui::Text("%s%s", EncodeContentPath(Library.QuerySelection()).c_str(), Library.QueryModified() ? " *" : "");
    const auto *Applied = Library.QueryApplied();
    if (Applied)
    {
        ImGui::Text("Applied: %s / %u layers", Applied->Name.c_str(), Applied->Scattering.Illumination.Configuration[0]);

    }
    ImGui::Separator();
    const float Height = std::max(24.f, ImGui::GetContentRegionAvail().y-150*QueryInterfaceScale());
    if (ImGui::InputTextMultiline("##MaterialSource", SourceText.data(), SourceText.size(), ImVec2(-1, Height)))
        Library.ReplaceSource(SourceText.data());
    ConstructActions();
}

void MaterialLibraryPanel::ConstructCreation()
{
    if ((Creating || Duplicating) && !ImGui::IsPopupOpen("Material destination")) ImGui::OpenPopup("Material destination");
    const MaterialModalScope ModalScope(QueryInterfaceScale());
    if (ImGui::BeginPopupModal("Material destination", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Duplicating ? "Duplicate current authored material" : "Create material");
        ImGui::TextWrapped("Destination is relative to ContentDirectory; existing files are never overwritten.");
        ImGui::InputText("Destination .material", DestinationText.data(), DestinationText.size());
        if (!Duplicating) ImGui::InputText("Material name", NameText.data(), NameText.size());
        if (ImGui::Button(Duplicating ? "Create duplicate" : "Create"))
        {
            const auto Destination = DecodeContentPath(DestinationText.data());
            const bool Complete = Duplicating ? Library.DuplicateMaterial(Destination) : Library.CreateMaterial(Destination, NameText.data());
            if (Complete)
            {
                SynchronizeSource();
                Creating = Duplicating = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { Creating = Duplicating = false; ImGui::CloseCurrentPopup(); }
        if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
        ImGui::EndPopup();
    }
}

void MaterialLibraryPanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    const float Scale = QueryInterfaceScale();
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 79 * Scale));
    ImGui::SetNextWindowViewport(Viewport->ID);
    const auto SavedPalette = ControlKit::Palette();
    auto Palette = SavedPalette;
    Palette.Panel = SpectrumQuad(0x121212);
    Palette.Inset = SpectrumQuad(0x1c1c1a);
    Palette.Stroke = {1, 1, 1, .032f};
    Palette.Text = SpectrumQuad(0xdeded5);
    Palette.TextDim = SpectrumQuad(0x85857b);
    Palette.TextFaint = SpectrumQuad(0x777766);
    Palette.SliderTrack = SpectrumQuad(0x171717);
    Palette.SliderFill = SpectrumQuad(0x454545);
    Palette.SliderThumb = SpectrumQuad(0xd7d7ce);
    ControlKit::ConfigurePalette(Palette);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeQuad(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.11f, .11f, .10f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeQuad(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.133f, .133f, .133f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.18f, .18f, .17f, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.035f, .035f, .035f, 1));
    ImGui::PushStyleColor(ImGuiCol_Header, ThemeQuad(Palette.Selected));
    const auto SavedColourButtonPosition = ImGui::GetStyle().ColorButtonPosition;
    ImGui::GetStyle().ColorButtonPosition = ImGuiDir_Left;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * Scale, 8.49f * Scale));
    // Shared dock chrome (PanelChrome): panel-toned tabs, square windows, hidden scrollbars. This
    // workspace historically leaves tab close buttons enabled and skips collapsed/docking-empty titles.
    const auto Chrome = PanelChrome::PushDockChrome(ImVec4(.035f, .035f, .035f, 1), ThemeQuad(Palette.Panel), false, false, Scale);
    // 📝 STB pixel height spans ascent/descent; CSS sizes the em. DM Sans hhea spans 1302/1000 em.
    ImGui::PushFont(static_cast<ImFont *>(ReferenceTypeface), 10 * 1.302f * Scale);
    ImGui::Begin("TextureWorkspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    auto *Ink = ImGui::GetWindowDrawList();
    const auto Origin = Viewport->WorkPos;
    const float Width = Viewport->WorkSize.x;
    const auto *Entry = QuerySelectedMaterial(Library);
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), Ink255(0x0b0b0b));
    Ink->AddRectFilled(ImVec2(Origin.x, Origin.y + 43 * Scale), ImVec2(Origin.x + Width, Origin.y + 79 * Scale), Ink255(0x111111));
    ConstructMaterialCaption(Origin.x + 16 * Scale, Origin.y + 17 * Scale, 10 * Scale, 0xcecec2, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    const float NameWidth = ImGui::GetFont()->CalcTextSizeA(10 * 1.302f * Scale, 300 * Scale, 0, Entry ? Entry->Name.c_str() : "Material explorer").x;
    ConstructMaterialCaption(Origin.x + 22 * Scale + NameWidth, Origin.y + 20 * Scale, 7 * Scale, 0x767669, ".material");
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    if (ConstructMaterialPill("Material", ImVec2(Origin.x + Width - 596 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale), true)) SourceVisible = false;
    ImGui::BeginDisabled(!ActivatePattern);
    if (ConstructMaterialPill("Pattern", ImVec2(Origin.x + Width - 522 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivatePattern) ActivatePattern();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!ActivateTexture);
    if (ConstructMaterialPill("Texture", ImVec2(Origin.x + Width - 448 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivateTexture) ActivateTexture();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!ActivateStamp);
    if (ConstructMaterialPill("Stamp", ImVec2(Origin.x + Width - 374 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivateStamp) ActivateStamp();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!ActivateBaking);
    if (ConstructMaterialPill("Baking", ImVec2(Origin.x + Width - 300 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivateBaking) ActivateBaking();
    ImGui::EndDisabled();
    if (ConstructMaterialPill("Open Library", ImVec2(Origin.x + Width - 225 * Scale, Origin.y + 8 * Scale), ImVec2(94 * Scale, 27 * Scale))) DirectoryRequested = true;
    if (ConstructMaterialPill("Refresh", ImVec2(Origin.x + Width - 120 * Scale, Origin.y + 8 * Scale), ImVec2(100 * Scale, 27 * Scale)))
    {
        (void)Library.Refresh();
        if (!Library.QueryModified() && !Library.QuerySelection().empty())
        {
            const auto Selection = Library.QuerySelection();
            if (Library.OpenMaterial(Selection)) SynchronizeSource();
        }
        if (InvalidateThumbnails) InvalidateThumbnails();
    }
    ConstructIcon(IconSymbol::EditorMaterial, Origin.x + 17 * Scale, Origin.y + 52 * Scale, 15 * Scale);
    ConstructMaterialCaption(Origin.x + 40 * Scale, Origin.y + 56 * Scale, 9 * Scale, 0x8f8f81, "Automotive essentials     >     Material explorer");
    if (ImGui::BeginPopupContextWindow("Material document operations"))
    {
        if (ImGui::MenuItem("Source")) SourceVisible = true;
        if (ImGui::MenuItem("Properties")) SourceVisible = false;
        if (ImGui::MenuItem("New material")) Creating = true;
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    if (DirectoryRequested) { ImGui::OpenPopup("Open material library"); DirectoryRequested = false; }
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    {
        const MaterialModalScope DirectoryModalScope(Scale);
        if (ImGui::BeginPopupModal("Open material library", nullptr, ImGuiWindowFlags_NoResize))
        {
            const auto Dialogue = ImGui::GetCursorScreenPos();
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 4 * Scale, 7 * Scale, 0x8b8d79, "MATERIAL LIBRARY");
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 26 * Scale, 17 * Scale, 0xdcdccd, "Open material library",
                static_cast<ImFont *>(HeadingTypeface));
            ImGui::Dummy(ImVec2(1, 58 * Scale));
            ImGui::TextWrapped("Switch to a different .material catalogue folder.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##Directory", "Material library folder", DirectoryText.data(), DirectoryText.size());
            if (Library.QueryModified() || Library.QueryParkedEdits())
                ImGui::TextWrapped("Save or revert current edits before switching libraries.");
            if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 2 * ButtonWidth - ButtonGap - 20 * Scale);
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 32 * Scale))) ImGui::CloseCurrentPopup();
            ImGui::SameLine(0, ButtonGap);
            ImGui::BeginDisabled(Library.QueryModified() || Library.QueryParkedEdits());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.29f,.42f,.25f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.35f,.49f,.30f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.24f,.35f,.20f,1));
            if (ImGui::Button("Open", ImVec2(ButtonWidth, 32 * Scale)) && ConfigureDirectory(DecodeContentPath(DirectoryText.data()))) ImGui::CloseCurrentPopup();
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
    ConstructCreation();
    if (ClosingRequested && !ImGui::IsPopupOpen("Unsaved material")) ImGui::OpenPopup("Unsaved material");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    {
        const MaterialModalScope UnsavedModalScope(Scale);
        if (ImGui::BeginPopupModal("Unsaved material", nullptr, ImGuiWindowFlags_NoResize))
        {
            const auto Dialogue = ImGui::GetCursorScreenPos();
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 4 * Scale, 7 * Scale, 0x8b8d79, "UNSAVED MATERIAL");
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 26 * Scale, 17 * Scale, 0xdcdccd, "Save your changes?",
                static_cast<ImFont *>(HeadingTypeface));
            ImGui::Dummy(ImVec2(1, 58 * Scale));
            const auto *Unsaved = QuerySelectedMaterial(Library);
            ImGui::TextWrapped("\u201c%s\u201d has unsaved edits. Save keeps them; Discard loses them.",
                Unsaved ? Unsaved->Name.c_str() : "This material");
            if (Library.QueryParkedEdits()) ImGui::TextWrapped("%u other material(s) hold parked edits; only the current material is saved here.",
                (unsigned)Library.QueryParkedCount());
            if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 3 * ButtonWidth - 2 * ButtonGap - 16 * Scale);
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 32 * Scale))) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0, ButtonGap);
            if (ImGui::Button("Discard", ImVec2(ButtonWidth, 32 * Scale))) CloseAuthorized = true;
            ImGui::SameLine(0, ButtonGap);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.29f,.42f,.25f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.35f,.49f,.30f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.24f,.35f,.20f,1));
            if (ImGui::Button("Save and close", ImVec2(ButtonWidth, 32 * Scale)) && Library.SaveMaterial())
            {
                if (InvalidateThumbnail) InvalidateThumbnail(Library.QuerySelection());
                CloseAuthorized = true;
            }
            ImGui::PopStyleColor(3);
            if (CloseAuthorized) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
    ImGui::End();
    if (!Panels.QuerySeated())
    {
        const float Width = std::max(1.f, Viewport->WorkSize.x), Scale = QueryInterfaceScale();
        const float Left = std::min(.30f, 278 * Scale / Width), Right = std::min(.34f, 338 * Scale / Width);
        (void)Panels.ConfigureColumns({{"MaterialLibrary", Left}, {"MaterialViewport", 1 - Left - Right}, {"MaterialInspector", Right}});
    }
    Panels.ConstructInterface(QueryInterfaceScale());
    ConstructFullscreenPreview();
    ImGui::PopFont();
    PanelChrome::PopDockChrome(Chrome);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(8);
    ControlKit::ConfigurePalette(SavedPalette);
    ImGui::GetStyle().ColorButtonPosition = SavedColourButtonPosition;
}
} // namespace Frontier
