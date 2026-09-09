//============================================================================================================================================
//                                                      EDITORRECORD.H
//============================================================================================================================================
// 🧩 Development editor feed — the record register and property sheet protocol. The project fills these from the
//    live scene; the panels borrow them each tick and edit in place, so a rename, a toggle or a slider move is
//    visible to the project on the same tick without any bus or queue between them.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECORD COMPASS
//------------------------------------------------------------------------------------------------------------------------

// The most records one tick may carry, the most rows one selection may hold, and the index that means "none".
//    Fixed: the tick never allocates.
constexpr uint32_t kMaxEditorRecords = 64u;
constexpr uint32_t kMaxEditorPicked  = 16u;
constexpr uint32_t kNoEditorRecord   = 0xFFFFFFFFu;

// Record kinds. The tint lives per record (kind colour, folders overridable); this names the behaviour.
enum class EditorRecordKind : uint32_t
{
    Folder = 0u,
    Geometry,
    Light,
    Camera,
    Sky,
    Sun,
    Moon,
    Count
};

// One row of the register. The feed walks in preorder: a folder's rows follow it, nested by Depth, so the
//    panel renders the hierarchy without any links of its own.
struct EditorRecord
{
    char             Label[44] = {};                        // display name; the panel renames in place
    char             Notes[256] = {};                       // per-record scratch; the inspector's notes card
    uint32_t         Depth     = 0u;                        // nesting depth in the preorder walk
    uint32_t         KidCount  = 0u;                        // direct rows below a folder (the count badge)
    EditorRecordKind Kind      = EditorRecordKind::Folder;
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

// Property kinds. One control per kind, drawn by EditorKit: the slider is always paired with its type-in pill,
//    the vector is always three axis fields, and the readout is always right-aligned tabular text.
enum class EditorPropertyKind : uint32_t
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
    EditorPropertyKind Kind      = EditorPropertyKind::Readout;

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

inline const char* EditorKindLabel(EditorRecordKind Kind) noexcept
{
    switch (Kind)
    {
    case EditorRecordKind::Folder:   return "Folder";
    case EditorRecordKind::Geometry: return "Geometry";
    case EditorRecordKind::Light:    return "Light";
    case EditorRecordKind::Camera:   return "Camera";
    case EditorRecordKind::Sky:      return "Sky";
    case EditorRecordKind::Sun:      return "Sun";
    case EditorRecordKind::Moon:     return "Moon";
    default:                         return "?";
    }
}

} // namespace Frontier
