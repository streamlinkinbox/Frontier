//============================================================================================================================================
//                                                     EDITORINSTANCE.H
//============================================================================================================================================
// 🧩 Development editor feed — the instance roster and property sheet protocol. The project fills these from the
//    live scene; the panels borrow them each tick and edit in place, so a rename, a toggle or a slider move is
//    visible to the project on the same tick without any bus or queue between them.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECORD COMPASS
//------------------------------------------------------------------------------------------------------------------------

// The most instances one tick may carry, the most rows one selection may hold, and the index that means "none".
//    Fixed: the tick never allocates.
constexpr uint32_t kMaxEditorInstances = 64u;
constexpr uint32_t kMaxEditorPicked  = 16u;
constexpr uint32_t kNoEditorInstance   = 0xFFFFFFFFu;

// Instance categories. The tint lives per instance (category colour, folders overridable); this names the behaviour.
enum class EditorInstanceCategory : uint32_t
{
    Folder = 0u,
    Geometry,
    Light,
    Camera,
    Count
};

// One row of the roster. The feed walks in preorder: a folder's rows follow it, deepened by Depth, so the
//    panel renders the hierarchy without any links of its own.
struct EditorInstance
{
    char             Label[44] = {};                        // display name; the panel renames in place
    char             Notes[256] = {};                       // per-instance scratch; the inspector's notes card
    uint32_t         Depth     = 0u;                        // Depth in the preorder walk
    uint32_t         KidCount  = 0u;                        // direct rows below a folder (the count badge)
    EditorInstanceCategory Category      = EditorInstanceCategory::Folder;
    float            Tint[3]   = { 1.0f, 1.0f, 1.0f };      // row glyph tint
    bool             Visible   = true;
    bool             Locked    = false;
    bool             Solo      = false;
    bool             Dynamic   = false;                     // the DYN badge
    bool             Physics   = false;                     // the PHYS badge
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROPERTY SHEET
//------------------------------------------------------------------------------------------------------------------------

// Property categories. One control per category, drawn by ControlPanel: the slider is always paired with its type-in pill,
//    the vector is always three axis fields, and the readout is always right-aligned tabular text.
enum class EditorPropertyCategory : uint32_t
{
    Slider = 0u,
    Switch,
    AxisVec3,
    Colour,
    Select,
    Readout,
    Count
};

constexpr uint32_t kMaxEditorOptions      = 6u;    // options one Select may offer
constexpr uint32_t kMaxEditorOptionChars  = 20u;   // chars per option, terminator included
constexpr uint32_t kMaxEditorGroupProps   = 10u;   // properties per card
constexpr uint32_t kMaxEditorSheetGroups  = 6u;    // cards per sheet

struct EditorProperty
{
    char               Label[28] = {};
    EditorPropertyCategory Category      = EditorPropertyCategory::Readout;

    // Slider: the figure, its range, and how the pill prints it.
    float    Minimum  = 0.0f;
    float    Maximum  = 1.0f;
    float    Figure   = 0.0f;
    uint32_t Decimals = 2u;
    char     Unit[8]  = {};
    bool     Hi       = false;   // the periwinkle fill, for the one slider that matters most

    // Switch + AxisVec3 + Colour.
    bool  On       = false;
    float Axes[3]  = {};
    float AxisStep = 0.05f;
    bool  Editable = true;
    float ColourTint[3] = { 1.0f, 1.0f, 1.0f };
    bool  Swatches      = false;   // the eight tint dots instead of the chip

    // Select + Readout.
    char     Options[kMaxEditorOptions][kMaxEditorOptionChars] = {};
    uint32_t OptionCount = 0u;
    uint32_t Picked      = 0u;
    char     Text[48]    = {};
};

struct EditorPropertyGroup
{
    char            Title[24] = {};
    EditorProperty  Properties[kMaxEditorGroupProps] = {};
    uint32_t        PropertyCount = 0u;
};

struct EditorSheet
{
    EditorPropertyGroup Groups[kMaxEditorSheetGroups] = {};
    uint32_t            GroupCount = 0u;
};

inline const char* EditorInstanceLabel(EditorInstanceCategory Category) noexcept
{
    switch (Category)
    {
    case EditorInstanceCategory::Folder:   return "Folder";
    case EditorInstanceCategory::Geometry: return "Geometry";
    case EditorInstanceCategory::Light:    return "Light";
    case EditorInstanceCategory::Camera:   return "Camera";
    default:                         return "?";
    }
}

} // namespace Frontier
