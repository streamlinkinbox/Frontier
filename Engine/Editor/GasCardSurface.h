//==============================================================================================================================================
//                                                           GASCARDSURFACE.H
//==============================================================================================================================================
// 📦 The native gas domain inspector — the HTML card stack ported chrome for chrome.
//
//    Reference: Experimental/ProjectZeroEditor/GasPanel.jsx and GasSpecification.js, drawn by the editor's
//    shared Control / Card / Tile / Metric / TransformPanel. Every measurement below is off Editor.css
//    (.property-card, .field, .slider-pill, .split-value, .metric, .quick-tile, .switch-row, .toggle,
//    .section-caption, .generic-card, input[type=range]), MaterialPanel.css (.transform-card,
//    .transform-table) and GasPanel.css (.gas-visual, .gas-state, .gas-note, the child list, the buttons).
//
// 🔴 THE PORT IS OF THE SECOND DESIGN, NOT THE FIRST.
//    The browser card was rebuilt onto the editor's own primitives before this was written, precisely so
//    that this file could be too. A gas slider here is the same pill as a fog slider here — same 30 px
//    row, same 92 px split value, same 26 px track with a 24 px thumb — because both are one painter,
//    not two that resemble each other. Any constant below that is shared with a fog or wind card is
//    shared because the stylesheet shares it.
//
// 📐 THREE THINGS THIS CARD SAYS OUT LOUD, AND THEY ARE THE REASON IT IS NOT JUST A LIST OF SLIDERS.
//    ① Bounds occupy the transform's third row, where a mesh has Scale: a domain's extent IS its scale, in
//       metres, and is not a child entity.
//    ② Rotation orients the children and the authored framing, NOT the lattice. CoarseGasField is an
//       axis-aligned cube, so the caption under the transform admits it rather than letting someone spend a
//       minute on a rotation that the solver throws away.
//    ③ Colliders are not children. They opt in from their own inspector, so deleting the domain cannot
//       delete the crate.
//
// This is the UI. It draws from a Subject the host fills; it does not advance a solver, and the preview
//    panel paints the same cheap stand-in the browser paints — smoke that scatters, heat that emits — and
//    refuses to paint anything at all for a domain the game would not be running.

#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Frontier::GasCards
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     PALETTE AND METRICS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 PageFill     = IM_COL32(  9,   9,   9, 255);   // [-] the inspector column behind the cards
constexpr ImU32 CardTop      = IM_COL32( 37,  37,  37, 255);   // [-] .property-card gradient 135deg start #252525
constexpr ImU32 CardFoot     = IM_COL32( 32,  32,  32, 255);   // [-] .property-card gradient end #202020
constexpr ImU32 CardEdge     = IM_COL32( 52,  52,  52, 255);   // [-] .property-card border #343434
constexpr ImU32 CardTitle    = IM_COL32(202, 202, 202, 255);   // [-] .property-card h3 #cacaca
constexpr ImU32 PlainFill    = IM_COL32( 26,  26,  26, 255);   // [-] .generic-card background #1a1a1a
constexpr ImU32 PlainEdge    = IM_COL32(255, 255, 255,  13);   // [-] .generic-card border #ffffff0d
constexpr ImU32 SummaryInk   = IM_COL32(144, 144, 144, 255);   // [-] .generic-card summary #909090
constexpr ImU32 FieldInk     = IM_COL32(145, 145, 145, 255);   // [-] .field > span #919191
constexpr ImU32 BodyInk      = IM_COL32(240, 240, 240, 255);   // [-] body colour #f0f0f0
constexpr ImU32 CaptionInk   = IM_COL32(153, 153, 153, 255);   // [-] .section-caption #999
constexpr ImU32 NoteInk      = IM_COL32(126, 140, 146, 255);   // [-] .gas-note #7e8c92
constexpr ImU32 TrackHigh    = IM_COL32( 69,  69,  69, 255);   // [-] range filled #454545
constexpr ImU32 TrackLow     = IM_COL32( 36,  36,  36, 255);   // [-] range remainder #242424
constexpr ImU32 ThumbFill    = IM_COL32(224, 224, 224, 255);   // [-] range thumb #e0e0e0
constexpr ImU32 SplitEdge    = IM_COL32(255, 255, 255,  13);   // [-] .split-value border #ffffff0d
constexpr ImU32 SplitFill    = IM_COL32(  0,   0,   0, 255);   // [-] .split-value input background #000
constexpr ImU32 UnitFill     = IM_COL32( 26,  26,  26, 255);   // [-] .split-value small background #1a1a1a
constexpr ImU32 UnitInk      = IM_COL32( 92,  92,  92, 255);   // [-] .split-value small colour #5c5c5c
constexpr ImU32 SwitchInk    = IM_COL32(170, 170, 170, 255);   // [-] .switch-row #aaa
constexpr ImU32 SwitchOff    = IM_COL32( 60,  60,  60, 255);   // [-] .toggle #3c3c3c
constexpr ImU32 SwitchOn     = IM_COL32( 52, 199,  89, 255);   // [-] .toggle.on #34c759
constexpr ImU32 Knob         = IM_COL32(238, 238, 238, 255);   // [-] .toggle i #eee
constexpr ImU32 SelectFill   = IM_COL32(  0,   0,   0, 255);   // [-] select background #000
constexpr ImU32 SelectEdge   = IM_COL32(255, 255, 255,  13);   // [-] select border #ffffff0d
constexpr ImU32 SelectInk    = IM_COL32(204, 204, 204, 255);   // [-] select colour #ccc
constexpr ImU32 MetricInk    = IM_COL32(233, 233, 233, 255);   // [-] .metric #e9e9e9
constexpr ImU32 MetricUnit   = IM_COL32(153, 153, 153, 255);   // [-] .metric small #999
constexpr ImU32 AmberInk     = IM_COL32(224, 180, 115, 255);   // [-] .gas-amber .metric #e0b473
constexpr ImU32 TileEdge     = IM_COL32(255, 255, 255,  18);   // [-] .quick-tile border #ffffff12
constexpr ImU32 TileFill     = IM_COL32(255, 255, 255,   6);   // [-] .quick-tile gradient 145deg #ffffff06
constexpr ImU32 TileInk      = IM_COL32(209, 209, 209, 255);   // [-] .quick-tile colour #d1d1d1
constexpr ImU32 TileOnInk    = IM_COL32( 91, 219, 135, 255);   // [-] --quick-ink on #5bdb87
constexpr ImU32 TileOffInk   = IM_COL32(225, 148, 131, 255);   // [-] --quick-ink off #e19483
constexpr ImU32 TransTitle   = IM_COL32(221, 221, 221, 255);   // [-] .transform-card h3 #ddd
constexpr ImU32 SpaceInk     = IM_COL32(119, 119, 119, 255);   // [-] .transform-card header > span #777
constexpr ImU32 RowInk       = IM_COL32(204, 204, 204, 255);   // [-] .transform-table tbody th #ccc
constexpr ImU32 RowUnitInk   = IM_COL32(112, 112, 112, 255);   // [-] .transform-table th small #707070
constexpr ImU32 AxisX        = IM_COL32(207, 154, 147, 255);   // [-] .axis-X #cf9a93
constexpr ImU32 AxisY        = IM_COL32(158, 195, 166, 255);   // [-] .axis-Y #9ec3a6
constexpr ImU32 AxisZ        = IM_COL32(145, 170, 206, 255);   // [-] .axis-Z #91aace
constexpr ImU32 NumberFill   = IM_COL32( 21,  21,  21, 255);   // [-] .transform-table input #151515
constexpr ImU32 NumberEdge   = IM_COL32(255, 255, 255,   9);   // [-] its border #ffffff09
constexpr ImU32 CanvasEdge   = IM_COL32(255, 255, 255,  18);   // [-] .gas-visual canvas border #ffffff12
constexpr ImU32 LiveInk      = IM_COL32(111, 208, 140, 255);   // [-] .gas-state.live i #6fd08c
constexpr ImU32 IdleInk      = IM_COL32( 90, 103, 109, 255);   // [-] .gas-state.idle i #5a676d
constexpr ImU32 StateInk     = IM_COL32(146, 169, 159, 255);   // [-] .gas-state #92a99f
constexpr ImU32 ChildFill    = IM_COL32(255, 255, 255,   6);   // [-] .gas-child-list button #ffffff06
constexpr ImU32 ChildEdge    = IM_COL32(255, 255, 255,  16);   // [-] its border #ffffff10
constexpr ImU32 ChildInk     = IM_COL32(207, 216, 212, 255);   // [-] its colour #cfd8d4
constexpr ImU32 ActionFill   = IM_COL32(255, 255, 255,   8);   // [-] .gas-add-child background #ffffff08
constexpr ImU32 ActionEdge   = IM_COL32(255, 255, 255,  20);   // [-] its border #ffffff14
constexpr ImU32 ActionInk    = IM_COL32(212, 222, 217, 255);   // [-] its colour #d4ded9
constexpr ImU32 OpenFill     = IM_COL32(255, 255, 255,  14);   // [-] .gas-open background #ffffff0e
constexpr ImU32 OpenEdge     = IM_COL32(255, 255, 255,  34);   // [-] its border #ffffff22

