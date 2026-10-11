//==============================================================================================================================================
//                                                        FRACTUREEDITORSURFACE.H
//==============================================================================================================================================
// 📦 The fracture editor itself — the second application the shipped editor opens, ported chrome for chrome.
//
//    Reference: Experimental/FractureEditor/index.html, a 788 KB self-contained bundle built by Build.mjs
//    from FracturePanel.html (12,172 B), FracturePanel.css (14,461 B) and FracturePanel.js (25,953 B),
//    with its material table from SourceDepot/Fragmentation/src/fracture/materials.ts and its two shaded
//    illustrations from FractureProjection.js. It is what the per-object Fracture card's ↗ button opens;
//    FractureCardSurface.h drew that button and had nothing to open, which this closes.
//
//    This is the UI only, as asked. It draws every pane, card, readout and control of the three-column
//    workspace at whatever size the host gives it, from a Subject the host fills. It does not partition
//    geometry: Fracture(), Validate() and the bake depot in FractureStructure.js are a separate body of
//    work and are not started here. Where a figure would come from that solver the Subject carries the
//    reference's own "not yet run" text, which is exactly what the browser shows before you press
//    Fracture object.
//
//    Everything is measured from the stylesheet. The app is a flex column of five fixed bands around one
//    flexible grid, so the layout is resolved once into Layout and both the painter and the harness read
//    the same numbers.

#pragma once

#include "BaseMeshSurface.h"
#include "WindInstrumentSurface.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier::FractureEditor
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                         PALETTE
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 PageInk      = IM_COL32(195, 195, 195, 255);  // [-] :root color #c3c3c3
constexpr ImU32 TitleFill    = IM_COL32( 18,  18,  18, 255);  // [-] .titlebar #121212
constexpr ImU32 Divider      = IM_COL32( 56,  56,  56, 255);  // [-] .divider #383838
constexpr ImU32 TailInk      = IM_COL32( 95,  95,  95, 255);  // [-] .title-tail #5f5f5f
constexpr ImU32 BarFill      = IM_COL32( 32,  32,  32, 255);  // [-] .workspace-bar #202020
constexpr ImU32 BarEdge      = IM_COL32(  8,   8,   8, 255);  // [-] its border-bottom #080808
constexpr ImU32 TabFill      = IM_COL32( 48,  48,  48, 255);  // [-] .workspace-tab #303030
constexpr ImU32 TabGlyph     = IM_COL32(146, 146, 146, 255);  // [-] .workspace-tab span #929292
constexpr ImU32 CrumbInk     = IM_COL32(119, 119, 119, 255);  // [-] #breadcrumb #777
constexpr ImU32 ButtonFill   = IM_COL32( 40,  40,  40, 255);  // [-] button background #282828
constexpr ImU32 ButtonEdge   = IM_COL32(255, 255, 255,  11);  // [-] button border #ffffff0b
constexpr ImU32 Gutter       = IM_COL32(  8,   8,   8, 255);  // [-] .workspace background, the 1 px gaps
constexpr ImU32 PaneFill     = IM_COL32( 27,  27,  27, 255);  // [-] .target-panel / .inspector-panel #1b1b1b
constexpr ImU32 HeadFill     = IM_COL32( 34,  34,  34, 255);  // [-] .pane-heading #222
constexpr ImU32 HeadEdge     = IM_COL32(255, 255, 255,   8);  // [-] its border-bottom #ffffff08
constexpr ImU32 HeadInk      = IM_COL32(170, 170, 170, 255);  // [-] .pane-heading #aaa
constexpr ImU32 HeadNote     = IM_COL32( 99,  99,  99, 255);  // [-] .pane-heading span #636363
constexpr ImU32 SelectedFill = IM_COL32( 43,  45,  43, 255);  // [-] .target-selected #2b2d2b
constexpr ImU32 SelectedEdge = IM_COL32(255, 255, 255,  12);  // [-] its border #ffffff0c
constexpr ImU32 SpecimenInk  = IM_COL32(211, 217, 213, 255);  // [-] .target-selected svg stroke #d3d9d5
constexpr ImU32 SpecimenSub  = IM_COL32(134, 139, 135, 255);  // [-] .target-selected span #868b87
constexpr ImU32 LiveDot      = IM_COL32(105, 147, 117, 255);  // [-] .target-selected i #699375
constexpr ImU32 TermInk      = IM_COL32( 99,  99,  99, 255);  // [-] .target-properties dt #636363
constexpr ImU32 ValueInk     = IM_COL32(151, 151, 151, 255);  // [-] .target-properties dd #979797
constexpr ImU32 FootFill     = IM_COL32( 23,  23,  23, 255);  // [-] .target-footer #171717
constexpr ImU32 TinyInk      = IM_COL32(114, 114, 114, 255);  // [-] .tiny-label #727272
constexpr ImU32 CodeInk      = IM_COL32(112, 112, 112, 255);  // [-] .target-footer code #707070
constexpr ImU32 ViewFill     = IM_COL32( 21,  21,  21, 255);  // [-] .viewport-panel #151515
constexpr ImU32 ToolFill     = IM_COL32( 28,  28,  28, 255);  // [-] .viewport-toolbar #1c1c1c
constexpr ImU32 PressedFill  = IM_COL32( 51,  51,  51, 255);  // [-] toolbar [aria-pressed] #333
constexpr ImU32 PressedInk   = IM_COL32(238, 238, 238, 255);  // [-] and its colour #eee
constexpr ImU32 EyebrowInk   = IM_COL32(113, 121, 115, 255);  // [-] .viewport-title > span #717973
constexpr ImU32 ViewTitleInk = IM_COL32(195, 199, 196, 255);  // [-] .viewport-title h1 #c3c7c4
constexpr ImU32 ViewNoteInk  = IM_COL32(102, 102, 102, 255);  // [-] .viewport-title p #666
constexpr ImU32 HelpInk      = IM_COL32( 86,  89,  86, 255);  // [-] .viewport-help #565956
constexpr ImU32 FieldInk     = IM_COL32(145, 145, 145, 255);  // [-] .field > span #919191
constexpr ImU32 FieldSmall   = IM_COL32(101, 101, 101, 255);  // [-] .preview-controls small #656565
constexpr ImU32 PrimaryFill  = IM_COL32(186, 203, 191, 255);  // [-] .primary #bacbbf
constexpr ImU32 PrimaryInk   = IM_COL32( 23,  39,  29, 255);  // [-] its colour #17271d
constexpr ImU32 MetricFill   = IM_COL32( 22,  22,  22, 255);  // [-] .metrics #161616
constexpr ImU32 MetricInk    = IM_COL32(183, 193, 186, 255);  // [-] .metrics strong #b7c1ba
constexpr ImU32 MetricKey    = IM_COL32( 96, 103,  96, 255);  // [-] .metrics span #606760
constexpr ImU32 CardFill     = IM_COL32( 25,  25,  25, 255);  // [-] .card #191919
constexpr ImU32 CardEdge     = IM_COL32(255, 255, 255,  11);  // [-] .card border #ffffff0b
constexpr ImU32 CardTitle    = IM_COL32(219, 219, 219, 255);  // [-] h2 #dbdbdb
constexpr ImU32 CardOrdinal  = IM_COL32( 72,  72,  72, 255);  // [-] .card > header > span #484848
constexpr ImU32 ProseInk     = IM_COL32(119, 119, 119, 255);  // [-] p #777
constexpr ImU32 OptionFill   = IM_COL32( 35,  35,  35, 255);  // [-] .material-options button #232323
constexpr ImU32 OptionInk    = IM_COL32(146, 146, 146, 255);  // [-] and its colour #929292
constexpr ImU32 OptionPicked = IM_COL32( 53,  57,  53, 255);  // [-] [aria-pressed] #353935
constexpr ImU32 OptionPickInk= IM_COL32(225, 231, 226, 255);  // [-] its colour #e1e7e2
constexpr ImU32 OptionPickEdge=IM_COL32(103, 118, 105, 255);  // [-] its border #677669
constexpr ImU32 ReadoutInk   = IM_COL32(185, 191, 186, 255);  // [-] .material-readout strong #b9bfba
constexpr ImU32 ReadoutUnit  = IM_COL32(119, 119, 119, 255);  // [-] its small #777
constexpr ImU32 ReadoutKey   = IM_COL32( 94,  99,  94, 255);  // [-] its span #5e635e
constexpr ImU32 ReadoutRule  = IM_COL32(255, 255, 255,   9);  // [-] its divider #ffffff09
constexpr ImU32 GraphRule    = IM_COL32(255, 255, 255,  13);  // [-] .graph-grid #ffffff0d
constexpr ImU32 GraphWash    = IM_COL32(140, 165, 149,  16);  // [-] .graph-area #8ca59510
constexpr ImU32 GraphLine    = IM_COL32(141, 166, 148, 255);  // [-] .graph-line #8da694
constexpr ImU32 GraphMark    = IM_COL32(155, 170, 160, 255);  // [-] .impact-graph svg fill #9baaa0
constexpr ImU32 GraphLabel   = IM_COL32( 98, 104,  98, 255);  // [-] .impact-graph text #626862
constexpr ImU32 SpotRule     = IM_COL32(255, 255, 255,   8);  // [-] .impact-location border-top #ffffff08
constexpr ImU32 InputFill    = IM_COL32( 16,  16,  16, 255);  // [-] input background #101010
constexpr ImU32 InputEdge    = IM_COL32(255, 255, 255,  16);  // [-] input border #ffffff10
constexpr ImU32 CheckFill    = IM_COL32( 37,  40,  37, 255);  // [-] .quality-checks span #252825
constexpr ImU32 CheckInk     = IM_COL32(130, 144, 135, 255);  // [-] its colour #829087
constexpr ImU32 CheckEdge    = IM_COL32( 57,  66,  57,  80);  // [-] its border #39423950
constexpr ImU32 ModeFill     = IM_COL32( 17,  17,  17, 255);  // [-] .mode-options #111
constexpr ImU32 ModeEdge     = IM_COL32(255, 255, 255,  10);  // [-] its border #ffffff0a
constexpr ImU32 ModeInk      = IM_COL32(116, 116, 116, 255);  // [-] its buttons #747474
constexpr ImU32 ModePicked   = IM_COL32( 52,  52,  52, 255);  // [-] [aria-pressed] #343434
constexpr ImU32 ReceiptEdge  = IM_COL32(255, 255, 255,   9);  // [-] .bake-receipt border #ffffff09
constexpr ImU32 ReceiptSmall = IM_COL32(101, 101, 101, 255);  // [-] .bake-receipt small #656565
constexpr ImU32 DotIdle      = IM_COL32( 85,  85,  85, 255);  // [-] #bake-dot default #555
constexpr ImU32 DotReady     = IM_COL32(137, 165, 145, 255);  // [-] ready #89a591
constexpr ImU32 DotStale     = IM_COL32(170, 121,  90, 255);  // [-] stale #aa795a
constexpr ImU32 ReceiptTerm  = IM_COL32(104, 104, 104, 255);  // [-] .verification-card dt #686868
constexpr ImU32 ReceiptValue = IM_COL32(160, 170, 163, 255);  // [-] .verification-card dd #a0aaa3
constexpr ImU32 StatusFill   = IM_COL32( 17,  17,  17, 255);  // [-] .statusbar #111
constexpr ImU32 StatusInk    = IM_COL32(116, 116, 116, 255);  // [-] its colour #747474
constexpr ImU32 StatusTail   = IM_COL32( 86,  95,  88, 255);  // [-] its last span #565f58
constexpr ImU32 StatusDot    = IM_COL32( 95, 120, 102, 255);  // [-] .statusbar i #5f7866
constexpr ImU32 RangeFilled  = IM_COL32( 69,  69,  69, 255);  // [-] input[type=range] filled #454545
constexpr ImU32 RangeEmpty   = IM_COL32( 36,  36,  36, 255);  // [-] its remainder #242424
constexpr ImU32 RangeKnob    = IM_COL32(224, 224, 224, 255);  // [-] its thumb #e0e0e0
constexpr ImU32 SplitEdge    = IM_COL32(255, 255, 255,  13);  // [-] .split-value border #ffffff0d
constexpr ImU32 SplitUnit    = IM_COL32( 26,  26,  26, 255);  // [-] .split-value small background #1a1a1a
constexpr ImU32 SplitUnitInk = IM_COL32( 92,  92,  92, 255);  // [-] its colour #5c5c5c
constexpr ImU32 SplitInk     = IM_COL32(240, 240, 240, 255);  // [-] .split-value input #f0f0f0
constexpr ImU32 Disabled     = IM_COL32(  0,   0,   0,  90);  // [-] button:disabled { opacity: .35 }, as a scrim

