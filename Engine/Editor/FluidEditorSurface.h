//==============================================================================================================================================
//                                                           FLUIDEDITORSURFACE.H
//==============================================================================================================================================
// 📦 The Fluid editor — the application the gas card's ↗ button opens, ported chrome for chrome.
//
//    Reference: Experimental/Fluid/index.html (630 lines) with src/FluidPanel.css (1,759 lines) and
//    src/FluidPanel.js (1,593 lines), the same browser app the user authored the eight sample scenes in.
//    GasCardSurface.h drew that ↗ and InspectorPanel::ConsumeFluidEditorRequest() raised a request with
//    nothing on the other end of it; this is the other end.
//
//    Every label, limit and option the inspector shows comes from FluidEditorControls.h, which is
//    generated from the browser's own SceneSpecification.js — the page below decides where a control sits,
//    never what it says or how far its slider travels. The eighteen preset cards and the nine debug
//    channels arrive the same way.
//
//    This is the UI, as the fracture editor port was. It paints the whole three-column workspace at
//    whatever size the host gives it from a Subject the host fills: the solver behind it is
//    CoarseGasField.h and is not driven from here. A figure that would come from a running simulation —
//    the frame rate, the elapsed time, the live voxel count — is carried on the Subject, so a host that
//    has no simulation yet shows the browser's own idle readings rather than inventing any.
//
//    Everything is measured from the stylesheet. The app is a flex column of four bands around one
//    flexible grid, so the layout resolves once into Layout and the painter and the harness read the same
//    numbers.

#pragma once

#include "FluidEditorControls.h"
#include "WindInstrumentSurface.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace Frontier::FluidEditor
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                         PALETTE
//------------------------------------------------------------------------------------------------------------------------
// ThemeSpecification.css :root, then the literals FluidPanel.css uses where it does not reach for a token.

constexpr ImU32 PageFill    = IM_COL32( 11,  11,  11, 255);   // [-] --bg #0b0b0b
constexpr ImU32 PanelFill   = IM_COL32( 18,  18,  18, 255);   // [-] --panel #121212
constexpr ImU32 CardFill    = IM_COL32( 26,  26,  26, 255);   // [-] --card #1a1a1a
constexpr ImU32 RaisedFill  = IM_COL32( 34,  34,  34, 255);   // [-] --raised #222222
constexpr ImU32 HeaderFill  = IM_COL32( 36,  36,  36, 255);   // [-] .app-header #242424
constexpr ImU32 HeaderEdge  = IM_COL32( 56,  56,  56, 255);   // [-] its border-bottom #383838
constexpr ImU32 DocFill     = IM_COL32( 29,  29,  29, 255);   // [-] .document-bar #1d1d1d
constexpr ImU32 CentreFill  = IM_COL32( 25,  25,  25, 255);   // [-] .center-panel #191919
constexpr ImU32 BarFill     = IM_COL32( 39,  39,  39, 255);   // [-] .viewport-bar #272727
constexpr ImU32 StageFill   = IM_COL32( 28,  28,  28, 255);   // [-] .viewport #1c1c1c
constexpr ImU32 MoveFill    = IM_COL32( 38,  38,  38, 255);   // [-] .transport #262626
constexpr ImU32 TimeFill    = IM_COL32( 31,  31,  31, 255);   // [-] .timeline #1f1f1f
constexpr ImU32 Line        = IM_COL32(255, 255, 255,  18);   // [-] --line rgba(255,255,255,.07)
constexpr ImU32 PaneEdge    = IM_COL32( 52,  52,  52, 255);   // [-] .panel-heading border #343434
constexpr ImU32 GroupEdge   = IM_COL32( 56,  56,  56, 255);   // [-] .property-group border #383838
constexpr ImU32 StatusEdge  = IM_COL32( 60,  60,  60, 255);   // [-] .status-bar border-top #3c3c3c

constexpr ImU32 PageInk     = IM_COL32(240, 240, 240, 255);   // [-] --text #f0f0f0
constexpr ImU32 StrongInk   = IM_COL32(198, 198, 198, 255);   // [-] #c6c6c6
constexpr ImU32 HeadInk     = IM_COL32(188, 188, 188, 255);   // [-] .panel-heading #bcbcbc
constexpr ImU32 BodyInk     = IM_COL32(171, 171, 171, 255);   // [-] #ababab
constexpr ImU32 LabelInk    = IM_COL32(159, 159, 159, 255);   // [-] .property-label #9f9f9f
constexpr ImU32 QuietInk    = IM_COL32(136, 136, 136, 255);   // [-] --muted #888888
constexpr ImU32 FaintInk    = IM_COL32(111, 111, 111, 255);   // [-] #6f6f6f
constexpr ImU32 BadgeInk    = IM_COL32(122, 122, 122, 255);   // [-] .section-badge #7a7a7a
constexpr ImU32 LiveInk     = IM_COL32( 52, 199,  89, 255);   // [-] --green #34c759
constexpr ImU32 AmberInk    = IM_COL32(255, 180,  84, 255);   // [-] --row-accent #ffb454
constexpr ImU32 AzureInk    = IM_COL32( 90, 169, 255, 255);   // [-] --row-accent #5aa9ff
constexpr ImU32 PaleInk     = IM_COL32(223, 230, 245, 255);   // [-] --row-accent #dfe6f5

constexpr ImU32 ControlFill = IM_COL32( 51,  51,  51, 255);   // [-] .button #333333
constexpr ImU32 ControlEdge = IM_COL32( 72,  72,  72, 255);   // [-] its border #484848
constexpr ImU32 FieldFill   = IM_COL32( 28,  28,  28, 255);   // [-] .search #1c1c1c
constexpr ImU32 FieldEdge   = IM_COL32( 59,  59,  59, 255);   // [-] its border #3b3b3b
constexpr ImU32 ChosenFill  = IM_COL32( 49,  49,  49, 255);   // [-] .filter-row button.active #313131
constexpr ImU32 ChosenEdge  = IM_COL32( 66,  66,  66, 255);   // [-] its border #424242
constexpr ImU32 RowChosen   = IM_COL32( 50,  50,  50, 255);   // [-] .scene-row.selected #323232
constexpr ImU32 RowEdge     = IM_COL32( 69,  69,  69, 255);   // [-] its border #454545
constexpr ImU32 CardEdge    = IM_COL32( 57,  57,  57, 255);   // [-] .preset-card border #393939
constexpr ImU32 CardBack    = IM_COL32( 40,  40,  40, 255);   // [-] .preset-card #282828
constexpr ImU32 CardLive    = IM_COL32( 46,  46,  46, 255);   // [-] .preset-card.active #2e2e2e
constexpr ImU32 CardLiveEdge= IM_COL32(102, 102, 102, 255);   // [-] its border #666666
constexpr ImU32 SwatchFill  = IM_COL32( 48,  48,  48, 255);   // [-] .preset-swatch #303030
constexpr ImU32 SwatchEdge  = IM_COL32( 83,  83,  83, 255);   // [-] its border #535353
constexpr ImU32 TrackFill   = IM_COL32( 66,  66,  66, 255);   // [-] range track #424242
constexpr ImU32 TrackRun    = IM_COL32(139, 139, 139, 255);   // [-] its filled part #8b8b8b
constexpr ImU32 ThumbFill   = IM_COL32(163, 163, 163, 255);   // [-] its thumb #a3a3a3
constexpr ImU32 ThumbEdge   = IM_COL32(188, 188, 188, 255);   // [-] its border #bcbcbc
constexpr ImU32 SwitchOff   = IM_COL32( 76,  76,  76, 255);   // [-] .switch > span #4c4c4c
constexpr ImU32 SwitchOffEdge = IM_COL32(92, 92,  92, 255);   // [-] its border #5c5c5c
constexpr ImU32 SwitchOn    = IM_COL32(131, 131, 131, 255);   // [-] :checked #838383
constexpr ImU32 SwitchOnEdge= IM_COL32(149, 149, 149, 255);   // [-] its border #959595
constexpr ImU32 KnobOff     = IM_COL32(164, 164, 164, 255);   // [-] its knob #a4a4a4
constexpr ImU32 KnobOn      = IM_COL32(225, 225, 225, 255);   // [-] :checked knob #e1e1e1
constexpr ImU32 Divider     = IM_COL32( 73,  73,  73, 255);   // [-] .bar-divider #494949

constexpr ImU32 BoundsInk   = IM_COL32(255, 255, 255,  56);   // [-] .domain-bounds stroke
constexpr ImU32 LatticeInk  = IM_COL32(255, 255, 255,  20);   // [-] the voxel lattice inside it
constexpr ImU32 RulerInk    = IM_COL32( 94,  94,  94,  59);   // [-] .ruler #5e5e5e3b
constexpr ImU32 PlayheadInk = IM_COL32(226, 226, 226, 255);   // [-] .playhead

//------------------------------------------------------------------------------------------------------------------------
//                                                   BAND AND PANE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr float HeaderBar    = 54.0f;   // [px] .app-header height
constexpr float HeaderPadX   = 18.0f;   // [px] its padding
constexpr float HeaderGap    = 36.0f;   // [px] its gap
constexpr float BrandGap     = 10.0f;   // [px] .brand gap
constexpr float BrandSize    = 15.0f;   // [px] the diamond
constexpr float BrandName    = 12.0f;   // [px] the wordmark
constexpr float NavSize      = 11.0f;   // [px] .nav-button
constexpr float NavGap       = 18.0f;   // [px] between the two
constexpr float TagSize      =  9.0f;   // [px] .gas-tag
constexpr float TagTrack      = 0.6f;   // [px] its letter-spacing

constexpr float DocBar       = 36.0f;   // [px] .document-bar height
constexpr float DocPadX      = 18.0f;   // [px] its padding
constexpr float DocGap       =  7.0f;   // [px] its gap
constexpr float DocSize      = 11.0f;   // [px] its type
constexpr float DirtyDot     =  5.0f;   // [px] .dirty-indicator

constexpr float LeftWide     = 236.0f;  // [px] .workspace grid column 1
constexpr float RightWide    = 310.0f;  // [px] column 3
constexpr float CentreMin    = 320.0f;  // [px] minmax(320px, 1fr)
constexpr float LeftWideBig  = 258.0f;  // [px] @media (min-width: 1600px)
constexpr float RightWideBig = 330.0f;  // [px] ditto
constexpr float LeftWideSnug = 205.0f;  // [px] @media (max-width: 1200px)
constexpr float RightWideSnug= 277.0f;  // [px] ditto
constexpr float CentreMinSnug= 310.0f;  // [px] ditto
constexpr float LeftWideTight= 175.0f;  // [px] @media (max-width: 960px)
constexpr float RightWideTight=245.0f;  // [px] ditto
constexpr float CentreMinTight=260.0f;  // [px] ditto
constexpr float RightWideBare= 230.0f;  // [px] @media (max-width: 760px), where the left pane goes

constexpr float PaneHead     = 41.0f;   // [px] .panel-heading height
constexpr float PaneHeadPadX = 15.0f;   // [px] its padding
constexpr float PaneHeadSize = 10.0f;   // [px] its font-size
constexpr float PaneHeadTrack=  1.4f;   // [px] its letter-spacing
constexpr float PaneHeadGap  =  8.0f;   // [px] its gap
constexpr float CountSize    =  9.0f;   // [px] .count
constexpr float CountTall    = 14.0f;   // [px] its line-height
constexpr float CountWide    = 16.0f;   // [px] its min-width

constexpr float StatRow      = 20.0f;   // [px] .scene-stat line box
constexpr float StatSize     = 10.0f;   // [px] its type
constexpr float SearchPadX   = 13.0f;   // [px] .search margin left / right
constexpr float SearchLift   =  3.0f;   // [px] its margin-top
constexpr float SearchDrop   = 12.0f;   // [px] its margin-bottom
constexpr float SearchPadIn  =  9.0f;   // [px] its padding-x
constexpr float SearchTall   = 29.0f;   // [px] 7 + 13 line + 7 + 2 border
constexpr float SearchRound  =  5.0f;   // [px] its border-radius
constexpr float SearchSize   = 11.0f;   // [px] its input