constexpr float CardPadX     = 24.0f;    // [px] .property-card padding 23px 24px 22px
constexpr float CardPadTop   = 23.0f;
constexpr float CardPadFoot  = 22.0f;
constexpr float CardRound    = 22.0f;    // [px] .property-card border-radius
constexpr float CardGap      = 16.0f;    // [px] .property-card margin-bottom
constexpr float PlainPadX    = 14.0f;    // [px] .generic-card padding 18px 14px
constexpr float PlainPadY    = 18.0f;
constexpr float PlainGap     = 12.0f;    // [px] .generic-card margin-bottom
constexpr float TitleSize    = 12.0f;    // [px] .property-card h3 font-size
constexpr float TitleFoot    = 28.0f;    // [px] its margin-bottom
constexpr float LabelSize    = 11.0f;    // [px] .field > span
constexpr float LabelFoot    =  8.0f;    // [px] its margin-bottom
constexpr float FieldGap     = 20.0f;    // [px] .field margin 20px 0 — collapsed between siblings
constexpr float PillTall     = 30.0f;    // [px] .slider-pill height
constexpr float PillGap      = 10.0f;    // [px] .slider-pill gap
constexpr float SplitWide    = 92.0f;    // [px] .split-value width
constexpr float SplitTall    = 28.0f;    // [px] .split-value height
constexpr float SplitNumber  = 58.0f;    // [px] .split-value input width
constexpr float TrackTall    = 26.0f;    // [px] input[type=range] height
constexpr float ThumbWide    = 24.0f;    // [px] its thumb
constexpr float SwitchTall   = 24.0f;    // [px] .toggle height
constexpr float SwitchWide   = 44.0f;    // [px] .toggle width
constexpr float SelectTall   = 34.0f;    // [px] select, Editor.css button/select sizing
constexpr float CaptionSize  = 10.0f;    // [px] .section-caption
constexpr float NoteSize     = 10.0f;    // [px] .gas-note font-size
constexpr float NoteLead     =  1.5f;    // [-]  its line-height
constexpr float MetricSize   = 30.0f;    // [px] .metric font-size
constexpr float MetricFoot   = 14.0f;    // [px] its margin-bottom
constexpr float TileTall     = 108.0f;   // [px] .quick-tile height
constexpr float TileGap      =  8.0f;    // [px] .tiles gap
constexpr float CanvasTall   = 170.0f;   // [px] .gas-visual canvas height
constexpr float RowTall      = 29.0f;    // [px] .transform-table input height
constexpr float RowPad       =  5.0f;    // [px] .transform-table th/td padding 5px 2px
constexpr float ChildTall    = 30.0f;    // [px] .gas-child-list button, 7px padding around 11px text
constexpr float ActionTall   = 33.0f;    // [px] .gas-add-child, 9px padding around 11px text
constexpr float OpenTall     = 52.0f;    // [px] .gas-open, two lines at 12px over 10px

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SHEET
//------------------------------------------------------------------------------------------------------------------------

// 📝 The same descriptor shape NativePanels.json uses and GasSpecification.js writes, which is exactly why
//    one painter draws a gas field and a fog field. Key is the simulator's own parameter name, so a reading
//    here and a reading in the Fluid editor are one reading rather than two that must be kept level.
enum class GasControl : uint32_t
{
    Slider = 0u,
    Switch = 1u,
    Select = 2u,
};

struct GasField
{
    const char* Label    = "";                      // [-] what the row says
    const char* Group    = "";                      // [-] which card it lands in
    const char* Key      = "";                      // [-] the simulator's parameter name
    GasControl  Control  = GasControl::Slider;      // [-] which painter
    float       Minimum  = 0.0f;
    float       Maximum  = 1.0f;
    float       Default  = 0.0f;
    uint32_t    Decimals = 2u;
    const char* Unit     = "";
};

// Eleven inline readings, in card order. The lattice is deliberately absent: the quality ladder owns the
//    extent at runtime, and a control the budget overrules every frame is a lie with a handle on it.
inline const GasField* GasSheet(uint32_t& Count) noexcept
{
    static const GasField Fields[] = {
        { "Dynamic Bounds", "Domain",     "dynamicBounds",      GasControl::Switch, 0.0f,  1.0f,  1.0f,  0u, ""    },
        { "Surge Limit",    "Domain",     "dynamicBoundsMax",   GasControl::Slider, 1.0f,  3.0f,  1.55f, 2u, "x"   },
        { "Enclosed Box",   "Domain",     "enclosedBox",        GasControl::Switch, 0.0f,  1.0f,  0.0f,  0u, ""    },
        { "Emission Rate",  "Source",     "emitterRate",        GasControl::Slider, 0.0f,  3.0f,  1.0f,  2u, "x"   },
        { "Smoke",          "Source",     "emitterSmoke",       GasControl::Slider, 0.0f,  8.0f,  1.4f,  2u, ""    },
        { "Temperature",    "Source",     "emitterTemperature", GasControl::Slider, 0.0f, 12.0f,  3.5f,  2u, "K"   },
        { "Buoyancy",       "Source",     "buoyancy",           GasControl::Slider, -4.0f,16.0f,  5.8f,  2u, "m/s2"},
        { "Density",        "Appearance", "densityExtinction",  GasControl::Slider, 0.0f, 60.0f, 18.5f,  1u, "1/m" },
        { "Albedo",         "Appearance", "smokeAlbedo",        GasControl::Slider, 0.0f,  1.0f,  0.22f, 2u, ""    },
        { "Fire",           "Appearance", "fireIntensity",      GasControl::Slider, 0.0f, 20.0f,  5.6f,  2u, "x"   },
        { "Exposure",       "Appearance", "exposure",           GasControl::Slider, 0.1f,  4.0f,  1.25f, 2u, "x"   },
    };
    Count = uint32_t(sizeof(Fields) / sizeof(Fields[0]));
    return Fields;
}

