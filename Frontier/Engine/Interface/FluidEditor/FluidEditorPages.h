/*==========================================================================================================================================
                                                         FLUIDEDITORPAGES.H
==========================================================================================================================================*/
// 🧩 The Fluid editor's Inspector pages on a FluidVolume record: the prototype's property panel (FluidPanel.js ConstructInspector) with the same groups,
//    badges, hints and defaults of open and closed, over the real Inspector controls (ControlColumn, SectionRows, SelectRow, TabStrip, DisclosureRow).
//    The controls themselves are not written here: each one is a row of the catalogue transcribed from the prototype (FluidEditorControls.h: its label,
//    range, step, unit and options), laid on the control its form asks for.
//
// 📝 The Outliner lists four parts of the one record: the domain, the emitter, the collider and the sun. A part has a page of three tabs (Source,
//    Simulation, Rendering); which groups a tab holds depends on the part, as in the prototype.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidEditor/FluidEditorControls.h"
#include "Engine/Interface/FluidEditor/FluidViewportPresentation.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Frontier
{

class ControlColumn;

// 📝 The four parts the Outliner lists, one Inspector page each.
enum class FluidPart : uint8_t
{
    Domain,
    Emitter,
    Collider,
    Sun
};

inline constexpr size_t FluidPartCount = 4;

// 📝 The three tabs of a part's page.
enum class FluidTab : uint8_t
{
    Source,
    Simulation,
    Rendering
};

inline constexpr size_t FluidTabCount = 3;

// 📝 What the window remembers about the volume and the record does not save (the prototype's own fields): the collider and the light as they were
//    before their eye switched them off, the names of the parts, and the groups a person opened or closed.
struct FluidEditorMemory
{
    int                                       SavedCollider = 1;                 // [-] the collider type the eye brings back
    float                                     SavedSun      = 2.2f;              // [-] the light's intensity the eye brings back
    std::string                               EmitterName   = "Fire emitter";
    std::string                               ColliderName  = "Sphere collider";
    std::vector<std::pair<std::string, bool>> Groups;                            // [-] a group's title and whether the person left it open
};

// 📝 What a page reads and edits. Requests carries the verbs a page cannot carry out itself ("restart", "camera:fit", "select:<part>", "burst:<kind>",
//    "library:on") and a "noted:<kind>[:<argument>]" line for the window's Log Stream.
struct FluidEditorContext
{
    FluidVolumeConfiguration& Volume;
    FluidEditorMemory&        Memory;
    const FluidVolumeReadout* Readout = nullptr;
    std::vector<std::string>& Requests;
};

namespace FluidEditorPages
{

// 📝 The edits a page, the Outliner and the library share. Each changes the record and leaves a "noted:" line in Requests.
void ApplyPreset(FluidEditorContext& Context, std::string_view Identity);
void RestoreTab(FluidEditorContext& Context, FluidPart Part, FluidTab Tab);
void ToggleEnabled(FluidEditorContext& Context);
void TogglePart(FluidEditorContext& Context, FluidPart Part);
bool AddPart(FluidEditorContext& Context, std::string_view Kind);
void RemovePart(FluidEditorContext& Context, FluidPart Part);

// 📝 One part's page: its heading, the tabs and the groups of the chosen tab. Tab is the window's: pressing a tab changes it.
void ConstructWindowPage(ControlColumn& Page, FluidEditorContext& Context, FluidPart Part, FluidTab& Tab);

// 📝 The settings a tab shows for a part, in the order the page lays them (what "reset this tab" restores).
[[nodiscard]] std::vector<std::string_view> ResolveVisibleKeys(FluidPart Part, FluidTab Tab);

// 📝 The Outliner's token of a part ("Domain", "Emitter", "Collider", "Sun"), a tab's ("source", "simulation", "rendering"), and back.
[[nodiscard]] const char* ResolveToken(FluidPart Part) noexcept;
[[nodiscard]] bool        ResolvePart(std::string_view Token, FluidPart& OutPart) noexcept;
[[nodiscard]] const char* ResolveTabToken(FluidTab Tab) noexcept;
[[nodiscard]] bool        ResolveTab(std::string_view Token, FluidTab& OutTab) noexcept;
// 📝 The tab a part opens on: the domain on its Simulation, the sun on its Rendering, the rest on their Source.
[[nodiscard]] FluidTab    ResolveOpeningTab(FluidPart Part) noexcept;

// 📝 What the Outliner shows of a part: whether it is on (the domain always is), whether it is listed (a collider only while there is one, or while
//    it is the selected part), its name, and the line of figures under the name.
[[nodiscard]] bool        QueryPartEnabled(const FluidVolumeConfiguration& Volume, FluidPart Part) noexcept;
[[nodiscard]] bool        QueryPartListed(const FluidVolumeConfiguration& Volume, FluidPart Part, FluidPart Selected) noexcept;
[[nodiscard]] std::string ResolvePartName(const FluidEditorMemory& Memory, FluidPart Part);
[[nodiscard]] std::string FormatPartFigures(const FluidVolumeConfiguration& Volume, FluidPart Part);
// 📝 The page's eyebrow: what kind of part it is ("EULERIAN VOLUME", "CONTINUOUS SOURCE", "SIGNED DISTANCE COLLIDER", "DIRECTIONAL LIGHT").
[[nodiscard]] const char* ResolvePartKind(FluidPart Part) noexcept;
// 📝 The glyph of a part in the Outliner: a box, a flame (a fuelled emitter) or a puff of smoke (an unfuelled one), a sphere, a sun.
[[nodiscard]] const char* ResolvePartGlyph(const FluidVolumeConfiguration& Volume, FluidPart Part) noexcept;

// 📝 "48³": a lattice's edge as the prototype's HUD writes it.
[[nodiscard]] std::string FormatLattice(int32_t Resolution);
// 📝 "110,592 voxels": a lattice's cells, with the thousands separated.
[[nodiscard]] std::string FormatVoxels(int32_t Resolution);
// 📝 The label the prototype gives a collider of this type ("Sphere", "Vertical cylinder", "Horizontal cylinder", "Deflector slab", "Tyre ring").
[[nodiscard]] const char* ResolveColliderLabel(int32_t ObstacleType) noexcept;
// 📝 "00:03.250": the simulated time as the prototype's timecode writes it.
[[nodiscard]] std::string FormatTimecode(double Seconds);
// 📝 What the prototype's burst select calls a pattern: burst "Single burst", shrapnel "Shrapnel", salvo "Cluster salvo", collide "Colliding waves", mushroom
//    "Mushroom cloud".
[[nodiscard]] const char* ResolveBurstLabel(std::string_view Pattern) noexcept;

} // namespace FluidEditorPages

} // namespace Frontier

#endif