constexpr float FilterPadX   = 13.0f;   // [px] .filter-row padding
constexpr float FilterDrop   = 12.0f;   // [px] its padding-bottom
constexpr float FilterGap    =  6.0f;   // [px] its gap
constexpr float FilterPadIn  =  9.0f;   // [px] a button's padding-x
constexpr float FilterPadY   =  4.0f;   // [px] its padding-y
constexpr float FilterSize   = 10.0f;   // [px] its type

constexpr float ToolRowTop   = 12.0f;   // [px] .scene-toolbar padding-top
constexpr float ToolRowPadX  = 14.0f;   // [px] its padding-x
constexpr float ToolRowDrop  =  9.0f;   // [px] its padding-bottom
constexpr float ToolRowSize  = 11.0f;   // [px] its type

constexpr float TreePadX     =  8.0f;   // [px] .scene-tree padding
constexpr float SceneRowTall = 33.0f;   // [px] .scene-row height
constexpr float SceneRowPadL = 20.0f;   // [px] its padding-left
constexpr float SceneRowPadR =  9.0f;   // [px] its padding-right
constexpr float SceneRowGap  =  8.0f;   // [px] its gap
constexpr float SceneRowSize = 11.0f;   // [px] its label
constexpr float SceneGlyph   = 15.0f;   // [px] its svg
constexpr float SceneBadge   =  9.0f;   // [px] .row-badge
constexpr float FootnoteSize =  9.0f;   // [px] .scene-footnote
constexpr float FootnotePadX = 15.0f;   // [px] its padding-x
constexpr float FootnoteTop  =  7.0f;   // [px] its padding-top
constexpr float FootnoteDrop = 15.0f;   // [px] its padding-bottom

constexpr float PresetPadX   = 12.0f;   // [px] .preset-list padding-x
constexpr float PresetCardPad=  7.0f;   // [px] .preset-card padding-x
constexpr float PresetCardPadY= 9.0f;   // [px] its padding-y
constexpr float PresetCardPadBig = 10.0f; // [px] at 1600 and up
constexpr float PresetCardGap= 10.0f;   // [px] its gap
constexpr float PresetCardDrop= 5.0f;   // [px] its margin-bottom
constexpr float PresetRound  =  5.0f;   // [px] its border-radius
constexpr float SwatchWide   = 39.0f;   // [px] .preset-swatch
constexpr float SwatchTall   = 43.0f;   // [px] ditto
constexpr float SwatchRound  =  4.0f;   // [px] ditto
constexpr float PresetName   = 11.0f;   // [px] its strong
constexpr float PresetNote   =  9.0f;   // [px] its small
constexpr float PresetCopyGap=  6.0f;   // [px] between them
constexpr float PresetArrow  = 14.0f;   // [px] .preset-arrow
constexpr float LeftFootTall = 33.0f;   // [px] .left-footer min-height
constexpr float LeftFootSize =  9.0f;   // [px] its type

constexpr float ViewBar      = 41.0f;   // [px] .viewport-bar height
constexpr float ViewBarPadX  = 12.0f;   // [px] its padding
constexpr float ViewBarGap   =  7.0f;   // [px] its gap
constexpr float ViewBarSize  = 11.0f;   // [px] its strong
constexpr float ViewSelect   = 10.0f;   // [px] its select
constexpr float DividerTall  = 14.0f;   // [px] .bar-divider
constexpr float ViewStageMin = 180.0f;  // [px] .viewport min-height
constexpr float CaptionPad   = 14.0f;   // [px] .viewport-caption inset
constexpr float CaptionSize  = 11.0f;   // [px] its object name
constexpr float CaptionNote  =  9.0f;   // [px] its subtitle
constexpr float PillTall     = 18.0f;   // [px] .live-pill
constexpr float ToolTile     = 28.0f;   // [px] .viewport-tools button
constexpr float ToolTileGap  =  4.0f;   // [px] between them
constexpr float HudSize      =  9.0f;   // [px] .live-metric
constexpr float HelpSize     =  9.0f;   // [px] .viewport-help
constexpr float GizmoBox     = 68.0f;   // [px] .axis-gizmo

constexpr float MoveBar      = 48.0f;   // [px] .transport height
constexpr float MoveBarPadX  = 13.0f;   // [px] its padding
constexpr float MoveBarGap   = 10.0f;   // [px] its gap
constexpr float PlayTile     = 29.0f;   // [px] .play-button
constexpr float StepTile     = 24.0f;   // [px] the two beside it
constexpr float TileGap      =  3.0f;   // [px] .transport-buttons gap
constexpr float TimecodeSize = 11.0f;   // [px] .timecode

constexpr float TimeBand     = 88.0f;   // [px] .timeline height
constexpr float TimeLabelTall= 25.0f;   // [px] .timeline-label
constexpr float TimeLabelSize=  8.0f;   // [px] its type
constexpr float TimeLabelTrack= 1.2f;   // [px] its letter-spacing
constexpr float RulerPadX    = 17.0f;   // [px] .ruler margin-x
constexpr float RulerDrop    = 11.0f;   // [px] its margin-bottom
constexpr float RulerTick    = 30.0f;   // [-]  3.333% -- one tick per thirtieth
constexpr float SourceTrack  = 22.0f;   // [px] .source-track
constexpr float SourceSize   =  9.0f;   // [px] its label

constexpr float ObjectPadTop = 16.0f;   // [px] .object-heading padding-top
constexpr float ObjectPadX   = 14.0f;   // [px] its padding-x
constexpr float ObjectPadBot = 17.0f;   // [px] its padding-bottom
constexpr float ObjectGap    = 10.0f;   // [px] its gap
constexpr float SymbolWide   = 34.0f;   // [px] .object-symbol
constexpr float SymbolTall   = 36.0f;   // [px] ditto
constexpr float ObjectName   = 13.0f;   // [px] its input
constexpr float ObjectNameDrop= 5.0f;   // [px] that input's margin-bottom
constexpr float ObjectType   =  8.0f;   // [px] #object-type
constexpr float ObjectTypeTrack= 1.0f;  // [px] its letter-spacing
constexpr float SwitchWide   = 25.0f;   // [px] .switch > span
constexpr float SwitchTall   = 14.0f;   // [px] ditto
constexpr float KnobBox      =  8.0f;   // [px] its knob
constexpr float KnobInset    =  2.0f;   // [px] its offset
constexpr float KnobTravel   = 11.0f;   // [px] translateX when checked

constexpr float TabBand      = 33.0f;   // [px] .inspector-tabs height
constexpr float TabPadX      = 12.0f;   // [px] its padding
constexpr float TabGap       = 16.0f;   // [px] its gap
constexpr float TabSize      = 10.0f;   // [px] a tab
constexpr float TabRule      =  2.0f;   // [px] the active tab's underline

constexpr float GroupPadX    = 15.0f;   // [px] .property-group summary padding-x
constexpr float GroupPadY    = 14.0f;   // [px] its padding-y
constexpr float GroupSize    = 11.0f;   // [px] its type
constexpr float GroupGap     =  7.0f;   // [px] its gap
constexpr float GroupMark    = 15.0f;   // [px] its chevron
constexpr float BadgeSize    =  8.0f;   // [px] .section-badge
constexpr float BadgeTrack   =  0.5f;   // [px] its letter-spacing
constexpr float ContentDrop  = 15.0f;   // [px] .group-content padding-bottom
constexpr float ContentDropBig= 18.0f;  // [px] at 1600 and up
constexpr float HintSize     =  9.0f;   // [px] .property-hint
constexpr float HintDrop     = 13.0f;   // [px] its margin-bottom
constexpr float HintLead     =  1.7f;   // [-]  its line-height
constexpr float FieldWide    = 88.0f;   // [px] .property-row second column
constexpr float FieldGap     =  9.0f;   // [px] its gap
constexpr float FieldTall    = 25.0f;   // [px] a number input
constexpr float FieldRound   =  4.0f;   // [px] its border-radius
constexpr float FieldSize    = 10.0f;   // [px] its type
constexpr float SelectTall   = 27.0f;   // [px] .property-row select
constexpr float RangeTall    =  3.0f;   // [px] the slider track
constexpr float RangeLift    =  1.0f;   // [px] its margin-top
constexpr float RangeDrop    =  2.0f;   // [px] its margin-bottom
constexpr float RangeThumb   =  8.0f;   // [px] its thumb
constexpr float RowDrop      = 12.0f;   // [px] .property-row margin-bottom
constexpr float RowDropBig   = 15.0f;   // [px] at 1600 and up
constexpr float LabelSize    = 10.0f;   // [px] .property-label
constexpr float FootBand     = 34.0f;   // [px] .inspector-footer
constexpr float FootSize     =  9.0f;   // [px] its type

constexpr float StatusBar    = 26.0f;   // [px] .status-bar height
constexpr float StatusPadX   = 13.0f;   // [px] its padding
constexpr float StatusGap    = 11.0f;   // [px] its gap
constexpr float StatusSize   =  9.0f;   // [px] its type

constexpr float ProseLead    =  1.7f;   // [-]  line-height wherever prose wraps

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MODEL
//------------------------------------------------------------------------------------------------------------------------

enum class ObjectKind : uint8_t
{
    Domain   = 0,   // [-] the Eulerian volume itself
    Emitter  = 1,   // [-] a continuous source inside it
    Collider = 2,   // [-] a signed-distance obstruction
    Sun      = 3,   // [-] the one directional light
};

enum class InspectorTab : uint8_t { Source = 0, Simulation = 1, Rendering = 2 };

enum class PresetFilter : uint8_t { All = 0, Fire = 1, Smoke = 2, Blast = 3 };

enum class CameraView : uint8_t { Perspective = 0, Front = 1, Side = 2, Top = 3 };

struct SceneRow
{
    const char* Name    = "";                      // [-] what the row prints
    ObjectKind  Kind    = ObjectKind::Domain;      // [-] which glyph and which inspector
    bool        Enabled = true;                    // [-] the row's own switch
    const char* Badge   = "";                      // [-] the right-hand note, empty for none
};

constexpr uint32_t SceneRowCeiling = 12u;          // [-] what the pane paints before it stops

struct Subject
{
    //--- the document -----------------------------------------------------------------------------------
    const char*  DocumentName   = "Pyro plume";    // [-]  #document-name
    bool         Unsaved        = false;           // [-]  the dirty dot
    const char*  PresetIdentity = "ue5_pyro_default";  // [-] which rail card reads as loaded
    PresetFilter Filter         = PresetFilter::All;   // [-] which rail filter is pressed

    //--- the scene --------------------------------------------------------------------------------------
    SceneRow     Rows[SceneRowCeiling] = {};       // [-]
    uint32_t     RowCount       = 0u;              // [-]
    uint32_t     Chosen         = 0u;              // [-]  the selected row

    //--- what is being edited ---------------------------------------------------------------------------
    GasSettings  Settings;                         // [-]  every value the inspector prints
    InspectorTab Open           = InspectorTab::Source;  // [-]