// An emitter's own seven. ② It is a child entity with a transform, not a fourth card on the domain.
inline const GasField* GasEmitterSheet(uint32_t& Count) noexcept
{
    static const GasField Fields[] = {
        { "Radius",        "Emission", "emitterRadius",          GasControl::Slider, 0.01f,  1.0f, 0.1f, 3u, ""    },
        { "Emission Rate", "Emission", "emitterRate",            GasControl::Slider, 0.0f,   3.0f, 1.0f, 2u, "x"   },
        { "Smoke",         "Emission", "emitterSmoke",           GasControl::Slider, 0.0f,   8.0f, 1.4f, 2u, ""    },
        { "Temperature",   "Emission", "emitterTemperature",     GasControl::Slider, 0.0f,  12.0f, 3.5f, 2u, "K"   },
        { "Fuel",          "Emission", "emitterFuel",            GasControl::Slider, 0.0f,  12.0f, 2.8f, 2u, ""    },
        { "Rise Speed",    "Emission", "emitterUpwardVelocity",  GasControl::Slider, 0.0f,  12.0f, 3.2f, 2u, "m/s" },
        { "Swirl",         "Emission", "emitterSwirl",           GasControl::Slider, 0.0f,   6.0f, 1.2f, 2u, ""    },
    };
    Count = uint32_t(sizeof(Fields) / sizeof(Fields[0]));
    return Fields;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SUBJECT
//------------------------------------------------------------------------------------------------------------------------

struct GasChild
{
    const char* Name    = "Emitter";
    char        Summary[48] = "1.00x - 3.5 K";
    bool        Enabled = true;
};

struct GasCardSubject
{
    const char* Name        = "Gas Domain";
    const char* PresetName  = "Camp fire";
    bool        Emitter     = false;             // ③ an emitter opens ParticleEditor, a domain FluidEditor
    bool        Hidden      = false;
    bool        Obstructs   = true;
    bool        Coupled     = false;             // 🔴 two-way coupling off by default, as the header has it
    float       Position[3] = { 0.0f, 0.0f, 0.0f };
    float       Rotation[3] = { 0.0f, 0.0f, 0.0f };
    float       Bounds[3]   = { 1.85f, 2.1f, 1.85f };   // ① the transform's third row, in metres
    uint32_t    Policy      = 1u;                // [-] GasRunPolicy
    float       Lifetime    = 4.0f;
    bool        Retire      = true;              // 🔴 a one-shot hands its fields back
    bool        Fired       = false;             // [-] is it simulating right now, in the editor
    float       Distance    = 12.0f;
    const char* QualityPin  = "Automatic - by distance";
    const char* TierReadout = "Near tier - 96 cubed lattice - 18 sweeps - 60 Hz";
    char        Cost[16]    = "40 MB";
    bool        OverAmber   = false;
    float       Readings[16] = { 1.0f, 1.55f, 0.0f, 1.0f, 1.4f, 3.5f, 5.8f, 18.5f, 0.22f, 5.6f, 1.25f };
    GasChild    Children[4];
    uint32_t    ChildCount  = 0u;
    uint32_t    Departures  = 0u;                // [-] readings moved off the preset
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      TEXT HELPERS
//------------------------------------------------------------------------------------------------------------------------

inline float Baseline(float BoxTop, float BoxTall, float Size) noexcept
{
    return BoxTop + (BoxTall - Size) * 0.5f;
}

inline void Spell(char* Out, size_t Room, float Reading, uint32_t Decimals) noexcept
{
    std::snprintf(Out, Room, "%.*f", int(Decimals), double(Reading));
}

inline int NoteLines(ImFont* Light, float Wide, const char* Body) noexcept
{
    if (Body == nullptr || *Body == '\0') return 0;
    const ImVec2 Measured = Light->CalcTextSizeA(NoteSize, Wide, Wide, Body);
    return std::max(1, int(std::lround(Measured.y / (NoteSize * NoteLead))));
}

inline float NoteHeight(ImFont* Light, float Wide, const char* Body) noexcept
{
    return float(NoteLines(Light, Wide, Body)) * NoteSize * NoteLead;
}

inline void PaintNote(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Body) noexcept
{
    if (Body == nullptr || *Body == '\0') return;
    Draw->AddText(Light, NoteSize, Spot, NoteInk, Body, nullptr, Wide);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// One field row: the label, then a 30 px pill holding the track and the 92 px split value. This is the
//    editor's Control, not a gas control — the same geometry draws a fog slider.
inline float FieldHeight(const GasField& Field) noexcept
{
    return Field.Control == GasControl::Switch ? SwitchTall
                                               : LabelSize + LabelFoot + PillTall;
}

inline void PaintSlider(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide,
                        const GasField& Field, float Reading) noexcept
{
    Draw->AddText(Light, LabelSize, { Spot.x, Spot.y }, FieldInk, Field.Label);
    const float PillTop  = Spot.y + LabelSize + LabelFoot;
    const float TrackWide = Wide - SplitWide - PillGap;
    const float TrackTop  = PillTop + (PillTall - TrackTall) * 0.5f;

    const float Span = std::max(1e-6f, Field.Maximum - Field.Minimum);
    const float Part = std::clamp((Reading - Field.Minimum) / Span, 0.0f, 1.0f);
    const float Fill = TrackWide * Part;

    Draw->AddRectFilled({ Spot.x, TrackTop }, { Spot.x + TrackWide, TrackTop + TrackTall }, TrackLow, TrackTall * 0.5f);
    if (Fill > 1.0f)
        Draw->AddRectFilled({ Spot.x, TrackTop }, { Spot.x + Fill, TrackTop + TrackTall }, TrackHigh, TrackTall * 0.5f);
    const float ThumbX = Spot.x + ThumbWide * 0.5f + (TrackWide - ThumbWide) * Part;
    Draw->AddCircleFilled({ ThumbX, TrackTop + TrackTall * 0.5f }, ThumbWide * 0.5f, ThumbFill, 24);

    const float SplitX = Spot.x + Wide - SplitWide;
    const float SplitY = PillTop + (PillTall - SplitTall) * 0.5f;
    Draw->AddRectFilled({ SplitX, SplitY }, { SplitX + SplitNumber, SplitY + SplitTall }, SplitFill, 14.0f,
                        ImDrawFlags_RoundCornersLeft);
    Draw->AddRectFilled({ SplitX + SplitNumber, SplitY }, { SplitX + SplitWide, SplitY + SplitTall }, UnitFill, 14.0f,
                        ImDrawFlags_RoundCornersRight);
    Draw->AddRect({ SplitX, SplitY }, { SplitX + SplitWide, SplitY + SplitTall }, SplitEdge, 14.0f, 0, 1.0f);

    char Figure[24];
    Spell(Figure, sizeof(Figure), Reading, Field.Decimals);
    const ImVec2 Measured = Light->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, Figure);
    Draw->AddText(Light, LabelSize, { SplitX + (SplitNumber - Measured.x) * 0.5f, Baseline(SplitY, SplitTall, LabelSize) },
                  BodyInk, Figure);
    if (Field.Unit != nullptr && *Field.Unit != '\0')
    {
        const ImVec2 UnitSize = Light->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, Field.Unit);
        Draw->AddText(Light, LabelSize,
                      { SplitX + SplitNumber + (SplitWide - SplitNumber - UnitSize.x) * 0.5f,
                        Baseline(SplitY, SplitTall, LabelSize) }, UnitInk, Field.Unit);
    }
}

inline void PaintSwitch(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Label, bool On) noexcept
{
    Draw->AddText(Light, LabelSize, { Spot.x, Baseline(Spot.y, SwitchTall, LabelSize) }, SwitchInk, Label);
    const float Left = Spot.x + Wide - SwitchWide;
    Draw->AddRectFilled({ Left, Spot.y }, { Left + SwitchWide, Spot.y + SwitchTall }, On ? SwitchOn : SwitchOff,
                        SwitchTall * 0.5f);
    const float KnobX = On ? Left + SwitchWide - 2.0f - 20.0f : Left + 2.0f;
    Draw->AddCircleFilled({ KnobX + 10.0f, Spot.y + SwitchTall * 0.5f }, 10.0f, Knob, 20);
}

inline void PaintSelect(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Label,
                        const char* Choice) noexcept
{
    Draw->AddText(Light, LabelSize, Spot, FieldInk, Label);
    const float Top = Spot.y + LabelSize + LabelFoot;
    Draw->AddRectFilled({ Spot.x, Top }, { Spot.x + Wide, Top + SelectTall }, SelectFill, 8.0f);
    Draw->AddRect({ Spot.x, Top }, { Spot.x + Wide, Top + SelectTall }, SelectEdge, 8.0f, 0, 1.0f);
    Draw->AddText(Light, LabelSize, { Spot.x + 11.0f, Baseline(Top, SelectTall, LabelSize) }, SelectInk, Choice,
                  nullptr, Wide - 34.0f);
    const float ArrowX = Spot.x + Wide - 18.0f, ArrowY = Top + SelectTall * 0.5f - 2.0f;
    Draw->AddLine({ ArrowX - 4.0f, ArrowY }, { ArrowX, ArrowY + 4.0f }, SelectInk, 1.2f);
    Draw->AddLine({ ArrowX, ArrowY + 4.0f }, { ArrowX + 4.0f, ArrowY }, SelectInk, 1.2f);
}

constexpr float SelectHeight = LabelSize + LabelFoot + SelectTall;

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE PREVIEW
//------------------------------------------------------------------------------------------------------------------------

// 🔴 A DORMANT DOMAIN DRAWS ITS BOUNDS AND NOTHING ELSE.
//    The browser preview refuses to animate a domain the game would not run, and so does this one. It would
//    be prettier to always show a plume, and it would teach everyone that effects run by themselves.
inline void PaintPreview(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const GasCardSubject& Subject,
                         bool Running) noexcept
{
    const float Tall = CanvasTall;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, IM_COL32(12, 15, 17, 255), 14.0f);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, CanvasEdge, 14.0f, 0, 1.0f);

    // The bounds box, in the aspect the authored extent gives it.
    const float Aspect  = std::clamp(Subject.Bounds[0] / std::max(0.05f, Subject.Bounds[1]), 0.35f, 2.4f);
    const float BoxTall = Tall * 0.74f;
    const float BoxWide = std::min(Wide * 0.62f, BoxTall * Aspect);
    const float Floor   = Spot.y + Tall * 0.88f;
    const ImVec2 Low    = { Spot.x + (Wide - BoxWide) * 0.5f, Floor };
    const ImVec2 High   = { Low.x + BoxWide, Floor - BoxTall };

    if (!Running)
    {
        // Dashed, because a solid box reads as something that is running.
        const float Step = 7.0f;
        for (float X = Low.x; X < High.x; X += Step * 2.0f)
        {
            Draw->AddLine({ X, Low.y }, { std::min(X + Step, High.x), Low.y }, IdleInk, 1.0f);
            Draw->AddLine({ X, High.y }, { std::min(X + Step, High.x), High.y }, IdleInk, 1.0f);
        }
        for (float Y = High.y; Y < Low.y; Y += Step * 2.0f)
        {
            Draw->AddLine({ Low.x, Y }, { Low.x, std::min(Y + Step, Low.y) }, IdleInk, 1.0f);
            Draw->AddLine({ High.x, Y }, { High.x, std::min(Y + Step, Low.y) }, IdleInk, 1.0f);
        }
        const char* Words = "DORMANT - NOT SIMULATING";
        const ImVec2 Measured = Light->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, Words);
        Draw->AddText(Light, 9.0f, { Spot.x + (Wide - Measured.x) * 0.5f, Floor - BoxTall * 0.5f }, StateInk, Words);
        return;
    }

    Draw->AddRect(Low, { High.x, High.y }, IM_COL32(255, 255, 255, 20), 2.0f, 0, 1.0f);

    // Smoke and heat, from the same four readings the raymarch weighs most: fire intensity and emitter
    //    temperature set how much of the base is incandescent, albedo lightens, extinction darkens.
    const float Heat   = std::min(1.0f, (Subject.Readings[9] / 10.0f) * std::min(1.0f, Subject.Readings[5] / 4.0f));
    const float Albedo = std::clamp(Subject.Readings[8], 0.0f, 1.0f);
    const float Rate   = std::max(0.05f, Subject.Readings[3]);
    for (int Index = 0; Index < 34; ++Index)
    {
        // Deterministic seeds: the same subject always paints the same plume, so a capture is comparable.
        const float Phase = float((Index * 61) % 97) / 97.0f;
        const float Sway  = (float((Index * 37) % 71) / 71.0f - 0.5f) * 2.0f;
        const float Size  = 0.45f + float((Index * 53) % 83) / 83.0f * 0.8f;
        const float Life  = Phase;
        const float Y     = Floor - Life * BoxTall * 0.96f;
        const float Spread = 0.1f + Life * 0.46f;
        const float X     = Low.x + BoxWide * 0.5f + Sway * Spread * BoxWide * 0.42f;
        const float Radius = Size * BoxWide * (0.07f + Spread * 0.2f);
        const float Fade  = std::sin(3.14159265f * std::min(1.0f, Life * 1.25f)) * Rate;
        const float Glow  = std::max(0.0f, Heat * (1.0f - Life * 3.2f));
        const int   Grey  = int(70.0f + Albedo * 150.0f);
        const int   Alpha = int(std::clamp(26.0f + Fade * 70.0f, 0.0f, 150.0f));
        const ImU32 Tone  = IM_COL32(int(Grey + Glow * (255 - Grey)), int(Grey + Glow * (150 - Grey)),
                                     int(Grey + Glow * (60 - Grey)), Alpha);
        Draw->AddCircleFilled({ X, Y }, std::max(1.0f, Radius), Tone, 18);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

// ① Position, Rotation, Bounds. Bounds take the row Scale occupies on a mesh, in metres, because the
//    domain's extent IS its scale — and a row you cannot move, delete or duplicate independently is not a
//    child entity, it is a property wearing a row.
inline float TransformHeight() noexcept
{
    return PlainPadY + 14.0f                 // header: .transform-card h3 at 14px
         + 18.0f                             // .transform-table margin-top
         + 10.0f + 10.0f                     // thead: 10px text + its 10px padding-bottom
         + 3.0f * (RowTall + RowPad * 2.0f)  // three rows
         + 14.0f + PlainPadY;                // table margin-bottom
}

inline void PaintTransform(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                           const GasCardSubject& Subject) noexcept
{
    const float Tall = TransformHeight();
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, PlainFill, CardRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, PlainEdge, CardRound, 0, 1.0f);

    const float Left = Spot.x + PlainPadX, Inner = Wide - PlainPadX * 2.0f;
    float Cursor = Spot.y + PlainPadY;
    Draw->AddText(Regular, 14.0f, { Left, Cursor }, TransTitle, "Transform");
    const char* Space = "WORLD SPACE";
    const ImVec2 SpaceSize = Light->CalcTextSizeA(8.0f, FLT_MAX, 0.0f, Space);
    Draw->AddText(Light, 8.0f, { Left + Inner - SpaceSize.x, Cursor + 4.0f }, SpaceInk, Space);
    Cursor += 14.0f + 18.0f;

    const float NameWide = Inner * 0.28f;                       // [-] .transform-table th:first-child 28%
    const float CellWide = (Inner - NameWide) / 3.0f;
    const ImU32 AxisInk[3] = { AxisX, AxisY, AxisZ };
    const char* AxisName[3] = { "X", "Y", "Z" };
    Draw->AddText(Light, 8.0f, { Left, Cursor }, SpaceInk, "Component");
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const ImVec2 Measured = Light->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, AxisName[Axis]);
        Draw->AddText(Light, 10.0f, { Left + NameWide + CellWide * Axis + (CellWide - Measured.x) * 0.5f, Cursor },
                      AxisInk[Axis], AxisName[Axis]);
    }
    Cursor += 10.0f + 10.0f;

    const char* RowName[3] = { "Position", "Rotation", "Bounds" };
    const char* RowUnit[3] = { "m", "deg", "m" };
    const float* RowValue[3] = { Subject.Position, Subject.Rotation, Subject.Bounds };
    for (int Row = 0; Row < 3; ++Row)
    {
        const float Top = Cursor + RowPad;
        Draw->AddText(Light, 10.0f, { Left, Top + 2.0f }, RowInk, RowName[Row]);
        Draw->AddText(Light, 8.0f, { Left, Top + 15.0f }, RowUnitInk, RowUnit[Row]);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float BoxLeft = Left + NameWide + CellWide * Axis + 2.0f;
            const float BoxWide = CellWide - 4.0f;
            Draw->AddRectFilled({ BoxLeft, Top }, { BoxLeft + BoxWide, Top + RowTall }, NumberFill, 7.0f);
            Draw->AddRect({ BoxLeft, Top }, { BoxLeft + BoxWide, Top + RowTall }, NumberEdge, 7.0f, 0, 1.0f);
            char Figure[24];
            Spell(Figure, sizeof(Figure), RowValue[Row][Axis], 2u);
            const ImVec2 Measured = Light->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, Figure);
            Draw->AddText(Light, LabelSize, { BoxLeft + (BoxWide - Measured.x) * 0.5f, Baseline(Top, RowTall, LabelSize) },
                          BodyInk, Figure);
        }
        Cursor += RowTall + RowPad * 2.0f;
    }
}