//------------------------------------------------------------------------------------------------------------------------
//                                                    BAND AND PANE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr float TitleBar     = 39.0f;   // [px] .titlebar height
constexpr float TitlePadX    = 18.0f;   // [px] its padding
constexpr float TitleGap     = 17.0f;   // [px] its gap
constexpr float TitleSize    = 11.0f;   // [px] its font-size
constexpr float BrandSize    = 15.0f;   // [px] .titlebar a font-size — the diamond
constexpr float BrandGap     =  8.0f;   // [px] its gap
constexpr float BrandTrack   =  0.5f;   // [px] .titlebar b letter-spacing
constexpr float DividerTall  = 13.0f;   // [px] .divider height
constexpr float TailSize     =  9.0f;   // [px] .title-tail font-size
constexpr float TailTrack    =  1.3f;   // [px] its letter-spacing

constexpr float WorkBar      = 40.0f;   // [px] .workspace-bar height
constexpr float WorkGap      = 19.0f;   // [px] its gap
constexpr float WorkPadRight = 12.0f;   // [px] its padding-right
constexpr float TabWide      = 150.0f;  // [px] .workspace-tab min-width
constexpr float TabPadX      = 20.0f;   // [px] its padding left / right
constexpr float TabPadTop    = 14.0f;   // [px] its padding-top
constexpr float TabRoundA    =  7.0f;   // [px] border-radius top-left
constexpr float TabRoundB    = 15.0f;   // [px] border-radius top-right
constexpr float TabGlyphGap  = 40.0f;   // [px] its span margin-left
constexpr float SmallButton  = 10.0f;   // [px] .workspace-bar button font-size
constexpr float SmallPadX    = 10.0f;   // [px] its padding-x
constexpr float SmallPadY    =  5.0f;   // [px] its padding-y
constexpr float ButtonRound  =  5.0f;   // [px] button border-radius

constexpr float LeftWide     = 240.0f;  // [px] .workspace grid column 1
constexpr float RightWide    = 330.0f;  // [px] grid column 3
constexpr float LeftWideBig  = 270.0f;  // [px] @media (min-width: 1600px)
constexpr float RightWideBig = 350.0f;  // [px] ditto
constexpr float LeftWideNarrow  = 190.0f;  // [px] @media (max-width: 1180px)
constexpr float RightWideNarrow = 300.0f;  // [px] ditto
constexpr float CentreMin    = 260.0f;  // [px] minmax(260px, 1fr)
constexpr float CentreMinBig = 300.0f;  // [px] at 1600 and up
constexpr float GridGap      =  1.0f;   // [px] .workspace gap

constexpr float PaneHead     = 39.0f;   // [px] .pane-heading height
constexpr float PaneHeadPadX = 16.0f;   // [px] its padding
constexpr float PaneHeadSize = 11.0f;   // [px] its font-size
constexpr float PaneNoteSize =  8.0f;   // [px] its span
constexpr float PaneNoteTrack=  1.2f;   // [px] its letter-spacing

constexpr float TargetPadY   = 18.0f;   // [px] .target-content padding
constexpr float TargetPadX   = 15.0f;   // [px] ditto
constexpr float TargetPadNarrow = 12.0f;// [px] @media (max-width: 1180px)
constexpr float ChosenPadY   = 13.0f;   // [px] .target-selected padding
constexpr float ChosenPadX   = 10.0f;   // [px] ditto
constexpr float ChosenGap    = 11.0f;   // [px] its gap
constexpr float ChosenRound  =  7.0f;   // [px] its border-radius
constexpr float ChosenArt    = 32.0f;   // [px] its svg
constexpr float ChosenStroke =  1.4f;   // [px] its stroke-width
constexpr float ChosenName   = 12.0f;   // [px] its strong
constexpr float ChosenSub    = 10.0f;   // [px] its span
constexpr float ChosenSubLift=  5.0f;   // [px] that span's margin-top
constexpr float ChosenDot    =  6.0f;   // [px] its i
constexpr float TermSize     = 10.0f;   // [px] .target-properties font-size
constexpr float TermRow      = 13.0f;   // [px] its row gap
constexpr float TermLift     = 23.0f;   // [px] its margin-top
constexpr float TermDrop     = 27.0f;   // [px] its margin-bottom
constexpr float FootPad      = 16.0f;   // [px] .target-footer padding
constexpr float TinySize     =  9.0f;   // [px] .tiny-label font-size
constexpr float TinyTrack    =  1.1f;   // [px] its letter-spacing
constexpr float CodeSize     =  9.0f;   // [px] .target-footer code
constexpr float CodeLift     =  6.0f;   // [px] its margin-top

constexpr float ToolBar      = 40.0f;   // [px] .viewport-toolbar height
constexpr float ToolPadX     = 13.0f;   // [px] its padding
constexpr float ToolGap      =  6.0f;   // [px] its gap
constexpr float TabGap       =  4.0f;   // [px] .view-tabs gap
constexpr float ToolSize     = 10.0f;   // [px] its buttons
constexpr float ToolPadButX  =  9.0f;   // [px] their padding-x
constexpr float ToolPadButY  =  5.0f;   // [px] their padding-y
constexpr float ViewTitleTop = 27.0f;   // [px] .viewport-title top / left
constexpr float ViewEyebrow  =  9.0f;   // [px] its span
constexpr float ViewEyeTrack =  1.5f;   // [px] its letter-spacing
constexpr float ViewHead     = 25.0f;   // [px] its h1
constexpr float ViewHeadLift = 10.0f;   // [px] h1 margin-top
constexpr float ViewHeadDrop =  7.0f;   // [px] h1 margin-bottom
constexpr float ViewNote     = 10.0f;   // [px] its p
constexpr float HelpSize     =  9.0f;   // [px] .viewport-help
constexpr float HelpFoot     = 16.0f;   // [px] its bottom
constexpr float ViewportMin  = 150.0f;  // [px] #viewport min-height

constexpr float PreviewPadTop= 17.0f;   // [px] .preview-controls padding-top
constexpr float PreviewPadX  = 20.0f;   // [px] its padding-x
constexpr float PreviewPadFoot=21.0f;   // [px] its padding-bottom
constexpr float PreviewGap   =  9.0f;   // [px] its gap
constexpr float PreviewButton= 30.0f;   // [px] its button height
constexpr float FieldLabel   = 10.0f;   // [px] .preview-controls .field > span
constexpr float FieldSmallSz =  8.0f;   // [px] its small
constexpr float FieldDrop    =  8.0f;   // [px] .field > span margin-bottom
constexpr float PillTall     = 30.0f;   // [px] .slider-pill height
constexpr float PillGap      = 10.0f;   // [px] its gap
constexpr float SplitWide    = 92.0f;   // [px] .split-value width
constexpr float SplitTall    = 28.0f;   // [px] its height
constexpr float SplitRound   = 14.0f;   // [px] its border-radius
constexpr float SplitValue   = 58.0f;   // [px] its input width
constexpr float SplitSize    = 11.0f;   // [px] its font-size
constexpr float RangeTall    = 26.0f;   // [px] input[type=range] height
constexpr float RangeRound   = 30.0f;   // [px] its border-radius
constexpr float RangeKnobBox = 24.0f;   // [px] its thumb

constexpr float MetricPadY   = 19.0f;   // [px] .metrics padding-y
constexpr float MetricPadX   = 10.0f;   // [px] its padding-x
constexpr float MetricValue  = 23.0f;   // [px] .metrics strong
constexpr float MetricUnit   = 10.0f;   // [px] its small
constexpr float MetricKeySz  =  7.0f;   // [px] .metrics span
constexpr float MetricKeyLift=  8.0f;   // [px] that span's margin-top
constexpr float MetricTrack  =  0.8f;   // [px] its letter-spacing

constexpr float ScrollPad    = 12.0f;   // [px] .inspector-scroll padding
constexpr float CardPadY     = 20.0f;   // [px] .card padding-y
constexpr float CardPadX     = 17.0f;   // [px] its padding-x
constexpr float CardRound    = 22.0f;   // [px] its border-radius
constexpr float CardDrop     = 12.0f;   // [px] its margin-bottom
constexpr float CardHead     = 15.0f;   // [px] h2
constexpr float CardOrdSize  =  9.0f;   // [px] .card > header > span
constexpr float CaptionSize  =  9.0f;   // [px] .card-caption
constexpr float CaptionLift  =  7.0f;   // [px] its margin-top
constexpr float ProseSize    = 10.0f;   // [px] .card > p
constexpr float ProseLift    = 13.0f;   // [px] its margin-top
constexpr float ProseLead    =  1.7f;   // [-]  p line-height

constexpr float OptionGap    =  6.0f;   // [px] .material-options gap
constexpr float OptionLift   = 17.0f;   // [px] its margin-top
constexpr float OptionPadY   =  9.0f;   // [px] its buttons' padding-y
constexpr float OptionPadX   =  8.0f;   // [px] their padding-x
constexpr float OptionSize   = 10.0f;   // [px] their font-size
constexpr float OptionSwatch =  7.0f;   // [px] their i
constexpr float OptionInner  =  9.0f;   // [px] their gap
constexpr float ReadoutLift  = 21.0f;   // [px] .material-readout margin-top
constexpr float ReadoutGap   = 12.0f;   // [px] its gap
constexpr float ReadoutSize  = 24.0f;   // [px] its strong
constexpr float ReadoutUnitSz=  9.0f;   // [px] its small
constexpr float ReadoutUnitGap= 4.0f;   // [px] that small's margin-left
constexpr float ReadoutKeySz =  7.0f;   // [px] its span
constexpr float ReadoutKeyLift= 7.0f;   // [px] that span's margin-top
constexpr float ReadoutTrack =  0.9f;   // [px] its letter-spacing

constexpr float GraphLift    = 15.0f;   // [px] .impact-graph padding-top
constexpr float GraphTall    = 100.0f;  // [px] its svg height
constexpr float GraphBoxW    = 280.0f;  // [-]  its viewBox
constexpr float GraphBoxH    = 100.0f;  // [-]  ditto
constexpr float SpotSize     =  9.0f;   // [px] .impact-location font-size
constexpr float SpotLift     = 13.0f;   // [px] its padding-top
constexpr float SpotGap      =  8.0f;   // [px] its gap
constexpr float SpotInner    =  5.0f;   // [px] its div gap
constexpr float SpotInput    = 10.0f;   // [px] its input font-size
constexpr float SpotInputPadY=  6.0f;   // [px] its input padding-y
constexpr float SpotButton   =  9.0f;   // [px] its button font-size
constexpr float SpotButtonPad=  5.0f;   // [px] its padding

constexpr float QualityLift  = 12.0f;   // [px] .quality-graph margin-top
constexpr float QualityBleed =  6.0f;   // [px] its negative side margin
constexpr float QualityTall  = 126.0f;  // [px] its svg height
constexpr float QualityBoxW  = 280.0f;  // [-]  its viewBox
constexpr float QualityBoxH  = 126.0f;  // [-]  ditto
constexpr float CheckGap     =  5.0f;   // [px] .quality-checks gap
constexpr float CheckSize    =  8.0f;   // [px] its span font-size
constexpr float CheckPadX    =  6.0f;   // [px] its padding-x
constexpr float CheckPadY    =  5.0f;   // [px] its padding-y
constexpr float CheckRound   =  4.0f;   // [px] its border-radius

constexpr float ModeLift     = 16.0f;   // [px] .mode-options margin-top
constexpr float ModePad      =  4.0f;   // [px] its padding and gap
constexpr float ModeRound    =  8.0f;   // [px] its border-radius
constexpr float ModeSize     = 11.0f;   // [px] its buttons
constexpr float ButtonTall   = 30.0f;   // [px] a 7px-padded 11px button's own height
constexpr float SdfLift      = 16.0f;   // [px] .sdf-authoring margin-top
constexpr float SdfPad       = 14.0f;   // [px] its padding-top
constexpr float SdfRule      =  1.0f;   // [px] its border-top
constexpr float SdfSize      = 11.0f;   // [px] .sdf-request font-size
constexpr float SdfBox       = 16.0f;   // [px] its checkbox
constexpr float SdfNote      =  9.0f;   // [px] .sdf-authoring p
constexpr float SelectTall   = 32.0f;   // [px] select height
constexpr float SelectPad    =  9.0f;   // [px] its padding
constexpr float ReceiptPad   = 13.0f;   // [px] .bake-receipt padding
constexpr float ReceiptGap   = 11.0f;   // [px] its gap
constexpr float ReceiptRound =  7.0f;   // [px] its border-radius
constexpr float ReceiptLift  = 16.0f;   // [px] its margin
constexpr float ReceiptDot   =  6.0f;   // [px] its dot
constexpr float ReceiptHead  = 11.0f;   // [px] its strong
constexpr float ReceiptNote  =  9.0f;   // [px] its small
constexpr float ReceiptLead  =  1.6f;   // [-]  that small's line-height
constexpr float ReceiptLift2 =  4.0f;   // [px] its margin-top
constexpr float ActionGap    =  6.0f;   // [px] .bake-actions gap
constexpr float ActionSize   = 10.0f;   // [px] its buttons
constexpr float ListLift     = 18.0f;   // [px] .verification-card dl margin-top
constexpr float ListGap      = 12.0f;   // [px] its gap
constexpr float ListSize     = 10.0f;   // [px] its font-size

