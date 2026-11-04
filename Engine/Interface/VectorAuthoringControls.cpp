/*============================================================================================================================================
                                                            VECTORAUTHORINGCONTROLS.CPP
============================================================================================================================================*/
// 🧩 Reuse the themed ControlKit switch/slider while matching the approved rounded vector field and disclosure surfaces.

#include "VectorAuthoringControls.h"
#include "GlyphSpace.h"
#include "DropdownControls.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Frontier
{
namespace
{
ImVec4 ConvertSpectrum(ColorQuad Spectrum)
{
    return {Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha};
}

//------------------------------------------------------------------------------------------------------------------------
//                                             COLOUR PALETTE AND GENERATOR SOURCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Hex values mirror Experimental/TextureStudio/src/MaterialControls.jsx's `palettes` table so a
// swatch picked here or in the browser authoring tool reads as the same colour either side.
struct NamedColourPalette { const char *Name; std::array<const char *, 5> Hex; };
constexpr std::array<NamedColourPalette, 4> AuthoringColourPalettes{{
    {"Spectrum",        {"#c6db89", "#59bea3", "#ab8bc9", "#d17b83", "#82a3d5"}},
    {"Vivid spectrum",  {"#ec5667", "#50c38c", "#ae67e5", "#5999ee", "#edc25c"}},
    {"Warm metallic",   {"#f0cb8a", "#d99a5d", "#b97046", "#e6bf83", "#e7d6a9"}},
    {"Cool metallic",   {"#93bbdc", "#a49cdc", "#6ed0c5", "#c0cedd", "#789cab"}},
}};

void ParseHexColour(const char *Hex, float &Red, float &Green, float &Blue) noexcept
{
    const auto Component = [&](int Index) { char Byte[3]{Hex[Index], Hex[Index + 1], 0}; return float(strtol(Byte, nullptr, 16)) / 255.f; };
    Red = Component(1); Green = Component(3); Blue = Component(5);
}

// 📝 Named colour generators mirror Experimental/TextureStudio/src/colourGenerators.js: each derives
// a single authored colour from the current one (plus a seed for the randomised entries) rather than
// a spatial mask — procedural mask generators remain the separate CHANNEL_GENERATORS toolkit.
enum class AuthoringColourGenerator : uint32_t { Complementary, HarmonicHue, Analogous, ProceduralNoise, Count };
constexpr std::array<const char *, size_t(AuthoringColourGenerator::Count)> AuthoringColourGeneratorNames{
    "Complementary", "Harmonic hue", "Analogous", "Procedural noise"};

// 📝 Mulberry32: a tiny deterministic 32-bit generator so a repeated seed reproduces the same suggestion.
float NextColourGeneratorSample(uint32_t &State) noexcept
{
    State += 0x6D2B79F5u;
    uint32_t Mixed = State;
    Mixed = (Mixed ^ (Mixed >> 15)) * (Mixed | 1u);
    Mixed ^= Mixed + (Mixed ^ (Mixed >> 7)) * (Mixed | 61u);
    return float((Mixed ^ (Mixed >> 14)) >> 0) / 4294967296.f;
}

ImVec4 GenerateColourSuggestion(AuthoringColourGenerator Generator, const std::array<float, 4> &Spectrum, uint32_t Seed) noexcept
{
    float Hue, Saturation, Value;
    ImGui::ColorConvertRGBtoHSV(Spectrum[0], Spectrum[1], Spectrum[2], Hue, Saturation, Value);
    uint32_t State = Seed;
    switch (Generator)
    {
        case AuthoringColourGenerator::Complementary: Hue = std::fmod(Hue + 0.5f, 1.f); break;
        case AuthoringColourGenerator::HarmonicHue:   Hue = std::fmod(Hue + 0.38196601125f, 1.f); break;                        // 📝 Golden-angle hue rotation
        case AuthoringColourGenerator::Analogous:     Hue = std::fmod(Hue + (20.f + NextColourGeneratorSample(State) * 25.f) / 360.f, 1.f); break;
        case AuthoringColourGenerator::ProceduralNoise: default:
            Hue = NextColourGeneratorSample(State);
            Saturation = 0.35f + NextColourGeneratorSample(State) * 0.5f;
            Value = 0.45f + NextColourGeneratorSample(State) * 0.45f;
            break;
    }
    float Red, Green, Blue;
    ImGui::ColorConvertHSVtoRGB(Hue, Saturation, Value, Red, Green, Blue);
    return {Red, Green, Blue, Spectrum[3]};
}
}

bool VectorAuthoringControlSurface::RenderColourSourcePicker(std::array<float, 4> &Spectrum)
{
    bool Changed = false;
    ImGui::TextDisabled("Palette");
    for (const auto &Entry : AuthoringColourPalettes)
    {
        ImGui::TextDisabled("%s", Entry.Name);
        for (size_t Index = 0; Index < Entry.Hex.size(); ++Index)
        {
            if (Index) ImGui::SameLine();
            ImGui::PushID(int(Index));
            float Red, Green, Blue;
            ParseHexColour(Entry.Hex[Index], Red, Green, Blue);
            if (ImGui::ColorButton(Entry.Hex[Index], ImVec4(Red, Green, Blue, 1), ImGuiColorEditFlags_NoAlpha, ImVec2(18 * Scale, 18 * Scale)))
            {
                Spectrum[0] = Red; Spectrum[1] = Green; Spectrum[2] = Blue;
                Changed = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Generate");
    for (size_t Index = 0; Index < AuthoringColourGeneratorNames.size(); ++Index)
    {
        ImGui::PushID(int(Index) + 100);
        const auto Suggestion = GenerateColourSuggestion(AuthoringColourGenerator(Index), Spectrum, uint32_t(Index) * 97u + 1u);
        ImGui::ColorButton("##Preview", Suggestion, ImGuiColorEditFlags_NoAlpha, ImVec2(16 * Scale, 16 * Scale));
        ImGui::SameLine();
        if (ImGui::Selectable(AuthoringColourGeneratorNames[Index]))
        {
            const auto Applied = GenerateColourSuggestion(AuthoringColourGenerator(Index), Spectrum,
                uint32_t(ImGui::GetTime() * 1000.0) ^ (uint32_t(Index) * 97u + 1u));
            Spectrum[0] = Applied.x; Spectrum[1] = Applied.y; Spectrum[2] = Applied.z;
            Changed = true;
        }
        ImGui::PopID();
    }
    return Changed;
}

void VectorAuthoringControlSurface::Register(const char *Caption)
{
    if (!RegisterExtent) return;
    const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    RegisterExtent(Caption, {Minimum.x, Minimum.y, Maximum.x, Maximum.y});
}

bool VectorAuthoringControlSurface::Button(
    const char       *Caption,
    float             Width,
    bool              Selected,
    std::string_view  Path,
    bool              Capsule)
{
    const auto &Palette = ControlKit::Palette();
    const float Height = ControlHeight * Scale;
    if (Width == 0) Width = std::max(Height, ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption)).x + 24 * Scale);
    const float Rounding = Capsule || !Path.empty() ? Height * .5f : std::min(6 * Scale, Height * .25f);
    const auto Position = ImGui::GetCursorScreenPos();
    const bool Pressed = ImGui::InvisibleButton(Caption, ImVec2(Width, Height));
    Register(Caption);
    const auto Colour = Selected ? Palette.Selected : ImGui::IsItemHovered() ? Palette.Raised : Palette.Inset;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Colour)), Rounding);
    Ink->AddRect(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Selected ? Palette.TextFaint : Palette.Stroke)), Rounding);
    if (!Path.empty())
    {
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(Surface, Path, {Position.x + (Width - 17 * Scale) * .5f, Position.y + 6 * Scale, 17 * Scale, 1.5f, Palette.TextDim});
    }
    else
    {
        const auto Extent = ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption));
        Ink->AddText(ImVec2(Position.x + (Width - Extent.x) * .5f, Position.y + (Height - Extent.y) * .5f), ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Caption, ImGui::FindRenderedTextEnd(Caption));
    }
    return Pressed;
}

