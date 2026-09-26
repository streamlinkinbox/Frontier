//============================================================================================================================================
//                                                      MATERIALINSPECTOR.H
//============================================================================================================================================
// 🔍 M7b editable material inspector: the loaded scene's material list, one selectable material, its reflectance
//    selection + complexity + slab counts, all 20 Sultan channels (value / source / texture), per-row constant
//    editors, the cutout-threshold + shaderball-preview header controls, and the fold-report lines attributed to it.
//
// Data model: Rebuild() snapshots ONE material from a MaterialIndex every frame (the feeder — GameExecution or a
//    harness — calls it; the layout only reads the snapshot, so the F-panel summary stays fresh without opening the
//    page) and retains the index as the Apply target (never owned). Rows read the RESOLVED slab — Flatten(descriptor,
//    limit).front(), the slab the Tier A kernel samples and DeriveReflectance derives the selection from — never the
//    authoring top slab, so a folded material shows exactly what renders. Fold lines are attributed by the
//    `material '<name>':` prefix Finalise writes.
//
// Editing model (M7b): every material carries an eager per-material DRAFT (a full MaterialSlabDescriptor + the
//    descriptor's AlphaCutoff, snapshotted from Slabs[0] at Rebuild). Sliders write drafts through SetDraftScalar /
//    SetDraftColor / SetDraftCutoff (the proof drives the same calls); Apply commits every dirty draft into its
//    descriptor's Slabs[0]/AlphaCutoff and re-runs Finalise at the index's own slab limit; Discard re-snapshots all
//    drafts from the descriptors. Drafts survive selection switches (switching never touches them) and are keyed by
//    (index pointer, material names) — a scene swap resets them. Sliders bind the draft (authoring top slab), so on
//    a folded material the knob can disagree with the row's resolved value: the knob is what you edit, the row is
//    what renders. Rows 05/13 (orientation: map-or-mesh-frame) and 20 (no carrier) have no editor.
//
// Revisions: Revision = selection (persisted [material] selected); CommitRevision = applied generation (every Apply
//    with ≥1 change; the feeder edge-detects it to restart the accumulation); PreviewRevision = preview-toggle
//    generation (persisted [material] preview). PreviewRequested is a level the feeder consumes with
//    TakePreviewRequest (set by Apply-with-changes when the preview is enabled, and by the Render-preview pill).

#pragma once

#include "ControlKit.h"
#include "PixelSpace.h"
#include "../ContentInterchange/MaterialIndex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      CHANNEL SOURCE
//------------------------------------------------------------------------------------------------------------------------

// Where a channel row's value comes from. Constant = the scalar carrier holds it (a real number even when the selection
//    doesn't sample it — unread channels are retained, Sultan-42 §5); Imported = a texture is bound; Absent = nothing
//    carries the channel (scalar at default AND unbound), including row 20 which has no carrier at all (ACK:M6-none).
enum class MaterialChannelSource : uint8_t { Constant = 0u, Imported = 1u, Absent = 2u, Count = 3u };

// One of the 20 Sultan channels: formatted value, optional second detail line (sub-parameters that share the row's
//    carrier — coat IOR, film thickness, attenuation — never extra rows), source, and texture binding.
struct MaterialChannelRow
{
    const char*         Name = "";                // [txt] "01 base colour" (Sultan names, static table)
    char                Value[80]   = {};         // [txt] formatted primary value ("(0.8, 0.8, 0.8)", "0.3", "—")
    char                Detail[160] = {};         // [txt] sub-parameter line, "" when the row has none
    char                Texture[48] = {};         // [txt] "tex 3 · uv1 · B" or "—"
    MaterialChannelSource Source = MaterialChannelSource::Absent;
    [[nodiscard]] bool HasDetail() const noexcept { return Detail[0] != '\0'; }
};

static constexpr uint32_t kMaterialChannelRowCount = 20u;

// What constant editor a channel row carries (structural — the layout and the proof share this table). Scalar = one
//    slider on the row's primary carrier (row 06 edits the occlusion binding's Scalar); Rgb = three stacked sliders
//    on the row's colour carrier; None = rows 05/13 (map-or-mesh-frame, no scalar primary) and 20 (no carrier).
enum class MaterialEditKind : uint8_t { None = 0u, Scalar = 1u, Rgb = 2u };

[[nodiscard]] MaterialEditKind MaterialRowEditKind(uint32_t Row) noexcept;