// ② The caption under the transform. The lattice is axis-aligned, so rotation orients the children and the
//    authored framing and nothing else — said on the card, not only in a comment.
inline const char* RotationCaption() noexcept
{
    return "ROTATION ORIENTS CHILDREN, NOT THE LATTICE - the solver cube is axis-aligned";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE CARDS
//------------------------------------------------------------------------------------------------------------------------

inline void PaintCardShell(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, float Tall,
                           const char* Title) noexcept
{
    Draw->AddRectFilledMultiColor({ Spot.x, Spot.y }, { Spot.x + Wide, Spot.y + Tall },
                                  CardTop, CardTop, CardFoot, CardFoot);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, CardEdge, CardRound, 0, 1.0f);
    Draw->AddText(Light, TitleSize, { Spot.x + CardPadX, Spot.y + CardPadTop }, CardTitle, Title);
}

inline void PaintTiles(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide,
                       const char* const* Labels, const bool* On, uint32_t Count) noexcept
{
    const float Each = (Wide - TileGap * float(Count - 1u)) / float(Count);
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const float Left = Spot.x + (Each + TileGap) * float(Index);
        Draw->AddRectFilled({ Left, Spot.y }, { Left + Each, Spot.y + TileTall }, TileFill, 17.0f);
        Draw->AddRect({ Left, Spot.y }, { Left + Each, Spot.y + TileTall }, TileEdge, 17.0f, 0, 1.0f);
        const ImU32 Ink = On[Index] ? TileOnInk : TileOffInk;
        Draw->AddRectFilled({ Left + Each * 0.5f - 20.0f, Spot.y + 12.0f },
                            { Left + Each * 0.5f + 20.0f, Spot.y + 52.0f },
                            IM_COL32(((Ink >> IM_COL32_R_SHIFT) & 0xFF), ((Ink >> IM_COL32_G_SHIFT) & 0xFF),
                                     ((Ink >> IM_COL32_B_SHIFT) & 0xFF), 24), 13.0f);
        const ImVec2 Measured = Light->CalcTextSizeA(LabelSize, Each - 12.0f, Each - 12.0f, Labels[Index]);
        Draw->AddText(Light, LabelSize, { Left + (Each - Measured.x) * 0.5f, Spot.y + 62.0f }, TileInk,
                      Labels[Index], nullptr, Each - 12.0f);
        const char* Status = On[Index] ? "ON" : "OFF";
        const ImVec2 StatusSize = Light->CalcTextSizeA(8.0f, FLT_MAX, 0.0f, Status);
        Draw->AddText(Light, 8.0f, { Left + (Each - StatusSize.x) * 0.5f, Spot.y + TileTall - 18.0f }, Ink, Status);
    }
}