constexpr float StatusBar    = 26.0f;   // [px] .statusbar height
constexpr float StatusPadX   = 13.0f;   // [px] its padding
constexpr float StatusSize   =  9.0f;   // [px] its font-size
constexpr float StatusTailSz =  8.0f;   // [px] its last span
constexpr float StatusTrack  =  0.6f;   // [px] that span's letter-spacing
constexpr float StatusDotBox =  4.0f;   // [px] .statusbar i
constexpr float StatusDotGap =  7.0f;   // [px] its margin

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE MATERIAL TABLE
//------------------------------------------------------------------------------------------------------------------------
// MaterialNames (FractureSpecification.js:22) gives the six offered keys and their labels in iteration
//    order; MATERIALS (materials.ts) gives each one its crack resistance, density and swatch. The
//    descriptions are FracturePanel.js:142. All three are keyed the same and are kept as one table.

enum class Material { Concrete, Rock, Wood, Glass, Tempered, Plastic, Count };

struct MaterialRow
{
    const char* Key;
    const char* Name;        // MaterialNames
    int         Toughness;   // [J/m^2] MATERIALS.Gc
    int         Density;     // [kg/m^3] MATERIALS.density
    ImU32       Swatch;      // MATERIALS.color
    const char* Description; // Descriptions
};

inline constexpr MaterialRow Materials[] =
{
    { "concrete", "Concrete",      140, 2350, IM_COL32(0x9d, 0x9d, 0x97, 255),
      "Energy-limited bulk cuts. Reinforcement is not represented." },
    { "rock",     "Stone",          95, 2700, IM_COL32(0x7c, 0x7b, 0x78, 255),
      "Brittle stone fracture with its own crack resistance; no surface-noise dressing." },
    { "wood",     "Wood",          320,  520, IM_COL32(0xb5, 0x85, 0x4a, 255),
      "Local X grain biases longitudinal splits; cross-grain cuts cost more energy." },
    { "glass",    "Glass",           7, 2500, IM_COL32(0xcf, 0xe6, 0xea, 255),
      "Brittle response. Thin panes split through their thickness with no discarded crack cells." },
    { "tempered", "Tempered glass",  7, 2500, IM_COL32(0xcf, 0xe6, 0xea, 255),
      "Stored elastic energy supports finer fracture, bounded by the quality controls." },
    { "plastic",  "ABS plastic",   460, 1050, IM_COL32(0xd8, 0x55, 0x2f, 255),
      "High crack resistance limits fragmentation. Plastic deformation is not simulated." },
};

static_assert(sizeof(Materials) / sizeof(Materials[0]) == size_t(Material::Count),
              "the six offered materials and the enumeration agree");

