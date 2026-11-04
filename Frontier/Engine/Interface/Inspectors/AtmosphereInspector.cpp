/*==========================================================================================================================================
                                                          ATMOSPHEREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The demo's Atmosphere page (AtmosphereInspectorDashboard.js, AtmosphereColumnInstrument.js, AtmosphereMediumLight.js and
//    AtmosphereInspector.css) on the level's AtmosphereConfiguration: the header with ● Live, four section tabs, the density
//    scan, coefficients, the sky-view capture, the medium recipe, the scale-height comparator, constituents, the phase plot,
//    the transmission dial, sky & surface, horizon shaping, integration and the baking cards.

#include "Engine/Interface/Inspectors/AtmosphereInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Inspectors/AtmosphereBakeImages.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

constexpr float AtmospherePi = 3.14159265358979f;

// 📝 AtmosphereInspectorDashboard.js Ranges, in the demo's own keys.
constexpr float RayleighMaximum = 3.0f, MieMaximum = 6.0f, OzoneMaximum = 4.0f;
constexpr float RayleighHeightMinimum = 1000.0f, RayleighHeightMaximum = 16000.0f;
constexpr float MieHeightMinimum = 500.0f, MieHeightMaximum = 8000.0f;
constexpr float MieGLimit = 0.95f, ShapingMaximum = 3.0f;
constexpr float TurbidityPerMie = 1.4f; // the level's turbidity: Mie = (turbidity − 1) / 1.4 (2.4 ≙ the demo's Mie 1)

// 📝 Pending: page keys the PlanetaryAtmosphere overlay (the sky the viewport shows) does not read yet.
constexpr const char *SkyPending   = "Pending: the PlanetaryAtmosphere overlay draws the visible sky and does not read this key yet "
                                     "(the port's own sky integrator does).";
constexpr const char *LutPending   = "Pending: the PlanetaryAtmosphere overlay bakes its lookup tables with fixed sample counts.";
constexpr const char *MultiPending = "Pending: the PlanetaryAtmosphere overlay always integrates multiple scattering.";

[[nodiscard]] float ResolveMie(const AtmosphereConfiguration &P) noexcept
{
    return std::clamp((P.MieTurbidity - 1.0f) / TurbidityPerMie, 0.0f, MieMaximum);
}

// 📝 AtmosphereDensityProfile: relative Rayleigh and Mie density at an altitude [m].
struct DensityProfile
{
    float Rayleigh = 1.0f;
    float Mie      = 1.0f;
};
[[nodiscard]] DensityProfile ResolveDensity(const AtmosphereConfiguration &P, float Altitude) noexcept
{
    const float Height = std::max(0.0f, Altitude);
    return {std::exp(-Height / std::max(1.0f, P.RayleighHeightMetres)), std::exp(-Height / std::max(1.0f, P.MieHeightMetres))};
}

// 📝 AtmosphereIntegrator.js ResolveSunTransmittance: 64 midpoint samples from 100 m to the top of the atmosphere along the
//    sun, the same constants as the demo's sky (6360 / 6460 km, Rayleigh, Mie 4.40e-6, the ozone tent at 25 km ± 15 km).
[[nodiscard]] std::array<float, 3> ResolveSunTransmittance(float ElevationDegrees, const AtmosphereConfiguration &P) noexcept
{
    constexpr double Ground = 6360000.0, Top = 6460000.0, Observer = 100.0;
    constexpr double RayleighScatter[3] = {5.802e-6, 13.558e-6, 33.1e-6};
    constexpr double MieExtinction      = 4.40e-6;
    constexpr double OzoneAbsorption[3] = {0.650e-6, 1.881e-6, 0.085e-6};
    const double     Elevation          = static_cast<double>(ElevationDegrees) * AtmospherePi / 180.0;
    const double     Dz                 = std::sin(Elevation);
    const double     Dx                 = std::cos(Elevation);
    const double     Oz                 = Ground + Observer;
    const double     Dot                = Oz * Dz;
    const double     Square             = Oz * Oz;
    const double     GroundDiscriminant = Dot * Dot - (Square - Ground * Ground);
    if (GroundDiscriminant > 0.0 && (-Dot - std::sqrt(GroundDiscriminant)) > 0.0) return {0.0f, 0.0f, 0.0f};
    const double Exit     = -Dot + std::sqrt(Dot * Dot - (Square - Top * Top));
    const double Step     = Exit / 64.0;
    double       Depth[3] = {0.0, 0.0, 0.0};
    for (int Sample = 0; Sample < 64; ++Sample)
    {
        const double Travel = (Sample + 0.5) * Step;
        const double X = Dx * Travel, Z = Oz + Dz * Travel;
        const double Altitude = std::sqrt(X * X + Z * Z) - Ground;
        const double Rayleigh = std::exp(-Altitude / std::max(1.0, static_cast<double>(P.RayleighHeightMetres)));
        const double Mie      = std::exp(-Altitude / std::max(1.0, static_cast<double>(P.MieHeightMetres)));
        const double Ozone    = std::max(0.0, 1.0 - std::fabs(Altitude - 25000.0) / 15000.0);
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Depth[Channel] += (RayleighScatter[Channel] * Rayleigh * P.RayleighScattering + MieExtinction * Mie * ResolveMie(P) +
                               OzoneAbsorption[Channel] * Ozone * P.OzoneAbsorptionScale) *
                              Step;
        }
    }
    return {static_cast<float>(std::exp(-Depth[0])), static_cast<float>(std::exp(-Depth[1])), static_cast<float>(std::exp(-Depth[2]))};
}

// 📝 AtmospherePhaseShape: the normalised Henyey–Greenstein shape (radius squared ∝ relative phase).
[[nodiscard]] float ResolvePhaseShape(float G, float Angle) noexcept
{
    const float Bounded = std::clamp(G, -MieGLimit, MieGLimit);
    const float Shape =
        (1.0f - std::fabs(Bounded)) * (1.0f - std::fabs(Bounded)) / (1.0f + Bounded * Bounded - 2.0f * Bounded * std::cos(Angle));
    return std::pow(Shape, 1.5f);
}

[[nodiscard]] std::string ResolveFigure(const char *Format, double Quantity)
{
    char Text[48]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

void Write(
    const ControlColumn &Page, ImFont *Face, float CssSize, ImVec2 Baseline, ImU32 Colour, const char *Text, float CssTracking = 0.0f)
{
    const float S = Page.QueryScale();
    (void)GlyphRun::Construct(Page, Face, CssSize * S, Baseline, Colour, Text, CssTracking * S);
}

[[nodiscard]] float Measure(const ControlColumn &Page, ImFont *Face, float CssSize, const char *Text, float CssTracking = 0.0f)
{
    const float S = Page.QueryScale();
    return GlyphRun::Measure(Page, Face, CssSize * S, Text, CssTracking * S);
}

// 📝 .at-section-head: h2 14 px (letter-spacing −.2) and the 8 px index (letter-spacing .7), baseline-aligned, margin-bottom
//    Below; Stacked puts the index under the title (.at-constituents).
void ConstructSectionHead(ControlColumn &Page, const char *Title, const char *Index, bool Stacked = false, float Below = 16.0f)
{
    const EditorTypefaces &Faces = Page.QueryFaces();
    const float            S     = Page.QueryScale();
    const float            Tall  = Stacked ? 14.0f * 1.25f + 7.0f + 8.0f * 1.4f : 14.0f * 1.25f;
    const ImVec2           Top   = Page.Space(Tall, 0.0f, Below);
    const float            Line  = GlyphRun::ResolveBaseline(Top.y, 14.0f * S, 1.25f);
    Write(Page, Faces.Regular, 14.0f, ImVec2(Top.x, Line), ResolveCssColour(0xE0E7D2FF), Title, -0.2f);
    if (Stacked)
    {
        const float Under = GlyphRun::ResolveBaseline(Top.y + (14.0f * 1.25f + 7.0f) * S, 8.0f * S, 1.4f);
        Write(Page, Faces.Regular, 8.0f, ImVec2(Top.x, Under), ResolveCssColour(0x81916FFF), Index, 0.7f);
        return;
    }
    const float Advance = Measure(Page, Faces.Regular, 8.0f, Index, 0.7f);
    Write(Page, Faces.Regular, 8.0f, ImVec2(Top.x + Page.QueryWidth() - Advance, Line), ResolveCssColour(0x81916FFF), Index, 0.7f);
}

// 📝 A top border line (border-top: 1px solid Colour) with its margins.
void ConstructRule(ControlColumn &Page, ImU32 Colour, float MarginTop, float PaddingBelow)
{
    const float  S   = Page.QueryScale();
    const ImVec2 Top = Page.Space(1.0f, MarginTop, 0.0f);
    Page.QueryDrawList()->AddRectFilled(Top, ImVec2(Top.x + Page.QueryWidth(), Top.y + S), Colour);
    (void)Page.Space(PaddingBelow, 0.0f, 0.0f);
}

// 📝 A paragraph in CSS px, wrapped to the column (the notes and small captions the theme's caption size does not cover).
void ConstructNote(
    ControlColumn &Page, const char *Text, float CssSize, ImU32 Colour, float MarginTop, float MarginBottom, float LineHeight = 1.5f)
{
    const EditorTypefaces   &Faces = Page.QueryFaces();
    const float              S     = Page.QueryScale();
    std::vector<std::string> Lines;
    std::string              Current;
    const char              *Cursor = Text;
    while (*Cursor != '\0')
    {
        const char *WordEnd = Cursor;
        while (*WordEnd != '\0' && *WordEnd != ' ')
            ++WordEnd;
        const std::string Word(Cursor, WordEnd);
        const std::string Trial = Current.empty() ? Word : Current + " " + Word;
        if (!Current.empty() && Measure(Page, Faces.Regular, CssSize, Trial.c_str()) > Page.QueryWidth())
        {
            Lines.push_back(Current);
            Current = Word;
        }
        else
        {
            Current = Trial;
        }
        Cursor = *WordEnd == ' ' ? WordEnd + 1 : WordEnd;
    }
    if (!Current.empty()) Lines.push_back(Current);
    const float  Leading = CssSize * LineHeight;
    const ImVec2 Top     = Page.Space(Leading * static_cast<float>(Lines.size()), MarginTop, MarginBottom);
    for (size_t Index = 0; Index < Lines.size(); ++Index)
    {
        const float Baseline = GlyphRun::ResolveBaseline(Top.y + Leading * static_cast<float>(Index) * S, CssSize * S, LineHeight);
        Write(Page, Faces.Regular, CssSize, ImVec2(Top.x, Baseline), Colour, Lines[Index].c_str());
    }
}

// 📝 A rounded pill drawn at a screen position (the probe tag, the preview tag): border, fill, text; returns its size.
ImVec2 ConstructPill(ControlColumn &Page,
                     ImVec2         Min,
                     const char    *Text,
                     float          CssSize,
                     float          PadX,
                     float          PadY,
                     ImU32          Fill,
                     ImU32          Border,
                     ImU32          Ink,
                     float          Radius)
{
    const EditorTypefaces &Faces = Page.QueryFaces();
    const float            S     = Page.QueryScale();
    const float            Width = Measure(Page, Faces.Regular, CssSize, Text) + 2.0f * PadX * S + 2.0f * S;
    const float            Tall  = (CssSize * 1.25f + 2.0f * PadY + 2.0f) * S;
    ImDrawList *const      Ink2  = Page.QueryDrawList();
    if (Fill != 0) Ink2->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + Tall), Fill, Radius * S);
    if (Border != 0) Ink2->AddRect(Min, ImVec2(Min.x + Width, Min.y + Tall), Border, Radius * S, ImDrawFlags_None, S);
    Write(Page, Faces.Regular, CssSize,
          ImVec2(Min.x + (PadX + 1.0f) * S, GlyphRun::ResolveBaseline(Min.y + (PadY + 1.0f) * S, CssSize * S, 1.25f)), Ink, Text);
    return ImVec2(Width, Tall);
}

// 📝 The .co-card of this page (background #232720, no border, radius 23).
CardLook ResolveCardLook()
{
    CardLook Look;
    Look.Fill   = ResolveCssColour(0x232720FF);
    Look.Radius = 23.0f;
    return Look;
}

// 📝 .at-column-scan: radial #6c8f4320 at 50% 55% over linear-gradient(155deg, #20271d99, #11171188), border #d6e3a610, r 25.
CardLook ResolveScanLook()
{
    CardLook Look;
    Look.Fill          = ResolveCssColour(0x1B2219FF);
    Look.FillEnd       = ResolveCssColour(0x121812FF);
    Look.Glow          = ResolveCssColour(0x6C8F4320);
    Look.GlowCentre    = ImVec2(0.5f, 0.55f);
    Look.GlowExtent    = 0.65f;
    Look.Border        = ResolveCssColour(0xD6E3A610);
    Look.Radius        = 25.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 10.0f;
    Look.PaddingBottom = 12.0f;
    Look.MarginBottom  = 18.0f;
    return Look;
}

// 📝 .at-atlas: padding 14, #1e241b, radius 18.
CardLook ResolveAtlasLook()
{
    CardLook Look;
    Look.Fill          = ResolveCssColour(0x1E241BFF);
    Look.Radius        = 18.0f;
    Look.PaddingTop    = 14.0f;
    Look.PaddingSide   = 14.0f;
    Look.PaddingBottom = 14.0f;
    return Look;
}

// 📝 A transparent block (the bare .at-* sections) with its padding and margins.
CardLook ResolveBareLook(float PaddingTop, float PaddingBottom, float MarginTop, float MarginBottom)
{
    CardLook Look;
    Look.Radius        = 0.0f;
    Look.PaddingTop    = PaddingTop;
    Look.PaddingSide   = 0.0f;
    Look.PaddingBottom = PaddingBottom;
    Look.MarginTop     = MarginTop;
    Look.MarginBottom  = MarginBottom;
    return Look;
}

// 📝 The baking cards (NativeEnvironmentPanels.js Card: the native panel's card on this page's #232720).
CardLook ResolveBakeLook()
{
    CardLook Look = ResolveCardLook();
    Look.Radius   = 16.0f;
    return Look;
}

// 📝 The optical bench's switch rows: label, then a 30 × 17 track with an 11 px knob (on: #cadba2 / #39432c).
bool ConstructSwitchRow(
    ControlColumn &Page, const char *Id, const char *Label, bool &InOutOn, bool Enabled, const char *Tooltip, bool RuleAbove)
{
    const EditorTypefaces &Faces = Page.QueryFaces();
    const float            S     = Page.QueryScale();
    const ImVec2           Top   = Page.Space(43.0f, 0.0f, 0.0f);
    ImDrawList *const      Ink   = Page.QueryDrawList();
    if (RuleAbove) Ink->AddRectFilled(Top, ImVec2(Top.x + Page.QueryWidth(), Top.y + S), ResolveCssColour(0xD6E3A60B));
    ImGui::SetCursorScreenPos(Top);
    const bool Clicked = ImGui::InvisibleButton(Id, ImVec2(Page.QueryWidth(), 43.0f * S)) && Enabled;
    if (Tooltip != nullptr) ControlColumn::ConstructTooltip(Tooltip);
    if (Clicked) InOutOn = !InOutOn;
    const float Fade   = Enabled ? 1.0f : 0.45f;
    const float Centre = Top.y + 21.5f * S;
    Write(Page, Faces.Regular, 10.0f, ImVec2(Top.x, GlyphRun::ResolveBaseline(Centre - 6.25f * S, 10.0f * S, 1.25f)),
          FadeColour(ResolveCssColour(0xBDC9ADFF), Fade), Label);
    const ImVec2 TrackMin(Top.x + Page.QueryWidth() - 30.0f * S, Centre - 8.5f * S);
    Ink->AddRectFilled(TrackMin, ImVec2(TrackMin.x + 30.0f * S, TrackMin.y + 17.0f * S),
                       FadeColour(InOutOn ? ResolveCssColour(0xCADBA2FF) : ResolveCssColour(0x424B38FF), Fade), 20.0f * S);
    Ink->AddCircleFilled(ImVec2(TrackMin.x + (InOutOn ? 21.5f : 8.5f) * S, Centre), 5.5f * S,
                         FadeColour(InOutOn ? ResolveCssColour(0x39432CFF) : ResolveCssColour(0xABB69BFF), Fade));
    return Clicked;
}

// 📝 .at-colour-well: the swatch, the name, the caption and the hex; a click opens the colour picker on the linear value.
bool ConstructColourWell(ControlColumn &Page,
                         const char    *Id,
                         ImVec2         Min,
                         float          Width,
                         const char    *Name,
                         const char    *Caption,
                         float (&InOutLinear)[3],
                         bool        Enabled,
                         const char *Tooltip)
{
    const EditorTypefaces &Faces = Page.QueryFaces();
    const float            S     = Page.QueryScale();
    ImDrawList *const      Ink   = Page.QueryDrawList();
    const float            Tall  = (16.0f + 45.0f + 12.0f + 11.0f * 1.25f + 6.0f + 9.0f * 1.25f + 6.0f + 9.0f + 10.0f * 1.25f + 16.0f) * S;
    const float            Fade  = Enabled ? 1.0f : 0.45f;
    Ink->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + Tall), ResolveCssColour(0x252B22FF), 20.0f * S);
    Ink->AddRect(Min, ImVec2(Min.x + Width, Min.y + Tall), ResolveCssColour(0xD6E3A60C), 20.0f * S, ImDrawFlags_None, S);
    int Display[3]{};
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        Display[Channel] = static_cast<int>(std::lround(std::pow(std::clamp(InOutLinear[Channel], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f));
    }
    const ImVec2 Swatch(Min.x + (16.0f + 22.5f) * S, Min.y + (16.0f + 22.5f) * S);
    Ink->AddCircle(Swatch, 29.5f * S, ResolveCssColour(0xFFFFFF08), 48, S);
    Ink->AddCircle(Swatch, 26.0f * S, ResolveCssColour(0xFFFFFF04), 48, 6.0f * S);
    Ink->AddCircleFilled(Swatch, 22.5f * S, FadeColour(IM_COL32(Display[0], Display[1], Display[2], 255), Fade), 48);
    float Line = Min.y + (16.0f + 45.0f + 12.0f) * S;
    Write(Page, Faces.Regular, 11.0f, ImVec2(Min.x + 16.0f * S, GlyphRun::ResolveBaseline(Line, 11.0f * S, 1.25f)),
          FadeColour(ResolveCssColour(0xDFE6D1FF), Fade), Name);
    Line += (11.0f * 1.25f + 6.0f) * S;
    Write(Page, Faces.Regular, 9.0f, ImVec2(Min.x + 16.0f * S, GlyphRun::ResolveBaseline(Line, 9.0f * S, 1.25f)),
          FadeColour(ResolveCssColour(0x94A183FF), Fade), Caption);
    Line += (9.0f * 1.25f + 6.0f + 9.0f) * S;
    char Hex[16]{};
    std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X", Display[0], Display[1], Display[2]);
    Write(Page, Faces.Regular, 10.0f, ImVec2(Min.x + 16.0f * S, GlyphRun::ResolveBaseline(Line, 10.0f * S, 1.25f)),
          FadeColour(ResolveCssColour(0xC0CCADFF), Fade), Hex);

    ImGui::SetCursorScreenPos(Min);
    ImGui::PushID(Id);
    const bool Pressed = ImGui::InvisibleButton("##Well", ImVec2(Width, Tall));
    if (Tooltip != nullptr) ControlColumn::ConstructTooltip(Tooltip);
    if (Pressed && Enabled) ImGui::OpenPopup("##WellPicker");
    bool Changed = false;
    if (ImGui::BeginPopup("##WellPicker"))
    {
        float Picked[3] = {Display[0] / 255.0f, Display[1] / 255.0f, Display[2] / 255.0f};
        if (ImGui::ColorPicker3("##Picker", Picked, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex))
        {
            for (int Channel = 0; Channel < 3; ++Channel)
                InOutLinear[Channel] = std::pow(std::clamp(Picked[Channel], 0.0f, 1.0f), 2.2f);
            Changed = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return Changed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE
//------------------------------------------------------------------------------------------------------------------------

void AtmosphereInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Multi-Scatter", "Precomputed LUT", &Parameters.MultiScatteringLut});
    OutSwitches.push_back({"Aerial Fog", "Camera volume", &Parameters.AerialPerspective});
}

void AtmosphereInspector::ConstructInspector()
{
    if (ActivePane == 0 || ActivePane == 3) RefreshBakedImages();
    const ControlTheme    Theme = ControlTheme::ResolveAtmosphere();
    const EditorTypefaces Faces = EditorTypefaces::Resolve();
    ControlColumn         Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool                  Changed = false;
    ImGui::PushID("AtmosphereInspectorPage");

    // 📝 The power pill: '● Live' / '○ Off' (co-power).
    if (PageHeading::Construct(Page, "Header", "ENVIRONMENT  /  03", "Atmosphere",
                               Parameters.Enabled ? "\xE2\x97\x8F Live" : "\xE2\x97\x8B Off", "Scattering, density & the visible sky.",
                               "Toggle atmosphere"))
    {
        Parameters.Enabled = !Parameters.Enabled;
        Changed            = true;
    }
    static constexpr std::array<const char *, 4> Panes = {"Overview", "Medium", "Light response", "Baking"};
    (void)TabStrip::Construct(Page, "Atmosphere inspector sections", Panes.data(), static_cast<int>(Panes.size()), ActivePane);

    switch (ActivePane)
    {
    case 0:
        Changed |= ConstructOverview(Page);
        break;
    case 1:
        Changed |= ConstructMedium(Page);
        break;
    case 2:
        Changed |= ConstructLightResponse(Page);
        break;
    default:
        Changed |= ConstructBaking(Page);
        break;
    }

    FooterRow::Construct(Page, "ATMOSPHERIC SCATTERING", "Settings apply live \xE2\x86\x97");
    Page.Finalize();
    ImGui::PopID();
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OVERVIEW
//------------------------------------------------------------------------------------------------------------------------

bool AtmosphereInspector::ConstructOverview(ControlColumn &Page)
{
    const EditorTypefaces &Faces   = Page.QueryFaces();
    const float            S       = Page.QueryScale();
    bool                   Changed = false;
    float                  Mie     = ResolveMie(Parameters);

    // ── 01 / DENSITY SCAN (AtmosphereColumnInstrument.js) ──────────────────────────────────────────────────────────
    Page.PushIndent(-8.0f, -8.0f); // margin: 0 -8px 18px
    Page.BeginCard(ResolveScanLook(), "Atmospheric column", "01 / DENSITY SCAN");
    const ImVec2 CardOrigin = Page.QueryCardOrigin();
    const float  CardWidth  = Page.QueryCardWidth();
    {
        DiagramDrag       Drag;
        const DiagramPen  Pen = DiagramCanvas::Begin(Page, "##DensityScan", 640.0f, 520.0f, 0.0f, 0.0f, &Drag,
                                                     "Atmosphere altitude probe \xC2\xB7 drag shell or use arrow keys");
        ImDrawList *const Ink = Pen.QueryDrawList();
        Ink->PushClipRect(Pen.QueryScreen(ImVec2(0.0f, 0.0f)), Pen.QueryScreen(ImVec2(640.0f, 520.0f)), true);
        const ImVec2    Centre(320.0f, 453.0f);
        constexpr float Radius = 240.0f;
        for (float Km = 30.0f; Km >= 0.0f; Km -= 0.5f)
        {
            const DensityProfile D     = ResolveDensity(Parameters, Km * 1000.0f);
            const float          Alpha = std::min(0.6f, 0.025f + D.Rayleigh * Parameters.RayleighScattering * 0.19f + D.Mie * Mie * 0.08f);
            Pen.Circle(Centre, Radius + Km * 4.0f, 0, IM_COL32(173, 207, 175, static_cast<int>(Alpha * 255.0f + 0.5f)));
        }
        for (const float Km : {0.0f, 5.0f, 10.0f, 20.0f, 30.0f})
        {
            const float         Shell = Radius + Km * 4.0f;
            std::vector<ImVec2> Arc;
            for (int Index = 0; Index <= 64; ++Index)
            {
                const float Angle = -AtmospherePi + static_cast<float>(Index) / 64.0f * AtmospherePi;
                Arc.push_back(ImVec2(Centre.x + std::cos(Angle) * Shell, Centre.y + std::sin(Angle) * Shell));
            }
            Pen.Path(Arc, ResolveCssColour(0xD9E4C12A), 1.0f, 2.0f, 7.0f);
        }
        // createRadialGradient(250, 300, 10, 320, 453, 270): #3b4239 → #232922 (.5) → #101511, stroke #b4c59955.
        Pen.Circle(Centre, Radius, ResolveCssColour(0x161B15FF));
        Pen.Glow(ImVec2(250.0f, 300.0f), 230.0f, IM_COL32(59, 66, 57, 255), 0.85f);
        Pen.Circle(Centre, Radius, 0, ResolveCssColour(0xB4C59955));
        for (const float Across : {65.0f, 120.0f, 180.0f})
            Pen.Ellipse(Centre, Across, 240.0f, 0, ResolveCssColour(0xACBAA315));
        for (const float Down : {70.0f, 140.0f, 210.0f})
            Pen.Ellipse(Centre, 240.0f, Down, 0, ResolveCssColour(0xACBAA315));

        const float         Probe = Radius + ProbeAltitude / 1000.0f * 4.0f;
        const ImVec2        Point(320.0f + std::cos(-1.17f) * Probe, 453.0f + std::sin(-1.17f) * Probe);
        std::vector<ImVec2> ProbeArc;
        for (int Index = 0; Index <= 64; ++Index)
        {
            const float Angle = -2.4f + static_cast<float>(Index) / 64.0f * 1.8f;
            ProbeArc.push_back(ImVec2(320.0f + std::cos(Angle) * Probe, 453.0f + std::sin(Angle) * Probe));
        }
        Pen.Path(ProbeArc, ResolveCssColour(0xD4E99B99), 1.5f);
        Pen.Line(Point, ImVec2(555.0f, Point.y - 40.0f), ResolveCssColour(0xD4E99B90), 1.0f);
        Pen.Line(ImVec2(555.0f, Point.y - 40.0f), ImVec2(595.0f, Point.y - 40.0f), ResolveCssColour(0xD4E99B90), 1.0f);
        Pen.Circle(Point, 17.0f, ResolveCssColour(0xD4E99B0D), ResolveCssColour(0xD4E99B80));
        Pen.Circle(Point, 5.0f, ResolveCssColour(0xE3EFC5FF));
        Pen.Line(ImVec2(Point.x - 25.0f, Point.y), ImVec2(Point.x + 25.0f, Point.y), ResolveCssColour(0xD4E99B55), 1.0f);
        Pen.Line(ImVec2(Point.x, Point.y - 25.0f), ImVec2(Point.x, Point.y + 25.0f), ResolveCssColour(0xD4E99B55), 1.0f);
        const std::string Kilometres = ResolveFigure("%.1f km", static_cast<double>(ProbeAltitude / 1000.0f));
        Pen.Text(ImVec2(595.0f, Point.y - 52.0f), Kilometres.c_str(), ResolveCssColour(0xDCE9C5FF), 21.0f, 1);
        Pen.Text(ImVec2(36.0f, 480.0f), "GROUND / Z = 0", ResolveCssColour(0xB1BAA5FF), 19.0f, -1);
        Ink->PopClipRect();
        if (Drag.Engaged)
        {
            const float Distance = std::hypot(Drag.Point.x - 320.0f, Drag.Point.y - 453.0f);
            ProbeAltitude        = std::clamp(std::round((Distance - 240.0f) / 4.0f * 1000.0f / 500.0f) * 500.0f, 0.0f, 30000.0f);
        }
    }
    // .at-scan-result (left 16, top 56) and .at-probe-tag (right 15, top 61) over the scan.
    {
        const DensityProfile D       = ResolveDensity(Parameters, ProbeAltitude);
        const std::string    Density = ResolveFigure("%.1f", static_cast<double>(D.Rayleigh * 100.0f));
        const ImVec2         Result(CardOrigin.x + 17.0f * S, CardOrigin.y + 57.0f * S);
        float                Line = Result.y;
        Write(Page, Faces.Regular, 9.0f, ImVec2(Result.x, GlyphRun::ResolveBaseline(Line, 9.0f * S, 1.25f)), ResolveCssColour(0xAAB69CFF),
              "Relative molecular density");
        Line += (9.0f * 1.25f + 3.0f) * S;
        Write(Page, Faces.Light, 43.0f, ImVec2(Result.x, GlyphRun::ResolveBaseline(Line, 43.0f * S, 1.15f)), ResolveCssColour(0xE9EFD9FF),
              Density.c_str(), -2.0f);
        Line += (43.0f * 1.15f + 3.0f) * S;
        Write(Page, Faces.Regular, 9.0f, ImVec2(Result.x, GlyphRun::ResolveBaseline(Line, 9.0f * S, 1.25f)), ResolveCssColour(0x92A17FFF),
              "% of sea level");
        const std::string Tag  = "\xE2\x86\x97 " + ResolveFigure("%.1f", static_cast<double>(ProbeAltitude / 1000.0f)) + " km probe";
        const float       TagW = Measure(Page, Faces.Regular, 10.0f, Tag.c_str()) + (20.0f + 2.0f) * S;
        (void)ConstructPill(Page, ImVec2(CardOrigin.x + CardWidth - 16.0f * S - TagW, CardOrigin.y + 62.0f * S), Tag.c_str(), 10.0f, 10.0f,
                            7.0f, ResolveCssColour(0x2E3A2670), ResolveCssColour(0xD6E3A626), ResolveCssColour(0xD8E5BDFF), 20.0f);
    }
    Page.PushIndent(7.0f, 7.0f); // .at-probe-input { padding: 0 7px }
    (void)SliderRow::Construct(Page, "ProbeAltitude", "Probe altitude", ProbeAltitude, 0.0f, 30000.0f, 500.0f, "km", 0.001f);
    ConstructNote(Page, "Exaggerated shell diagram \xC2\xB7 relative density, not a rendered planet. Probe changes this diagnostic only.",
                  9.0f, ResolveCssColour(0x89977CFF), 9.0f, 0.0f);
    Page.PopIndent();
    const float ScanTop = CardOrigin.y;
    Page.EndCard();
    const float ScanBottom = Page.QueryCursor().y;
    Page.PopIndent();
    // .at-aerosol-result: right 13, top 57 % of the scan.
    {
        const DensityProfile D     = ResolveDensity(Parameters, ProbeAltitude);
        const std::string    Value = ResolveFigure("%.2f%%", static_cast<double>(D.Mie * 100.0f));
        const float          BoxW =
            std::max(Measure(Page, Faces.Regular, 9.0f, "Aerosol density"), Measure(Page, Faces.Light, 22.0f, Value.c_str())) +
            (26.0f + 2.0f) * S;
        const float       BoxH = (11.0f + 9.0f * 1.25f + 7.0f + 22.0f * 1.25f + 11.0f + 2.0f) * S;
        const ImVec2      BoxMin(CardOrigin.x + CardWidth - 14.0f * S - BoxW, ScanTop + 0.57f * (ScanBottom - ScanTop));
        ImDrawList *const Ink = Page.QueryDrawList();
        Ink->AddRectFilled(ImVec2(BoxMin.x, BoxMin.y + 7.0f * S), ImVec2(BoxMin.x + BoxW, BoxMin.y + BoxH + 11.0f * S),
                           ResolveCssColour(0x00000018), 18.0f * S);
        Ink->AddRectFilled(BoxMin, ImVec2(BoxMin.x + BoxW, BoxMin.y + BoxH), ResolveCssColour(0x252D22E8), 15.0f * S);
        Ink->AddRect(BoxMin, ImVec2(BoxMin.x + BoxW, BoxMin.y + BoxH), ResolveCssColour(0xD6E3A612), 15.0f * S, ImDrawFlags_None, S);
        float Line = BoxMin.y + 12.0f * S;
        Write(Page, Faces.Regular, 9.0f, ImVec2(BoxMin.x + 14.0f * S, GlyphRun::ResolveBaseline(Line, 9.0f * S, 1.25f)),
              ResolveCssColour(0xA8B398FF), "Aerosol density");
        Line += (9.0f * 1.25f + 7.0f) * S;
        Write(Page, Faces.Light, 22.0f, ImVec2(BoxMin.x + 14.0f * S, GlyphRun::ResolveBaseline(Line, 22.0f * S, 1.25f)),
              ResolveCssColour(0xDCE8BFFF), Value.c_str());
    }

    // ── KPIs (co-kpis) ─────────────────────────────────────────────────────────────────────────────────────────────
    {
        const std::array<MetricTile, 3> Metrics = {{
            {"Rayleigh", ResolveFigure("%.2f", static_cast<double>(Parameters.RayleighScattering)), "\xC3\x97 molecules"},
            {"Mie", ResolveFigure("%.2f", static_cast<double>(Mie)), "\xC3\x97 aerosols"},
            {"Ozone", ResolveFigure("%.2f", static_cast<double>(Parameters.OzoneAbsorptionScale)), "\xC3\x97 absorption"},
        }};
        MetricTiles::Construct(Page, Metrics.data(), static_cast<int>(Metrics.size()));
    }

    // ── GPU / SCATTERING (the sky-view capture) ────────────────────────────────────────────────────────────────────
    Page.BeginCard(ResolveAtlasLook(), "Sky-view capture", "GPU / SCATTERING");
    {
        const ImVec2 Row      = Page.Space(8.0f * 1.25f + 10.0f, 0.0f, 10.0f);
        const float  Baseline = GlyphRun::ResolveBaseline(Row.y, 13.0f * S, 1.25f) + 1.0f * S;
        Write(Page, Faces.Regular, 9.0f, ImVec2(Row.x, Baseline), Page.QueryTheme().Label, "Lookup revision");
        Write(Page, Faces.Regular, 13.0f, ImVec2(Row.x + Measure(Page, Faces.Regular, 9.0f, "Lookup revision") + 7.0f * S, Baseline),
              Page.QueryTheme().Ink, std::to_string(UploadedGeneration).c_str());
        const float TagW = Measure(Page, Faces.Regular, 8.0f, "GPU / RGBA16F") + (12.0f + 2.0f) * S;
        (void)ConstructPill(Page, ImVec2(Row.x + Page.QueryWidth() - TagW, Row.y), "GPU / RGBA16F", 8.0f, 6.0f, 4.0f,
                            Page.QueryTheme().TagFill, Page.QueryTheme().TagBorder, Page.QueryTheme().TagText, 20.0f);
        const DiagramPen Pen = DiagramCanvas::Begin(Page, "##SkyCapture", 640, 340, 0, 0, nullptr, "Actual GPU sky-view lookup readback");
        Pen.Rect({0, 0}, {640, 340}, ResolveCssColour(0x141913FF), 14);
        if (SkyPlate.QueryReady())
            SkyPlate.Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 340}), IM_COL32_WHITE, 14 * S);
        else
            Pen.Text({320, 178}, "Render the viewport to obtain a sky LUT", ResolveCssColour(0x8C9A7DFF), 18);
        ConstructNote(Page,
                      "Decoded sky-view GPU pixels \xC2\xB7 tone-mapped diagnostic, not a scene camera. Sun disc, stars, Moon and fog are "
                      "excluded.",
                      9.0f, Page.QueryTheme().Caption, 6.0f, 0.0f);
    }
    Page.EndCard();

    // ── LIVE COEFFICIENTS (the medium recipe) ──────────────────────────────────────────────────────────────────────
    ConstructRule(Page, ResolveCssColour(0xD6E3A61A), 24.0f, 18.0f);
    ConstructSectionHead(Page, "Medium recipe", "LIVE COEFFICIENTS");
    struct Channel
    {
        const char *Id;
        const char *Label;
        const char *Mark;
        const char *Note;
        float      *Quantity;
        float       Maximum;
        uint32_t    Fill;
        uint32_t    Ink;
    };
    const std::array<Channel, 3> Channels = {{
        {"RecipeRayleigh", "Molecular scattering", "R", "Air molecules", &Parameters.RayleighScattering, RayleighMaximum, 0x92BBA315,
         0xB8D3BDFF},
        {"RecipeMie", "Aerosol haze", "M", "Suspended particles", &Mie, MieMaximum, 0xD4B77F15, 0xDECFA8FF},
        {"RecipeOzone", "Ozone absorption", "O\xE2\x82\x83", "Selective absorption", &Parameters.OzoneAbsorptionScale, OzoneMaximum,
         0xB7A2CC15, 0xCFC0DFFF},
    }};
    for (const Channel &Entry : Channels)
    {
        const float Top = Page.QueryCursor().y;
        Page.PushIndent(36.0f + 13.0f);
        Changed |= SliderRow::Construct(Page, Entry.Id, Entry.Label, *Entry.Quantity, 0.0f, Entry.Maximum, 0.01f, "\xC3\x97");
        ConstructNote(Page, Entry.Note, 9.0f, ResolveCssColour(0x7F8B75FF), 3.0f, 15.0f, 1.25f);
        Page.PopIndent();
        const float  Bottom = Page.QueryCursor().y;
        const ImVec2 MarkMin(Page.QueryCursor().x, 0.5f * (Top + Bottom) - 22.5f * S);
        Page.QueryDrawList()->AddRectFilled(MarkMin, ImVec2(MarkMin.x + 33.0f * S, MarkMin.y + 45.0f * S), ResolveCssColour(Entry.Fill),
                                            20.0f * S);
        const float MarkW = Measure(Page, Faces.Regular, 15.0f, Entry.Mark);
        Write(Page, Faces.Regular, 15.0f,
              ImVec2(MarkMin.x + 0.5f * (33.0f * S - MarkW), GlyphRun::ResolveBaseline(MarkMin.y + 15.0f * S, 15.0f * S, 1.0f)),
              ResolveCssColour(Entry.Ink), Entry.Mark);
    }
    Parameters.MieTurbidity = 1.0f + TurbidityPerMie * Mie;
    (void)Page.Space(0.0f, 0.0f, 12.0f);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MEDIUM
//------------------------------------------------------------------------------------------------------------------------

bool AtmosphereInspector::ConstructMedium(ControlColumn &Page)
{
    const EditorTypefaces &Faces   = Page.QueryFaces();
    const float            S       = Page.QueryScale();
    bool                   Changed = false;
    float                  Mie     = ResolveMie(Parameters);

    // ── SCALE-HEIGHT COMPARATOR (How the air thins) ────────────────────────────────────────────────────────────────
    (void)Page.Space(0.0f, 8.0f, 0.0f);
    ConstructSectionHead(Page, "How the air thins", "SCALE-HEIGHT COMPARATOR");
    {
        const ImVec2 Legend = Page.Space(10.0f + 10.0f * 1.25f + 10.0f, 0.0f, 0.0f);
        const float  Line   = GlyphRun::ResolveBaseline(Legend.y + 10.0f * S, 10.0f * S, 1.25f);
        Write(Page, Faces.Regular, 10.0f, ImVec2(Legend.x, Line), ResolveCssColour(0xB8D3BDFF), "Rayleigh / molecules");
        const float MieW = Measure(Page, Faces.Regular, 10.0f, "Mie / aerosols");
        Write(Page, Faces.Regular, 10.0f, ImVec2(Legend.x + Page.QueryWidth() - MieW, Line), ResolveCssColour(0xDECFA8FF),
              "Mie / aerosols");
        const float RuleY = Legend.y + (10.0f + 10.0f * 1.25f + 10.0f) * S;
        Page.QueryDrawList()->AddRectFilled(ImVec2(Legend.x, RuleY - S), ImVec2(Legend.x + Page.QueryWidth(), RuleY),
                                            ResolveCssColour(0xD6E3A615));
    }
    {
        DiagramDrag      Drag;
        const DiagramPen Pen = DiagramCanvas::Begin(Page, "##ScaleHeights", 640.0f, 430.0f, 15.0f, 12.0f, &Drag,
                                                    "Scale height comparator \xC2\xB7 drag left for Rayleigh or right for Mie");
        // linear-gradient(90deg, #92bba308, transparent 47%, transparent 53%, #d4b77f08)
        {
            ImDrawList *const Panes = Pen.QueryDrawList();
            const ImU32       Clear = IM_COL32(0, 0, 0, 0);
            Panes->AddRectFilledMultiColor(Pen.QueryScreen(ImVec2(0.0f, 0.0f)), Pen.QueryScreen(ImVec2(300.8f, 430.0f)),
                                           ResolveCssColour(0x92BBA308), Clear, Clear, ResolveCssColour(0x92BBA308));
            Panes->AddRectFilledMultiColor(Pen.QueryScreen(ImVec2(339.2f, 0.0f)), Pen.QueryScreen(ImVec2(640.0f, 430.0f)), Clear,
                                           ResolveCssColour(0xD4B77F08), ResolveCssColour(0xD4B77F08), Clear);
        }
        constexpr float Bottom = 350.0f, Top = 55.0f, Height = Bottom - Top;
        auto            Y = [&](float Metres) { return Bottom - Metres / 20000.0f * Height; };
        for (const float Km : {0.0f, 5.0f, 10.0f, 15.0f, 20.0f})
        {
            const float Row = Y(Km * 1000.0f);
            Pen.Line(ImVec2(28.0f, Row), ImVec2(280.0f, Row), ResolveCssColour(0xB0C6AA15), 1.0f, 2.0f, 7.0f);
            Pen.Line(ImVec2(360.0f, Row), ImVec2(612.0f, Row), ResolveCssColour(0xB0C6AA15), 1.0f, 2.0f, 7.0f);
            Pen.Text(ImVec2(320.0f, Row + 6.0f), ResolveFigure("%.0f", static_cast<double>(Km)).c_str(), ResolveCssColour(0xA9B59BFF),
                     20.0f);
        }
        Pen.Text(ImVec2(320.0f, 25.0f), "km", ResolveCssColour(0x7C8C70FF), 20.0f);
        struct Ribbon
        {
            int      Key;
            float    Centre;
            float    Sign;
            uint32_t Colour;
            uint32_t Fill;
            float    Height;
        };
        const std::array<Ribbon, 2> Ribbons = {{{0, 280.0f, -1.0f, 0xB8D3BDFF, 0x92BBA324, Parameters.RayleighHeightMetres},
                                                {1, 360.0f, 1.0f, 0xDECFA8FF, 0xD4B77F20, Parameters.MieHeightMetres}}};
        ImDrawList *const           Ink     = Pen.QueryDrawList();
        for (const Ribbon &Entry : Ribbons)
        {
            std::vector<ImVec2> Points;
            for (int Index = 0; Index <= 100; ++Index)
            {
                const float          Altitude = static_cast<float>(Index) / 100.0f * 20000.0f;
                const DensityProfile D        = ResolveDensity(Parameters, Altitude);
                Points.push_back(ImVec2(Entry.Centre + Entry.Sign * (Entry.Key == 0 ? D.Rayleigh : D.Mie) * 235.0f, Y(Altitude)));
            }
            // The ribbon (moveTo the axis foot, the curve, lineTo the axis top, fill): strips between the axis and the curve,
            //    without fill antialiasing so adjacent strips meet without seams.
            const ImDrawListFlags Flags = Ink->Flags;
            Ink->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (size_t Index = 1; Index < Points.size(); ++Index)
            {
                Ink->AddQuadFilled(Pen.QueryScreen(ImVec2(Entry.Centre, Points[Index - 1].y)), Pen.QueryScreen(Points[Index - 1]),
                                   Pen.QueryScreen(Points[Index]), Pen.QueryScreen(ImVec2(Entry.Centre, Points[Index].y)),
                                   ResolveCssColour(Entry.Fill));
            }
            Ink->Flags = Flags;
            Pen.Path(Points, ResolveCssColour(Entry.Colour), 2.5f);
            Pen.Line(ImVec2(Entry.Centre, Top), ImVec2(Entry.Centre, Bottom), ResolveCssColour(0xB0C6AA35), 1.0f);
            const float Row    = Y(Entry.Height);
            const float Handle = Entry.Centre + Entry.Sign * std::exp(-1.0f) * 235.0f;
            Pen.Line(ImVec2(Entry.Centre, Row), ImVec2(Handle, Row), ResolveCssColour(Entry.Colour), 1.0f, 3.0f, 6.0f);
            Pen.Circle(ImVec2(Handle, Row), 12.0f, ResolveCssColour(0x141B12FF), ResolveCssColour(Entry.Colour));
            Pen.Circle(ImVec2(Handle, Row), 4.0f, ResolveCssColour(Entry.Colour));
            const std::string Kilometres = ResolveFigure("%.1f km", static_cast<double>(Entry.Height / 1000.0f));
            Pen.Text(ImVec2(Entry.Key == 0 ? 40.0f : 600.0f, 402.0f), Kilometres.c_str(), ResolveCssColour(Entry.Colour), 30.0f,
                     Entry.Key == 0 ? -1 : 1);
        }
        Pen.Text(ImVec2(320.0f, 402.0f), "1/e HEIGHT", ResolveCssColour(0x7F9071FF), 17.0f);
        if (Drag.Engaged)
        {
            ScaleRibbon       = Drag.Point.x < 320.0f ? 0 : 1;
            const float Value = std::round((350.0f - Drag.Point.y) / 295.0f * 20000.0f / 100.0f) * 100.0f;
            if (ScaleRibbon == 0)
                Parameters.RayleighHeightMetres = std::clamp(Value, RayleighHeightMinimum, RayleighHeightMaximum);
            else
                Parameters.MieHeightMetres = std::clamp(Value, MieHeightMinimum, MieHeightMaximum);
            Changed = true;
        }
    }
    Changed |= SliderRow::Construct(Page, "RayleighHeight", "Rayleigh scale height", Parameters.RayleighHeightMetres, RayleighHeightMinimum,
                                    RayleighHeightMaximum, 100.0f, "m");
    Changed |= SliderRow::Construct(Page, "MieHeight", "Mie scale height", Parameters.MieHeightMetres, MieHeightMinimum, MieHeightMaximum,
                                    100.0f, "m");
    ConstructNote(Page,
                  "Drag either ribbon to edit its scale height. Markers sit at 36.8% of sea-level density. Each ribbon is normalised "
                  "separately.",
                  10.0f, Page.QueryTheme().Caption, 12.0f, 16.0f);

    // ── SEA-LEVEL MULTIPLIERS (Constituents) ───────────────────────────────────────────────────────────────────────
    {
        CardLook Look    = ResolveBareLook(18.0f, 12.0f, 6.0f, 24.0f);
        Look.Fill        = ResolveCssColour(0x1D211BFF); // #252b2180 over the page
        Look.Radius      = 20.0f;
        Look.Corners     = ImDrawFlags_RoundCornersRight;
        Look.LeftRule    = ResolveCssColour(0xB5C79A40);
        Look.PaddingSide = 15.0f;
        Page.BeginCard(Look, nullptr, nullptr);
        ConstructSectionHead(Page, "Constituents", "SEA-LEVEL MULTIPLIERS", true);
        Changed |= SliderRow::Construct(Page, "Rayleigh", "Rayleigh multiplier", Parameters.RayleighScattering, 0.0f, RayleighMaximum,
                                        0.01f, "\xC3\x97");
        Changed |= SliderRow::Construct(Page, "Mie", "Mie multiplier", Mie, 0.0f, MieMaximum, 0.01f, "\xC3\x97");
        Changed |=
            SliderRow::Construct(Page, "Ozone", "Ozone multiplier", Parameters.OzoneAbsorptionScale, 0.0f, OzoneMaximum, 0.01f, "\xC3\x97");
        Page.EndCard();
        Parameters.MieTurbidity = 1.0f + TurbidityPerMie * Mie;
    }

    // ── DIRECTIONAL RESPONSE (Where light scatters) ────────────────────────────────────────────────────────────────
    ConstructRule(Page, ResolveCssColour(0xD6E3A620), 0.0f, 20.0f);
    ConstructSectionHead(Page, "Where light scatters", "DIRECTIONAL RESPONSE");
    {
        const DiagramPen Pen = DiagramCanvas::Begin(Page, "##PhaseShape", 640.0f, 340.0f, 0.0f, 12.0f, nullptr,
                                                    "Normalised Henyey\xE2\x80\x93Greenstein directional scattering shape");
        Pen.Glow(ImVec2(320.0f, 170.0f), 300.0f, IM_COL32(214, 227, 166, 255), 0.03f);
        const ImVec2 Origin(320.0f, 165.0f);
        for (const float Ring : {45.0f, 90.0f, 135.0f})
            Pen.Circle(Origin, Ring, 0, ResolveCssColour(0xAEBC9819));
        Pen.Line(ImVec2(40.0f, 165.0f), ImVec2(600.0f, 165.0f), ResolveCssColour(0xAEBC9830), 1.0f, 3.0f, 6.0f);
        std::vector<ImVec2> Shape;
        for (int Index = 0; Index <= 360; ++Index)
        {
            const float Angle  = static_cast<float>(Index) / 360.0f * 2.0f * AtmospherePi;
            const float Radius = std::sqrt(ResolvePhaseShape(Parameters.MieAnisotropyG, Angle)) * 135.0f;
            Shape.push_back(ImVec2(320.0f + std::cos(Angle) * Radius, 165.0f + std::sin(Angle) * Radius));
        }
        Pen.Path(Shape, ResolveCssColour(0xD6DCB0FF), 2.5f);
        Pen.Line(ImVec2(40.0f, 165.0f), ImVec2(175.0f, 165.0f), ResolveCssColour(0xBAC5A7FF), 2.0f);
        Pen.Path({ImVec2(162.0f, 157.0f), ImVec2(175.0f, 165.0f), ImVec2(162.0f, 173.0f)}, ResolveCssColour(0xBAC5A7FF), 2.0f);
        Pen.Circle(Origin, 5.0f, ResolveCssColour(0xE0E9C8FF));
        Pen.Text(ImVec2(40.0f, 320.0f), "INCIDENT LIGHT \xE2\x86\x92", ResolveCssColour(0x9AA98CFF), 20.0f, -1);
        Pen.Text(ImVec2(600.0f, 320.0f), "FORWARD \xE2\x86\x92", ResolveCssColour(0x9AA98CFF), 20.0f, 1);
    }
    Changed |= SliderRow::Construct(Page, "MieG", "Mie anisotropy", Parameters.MieAnisotropyG, -MieGLimit, MieGLimit, 0.01f, "g");
    ConstructNote(Page,
                  "Normalised phase reference, not radiance. Squared plot radius represents relative phase strength; positive g favours "
                  "forward scattering.",
                  10.0f, Page.QueryTheme().Caption, 12.0f, 10.0f);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHT RESPONSE
//------------------------------------------------------------------------------------------------------------------------

bool AtmosphereInspector::ConstructLightResponse(ControlColumn &Page)
{
    const EditorTypefaces &Faces     = Page.QueryFaces();
    const float            S         = Page.QueryScale();
    bool                   Changed   = false;
    const float            Elevation = InspectorRuntime::Instance().QuerySolarElevation();
    const auto             Through   = ResolveSunTransmittance(Elevation, Parameters);

    // ── TRANSMISSION (Light through air) ───────────────────────────────────────────────────────────────────────────
    {
        CardLook Look      = ResolveCardLook();
        Look.Fill          = ResolveCssColour(0x20251EFF);
        Look.Glow          = ResolveCssColour(0x7589530D);
        Look.GlowCentre    = ImVec2(0.5f, 0.35f);
        Look.GlowExtent    = 0.65f;
        Look.Border        = ResolveCssColour(0xC9D5B414);
        Look.Radius        = 23.0f;
        Look.PaddingTop    = 17.0f;
        Look.PaddingSide   = 16.0f;
        Look.PaddingBottom = 8.0f;
        Look.MarginBottom  = 2.0f;
        Page.BeginCard(Look, nullptr, nullptr);
        ConstructSectionHead(Page, "Light through air", "TRANSMISSION", false, 11.0f);
        const char *Status = !Parameters.Enabled   ? "Atmosphere off \xC2\xB7 reference only"
                             : !Parameters.AirMass ? "Attenuation bypassed \xC2\xB7 reference only"
                                                   : "Direct sunlight \xC2\xB7 linked";
        ConstructNote(Page, Status, 9.0f, ResolveCssColour(0x8D9C7DFF), 0.0f, 3.0f, 1.25f);

        Page.PushIndent(-8.0f, -8.0f); // .at-transmission-dial { margin: 8px -8px 0 }
        const DiagramPen Pen = DiagramCanvas::Begin(Page, "##TransmissionDial", 640.0f, 330.0f, 8.0f, 0.0f, nullptr,
                                                    "RGB transmission sweeps \xC2\xB7 zero to one hundred percent");
        const ImVec2     Centre(320.0f, 250.0f);
        auto             Arc = [&](float Radius, float Fraction)
        {
            std::vector<ImVec2> Points;
            for (int Index = 0; Index <= 96; ++Index)
            {
                const float Angle = AtmospherePi + static_cast<float>(Index) / 96.0f * AtmospherePi * Fraction;
                Points.push_back(ImVec2(Centre.x + std::cos(Angle) * Radius, Centre.y + std::sin(Angle) * Radius));
            }
            return Points;
        };
        constexpr std::array<uint32_t, 3> Sweeps = {0xD2A49BFF, 0xB6CC9CFF, 0xA4C6D7FF};
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const float Radius = 209.0f - static_cast<float>(Channel) * 31.0f;
            Pen.Path(Arc(Radius, 1.0f), ResolveCssColour(0xC5CFB913), 8.0f);
            if (Through[Channel] > 0.0f)
            {
                Pen.Path(Arc(Radius, Through[Channel]), ResolveCssColour(Sweeps[Channel]), 8.0f);
                const float Angle = AtmospherePi + Through[Channel] * AtmospherePi;
                Pen.Circle(ImVec2(Centre.x + std::cos(Angle) * Radius, Centre.y + std::sin(Angle) * Radius), 5.0f,
                           ResolveCssColour(Sweeps[Channel]));
            }
        }
        Pen.Text(ImVec2(96.0f, 290.0f), "0%", ResolveCssColour(0x7F8C73FF), 20.0f);
        Pen.Text(ImVec2(544.0f, 290.0f), "100%", ResolveCssColour(0x7F8C73FF), 20.0f);
        // .at-neutral-readout: centred at 45 % of the dial.
        const float       Neutral = Through[0] * 0.2126f + Through[1] * 0.7152f + Through[2] * 0.0722f;
        const std::string Value   = ResolveFigure("%.1f%%", static_cast<double>(Neutral * 100.0f));
        const ImVec2      DialTop = Pen.QueryScreen(ImVec2(0.0f, 0.0f));
        const float       DialH   = Pen.QueryScreen(ImVec2(0.0f, 330.0f)).y - DialTop.y;
        const float       Middle  = DialTop.x + 0.5f * Page.QueryWidth();
        const float       ReadTop = DialTop.y + 0.45f * DialH;
        const float       ValueW  = Measure(Page, Faces.Light, 38.0f, Value.c_str(), -1.8f);
        Write(Page, Faces.Light, 38.0f, ImVec2(Middle - 0.5f * ValueW, GlyphRun::ResolveBaseline(ReadTop, 38.0f * S, 1.05f)),
              ResolveCssColour(0xE7EDDCFF), Value.c_str(), -1.8f);
        const float LabelW = Measure(Page, Faces.Regular, 9.0f, "neutral-light transmission");
        Write(Page, Faces.Regular, 9.0f,
              ImVec2(Middle - 0.5f * LabelW, GlyphRun::ResolveBaseline(ReadTop + (38.0f * 1.05f + 5.0f) * S, 9.0f * S, 1.25f)),
              ResolveCssColour(0x9EAC90FF), "neutral-light transmission");
        Page.PopIndent();

        // .at-output-strip: three columns, border-top/bottom #d6e3a610, padding 12 0.
        {
            const ImVec2      Strip = Page.Space(12.0f + 9.0f * 1.25f + 6.0f + 20.0f * 1.25f + 12.0f, 6.0f, 12.0f);
            ImDrawList *const Ink   = Page.QueryDrawList();
            const float       Width = Page.QueryWidth();
            const float       Tall  = (12.0f + 9.0f * 1.25f + 6.0f + 20.0f * 1.25f + 12.0f) * S;
            Ink->AddRectFilled(Strip, ImVec2(Strip.x + Width, Strip.y + S), ResolveCssColour(0xD6E3A610));
            Ink->AddRectFilled(ImVec2(Strip.x, Strip.y + Tall - S), ImVec2(Strip.x + Width, Strip.y + Tall), ResolveCssColour(0xD6E3A610));
            constexpr std::array<const char *, 3> Names = {"Red", "Green", "Blue"};
            for (size_t Channel = 0; Channel < 3; ++Channel)
            {
                const float Column = Strip.x + Width * static_cast<float>(Channel) / 3.0f + (Channel > 0 ? 14.0f * S : 0.0f);
                Write(Page, Faces.Regular, 9.0f, ImVec2(Column, GlyphRun::ResolveBaseline(Strip.y + 12.0f * S, 9.0f * S, 1.25f)),
                      ResolveCssColour(0xACB79EFF), Names[Channel]);
                const std::string Figure = ResolveFigure("%.1f%%", static_cast<double>(Through[Channel] * 100.0f));
                Write(Page, Faces.Light, 20.0f,
                      ImVec2(Column, GlyphRun::ResolveBaseline(Strip.y + (12.0f + 9.0f * 1.25f + 6.0f) * S, 20.0f * S, 1.25f)),
                      ResolveCssColour(Sweeps[Channel]), Figure.c_str(), -0.7f);
                if (Channel < 2)
                {
                    const float Divider = Strip.x + Width * static_cast<float>(Channel + 1) / 3.0f;
                    Ink->AddRectFilled(ImVec2(Divider - S, Strip.y + 12.0f * S), ImVec2(Divider, Strip.y + Tall - 12.0f * S),
                                       ResolveCssColour(0xFFFFFF12));
                }
            }
        }
        // .at-transmission-context
        {
            const ImVec2      Row  = Page.Space(9.0f * 1.25f + 8.0f, 0.0f, 0.0f);
            const std::string Sun  = "Sun / " + ResolveFigure("%.1f", static_cast<double>(Elevation)) + "\xC2\xB0";
            const float       Line = GlyphRun::ResolveBaseline(Row.y, 9.0f * S, 1.25f);
            Write(Page, Faces.Regular, 9.0f, ImVec2(Row.x, Line), ResolveCssColour(0x9CA98DFF), Sun.c_str());
            const float ObserverW = Measure(Page, Faces.Regular, 9.0f, "Observer / 100 m");
            Write(Page, Faces.Regular, 9.0f, ImVec2(Row.x + Page.QueryWidth() - ObserverW, Line), ResolveCssColour(0x9CA98DFF),
                  "Observer / 100 m");
        }
        ConstructNote(
            Page,
            "Isolated atmosphere, neutral RGB source. Centre value is luminance-weighted transmission; excludes clouds and source "
            "tint.",
            9.0f, ResolveCssColour(0x89987AFF), 0.0f, 12.0f);
        {
            const ImVec2 Rule = Page.Space(1.0f, 0.0f, 0.0f);
            Page.QueryDrawList()->AddRectFilled(Rule, ImVec2(Rule.x + Page.QueryWidth(), Rule.y + S), ResolveCssColour(0xD6E3A610));
        }
        Changed |= ConstructSwitchRow(Page, "##AirMass", "Apply air-mass attenuation", Parameters.AirMass, true, nullptr, false);
        bool Multiple = Parameters.MultiScatteringLut;
        if (ConstructSwitchRow(Page, "##MultipleScattering", "Multiple scattering", Multiple, true, nullptr, false))
        {
            Parameters.MultiScatteringLut = Multiple;
            Changed                       = true;
        }
        Page.EndCard();
    }

    // ── LINEAR RGB TINTS (Sky & surface) ───────────────────────────────────────────────────────────────────────────
    (void)Page.Space(0.0f, 22.0f, 0.0f);
    ConstructSectionHead(Page, "Sky & surface", "LINEAR RGB TINTS");
    {
        const float  Gap   = 10.0f * S;
        const float  WellW = 0.5f * (Page.QueryWidth() - Gap);
        const float  Tall  = 16.0f + 45.0f + 12.0f + 11.0f * 1.25f + 6.0f + 9.0f * 1.25f + 6.0f + 9.0f + 10.0f * 1.25f + 16.0f;
        const ImVec2 Row   = Page.Space(Tall, 19.0f - 16.0f, 17.0f);
        Changed |= ConstructColourWell(Page, "SkyTint", Row, WellW, "Sky Tint", "Scattered sky", Parameters.SkyTint, false, SkyPending);
        Changed |= ConstructColourWell(Page, "GroundAlbedo", ImVec2(Row.x + WellW + Gap, Row.y), WellW, "Ground Albedo", "Ground bounce",
                                       Parameters.GroundAlbedo, true, nullptr);
    }
    Changed |= SliderRow::Construct(Page, "SkyScale", "Sky brightness", Parameters.SkyScale, 0.0f, ShapingMaximum, 0.01f, "\xC3\x97", 1.0f,
                                    false, SkyPending);
    ConstructNote(Page,
                  "Ground albedo feeds the existing multiple-scattering closure; these wells are source colours, not a camera preview.",
                  9.0f, Page.QueryTheme().Caption, 6.0f, 8.0f);

    // ── Horizon shaping (details drawer) ───────────────────────────────────────────────────────────────────────────
    {
        const ImVec2 Rule = Page.Space(1.0f, 12.0f, 0.0f);
        Page.QueryDrawList()->AddRectFilled(Rule, ImVec2(Rule.x + Page.QueryWidth(), Rule.y + S), ResolveCssColour(0xD6E3A61A));
        const ImVec2 Summary = Page.Space(16.0f + 12.0f * 1.25f + 16.0f, 0.0f, 0.0f);
        ImGui::SetCursorScreenPos(Summary);
        if (ImGui::InvisibleButton("##HorizonDrawer", ImVec2(Page.QueryWidth(), (16.0f + 12.0f * 1.25f + 16.0f) * S)))
            HorizonOpen = !HorizonOpen;
        Write(Page, Faces.Regular, 12.0f, ImVec2(Summary.x, GlyphRun::ResolveBaseline(Summary.y + 16.0f * S, 12.0f * S, 1.25f)),
              HorizonOpen ? ResolveCssColour(0xE1EBC7FF) : ResolveCssColour(0xB8C5A6FF), "Horizon shaping  \xE2\x86\x97");
        if (HorizonOpen)
        {
            Changed |=
                SliderRow::Construct(Page, "HorizonGlow", "Horizon glow", Parameters.HorizonGlow, 0.0f, ShapingMaximum, 0.01f, "\xC3\x97");
            Changed |= SliderRow::Construct(Page, "WhiteLine", "White line", Parameters.WhiteLine, 0.0f, ShapingMaximum, 0.01f, "\xC3\x97",
                                            1.0f, false, SkyPending);
            Changed |= ToggleRow::Construct(Page, "CivilOnly", "Civil twilight only", Parameters.LineAtCivilOnly, false, SkyPending);
        }
        const ImVec2 Close = Page.Space(1.0f, 3.0f, 0.0f);
        Page.QueryDrawList()->AddRectFilled(Close, ImVec2(Close.x + Page.QueryWidth(), Close.y + S), ResolveCssColour(0xD6E3A61A));
    }

    // ── GPU LOOKUP QUALITY (Integration) ───────────────────────────────────────────────────────────────────────────
    (void)Page.Space(0.0f, 20.0f, 0.0f);
    ConstructSectionHead(Page, "Integration", "GPU LOOKUP QUALITY", false, 8.0f);
    Changed |= SliderRow::ConstructInteger(Page, "TierSamples", "Transmittance bake samples", Parameters.TierSamples, 8u, 64u, "samples");
    (void)Page.Space(0.0f, 0.0f, 6.0f);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BAKING
//------------------------------------------------------------------------------------------------------------------------

void AtmosphereInspector::RefreshBakedImages()
{
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    if (Access.Atmosphere != &Parameters || !Access.Rendering || !Access.Rendering->AtmosphereBake) return;
    const auto &Bake = *Access.Rendering->AtmosphereBake;
    if (!Bake.Ready) return;
    if (UploadedGeneration == Bake.Generation && UploadedSource == Bake.SourceRecord && SkyPlate.QueryReady()) return;
    bool Ready = SkyPlate.Configure(192, 108, ResolveLookupPixels(Bake.SkyView, false));
    Ready &= TransmittancePlate.Configure(256, 64, ResolveLookupPixels(Bake.Transmittance, true));
    Ready &= MultiplePlate.Configure(32, 32, ResolveLookupPixels(Bake.MultipleScattering, false));
    for (int View = 0; View < 4; ++View)
        Ready &= BakeViews[View].Configure(320, 160, ProjectAtmosphereLookup(Bake, View));
    if (Ready)
    {
        UploadedGeneration = Bake.Generation;
        UploadedSource     = Bake.SourceRecord;
    }
}

bool AtmosphereInspector::ConstructBaking(ControlColumn &Page)
{
    const auto &Access  = InspectorRuntime::Instance().QueryEnvironmentAccess();
    const auto  Bake    = Access.Rendering ? Access.Rendering->AtmosphereBake : nullptr;
    const bool  Active  = Access.Atmosphere == &Parameters && Access.Rendering && Access.Rendering->Ready;
    const bool  Pending = Parameters.BakeRequested != Parameters.BakeAcknowledged;
    bool        Changed = false;
    Page.BeginCard(ResolveBakeLook(), "Atmosphere bake", nullptr);
    Changed |= ToggleRow::Construct(Page, "UseBaked", "Use baked settings", Parameters.UseBaked, Parameters.BakedAvailable,
                                    "Bake once to capture the medium used by the viewport. The Sun remains dynamic.");
    if (NextButton::Construct(Page, "##BakeAtmosphere", Pending ? "Bake again / replace pending request" : "Bake atmosphere",
                              Active && Parameters.Enabled, "Requires this atmosphere to be the active record of a live viewport."))
    {
        ++Parameters.BakeRequested;
        Parameters.UseBaked = true;
        Changed             = true;
    }
    const char *Status = !Active   ? "Viewport unavailable · no GPU bake can be submitted."
                         : Pending ? "Bake requested · waiting for successful GPU execution and readback."
                         : Parameters.UseBaked && Parameters.BakedAvailable
                             ? "Using the baked medium snapshot. Authoring edits do not replace it until Bake is pressed again."
                             : "Live mode · native lookups rebuild only when their inputs change.";
    CaptionText::Construct(Page, Status);
    if (Parameters.UseBaked && Parameters.BakedAvailable &&
        static_cast<const AtmosphereOpticalConfiguration &>(Parameters) != Parameters.Baked)
        CaptionText::Construct(Page, "Authored optics changed · the displayed/used snapshot is unchanged until you bake again.");
    if (Bake) CaptionText::Construct(Page, Bake->Diagnostic.c_str());
    CaptionText::Construct(Page, "Baked coefficients are saved with the level. GPU images are rebuilt from those coefficients on load; "
                                 "sky-view lighting continues to follow the Sun.");
    Page.EndCard();
    Page.BeginCard(ResolveBakeLook(), "Baked atmosphere", nullptr);
    constexpr const char *Views[] = {"Air", "Sky", "Transmit", "Sphere"};
    TabStrip::Construct(Page, "Baked atmosphere views", Views, 4, BakedView);
    const auto Pen = DiagramCanvas::Begin(Page, "##BakedPreview", 640, 320, 8, 0);
    Pen.Rect({0, 0}, {640, 320}, ResolveCssColour(0x141913FF), 12);
    if (BakeViews[BakedView].QueryReady())
        BakeViews[BakedView].Construct(Pen.QueryDrawList(), Pen.QueryScreen({0, 0}), Pen.QueryScreen({640, 320}), IM_COL32_WHITE,
                                       12 * Page.QueryScale());
    else
        Pen.Text({320, 168}, "No completed GPU lookup readback", ResolveCssColour(0x8C9A7DFF), 18);
    CaptionText::Construct(Page, "Actual GPU tables. Air is the native sky atlas; Sky and Sphere reproject it; Transmit displays spectral "
                                 "transmission. Not a scene-camera capture.");
    Page.EndCard();
    if (Bake && Bake->Ready && Active)
    {
        Page.BeginCard(ResolveBakeLook(), "Inputs used by the displayed bake", nullptr);
        const auto &Inputs = Bake->Inputs;
        CaptionText::Construct(
            Page,
            ("Readback generation " + std::to_string(Bake->Generation) + " · request " + std::to_string(Bake->RequestSerial)).c_str());
        auto Reading = [&](const char *Name, double Quantity, const char *Unit)
        { CaptionText::Construct(Page, (std::string(Name) + "  " + ResolveFigure("%.3f", Quantity) + " " + Unit).c_str(), 4, 4); };
        Reading("Rayleigh", Inputs.RayleighScattering, "×");
        Reading("Mie turbidity", Inputs.MieTurbidity, "");
        Reading("Mie anisotropy", Inputs.MieAnisotropyG, "g");
        Reading("Ozone", Inputs.OzoneAbsorptionScale, "×");
        Reading("Rayleigh height", Inputs.RayleighHeightMetres, "m");
        Reading("Mie height", Inputs.MieHeightMetres, "m");
        Reading("Observer altitude", Bake->ObserverAltitudeKm, "km");
        Reading("Planet radius", Inputs.PlanetRadiusKm, "km");
        Reading("Atmosphere depth", Inputs.AtmosphereThicknessKm, "km");
        Reading("Ground albedo R", Inputs.GroundAlbedo[0], "");
        Reading("Ground albedo G", Inputs.GroundAlbedo[1], "");
        Reading("Ground albedo B", Inputs.GroundAlbedo[2], "");
        Reading("Horizon gain", Inputs.HorizonGlow, "×");
        CaptionText::Construct(Page, Inputs.MultiScatteringLut ? "Multiple scattering: enabled" : "Multiple scattering: disabled");
        Reading("Transmittance samples", Inputs.TierSamples, "");
        Page.EndCard();
    }
    if (DisclosureRow::Construct(Page, "##LookupTables", "GPU lookup tables", LookupsOpen))
    {
        for (int Index = 0; Index < 2; ++Index)
        {
            const char *Title = Index == 0 ? "Transmittance · 256 × 64" : "Multiple scattering · 32 × 32";
            CaptionText::Construct(Page, Title);
            const auto Preview = DiagramCanvas::Begin(Page, Title, 640, 200, 0, 8);
            auto      &Plate   = Index == 0 ? TransmittancePlate : MultiplePlate;
            if (Plate.QueryReady())
                Plate.Construct(Preview.QueryDrawList(), Preview.QueryScreen({0, 0}), Preview.QueryScreen({640, 200}));
            else
                Preview.Text({320, 108}, "No completed GPU readback", ResolveCssColour(0x8C9A7DFF), 18);
        }
    }
    return Changed;
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void AtmosphereInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void AtmosphereInspector::ConstructInspector() {}
bool AtmosphereInspector::ConstructOverview(ControlColumn &)
{
    return false;
}
bool AtmosphereInspector::ConstructMedium(ControlColumn &)
{
    return false;
}
bool AtmosphereInspector::ConstructLightResponse(ControlColumn &)
{
    return false;
}
bool AtmosphereInspector::ConstructBaking(ControlColumn &)
{
    return false;
}
} // namespace Frontier

#endif