    //--- the viewport and the transport -----------------------------------------------------------------
    CameraView   Camera         = CameraView::Perspective;  // [-]
    int32_t      Channel        = 0;               // [-]  an index into DebugChannels()
    bool         Running        = false;           // [-]  the play button's face
    float        Elapsed        = 0.0f;            // [s]  the timecode and the playhead
    float        Span           = 10.0f;           // [s]  what the ruler covers
    uint32_t     Advances       = 0u;              // [-]  #frame-counter
    float        Hertz          = 0.0f;            // [Hz] the HUD's rate; 0 prints an em dash
    bool         BoundsShown    = true;            // [-]  showBoundingBox, as the overlay button leaves it
    bool         LatticeShown   = false;           // [-]  showVoxelGridLines
    const char*  Backend        = "WebGL2";        // [-]  the status bar's renderer
    const char*  Readiness      = "Ready";         // [-]  #status-ready
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      DERIVED TEXT
//------------------------------------------------------------------------------------------------------------------------
// FluidPanel.js derives these in Refresh() and ConstructInspector(). They live here, not in the painter,
//    so the harness can pin the string rather than a picture of the string.

inline const char* KindName(ObjectKind Kind) noexcept
{
    switch (Kind)
    {
        case ObjectKind::Domain:   return "EULERIAN VOLUME";
        case ObjectKind::Emitter:  return "CONTINUOUS SOURCE";
        case ObjectKind::Collider: return "SIGNED DISTANCE COLLIDER";
        default:                   return "DIRECTIONAL LIGHT";
    }
}

inline const char* FilterName(PresetFilter Which) noexcept
{
    switch (Which)
    {
        case PresetFilter::Fire:  return "fire";
        case PresetFilter::Smoke: return "smoke";
        case PresetFilter::Blast: return "blast";
        default:                  return "all";
    }
}

inline const char* CameraName(CameraView Which) noexcept
{
    switch (Which)
    {
        case CameraView::Front: return "Front";
        case CameraView::Side:  return "Side";
        case CameraView::Top:   return "Top";
        default:                return "Perspective";
    }
}

inline const char* TabName(InspectorTab Which) noexcept
{
    switch (Which)
    {
        case InspectorTab::Simulation: return "Simulation";
        case InspectorTab::Rendering:  return "Rendering";
        default:                       return "Source";
    }
}

/// 📦 Whether a preset card survives the pressed filter. Search is the host's business, not the page's.
inline bool Shows(const PresetCard& Card, PresetFilter Which) noexcept
{
    return Which == PresetFilter::All || std::strcmp(Card.Category, FilterName(Which)) == 0;
}

inline uint32_t EnabledCount(const Subject& Scene) noexcept
{
    uint32_t Count = 0u;
    for (uint32_t Index = 0u; Index < Scene.RowCount; ++Index) Count += Scene.Rows[Index].Enabled ? 1u : 0u;
    return Count;
}

/// 📦 "00:00.000" — the browser's own timecode, minutes, seconds and milliseconds.
inline void Timecode(float Seconds, char* Out, size_t Room)
{
    const float Held   = Seconds < 0.0f ? 0.0f : Seconds;
    const int   Whole  = int(Held);
    const int   Rest   = int((Held - float(Whole)) * 1000.0f + 0.5f);
    const int Minutes = (Whole / 60) % 100, Ticks = Whole % 60;
    std::snprintf(Out, Room, "%02d:%02d.%03d", Minutes, Ticks, Rest > 999 ? 999 : Rest);
}

/// 📦 "110,592 voxels" — the status bar's count, grouped the way toLocaleString groups it.
inline void VoxelCount(int32_t Resolution, char* Out, size_t Room)
{
    const long long Total = (long long)Resolution * Resolution * Resolution;
    char Plain[24];
    std::snprintf(Plain, sizeof(Plain), "%lld", Total);
    const int Digits = int(std::strlen(Plain));
    char Grouped[32];
    size_t Write = 0u;
    for (int Index = 0; Index < Digits && Write + 2u < sizeof(Grouped); ++Index)
    {
        if (Index > 0 && (Digits - Index) % 3 == 0) Grouped[Write++] = ',';
        Grouped[Write++] = Plain[Index];
    }
    Grouped[Write] = 0;
    std::snprintf(Out, Room, "%.*s voxels", int(Write), Grouped);
}

/// 📦 "1.9 × 2.1 × 1.9 m" — #bounds-status. Width is both X and Z; only the height differs.
inline void BoundsReading(const GasSettings& Settings, char* Out, size_t Room)
{
    std::snprintf(Out, Room, "%.1f \xc3\x97 %.1f \xc3\x97 %.1f m", double(Settings.BoundsWidth),
                  double(Settings.BoundsHeight), double(Settings.BoundsWidth));
}

/// 📦 "48³ GRID" — the viewport's right-hand reading, with the lattice's own superscript three.
inline void LatticeReading(int32_t Resolution, char* Out, size_t Room)
{
    std::snprintf(Out, Room, "%d\xc2\xb3 LATTICE", Resolution);
}

/// 📦 The caption under the viewport: what is selected and how it is being drawn.
inline void StageCaption(const Subject& Scene, char* Out, size_t Room)
{
    const ControlChoice* Channels = DebugChannels();
    const uint32_t Index = Scene.Channel >= 0 && uint32_t(Scene.Channel) < DebugChannelCount
                           ? uint32_t(Scene.Channel) : 0u;
    if (Index == 0u) { std::snprintf(Out, Room, "Eulerian volume \xc2\xb7 physically shaded"); return; }
    std::snprintf(Out, Room, "Eulerian volume \xc2\xb7 %s", Channels[Index].Label);
}

/// 📦 The source track's label: what the emitter is doing over the ruler's span.
inline const char* SourceReading(const Subject& Scene) noexcept
{
    if (!Scene.Settings.EmitterEnabled) return "No continuous emission";
    return Scene.Settings.ShrapnelEnabled ? "Continuous emission \xc2\xb7 burst armed" : "Continuous emission";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INSPECTOR GROUPS
//------------------------------------------------------------------------------------------------------------------------
// ConstructInspector() builds these three tabs. The group order, the badges and which groups open closed
//    are the browser's; the keys are its arrays, read back out of FluidEditorControls.h for their limits.

constexpr uint32_t GroupKeyCeiling = 8u;
constexpr uint32_t GroupCeiling    = 8u;

struct PropertyGroup
{
    const char* Title   = "";                      // [-] the summary line
    const char* Badge   = "";                      // [-] the right-hand eyebrow
    bool        Opened  = true;                    // [-] <details open>
    const char* Hint    = "";                      // [-] the paragraph above the rows, empty for none
    const char* Keys[GroupKeyCeiling] = {};        // [-]
    uint32_t    KeyCount = 0u;                     // [-]
};

struct GroupListing
{
    PropertyGroup Groups[GroupCeiling] = {};
    uint32_t      Count = 0u;
};

namespace Detail
{
inline void Add(GroupListing& Listing, const char* Title, const char* Badge, bool Opened, const char* Hint,
                std::initializer_list<const char*> Keys)
{
    if (Listing.Count >= GroupCeiling) return;
    PropertyGroup& Group = Listing.Groups[Listing.Count++];
    Group.Title = Title; Group.Badge = Badge; Group.Opened = Opened; Group.Hint = Hint;
    for (const char* Key : Keys)
    {
        if (Group.KeyCount >= GroupKeyCeiling) break;
        Group.Keys[Group.KeyCount++] = Key;
    }
}

inline constexpr const char* ColliderHint =
    "Position is normalised within the domain. The collider changes the simulated flow without restarting it.";
inline constexpr const char* EmissionHint =
    "A continuous source at the domain\xe2\x80\x99s X/Z centre. Height and radius are live simulation parameters.";
inline constexpr const char* LatticeHint =
    "Changing voxel resolution rebuilds the grid and restarts the simulation. Other properties update live.";
}   // namespace Detail

/// 📦 The groups one tab shows for one kind of object.
/// in    Open      [-]  which tab is pressed
/// in    Kind      [-]  what is selected; a collider and the sun each promote their own group to the top
/// out   GroupListing   [-]  at most GroupCeiling groups, in the browser's order
/// cost  ✔️
inline GroupListing InspectorGroups(InspectorTab Open, ObjectKind Kind)
{
    using Detail::Add;
    GroupListing Listing;

    if (Open == InspectorTab::Source)
    {
        if (Kind == ObjectKind::Collider)
            Add(Listing, "Collider", "SDF", true, Detail::ColliderHint,
                { "obstacleType", "obstacleX", "obstacleY", "obstacleZ", "obstacleRadius",
                  "colliderAutoMove", "colliderSpeed" });
        else
        {
            Add(Listing, "Emission", "SOURCE", true, Detail::EmissionHint,
                { "emitterEnabled", "emitterRate", "emitterRadius", "emitterHeight" });
            Add(Listing, "Fuel & temperature", "INJECTION", true, "",
                { "emitterTemperature", "emitterFuel", "emitterSmoke" });
            Add(Listing, "Velocity", "FLOW", true, "", { "emitterUpwardVelocity", "emitterSwirl" });
        }
        Add(Listing, "Burst generator", "IMPULSE", false, "",
            { "shrapnelEnabled", "blastStrength", "blastRadius", "blastTemperature", "blastFuel",
              "blastSmoke", "blastLobes" });
        if (Kind != ObjectKind::Collider)
            Add(Listing, "Collider", "SDF", false, "",
                { "obstacleType", "obstacleX", "obstacleY", "obstacleZ", "obstacleRadius",
                  "colliderAutoMove", "colliderSpeed" });
        return Listing;
    }

    if (Open == InspectorTab::Simulation)
    {
        Add(Listing, "Domain", "3D", true, "",
            { "boundsWidth", "boundsHeight", "dynamicBounds", "dynamicBoundsMax" });
        Add(Listing, "Voxel lattice", "SOLVER", true, Detail::LatticeHint,
            { "gridResolution", "pressureIterations", "macCormackAdvection", "enclosedBox", "timeScale" });
        Add(Listing, "Forces & turbulence", "FLOW", false, "",
            { "vorticityConfinement", "buoyancy", "smokeWeight", "turbulenceStrength", "turbulenceScale",
              "windX", "windZ", "velocityDamping" });
        Add(Listing, "Combustion", "THERMAL", false, "",
            { "burnRate", "burnHeat", "sootGeneration", "combustionExpansion", "coolingRate",
              "smokeDissipation" });
        return Listing;
    }

    if (Kind == ObjectKind::Sun)
        Add(Listing, "Directional light", "LIGHT", true, "",
            { "sunIntensity", "sunAzimuth", "sunElevation", "ambientIntensity" });
    Add(Listing, "Volume shading", "VOLUME", true, "",
        { "colorPalette", "fireIntensity", "densityExtinction", "smokeAlbedo", "exposure" });
    if (Kind != ObjectKind::Sun)
        Add(Listing, "Directional light", "LIGHT", false, "",
            { "sunIntensity", "sunAzimuth", "sunElevation", "ambientIntensity" });
    Add(Listing, "Scattering & glow", "OPTICS", false, "",
        { "bloomIntensity", "godRaysIntensity", "shockwaveStrength", "temperatureScale",
          "internalScattering", "shadowDensity", "phaseAnisotropy" });
    Add(Listing, "Render quality", "GPU", false, "",
        { "raymarchSteps", "shadowSteps", "renderScale", "autoGpuGovernor", "voxelQuantization" });
    Add(Listing, "Embers & ash", "PARTICLES", false, "",
        { "showEmbers", "emberCount", "emberSize", "emberIntensity", "emberLifetime", "emberAshiness" });
    return Listing;
}

/// 📦 FormatNumber(Value, Step): a whole step prints a whole number, a step finer than a hundredth prints
///    three decimals, and everything between prints two. Not "as many decimals as the step has" -- the
///    browser really does print 1.85 for a step of 0.05, and matching it matters more than tidiness.
inline void Reading(float Value, float Step, char* Out, size_t Room)
{
    const bool Whole  = Step >= 1.0f && Step == float(int(Step));
    const int  Places = Whole ? 0 : (Step < 0.01f ? 3 : 2);
    std::snprintf(Out, Room, "%.*f", Places, double(Value));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TEXT HELPERS
//------------------------------------------------------------------------------------------------------------------------

inline void Ink(ImDrawList* Draw, ImFont* Face, float X, float BoxTop, float BoxTall, float Size,
                ImU32 Colour, const char* Body, Kit::Anchor Side = Kit::Anchor::Start)
{
    Kit::Inked(Draw, Face, X, BoxTop + (BoxTall - Kit::Grind(Size)) * 0.5f + Kit::AscentShare * Size,
               Size, Colour, Body, Side);
}

inline void TrackedInk(ImDrawList* Draw, ImFont* Face, float X, float BoxTop, float BoxTall, float Size,
                       ImU32 Colour, const char* Body, float Extra)
{
    Kit::Tracked(Draw, Face, X, BoxTop + (BoxTall - Kit::Grind(Size)) * 0.5f, Size, Colour, Body, Extra);
}

inline void TrackedRight(ImDrawList* Draw, ImFont* Face, float Right, float BoxTop, float BoxTall,
                         float Size, ImU32 Colour, const char* Body, float Extra)
{
    TrackedInk(Draw, Face, Right - Kit::TrackedWidth(Face, Size, Body, Extra), BoxTop, BoxTall, Size,
               Colour, Body, Extra);
}

inline float ProseHeight(ImFont* Face, float Size, float Wide, const char* Body)
{
    Kit::TrackedLine Lines[12];
    return float(ImMax(1, Kit::TrackedWrap(Face, Size, Body, 0.0f, Wide, Lines, 12))) * Size * ProseLead;
}

inline float PaintProse(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Size, float Wide, ImU32 Colour,
                        const char* Body)
{
    Kit::TrackedLine Lines[12];
    const int   Count   = ImMax(1, Kit::TrackedWrap(Face, Size, Body, 0.0f, Wide, Lines, 12));
    const float Leading = Size * ProseLead;
    for (int Index = 0; Index < Count; ++Index)
    {
        char Piece[256];
        const size_t Taken = ImMin(sizeof(Piece) - 1u, size_t(Lines[Index].To - Lines[Index].From));
        std::memcpy(Piece, Lines[Index].From, Taken);
        Piece[Taken] = 0;
        Ink(Draw, Face, Spot.x, Spot.y + float(Index) * Leading, Leading, Size, Colour, Piece);
    }
    return float(Count) * Leading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REUSABLE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ The editor face is DM Sans, which carries ×, ·, ³ and ↗ and no dingbats at all. Every other mark the
//    browser writes as a character -- the brand diamond, the disclosure chevrons, the viewport tools, the
//    transport's reset and step -- is drawn here rather than typed, because typing it prints a tofu box.

inline void PaintDiamond(ImDrawList* Draw, ImVec2 Heart, float Reach, ImU32 Colour)
{
    Draw->AddQuadFilled({ Heart.x, Heart.y - Reach }, { Heart.x + Reach, Heart.y },
                        { Heart.x, Heart.y + Reach }, { Heart.x - Reach, Heart.y }, Colour);
}

inline void PaintChevron(ImDrawList* Draw, ImVec2 Heart, float Reach, ImU32 Colour, bool Down)
{
    const float Drop = Down ? Reach * 0.5f : -Reach * 0.5f;
    Draw->AddLine({ Heart.x - Reach, Heart.y - Drop }, { Heart.x, Heart.y + Drop }, Colour, 1.2f);
    Draw->AddLine({ Heart.x, Heart.y + Drop }, { Heart.x + Reach, Heart.y - Drop }, Colour, 1.2f);
}

inline void PaintArc(ImDrawList* Draw, ImVec2 Heart, float Reach, ImU32 Colour)
{
    Draw->PathArcTo(Heart, Reach, Kit::Pi * 0.35f, Kit::Pi * 1.9f, 20);
    Draw->PathStroke(Colour, 0, 1.2f);
    Draw->AddTriangleFilled({ Heart.x + Reach * 0.55f, Heart.y - Reach * 1.05f },
                            { Heart.x + Reach * 1.25f, Heart.y - Reach * 0.75f },
                            { Heart.x + Reach * 0.45f, Heart.y - Reach * 0.35f }, Colour);
}

inline void PaintStar(ImDrawList* Draw, ImVec2 Heart, float Reach, ImU32 Colour)
{
    for (int Spoke = 0; Spoke < 6; ++Spoke)
    {
        const float Turn = float(Spoke) * (Kit::Pi / 3.0f);
        Draw->AddLine(Heart, { Heart.x + std::cos(Turn) * Reach, Heart.y + std::sin(Turn) * Reach },
                      Colour, 1.0f);
    }
}

inline void PaintSkipMark(ImDrawList* Draw, ImVec2 Heart, float Reach, ImU32 Colour)
{
    Draw->AddTriangleFilled({ Heart.x - Reach, Heart.y - Reach }, { Heart.x + Reach * 0.2f, Heart.y },
                            { Heart.x - Reach, Heart.y + Reach }, Colour);
    Draw->AddRectFilled({ Heart.x + Reach * 0.4f, Heart.y - Reach },
                        { Heart.x + Reach * 0.9f, Heart.y + Reach }, Colour);
}

inline void PaintPlate(ImDrawList* Draw, ImVec2 Spot, float Wide, float Tall, ImU32 Fill, ImU32 Edge,
                       float Round)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, Fill, Round);
    if (Edge) Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, Edge, Round, 0, 1.0f);
}

inline float ButtonWidth(ImFont* Face, float Size, const char* Body, float PadX)
{
    return Kit::Measured(Face, Size, Body) + PadX * 2.0f;
}

inline void PaintButton(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall, float Size,
                        const char* Body, bool Raised)
{
    PaintPlate(Draw, Spot, Wide, Tall, Raised ? ControlFill : ChosenFill,
               Raised ? ControlEdge : ChosenEdge, 5.0f);
    Ink(Draw, Face, Spot.x + Wide * 0.5f, Spot.y, Tall, Size, Raised ? StrongInk : BodyInk, Body,
        Kit::Anchor::Middle);
}

/// 📦 The .switch: a 25 × 14 pill with an 8 px knob that travels 11 px when it is on.
inline void PaintSwitch(ImDrawList* Draw, ImVec2 Spot, bool On)
{
    Draw->AddRectFilled(Spot, { Spot.x + SwitchWide, Spot.y + SwitchTall }, On ? SwitchOn : SwitchOff,
                        SwitchTall * 0.5f);
    Draw->AddRect(Spot, { Spot.x + SwitchWide, Spot.y + SwitchTall }, On ? SwitchOnEdge : SwitchOffEdge,
                  SwitchTall * 0.5f, 0, 1.0f);
    const float Left = Spot.x + KnobInset + (On ? KnobTravel : 0.0f);
    Draw->AddCircleFilled({ Left + KnobBox * 0.5f, Spot.y + KnobInset + KnobBox * 0.5f }, KnobBox * 0.5f,
                          On ? KnobOn : KnobOff, 16);
}

/// 📦 A select: a plate with the chosen option and a chevron.
inline void PaintSelect(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall, float Size,
                        const char* Body)
{
    PaintPlate(Draw, Spot, Wide, Tall, FieldFill, FieldEdge, FieldRound);
    Ink(Draw, Face, Spot.x + 7.0f, Spot.y, Tall, Size, StrongInk, Body);
    const float Mid = Spot.y + Tall * 0.5f;
    const float End = Spot.x + Wide - 9.0f;
    Draw->AddLine({ End - 6.0f, Mid - 2.0f }, { End - 3.0f, Mid + 2.0f }, QuietInk, 1.0f);
    Draw->AddLine({ End - 3.0f, Mid + 2.0f }, { End, Mid - 2.0f }, QuietInk, 1.0f);
}

/// 📦 The value pill: a right-aligned monospaced reading with its unit cell behind it.
inline void PaintPill(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Value,
                      const char* Unit)
{
    PaintPlate(Draw, Spot, Wide, FieldTall, FieldFill, ChosenEdge, FieldRound);
    const float UnitWide = Kit::Measured(Face, FieldSize, Unit);
    Ink(Draw, Face, Spot.x + Wide - 7.0f, Spot.y, FieldTall, FieldSize, FaintInk, Unit, Kit::Anchor::End);
    Ink(Draw, Face, Spot.x + Wide - 7.0f - UnitWide - 6.0f, Spot.y, FieldTall, FieldSize, StrongInk, Value,
        Kit::Anchor::End);
}

/// 📦 The 3 px track beneath a pill, filled to where the value sits between its limits.
inline void PaintTrack(ImDrawList* Draw, ImVec2 Spot, float Wide, float Share)
{
    const float Held = Share < 0.0f ? 0.0f : (Share > 1.0f ? 1.0f : Share);
    const float Mid  = Spot.y + RangeTall * 0.5f;
    Draw->AddRectFilled({ Spot.x, Spot.y }, { Spot.x + Wide, Spot.y + RangeTall }, TrackFill, 2.0f);
    if (Held > 0.0f)
        Draw->AddRectFilled({ Spot.x, Spot.y }, { Spot.x + Wide * Held, Spot.y + RangeTall }, TrackRun, 2.0f);
    const float Knob = Spot.x + Wide * Held;
    Draw->AddRectFilled({ Knob - RangeThumb * 0.5f, Mid - RangeThumb * 0.5f },
                        { Knob + RangeThumb * 0.5f, Mid + RangeThumb * 0.5f }, ThumbFill, 2.0f);
    Draw->AddRect({ Knob - RangeThumb * 0.5f, Mid - RangeThumb * 0.5f },
                  { Knob + RangeThumb * 0.5f, Mid + RangeThumb * 0.5f }, ThumbEdge, 2.0f, 0, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ROW HEIGHTS
//------------------------------------------------------------------------------------------------------------------------
// A property row is a two-column grid: the label, then an 88 px control. A range adds its track on a
//    second line spanning both columns; a select is taller than a number; a toggle is only the switch.

inline float ControlRowHeight(ControlKind Kind)
{
    switch (Kind)
    {
        case ControlKind::Toggle: return SwitchTall + 2.0f;
        case ControlKind::Choice: return SelectTall;
        default:                  return FieldTall + RangeLift + RangeTall + RangeDrop;
    }
}

inline float GroupSummaryHeight() { return GroupPadY * 2.0f + Kit::Grind(GroupSize); }

inline float GroupHeight(ImFont* Face, float PaneWide, const PropertyGroup& Group, float RowSpacing,
                         float ContentFoot)
{
    float Tall = GroupSummaryHeight();
    if (!Group.Opened) return Tall;
    const float Inner = PaneWide - GroupPadX * 2.0f;
    if (Group.Hint[0]) Tall += ProseHeight(Face, HintSize, Inner, Group.Hint) + HintDrop;
    for (uint32_t Index = 0u; Index < Group.KeyCount; ++Index)
    {
        const ControlRow* Control = ReadControl(Group.Keys[Index]);
        Tall += ControlRowHeight(Control ? Control->Kind : ControlKind::Range);
        if (Index + 1u < Group.KeyCount) Tall += RowSpacing;
    }
    return Tall + ContentFoot;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------
// #app is `height: 100dvh; display: flex; flex-direction: column`, so the header, the document bar and the
//    status bar take their fixed heights and .workspace absorbs the rest. The breakpoints are the
//    stylesheet's own: 1600, 1200, 960 and 760.

struct Layout
{
    float Wide = 0.0f, Tall = 0.0f;
    float HeaderTop = 0.0f, DocTop = 0.0f, WorkTop = 0.0f, WorkTall = 0.0f, StatusTop = 0.0f;
    float LeftX = 0.0f, LeftW = 0.0f;
    float CentreX = 0.0f, CentreW = 0.0f;
    float RightX = 0.0f, RightW = 0.0f;
    float StageTop = 0.0f, StageTall = 0.0f;   // [px] the viewport between its bar and the transport
    float MoveTop = 0.0f, TimeTop = 0.0f;
    float RowSpacing  = RowDrop;               // [px] .property-row margin-bottom
    float ContentFoot = ContentDrop;           // [px] .group-content padding-bottom
    float CardPad     = PresetCardPad;         // [px] .preset-card padding
    bool  ShowOutliner = true;                 // [-]  @media (max-width: 760px) drops the left pane
    bool  ShowTag      = true;                 // [-]  @media (max-width: 960px) drops the gas tag
};

inline Layout Measure(float Wide, float Tall)
{
    Layout Page;
    Page.Wide = Wide; Page.Tall = Tall;
    Page.HeaderTop = 0.0f;
    Page.DocTop    = HeaderBar;
    Page.WorkTop   = HeaderBar + DocBar;
    Page.StatusTop = Tall - StatusBar;
    Page.WorkTall  = ImMax(0.0f, Page.StatusTop - Page.WorkTop);

    float Left = LeftWide, Right = RightWide, CentreFloor = CentreMin;
    if (Wide >= 1600.0f)
    {
        Left = LeftWideBig; Right = RightWideBig;
        Page.RowSpacing = RowDropBig; Page.ContentFoot = ContentDropBig; Page.CardPad = PresetCardPadBig;
    }
    else if (Wide <= 760.0f)
    {
        Page.ShowOutliner = false; Page.ShowTag = false;
        Left = 0.0f; Right = RightWideBare; CentreFloor = CentreMinTight;
    }
    else if (Wide <= 960.0f)
    {
        Page.ShowTag = false;
        Left = LeftWideTight; Right = RightWideTight; CentreFloor = CentreMinTight;
    }
    else if (Wide <= 1200.0f)
    {
        Left = LeftWideSnug; Right = RightWideSnug; CentreFloor = CentreMinSnug;
    }

    Page.LeftX = 0.0f; Page.LeftW = Left;
    Page.CentreX = Left;
    Page.RightW = Right;
    Page.RightX = Wide - Right;
    Page.CentreW = ImMax(CentreFloor, Page.RightX - Page.CentreX);

    Page.TimeTop  = Page.StatusTop - TimeBand;
    Page.MoveTop  = Page.TimeTop - MoveBar;
    Page.StageTop = Page.WorkTop + ViewBar;
    Page.StageTall = ImMax(ViewStageMin, Page.MoveTop - Page.StageTop);
    return Page;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE BANDS
//------------------------------------------------------------------------------------------------------------------------

inline void PaintHeader(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                        const Subject& Scene)
{
    Draw->AddRectFilled({ 0, Page.HeaderTop }, { Page.Wide, Page.HeaderTop + HeaderBar }, HeaderFill);
    Draw->AddRectFilled({ 0, Page.HeaderTop + HeaderBar - 1.0f }, { Page.Wide, Page.HeaderTop + HeaderBar },
                        HeaderEdge);

    float Pen = HeaderPadX;
    PaintDiamond(Draw, { Pen + 6.0f, Page.HeaderTop + HeaderBar * 0.5f }, 6.0f, PageInk);
    Pen += 12.0f + BrandGap;
    Ink(Draw, Regular, Pen, Page.HeaderTop, HeaderBar, BrandName, PageInk, "Frontier");
    Pen += Kit::Measured(Regular, BrandName, "Frontier") + HeaderGap;

    const char* Nav[2] = { "Workspace", "Presets" };
    for (int Index = 0; Index < 2; ++Index)
    {
        const bool Live = Index == 0;
        Ink(Draw, Light, Pen, Page.HeaderTop, HeaderBar, NavSize, Live ? PageInk : QuietInk, Nav[Index]);
        const float Run = Kit::Measured(Light, NavSize, Nav[Index]);
        if (Live)
            Draw->AddRectFilled({ Pen, Page.HeaderTop + HeaderBar - 2.0f },
                                { Pen + Run, Page.HeaderTop + HeaderBar }, PageInk);
        Pen += Run + NavGap;
    }

    float Right = Page.Wide - HeaderPadX;
    const char* Export = "Export scene";
    const float ExportWide = ButtonWidth(Light, DocSize, Export, 12.0f);
    PaintButton(Draw, Light, { Right - ExportWide, Page.HeaderTop + (HeaderBar - 28.0f) * 0.5f }, ExportWide,
                28.0f, DocSize, Export, true);
    Right -= ExportWide + 10.0f;

    if (Page.ShowTag)
    {
        const char* Tag = "GAS SIMULATION";
        TrackedRight(Draw, Light, Right, Page.HeaderTop, HeaderBar, TagSize, QuietInk, Tag, TagTrack);
        const float TagWide = Kit::TrackedWidth(Light, TagSize, Tag, TagTrack);
        Draw->AddCircleFilled({ Right - TagWide - 9.0f, Page.HeaderTop + HeaderBar * 0.5f }, 2.5f, LiveInk, 12);
    }
}

inline void PaintDocumentBar(ImDrawList* Draw, ImFont* Light, const Layout& Page, const Subject& Scene)
{
    Draw->AddRectFilled({ 0, Page.DocTop }, { Page.Wide, Page.DocTop + DocBar }, DocFill);
    Draw->AddRectFilled({ 0, Page.DocTop + DocBar - 1.0f }, { Page.Wide, Page.DocTop + DocBar }, Line);

    float Pen = DocPadX;
    Ink(Draw, Light, Pen, Page.DocTop, DocBar, DocSize, BodyInk, Scene.DocumentName);
    Pen += Kit::Measured(Light, DocSize, Scene.DocumentName) + 2.0f;
    Ink(Draw, Light, Pen, Page.DocTop, DocBar, DocSize, FaintInk, ".fluid");
    Pen += Kit::Measured(Light, DocSize, ".fluid") + DocGap;
    if (Scene.Unsaved)
    {
        Draw->AddCircleFilled({ Pen + DirtyDot * 0.5f, Page.DocTop + DocBar * 0.5f }, DirtyDot * 0.5f,
                              AmberInk, 12);
        Pen += DirtyDot + DocGap;
    }
    Ink(Draw, Light, Pen, Page.DocTop, DocBar, DocSize, FaintInk, "Smoke / fire authoring");

    float Right = Page.Wide - DocPadX;
    Ink(Draw, Light, Right, Page.DocTop, DocBar, DocSize, QuietInk, "Import scene", Kit::Anchor::End);
    Right -= Kit::Measured(Light, DocSize, "Import scene") + DocGap * 2.0f;
    Ink(Draw, Light, Right, Page.DocTop, DocBar, DocSize, FaintInk, "Samples", Kit::Anchor::End);
}

inline void PaintStatusBar(ImDrawList* Draw, ImFont* Light, const Layout& Page, const Subject& Scene)
{
    Draw->AddRectFilled({ 0, Page.StatusTop }, { Page.Wide, Page.Tall }, HeaderFill);
    Draw->AddRectFilled({ 0, Page.StatusTop }, { Page.Wide, Page.StatusTop + 1.0f }, StatusEdge);

    float Pen = StatusPadX;
    Draw->AddCircleFilled({ Pen + 2.5f, Page.StatusTop + StatusBar * 0.5f }, 2.5f, LiveInk, 12);
    Pen += 5.0f + 6.0f;
    Ink(Draw, Light, Pen, Page.StatusTop, StatusBar, StatusSize, QuietInk, Scene.Readiness);
    Pen += Kit::Measured(Light, StatusSize, Scene.Readiness) + StatusGap;

    char Voxels[48];
    VoxelCount(Scene.Settings.LatticeResolution, Voxels, sizeof(Voxels));
    Ink(Draw, Light, Pen, Page.StatusTop, StatusBar, StatusSize, QuietInk, Voxels);
    Pen += Kit::Measured(Light, StatusSize, Voxels) + StatusGap;

    char Bounds[48];
    BoundsReading(Scene.Settings, Bounds, sizeof(Bounds));
    Ink(Draw, Light, Pen, Page.StatusTop, StatusBar, StatusSize, QuietInk, Bounds);

    float Right = Page.Wide - StatusPadX;
    Ink(Draw, Light, Right, Page.StatusTop, StatusBar, StatusSize, FaintInk, "FRONTIER / FLUID 0.1",
        Kit::Anchor::End);
    Right -= Kit::Measured(Light, StatusSize, "FRONTIER / FLUID 0.1") + StatusGap;
    Ink(Draw, Light, Right, Page.StatusTop, StatusBar, StatusSize, QuietInk, Scene.Backend, Kit::Anchor::End);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LEFT PANE
//------------------------------------------------------------------------------------------------------------------------

inline float PaintPaneHeading(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Title,
                              const char* Count, bool Ruled)
{
    TrackedInk(Draw, Light, Spot.x + PaneHeadPadX, Spot.y, PaneHead, PaneHeadSize, HeadInk, Title,
               PaneHeadTrack);
    if (Count && Count[0])
    {
        const float Run = ImMax(CountWide, Kit::Measured(Light, CountSize, Count) + 8.0f);
        const float Left = Spot.x + PaneHeadPadX
                         + Kit::TrackedWidth(Light, PaneHeadSize, Title, PaneHeadTrack) + PaneHeadGap;
        PaintPlate(Draw, { Left, Spot.y + (PaneHead - CountTall) * 0.5f }, Run, CountTall, ChosenFill, 0, 3.0f);
        Ink(Draw, Light, Left + Run * 0.5f, Spot.y + (PaneHead - CountTall) * 0.5f, CountTall, CountSize,
            QuietInk, Count, Kit::Anchor::Middle);
    }
    if (Ruled)
        Draw->AddRectFilled({ Spot.x, Spot.y + PaneHead - 1.0f }, { Spot.x + Wide, Spot.y + PaneHead },
                            PaneEdge);
    return PaneHead;
}

inline void PaintSearch(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Placeholder,
                        const char* Hint)
{
    PaintPlate(Draw, Spot, Wide, SearchTall, FieldFill, FieldEdge, SearchRound);
    Ink(Draw, Light, Spot.x + SearchPadIn, Spot.y, SearchTall, SearchSize, FaintInk, Placeholder);
    if (Hint && Hint[0])
    {
        const float Run = Kit::Measured(Light, CountSize, Hint) + 8.0f;
        PaintPlate(Draw, { Spot.x + Wide - SearchPadIn - Run, Spot.y + (SearchTall - 14.0f) * 0.5f }, Run,
                   14.0f, 0, ChosenEdge, 3.0f);
        Ink(Draw, Light, Spot.x + Wide - SearchPadIn - Run * 0.5f, Spot.y + (SearchTall - 14.0f) * 0.5f,
            14.0f, CountSize, FaintInk, Hint, Kit::Anchor::Middle);
    }
}

inline float PaintFilterRow(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, const char* const* Names,
                            uint32_t Count, uint32_t Pressed)
{
    const float Tall = Kit::Grind(FilterSize) + FilterPadY * 2.0f;
    float Pen = Spot.x;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const float Run = Kit::Measured(Light, FilterSize, Names[Index]) + FilterPadIn * 2.0f;
        if (Index == Pressed) PaintPlate(Draw, { Pen, Spot.y }, Run, Tall, ChosenFill, ChosenEdge, 4.0f);
        Ink(Draw, Light, Pen + Run * 0.5f, Spot.y, Tall, FilterSize,
            Index == Pressed ? StrongInk : QuietInk, Names[Index], Kit::Anchor::Middle);
        Pen += Run + FilterGap;
    }
    return Tall;
}

/// 📦 One outliner row: the glyph, the name, and either a badge or the row's own switch.
inline void PaintSceneRow(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const SceneRow& Row,
                          bool Selected)
{
    if (Selected) PaintPlate(Draw, Spot, Wide, SceneRowTall, RowChosen, RowEdge, 4.0f);

    const float Mid = Spot.y + SceneRowTall * 0.5f;
    const float GlyphX = Spot.x + SceneRowPadL - SceneGlyph;
    const ImU32 Accent = Row.Kind == ObjectKind::Emitter ? AmberInk
                       : Row.Kind == ObjectKind::Collider ? PaleInk
                       : Row.Kind == ObjectKind::Sun ? AmberInk : AzureInk;
    switch (Row.Kind)
    {
        case ObjectKind::Domain:
            Draw->AddRect({ GlyphX + 2.0f, Mid - 5.0f }, { GlyphX + 12.0f, Mid + 5.0f }, Accent, 1.0f, 0, 1.0f);
            break;
        case ObjectKind::Emitter:
            Draw->AddTriangleFilled({ GlyphX + 7.0f, Mid - 6.0f }, { GlyphX + 12.0f, Mid + 5.0f },
                                    { GlyphX + 2.0f, Mid + 5.0f }, Accent);
            break;
        case ObjectKind::Collider:
            Draw->AddCircle({ GlyphX + 7.0f, Mid }, 5.0f, Accent, 16, 1.0f);
            break;
        default:
            Draw->AddCircleFilled({ GlyphX + 7.0f, Mid }, 3.5f, Accent, 16);
            break;
    }

    Ink(Draw, Light, Spot.x + SceneRowPadL + SceneRowGap, Spot.y, SceneRowTall, SceneRowSize,
        Row.Enabled ? (Selected ? StrongInk : BodyInk) : FaintInk, Row.Name);

    if (Row.Badge[0])
        Ink(Draw, Light, Spot.x + Wide - SceneRowPadR, Spot.y, SceneRowTall, SceneBadge, QuietInk, Row.Badge,
            Kit::Anchor::End);
    else
        PaintSwitch(Draw, { Spot.x + Wide - SceneRowPadR - SwitchWide, Mid - SwitchTall * 0.5f }, Row.Enabled);
}

inline float PresetCardHeight(float CardPad)
{
    return ImMax(SwatchTall, Kit::Grind(PresetName) + PresetCopyGap + Kit::Grind(PresetNote))
         + CardPad * 2.0f;
}

inline void PaintPresetCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                            float CardPad, const PresetCard& Card, bool Loaded)
{
    const float Tall = PresetCardHeight(CardPad);
    PaintPlate(Draw, Spot, Wide, Tall, Loaded ? CardLive : CardBack, Loaded ? CardLiveEdge : CardEdge,
               PresetRound);

    const ImVec2 Swatch = { Spot.x + CardPad, Spot.y + (Tall - SwatchTall) * 0.5f };
    PaintPlate(Draw, Swatch, SwatchWide, SwatchTall, SwatchFill, SwatchEdge, SwatchRound);
    // The four glyphs the rail uses, drawn rather than imported: a flame, a burst, a cloud and a ring.
    const ImVec2 Heart = { Swatch.x + SwatchWide * 0.5f, Swatch.y + SwatchTall * 0.5f };
    if (std::strcmp(Card.Glyph, "flame") == 0)
    {
        Draw->AddTriangleFilled({ Heart.x, Heart.y - 9.0f }, { Heart.x + 6.0f, Heart.y + 7.0f },
                                { Heart.x - 6.0f, Heart.y + 7.0f }, BodyInk);
        Draw->AddTriangleFilled({ Heart.x, Heart.y - 1.0f }, { Heart.x + 3.0f, Heart.y + 7.0f },
                                { Heart.x - 3.0f, Heart.y + 7.0f }, SwatchFill);
    }
    else if (std::strcmp(Card.Glyph, "burst") == 0)
        for (int Spoke = 0; Spoke < 8; ++Spoke)
        {
            const float Turn = float(Spoke) * (Kit::Pi * 0.25f);
            Draw->AddLine({ Heart.x + std::cos(Turn) * 2.5f, Heart.y + std::sin(Turn) * 2.5f },
                          { Heart.x + std::cos(Turn) * 9.0f, Heart.y + std::sin(Turn) * 9.0f }, BodyInk, 1.0f);
        }
    else if (std::strcmp(Card.Glyph, "rotate") == 0 || std::strcmp(Card.Glyph, "orbit") == 0)
    {
        Draw->AddCircle(Heart, 8.0f, BodyInk, 24, 1.0f);
        Draw->AddCircleFilled({ Heart.x + 8.0f, Heart.y }, 2.0f, BodyInk, 12);
    }
    else if (std::strcmp(Card.Glyph, "box") == 0 || std::strcmp(Card.Glyph, "cylinder") == 0
             || std::strcmp(Card.Glyph, "wall") == 0)
        Draw->AddRect({ Heart.x - 7.0f, Heart.y - 7.0f }, { Heart.x + 7.0f, Heart.y + 7.0f }, BodyInk, 2.0f,
                      0, 1.0f);
    else
    {
        Draw->AddCircleFilled({ Heart.x - 4.0f, Heart.y + 2.0f }, 4.5f, BodyInk, 16);
        Draw->AddCircleFilled({ Heart.x + 3.0f, Heart.y + 1.0f }, 5.5f, BodyInk, 16);
    }

    const float CopyX = Swatch.x + SwatchWide + PresetCardGap;
    const float CopyTall = Kit::Grind(PresetName) + PresetCopyGap + Kit::Grind(PresetNote);
    float CopyY = Spot.y + (Tall - CopyTall) * 0.5f;
    Ink(Draw, Regular, CopyX, CopyY, Kit::Grind(PresetName), PresetName, PageInk, Card.Name);
    CopyY += Kit::Grind(PresetName) + PresetCopyGap;
    Ink(Draw, Light, CopyX, CopyY, Kit::Grind(PresetNote), PresetNote, QuietInk, Card.Summary);

    Ink(Draw, Light, Spot.x + Wide - CardPad, Spot.y, Tall, PresetArrow, FaintInk, "\xe2\x86\x97",
        Kit::Anchor::End);
}

inline void PaintOutlinerPane(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                              const Subject& Scene)
{
    if (!Page.ShowOutliner) return;
    const float Left = Page.LeftX, Wide = Page.LeftW;
    Draw->AddRectFilled({ Left, Page.WorkTop }, { Left + Wide, Page.StatusTop }, PanelFill);
    Draw->AddRectFilled({ Left + Wide - 1.0f, Page.WorkTop }, { Left + Wide, Page.StatusTop }, Line);

    char Nodes[32];
    std::snprintf(Nodes, sizeof(Nodes), "%u", Scene.RowCount);
    float Pen = Page.WorkTop;
    TrackedInk(Draw, Light, Left + PaneHeadPadX, Pen, PaneHead, PaneHeadSize, HeadInk, "OUTLINER",
               PaneHeadTrack);
    Ink(Draw, Light, Left + Wide - PaneHeadPadX, Pen, PaneHead, CountSize, FaintInk, Nodes, Kit::Anchor::End);
    Pen += PaneHead;

    char Tally[64];
    std::snprintf(Tally, sizeof(Tally), "Enabled %u", EnabledCount(Scene));
    Ink(Draw, Light, Left + PaneHeadPadX, Pen, StatRow, StatSize, QuietInk, Tally);
    std::snprintf(Tally, sizeof(Tally), "Disabled %u", Scene.RowCount - EnabledCount(Scene));
    Ink(Draw, Light, Left + Wide - PaneHeadPadX, Pen, StatRow, StatSize, FaintInk, Tally, Kit::Anchor::End);
    Pen += StatRow + SearchLift;

    PaintSearch(Draw, Light, { Left + SearchPadX, Pen }, Wide - SearchPadX * 2.0f, "Search scene", "/");
    Pen += SearchTall + SearchDrop;

    static const char* SceneFilters[4] = { "All", "Gas", "Geometry", "Lights" };
    Pen += PaintFilterRow(Draw, Light, { Left + FilterPadX, Pen }, SceneFilters, 4u, 0u) + FilterDrop;

    Pen += ToolRowTop;
    PaintChevron(Draw, { Left + ToolRowPadX + 4.0f, Pen + Kit::Grind(ToolRowSize) * 0.5f }, 4.0f, QuietInk,
                 true);
    Ink(Draw, Light, Left + ToolRowPadX + 14.0f, Pen, Kit::Grind(ToolRowSize), ToolRowSize, BodyInk,
        "Collection");
    Ink(Draw, Light, Left + Wide - ToolRowPadX, Pen, Kit::Grind(ToolRowSize), ToolRowSize, QuietInk, "+",
        Kit::Anchor::End);
    Pen += Kit::Grind(ToolRowSize) + ToolRowDrop;

    for (uint32_t Index = 0u; Index < Scene.RowCount && Index < SceneRowCeiling; ++Index)
    {
        PaintSceneRow(Draw, Light, { Left + TreePadX, Pen }, Wide - TreePadX * 2.0f, Scene.Rows[Index],
                      Index == Scene.Chosen);
        Pen += SceneRowTall;
    }
    Pen += FootnoteTop;
    Ink(Draw, Light, Left + FootnotePadX, Pen, Kit::Grind(FootnoteSize), FootnoteSize, FaintInk,
        "Drag a preset onto a row to retune it");
    Pen += Kit::Grind(FootnoteSize) + FootnoteDrop;

    //--- the preset library, which takes the rest of the pane ---------------------------------------------
    Draw->AddRectFilled({ Left, Pen }, { Left + Wide, Pen + 1.0f }, Line);
    char Count[16];
    std::snprintf(Count, sizeof(Count), "%u", FluidPresetCardCount);
    Pen += PaintPaneHeading(Draw, Light, { Left, Pen }, Wide, "EFFECTS", Count, false);

    PaintSearch(Draw, Light, { Left + SearchPadX, Pen + SearchLift }, Wide - SearchPadX * 2.0f,
                "Search effects", "");
    Pen += SearchLift + SearchTall + SearchDrop;

    static const char* RailFilters[4] = { "All", "Fire", "Smoke", "Blast" };
    Pen += PaintFilterRow(Draw, Light, { Left + FilterPadX, Pen }, RailFilters, 4u, uint32_t(Scene.Filter))
         + FilterDrop;

    const PresetCard* Rail = FluidPresetCards();
    const float CardTall = PresetCardHeight(Page.CardPad);
    for (uint32_t Index = 0u; Index < FluidPresetCardCount; ++Index)
    {
        if (!Shows(Rail[Index], Scene.Filter)) continue;
        if (Pen + CardTall > Page.StatusTop - LeftFootTall) break;
        PaintPresetCard(Draw, Light, Regular, { Left + PresetPadX, Pen }, Wide - PresetPadX * 2.0f,
                        Page.CardPad, Rail[Index],
                        std::strcmp(Rail[Index].Identity, Scene.PresetIdentity) == 0);
        Pen += CardTall + PresetCardDrop;
    }

    Draw->AddRectFilled({ Left, Page.StatusTop - LeftFootTall }, { Left + Wide, Page.StatusTop - LeftFootTall + 1.0f },
                        Line);
    Ink(Draw, Light, Left + PaneHeadPadX, Page.StatusTop - LeftFootTall, LeftFootTall, LeftFootSize, FaintInk,
        "Presets retune the selected domain");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CENTRE PANE
//------------------------------------------------------------------------------------------------------------------------

inline void PaintViewportBar(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                             const Subject& Scene)
{
    const float Left = Page.CentreX, Wide = Page.CentreW;
    Draw->AddRectFilled({ Left, Page.WorkTop }, { Left + Wide, Page.WorkTop + ViewBar }, BarFill);
    Draw->AddRectFilled({ Left, Page.WorkTop + ViewBar - 1.0f }, { Left + Wide, Page.WorkTop + ViewBar }, Line);

    float Pen = Left + ViewBarPadX;
    Ink(Draw, Regular, Pen, Page.WorkTop, ViewBar, ViewBarSize, BodyInk, "Scene view");
    Pen += Kit::Measured(Regular, ViewBarSize, "Scene view") + ViewBarGap;
    Draw->AddRectFilled({ Pen, Page.WorkTop + (ViewBar - DividerTall) * 0.5f },
                        { Pen + 1.0f, Page.WorkTop + (ViewBar + DividerTall) * 0.5f }, Divider);
    Pen += 1.0f + ViewBarGap;
    PaintSelect(Draw, Light, { Pen, Page.WorkTop + (ViewBar - 24.0f) * 0.5f }, 104.0f, 24.0f, ViewSelect,
                CameraName(Scene.Camera));

    float Right = Left + Wide - ViewBarPadX;
    const char* Overlay = Scene.BoundsShown ? "Overlays on" : "Overlays off";
    const float OverlayWide = ButtonWidth(Light, ViewSelect, Overlay, 9.0f);
    PaintButton(Draw, Light, { Right - OverlayWide, Page.WorkTop + (ViewBar - 24.0f) * 0.5f }, OverlayWide,
                24.0f, ViewSelect, Overlay, !Scene.BoundsShown);
    Right -= OverlayWide + ViewBarGap;

    const ControlChoice* Channels = DebugChannels();
    const uint32_t Picked = Scene.Channel >= 0 && uint32_t(Scene.Channel) < DebugChannelCount
                          ? uint32_t(Scene.Channel) : 0u;
    const float ChannelWide = 148.0f;
    PaintSelect(Draw, Light, { Right - ChannelWide, Page.WorkTop + (ViewBar - 24.0f) * 0.5f }, ChannelWide,
                24.0f, ViewSelect, Channels[Picked].Label);
    Right -= ChannelWide + ViewBarGap;

    const float BakeWide = ButtonWidth(Light, ViewSelect, "Bake flipbook", 10.0f);
    PaintButton(Draw, Light, { Right - BakeWide, Page.WorkTop + (ViewBar - 24.0f) * 0.5f }, BakeWide, 24.0f,
                ViewSelect, "Bake flipbook", true);
}

/// 📦 The bounds box and, when it is asked for, the voxel lattice inside it: an axis-aligned box in a
///    three-quarter projection, which is what .domain-bounds draws over the canvas.
inline void PaintBoundsBox(ImDrawList* Draw, ImVec2 Heart, float Half, float Rise, const Subject& Scene)
{
    const float Skew = Half * 0.45f;
    const ImVec2 FrontA = { Heart.x - Half,        Heart.y + Rise };
    const ImVec2 FrontB = { Heart.x + Half,        Heart.y + Rise };
    const ImVec2 FrontC = { Heart.x + Half,        Heart.y - Rise };
    const ImVec2 FrontD = { Heart.x - Half,        Heart.y - Rise };
    const ImVec2 BackA  = { FrontA.x + Skew,       FrontA.y - Skew * 0.55f };
    const ImVec2 BackB  = { FrontB.x + Skew,       FrontB.y - Skew * 0.55f };
    const ImVec2 BackC  = { FrontC.x + Skew,       FrontC.y - Skew * 0.55f };
    const ImVec2 BackD  = { FrontD.x + Skew,       FrontD.y - Skew * 0.55f };

    if (Scene.LatticeShown)
    {
        const int Lines = 8;
        for (int Index = 1; Index < Lines; ++Index)
        {
            const float Share = float(Index) / float(Lines);
            Draw->AddLine({ FrontA.x + Half * 2.0f * Share, FrontA.y },
                          { FrontD.x + Half * 2.0f * Share, FrontD.y }, LatticeInk, 1.0f);
            Draw->AddLine({ FrontA.x, FrontA.y - Rise * 2.0f * Share },
                          { FrontB.x, FrontB.y - Rise * 2.0f * Share }, LatticeInk, 1.0f);
        }
    }

    if (!Scene.BoundsShown) return;
    const ImVec2 Front[4] = { FrontA, FrontB, FrontC, FrontD };
    const ImVec2 Back[4]  = { BackA,  BackB,  BackC,  BackD  };
    for (int Index = 0; Index < 4; ++Index)
    {
        Draw->AddLine(Front[Index], Front[(Index + 1) % 4], BoundsInk, 1.0f);
        Draw->AddLine(Back[Index],  Back[(Index + 1) % 4],  BoundsInk, 1.0f);
        Draw->AddLine(Front[Index], Back[Index],            BoundsInk, 1.0f);
    }
}

inline void PaintViewport(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                          const Subject& Scene)
{
    const float Left = Page.CentreX, Wide = Page.CentreW;
    const float Top = Page.StageTop, Tall = Page.StageTall;
    Draw->AddRectFilled({ Left, Top }, { Left + Wide, Top + Tall }, StageFill);

    PaintBoundsBox(Draw, { Left + Wide * 0.5f, Top + Tall * 0.52f }, ImMin(Wide, Tall) * 0.22f,
                   ImMin(Wide, Tall) * 0.26f, Scene);

    //--- the caption ------------------------------------------------------------------------------------
    float Pen = Left + CaptionPad;
    const char* Pill = Scene.Running ? "LIVE" : "HELD";
    const float PillWide = Kit::Measured(Light, CountSize, Pill) + 14.0f;
    PaintPlate(Draw, { Pen, Top + CaptionPad }, PillWide, PillTall, ChosenFill, ChosenEdge, 9.0f);
    Draw->AddCircleFilled({ Pen + 7.0f, Top + CaptionPad + PillTall * 0.5f }, 2.5f,
                          Scene.Running ? LiveInk : FaintInk, 12);
    Ink(Draw, Light, Pen + 14.0f, Top + CaptionPad, PillTall, CountSize, BodyInk, Pill);
    Pen += PillWide + 9.0f;

    const SceneRow& Chosen = Scene.Rows[Scene.Chosen < Scene.RowCount ? Scene.Chosen : 0u];
    Ink(Draw, Regular, Pen, Top + CaptionPad, PillTall, CaptionSize, PageInk, Chosen.Name);
    char Caption[96];
    StageCaption(Scene, Caption, sizeof(Caption));
    Ink(Draw, Light, Left + CaptionPad, Top + CaptionPad + PillTall + 4.0f, Kit::Grind(CaptionNote),
        CaptionNote, QuietInk, Caption);

    //--- the tool column --------------------------------------------------------------------------------
    for (int Index = 0; Index < 4; ++Index)
    {
        const ImVec2 Spot = { Left + CaptionPad, Top + Tall * 0.5f - ToolTile * 2.0f
                                                 + float(Index) * (ToolTile + ToolTileGap) };
        PaintPlate(Draw, Spot, ToolTile, ToolTile, Index == 0 ? ChosenFill : ControlFill,
                   Index == 0 ? ChosenEdge : ControlEdge, 5.0f);
        const ImVec2 Heart = { Spot.x + ToolTile * 0.5f, Spot.y + ToolTile * 0.5f };
        const ImU32  Face  = Index == 0 ? StrongInk : QuietInk;
        if (Index == 0)   // orbit
        {
            Draw->AddCircle(Heart, 6.0f, Face, 20, 1.0f);
            Draw->AddCircleFilled({ Heart.x + 6.0f, Heart.y }, 1.8f, Face, 10);
        }
        else if (Index == 1)   // the flamethrower brush
            Draw->AddTriangleFilled({ Heart.x, Heart.y - 6.0f }, { Heart.x + 5.0f, Heart.y + 5.0f },
                                    { Heart.x - 5.0f, Heart.y + 5.0f }, Face);
        else if (Index == 2)   // detonate
            PaintStar(Draw, Heart, 6.0f, Face);
        else                   // turntable
            PaintArc(Draw, Heart, 5.0f, Face);
    }

    //--- the right-hand readings ------------------------------------------------------------------------
    char Rate[32];
    if (Scene.Hertz > 0.0f) std::snprintf(Rate, sizeof(Rate), "%.0f FPS", double(Scene.Hertz));
    else                    std::snprintf(Rate, sizeof(Rate), "\xe2\x80\x94 FPS");
    char Lattice[32];
    LatticeReading(Scene.Settings.LatticeResolution, Lattice, sizeof(Lattice));
    Ink(Draw, Light, Left + Wide - CaptionPad, Top + CaptionPad, PillTall, HudSize, BodyInk, Rate,
        Kit::Anchor::End);
    Ink(Draw, Light, Left + Wide - CaptionPad, Top + CaptionPad + PillTall, PillTall, HudSize, QuietInk,
        Lattice, Kit::Anchor::End);

    //--- the help line and the axis gizmo ---------------------------------------------------------------
    Ink(Draw, Light, Left + Wide * 0.5f, Top + Tall - CaptionPad - Kit::Grind(HelpSize),
        Kit::Grind(HelpSize), HelpSize, FaintInk,
        "Drag to orbit \xc2\xb7 Right-drag to pan \xc2\xb7 Scroll to zoom", Kit::Anchor::Middle);

    const ImVec2 Gizmo = { Left + Wide - CaptionPad - GizmoBox, Top + Tall - CaptionPad - GizmoBox };
    const ImVec2 Heart = { Gizmo.x + GizmoBox * 0.5f, Gizmo.y + GizmoBox * 0.5f };
    Draw->AddLine(Heart, { Heart.x, Heart.y - 22.0f }, AzureInk, 1.0f);
    Draw->AddLine(Heart, { Heart.x - 19.0f, Heart.y + 11.0f }, AmberInk, 1.0f);
    Draw->AddLine(Heart, { Heart.x + 19.0f, Heart.y + 11.0f }, PaleInk, 1.0f);
    Ink(Draw, Light, Heart.x + 3.0f, Heart.y - 30.0f, 9.0f, 8.0f, AzureInk, "Y");
    Ink(Draw, Light, Heart.x - 26.0f, Heart.y + 11.0f, 9.0f, 8.0f, AmberInk, "X");
    Ink(Draw, Light, Heart.x + 21.0f, Heart.y + 11.0f, 9.0f, 8.0f, PaleInk, "Z");
}

inline void PaintTransport(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                           const Subject& Scene)
{
    const float Left = Page.CentreX, Wide = Page.CentreW;
    Draw->AddRectFilled({ Left, Page.MoveTop }, { Left + Wide, Page.MoveTop + MoveBar }, MoveFill);
    Draw->AddRectFilled({ Left, Page.MoveTop }, { Left + Wide, Page.MoveTop + 1.0f }, Line);
    Draw->AddRectFilled({ Left, Page.MoveTop + MoveBar - 1.0f }, { Left + Wide, Page.MoveTop + MoveBar }, Line);

    float Pen = Left + MoveBarPadX;
    PaintPlate(Draw, { Pen, Page.MoveTop + (MoveBar - StepTile) * 0.5f }, StepTile, StepTile, ControlFill,
               ControlEdge, 5.0f);
    PaintArc(Draw, { Pen + StepTile * 0.5f, Page.MoveTop + MoveBar * 0.5f }, 5.0f, BodyInk);
    Pen += StepTile + TileGap;

    const ImVec2 Play = { Pen, Page.MoveTop + (MoveBar - PlayTile) * 0.5f };
    PaintPlate(Draw, Play, PlayTile, PlayTile, ChosenFill, ChosenEdge, PlayTile * 0.5f);
    if (Scene.Running)
    {
        Draw->AddRectFilled({ Play.x + 10.0f, Play.y + 9.0f }, { Play.x + 12.5f, Play.y + 20.0f }, PageInk);
        Draw->AddRectFilled({ Play.x + 16.5f, Play.y + 9.0f }, { Play.x + 19.0f, Play.y + 20.0f }, PageInk);
    }
    else
        Draw->AddTriangleFilled({ Play.x + 11.0f, Play.y + 9.0f }, { Play.x + 21.0f, Play.y + 14.5f },
                                { Play.x + 11.0f, Play.y + 20.0f }, PageInk);
    Pen += PlayTile + TileGap;

    PaintPlate(Draw, { Pen, Page.MoveTop + (MoveBar - StepTile) * 0.5f }, StepTile, StepTile, ControlFill,
               ControlEdge, 5.0f);
    PaintSkipMark(Draw, { Pen + StepTile * 0.5f, Page.MoveTop + MoveBar * 0.5f }, 5.0f, BodyInk);
    Pen += StepTile + MoveBarGap;

    char Clock[24];
    Timecode(Scene.Elapsed, Clock, sizeof(Clock));
    Ink(Draw, Regular, Pen, Page.MoveTop, MoveBar, TimecodeSize, PageInk, Clock);
    Pen += Kit::Measured(Regular, TimecodeSize, Clock) + MoveBarGap;

    char Speed[24];
    std::snprintf(Speed, sizeof(Speed), "%.2f\xc3\x97", double(Scene.Settings.TimeScale));
    PaintSelect(Draw, Light, { Pen, Page.MoveTop + (MoveBar - 24.0f) * 0.5f }, 62.0f, 24.0f, ViewSelect, Speed);

    float Right = Left + Wide - MoveBarPadX;
    const float BurstWide = ButtonWidth(Light, ViewSelect, "Trigger burst", 10.0f);
    PaintButton(Draw, Light, { Right - BurstWide, Page.MoveTop + (MoveBar - 24.0f) * 0.5f }, BurstWide, 24.0f,
                ViewSelect, "Trigger burst", true);
    Right -= BurstWide + ViewBarGap;
    PaintSelect(Draw, Light, { Right - 110.0f, Page.MoveTop + (MoveBar - 24.0f) * 0.5f }, 110.0f, 24.0f,
                ViewSelect, "Single burst");
}

inline void PaintTimeline(ImDrawList* Draw, ImFont* Light, const Layout& Page, const Subject& Scene)
{
    const float Left = Page.CentreX, Wide = Page.CentreW;
    Draw->AddRectFilled({ Left, Page.TimeTop }, { Left + Wide, Page.TimeTop + TimeBand }, TimeFill);

    char Count[48];
    std::snprintf(Count, sizeof(Count), "%u ADVANCES", Scene.Advances);
    TrackedInk(Draw, Light, Left + RulerPadX, Page.TimeTop, TimeLabelTall, TimeLabelSize, QuietInk,
               "SIMULATION", TimeLabelTrack);
    TrackedRight(Draw, Light, Left + Wide - RulerPadX, Page.TimeTop, TimeLabelTall, TimeLabelSize, FaintInk,
                 Count, TimeLabelTrack);

    const float RulerTop  = Page.TimeTop + TimeLabelTall;
    const float RulerTall = TimeBand - TimeLabelTall - RulerDrop;
    const float RulerLeft = Left + RulerPadX, RulerWide = Wide - RulerPadX * 2.0f;
    for (int Tick = 0; Tick <= int(RulerTick); ++Tick)
        Draw->AddRectFilled({ RulerLeft + RulerWide * (float(Tick) / RulerTick), RulerTop },
                            { RulerLeft + RulerWide * (float(Tick) / RulerTick) + 1.0f, RulerTop + RulerTall },
                            RulerInk);

    const float TrackTop = RulerTop + RulerTall - SourceTrack;
    PaintPlate(Draw, { RulerLeft, TrackTop }, RulerWide, SourceTrack,
               Scene.Settings.EmitterEnabled ? ChosenFill : CardFill, ChosenEdge, 4.0f);
    Ink(Draw, Light, RulerLeft + 9.0f, TrackTop, SourceTrack, SourceSize,
        Scene.Settings.EmitterEnabled ? BodyInk : FaintInk, SourceReading(Scene));

    const float Share = Scene.Span > 0.0f ? ImMin(1.0f, Scene.Elapsed / Scene.Span) : 0.0f;
    const float Head  = RulerLeft + RulerWide * Share;
    Draw->AddRectFilled({ Head, RulerTop }, { Head + 1.0f, RulerTop + RulerTall }, PlayheadInk);
    Draw->AddTriangleFilled({ Head - 4.0f, RulerTop }, { Head + 5.0f, RulerTop }, { Head + 0.5f, RulerTop + 6.0f },
                            PlayheadInk);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INSPECTOR PANE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One property row, drawn from whatever FluidEditorControls.h says the control is.
inline float PaintPropertyRow(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Key,
                              const GasSettings& Settings)
{
    const ControlRow* Control = ReadControl(Key);
    if (!Control)
    {
        Ink(Draw, Light, Spot.x, Spot.y, FieldTall, LabelSize, FaintInk, Key);
        return FieldTall;
    }

    const float Tall = ControlRowHeight(Control->Kind);
    float Value = 0.0f;
    const bool Known = ReadSetting(Settings, Key, Value);

    if (Control->Kind == ControlKind::Toggle)
    {
        Ink(Draw, Light, Spot.x, Spot.y, Tall, LabelSize, LabelInk, Control->Label);
        PaintSwitch(Draw, { Spot.x + Wide - SwitchWide, Spot.y + (Tall - SwitchTall) * 0.5f },
                    Known && Value >= 0.5f);
        return Tall;
    }

    if (Control->Kind == ControlKind::Choice)
    {
        const char* Shown = Control->ChoiceCount ? Control->Choices[0].Label : "";
        for (uint32_t Index = 0u; Index < Control->ChoiceCount; ++Index)
            if (Known && Control->Choices[Index].Ordinal == int(Value + 0.5f))
                Shown = Control->Choices[Index].Label;
        const float ControlWide = ImMax(FieldWide, Wide * 0.52f);
        Ink(Draw, Light, Spot.x, Spot.y, Tall, LabelSize, LabelInk, Control->Label);
        PaintSelect(Draw, Light, { Spot.x + Wide - ControlWide, Spot.y }, ControlWide, SelectTall, FieldSize,
                    Shown);
        return Tall;
    }

    char Shown[32];
    Reading(Value, Control->Step, Shown, sizeof(Shown));
    Ink(Draw, Light, Spot.x, Spot.y, FieldTall, LabelSize, LabelInk, Control->Label);
    PaintPill(Draw, Light, { Spot.x + Wide - FieldWide, Spot.y }, FieldWide, Shown, Control->Unit);
    const float Reach = Control->High - Control->Low;
    PaintTrack(Draw, { Spot.x, Spot.y + FieldTall + RangeLift }, Wide,
               Reach > 0.0f ? (Value - Control->Low) / Reach : 0.0f);
    return Tall;
}

inline float PaintPropertyGroup(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                                const PropertyGroup& Group, const GasSettings& Settings, float RowSpacing,
                                float ContentFoot)
{
    const float Summary = GroupSummaryHeight();
    const float Mark = Spot.x + GroupPadX;
    const float Mid  = Spot.y + Summary * 0.5f;
    if (Group.Opened) PaintChevron(Draw, { Mark + 5.0f, Mid }, 5.0f, QuietInk, true);
    else
    {
        Draw->AddLine({ Mark + 2.0f, Mid - 5.0f }, { Mark + 7.0f, Mid }, QuietInk, 1.2f);
        Draw->AddLine({ Mark + 7.0f, Mid }, { Mark + 2.0f, Mid + 5.0f }, QuietInk, 1.2f);
    }
    Ink(Draw, Regular, Mark + GroupMark + GroupGap - 4.0f, Spot.y, Summary, GroupSize, StrongInk, Group.Title);
    TrackedRight(Draw, Light, Spot.x + Wide - GroupPadX, Spot.y, Summary, BadgeSize, BadgeInk, Group.Badge,
                 BadgeTrack);

    float Pen = Spot.y + Summary;
    if (Group.Opened)
    {
        const float Inner = Wide - GroupPadX * 2.0f;
        if (Group.Hint[0])
            Pen += PaintProse(Draw, Light, { Spot.x + GroupPadX, Pen }, HintSize, Inner, FaintInk, Group.Hint)
                 + HintDrop;
        for (uint32_t Index = 0u; Index < Group.KeyCount; ++Index)
        {
            Pen += PaintPropertyRow(Draw, Light, { Spot.x + GroupPadX, Pen }, Inner, Group.Keys[Index],
                                    Settings);
            if (Index + 1u < Group.KeyCount) Pen += RowSpacing;
        }
        Pen += ContentFoot;
    }
    Draw->AddRectFilled({ Spot.x, Pen - 1.0f }, { Spot.x + Wide, Pen }, GroupEdge);
    return Pen - Spot.y;
}

inline void PaintInspectorPane(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page,
                               const Subject& Scene)
{
    const float Left = Page.RightX, Wide = Page.RightW;
    Draw->AddRectFilled({ Left, Page.WorkTop }, { Left + Wide, Page.StatusTop }, PanelFill);
    Draw->AddRectFilled({ Left, Page.WorkTop }, { Left + 1.0f, Page.StatusTop }, Line);

    float Pen = Page.WorkTop;
    PaintPaneHeading(Draw, Light, { Left, Pen }, Wide, "INSPECTOR", "", true);
    Ink(Draw, Light, Left + Wide - PaneHeadPadX, Pen, PaneHead, CountSize, FaintInk, "Revert",
        Kit::Anchor::End);
    Pen += PaneHead;

    const SceneRow& Chosen = Scene.Rows[Scene.Chosen < Scene.RowCount ? Scene.Chosen : 0u];
    const ImVec2 Symbol = { Left + ObjectPadX, Pen + ObjectPadTop };
    PaintPlate(Draw, Symbol, SymbolWide, SymbolTall, CardLive, RowEdge, 5.0f);
    Draw->AddRect({ Symbol.x + 10.0f, Symbol.y + 10.0f }, { Symbol.x + 24.0f, Symbol.y + 26.0f }, BodyInk,
                  2.0f, 0, 1.0f);

    const float NameX = Symbol.x + SymbolWide + ObjectGap;
    Ink(Draw, Regular, NameX, Symbol.y, Kit::Grind(ObjectName), ObjectName, PageInk, Chosen.Name);
    TrackedInk(Draw, Light, NameX, Symbol.y + Kit::Grind(ObjectName) + ObjectNameDrop, Kit::Grind(ObjectType),
               ObjectType, QuietInk, KindName(Chosen.Kind), ObjectTypeTrack);
    PaintSwitch(Draw, { Left + Wide - ObjectPadX - SwitchWide, Symbol.y + (SymbolTall - SwitchTall) * 0.5f },
                Chosen.Enabled);
    Pen += ObjectPadTop + SymbolTall + ObjectPadBot;

    //--- the three tabs -----------------------------------------------------------------------------------
    float TabPen = Left + TabPadX;
    for (uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const InspectorTab Which = InspectorTab(Index);
        const bool Live = Which == Scene.Open;
        const char* Title = TabName(Which);
        const float Run = Kit::Measured(Light, TabSize, Title);
        Ink(Draw, Light, TabPen, Pen, TabBand - 9.0f, TabSize, Live ? StrongInk : QuietInk, Title);
        if (Live)
            Draw->AddRectFilled({ TabPen, Pen + TabBand - TabRule }, { TabPen + Run, Pen + TabBand },
                                StrongInk);
        TabPen += Run + TabGap;
    }
    Draw->AddRectFilled({ Left, Pen + TabBand - 1.0f }, { Left + Wide, Pen + TabBand }, GroupEdge);
    Pen += TabBand;

    //--- the groups ---------------------------------------------------------------------------------------
    const GroupListing Listing = InspectorGroups(Scene.Open, Chosen.Kind);
    const float Floor = Page.StatusTop - FootBand;
    for (uint32_t Index = 0u; Index < Listing.Count; ++Index)
    {
        const float Tall = GroupHeight(Light, Wide, Listing.Groups[Index], Page.RowSpacing, Page.ContentFoot);
        if (Pen + Tall > Floor) break;
        Pen += PaintPropertyGroup(Draw, Light, Regular, { Left, Pen }, Wide, Listing.Groups[Index],
                                  Scene.Settings, Page.RowSpacing, Page.ContentFoot);
    }

    Draw->AddRectFilled({ Left, Floor }, { Left + Wide, Floor + 1.0f }, Line);
    Ink(Draw, Light, Left + PaneHeadPadX, Floor, FootBand, FootSize, FaintInk,
        "Changes apply while rendering");
}

/// 📦 What the inspector would need to show every group of the open tab without clipping.
inline float InspectorContentHeight(ImFont* Face, float PaneWide, const Subject& Scene, float RowSpacing,
                                    float ContentFoot)
{
    const SceneRow& Chosen = Scene.Rows[Scene.Chosen < Scene.RowCount ? Scene.Chosen : 0u];
    const GroupListing Listing = InspectorGroups(Scene.Open, Chosen.Kind);
    float Tall = PaneHead + ObjectPadTop + SymbolTall + ObjectPadBot + TabBand + FootBand;
    for (uint32_t Index = 0u; Index < Listing.Count; ++Index)
        Tall += GroupHeight(Face, PaneWide, Listing.Groups[Index], RowSpacing, ContentFoot);
    return Tall;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE PAGE
//------------------------------------------------------------------------------------------------------------------------

inline void Paint(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& Page, const Subject& Scene)
{
    Draw->AddRectFilled({ 0, 0 }, { Page.Wide, Page.Tall }, PageFill);
    Draw->AddRectFilled({ Page.CentreX, Page.WorkTop }, { Page.CentreX + Page.CentreW, Page.StatusTop },
                        CentreFill);
    PaintHeader(Draw, Light, Regular, Page, Scene);
    PaintDocumentBar(Draw, Light, Page, Scene);
    PaintOutlinerPane(Draw, Light, Regular, Page, Scene);
    PaintViewportBar(Draw, Light, Regular, Page, Scene);
    PaintViewport(Draw, Light, Regular, Page, Scene);
    PaintTransport(Draw, Light, Regular, Page, Scene);
    PaintTimeline(Draw, Light, Page, Scene);
    PaintInspectorPane(Draw, Light, Regular, Page, Scene);
    PaintStatusBar(Draw, Light, Page, Scene);
}

}   // namespace Frontier::FluidEditor