// Number.prototype.toLocaleString() in an en locale: thousands separated by commas.
inline void Grouped(int Value, char* Out, size_t Room)
{
    char Plain[24];
    std::snprintf(Plain, sizeof(Plain), "%d", Value < 0 ? -Value : Value);
    const int Digits = int(std::strlen(Plain));
    size_t Write = 0;
    if (Value < 0 && Write + 1 < Room) Out[Write++] = '-';
    for (int I = 0; I < Digits; ++I)
    {
        if (I > 0 && (Digits - I) % 3 == 0 && Write + 1 < Room) Out[Write++] = ',';
        if (Write + 1 < Room) Out[Write++] = Plain[I];
    }
    Out[Write < Room ? Write : Room - 1] = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MODEL
//------------------------------------------------------------------------------------------------------------------------
// Everything the page displays, in the state the host hands it. FracturePanel.js derives most of this in
//    Refresh(); the derivations that are pure text live here so the harness can pin them.

enum class Mode { Dynamic, Baked };
enum class Freshness { None, Stale, Ready };
enum class Primitive { Cube, Sphere, Cylinder, Cone, Torus, Count };

inline constexpr const char* PrimitiveName[] = { "cube", "sphere", "cylinder", "cone", "torus" };

struct Recipe
{
    bool      Enabled       = true;
    Mode      Run           = Mode::Dynamic;
    bool      PieceSdf      = false;
    int       SdfResolution = 64;
    Material  Stock         = Material::Concrete;
    float     Energy        = 2500.0f;
    int       Seed          = 42;
    int       Ceiling       = 48;
    float     MinimumSize   = 0.045f;
    float     X = 0.0f, Y = 0.0f, Z = 0.0f;
};

struct Subject
{
    Recipe      Settings;
    const char* OwnerName   = "Geometry specimen";
    const char* OwnerId     = "standalone-specimen";
    Primitive   Shape        = Primitive::Cube;
    bool        Linked      = true;        // opened from a scene object, so the specimen picker is hidden
    float       Scale[3]    = { 1.0f, 1.0f, 1.0f };
    const char* Dimensions  = "1.000 \xc3\x97 1.000 \xc3\x97 1.000 m";
    const char* SourceVolume= "1.0000 m\xc2\xb3";
    const char* Storage     = "Browser \xc2\xb7 per object";
    Freshness   Stored      = Freshness::None;
    int         StoredParts = 0;
    float       StoredKiB   = 0.0f;
    bool        ShowingFragments = false;
    bool        Wireframe   = false;
    int         Separation  = 18;          // [%] inspection-only fragment separation
    // The four metrics and the receipt. Before a run the browser shows one fragment, full volume and an
    //    em dash for everything the solver would have produced.
    int         Pieces      = 1;
    float       Occupied    = 100.0f;      // [%]
    const char* Quality     = "\xe2\x80\x94";
    const char* Closure     = "Closed";
    const char* Triangles   = "\xe2\x80\x94";
    const char* Refused     = "\xe2\x80\x94";
    const char* VolumeError = "\xe2\x80\x94";
    const char* Generation  = "\xe2\x80\x94";
    const char* Status      = "Ready";
};

inline const MaterialRow& Stock(const Subject& Specimen) { return Materials[size_t(Specimen.Settings.Stock)]; }

// Refresh(): "DYNAMIC / GEOMETRY", or "DISABLED / GEOMETRY" when the card's switch is off.
inline void ExecutionLabel(const Subject& Specimen, char* Out, size_t Room)
{
    const char* Lead = !Specimen.Settings.Enabled ? "DISABLED" : Specimen.Settings.Run == Mode::Dynamic ? "DYNAMIC" : "BAKED";
    std::snprintf(Out, Room, "%s / GEOMETRY", Lead);
}

inline void ViewportCaption(const Subject& Specimen, char* Out, size_t Room)
{
    if (!Specimen.Settings.Enabled) { std::snprintf(Out, Room, "Fracture disabled in the object inspector"); return; }
    if (Specimen.ShowingFragments)
        std::snprintf(Out, Room, "%d closed fragments \xc2\xb7 %s", Specimen.Pieces, Stock(Specimen).Name);
    else
        std::snprintf(Out, Room, "Source geometry \xc2\xb7 %s response", Stock(Specimen).Name);
}

inline const char* ModeDescription(const Subject& Specimen)
{
    return Specimen.Settings.Run == Mode::Dynamic
        ? "Generate geometry on demand from this object\xe2\x80\x99s recipe."
        : "Use the stored fragment geometry\xe2\x80\x94no fracture generation during replay.";
}

inline const char* StoredStatus(const Subject& Specimen)
{
    if (Specimen.Stored == Freshness::Ready) return Specimen.Settings.PieceSdf ? "Geometry ready \xc2\xb7 SDF pending" : "Baked \xc2\xb7 ready";
    return Specimen.Stored == Freshness::Stale ? "Bake is stale" : "Not baked";
}

inline void StoredDetail(const Subject& Specimen, char* Out, size_t Room)
{
    if (Specimen.Stored == Freshness::None) { std::snprintf(Out, Room, "No stored fragment geometry"); return; }
    std::snprintf(Out, Room, "%d closed pieces \xc2\xb7 %.1f KiB \xc2\xb7 %s", Specimen.StoredParts, double(Specimen.StoredKiB),
                  Specimen.Stored == Freshness::Ready ? "per object" : "geometry / recipe changed");
}

inline ImU32 StoredDot(const Subject& Specimen)
{
    return Specimen.Stored == Freshness::Ready ? DotReady : Specimen.Stored == Freshness::Stale ? DotStale : DotIdle;
}

inline const char* StoredAction(const Subject& Specimen)
{
    return Specimen.Settings.PieceSdf ? "Bake geometry" : Specimen.Stored != Freshness::None ? "Rebake object" : "Bake object";
}

inline const char* FractureAction(const Subject& Specimen)
{
    return Specimen.Settings.Run == Mode::Dynamic ? "Fracture object" : "Show baked fracture";
}

// "Analytical cube", except the thin pane, which names its own plate.
inline void SourceLabel(const Subject& Specimen, char* Out, size_t Room)
{
    std::snprintf(Out, Room, "Analytical %s", PrimitiveName[size_t(Specimen.Shape)]);
}

inline void ScaleLabel(const Subject& Specimen, char* Out, size_t Room)
{
    std::snprintf(Out, Room, "%.2f \xc3\x97 %.2f \xc3\x97 %.2f", double(Specimen.Scale[0]), double(Specimen.Scale[1]),
                  double(Specimen.Scale[2]));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TEXT HELPERS
//------------------------------------------------------------------------------------------------------------------------

// One line of type centred in a box of BoxTall, the way a flex row with align-items:center lays it out.
inline void Line(ImDrawList* Draw, ImFont* Face, float X, float BoxTop, float BoxTall, float Size,
                 ImU32 Colour, const char* Body, Kit::Anchor Side = Kit::Anchor::Start)
{
    Kit::Inked(Draw, Face, X, BoxTop + (BoxTall - Kit::Grind(Size)) * 0.5f + Kit::AscentShare * Size,
               Size, Colour, Body, Side);
}

inline void TrackedLine(ImDrawList* Draw, ImFont* Face, float X, float BoxTop, float BoxTall, float Size,
                        ImU32 Colour, const char* Body, float Extra)
{
    Kit::Tracked(Draw, Face, X, BoxTop + (BoxTall - Kit::Grind(Size)) * 0.5f, Size, Colour, Body, Extra);
}

inline void TrackedRight(ImDrawList* Draw, ImFont* Face, float Right, float BoxTop, float BoxTall,
                         float Size, ImU32 Colour, const char* Body, float Extra)
{
    const float Run = Kit::TrackedWidth(Face, Size, Body, Extra);
    TrackedLine(Draw, Face, Right - Run, BoxTop, BoxTall, Size, Colour, Body, Extra);
}

// p { font-size: 11px; line-height: 1.7 } — the cards override the size, never the leading.
inline float ProseLeading(float Size) { return Size * ProseLead; }

inline float ProseHeight(ImFont* Face, float Size, float Wide, const char* Body)
{
    Kit::TrackedLine Lines[16];
    return float(ImMax(1, Kit::TrackedWrap(Face, Size, Body, 0.0f, Wide, Lines, 16))) * ProseLeading(Size);
}

inline float PaintProse(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Size, float Wide, ImU32 Colour,
                        const char* Body)
{
    Kit::TrackedLine Lines[16];
    const int Count = ImMax(1, Kit::TrackedWrap(Face, Size, Body, 0.0f, Wide, Lines, 16));
    const float Leading = ProseLeading(Size);
    for (int I = 0; I < Count; ++I)
    {
        char Piece[256];
        const size_t Taken = ImMin(sizeof(Piece) - 1, size_t(Lines[I].To - Lines[I].From));
        std::memcpy(Piece, Lines[I].From, Taken);
        Piece[Taken] = 0;
        Line(Draw, Face, Spot.x, Spot.y + float(I) * Leading, Leading, Size, Colour, Piece);
    }
    return float(Count) * Leading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REUSABLE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// A plain button: 1 px border, 5 px radius, the label centred. Disabled is opacity .35, which over these
//    backgrounds reads as a scrim rather than a different fill.
inline void PaintButton(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall, float Size,
                        const char* Label, bool Primary = false, bool Enabled = true, bool Pressed = false,
                        bool Chromeless = false)
{
    const ImVec2 Far { Spot.x + Wide, Spot.y + Tall };
    if (Primary)
    {
        Draw->AddRectFilled(Spot, Far, PrimaryFill, ButtonRound);
    }
    else if (!Chromeless || Pressed)
    {
        Draw->AddRectFilled(Spot, Far, Pressed ? PressedFill : ButtonFill, ButtonRound);
        if (!Chromeless) Draw->AddRect(Spot, Far, ButtonEdge, ButtonRound);
    }
    const ImU32 Ink = Primary ? PrimaryInk : Pressed ? PressedInk : PageInk;
    Line(Draw, Face, Spot.x + Wide * 0.5f, Spot.y, Tall, Size, Ink, Label, Kit::Anchor::Middle);
    if (!Enabled) Draw->AddRectFilled(Spot, Far, Disabled, ButtonRound);
}

// .slider-pill: the 92 px split value box, a 10 px gap, then the range filling what is left.
inline void PaintSliderPill(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Value,
                            const char* Unit, float Fill, bool Enabled = true)
{
    const float Top = Spot.y + (PillTall - SplitTall) * 0.5f;
    Draw->AddRectFilled({ Spot.x, Top }, { Spot.x + SplitValue, Top + SplitTall }, InputFill, SplitRound,
                        ImDrawFlags_RoundCornersLeft);
    Draw->AddRectFilled({ Spot.x + SplitValue, Top }, { Spot.x + SplitWide, Top + SplitTall }, SplitUnit,
                        SplitRound, ImDrawFlags_RoundCornersRight);
    Draw->AddRect({ Spot.x, Top }, { Spot.x + SplitWide, Top + SplitTall }, SplitEdge, SplitRound);
    Draw->AddLine({ Spot.x + SplitValue, Top }, { Spot.x + SplitValue, Top + SplitTall }, SplitEdge);
    Line(Draw, Face, Spot.x + SplitValue * 0.5f, Top, SplitTall, SplitSize, SplitInk, Value,
         Kit::Anchor::Middle);
    Line(Draw, Face, Spot.x + (SplitValue + SplitWide) * 0.5f, Top, SplitTall, SplitSize, SplitUnitInk, Unit,
         Kit::Anchor::Middle);

    const float TrackX = Spot.x + SplitWide + PillGap;
    const float TrackW = ImMax(RangeKnobBox, Wide - SplitWide - PillGap);
    const float TrackY = Spot.y + (PillTall - RangeTall) * 0.5f;
    const float Share  = ImClamp(Fill, 0.0f, 1.0f);
    Draw->AddRectFilled({ TrackX, TrackY }, { TrackX + TrackW, TrackY + RangeTall }, RangeEmpty, RangeRound);
    if (Share > 0.0f)
        Draw->PushClipRect({ TrackX, TrackY }, { TrackX + TrackW * Share, TrackY + RangeTall }, true);
    Draw->AddRectFilled({ TrackX, TrackY }, { TrackX + TrackW, TrackY + RangeTall }, RangeFilled, RangeRound);
    if (Share > 0.0f) Draw->PopClipRect();
    // The thumb is 24 px and the browser keeps it inside the track, so its centre runs over the inner span.
    const float KnobX = TrackX + RangeKnobBox * 0.5f + (TrackW - RangeKnobBox) * Share;
    Draw->AddCircleFilled({ KnobX, TrackY + RangeTall * 0.5f }, RangeKnobBox * 0.5f, RangeKnob, 48);
    if (!Enabled)
        Draw->AddRectFilled({ Spot.x, Spot.y }, { Spot.x + Wide, Spot.y + PillTall }, Disabled, RangeRound);
}

// .field: a 10 or 11 px label, 8 px, then the control.
inline float FieldHeight(float LabelSize) { return Kit::Grind(LabelSize) + FieldDrop + PillTall; }

inline void PaintField(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float LabelSize,
                       const char* Label, const char* Value, const char* Unit, float Fill,
                       const char* Trailing = nullptr, bool Enabled = true)
{
    Kit::Inked(Draw, Face, Spot.x, Spot.y + Kit::AscentShare * LabelSize, LabelSize, FieldInk, Label);
    if (Trailing != nullptr)
        Kit::Inked(Draw, Face, Spot.x + Wide, Spot.y + Kit::AscentShare * LabelSize, FieldSmallSz,
                   FieldSmall, Trailing, Kit::Anchor::End);
    PaintSliderPill(Draw, Face, { Spot.x, Spot.y + Kit::Grind(LabelSize) + FieldDrop }, Wide, Value, Unit,
                    Fill, Enabled);
}

// select, drawn closed: the box, the value and the chevron the browser's own control shows.
inline void PaintSelect(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Value)
{
    const ImVec2 Far { Spot.x + Wide, Spot.y + SelectTall };
    Draw->AddRectFilled(Spot, Far, InputFill, ButtonRound);
    Draw->AddRect(Spot, Far, InputEdge, ButtonRound);
    Line(Draw, Face, Spot.x + SelectPad, Spot.y, SelectTall, SdfSize, PageInk, Value);
    const float CX = Far.x - 14.0f, CY = Spot.y + SelectTall * 0.5f;
    Draw->AddLine({ CX - 4.0f, CY - 2.0f }, { CX, CY + 2.0f }, SplitUnitInk, 1.2f);
    Draw->AddLine({ CX, CY + 2.0f }, { CX + 4.0f, CY - 2.0f }, SplitUnitInk, 1.2f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE SPECIMEN LINE ART (REFRESH)
//------------------------------------------------------------------------------------------------------------------------
// FracturePanel.js Refresh() swaps the 60 x 60 svg's contents by primitive. Four shapes; anything that is
//    not a sphere, cylinder or cone falls through to the cube, which is what the HTML ships inline.

inline void PaintSpecimen(ImDrawList* Draw, ImVec2 Spot, float Box, Primitive Shape)
{
    const float Scale = Box / 60.0f;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * Scale, Spot.y + Y * Scale }; };
    const float Thick = ChosenStroke * Scale;
    auto Ring = [&](float CX, float CY, float RX, float RY)
    {
        Draw->AddEllipse(At(CX, CY), { RX * Scale, RY * Scale }, SpecimenInk, 0.0f, 48, Thick);
    };
    switch (Shape)
    {
    case Primitive::Sphere:
        Ring(30, 30, 23, 23); Ring(30, 30, 10, 23); Ring(30, 30, 23, 9);
        break;
    case Primitive::Cylinder:
    {
        // "M9 13v33c0 11 42 11 42 0V13M9 46c0-11 42-11 42 0"
        Ring(30, 13, 21, 8);
        Draw->AddLine(At(9, 13), At(9, 46), SpecimenInk, Thick);
        Draw->AddLine(At(51, 13), At(51, 46), SpecimenInk, Thick);
        Draw->AddBezierCubic(At(9, 46), At(9, 57), At(51, 57), At(51, 46), SpecimenInk, Thick, 24);
        Draw->AddBezierCubic(At(9, 46), At(9, 35), At(51, 35), At(51, 46), SpecimenInk, Thick, 24);
        break;
    }
    case Primitive::Cone:
        Draw->AddLine(At(9, 46), At(30, 6), SpecimenInk, Thick);
        Draw->AddLine(At(30, 6), At(51, 46), SpecimenInk, Thick);
        Ring(30, 46, 21, 8);
        break;
    default:
    {
        // "m30 5 23 13v26L30 56 7 44V18Z M7 18l23 13 23-13M30 31v25M30 5v26"
        const ImVec2 Hull[6] = { At(30, 5), At(53, 18), At(53, 44), At(30, 56), At(7, 44), At(7, 18) };
        for (int I = 0; I < 6; ++I) Draw->AddLine(Hull[I], Hull[(I + 1) % 6], SpecimenInk, Thick);
        Draw->AddLine(At(7, 18), At(30, 31), SpecimenInk, Thick);
        Draw->AddLine(At(30, 31), At(53, 18), SpecimenInk, Thick);
        Draw->AddLine(At(30, 5), At(30, 56), SpecimenInk, Thick);
        break;
    }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           THE QUALITY ILLUSTRATION (FRACTUREPROJECTION.JS)
//------------------------------------------------------------------------------------------------------------------------
// QualityGlyph(Ceiling, MinimumSize): a Voronoi tiling of a 228 x 72 rectangle by a golden-ratio lattice
//    of sites, each cell shrunk toward its own centroid by a gap that grows with the minimum span. It is
//    the sibling of FractureGlyph(), which FractureCardSurface.h already carries, and it shares Clip(),
//    Cells(), Shade() and Facet() with it — those live there and are reused rather than copied.

struct Spot2 { double X = 0.0, Y = 0.0; };
using Outline = std::vector<Spot2>;

inline Outline ClipHalf(const Outline& Shape, Spot2 Direction, double Distance)
{
    Outline Out;
    const size_t Count = Shape.size();
    for (size_t I = 0; I < Count; ++I)
    {
        const Spot2& Here = Shape[I];
        const Spot2& Next = Shape[(I + 1) % Count];
        const double Offset     = Here.X * Direction.X + Here.Y * Direction.Y - Distance;
        const double NextOffset = Next.X * Direction.X + Next.Y * Direction.Y - Distance;
        if (Offset <= 0.0) Out.push_back(Here);
        if ((Offset < 0.0) != (NextOffset < 0.0))
        {
            const double Fraction = Offset / (Offset - NextOffset);
            Out.push_back({ Here.X + (Next.X - Here.X) * Fraction, Here.Y + (Next.Y - Here.Y) * Fraction });
        }
    }
    return Out;
}

// The site count: 6 + 1.6 sqrt(ceiling) - 30 minimum-size, rounded, clamped to 6 ... 28.
inline int QualitySiteCount(int Ceiling, float MinimumSize)
{
    const double Raw = 6.0 + std::sqrt(double(Ceiling)) * 1.6 - double(MinimumSize) * 30.0;
    return int(ImClamp(double(std::lround(Raw)), 6.0, 28.0));
}

inline std::vector<Spot2> QualitySites(int Count)
{
    std::vector<Spot2> Sites;
    Sites.reserve(size_t(Count));
    for (int I = 0; I < Count; ++I)
    {
        const double A = std::fmod(double(I) * 0.61803398875 + 0.1, 1.0);
        const double B = std::fmod(double(I) * 0.41421356237 + 0.17, 1.0);
        Sites.push_back({ A * 226.0 - 113.0, B * 69.0 - 34.5 });
    }
    return Sites;
}

inline std::vector<Outline> QualityCells(int Ceiling, float MinimumSize)
{
    const std::vector<Spot2> Sites = QualitySites(QualitySiteCount(Ceiling, MinimumSize));
    const Outline Frame { { -114, -36 }, { 114, -36 }, { 114, 36 }, { -114, 36 } };
    std::vector<Outline> Out;
    Out.reserve(Sites.size());
    for (size_t I = 0; I < Sites.size(); ++I)
    {
        Outline Shape = Frame;
        for (size_t S = 0; S < Sites.size(); ++S)
        {
            if (S == I) continue;
            const Spot2 Direction { Sites[S].X - Sites[I].X, Sites[S].Y - Sites[I].Y };
            const double Distance = (Sites[S].X * Sites[S].X + Sites[S].Y * Sites[S].Y
                                   - Sites[I].X * Sites[I].X - Sites[I].Y * Sites[I].Y) / 2.0;
            Shape = ClipHalf(Shape, Direction, Distance);
        }
        Out.push_back(Shape);
    }
    return Out;
}

inline ImU32 QualityShade(double Tone, bool Green)
{
    const int Level = int(std::lround(ImClamp(Tone, 35.0, 210.0)));
    return Green ? IM_COL32(Level - 12, Level + 3, Level - 4, 255)
                 : IM_COL32(Level,      Level + 2, Level + 1, 255);
}

// Facet(), with QualityGlyph's own projection: every vertex is pulled toward the cell's centroid by
//    max(.35, 1 - gap/len), which is what opens the visible seams between pieces.
inline void PaintQualityGlyph(ImDrawList* Draw, ImVec2 Spot, float Wide, int Ceiling, float MinimumSize)
{
    const float Scale = ImMin(Wide / QualityBoxW, 1.0f);
    const ImVec2 Origin { Spot.x + (Wide - QualityBoxW * Scale) * 0.5f, Spot.y };
    auto Place = [&](double X, double Y) { return ImVec2{ Origin.x + float(X) * Scale, Origin.y + float(Y) * Scale }; };
    Draw->AddEllipseFilled(Place(140, 107), { 110.0f * Scale, 8.0f * Scale }, IM_COL32(0, 0, 0, 41));

    const double Gap = 2.0 + double(MinimumSize) * 10.0;
    const std::vector<Outline> Pieces = QualityCells(Ceiling, MinimumSize);
    for (size_t Index = 0; Index < Pieces.size(); ++Index)
    {
        const Outline& Shape = Pieces[Index];
        if (Shape.size() < 3) continue;
        Spot2 Centre;
        for (const Spot2& P : Shape) { Centre.X += P.X / double(Shape.size()); Centre.Y += P.Y / double(Shape.size()); }
        auto Project = [&](Spot2 P)
        {
            const double DX = P.X - Centre.X, DY = P.Y - Centre.Y;
            const double Length = std::hypot(DX, DY) == 0.0 ? 1.0 : std::hypot(DX, DY);
            const double Pull = std::max(0.35, 1.0 - Gap / Length);
            return Spot2{ 140.0 + Centre.X + DX * Pull, 60.0 + Centre.Y + DY * Pull };
        };
        std::vector<Spot2> Surface;
        Surface.reserve(Shape.size());
        for (const Spot2& P : Shape) Surface.push_back(Project(P));
        const Spot2 Interior = Project(Centre);
        const double Tone = 133.0 - Centre.X * 0.36 - Centre.Y * 0.35;
        const size_t Count = Surface.size();
        for (size_t S = 0; S < Count; ++S)
        {
            const Spot2& Here = Surface[S];
            const Spot2& Next = Surface[(S + 1) % Count];
            if (Next.X < Here.X)
                Draw->AddQuadFilled(Place(Here.X, Here.Y), Place(Next.X, Next.Y),
                                    Place(Next.X + 1.0, Next.Y + 7.0), Place(Here.X + 1.0, Here.Y + 7.0),
                                    QualityShade(Tone * 0.42, false));
        }
        for (size_t S = 0; S < Count; ++S)
        {
            const Spot2& Here = Surface[S];
            const Spot2& Next = Surface[(S + 1) % Count];
            Draw->AddTriangleFilled(Place(Here.X, Here.Y), Place(Next.X, Next.Y), Place(Interior.X, Interior.Y),
                                    QualityShade(Tone + std::sin(double(S) * 2.3 + double(Index)) * 15.0,
                                                 Index % 7 == 0));
        }
        for (size_t S = 0; S < Count; ++S)
            Draw->AddLine(Place(Surface[S].X, Surface[S].Y),
                          Place(Surface[(S + 1) % Count].X, Surface[(S + 1) % Count].Y),
                          IM_COL32(212, 223, 212, 48), ImMax(0.6f * Scale, 0.5f));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE IMPACT GRAPH
//------------------------------------------------------------------------------------------------------------------------
// A fixed illustration in the HTML: four path commands in a 280 x 100 viewBox, overflow visible.
//    "M15 10V83H269M15 47H269M79 10V83M143 10V83M207 10V83" grid,
//    "M15 18C65 34 129 59 269 75V83H15Z" area, the same curve as the line, a 3 px dot and two labels.

inline void PaintImpactGraph(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide)
{
    const float Scale = Wide / GraphBoxW;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * Scale, Spot.y + Y * Scale }; };
    const float Hair = ImMax(1.0f * Scale, 0.75f);

    Draw->AddLine(At(15, 10), At(15, 83), GraphRule, Hair);
    Draw->AddLine(At(15, 83), At(269, 83), GraphRule, Hair);
    Draw->AddLine(At(15, 47), At(269, 47), GraphRule, Hair);
    for (float X : { 79.0f, 143.0f, 207.0f }) Draw->AddLine(At(X, 10), At(X, 83), GraphRule, Hair);

    // The curve, flattened once and used for both the wash and the stroke.
    constexpr int Steps = 48;
    ImVec2 Curve[Steps + 1];
    for (int I = 0; I <= Steps; ++I)
    {
        const float T = float(I) / float(Steps), U = 1.0f - T;
        const float X = U * U * U * 15 + 3 * U * U * T * 65 + 3 * U * T * T * 129 + T * T * T * 269;
        const float Y = U * U * U * 18 + 3 * U * U * T * 34 + 3 * U * T * T * 59 + T * T * T * 75;
        Curve[I] = At(X, Y);
    }
    for (int I = 0; I < Steps; ++I)
        Draw->AddQuadFilled(Curve[I], Curve[I + 1], At(269, 83), At(15, 83), GraphWash);
    for (int I = 0; I < Steps; ++I) Draw->AddLine(Curve[I], Curve[I + 1], GraphLine, ImMax(1.5f * Scale, 1.0f));
    Draw->AddCircleFilled(At(15, 18), 3.0f * Scale, GraphMark, 24);

    // The two labels are 8px Arial in the stylesheet; the engine ships DM Sans, which is the one deviation.
    Kit::Inked(Draw, Face, Spot.x + 22.0f * Scale, Spot.y + 16.0f * Scale, 8.0f * Scale, GraphLabel,
               "cut priority");
    Kit::Inked(Draw, Face, Spot.x + 195.0f * Scale, Spot.y + 96.0f * Scale, 8.0f * Scale, GraphLabel,
               "distance \xe2\x86\x92");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------
// The app is `height: 100dvh; display: flex; flex-direction: column`, so the five bands take their fixed
//    heights and the workspace grid absorbs the rest. The breakpoints are the stylesheet's own.

struct Layout
{
    float Wide = 0.0f, Tall = 0.0f;
    float TitleTop = 0.0f, BarTop = 0.0f, WorkTop = 0.0f, WorkTall = 0.0f, StatusTop = 0.0f;
    float LeftX = 0.0f, LeftW = 0.0f;
    float CentreX = 0.0f, CentreW = 0.0f;
    float RightX = 0.0f, RightW = 0.0f;
    float TargetPad = TargetPadX;
    bool  ShowTarget = true;     // @media (max-width: 900px) hides the left pane
};

inline Layout Measure(float Wide, float Tall)
{
    Layout L;
    L.Wide = Wide; L.Tall = Tall;
    L.TitleTop = 0.0f;
    L.BarTop   = TitleBar;
    L.WorkTop  = TitleBar + WorkBar;
    L.StatusTop = Tall - StatusBar;
    L.WorkTall = ImMax(0.0f, L.StatusTop - L.WorkTop);

    float Left = LeftWide, Right = RightWide;
    if (Wide >= 1600.0f)      { Left = LeftWideBig;    Right = RightWideBig; }
    else if (Wide <= 1180.0f) { Left = LeftWideNarrow; Right = RightWideNarrow; L.TargetPad = TargetPadNarrow; }
    if (Wide <= 900.0f)       { L.ShowTarget = false;  Right = 310.0f; }

    if (L.ShowTarget)
    {
        L.LeftX = 0.0f; L.LeftW = Left;
        L.CentreX = Left + GridGap;
        L.RightW = Right;
        L.RightX = Wide - Right;
        L.CentreW = ImMax(CentreMin, L.RightX - GridGap - L.CentreX);
    }
    else
    {
        L.LeftX = 0.0f; L.LeftW = 0.0f;
        L.CentreX = 0.0f;
        L.RightW = Right;
        L.RightX = Wide - Right;
        L.CentreW = ImMax(CentreMin, L.RightX - GridGap);
    }
    return L;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE BANDS
//------------------------------------------------------------------------------------------------------------------------

inline void PaintTitleBar(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& L)
{
    Draw->AddRectFilled({ 0, L.TitleTop }, { L.Wide, L.TitleTop + TitleBar }, TitleFill);
    float Pen = TitlePadX;
    Line(Draw, Light, Pen, L.TitleTop, TitleBar, BrandSize, PageInk, "\xe2\x97\x88");
    Pen += Kit::Measured(Light, BrandSize, "\xe2\x97\x88") + BrandGap;
    TrackedLine(Draw, Regular, Pen, L.TitleTop, TitleBar, TitleSize, PageInk, "Frontier", BrandTrack);
    Pen += Kit::TrackedWidth(Regular, TitleSize, "Frontier", BrandTrack) + TitleGap;
    Draw->AddRectFilled({ Pen, L.TitleTop + (TitleBar - DividerTall) * 0.5f },
                        { Pen + 1.0f, L.TitleTop + (TitleBar + DividerTall) * 0.5f }, Divider);
    Pen += 1.0f + TitleGap;
    Line(Draw, Light, Pen, L.TitleTop, TitleBar, TitleSize, PageInk, "Project-Zero");
    TrackedRight(Draw, Light, L.Wide - TitlePadX, L.TitleTop, TitleBar, TailSize, TailInk,
                 "HTML AUTHORING PREVIEW", TailTrack);
}

inline void PaintWorkspaceBar(ImDrawList* Draw, ImFont* Light, const Layout& L, const Subject& Specimen)
{
    Draw->AddRectFilled({ 0, L.BarTop }, { L.Wide, L.BarTop + WorkBar }, BarFill);
    Draw->AddRectFilled({ 0, L.BarTop + WorkBar - 1.0f }, { L.Wide, L.BarTop + WorkBar }, BarEdge);

    // .workspace-tab is full height with a top-only radius and asymmetric corners — 7 left, 15 right.
    const float TabW = ImMax(TabWide, TabPadX * 2.0f + Kit::Measured(Light, TitleSize, "Fracture")
                                      + TabGlyphGap + Kit::Measured(Light, TitleSize, "\xe2\x86\x97"));
    Draw->AddRectFilled({ 0, L.BarTop }, { TabW, L.BarTop + WorkBar }, TabFill, TabRoundB,
                        ImDrawFlags_RoundCornersTopRight);
    Draw->AddRectFilled({ 0, L.BarTop }, { TabW * 0.5f, L.BarTop + WorkBar }, TabFill, TabRoundA,
                        ImDrawFlags_RoundCornersTopLeft);
    // padding: 14px 20px 0 — the text sits on the padding edge, not centred in the tab.
    Kit::Inked(Draw, Light, TabPadX, L.BarTop + TabPadTop + Kit::AscentShare * TitleSize, TitleSize, PageInk,
               "Fracture");
    Kit::Inked(Draw, Light, TabW - TabPadX, L.BarTop + TabPadTop + Kit::AscentShare * TitleSize, TitleSize,
               TabGlyph, "\xe2\x86\x97", Kit::Anchor::End);

    char Crumb[160];
    std::snprintf(Crumb, sizeof(Crumb), "%s / Fracture", Specimen.OwnerName);
    Line(Draw, Light, TabW + WorkGap, L.BarTop, WorkBar, TitleSize, CrumbInk, Crumb);

    const float ExportW = Kit::Measured(Light, SmallButton, "Export fracture\xe2\x80\xa6") + SmallPadX * 2.0f;
    const float ExportH = Kit::Grind(SmallButton) + SmallPadY * 2.0f + 2.0f;
    PaintButton(Draw, Light, { L.Wide - WorkPadRight - ExportW, L.BarTop + (WorkBar - ExportH) * 0.5f },
                ExportW, ExportH, SmallButton, "Export fracture\xe2\x80\xa6");
}

inline void PaintStatusBar(ImDrawList* Draw, ImFont* Light, const Layout& L, const Subject& Specimen)
{
    Draw->AddRectFilled({ 0, L.StatusTop }, { L.Wide, L.Tall }, StatusFill);
    Draw->AddRectFilled({ 0, L.StatusTop }, { L.Wide, L.StatusTop + 1.0f }, HeadEdge);
    Line(Draw, Light, StatusPadX, L.StatusTop, StatusBar, StatusSize, StatusInk, Specimen.Status);

    // "HTML ONLY <i></i> NATIVE PORT PENDING" — the dot is an inline 4 px bullet with 7 px either side.
    const float TailRun = Kit::TrackedWidth(Light, StatusTailSz, "HTML ONLY", StatusTrack)
                        + StatusDotGap * 2.0f + StatusDotBox
                        + Kit::TrackedWidth(Light, StatusTailSz, "NATIVE PORT PENDING", StatusTrack);
    float Pen = L.Wide - StatusPadX - TailRun;
    TrackedLine(Draw, Light, Pen, L.StatusTop, StatusBar, StatusTailSz, StatusTail, "HTML ONLY", StatusTrack);
    Pen += Kit::TrackedWidth(Light, StatusTailSz, "HTML ONLY", StatusTrack) + StatusDotGap;
    Draw->AddCircleFilled({ Pen + StatusDotBox * 0.5f, L.StatusTop + StatusBar * 0.5f }, StatusDotBox * 0.5f,
                          StatusDot, 16);
    Pen += StatusDotBox + StatusDotGap;
    TrackedLine(Draw, Light, Pen, L.StatusTop, StatusBar, StatusTailSz, StatusTail, "NATIVE PORT PENDING",
                StatusTrack);
}

// .pane-heading, shared by both asides.
inline void PaintPaneHeading(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Title,
                             const char* Note)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + PaneHead }, HeadFill);
    Draw->AddRectFilled({ Spot.x, Spot.y + PaneHead - 1.0f }, { Spot.x + Wide, Spot.y + PaneHead }, HeadEdge);
    Line(Draw, Light, Spot.x + PaneHeadPadX, Spot.y, PaneHead, PaneHeadSize, HeadInk, Title);
    TrackedRight(Draw, Light, Spot.x + Wide - PaneHeadPadX, Spot.y, PaneHead, PaneNoteSize, HeadNote, Note,
                 PaneNoteTrack);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE OBJECT PANE
//------------------------------------------------------------------------------------------------------------------------

inline void PaintTargetPane(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& L, const Subject& Specimen)
{
    if (!L.ShowTarget) return;
    const float X = L.LeftX, W = L.LeftW, Top = L.WorkTop, Tall = L.WorkTall;
    Draw->AddRectFilled({ X, Top }, { X + W, Top + Tall }, PaneFill);
    PaintPaneHeading(Draw, Light, { X, Top }, W, "Object", "LOCAL SPACE");

    const float Pad = L.TargetPad;
    const float Inner = W - Pad * 2.0f;
    float Y = Top + PaneHead + TargetPadY;

    // .target-selected
    const float ChosenTall = ImMax(ChosenArt, Kit::Grind(ChosenName) + ChosenSubLift + Kit::Grind(ChosenSub))
                           + ChosenPadY * 2.0f;
    Draw->AddRectFilled({ X + Pad, Y }, { X + Pad + Inner, Y + ChosenTall }, SelectedFill, ChosenRound);
    Draw->AddRect({ X + Pad, Y }, { X + Pad + Inner, Y + ChosenTall }, SelectedEdge, ChosenRound);
    PaintSpecimen(Draw, { X + Pad + ChosenPadX, Y + (ChosenTall - ChosenArt) * 0.5f }, ChosenArt, Specimen.Shape);
    const float TextX = X + Pad + ChosenPadX + ChosenArt + ChosenGap;
    const float Stack = Kit::Grind(ChosenName) + ChosenSubLift + Kit::Grind(ChosenSub);
    float TextY = Y + (ChosenTall - Stack) * 0.5f;
    Kit::Inked(Draw, Regular, TextX, TextY + Kit::AscentShare * ChosenName, ChosenName, PageInk, Specimen.OwnerName);
    TextY += Kit::Grind(ChosenName) + ChosenSubLift;
    // text-transform: capitalize
    char Capital[32];
    std::snprintf(Capital, sizeof(Capital), "%s", PrimitiveName[size_t(Specimen.Shape)]);
    if (Capital[0] >= 'a' && Capital[0] <= 'z') Capital[0] = char(Capital[0] - 'a' + 'A');
    Kit::Inked(Draw, Light, TextX, TextY + Kit::AscentShare * ChosenSub, ChosenSub, SpecimenSub, Capital);
    Draw->AddCircleFilled({ X + Pad + Inner - ChosenPadX - ChosenDot * 0.5f, Y + ChosenTall * 0.5f },
                          ChosenDot * 0.5f, LiveDot, 16);
    Y += ChosenTall + TermLift;

    // .target-properties — a two-column definition list, terms left, values hard right.
    char Scale[48]; ScaleLabel(Specimen, Scale, sizeof(Scale));
    char Source[48]; SourceLabel(Specimen, Source, sizeof(Source));
    const char* Terms[]  = { "Source", "Dimensions", "Object scale", "Source volume", "Storage" };
    const char* Values[] = { Source, Specimen.Dimensions, Scale, Specimen.SourceVolume, Specimen.Storage };
    for (int I = 0; I < 5; ++I)
    {
        const float RowTall = Kit::Grind(TermSize);
        Kit::Inked(Draw, Light, X + Pad, Y + Kit::AscentShare * TermSize, TermSize, TermInk, Terms[I]);
        Kit::Inked(Draw, Light, X + Pad + Inner, Y + Kit::AscentShare * TermSize, TermSize, ValueInk,
                   Values[I], Kit::Anchor::End);
        Y += RowTall + (I < 4 ? TermRow : 0.0f);
    }

    // .target-footer — margin-top:auto pins it to the bottom of the pane.
    const float FootTall = FootPad * 2.0f + Kit::Grind(TinySize) + CodeLift + Kit::Grind(CodeSize);
    const float FootTop = Top + Tall - FootTall;
    Draw->AddRectFilled({ X, FootTop }, { X + W, Top + Tall }, FootFill);
    Draw->AddRectFilled({ X, FootTop }, { X + W, FootTop + 1.0f }, HeadEdge);
    Kit::Tracked(Draw, Light, X + FootPad, FootTop + FootPad, TinySize, TinyInk, "SOURCE OWNER", TinyTrack);
    Kit::Inked(Draw, Light, X + FootPad, FootTop + FootPad + Kit::Grind(TinySize) + CodeLift
                                             + Kit::AscentShare * CodeSize, CodeSize, CodeInk, Specimen.OwnerId);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE VIEWPORT PANE
//------------------------------------------------------------------------------------------------------------------------

inline float PreviewControlsHeight()
{
    return PreviewPadTop + FieldHeight(FieldLabel) + PreviewPadFoot;
}

inline float MetricsHeight()
{
    return MetricPadY * 2.0f + Kit::Grind(MetricValue) + MetricKeyLift + Kit::Grind(MetricKeySz);
}

// CheckerViewport's roster is cube / sphere / cylinder / torus / cone; the editor's Primitive is
//    cube / sphere / cylinder / cone / torus. The two orders differ in their last two entries, so the
//    crossing is spelled out rather than cast.
inline BaseMesh::Solid MarkerPrimitive(Primitive Which)
{
    switch (Which)
    {
    case Primitive::Cube:     return BaseMesh::Solid::Cube;
    case Primitive::Sphere:   return BaseMesh::Solid::Sphere;
    case Primitive::Cylinder: return BaseMesh::Solid::Cylinder;
    case Primitive::Cone:     return BaseMesh::Solid::Cone;
    case Primitive::Torus:    return BaseMesh::Solid::Torus;
    default:                  return BaseMesh::Solid::Cube;
    }
}

inline void PaintViewportPane(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& L, const Subject& Specimen)
{
    const float X = L.CentreX, W = L.CentreW, Top = L.WorkTop, Tall = L.WorkTall;
    Draw->AddRectFilled({ X, Top }, { X + W, Top + Tall }, ViewFill);

    // .viewport-toolbar
    Draw->AddRectFilled({ X, Top }, { X + W, Top + ToolBar }, ToolFill);
    Draw->AddRectFilled({ X, Top + ToolBar - 1.0f }, { X + W, Top + ToolBar }, HeadEdge);
    const float ToolTall = Kit::Grind(ToolSize) + ToolPadButY * 2.0f + 2.0f;
    const float ToolTop = Top + (ToolBar - ToolTall) * 0.5f;
    float Pen = X + ToolPadX;
    auto Chip = [&](const char* Label, bool Pressed, bool Enabled, float Gap)
    {
        const float Run = Kit::Measured(Light, ToolSize, Label) + ToolPadButX * 2.0f;
        PaintButton(Draw, Light, { Pen, ToolTop }, Run, ToolTall, ToolSize, Label, false, Enabled, Pressed, true);
        Pen += Run + Gap;
    };
    Chip("Source", !Specimen.ShowingFragments, true, TabGap);
    Chip("Fragments", Specimen.ShowingFragments, Specimen.ShowingFragments, ToolGap);
    const float WireRun = Kit::Measured(Light, ToolSize, "Wireframe") + ToolPadButX * 2.0f;
    const float FitRun  = Kit::Measured(Light, ToolSize, "Fit") + ToolPadButX * 2.0f;
    Pen = X + W - ToolPadX - FitRun;
    PaintButton(Draw, Light, { Pen, ToolTop }, FitRun, ToolTall, ToolSize, "Fit", false, true, false, true);
    Pen -= ToolGap + WireRun;
    PaintButton(Draw, Light, { Pen, ToolTop }, WireRun, ToolTall, ToolSize, "Wireframe", false, true,
                Specimen.Wireframe, true);

    const float Controls = PreviewControlsHeight();
    const float Metrics  = MetricsHeight();
    const float ViewTop  = Top + ToolBar;
    const float ViewTall = ImMax(ViewportMin, Tall - ToolBar - Controls - Metrics);

    // #viewport — the three.js canvas. No solver runs behind this port, so the canvas is stood in for the
    //    way the shipped editor itself stands it in when WebGL is unavailable: CheckerViewport's
    //    analytical marker for the same primitive, over the same checker. The overlay is as shipped.
    {
        const float CheckTop = ViewTop, CheckTall = ViewTall;
        Draw->PushClipRect({ X, CheckTop }, { X + W, CheckTop + CheckTall }, true);
        const float Box = ImMin(ImMin(W, CheckTall) * 0.46f, 320.0f);
        BaseMesh::PaintMarkerIn(Draw, { X + (W - Box) * 0.5f, CheckTop + (CheckTall - Box) * 0.5f },
                                Box, Box, MarkerPrimitive(Specimen.Shape),
                                Specimen.Settings.Enabled ? SpecimenInk : TinyInk, IM_COL32(23, 23, 23, 117),
                                BaseMesh::MarkerStroke * 0.6f);
        Draw->PopClipRect();
    }
    char Eyebrow[48];  ExecutionLabel(Specimen, Eyebrow, sizeof(Eyebrow));
    char Caption[160]; ViewportCaption(Specimen, Caption, sizeof(Caption));
    float TitleY = ViewTop + ViewTitleTop;
    Kit::Tracked(Draw, Light, X + ViewTitleTop, TitleY, ViewEyebrow, EyebrowInk, Eyebrow, ViewEyeTrack);
    TitleY += Kit::Grind(ViewEyebrow) + ViewHeadLift;
    Kit::Inked(Draw, Regular, X + ViewTitleTop, TitleY + Kit::AscentShare * ViewHead, ViewHead, ViewTitleInk,
               Specimen.OwnerName);
    TitleY += Kit::Grind(ViewHead) + ViewHeadDrop;
    Kit::Inked(Draw, Light, X + ViewTitleTop, TitleY + Kit::AscentShare * ViewNote, ViewNote, ViewNoteInk,
               Caption);
    Kit::Inked(Draw, Light, X + W * 0.5f, ViewTop + ViewTall - HelpFoot - Kit::Grind(HelpSize)
                                              + Kit::AscentShare * HelpSize, HelpSize, HelpInk,
               "Drag to orbit \xc2\xb7 Scroll to zoom \xc2\xb7 Shift-click to place impact", Kit::Anchor::Middle);

    // .preview-controls
    const float ControlTop = ViewTop + ViewTall;
    Draw->AddRectFilled({ X, ControlTop }, { X + W, ControlTop + Controls }, ToolFill);
    Draw->AddRectFilled({ X, ControlTop }, { X + W, ControlTop + 1.0f }, HeadEdge);
    const float ReassembleW = Kit::Measured(Light, ToolSize, "Reassemble") + SmallPadX * 2.0f;
    const float FractureW   = Kit::Measured(Light, ToolSize, FractureAction(Specimen)) + SmallPadX * 2.0f;
    const float FieldW = ImMax(95.0f, W - PreviewPadX * 2.0f - ReassembleW - FractureW - PreviewGap * 2.0f);
    char Separation[16];
    std::snprintf(Separation, sizeof(Separation), "%d", Specimen.Separation);
    PaintField(Draw, Light, { X + PreviewPadX, ControlTop + PreviewPadTop }, FieldW, FieldLabel,
               "Fragment separation", Separation, "%", float(Specimen.Separation) / 100.0f, "inspection only");
    // align-items: flex-end — the two buttons sit on the field's baseline, not its top.
    const float ButtonTop = ControlTop + PreviewPadTop + FieldHeight(FieldLabel) - PreviewButton;
    PaintButton(Draw, Light, { X + PreviewPadX + FieldW + PreviewGap, ButtonTop }, ReassembleW,
                PreviewButton, ToolSize, "Reassemble");
    PaintButton(Draw, Light, { X + PreviewPadX + FieldW + PreviewGap + ReassembleW + PreviewGap, ButtonTop },
                FractureW, PreviewButton, ToolSize, FractureAction(Specimen), true, Specimen.Settings.Enabled);

    // .metrics — four centred columns with hairline dividers between them.
    const float MetricTop = ControlTop + Controls;
    Draw->AddRectFilled({ X, MetricTop }, { X + W, MetricTop + Metrics }, MetricFill);
    Draw->AddRectFilled({ X, MetricTop }, { X + W, MetricTop + 1.0f }, HeadEdge);
    char Pieces[16];   std::snprintf(Pieces, sizeof(Pieces), "%d", Specimen.Pieces);
    char Occupied[24]; std::snprintf(Occupied, sizeof(Occupied), "%.3f", double(Specimen.Occupied));
    const char* Values[4] = { Pieces, Occupied, Specimen.Quality, Specimen.Closure };
    const char* Units [4] = { nullptr, "%", nullptr, nullptr };
    const char* Keys  [4] = { "FRAGMENTS", "OCCUPIED VOLUME", "MIN TRIANGLE QUALITY", "TOPOLOGY" };
    const float Column = (W - MetricPadX * 2.0f) / 4.0f;
    for (int I = 0; I < 4; ++I)
    {
        const float Mid = X + MetricPadX + Column * (float(I) + 0.5f);
        float Run = Kit::Measured(Regular, MetricValue, Values[I]);
        if (Units[I] != nullptr) Run += Kit::Measured(Light, MetricUnit, Units[I]) + 2.0f;
        const float Start = Mid - Run * 0.5f;
        Kit::Inked(Draw, Regular, Start, MetricTop + MetricPadY + Kit::AscentShare * MetricValue, MetricValue,
                   MetricInk, Values[I]);
        if (Units[I] != nullptr)
            Kit::Inked(Draw, Light, Start + Kit::Measured(Regular, MetricValue, Values[I]) + 2.0f,
                       MetricTop + MetricPadY + Kit::AscentShare * MetricValue, MetricUnit, MetricInk,
                       Units[I]);
        Kit::Tracked(Draw, Light,
                     Mid - Kit::TrackedWidth(Light, MetricKeySz, Keys[I], MetricTrack) * 0.5f,
                     MetricTop + MetricPadY + Kit::Grind(MetricValue) + MetricKeyLift, MetricKeySz,
                     MetricKey, Keys[I], MetricTrack);
        if (I < 3)
            Draw->AddLine({ X + MetricPadX + Column * float(I + 1), MetricTop + MetricPadY },
                          { X + MetricPadX + Column * float(I + 1), MetricTop + Metrics - MetricPadY },
                          ReadoutRule);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INSPECTOR CARDS
//------------------------------------------------------------------------------------------------------------------------

inline float CardHeaderHeight() { return Kit::Grind(CardHead); }

// Opens a card: the rounded box, the title and the two-digit ordinal. Returns the content's first y.
inline float PaintCardHead(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                           float Tall, const char* Title, const char* Ordinal)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, CardFill, CardRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, CardEdge, CardRound);
    const float HeadTop = Spot.y + CardPadY;
    Kit::Inked(Draw, Regular, Spot.x + CardPadX, HeadTop + Kit::AscentShare * CardHead, CardHead, CardTitle,
               Title);
    Line(Draw, Light, Spot.x + Wide - CardPadX, HeadTop, Kit::Grind(CardHead), CardOrdSize, CardOrdinal,
         Ordinal, Kit::Anchor::End);
    return HeadTop + CardHeaderHeight();
}

inline float MaterialCardHeight(ImFont* Light, float Wide)
{
    const float Inner = Wide - CardPadX * 2.0f;
    const float OptionTall = Kit::Grind(OptionSize) + OptionPadY * 2.0f + 2.0f;
    return CardPadY * 2.0f + CardHeaderHeight() + CaptionLift + Kit::Grind(CaptionSize)
         + OptionLift + OptionTall * 3.0f + OptionGap * 2.0f
         + ReadoutLift + Kit::Grind(ReadoutSize) + ReadoutKeyLift + Kit::Grind(ReadoutKeySz)
         + ProseLift + ProseHeight(Light, ProseSize, Inner, Materials[0].Description);
}

inline void PaintMaterialCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                              float Tall, const Subject& Specimen)
{
    float Y = PaintCardHead(Draw, Light, Regular, Spot, Wide, Tall, "Material", "01");
    const float Inner = Wide - CardPadX * 2.0f;
    const float X = Spot.x + CardPadX;

    Y += CaptionLift;
    Kit::Inked(Draw, Light, X, Y + Kit::AscentShare * CaptionSize, CaptionSize, ProseInk,
               "Fracture response \xc2\xb7 not surface appearance");
    Y += Kit::Grind(CaptionSize) + OptionLift;

    // .material-options — a 2 x 3 grid in MaterialNames order.
    const float OptionTall = Kit::Grind(OptionSize) + OptionPadY * 2.0f + 2.0f;
    const float OptionWide = (Inner - OptionGap) * 0.5f;
    for (int I = 0; I < int(Material::Count); ++I)
    {
        const MaterialRow& One = Materials[I];
        const bool Picked = size_t(Specimen.Settings.Stock) == size_t(I);
        const ImVec2 At { X + float(I % 2) * (OptionWide + OptionGap),
                          Y + float(I / 2) * (OptionTall + OptionGap) };
        Draw->AddRectFilled(At, { At.x + OptionWide, At.y + OptionTall }, Picked ? OptionPicked : OptionFill,
                            ButtonRound);
        Draw->AddRect(At, { At.x + OptionWide, At.y + OptionTall }, Picked ? OptionPickEdge : ButtonEdge,
                      ButtonRound);
        Draw->AddCircleFilled({ At.x + OptionPadX + OptionSwatch * 0.5f, At.y + OptionTall * 0.5f },
                              OptionSwatch * 0.5f, One.Swatch, 16);
        Line(Draw, Light, At.x + OptionPadX + OptionSwatch + OptionInner, At.y, OptionTall, OptionSize,
             Picked ? OptionPickInk : OptionInk, One.Name);
    }
    Y += OptionTall * 3.0f + OptionGap * 2.0f + ReadoutLift;

    // .material-readout — two figures with their units and keys, split by a hairline.
    const MaterialRow& Now = Stock(Specimen);
    char Toughness[16], Density[16];
    Grouped(Now.Toughness, Toughness, sizeof(Toughness));
    Grouped(Now.Density,   Density,   sizeof(Density));
    const char* Figures[2] = { Toughness, Density };
    const char* Units  [2] = { "J/m\xc2\xb2", "kg/m\xc2\xb3" };
    const char* Keys   [2] = { "CRACK RESISTANCE", "DENSITY" };
    const float Half = (Inner - ReadoutGap) * 0.5f;
    for (int I = 0; I < 2; ++I)
    {
        const float At = X + float(I) * (Half + ReadoutGap);
        Kit::Inked(Draw, Regular, At, Y + Kit::AscentShare * ReadoutSize, ReadoutSize, ReadoutInk, Figures[I]);
        Kit::Inked(Draw, Light, At + Kit::Measured(Regular, ReadoutSize, Figures[I]) + ReadoutUnitGap,
                   Y + Kit::AscentShare * ReadoutSize, ReadoutUnitSz, ReadoutUnit, Units[I]);
        Kit::Tracked(Draw, Light, At, Y + Kit::Grind(ReadoutSize) + ReadoutKeyLift, ReadoutKeySz, ReadoutKey,
                     Keys[I], ReadoutTrack);
        if (I == 0)
            Draw->AddLine({ X + Half, Y }, { X + Half, Y + Kit::Grind(ReadoutSize) + ReadoutKeyLift
                                                         + Kit::Grind(ReadoutKeySz) }, ReadoutRule);
    }
    Y += Kit::Grind(ReadoutSize) + ReadoutKeyLift + Kit::Grind(ReadoutKeySz) + ProseLift;
    PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk, Now.Description);
}

inline float ImpactCardHeight(ImFont* Light, float Wide)
{
    const float Inner = Wide - CardPadX * 2.0f;
    const float Graph = Inner * (GraphTall / GraphBoxW);
    const float SpotRow = Kit::Grind(SpotSize) + SpotGap
                        + ImMax(Kit::Grind(SpotInput) + SpotInputPadY * 2.0f + 2.0f,
                                Kit::Grind(SpotButton) + SpotButtonPad * 2.0f + 2.0f);
    return CardPadY * 2.0f + CardHeaderHeight() + GraphLift + Graph
         + FieldHeight(FieldLabel) * 2.0f + 20.0f * 2.0f   // two .field blocks, margin 20px 0 collapsed
         + SpotLift + SpotRow + ProseLift
         + ProseHeight(Light, ProseSize, Inner,
                       "Shift-click the surface to place an impact. Closer, larger pieces receive cut priority.");
}

inline void PaintImpactCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                            float Tall, const Subject& Specimen)
{
    float Y = PaintCardHead(Draw, Light, Regular, Spot, Wide, Tall, "Impact", "02");
    const float Inner = Wide - CardPadX * 2.0f;
    const float X = Spot.x + CardPadX;

    Y += GraphLift;
    PaintImpactGraph(Draw, Light, { X, Y }, Inner);
    Y += Inner * (GraphTall / GraphBoxW);

    // Slider("impact-controls", "Energy", "Impact energy", 0, 50000, 10, "J") and the pattern seed.
    char Energy[24]; std::snprintf(Energy, sizeof(Energy), "%g", double(Specimen.Settings.Energy));
    char Seed[24];   std::snprintf(Seed, sizeof(Seed), "%d", Specimen.Settings.Seed);
    Y += 20.0f;
    PaintField(Draw, Light, { X, Y }, Inner, SdfSize, "Impact energy", Energy, "J",
               Specimen.Settings.Energy / 50000.0f, nullptr, Specimen.Settings.Enabled);
    Y += FieldHeight(SdfSize) + 20.0f;
    PaintField(Draw, Light, { X, Y }, Inner, SdfSize, "Pattern seed", Seed, "#",
               float(Specimen.Settings.Seed - 1) / 999998.0f, nullptr, Specimen.Settings.Enabled);
    Y += FieldHeight(SdfSize) + 20.0f;

    // .impact-location — a ruled row of three numeric cells and a Centre button.
    Draw->AddLine({ X, Y }, { X + Inner, Y }, SpotRule);
    Y += SpotLift;
    Kit::Inked(Draw, Light, X, Y + Kit::AscentShare * SpotSize, SpotSize, ProseInk,
               "Impact \xc2\xb7 local m");
    Y += Kit::Grind(SpotSize) + SpotGap;
    const float CellTall = Kit::Grind(SpotInput) + SpotInputPadY * 2.0f + 2.0f;
    const float CentreW = Kit::Measured(Light, SpotButton, "Centre") + SpotButtonPad * 2.0f;
    const float Cells = Inner - CentreW - SpotGap;
    const float CellWide = (Cells - SpotInner * 2.0f) / 3.0f;
    const float Axis[3] = { Specimen.Settings.X, Specimen.Settings.Y, Specimen.Settings.Z };
    const char* Names[3] = { "X", "Y", "Z" };
    for (int I = 0; I < 3; ++I)
    {
        const float At = X + float(I) * (CellWide + SpotInner);
        const float Label = Kit::Measured(Light, SpotSize, Names[I]) + 3.0f;
        Line(Draw, Light, At, Y, CellTall, SpotSize, ProseInk, Names[I]);
        Draw->AddRectFilled({ At + Label, Y }, { At + CellWide, Y + CellTall }, InputFill, ButtonRound);
        Draw->AddRect({ At + Label, Y }, { At + CellWide, Y + CellTall }, InputEdge, ButtonRound);
        char Figure[24]; std::snprintf(Figure, sizeof(Figure), "%g", double(Axis[I]));
        Line(Draw, Light, (At + Label + At + CellWide) * 0.5f, Y, CellTall, SpotInput, PageInk, Figure,
             Kit::Anchor::Middle);
    }
    PaintButton(Draw, Light, { X + Inner - CentreW, Y }, CentreW, CellTall, SpotButton, "Centre");
    Y += CellTall + ProseLift;
    PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk,
               "Shift-click the surface to place an impact. Closer, larger pieces receive cut priority.");
}

inline float QualityCardHeight(ImFont* Light, float Wide)
{
    const float Inner = Wide - CardPadX * 2.0f;
    const float Bleed = Inner + QualityBleed * 2.0f;
    return CardPadY * 2.0f + CardHeaderHeight() + QualityLift + Bleed * (QualityTall / QualityBoxW)
         + FieldHeight(SdfSize) * 2.0f + 20.0f * 3.0f
         + Kit::Grind(CheckSize) + CheckPadY * 2.0f + 2.0f + ProseLift
         + ProseHeight(Light, ProseSize, Inner,
                       "Near-edge cuts and tiny pieces are refused. A limit is a ceiling\xe2\x80\x94not a promise to force bad fragments.");
}

inline void PaintQualityCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                             float Tall, const Subject& Specimen)
{
    float Y = PaintCardHead(Draw, Light, Regular, Spot, Wide, Tall, "Fragment quality", "03");
    const float Inner = Wide - CardPadX * 2.0f;
    const float X = Spot.x + CardPadX;

    // .quality-graph { margin: 12px -6px 0 } — it bleeds 6 px past the card's padding on both sides.
    Y += QualityLift;
    const float Bleed = Inner + QualityBleed * 2.0f;
    PaintQualityGlyph(Draw, { X - QualityBleed, Y }, Bleed, Specimen.Settings.Ceiling, Specimen.Settings.MinimumSize);
    Y += Bleed * (QualityTall / QualityBoxW);

    char Ceiling[16]; std::snprintf(Ceiling, sizeof(Ceiling), "%d", Specimen.Settings.Ceiling);
    char Span[16];    std::snprintf(Span, sizeof(Span), "%.3f", double(Specimen.Settings.MinimumSize));
    Y += 20.0f;
    PaintField(Draw, Light, { X, Y }, Inner, SdfSize, "Fragment ceiling", Ceiling, "pcs",
               float(Specimen.Settings.Ceiling - 2) / 158.0f, nullptr, Specimen.Settings.Enabled);
    Y += FieldHeight(SdfSize) + 20.0f;
    PaintField(Draw, Light, { X, Y }, Inner, SdfSize, "Minimum span", Span, "m",
               (Specimen.Settings.MinimumSize - 0.002f) / 0.298f, nullptr, Specimen.Settings.Enabled);
    Y += FieldHeight(SdfSize) + 20.0f;

    // .quality-checks — three pills that wrap; at 330 px they sit on one row.
    const char* Checks[3] = { "Needle rejection", "Closed caps", "Quality-aware triangulation" };
    const float PillTallCheck = Kit::Grind(CheckSize) + CheckPadY * 2.0f + 2.0f;
    float Pen = X;
    for (int I = 0; I < 3; ++I)
    {
        const float Run = Kit::Measured(Light, CheckSize, Checks[I]) + CheckPadX * 2.0f;
        if (I > 0 && Pen + Run > X + Inner) { Pen = X; Y += PillTallCheck + CheckGap; }
        Draw->AddRectFilled({ Pen, Y }, { Pen + Run, Y + PillTallCheck }, CheckFill, CheckRound);
        Draw->AddRect({ Pen, Y }, { Pen + Run, Y + PillTallCheck }, CheckEdge, CheckRound);
        Line(Draw, Light, Pen + CheckPadX, Y, PillTallCheck, CheckSize, CheckInk, Checks[I]);
        Pen += Run + CheckGap;
    }
    Y += PillTallCheck + ProseLift;
    PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk,
               "Near-edge cuts and tiny pieces are refused. A limit is a ceiling\xe2\x80\x94not a promise to force bad fragments.");
}

