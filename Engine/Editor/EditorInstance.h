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
// 256 rows: the 101-object showcase (spans as named nodes) plus folders, cameras and the 15 celestial
//    rows fit with headroom; 64 starved the celestial block on any level past a dozen placements.
constexpr uint32_t kMaxEditorInstances = 256u;
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

// The outliner's row glyphs — the celestial page's own SVG set, one key per row. Auto lets the panel pick
//    by category (folder / plane / bulb / camera); everything else names a glyph outright.
enum class EditorGlyph : uint32_t
{
    Auto = 0u,
    Globe, Cloud, VolumeClouds, LocalCloud, Rainbow, Wind, Rain, AerialFog, VolumeFog, Folder, Fog,
    Atmosphere, Sun, Sky, Check, Dot, Warn, Shadow, Bounce, Collide, Stars, Moon, Camera, Effects, Eye,
    EyeOff, Chevron, ChevronUp, Wave, Horizon, Orbit, Bulb, Palette, Ground, Lattice, Galaxy, Aperture,
    Sliders, Flare, Up, Down, Flat, Plus, Plane, Key, Minus, Trash, Search, Compact,
    Count
};

// The row's standing dot: the panel derives Auto (hidden → Err "Hidden", else Ok "Seated"; folders total
//    their rows), the feed overrides with what the tick knows (below horizon, washed out, set, lifted).
enum class EditorStanding : uint32_t
{
    Auto = 0u,
    Ok,
    Quiet,
    Warn,
    Err
};

// The five narrowing pills the outliner offers. Auto maps from the category: Light → Lights, Camera →
//    Camera, Geometry → Geometry; the feed names Sky and Bodies for the rows the category cannot tell apart.
enum class EditorNarrowing : uint32_t
{
    Auto = 0u,
    Lights,
    Sky,
    Bodies,
    Geometry,
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

    // The outliner's extra columns — all optional; a feed that leaves them alone gets the category defaults.
    EditorGlyph      Glyph     = EditorGlyph::Auto;         // the 14 px row icon
    EditorNarrowing  Narrowing = EditorNarrowing::Auto;     // which pill narrows to this row
    EditorStanding   Standing  = EditorStanding::Auto;      // the 16 px standing dot
    char             StandingNote[20] = {};                 // its hover title ("Below horizon")
    char             Meta[24]  = {};                        // the right-hand live figure ("12.4°", "AM 1.02")
    char             Tag[8]    = {};                        // the small pill after the name ("Comp")
    bool             Pinned    = false;                     // true: no drag, no eye — the page's World / Lights
    bool             Shut      = false;                     // the feed's opening pose (false reads open)
};

// The foot strips: one height across the outliner, the inspector and the viewport, so the three hems
//    draw one unbroken line. Every foot reserves exactly this and draws exactly this.
constexpr float kEditorFooterH = 40.0f;

// The realtime band: Good holds 50 and up, Fair the middle, Poor below 24. The strips tint the figure
//    by the band and hang the warning triangle off Poor.
enum class EditorFpsBand : uint32_t
{
    Good = 0u,
    Fair,
    Poor
};

inline EditorFpsBand EditorFpsBandFor(float Fps) noexcept
{
    if (Fps < 24.0f)
    {
        return EditorFpsBand::Poor;
    }
    if (Fps < 50.0f)
    {
        return EditorFpsBand::Fair;
    }
    return EditorFpsBand::Good;
}

// The outliner's footer strip: five figures the tick refreshes — the page's Realtime / Quality / Sun / Moons / Cam.
struct EditorReadout
{
    float    Fps            = 60.0f;
    char     Quality[16]    = "Standard";
    char     Pixels[16]     = {};                           // "1280×720"
    float    SunElevation   = 0.0f;                         // [deg]
    uint32_t MoonCount      = 1u;
    uint32_t MoonCap        = 4u;
    uint32_t Triangles      = 0u;                            // live triangle total; 0 reads unknown — the strips print their dash
    float    Cam[3]         = { 0.0f, 2.0f, 0.0f };         // x, height, z — printed "0, 2.0, 0"
    char     Scene[24]      = "Scene";                       // level name — the outliner head prints it
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
