/*====================================================================================================================================
                                                          SOLIDARCOUTLINER.H
====================================================================================================================================*/
// 🧩 Outliner panel, a 1:1 port of the HTML SolidArc Outliner (index.html aside.outliner, WorkspacePanel.css): heading,
//    Bodies / Sketches tiles, scene search, filter chips, the geometry collection rows, and the Construct launcher

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/PixelSpace.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

struct SolidArcWorkspaceState;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 HTML figure classification: 0 curve, 1 surface, 2 body (WorkspacePanel.js ["CURVE", "SURFACE", "BODY"]).
enum class SolidArcFigureCategory : uint8_t
{
    Curve   = 0,                                                                             // A sketch or a space curve
    Surface = 1,                                                                             // An open sheet body (a lofted panel)
    Body    = 2,                                                                             // A closed solid body
    Image   = 3,                                                                             // A reference image plane (outside the history)
};

// Where a figure lives in the document.
enum class SolidArcFigureSource : uint8_t
{
    Body       = 0,                                                                          // Regenerated body (key = creating step label)
    Sketch     = 1,                                                                          // Sketch step no feature consumed
    SpaceCurve = 2,                                                                          // SpaceCurve step (imported, fitted)
    Image      = 3,                                                                          // Document image plane (key = its name)
};

enum class SolidArcOutlinerVerb : uint8_t
{
    None             = 0,                                                                    // Nothing this frame
    Pick             = 1,                                                                    // Row click (Shift / Ctrl: extend)
    ToggleVisibility = 2,                                                                    // Row eye button
    Construct        = 3,                                                                    // Construct launcher / collection +
    ToggleLock       = 4,                                                                    // Row lock button
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcFigure
{
    std::string            Key               = {};                                           // [-] - Body key or step label
    std::string            Name              = {};                                           // [-] - Shown caption
    SolidArcFigureCategory Category          = SolidArcFigureCategory::Body;                 // [-] - Colour, tag, filter
    SolidArcFigureSource   Source            = SolidArcFigureSource::Body;                   // [-] - What a pick selects
    IconSymbol             Icon              = IconSymbol::SolidArcBody;                     // [-] - Row glyph
    bool                   HiddenCondition     = false;                                      // [-] - Out of view (Plasticity H / Outliner eye)
    bool                   LockedCondition     = false;                                      // [-] - In view, not pickable (Outliner lock)
    bool                   LiveCutterCondition = false;                                      // [-] - Wireframe Live Boolean cutter
    bool                   SelectedCondition   = false;                                      // [-] - Picked, or a face / edge of it is
    int                    Rank              = -1;                                           // [-] - Pick order (0 = active), −1 unpicked
};

// 📝 Panel state the HTML keeps in the DOM: search text, filter chip, compact rows, folded collection, rows scroll.
struct SolidArcOutlinerState
{
    std::array<char, 96> Search          = {};                                               // [-]  - #scene-search
    int                  Filter          = -1;                                               // [-]  - −1 All, else a SolidArcFigureCategory
    bool                 CompactEnabled  = false;                                            // [-]  - #compact-button (32 px rows)
    bool                 FoldedEnabled   = false;                                            // [-]  - #fold-button (rows hidden)
    bool                 SearchFocusRequest = false;                                         // [-]  - Ctrl+F focuses the search
    bool                 HoveredCondition   = false;                                         // [-]  - The pointer is over a row (X / Delete deletes it)
    SolidArcFigure       Hovered            = {};                                            // [-]  - That row
    std::string          AnchorKey          = {};                                            // [-]  - Anchor row key for Shift+Click [A..Z] range select
};

// One user action the Outliner asks the workspace to carry out (the workspace owns the commands and the status line).
struct SolidArcOutlinerIntent
{
    SolidArcOutlinerVerb        Verb              = SolidArcOutlinerVerb::None;              // [-] - What was asked
    SolidArcFigure              Figure            = {};                                      // [-] - Row it concerns
    bool                        AdditiveCondition = false;                                   // [-] - Shift / Ctrl held on a pick
    bool                        ShiftCondition    = false;                                   // [-] - Shift held (contiguous [A..Z] range select)
    bool                        CtrlCondition     = false;                                   // [-] - Ctrl held (item toggle)
    std::vector<SolidArcFigure> RangeFigures      = {};                                      // [-] - Shown unlocked figures in [Anchor .. Clicked]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bodies (creation order), then sketches and space curves (tree order), then image planes, with selection, visibility and lock.
[[nodiscard]] std::vector<SolidArcFigure> CollectSolidArcFigures(const SolidArcWorkspaceState& State);

// Draws the panel into the current ImGui window over Panel (logical px) and returns the action of this frame.
SolidArcOutlinerIntent ConstructSolidArcOutliner(SolidArcWorkspaceState& State, const PlaneExtent& Panel);

} // namespace Frontier::SolidArc
