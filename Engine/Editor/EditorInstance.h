//============================================================================================================================================
//                                                     EDITORINSTANCE.H
//============================================================================================================================================
// 🧩 Development editor feed — the instance roster and property sheet protocol. The project fills these from the
//    live scene; the panels borrow them each tick and edit in place, so a rename, a toggle or a slider move is
//    visible to the project on the same tick without any bus or queue between them.

#pragma once

#include <cstdint>
#include "../DisplayPresentation/IconArt.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECORD COMPASS
//------------------------------------------------------------------------------------------------------------------------

// The most instances one tick may carry, the most rows one selection may hold, and the index that means "none".
//    Fixed: the tick never allocates.
// 1024 rows: the showcase (spans as named nodes) plus folders, cameras, dynamic objects, scenery plinths, and celestial
//    rows fit with ample headroom.
constexpr uint32_t kMaxEditorInstances = 1024u;
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

// The Project-Zero/game default filter slots. Tools can override the visible filter catalogue and can set
//    EditorInstance::FilterMask for their own domain-specific rows; Auto still maps from category for game rows.
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
    uint64_t InspectorKey=0; // Stable project identity; never inferred from row order or display name.
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
    IconSymbol       Artwork = IconSymbol::Count;           // Count keeps semantic glyph/category mapping
    EditorGlyph      Glyph     = EditorGlyph::Auto;         // the 14 px row icon
    EditorNarrowing  Narrowing = EditorNarrowing::Auto;     // default/game filter slot; Auto picks from Category
    uint32_t         FilterMask = 0u;                        // optional per-tool filter bits; 0 derives from Narrowing/Category
    EditorStanding   Standing  = EditorStanding::Auto;      // the 16 px standing dot
    char             StandingNote[20] = {};                 // its hover title ("Below horizon")
    char             Meta[24]  = {};                        // the right-hand live figure ("12.4°", "AM 1.02")
    char             Tag[8]    = {};                        // the small pill after the name ("Comp")
    bool             Pinned    = false;                     // true: no drag, no eye — the page's World / Lights
    bool             Component = false;                    // owned leaf: cannot be reparented independently
    bool             Shut      = false;                     // row-owned collapse pose (false reads open)
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
    uint32_t OptionValues[kMaxEditorOptions] = {}; // stable reference IDs when a selector needs them
    uint32_t OptionCount = 0u;
    uint32_t Picked      = 0u;
    char     Text[48]    = {};
};

struct EditorPropertyGroup
{
    char            Title[24] = {};
    char            Caption[128] = {};
    bool            StackedLabels = false;
    bool            Clock24 = false; // first property supplies local hours; display only
    EditorProperty  Properties[kMaxEditorGroupProps] = {};
    uint32_t        PropertyCount = 0u;
};

enum class EditorSheetAppearance : uint8_t { Generic, Sun, LensFlare, AtmosphereSky, Moon, Stars, GlobalCloud, LocalCloud, HeightFog, AerialFog, LocalFog, Wind, Precipitation, Rainbow, Camera };

// Borrowed immutable image data; project retains ownership through the editor frame.
struct EditorSkyImage {
    const uint16_t* Pixels=nullptr;
    uint32_t Width=0, Height=0;
    uint64_t Revision=0;
    float BakedSunDirection[3]={1,0,0};
    bool Stale=false, Resident=false, Pending=false, RequestBake=false;
};
struct EditorMoonBody {
 const uint8_t* Pixels=nullptr;uint32_t Width=0,Height=0;float Tint[3]={1,1,1};float Tilt=0,Haze=0,Gamma=1;
};
struct StarRecord;
struct EditorStarPreview { const StarRecord* Records=nullptr; uint32_t Count=0; float Seconds=0; };
struct EditorFogPreview {float Rayleigh[3]={5.8e-6f,13.5e-6f,33.1e-6f};float Mie=21e-6f,RayleighHeight=8000,MieHeight=1200;};
struct EditorWeatherPreview { float Wind[8]={}; uint32_t Alive=0; float SnowDepth=0, RainVisibility=0; bool AboveWeather=false; };
struct EditorSheet
{
    uint64_t InspectorKey=0;
    EditorWeatherPreview WeatherPreview{};
    bool CameraLive=false;float CameraAspect=1.5f;
    EditorSkyImage SkyImage{};
    EditorStarPreview StarPreview{};
    EditorFogPreview FogPreview{};
    EditorMoonBody MoonBodies[6]{};uint64_t MoonAtlasRevision=0;
    float MoonAzimuth=0, MoonElevation=0, MoonPhase=0; // solved read-only lunar inputs
    EditorSheetAppearance Appearance = EditorSheetAppearance::Generic;
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