inline void PaintStateLine(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, bool Running,
                           const char* Words) noexcept
{
    Draw->AddCircleFilled({ Spot.x + 3.5f, Spot.y + 6.0f }, 3.5f, Running ? LiveInk : IdleInk, 12);
    Draw->AddText(Light, LabelSize, { Spot.x + 15.0f, Spot.y }, StateInk, Words, nullptr, Wide - 15.0f);
}

inline void PaintChildRow(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const GasChild& Child) noexcept
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + ChildTall }, ChildFill, 9.0f);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + ChildTall }, ChildEdge, 9.0f, 0, 1.0f);
    Draw->AddCircleFilled({ Spot.x + 17.0f, Spot.y + ChildTall * 0.5f }, 4.0f,
                          Child.Enabled ? TileOnInk : IdleInk, 12);
    Draw->AddText(Light, LabelSize, { Spot.x + 30.0f, Baseline(Spot.y, ChildTall, LabelSize) }, ChildInk, Child.Name);
    const ImVec2 Measured = Light->CalcTextSizeA(NoteSize, FLT_MAX, 0.0f, Child.Summary);
    Draw->AddText(Light, NoteSize, { Spot.x + Wide - 9.0f - Measured.x, Baseline(Spot.y, ChildTall, NoteSize) },
                  NoteInk, Child.Summary);
}

