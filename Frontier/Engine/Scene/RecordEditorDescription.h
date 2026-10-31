/*====================================================================================================================================
                                                    RECORDEDITORDESCRIPTION.H
====================================================================================================================================*/
// 🧩 Editor-only description a record builds for itself: outliner grouping, inspector controls, and live infographics.

#pragma once

// 📝 This header is empty outside the editor profile. A record's constructor builds one of these
//    descriptions under the same condition and hands it to the SceneDirectory; shipping binaries
//    contain neither the description nor any field pointers into record settings.
#if defined(FRONTIER_EDITOR_PROFILE)

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One numeric inspector field, bound directly to a live setting in the owning record. Keeping the
//    first seven members in exact order preserves aggregate brace-initialisation in EnvironmentRecords.
struct RecordEditorScalarField
{
    const char *Label       = "";       // [text] - Inspector row label
    const char *Unit        = "";       // [text] - Unit suffix shown in the number field
    double      Minimum     = 0.0;      // [-] - Slider lower bound, in the field's unit
    double      Maximum     = 0.0;      // [-] - Slider upper bound, in the field's unit
    double      Step        = 0.0;      // [-] - Slider / input step, in the field's unit
    int32_t     Digits      = 2;        // [digits] - Fractional digits in the number field
    double     *Target      = nullptr;  // [-] - Live double setting in the owning record; never owned here
    const char *Section     = "";       // [text] - Optional section card title
    float      *FloatTarget = nullptr;  // [-] - Optional live float setting in the owning record
    const char *MinCaption  = "";       // [text] - Optional left range caption (EditorControls.js Labels[0])
    const char *MaxCaption  = "";       // [text] - Optional right range caption (EditorControls.js Labels[1])
};

// 📝 One inspector property switch, bound directly to a live setting in the owning record.
struct RecordEditorSwitch
{
    const char *Label   = "";       // [text] - Switch caption
    bool       *Target  = nullptr;  // [-] - Live setting in the owning record; never owned here
    const char *Section = "";       // [text] - Optional section card title
};

// 📝 Segmented pill / radio option inside a RecordEditorChoiceField.
struct RecordEditorChoiceOption
{
    const char *Label = "";         // [text] - Pill label
    int32_t     Value = 0;          // [-] - Discrete integer value stored in Target
};

// 📝 Discrete mode / tier / preset choice control bound directly to an int32_t in the owning record.
struct RecordEditorChoiceField
{
    const char                           *Label     = "";      // [text] - Choice group caption
    std::vector<RecordEditorChoiceOption> Options;             // [-] - Ordered pill options
    int32_t                              *Target    = nullptr; // [-] - Live int32_t setting in the owning record
    bool                                  Segmented = true;    // [-] - Render as segmented pill bar
    const char                           *Section   = "";      // [text] - Optional section card title
};

// 📝 One clickable action button in the Inspector (e.g. Cleave Target Monolith, Ignite Pyro, Preset).
struct RecordEditorAction
{
    const char           *Label   = "";  // [text] - Button caption
    int32_t               Tone    = 0;   // [-] - 0 = neutral, 1 = accent primary, 2 = warning/destructive
    const char           *Section = "";  // [text] - Optional grouping tag
    std::function<void()> Invoke;        // [-] - Invoked on click on the UI thread
};

// 📝 Everything the Outliner and Inspector need for one record, built by that record's own
//    editor-conditional constructor code (#if defined(FRONTIER_EDITOR_PROFILE)).
struct RecordEditorDescription
{
    const char *StableId    = "";      // [text] - Pinned HTML entity id, e.g. "sun", "vehicle"
    const char *Group       = "";      // [text] - Pinned HTML outliner group, e.g. "Environment"
    const char *DisplayName = "";      // [text] - Row and inspector header name
    const char *Category    = "";      // [text] - Inspector eyebrow type, e.g. "Directional light"
    const char *Glyph       = "";      // [text] - Pinned HTML glyph name
    const char *Colour      = "";      // [text] - Row accent colour, "#rrggbb"
    const char *Description = "";      // [text] - Inspector header description
    bool       *Enabled     = nullptr; // [-] - Live enabled setting; nullptr means always enabled
    uint64_t   *Revision    = nullptr; // [-] - Optional dirty counter incremented on live Inspector edits

    std::vector<RecordEditorScalarField> ScalarFields;   // [-] - Inspector sliders, in display order
    std::vector<RecordEditorSwitch>      Switches;       // [-] - Inspector property switches, in display order
    std::vector<RecordEditorChoiceField> ChoiceFields;   // [-] - Segmented discrete choice pills
    std::vector<RecordEditorAction>      Actions;        // [-] - Preset / trigger buttons

    std::function<std::string()> QueryLiveMeta;          // [-] - Right-aligned live Outliner row readout
    std::function<void()>        ConstructInfographic;   // [-] - Custom VehicleHMI / General UI infographic
    std::function<void(const std::function<void()>&)> ConstructInspector; // [-] - Record-owned page with host navigation
    const char                  *DedicatedEditorTitle = ""; // [text] - Dedicated popup editor window title
    std::function<void()>        OpenDedicatedEditor;    // [-] - Launches dedicated zero-copy popup editor window

    // VehicleHMI Outliner row behavior is distinct from Inspector Enable and scene visibility: only entities
    // that authored an Outliner toggle show the eye control. A state callback returns "live", "warm", or "".
    bool                       *OutlinerToggle = nullptr;
    std::function<std::string()> QueryLiveState;
    bool                        HiddenInOutliner = false;
};

} // namespace Frontier

#endif // FRONTIER_EDITOR_PROFILE