inline float StoredCardHeight(ImFont* Light, float Wide, const Subject& Specimen)
{
    const float Inner = Wide - CardPadX * 2.0f;
    float Tall = CardPadY * 2.0f + CardHeaderHeight() + ModeLift
               + ButtonTall + ModePad * 2.0f + 2.0f
               + ProseLift + ProseHeight(Light, ProseSize, Inner, ModeDescription(Specimen));
    if (Specimen.Settings.Run == Mode::Baked)
    {
        Tall += SdfLift + SdfRule + SdfPad + ImMax(Kit::Grind(SdfSize), SdfBox);
        if (Specimen.Settings.PieceSdf)
            Tall += 20.0f + Kit::Grind(SdfSize) + FieldDrop + SelectTall + 20.0f
                  + ProseLeading(SdfNote);
    }
    Tall += ReceiptLift + ReceiptPad * 2.0f + Kit::Grind(ReceiptHead) + ReceiptLift2
          + ReceiptNote * ReceiptLead + ReceiptLift;
    Tall += PreviewButton;
    Tall += ProseLift + ProseHeight(Light, ProseSize, Inner,
            "Stores closed fragment geometry in this browser, per object. Export includes a matching baked pattern. Native assets are not written.");
    return Tall;
}

inline void PaintStoredCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                          float Tall, const Subject& Specimen)
{
    float Y = PaintCardHead(Draw, Light, Regular, Spot, Wide, Tall, "Bake", "04");
    const float Inner = Wide - CardPadX * 2.0f;
    const float X = Spot.x + CardPadX;

    // .mode-options — a two-button segmented control on its own inset track.
    Y += ModeLift;
    const float TrackTall = ButtonTall + ModePad * 2.0f + 2.0f;
    Draw->AddRectFilled({ X, Y }, { X + Inner, Y + TrackTall }, ModeFill, ModeRound);
    Draw->AddRect({ X, Y }, { X + Inner, Y + TrackTall }, ModeEdge, ModeRound);
    const float Each = (Inner - 2.0f - ModePad * 3.0f) * 0.5f;
    for (int I = 0; I < 2; ++I)
    {
        const bool Picked = (I == 0) == (Specimen.Settings.Run == Mode::Dynamic);
        const ImVec2 At { X + 1.0f + ModePad + float(I) * (Each + ModePad), Y + 1.0f + ModePad };
        if (Picked) Draw->AddRectFilled(At, { At.x + Each, At.y + ButtonTall }, ModePicked, ButtonRound);
        Line(Draw, Light, At.x + Each * 0.5f, At.y, ButtonTall, ModeSize, Picked ? PressedInk : ModeInk,
             I == 0 ? "Dynamic" : "Baked", Kit::Anchor::Middle);
    }
    Y += TrackTall + ProseLift;
    Y += PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk, ModeDescription(Specimen));

    // .sdf-authoring — present only in baked mode; its border-top breaks the margin collapse.
    if (Specimen.Settings.Run == Mode::Baked)
    {
        Y += SdfLift;
        Draw->AddLine({ X, Y }, { X + Inner, Y }, CardEdge);
        Y += SdfRule + SdfPad;
        const float RowTall = ImMax(Kit::Grind(SdfSize), SdfBox);
        Line(Draw, Light, X, Y, RowTall, SdfSize, HeadInk, "Bake SDF per piece");
        const ImVec2 Box { X + Inner - SdfBox, Y + (RowTall - SdfBox) * 0.5f };
        Draw->AddRectFilled(Box, { Box.x + SdfBox, Box.y + SdfBox },
                            Specimen.Settings.PieceSdf ? IM_COL32(0x9f, 0xb8, 0xa6, 255) : InputFill, 3.0f);
        Draw->AddRect(Box, { Box.x + SdfBox, Box.y + SdfBox }, InputEdge, 3.0f);
        if (Specimen.Settings.PieceSdf)
        {
            Draw->AddLine({ Box.x + 3.5f, Box.y + 8.5f }, { Box.x + 6.5f, Box.y + 11.5f }, PrimaryInk, 2.0f);
            Draw->AddLine({ Box.x + 6.5f, Box.y + 11.5f }, { Box.x + 12.5f, Box.y + 4.5f }, PrimaryInk, 2.0f);
        }
        Y += RowTall;
        if (Specimen.Settings.PieceSdf)
        {
            Y += 20.0f;
            Kit::Inked(Draw, Light, X, Y + Kit::AscentShare * SdfSize, SdfSize, FieldInk,
                       "Resolution per piece");
            Y += Kit::Grind(SdfSize) + FieldDrop;
            char Choice[32];
            std::snprintf(Choice, sizeof(Choice), "%d\xc2\xb3 \xc2\xb7 R16F", Specimen.Settings.SdfResolution);
            PaintSelect(Draw, Light, { X, Y }, Inner, Choice);
            Y += SelectTall + 20.0f;
            Line(Draw, Light, X, Y, ProseLeading(SdfNote), SdfNote, CheckInk,
                 "Authoring setting only \xc2\xb7 SDF generation pending.");
            Y += ProseLeading(SdfNote);
        }
    }

    // .bake-receipt
    Y += ReceiptLift;
    const float ReceiptTall = ReceiptPad * 2.0f + Kit::Grind(ReceiptHead) + ReceiptLift2
                            + ReceiptNote * ReceiptLead;
    Draw->AddRectFilled({ X, Y }, { X + Inner, Y + ReceiptTall }, ModeFill, ReceiptRound);
    Draw->AddRect({ X, Y }, { X + Inner, Y + ReceiptTall }, ReceiptEdge, ReceiptRound);
    Draw->AddCircleFilled({ X + ReceiptPad + ReceiptDot * 0.5f, Y + ReceiptTall * 0.5f }, ReceiptDot * 0.5f,
                          StoredDot(Specimen), 16);
    const float TextX = X + ReceiptPad + ReceiptDot + ReceiptGap;
    char Detail[128]; StoredDetail(Specimen, Detail, sizeof(Detail));
    Kit::Inked(Draw, Regular, TextX, Y + ReceiptPad + Kit::AscentShare * ReceiptHead, ReceiptHead, PageInk,
               StoredStatus(Specimen));
    Line(Draw, Light, TextX, Y + ReceiptPad + Kit::Grind(ReceiptHead) + ReceiptLift2,
         ReceiptNote * ReceiptLead, ReceiptNote, ReceiptSmall, Detail);
    Y += ReceiptTall + ReceiptLift;

    // .bake-actions — two equal buttons, the first primary.
    const float ActionWide = (Inner - ActionGap) * 0.5f;
    PaintButton(Draw, Light, { X, Y }, ActionWide, PreviewButton, ActionSize, StoredAction(Specimen), true,
                Specimen.Settings.Enabled);
    PaintButton(Draw, Light, { X + ActionWide + ActionGap, Y }, ActionWide, PreviewButton, ActionSize,
                "Clear", false, Specimen.Stored != Freshness::None);
    Y += PreviewButton + ProseLift;
    PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk,
               "Stores closed fragment geometry in this browser, per object. Export includes a matching baked pattern. Native assets are not written.");
}