inline void PaintAction(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Words,
                        ImU32 Fill, ImU32 Edge, ImU32 Ink) noexcept
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + ActionTall }, Fill, 10.0f);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + ActionTall }, Edge, 10.0f, 0, 1.0f);
    const ImVec2 Measured = Light->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, Words);
    Draw->AddText(Light, LabelSize, { Spot.x + (Wide - Measured.x) * 0.5f, Baseline(Spot.y, ActionTall, LabelSize) },
                  Ink, Words);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STACK
//------------------------------------------------------------------------------------------------------------------------

// The eight bands of the card stack, in the browser's order. Heights are resolved once so the painter and
//    the harness read the same numbers, which is the only way a capture can be checked against a layout.
struct GasStackLayout
{
    float Tiles      = 0.0f;
    float Transform  = 0.0f;
    float Caption    = 0.0f;
    float Domain     = 0.0f;
    float Simulation = 0.0f;
    float Budget     = 0.0f;
    float Source     = 0.0f;
    float Appearance = 0.0f;
    float Hierarchy  = 0.0f;
    float Open       = 0.0f;
    float Total      = 0.0f;
};

inline const char* PolicyName(uint32_t Policy) noexcept
{
    switch (Policy)
    {
        case 0u: return "Dormant";
        case 1u: return "On trigger";
        case 2u: return "On proximity";
        default: return "Always";
    }
}

inline const char* PolicyNote(uint32_t Policy) noexcept
{
    switch (Policy)
    {
        case 0u:
            return "Present in the scene and drawing its bounds, simulating nothing until a gameplay event "
                   "fires it. A fracture burst or a detonation is this.";
        case 1u:
            return "Dormant until triggered, then simulates for its lifetime and retires. Most gas is this: "
                   "a one-shot that runs once, is destroyed, and gives its fields back.";
        case 2u:
            return "Simulates while the viewer is inside the far band and sleeps beyond it. Proximity decides "
                   "whether it runs; the budget still decides how well.";
        default:
            return "A camp fire, a chimney, a vent. Runs whenever the level is loaded, at whatever tier the "
                   "budget grants.";
    }
}

inline const char* HierarchyNote() noexcept
{
    // ③ The load-bearing one. A crate obstructs three domains at once and must survive the deletion of any
    //    of them, so obstruction is an opt-in on the object, not a parenting.
    return "Colliders are not children. An object obstructs gas by opting in from its own inspector, so it "
           "can obstruct several domains, move between them, and survive the deletion of any of them.";
}

inline bool GasCardRunning(const GasCardSubject& Subject) noexcept
{
    if (Subject.Hidden) return false;
    if (Subject.Policy == 3u) return true;                              // Always
    if (Subject.Policy == 2u) return Subject.Distance < 150.0f;         // Proximity
    return Subject.Fired;                                               // Dormant / Triggered
}

inline void StateWords(const GasCardSubject& Subject, char* Out, size_t Room) noexcept
{
    if (Subject.Hidden)          { std::snprintf(Out, Room, "Hidden - nothing simulates"); return; }
    if (Subject.Policy == 0u)    { std::snprintf(Out, Room, "In the scene, not simulating - waits to be fired"); return; }
    if (Subject.Policy == 1u)
    {
        if (Subject.Retire)
            std::snprintf(Out, Room, "Fires once - %.1f s + 1.6 s fade, then destroyed", double(Subject.Lifetime));
        else
            std::snprintf(Out, Room, "Fires on trigger - runs %.1f s, then sleeps holding its fields",
                          double(Subject.Lifetime));
        return;
    }
    if (Subject.Policy == 2u)
    {
        std::snprintf(Out, Room, "Simulates inside 150 m - %.0f m away", double(Subject.Distance));
        return;
    }
    std::snprintf(Out, Room, "Always on - %s", Subject.TierReadout);
}

inline float SheetHeight(const GasField* Fields, uint32_t Count, const char* Group) noexcept
{
    float Tall = 0.0f;
    uint32_t Shown = 0u;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (std::strcmp(Fields[Index].Group, Group) != 0) continue;
        if (Shown++) Tall += FieldGap;
        Tall += FieldHeight(Fields[Index]);
    }
    return Tall;
}

inline GasStackLayout ResolveStack(ImFont* Light, float Wide, const GasCardSubject& Subject) noexcept
{
    const float Inner      = Wide - CardPadX * 2.0f;
    const float PlainInner = Wide - PlainPadX * 2.0f;
    uint32_t Count = 0u;
    const GasField* Fields = GasSheet(Count);

    GasStackLayout Layout;
    Layout.Tiles     = TileTall + CardGap;
    Layout.Transform = TransformHeight() + PlainGap;
    Layout.Caption   = NoteHeight(Light, Wide, RotationCaption()) + 20.0f;

    const char* DomainNote = "Dynamic bounds let the cube grow to the surge limit when the plume reaches a "
                             "wall, at the cost of coarser voxels for the same count. Enclosed stops it "
                             "venting through the sides.";
    Layout.Domain = CardPadTop + TitleSize + TitleFoot + CanvasTall + 12.0f + LabelSize + 10.0f
                  + SelectHeight + FieldGap + SheetHeight(Fields, Count, "Domain")
                  + 10.0f + NoteHeight(Light, Inner, DomainNote) + CardPadFoot + CardGap;

    Layout.Simulation = CardPadTop + TitleSize + TitleFoot + SelectHeight
                      + 10.0f + NoteHeight(Light, Inner, PolicyNote(Subject.Policy));
    if (Subject.Policy == 1u)
        Layout.Simulation += FieldGap + LabelSize + LabelFoot + PillTall + FieldGap + SwitchTall;
    if (Subject.Policy != 3u) Layout.Simulation += 10.0f + ActionTall;
    Layout.Simulation += CardPadFoot + CardGap;

    Layout.Budget = CardPadTop + TitleSize + TitleFoot + MetricSize + MetricFoot + NoteSize * NoteLead
                  + FieldGap + SelectHeight + FieldGap + LabelSize + LabelFoot + PillTall
                  + CardPadFoot + CardGap;

    Layout.Source = CardPadTop + TitleSize + TitleFoot + CardPadFoot + CardGap;
    if (Subject.ChildCount > 0u)
        Layout.Source += NoteHeight(Light, Inner, "Emission comes from the child emitters below.");
    else
        Layout.Source += SheetHeight(Fields, Count, "Source");

    Layout.Appearance = CardPadTop + TitleSize + TitleFoot + SheetHeight(Fields, Count, "Appearance")
                      + CardPadFoot + CardGap;

    Layout.Hierarchy = PlainPadY + CaptionSize + 10.0f;
    if (Subject.ChildCount == 0u)
        Layout.Hierarchy += NoteHeight(Light, PlainInner,
                                       "No child emitters. The domain emits from its own centre.");
    else
        Layout.Hierarchy += float(Subject.ChildCount) * (ChildTall + 4.0f);
    Layout.Hierarchy += 10.0f + ActionTall + 10.0f + NoteHeight(Light, PlainInner, HierarchyNote())
                      + PlainPadY + PlainGap;

    Layout.Open = (Subject.Departures > 0u ? ActionTall + 10.0f : 0.0f) + OpenTall + 14.0f;

    Layout.Total = Layout.Tiles + Layout.Transform + Layout.Caption + Layout.Domain + Layout.Simulation
                 + Layout.Budget + Layout.Source + Layout.Appearance + Layout.Hierarchy + Layout.Open;
    return Layout;
}


