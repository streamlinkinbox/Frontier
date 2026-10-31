/*==========================================================================================================================================
                                                       RECORD EDITOR DESCRIPTION
                                     ------------------------------------------------------------------
              Editor/Development-only inspector contract built by a RecordEntry constructor and reclaimed on destruction.
==========================================================================================================================================*/

// 🧩 Editor/Development-only inspector contract built by a RecordEntry constructor and reclaimed on destruction.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

// 📝 Empty in Shipping builds: shipping binaries carry neither field pointers nor inspector metadata.
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

class RecordEntry;
class SceneDirectory;
class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTRUMENT CATEGORY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Selects either the reusable declarative inspector layout, one of the global Engine observatory dashboards
//    ported from the VehicleHMI reference exhibits (Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind), or a
//    project-supplied custom inspector callback (e.g. Project Everest's ControlVehicle or dual-tier renderer).
enum class InspectorInstrumentCategory : uint8_t
{
    Standard,
    FolderObservatory,
    SunObservatory,
    LensFlareObservatory,
    AtmosphereObservatory,
    CloudObservatory,
    WindObservatory,
    FogObservatory,
    StarsObservatory,
    MoonObservatory,
    WeatherObservatory,
    Custom
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECLARATIVE FIELDS
//------------------------------------------------------------------------------------------------------------------------

struct RecordEditorScalarField
{
    const char* Section          = "PROPERTIES";   // [text] - Card heading grouping adjacent controls
    const char* Label            = "";             // [text] - Control caption
    const char* Unit             = "";             // [text] - Unit suffix shown next to the numeric readout
    double      Minimum          = 0.0;            // [-] - Lower bound in the field's unit
    double      Maximum          = 1.0;            // [-] - Upper bound in the field's unit
    double      Step             = 0.01;           // [-] - Drag / step quantum
    int32_t     FractionalDigits = 2;              // [digits] - Displayed precision
    double*     Target           = nullptr;        // [-] - Live setting inside the owning RecordEntry
};

struct RecordEditorSwitchField
{
    const char* Section = "PROPERTIES";   // [text] - Card heading
    const char* Label   = "";             // [text] - Toggle caption
    bool*       Target  = nullptr;        // [-] - Live setting inside the owning RecordEntry
};

struct RecordEditorOptionField
{
    const char*              Section = "PROPERTIES";   // [text] - Card heading
    const char*              Label   = "";             // [text] - Selector caption
    std::vector<const char*> Options;                  // [text] - Ordered option labels
    int32_t*                 Target  = nullptr;        // [-] - Live 0-based index inside the owning RecordEntry
};

struct RecordEditorColourField
{
    const char* Section     = "COLOUR";   // [text] - Card heading
    const char* Label       = "";         // [text] - Well title
    const char* Subtitle    = "";         // [text] - Secondary caption
    double*     TargetRed   = nullptr;    // [-] - Linear red channel [0..1]
    double*     TargetGreen = nullptr;    // [-] - Linear green channel [0..1]
    double*     TargetBlue  = nullptr;    // [-] - Linear blue channel [0..1]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 RECORD EDITOR DESCRIPTION
//------------------------------------------------------------------------------------------------------------------------

struct RecordEditorDescription
{
    std::string                 Identifier;                                                     // [text] - Cross-link key ("sun", "flare", "wind", ...)
    std::string                 Eyebrow;                                                        // [text] - Inspector eyebrow ("ENVIRONMENT / 02")
    std::string                 DisplayName;                                                    // [text] - Default Outliner + Inspector title
    std::string                 Subtitle;                                                       // [text] - Lead description beneath title
    InspectorInstrumentCategory InstrumentCategory = InspectorInstrumentCategory::Standard;     // [-] - Observatory or custom dispatch
    uint32_t                    AccentColour       = 0;                                         // [RGBA8] - 0 = classification default
    bool*                       EnabledTarget      = nullptr;                                   // [-] - Live row/header power switch
    RecordEntry*                Subject            = nullptr;                                   // [-] - Owning RecordEntry (valid while registered)

    std::function<std::string()>                                      QueryLiveBadge;           // [text] - Compact Outliner row telemetry readout
    std::function<void(SceneDirectory&, SceneDirectoryPanel&)>        ConstructCustomInspector; // [-] - Custom ImGui body for InstrumentCategory::Custom

    std::vector<RecordEditorScalarField> ScalarFields;
    std::vector<RecordEditorSwitchField> Switches;
    std::vector<RecordEditorOptionField> OptionFields;
    std::vector<RecordEditorColourField> ColourFields;
};

} // namespace Frontier

#endif // Editor/Development profiles