inline float ReceiptCardHeight(ImFont* Light, float Wide)
{
    const float Inner = Wide - CardPadX * 2.0f;
    return CardPadY * 2.0f + CardHeaderHeight() + ListLift + Kit::Grind(ListSize) * 4.0f + ListGap * 3.0f
         + ProseLift + ProseHeight(Light, ProseSize, Inner,
                "Material-biased, energy-limited geometric partitioning. Not a calibrated physical failure simulation.");
}

inline void PaintReceiptCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                             float Tall, const Subject& Specimen)
{
    float Y = PaintCardHead(Draw, Light, Regular, Spot, Wide, Tall, "Geometry receipt", "05");
    const float Inner = Wide - CardPadX * 2.0f;
    const float X = Spot.x + CardPadX;

    Y += ListLift;
    const char* Terms[4]  = { "Triangles", "Refused candidates", "Volume error", "Geometry generation" };
    const char* Values[4] = { Specimen.Triangles, Specimen.Refused, Specimen.VolumeError, Specimen.Generation };
    for (int I = 0; I < 4; ++I)
    {
        Kit::Inked(Draw, Light, X, Y + Kit::AscentShare * ListSize, ListSize, ReceiptTerm, Terms[I]);
        Kit::Inked(Draw, Light, X + Inner, Y + Kit::AscentShare * ListSize, ListSize, ReceiptValue, Values[I],
                   Kit::Anchor::End);
        Y += Kit::Grind(ListSize) + (I < 3 ? ListGap : 0.0f);
    }
    Y += ProseLift;
    PaintProse(Draw, Light, { X, Y }, ProseSize, Inner, ProseInk,
               "Material-biased, energy-limited geometric partitioning. Not a calibrated physical failure simulation.");
}