// 📝 WHERE THE PANEL MUST PUT ITS BUTTONS, WRITTEN BY THE PAINTER RATHER THAN DERIVED TWICE.
//    A draw-list card has no widgets, so InspectorPanel.cpp lays invisible buttons over it. Recomputing
//    the cursor walk there would be a second copy of this file's arithmetic, and the two would part
//    company the first time a note gained a line. So the painter reports where it actually painted, and
//    every region below is in absolute screen coordinates: x, y, width, height.
struct GasHitRegions
{
    ImVec4   Tiles[3]  = {};     // [px] - Visible, Obstructs, Pushes objects
    ImVec4   Preset    = {};
    ImVec4   Policy    = {};
    ImVec4   Lifetime  = {};     // [px] - the slider track only; empty unless triggered
    ImVec4   Retire    = {};     // [px] - 🔴 the toggle that decides whether a one-shot gives its fields back
    ImVec4   Fire      = {};
    ImVec4   Quality   = {};
    ImVec4   Distance  = {};
    ImVec4   Field[16] = {};     // [px] - by sheet index; zero width means the band did not draw it
    ImVec4   Children[4] = {};
    ImVec4   AddEmitter = {};
    ImVec4   Revert    = {};
    ImVec4   Open      = {};
};

inline bool Hit(const ImVec4& Region, ImVec2 Spot) noexcept
{
    return Region.z > 0.0f && Spot.x >= Region.x && Spot.x <= Region.x + Region.z &&
           Spot.y >= Region.y && Spot.y <= Region.y + Region.w;
}