// Static name tables (Sultan-42 §5 rows; MaterialReflectance / MaterialComplexityClass / MaterialChannelSource labels).
[[nodiscard]] const char* MaterialChannelRowName(uint32_t Row) noexcept;
[[nodiscard]] const char* MaterialSelectionName(MaterialReflectance Selection) noexcept;
[[nodiscard]] const char* MaterialComplexityName(uint32_t Complexity) noexcept;
[[nodiscard]] const char* MaterialChannelSourceName(MaterialChannelSource Source) noexcept;
[[nodiscard]] const char* MaterialTextureChannelName(TextureChannelSelection Channel) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class MaterialInspector
{
public:
    MaterialInspector() noexcept = default;

    // Seed the selection from the persisted [material] selected name (exact match at Rebuild, else material 0).
    // Never bumps the revision — the following Rebuild resolves it silently.
    void SeedSelection(const char* PersistedName) noexcept;

    // Seed the preview toggle from the persisted [material] preview flag. Never bumps the preview revision.
    void SeedPreview(bool Enabled) noexcept { PreviewEnabled = Enabled; }

    // In-page selector pick. Clamps to the loaded materials, syncs the name, bumps the revision on change.
    // Drafts are untouched (per-material retention across switches).
    [[nodiscard]] bool Select(uint32_t Id) noexcept;

    // Snapshot the selection from Index (nullptr or empty = cleared "no scene" state). Re-resolves the name, flattens
    //    the selected descriptor at the scene's slab limit, rebuilds the 20 rows + fold lines + status/summary lines,
    //    syncs the drafts ((index, names) key — a scene swap re-snapshots them), and retains Index (NOT owned) as the
    //    Apply target. Bumps the revision iff the resolved NAME changed (scene swap, seed resolution, or selector
    //    pick via Select). The feeder calls this every frame; it must outlive the inspector between calls.
    void Rebuild(MaterialIndex* Index) noexcept;

    // Draft writers (sliders + proof share these; values clamp to the row's range, out-of-range ids/rows no-op).
    void SetDraftScalar(uint32_t Material, uint32_t Row, float V) noexcept;
    void SetDraftColor(uint32_t Material, uint32_t Row, uint32_t Component, float V) noexcept;   // Rgb rows, 0..1
    void SetDraftCutoff(uint32_t Material, float V) noexcept;                                    // 0..1

    // Commit every dirty draft into its descriptor (Slabs[0] + AlphaCutoff) and re-run Finalise at the index's own
    //    limit. Bumps CommitRevision and stamps LastCommitCount iff ≥1 draft changed; requests a preview when the
    //    preview is enabled. No index, no materials, or no changes = silent no-op.
    void Apply() noexcept;
    // Re-snapshot every draft from its descriptor (page-level, like every other inspector page's Discard).
    void Discard() noexcept;
    [[nodiscard]] bool     IsDirty()         const noexcept;
    [[nodiscard]] uint32_t QueryDirtyCount() const noexcept;

    // Preview toggle (in-page switch). Bumps PreviewRevision on change (the feeder persists it).
    void SetPreviewEnabled(bool Enabled) noexcept { if (Enabled != PreviewEnabled) { PreviewEnabled = Enabled; ++PreviewRevision; } }
    [[nodiscard]] bool QueryPreviewEnabled() const noexcept { return PreviewEnabled; }

    // Feeder consume: a preview was requested (Apply-with-changes while enabled, or the Render-preview pill).
    [[nodiscard]] bool TakePreviewRequest() noexcept { const bool R = PreviewRequested; PreviewRequested = false; return R; }
    // Feeder stamp after rendering (or failing to): header preview line. MaterialName identifies the render (the
    //    stamp survives selection switches); Detail is the written path on success, the error on failure.
    void NotifyPreviewRendered(bool Ok, const char* MaterialName, const char* Detail, double Seconds, uint32_t Commit) noexcept;

    // Records the page body (already clipped by the caller) offset by ScrollY; returns content height.
    // Refreshes the status/summary lines on the way out, so a drag updates the host footer without a Rebuild.
    float ConstructMaterialsLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;
    // Selector dropdown menu (called after the body clip is popped, like Appearance's floating layer).
    void  ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept;
    void  CloseMenus() noexcept { SelectorOpen = false; }
    [[nodiscard]] bool HasOpenMenu() const noexcept { return SelectorOpen; }

    [[nodiscard]] uint32_t                    QueryRevision()       const noexcept { return Revision; }
    [[nodiscard]] uint32_t                    QueryCommitRevision() const noexcept { return CommitRevision; }
    [[nodiscard]] uint32_t                    QueryLastCommitCount() const noexcept { return LastCommitCount; }
    [[nodiscard]] uint32_t                    QueryPreviewRevision() const noexcept { return PreviewRevision; }
    [[nodiscard]] uint32_t                    QuerySelectedId()     const noexcept { return SelectedId; }
    [[nodiscard]] const char*                 QuerySelectedName()   const noexcept { return SelectedName; }
    [[nodiscard]] uint32_t                    QueryMaterialCount()  const noexcept { return static_cast<uint32_t>(Names.size()); }
    [[nodiscard]] MaterialReflectance         QuerySelection()      const noexcept { return Selection; }
    [[nodiscard]] uint32_t                    QueryComplexity()     const noexcept { return Complexity; }
    [[nodiscard]] uint32_t                    QueryAuthoredSlabs()  const noexcept { return AuthoredSlabs; }
    [[nodiscard]] uint32_t                    QueryResidentSlabs()  const noexcept { return ResidentSlabs; }
    [[nodiscard]] uint32_t                    QuerySlabLimit()      const noexcept { return SlabLimit; }
    [[nodiscard]] const MaterialChannelRow&   QueryRow(uint32_t Row) const noexcept { return Rows[Row < kMaterialChannelRowCount ? Row : 0u]; }
    [[nodiscard]] const MaterialSlabDescriptor& QueryDraftSlab(uint32_t Material) const noexcept;
    [[nodiscard]] float                       QueryDraftCutoff(uint32_t Material) const noexcept;
    [[nodiscard]] float                       QueryDraftScalar(uint32_t Row) const noexcept;                 // selected draft's scalar (layout knob + proof share)
    [[nodiscard]] float                       QueryDraftColor(uint32_t Row, uint32_t Component) const noexcept;
    [[nodiscard]] uint32_t                    QueryFoldLineCount() const noexcept { return static_cast<uint32_t>(FoldLines.size()); }
    [[nodiscard]] const char*                 QueryFoldLine(uint32_t I) const noexcept { return I < FoldLines.size() ? FoldLines[I].c_str() : ""; }
    [[nodiscard]] const char*                 QueryStatusLine()    const noexcept { return StatusLine; }    // host footer pill
    [[nodiscard]] const char*                 QuerySummaryLine()   const noexcept { return SummaryLine; }   // F-panel row ("" = omit)
    [[nodiscard]] const char*                 QueryPreviewStatus() const noexcept { return PreviewStatus; } // header preview row
    [[nodiscard]] const PlaneExtent&          QuerySelectorExtent() const noexcept { return SelectorButton; }
    [[nodiscard]] const PlaneExtent&          QueryRowExtent(uint32_t Row) const noexcept { return RowExtents[Row < kMaterialChannelRowCount ? Row : 0u]; }
    [[nodiscard]] const PlaneExtent&          QueryEditExtent(uint32_t Row, uint32_t Component = 0u) const noexcept;
    [[nodiscard]] const PlaneExtent&          QueryCutoutExtent() const noexcept { return CutoutSlider; }

private:
    struct MaterialDraft
    {
        MaterialSlabDescriptor Slab;
        float                  AlphaCutoff = 0.5f;
    };

    void BuildRows(const MaterialDescriptor& D, const MaterialSlabDescriptor& S) noexcept;
    void BuildStatusAndSummary() noexcept;
    void SyncDrafts(const std::vector<MaterialDescriptor>& Descriptors) noexcept;
    [[nodiscard]] bool DraftDiffers(uint32_t Material) const noexcept;

    uint32_t              Revision        = 0u;
    uint32_t              CommitRevision  = 0u;
    uint32_t              LastCommitCount = 0u;
    uint32_t              PreviewRevision = 0u;
    uint32_t              SelectedId      = 0u;
    char                  SelectedName[128] = {};
    MaterialReflectance   Selection       = MaterialReflectance::Standard;
    uint32_t              Complexity      = MaterialComplexitySimple;
    uint32_t              AuthoredSlabs   = 0u;
    uint32_t              ResidentSlabs   = 0u;
    uint32_t              SlabLimit       = 1u;
    MaterialChannelRow    Rows[kMaterialChannelRowCount];
    std::vector<std::string> Names;              // material names, Rebuild order (menu options)
    std::vector<const char*> NamePointers;       // c_str views into Names, rebuilt with them
    std::vector<std::string> FoldLines;          // fold-report lines attributed to the selection
    std::vector<MaterialDraft> Drafts;           // per-material drafts, Rebuild order (retained across switches)
    std::vector<std::string> DraftNames;         // scene key: names the drafts were snapshotted from
    MaterialIndex*        CachedIndex     = nullptr;   // Apply target (retained, never owned)
    MaterialIndex*        DraftIndex      = nullptr;   // scene key: index the drafts were snapshotted from
    bool                  PreviewEnabled  = true;      // matches [material] preview's default (proof A1)
    bool                  PreviewRequested = false;
    char                  StatusLine[192]  = {};
    char                  SummaryLine[192] = {};
    char                  HeaderSlabs[64]  = {};
    char                  HeaderFlags[128] = {};
    char                  SelectionText[32] = {};
    char                  ComplexityText[32] = {};
    char                  PreviewStatus[192] = {};
    bool                  SelectorOpen  = false;
    int                   DraggingEdit  = -1;   // scalar: row; Rgb: 64 + row*3 + component; cutout: 200
    PlaneExtent           SelectorButton{};
    PlaneExtent           RowExtents[kMaterialChannelRowCount]{};
    PlaneExtent           EditExtents[kMaterialChannelRowCount][3]{};
    PlaneExtent           CutoutSlider{};
};

} // namespace Frontier