inline void PaintInspectorPane(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& L, const Subject& Specimen)
{
    const float X = L.RightX, W = L.RightW, Top = L.WorkTop, Tall = L.WorkTall;
    Draw->AddRectFilled({ X, Top }, { X + W, Top + Tall }, PaneFill);
    PaintPaneHeading(Draw, Light, { X, Top }, W, "Fracture inspector", "PER OBJECT");

    const float CardWide = W - ScrollPad * 2.0f;
    float Y = Top + PaneHead + ScrollPad;
    Draw->PushClipRect({ X, Top + PaneHead }, { X + W, Top + Tall }, true);

    float Height = MaterialCardHeight(Light, CardWide);
    PaintMaterialCard(Draw, Light, Regular, { X + ScrollPad, Y }, CardWide, Height, Specimen);
    Y += Height + CardDrop;

    Height = ImpactCardHeight(Light, CardWide);
    PaintImpactCard(Draw, Light, Regular, { X + ScrollPad, Y }, CardWide, Height, Specimen);
    Y += Height + CardDrop;

    Height = QualityCardHeight(Light, CardWide);
    PaintQualityCard(Draw, Light, Regular, { X + ScrollPad, Y }, CardWide, Height, Specimen);
    Y += Height + CardDrop;

    Height = StoredCardHeight(Light, CardWide, Specimen);
    PaintStoredCard(Draw, Light, Regular, { X + ScrollPad, Y }, CardWide, Height, Specimen);
    Y += Height + CardDrop;

    Height = ReceiptCardHeight(Light, CardWide);
    PaintReceiptCard(Draw, Light, Regular, { X + ScrollPad, Y }, CardWide, Height, Specimen);

    Draw->PopClipRect();
}