/// 📦 Paint the whole gas domain inspector at Spot, in a column Wide pixels across.
/// in    Draw / Light / Regular   [-]   the draw list and the two DM Sans faces the editor ships
/// in    Spot                     [px]  top-left of the card stack
/// in    Wide                     [px]  column width; 300 in the shipped inspector
/// in    Subject                  [-]   the domain as the host resolved it
/// out   float                    [px]  the height consumed, matching ResolveStack(...).Total
/// cost  ✔️ one pass, no allocation
/// tag   api, nonthrowing
inline float PaintGasCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                          const GasCardSubject& Subject, GasHitRegions* Regions = nullptr) noexcept
{
    GasHitRegions Discard;
    GasHitRegions& Where = Regions != nullptr ? *Regions : Discard;
    Where = GasHitRegions{};
    const GasStackLayout Layout = ResolveStack(Light, Wide, Subject);
    const float Inner      = Wide - CardPadX * 2.0f;
    const float PlainInner = Wide - PlainPadX * 2.0f;
    const bool  Running    = GasCardRunning(Subject);
    uint32_t Count = 0u;
    const GasField* Fields = GasSheet(Count);
    float Cursor = Spot.y;

    // Quick controls.
    const char* TileLabel[3] = { "Visible", "Obstructs", "Pushes objects" };
    const bool  TileOn[3]    = { !Subject.Hidden, Subject.Obstructs, Subject.Coupled };
    PaintTiles(Draw, Light, { Spot.x, Cursor }, Wide, TileLabel, TileOn, 3u);
    const float TileEach = (Wide - TileGap * 2.0f) / 3.0f;
    for (int Index = 0; Index < 3; ++Index)
        Where.Tiles[Index] = { Spot.x + (TileEach + TileGap) * float(Index), Cursor, TileEach, TileTall };
    Cursor += Layout.Tiles;

    // ① The transform, first, because this is a 3D entity before it is an effect.
    PaintTransform(Draw, Light, Regular, { Spot.x, Cursor }, Wide, Subject);
    Cursor += Layout.Transform;
    Draw->AddText(Light, CaptionSize, { Spot.x, Cursor }, CaptionInk, RotationCaption(), nullptr, Wide);
    Cursor += Layout.Caption;

    // Domain.
    {
        const float Tall = Layout.Domain - CardGap;
        PaintCardShell(Draw, Light, { Spot.x, Cursor }, Wide, Tall, "Domain");
        float Inside = Cursor + CardPadTop + TitleSize + TitleFoot;
        PaintPreview(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Subject, Running);
        Inside += CanvasTall + 12.0f;
        char Words[128];
        StateWords(Subject, Words, sizeof(Words));
        PaintStateLine(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Running, Words);
        Inside += LabelSize + 10.0f;
        PaintSelect(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, "Preset", Subject.PresetName);
        Where.Preset = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot, Inner, SelectTall };
        Inside += SelectHeight + FieldGap;
        uint32_t Reading = 0u;
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            if (std::strcmp(Fields[Index].Group, "Domain") != 0) { ++Reading; continue; }
            if (Fields[Index].Control == GasControl::Switch)
            {
                PaintSwitch(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Fields[Index].Label,
                            Subject.Readings[Index] > 0.5f);
                Where.Field[Index] = { Spot.x + CardPadX + Inner - SwitchWide, Inside, SwitchWide, SwitchTall };
                Inside += SwitchTall + FieldGap;
            }
            else
            {
                PaintSlider(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Fields[Index], Subject.Readings[Index]);
                Where.Field[Index] = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot + (PillTall - TrackTall) * 0.5f,
                                       Inner - SplitWide - PillGap, TrackTall };
                Inside += FieldHeight(Fields[Index]) + FieldGap;
            }
            ++Reading;
        }
        Inside += -FieldGap + 10.0f;
        PaintNote(Draw, Light, { Spot.x + CardPadX, Inside }, Inner,
                  "Dynamic bounds let the cube grow to the surge limit when the plume reaches a wall, at the "
                  "cost of coarser voxels for the same count. Enclosed stops it venting through the sides.");
        Cursor += Layout.Domain;
    }

    // Simulation — the run policy, and 🔴 whether a one-shot gives its fields back.
    {
        const float Tall = Layout.Simulation - CardGap;
        PaintCardShell(Draw, Light, { Spot.x, Cursor }, Wide, Tall, "Simulation");
        float Inside = Cursor + CardPadTop + TitleSize + TitleFoot;
        PaintSelect(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, "Runs", PolicyName(Subject.Policy));
        Where.Policy = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot, Inner, SelectTall };
        Inside += SelectHeight + 10.0f;
        PaintNote(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, PolicyNote(Subject.Policy));
        Inside += NoteHeight(Light, Inner, PolicyNote(Subject.Policy));
        if (Subject.Policy == 1u)
        {
            Inside += FieldGap;
            const GasField Life{ "Lifetime", "Simulation", "lifetime", GasControl::Slider, 0.2f, 30.0f, 4.0f, 1u, "s" };
            PaintSlider(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Life, Subject.Lifetime);
            Where.Lifetime = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot + (PillTall - TrackTall) * 0.5f,
                               Inner - SplitWide - PillGap, TrackTall };
            Inside += FieldHeight(Life) + FieldGap;
            PaintSwitch(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, "Retire When Finished", Subject.Retire);
            Where.Retire = { Spot.x + CardPadX + Inner - SwitchWide, Inside, SwitchWide, SwitchTall };
            Inside += SwitchTall;
        }
        if (Subject.Policy != 3u)
        {
            Inside += 10.0f;
            PaintAction(Draw, Light, { Spot.x + CardPadX, Inside }, Inner,
                        Subject.Fired ? "Stop preview" : "Fire in preview", ActionFill, ActionEdge, ActionInk);
            Where.Fire = { Spot.x + CardPadX, Inside, Inner, ActionTall };
        }
        Cursor += Layout.Simulation;
    }

    // Budget.
    {
        const float Tall = Layout.Budget - CardGap;
        PaintCardShell(Draw, Light, { Spot.x, Cursor }, Wide, Tall, "Budget");
        float Inside = Cursor + CardPadTop + TitleSize + TitleFoot;
        Draw->AddText(Light, MetricSize, { Spot.x + CardPadX, Inside },
                      Subject.OverAmber ? AmberInk : MetricInk, Subject.Cost);
        Inside += MetricSize + MetricFoot;
        PaintNote(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Subject.TierReadout);
        Inside += NoteSize * NoteLead + FieldGap;
        PaintSelect(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, "Quality", Subject.QualityPin);
        Where.Quality = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot, Inner, SelectTall };
        Inside += SelectHeight + FieldGap;
        const GasField Far{ "Viewer Distance", "Budget", "distance", GasControl::Slider, 0.0f, 200.0f, 12.0f, 0u, "m" };
        PaintSlider(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Far, Subject.Distance);
        Where.Distance = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot + (PillTall - TrackTall) * 0.5f,
                           Inner - SplitWide - PillGap, TrackTall };
        Cursor += Layout.Budget;
    }

    // Source — or a pointer to the children that replaced it.
    {
        const float Tall = Layout.Source - CardGap;
        PaintCardShell(Draw, Light, { Spot.x, Cursor }, Wide, Tall, "Source");
        float Inside = Cursor + CardPadTop + TitleSize + TitleFoot;
        if (Subject.ChildCount > 0u)
        {
            PaintNote(Draw, Light, { Spot.x + CardPadX, Inside }, Inner,
                      "Emission comes from the child emitters below.");
        }
        else
        {
            for (uint32_t Index = 0u; Index < Count; ++Index)
            {
                if (std::strcmp(Fields[Index].Group, "Source") != 0) continue;
                PaintSlider(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Fields[Index], Subject.Readings[Index]);
                Where.Field[Index] = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot + (PillTall - TrackTall) * 0.5f,
                                       Inner - SplitWide - PillGap, TrackTall };
                Inside += FieldHeight(Fields[Index]) + FieldGap;
            }
        }
        Cursor += Layout.Source;
    }

    // Appearance.
    {
        const float Tall = Layout.Appearance - CardGap;
        PaintCardShell(Draw, Light, { Spot.x, Cursor }, Wide, Tall, "Appearance");
        float Inside = Cursor + CardPadTop + TitleSize + TitleFoot;
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            if (std::strcmp(Fields[Index].Group, "Appearance") != 0) continue;
            PaintSlider(Draw, Light, { Spot.x + CardPadX, Inside }, Inner, Fields[Index], Subject.Readings[Index]);
            Where.Field[Index] = { Spot.x + CardPadX, Inside + LabelSize + LabelFoot + (PillTall - TrackTall) * 0.5f,
                                   Inner - SplitWide - PillGap, TrackTall };
            Inside += FieldHeight(Fields[Index]) + FieldGap;
        }
        Cursor += Layout.Appearance;
    }

    // ② Hierarchy — emitters only. ③ And the sentence about why a collider is not here.
    {
        const float Tall = Layout.Hierarchy - PlainGap;
        Draw->AddRectFilled({ Spot.x, Cursor }, { Spot.x + Wide, Cursor + Tall }, PlainFill, CardRound);
        Draw->AddRect({ Spot.x, Cursor }, { Spot.x + Wide, Cursor + Tall }, PlainEdge, CardRound, 0, 1.0f);
        float Inside = Cursor + PlainPadY;
        Draw->AddText(Light, CaptionSize, { Spot.x + PlainPadX, Inside }, SummaryInk, "HIERARCHY");
        Inside += CaptionSize + 10.0f;
        if (Subject.ChildCount == 0u)
        {
            const char* Empty = "No child emitters. The domain emits from its own centre.";
            PaintNote(Draw, Light, { Spot.x + PlainPadX, Inside }, PlainInner, Empty);
            Inside += NoteHeight(Light, PlainInner, Empty);
        }
        else
        {
            for (uint32_t Index = 0u; Index < Subject.ChildCount; ++Index)
            {
                PaintChildRow(Draw, Light, { Spot.x + PlainPadX, Inside }, PlainInner, Subject.Children[Index]);
                if (Index < 4u) Where.Children[Index] = { Spot.x + PlainPadX, Inside, PlainInner, ChildTall };
                Inside += ChildTall + 4.0f;
            }
        }
        Inside += 10.0f;
        PaintAction(Draw, Light, { Spot.x + PlainPadX, Inside }, PlainInner, "Add emitter",
                    ActionFill, ActionEdge, ActionInk);
        Where.AddEmitter = { Spot.x + PlainPadX, Inside, PlainInner, ActionTall };
        Inside += ActionTall + 10.0f;
        PaintNote(Draw, Light, { Spot.x + PlainPadX, Inside }, PlainInner, HierarchyNote());
        Cursor += Layout.Hierarchy;
    }

    // Revert, then the one button to the full editor.
    if (Subject.Departures > 0u)
    {
        char Words[64];
        std::snprintf(Words, sizeof(Words), "Revert %u change%s to %s", Subject.Departures,
                      Subject.Departures == 1u ? "" : "s", Subject.PresetName);
        PaintAction(Draw, Light, { Spot.x, Cursor }, Wide, Words, ActionFill, ActionEdge, SummaryInk);
        Where.Revert = { Spot.x, Cursor, Wide, ActionTall };
        Cursor += ActionTall + 10.0f;
    }
    Draw->AddRectFilled({ Spot.x, Cursor }, { Spot.x + Wide, Cursor + OpenTall }, OpenFill, 10.0f);
    Draw->AddRect({ Spot.x, Cursor }, { Spot.x + Wide, Cursor + OpenTall }, OpenEdge, 10.0f, 0, 1.0f);
    Where.Open = { Spot.x, Cursor, Wide, OpenTall };
    {
        // ③ A domain is a volume a solver integrates and opens in the Fluid simulator. An emitter is a
        //    source of discrete things and opens in the Particle Editor. One button, two destinations,
        //    chosen by what the row is -- not two buttons, and not one editor pretending to be both.
        const char* Title = Subject.Emitter ? "Open ParticleEditor" : "Open FluidEditor";
        const char* Under = Subject.Emitter ? "52 presets - sparks - debris - fibres - lightning"
                                            : "all 85 settings - presets - flipbook bake";
        const ImVec2 TitleSized = Light->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, Title);
        const ImVec2 UnderSized = Light->CalcTextSizeA(NoteSize, FLT_MAX, 0.0f, Under);
        Draw->AddText(Light, 12.0f, { Spot.x + (Wide - TitleSized.x) * 0.5f, Cursor + 13.0f }, ActionInk, Title);
        Draw->AddText(Light, NoteSize, { Spot.x + (Wide - UnderSized.x) * 0.5f, Cursor + 30.0f }, NoteInk, Under);
    }

    return Layout.Total;
}

}   // namespace Frontier::GasCards