void VectorAuthoringControlSurface::Tooltip(const char *Caption, const char *Description)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsItemFocused()) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 10 * Scale));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ConvertSpectrum(ControlKit::Palette().Raised));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240 * Scale);
    ImGui::TextUnformatted(Caption);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Description);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

bool VectorAuthoringControlSurface::Toggle(const char *Caption, bool &Enabled)
{
    const auto Position = ImGui::GetCursorScreenPos();
    const float Width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(Caption, nullptr, true).x + 47 * Scale);
    const bool Changed = ImGui::InvisibleButton(Caption, ImVec2(Width, 28 * Scale));
    Register(Caption);
    if (Changed) Enabled = !Enabled;
    PixelSpace Surface;
    const float SwitchScale = .66f * Scale;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, SwitchScale);
    (void)ControlKit::Switch(Surface, Position.x / SwitchScale, (Position.y + 5 * Scale) / SwitchScale, Enabled, ControlPointer{}, ImGui::GetStyle().Alpha);
    ImGui::GetWindowDrawList()->AddText(ImVec2(Position.x + 47 * Scale, Position.y + 6 * Scale), ImGui::GetColorU32(ImGuiCol_Text), Caption, ImGui::FindRenderedTextEnd(Caption));
    return Changed;
}

bool VectorAuthoringControlSurface::Number(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum)
{
    ImGui::PushID(Caption);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    const bool Changed = ImGui::InputFloat("##Quantity", &Quantity, 0, 0, "%.3g");
    Register(Caption);
    if (Changed) Quantity = std::isfinite(Quantity) ? std::clamp(Quantity, Minimum, Maximum) : Minimum;
    ImGui::EndGroup();
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::RangeCore(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum,
    bool        StripOwnsCaption)
{
    bool Changed = Number(Caption, Quantity, Minimum, Maximum);
    ImGui::PushID(Caption);
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(48.f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("Range", ImVec2(Width, 28 * Scale));
    // 📝 Range re-registers the same caption over Number()'s own (smaller, text-box-only) extent so
    // headless/automated drivers - and anything else keying off QueryControlExtent - can address the
    // actual draggable slider strip, not just the typed-number field above it. Slider instead leaves
    // Caption on the number box (typed edits keep addressing it) and registers the strip alongside.
    if (StripOwnsCaption) Register(Caption);
    else Register((std::string(Caption) + " slider").c_str());
    const auto &Input = ImGui::GetIO();
    const bool Active = ImGui::IsItemActive();
    const ControlPointer Pointer{Input.MousePos.x, Input.MousePos.y, Input.MouseDown[0], ImGui::IsItemActivated(), ImGui::IsMouseReleased(0), Active || ImGui::IsItemHovered(), Input.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, Input.DisplaySize.x, Input.DisplaySize.y, 1);
    float Replacement = Quantity;
    (void)ControlKit::Slider(Surface, {Origin.x, Origin.y, Origin.x + Width, Origin.y + 28 * Scale}, Minimum, Maximum, Quantity, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 28 * Scale, 26 * Scale);
    if (Active && Replacement != Quantity) { Quantity = Replacement; Changed = true; }
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Range(const char *Caption, float &Quantity, float Minimum, float Maximum)
{
    return RangeCore(Caption, Quantity, Minimum, Maximum, true);
}

bool VectorAuthoringControlSurface::Slider(const char *Caption, float &Quantity, float Minimum, float Maximum)
{
    return RangeCore(Caption, Quantity, Minimum, Maximum, false);
}

bool VectorAuthoringControlSurface::Dropdown(const char *Caption, int &Selection, std::span<const char *const> Captions)
{
    ImGui::PushID(Caption);
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    bool Changed = false;
    if (Captions.empty()) { ImGui::PopID(); return false; }
    if (DropdownControlSurface::BeginSelection("##Selection", Captions[size_t(std::clamp(Selection, 0, int(Captions.size()) - 1))], Scale))
    {
        for (size_t Index = 0; Index < Captions.size(); ++Index)
        {
            if (DropdownControlSurface::Option(Captions[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
            }
        }
        DropdownControlSurface::EndSelection();
    }
    Register(Caption);
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Colour(const char *Caption, std::array<float, 4> &Spectrum)
{
    ImGui::PushID(Caption);
    ImGui::TextDisabled("%s", Caption);

    const auto &Palette = ControlKit::Palette();

    // 📝 Collapsible "colorbar" (swatch chip + hex label + caret) that expands INLINE into an SV
    // box + a SEPARATE hue/alpha bar + a hex field — matching the HTML reference's `.colorbar` /
    // `.picker` pair (LayerInspector.js's BuildColourField) instead of ImGui's native hue-wheel
    // popup. Open/closed state is read from ImGui's own window state storage keyed on this
    // Caption's ID-stack path, so (unlike the HTML's DOM, which BuildColourField's OpenPickers/
    // BeginScrub machinery exists to protect from a full Refresh() rebuild) it just survives every
    // frame the normal ImGui way — there is nothing here that tears the widget down mid-interaction.
    const ImGuiID OpenId = ImGui::GetID("ColourOpen");
    ImGuiStorage *Storage = ImGui::GetStateStorage();
    bool Open = Storage->GetBool(OpenId, false);

    const float Height = 26 * Scale;
    const float Width  = std::max(ImGui::GetContentRegionAvail().x - 30 * Scale, Height * 3);
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ColourBar", ImVec2(Width, Height));
    const bool Hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) { Open = !Open; Storage->SetBool(OpenId, Open); }
    Register(Caption);

    auto      *Ink      = ImGui::GetWindowDrawList();
    const float Rounding = Height * .5f;
    const ImVec2 BarMax(Position.x + Width, Position.y + Height);
    Ink->AddRectFilled(Position, BarMax, ImGui::GetColorU32(ConvertSpectrum(Hovered ? Palette.Raised : Palette.Inset)), Rounding);
    if (Open) Ink->AddRect(Position, BarMax, ImGui::GetColorU32(ConvertSpectrum(Palette.StrokeStrong)), Rounding, 0, 1 * Scale);

    const float  ChipRadius = Height * .29f;
    const ImVec2 ChipCentre(Position.x + 6 * Scale + ChipRadius, Position.y + Height * .5f);
    const ImVec4 Swatch(std::clamp(Spectrum[0], 0.f, 1.f), std::clamp(Spectrum[1], 0.f, 1.f),
                        std::clamp(Spectrum[2], 0.f, 1.f), std::clamp(Spectrum[3], 0.f, 1.f));
    Ink->AddCircleFilled(ChipCentre, ChipRadius, ImGui::GetColorU32(Swatch), 20);
    Ink->AddCircle(ChipCentre, ChipRadius, ImGui::GetColorU32(ImVec4(0, 0, 0, .35f)), 20, 1 * Scale);

    char Hex[16];
    std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X", int(std::round(Swatch.x * 255.f)),
        int(std::round(Swatch.y * 255.f)), int(std::round(Swatch.z * 255.f)));
    Ink->AddText(ImVec2(ChipCentre.x + ChipRadius + 7 * Scale, Position.y + (Height - ImGui::GetFontSize()) * .5f),
        ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Hex);

    // 📝 A small drawn triangle rather than a glyph, so the caret needs no icon-font/atlas lookup:
    // it points down while closed and flips to point up while the picker is expanded.
    const ImVec2 CaretCentre(Position.x + Width - Height * .5f, Position.y + Height * .5f);
    const float  CaretHalf   = 3.5f * Scale;
    const ImU32  CaretColour = ImGui::GetColorU32(ConvertSpectrum(Palette.TextDim));
    if (Open)
        Ink->AddTriangleFilled(ImVec2(CaretCentre.x - CaretHalf, CaretCentre.y + CaretHalf * .5f),
            ImVec2(CaretCentre.x + CaretHalf, CaretCentre.y + CaretHalf * .5f),
            ImVec2(CaretCentre.x, CaretCentre.y - CaretHalf * .7f), CaretColour);
    else
        Ink->AddTriangleFilled(ImVec2(CaretCentre.x - CaretHalf, CaretCentre.y - CaretHalf * .5f),
            ImVec2(CaretCentre.x + CaretHalf, CaretCentre.y - CaretHalf * .5f),
            ImVec2(CaretCentre.x, CaretCentre.y + CaretHalf * .7f), CaretColour);

    bool Changed = false;
    if (Open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * Scale, 8 * Scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ConvertSpectrum(Palette.Panel));
        ImGui::PushStyleColor(ImGuiCol_Border, ConvertSpectrum(Palette.StrokeStrong));
        ImGui::BeginChild("ColourPicker", ImVec2(0, 0),
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetNextItemWidth(-1);
        // 📝 Square SV plane + a SEPARATE horizontal hue bar (PickerHueBar, not the native hue
        // wheel) + a single hex field (DisplayHex, no RGB/HSV cycle button) — the same three
        // pieces as the HTML's `.svbox` + `.barstack > .huebar` + `.hexrow`. NoSidePreview/
        // NoSmallPreview/NoOptions drop ImGui's own preview swatches and format menu since the
        // closed bar's chip already is the preview.
        if (ImGui::ColorPicker4("##Picker", Spectrum.data(),
                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoLabel |
                ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar |
                ImGuiColorEditFlags_DisplayHex))
            Changed = true;
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    // 📝 Additive palette/generator source — not part of the HTML reference, but an existing
    // Frontier affordance nothing asked to remove, so it stays as a secondary "..." button beside
    // the bar rather than folding into (or replacing) the redesigned picker above.
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("ColourSourcePopup");
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 12 * Scale));
    if (ImGui::BeginPopup("ColourSourcePopup"))
    {
        if (RenderColourSourcePicker(Spectrum)) Changed = true;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);

    ImGui::PopID();
    return Changed;
}

void VectorAuthoringControlSurface::BeginCard(const char *Caption, bool Raised, bool Subordinate)
{
    // 📝 Material-inspector card language (ControlKit::SectionCard): palette fill, hairline Stroke
    // outline, 15px radius, 17px content inset and a micro-overline caption - so Pattern/Stamp (and
    // Baking/Texture Paint) inspector cards read as the same objects as the Material workspace's
    // instead of plain bordered ImGui children with a loose text title.
    // Disambiguate empty captions per frame so multiple custom-headed cards never collide into one child window.
    const int CurrentFrame = ImGui::GetFrameCount();
    if (CurrentFrame != LastCardFrame)
    {
        LastCardFrame = CurrentFrame;
        CardOrdinal = 0;
    }
    const bool HasCaption = Caption && Caption[0] != '\0';
    if (HasCaption) ImGui::PushID(Caption);
    else ImGui::PushID(0x5F435244 + CardOrdinal++);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 15 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(17 * Scale, 14 * Scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ConvertSpectrum(Raised ? ControlKit::Palette().Raised : Subordinate ? ControlKit::Palette().Panel : ControlKit::Palette().Inset));
    ImGui::PushStyleColor(ImGuiCol_Border, ConvertSpectrum(ControlKit::Palette().Stroke));
    ImGui::BeginChild("Card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (HasCaption)
    {
        std::string Overline(Caption);
        for (char &Character : Overline) Character = char(std::toupper(static_cast<unsigned char>(Character)));
        ImGui::PushFont(nullptr, 8.f * 1.302f * Scale);
        ImGui::PushStyleColor(ImGuiCol_Text, ConvertSpectrum(ControlKit::Palette().TextDim));
        ImGui::TextUnformatted(Overline.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Spacing();
    }
}

void VectorAuthoringControlSurface::EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 10 * Scale));
}

void VectorAuthoringControlSurface::ArrayFields(VectorAuthoringArraySpecification &Specification, const std::string &Suffix)
{
    int Category = int(Specification.Category), Profile = int(Specification.ScaleProfile);
    const char *Layouts[]{"Linear", "Grid", "Circular / arc", "Radial rings"};
    if (Dropdown(("Array layout" + Suffix).c_str(), Category, Layouts))
    {
        Specification.Category = VectorAuthoringArrayCategory(Category);
        if (Category == 1 && Specification.StepY == 0) Specification.StepY = 80;
    }
    // 📝 Modifier parameters are edited live (Blender-style), so every field here is a Slider - a
    // draggable strip under the number box that updates the value (and thus the evaluated canvas)
    // every frame of the drag - instead of a type-a-number box. Seeds keep plain Number (a 0..999999
    // strip is useless); counts go through the same Slider and round to the nearest whole step.
    const auto Integral = [&](const std::string &Caption, uint32_t &Quantity, float Minimum, float Maximum)
    {
        float Replacement = float(Quantity);
        if (Slider(Caption.c_str(), Replacement, Minimum, Maximum)) Quantity = uint32_t(std::lround(Replacement));
    };
    if (Category == 1)
    {
        Integral("Columns" + Suffix, Specification.Columns, 1, 8);
        Integral("Rows" + Suffix, Specification.Rows, 1, 8);
    }
    else Integral((Category >= 2 ? "Copies per ring" : "Copies") + Suffix, Specification.Count, 2, 64);
    if (Category >= 2)
    {
        Slider(("Radius" + Suffix).c_str(), Specification.Radius, 0, 512);
        if (Category == 3)
        {
            Integral("Rings" + Suffix, Specification.Rings, 1, 8);
            Slider(("Ring spacing" + Suffix).c_str(), Specification.RadiusStep, 1, 512);
        }
        Slider(("Centre X" + Suffix).c_str(), Specification.CentreX, -512, 1024);
        Slider(("Centre Y" + Suffix).c_str(), Specification.CentreY, -512, 1024);
        Slider(("Start angle" + Suffix).c_str(), Specification.StartAngle, -360, 360);
        Slider(("Arc sweep" + Suffix).c_str(), Specification.Sweep, -360, 360);
        Toggle(("Rotate copies around centre" + Suffix).c_str(), Specification.RotateCopies);
    }
    else
    {
        Slider(("Step X" + Suffix).c_str(), Specification.StepX, -512, 512);
        Slider(("Step Y" + Suffix).c_str(), Specification.StepY, -512, 512);
    }
    const char *Profiles[]{"Uniform", "Linear progression", "Circular wave", "Radial distance", "Seeded noise"};
    if (Dropdown(("Scale profile" + Suffix).c_str(), Profile, Profiles)) Specification.ScaleProfile = VectorAuthoringScaleProfile(Profile);
    Slider(("Start scale %" + Suffix).c_str(), Specification.StartScale, 1, 400);
    if (Profile != 0) Slider(("End scale %" + Suffix).c_str(), Specification.EndScale, 1, 400);
    if (Profile == 4)
    {
        float Seed = float(Specification.Seed);
        if (Number(("Noise seed" + Suffix).c_str(), Seed, 0, 999999)) Specification.Seed = uint32_t(Seed);
    }
}
void VectorAuthoringControlSurface::MirrorFields(VectorAuthoringMirrorSpecification &Specification, const std::string &Suffix)
{
    Slider(("Pivot X" + Suffix).c_str(), Specification.PivotX, -512, 1024);
    Slider(("Pivot Y" + Suffix).c_str(), Specification.PivotY, -512, 1024);
    Slider(("Axis angle" + Suffix).c_str(), Specification.AxisAngle, -360, 360);
    Tooltip(("Axis angle" + Suffix).c_str(), "0deg mirrors top/bottom across a horizontal axis; 90deg mirrors left/right across a vertical axis through the pivot.");
    Toggle(("Keep original" + Suffix).c_str(), Specification.KeepOriginal);
    Tooltip(("Keep original" + Suffix).c_str(), "On: the mirrored copy sits alongside the untouched source, like Blender's Mirror modifier. Off: the source itself is replaced by its mirrored placement.");
}
void VectorAuthoringControlSurface::NoiseFields(VectorAuthoringNoiseSpecification &Specification, const std::string &Suffix)
{
    Slider(("Position jitter" + Suffix).c_str(), Specification.PositionJitter, 0, 256);
    Slider(("Rotation jitter" + Suffix).c_str(), Specification.RotationJitter, 0, 180);
    Slider(("Scale jitter %" + Suffix).c_str(), Specification.ScaleJitter, 0, 90);
    const auto Integral = [&](const std::string &Caption, uint32_t &Quantity, float Minimum, float Maximum)
    {
        float Replacement = float(Quantity);
        if (Number(Caption.c_str(), Replacement, Minimum, Maximum)) Quantity = uint32_t(Replacement);
    };
    Integral("Jitter seed" + Suffix, Specification.Seed, 0, 999999);
    Tooltip(("Jitter seed" + Suffix).c_str(), "Deterministic - the same seed always nudges the selection by the same amount, so re-opening the document never reshuffles it.");
}
void VectorAuthoringControlSurface::OffsetFields(VectorAuthoringOffsetSpecification &Specification, const std::string &Suffix)
{
    Slider(("Offset distance" + Suffix).c_str(), Specification.Distance, -256, 256);
    Tooltip(("Offset distance" + Suffix).c_str(), "Grows the shape's own bounding frame when positive, shrinks it when negative. Exact for rectangles and ellipses; an approximation (bounding-frame grow/shrink) for diamonds, triangles, polygons, stars and freeform paths.");
}
void VectorAuthoringControlSurface::ColourFields(VectorAuthoringColourSpecification &Specification, const std::string &Suffix)
{
    int Generator = int(Specification.Generator);
    const char *Generators[]{"Linear gradient", "Radial gradient", "Colour ramp (grouped)"};
    if (Dropdown(("Colour generator" + Suffix).c_str(), Generator, Generators)) Specification.Generator = VectorAuthoringColourGenerator(Generator);
    Tooltip(("Colour generator" + Suffix).c_str(), "Linear/Radial blend smoothly across the selection's own positions. Ramp instead cycles discretely through the stops by group - pick it to give each array row/ring its own flat colour.");
    int Target = int(Specification.Target);
    const char *Targets[]{"Fill", "Stroke / outline", "Fill + stroke"};
    if (Dropdown(("Paints" + Suffix).c_str(), Target, Targets)) Specification.Target = VectorAuthoringColourTarget(Target);
    Tooltip(("Paints" + Suffix).c_str(), "Stroke only shows if the shape already has a stroke width above zero. Stack two Colour entries to drive fill and stroke independently.");
    if (Generator == 0)
    {
        Slider(("Gradient angle" + Suffix).c_str(), Specification.AxisAngle, -360, 360);
        Tooltip(("Gradient angle" + Suffix).c_str(), "0deg blends left-to-right, 90deg bottom-to-top, auto-fit across the selection's own combined bounding box - no manual pivot needed.");
    }
    else if (Generator == 1)
    {
        Slider(("Centre X" + Suffix).c_str(), Specification.CentreX, -512, 1024);
        Slider(("Centre Y" + Suffix).c_str(), Specification.CentreY, -512, 1024);
        Slider(("Radius" + Suffix).c_str(), Specification.Radius, 1, 2048);
        Tooltip(("Radius" + Suffix).c_str(), "Distance from the centre at which the gradient reaches its last stop.");
    }
    const auto Integral = [&](const std::string &Caption, uint32_t &Quantity, float Minimum, float Maximum)
    {
        float Replacement = float(Quantity);
        if (Slider(Caption.c_str(), Replacement, Minimum, Maximum)) Quantity = uint32_t(std::lround(Replacement));
    };
    Integral("Stop count" + Suffix, Specification.StopCount, 2, 4);
    for (uint32_t Index = 0; Index < Specification.StopCount; ++Index)
        Colour(("Stop " + std::to_string(Index + 1) + Suffix).c_str(), Specification.Stops[Index]);
    if (Generator == 2)
    {
        Toggle(("Auto group from array" + Suffix).c_str(), Specification.AutoGroup);
        Tooltip(("Auto group from array" + Suffix).c_str(), "On: bands follow the preceding Array modifier's own rows/rings (a Grid gets one colour per row, a Radial array one per ring). Off: pick the group size yourself below. With no preceding Array, every element gets its own colour either way.");
        if (!Specification.AutoGroup) Integral("Group size" + Suffix, Specification.GroupSize, 1, 64);
    }
}
bool VectorAuthoringControlSurface::Array(
    VectorAuthoringArraySpecification &Specification,
    size_t                     LayerCount,
    size_t                     SelectionCount,
    size_t                     Limit,
    bool                       Protected)
{
    ImGui::BeginDisabled(Protected);
    ArrayFields(Specification);
    const int Category = int(Specification.Category);
    ImGui::TextWrapped("Bakes the selection into real, independent copies right now (the group-array path for multi-selections) - undo restores the originals. With a single layer selected, prefer a live Array modifier instead: fully procedural, slider-editable, and it never adds layers until you Apply it.");
    const auto Population = Category == 1 ? Specification.Columns * Specification.Rows : Specification.Count * (Category == 3 ? Specification.Rings : 1);
    const auto Total = LayerCount - SelectionCount + Population * SelectionCount;
    ImGui::TextDisabled("%zu / %zu document entries after generation", Total, Limit);
    ImGui::BeginDisabled(Total > Limit || Population < 2);
    const bool Generate = Button("Generate array", ImGui::GetContentRegionAvail().x);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return Generate;
}
}