// The total height of the five cards and their gaps — what .inspector-scroll scrolls through.
inline float InspectorContentHeight(ImFont* Light, float PaneWide, const Subject& Specimen)
{
    const float CardWide = PaneWide - ScrollPad * 2.0f;
    return ScrollPad * 2.0f + MaterialCardHeight(Light, CardWide) + CardDrop
         + ImpactCardHeight(Light, CardWide) + CardDrop
         + QualityCardHeight(Light, CardWide) + CardDrop
         + StoredCardHeight(Light, CardWide, Specimen) + CardDrop
         + ReceiptCardHeight(Light, CardWide);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE PAGE
//------------------------------------------------------------------------------------------------------------------------

inline void Paint(ImDrawList* Draw, ImFont* Light, ImFont* Regular, const Layout& L, const Subject& Specimen)
{
    Draw->AddRectFilled({ 0, 0 }, { L.Wide, L.Tall }, Gutter);   // the 1 px grid gaps show this through
    PaintTitleBar(Draw, Light, Regular, L);
    PaintWorkspaceBar(Draw, Light, L, Specimen);
    PaintTargetPane(Draw, Light, Regular, L, Specimen);
    PaintViewportPane(Draw, Light, Regular, L, Specimen);
    PaintInspectorPane(Draw, Light, Regular, L, Specimen);
    PaintStatusBar(Draw, Light, L, Specimen);
}

}   // namespace Frontier::FractureEditor
