/*============================================================================================================================================
                                                  TEXTUREPAINTWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native port of Experimental/TextureStudio/src/TexturePaintStudio.jsx's Texture Paint workspace: the same
//    three-dock composition (layer stack LEFT, Teapot viewport CENTER, inspector/history RIGHT), the same header,
//    authoring bar, mask toolbar, content-browser launcher and status bar, built from the shared native surfaces
//    every other TextureStudio workspace already uses (PanelDockspace, VectorAuthoringControlSurface, GlyphSpace
//    icons and the Material workspace's orbit camera in MaterialPreviewConfiguration.h).
//
// 📝 Scope, stated plainly: the layer document below is real local state (add/duplicate/delete/reorder/folder/
//    mask/channel/opacity/blend edits, snapshot undo/redo history, JSON project save), and the viewport is a real
//    CPU draw-list rasterizer over the same built-in Teapot geometry the Baking workspace bakes (BakeMesh.h) -
//    orbit/zoom/pick, wireframe, ground grid, part targeting, an authoring-UDIM UV view derived from the mesh's
//    actual chart ids, and preview-only digital dabs/fills evaluated per-triangle for the on-screen composite.
//    It is NOT Alloy's WebGL surface compositor: no GPU paint slices, no baked PBR map export and no
//    material-snapshot shading are claimed here. Stamp placement is real (each stamp lands on its own
//    Decal layer and stays draggable via the Transform tool); controls whose engine is not native yet
//    (project file picking) say so in the status line.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include "Engine/Render/WorkspaceViewportRendering.h"
#include "UI/TextureStudio/PaintStampEngine.h"
#include "UI/TextureStudio/StampLibrary.h"
#include "UI/TextureStudio/TypefaceCatalogue.h"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Frontier
{
class TexturePaintWorkspacePanel final : public ActivationSequence
{
  public:
    TexturePaintWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; Controls.Scale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as Pattern/Stamp/Baking's ConfigureTypeface: shared "DM Sans" regular/light handles.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Baking/Stamp/Pattern's ActivateMaterial/... hooks.
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    std::function<void()> ActivateBaking;

    // 📝 Material catalogue snapshot retained for layer source-asset links (name/category/colour only -
    // assigning one stores an asset link on the selected layer, Alloy's own "Asset link stored on the
    // layer" behaviour; it does not shade the preview with that material's full recipe). The content
    // browser UI that consumed this snapshot was deleted; the data plumbing stays for its return.
    struct MaterialAsset
    {
        std::string Name;
        std::string Category;
        std::string ColourHex;
    };
    void ConfigureMaterialAssets(std::vector<MaterialAsset> Assets) { MaterialAssets = std::move(Assets); }
    [[nodiscard]] size_t QueryMaterialAssetCount() const noexcept { return MaterialAssets.size(); }

    // 📝 Save project / autosaved draft land here (the material library directory the app already owns).
    void ConfigureProjectDirectory(std::filesystem::path Directory) { ProjectDirectory = std::move(Directory); }

    // 🖨️ Shared stamp library folder (<library>/Stamps): the Stamp workspace saves decals here, the
    //    stamp tool's right-click picker reads them (plus built-in presets) from the same place.
    void ConfigureStampLibraryDirectory(std::filesystem::path Directory)
    {
        StampLibraryDirectory = std::move(Directory);
        EnsureStampLibrary(true);
    }

    // 🅰️ Bundled engine-content root: the Text decal tool reads its TrueType glyph outlines from
    // <root>/EngineContent/FontArchives (same resolution the Stamp workspace uses); empty = try
    // the working directory.
    void ConfigureContentRoot(std::filesystem::path Root) { ContentRoot = std::move(Root); }

    // 🎨 UV-space preview canvas: the layer stack composited per texel (one 512² tile per UDIM part,
    // 2x2 in a MaterialThumbnailAtlas page) so the 3D/2D views texture-map the mesh per pixel like
    // the HTML paint workshop, instead of flat-shading one colour per face. Host plumbing mirrors
    // the Stamp/Pattern layer atlases exactly: the host supplies the sampled image through
    // QueryPaintCanvasImage and uploads QueryPaintCanvasPixels whenever the revision moves. With no
    // host image the panel keeps its per-triangle centroid preview (headless validation hosts).
    static constexpr uint32_t PaintCanvasTile = 512;
    static constexpr uint32_t PaintCanvasColumns = 2;
    static constexpr uint32_t PaintCanvasExtent = (PaintCanvasTile + 2) * PaintCanvasColumns;
    struct PaintCanvasImage
    {
        uint64_t Texture = 0;
        float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
    };
    std::function<PaintCanvasImage()> QueryPaintCanvasImage;
    [[nodiscard]] uint64_t QueryPaintCanvasRevision();
    [[nodiscard]] const std::vector<uint8_t> &QueryPaintCanvasPixels();
    [[nodiscard]] const std::vector<uint8_t> &QueryPaintOrmPixels() const noexcept { return PaintOrmPixels; }
    [[nodiscard]] const std::vector<uint8_t> &QueryPaintNormalPixels() const noexcept { return PaintNormalPixels; }
    [[nodiscard]] const std::vector<uint8_t> &QueryPaintEmissivePixels() const noexcept { return PaintEmissivePixels; }
    [[nodiscard]] static uint32_t QueryPaintCanvasExtent() noexcept { return PaintCanvasExtent; }

    // Headless / programmatic authoring hooks for validation & evidence capture.
    void ForceCompositePreview();
    void AuthorAddMaterialLayer(const std::string &MaterialNameOrId);
    void AuthorAddGeneratorLayer(const std::string &ChannelId, const std::string &GeneratorId,
        const std::array<float, 4> &Colour, float ScalarAmount);
    void AuthorAddGradientFillLayer(const std::array<float, 4> &StartColour, const std::array<float, 4> &EndColour,
        float AngleDegrees, int AxisMode);
    void AuthorPlaceStampAtUv(const std::string &StampId, float U, float V, float Radius);
    void AuthorPaintDabStroke(float U0, float V0, float U1, float V1, const std::array<float, 4> &Colour,
        float RadiusUv, float Roughness, float Metalness, float HeightDeposit);

    // Unified 3D PBR viewport hooks shared with Material and Baking via MaterialLibraryRendering.
    std::function<void(const SurfaceGeometry &)> ConfigureViewportGeometry;
    std::function<void(const CompiledSurfaceMaterial &)> ConfigureViewportMaterial;
    std::function<void(const WorkspaceViewportOrbitCamera &)> ConfigureViewportCamera;
    std::function<void(uint32_t, uint32_t)> ConfigureViewportExtent;
    std::function<WorkspaceViewportImage()> QueryViewport;

    // 📝 Read-only document facts for headless construction validation (mirrors Baking's QuerySettings).
    [[nodiscard]] const std::string &QueryDocumentName() const noexcept { return DocumentName; }
    [[nodiscard]] size_t QueryLayerCount() const noexcept { return Layers.size(); }
    [[nodiscard]] std::string QuerySelectedLayerName() const;
    [[nodiscard]] size_t QueryHistoryCount() const noexcept { return HistoryEntries.size(); }
    [[nodiscard]] size_t QueryTriangleCount() const noexcept { return Topology.size(); }
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;

    // 📝 Same host contract every other workspace panel exposes: one shared dockspace owns layout/platform.
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }

  private:
    bool InitializeContent() noexcept override;
    void FinalizeContent() noexcept override;
    void IntegrateContent(double DeltaSeconds) noexcept override;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 DOCUMENT MODEL
    //--------------------------------------------------------------------------------------------------------------
    enum class LayerKind { Paint, Fill, Material, Generator, Decal, Folder };
    enum class RegionScope { Face, Tile, Island, Object };

    struct PaintDab
    {
        float U = 0, V = 0;      // authoring-UDIM coordinates (U spans 0..4 across tiles 1001-1004)
        float Radius = 0.03f;    // in UV units of one tile
        std::array<float, 4> Colour{1, 1, 1, 1}; // rgb + dab strength
        int Part = -1;
        float Hardness = .92f;
        float Roundness = 1.f;
        float Angle = 0.f;
        bool Square = false;
        uint8_t GrainMode = 0; // 0 = none, 1 = noise, 2 = lines, 3 = checker
        float GrainAmount = 0.f;
        float GrainScale = 4.f;
        float HeightStrength = .4f;
    };
    struct PaintFill
    {
        RegionScope Scope = RegionScope::Object;
        int Part = -1;
        int Chart = -1;
        int Triangle = -1;
        std::array<float, 4> Colour{1, 1, 1, 1};
    };
    // 🖨️ One placed decal: a StampLibrary entry (by id) anchored at an authoring UV, tinted at
    //    placement time, with its own PBR behaviour. Two evaluations exist: the legacy UV-planar
    //    quad (U/V/Radius) and the surface PROJECTOR (Projected=true) - a world-space frame the
    //    stamp is projected through onto the surface, so it lands only where the projector box and
    //    facing test say, never smeared across unrelated UV islands or the model's back side.
    //    U/V/Radius stay maintained as UV-space mirrors (UV-view gizmo, footprint extents).
    struct StampPlacement
    {
        std::string StampId;
        float U = 0, V = 0;
        float Radius = 0.06f; // half-size in authoring-UV units (mirror of WorldRadius when projected)
        float Rotation = 0;   // radians, counter-clockwise about the projector normal / in UV space
        std::array<float, 4> Tint{1, 1, 1, 1};
        bool Projected = false;
        float Origin[3]{};    // projector centre on the surface (world)
        float AxisRight[3]{}; // final stamp axes (base frame rotated by Rotation about Normal)
        float AxisUp[3]{};
        float Normal[3]{}; // projector aim: the stamp projects along -Normal
        float BaseRight[3]{}; // unrotated anchor frame (camera- or UV-tangent-aligned at placement)
        float BaseUp[3]{};
        float WorldRadius = 0; // half-size in world units
        // 🎚️ Per-decal channel targets (ChannelTargetsPanel's flat targets, stored flattened like
        // LayerState's channelSettings): which paint channels this decal writes, each channel's
        // deposited Value, and its saved source (mode/generator). Seeded at placement from the
        // stamp design's surface-channel strengths when it carries a vector source; the neutral
        // PBR defaults (roughness .6 / metalness 0 / height .5) preserve the pre-targets floats.
        std::vector<std::string> Channels{"baseColor", "metalness", "roughness", "height"};
        std::map<std::string, std::string> ChannelModes; // value | texture | generator
        std::map<std::string, float> ChannelValues{{"roughness", .6f}, {"metalness", 0.f}, {"height", .5f}};
        std::map<std::string, std::array<float, 4>> ChannelColours;
        std::map<std::string, std::string> ChannelGenerators;
        std::map<std::string, std::map<std::string, float>> ChannelGeneratorParams;
    };
    struct MaskState
    {
        bool Present = false;
        std::string Kind = "fill"; // fill | paint | color | gradient (Alloy's mask type pills)
        bool Enabled = true;
        bool Inverted = false;
        float Fill = 1.f;          // 1 = white, 0 = black
        float Strength = 100.f;
        std::array<float, 4> Colour{0.72f, 0.45f, 0.2f, 1.f}; // #b87333
        float Tolerance = 0.1f;
        float Softness = 0.1f;
        std::vector<PaintDab> Dabs;
        std::vector<PaintFill> Fills;
    };
    struct LayerState
    {
        std::string Id;
        std::string Name;
        LayerKind Kind = LayerKind::Paint;
        std::string ParentId; // empty = root
        bool Visible = true;
        bool Locked = false;
        bool Expanded = false;
        float Opacity = 100.f;
        std::string Blend = "Normal";
        std::vector<std::string> Channels;
        // 📝 paintChannelModel's channelSettings, flattened: per-channel source configuration (mode/value/
        // generator) stored sparsely - a missing key reads as Alloy's documented default for that channel.
        // These are authoring targets only; the preview composite does not evaluate channel sources.
        std::map<std::string, std::string> ChannelModes;                          // value | texture | generator | gradient
        std::map<std::string, float> ChannelValues;                               // scalar-edit channels
        std::map<std::string, std::array<float, 4>> ChannelColours;               // color-edit channels
        std::map<std::string, std::string> ChannelGenerators;                     // generator id ("" = none)
        std::map<std::string, std::map<std::string, float>> ChannelGeneratorParams;
        MaskState Mask;
        std::vector<PaintDab> Dabs;
        std::vector<PaintFill> Fills;
        std::vector<StampPlacement> Stamps;
        int StrokeCount = 0;
        int DecalCount = 0;
        std::string SourceAsset;       // assigned content-browser asset name, empty = none
        std::string SourceAssetType;   // material | generator | brush
        std::array<float, 4> SourceColour{0, 0, 0, 0};
    };
    // 🖌️ digitalBrush.js's DIGITAL_BRUSH_DEFAULTS, field for field.
    struct BrushState
    {
        std::string Preset = "round";
        std::string Tool = "paint"; // paint | erase | fill
        // 🖌️ PaintWorkshop instrument driving the stamp engine (PaintStampEngine.h). "Digital" preserves the
        // original dab-only behaviour; anything else feeds the raster stamp canvas for the stroke's layer.
        std::string PhysicalTool = "Digital";
        std::array<float, 4> Colour{0.843f, 0.549f, 0.349f, 1.f}; // #d78c59
        float Size = 24.f;
        float Opacity = 1.f;
        float Flow = 1.f;
        float Hardness = 0.92f;
        float Spacing = 0.16f;
        float Stabilization = 0.f;
        float SizeJitter = 0.f;
        float OpacityJitter = 0.f;
        float Scatter = 0.f;
        std::string Shape = "round"; // round | square
        float Roundness = 1.f;
        float Rotation = 0.f;
        bool Follow = false;
        std::string Grain = "none"; // none | noise | lines | checker
        float GrainAmount = 0.f;
        float GrainScale = 4.f;
        float HeightStrength = 0.4f;
        RegionScope FillScope = RegionScope::Object;
    };

    std::string DocumentName = "Teapot study";
    std::vector<LayerState> Layers; // topmost first, everywhere (Alloy's ordering contract)
    std::string SelectedLayerId;
    bool MaskSelected = false;
    uint64_t NextLayerSerial = 1;
    BrushState Brush;

    void ResetDocument();
    [[nodiscard]] LayerState *FindLayer(const std::string &Id) noexcept;
    [[nodiscard]] const LayerState *FindLayer(const std::string &Id) const noexcept;
    [[nodiscard]] LayerState *SelectedLayer() noexcept { return FindLayer(SelectedLayerId); }
    [[nodiscard]] int LayerDepth(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool EffectiveLocked(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool EffectiveVisible(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool HiddenByCollapsedFolder(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool IsDescendantOf(const std::string &Id, const std::string &AncestorId) const noexcept;
    void SelectLayer(const std::string &Id, bool Mask = false);
    void AddLayer(LayerKind Kind);
    void DuplicateSelectedLayer();
    void DeleteSelectedLayer();
    void MoveSelectedLayer(int Direction); // -1 = up (toward top), +1 = down, among siblings
    void SetLayerParent(const std::string &Id, const std::string &ParentId);
    void UngroupSelectedFolder();
    void AddMaskToSelected(bool ColourMask);
    void RemoveSelectedMask();
    void ToggleChannel(LayerState &Layer, const std::string &ChannelId);
    void ClearSelectedPaint();
    [[nodiscard]] std::string MakeLayerId(const char *Prefix);
    [[nodiscard]] std::string UniqueLayerName(const std::string &Base) const;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 HISTORY (snapshot undo/redo)
    //--------------------------------------------------------------------------------------------------------------
    struct Snapshot
    {
        std::vector<LayerState> Layers;
        std::string SelectedId;
        bool MaskSelected = false;
        BrushState Brush;
    };
    struct HistoryEntry
    {
        Snapshot State;
        std::string Label;
        std::string Detail;
        std::string Kind; // project | layer | folder | mask | channel | stroke | tool | source
        std::string TimeText;
    };
    std::vector<HistoryEntry> HistoryEntries;
    int HistoryIndex = -1;
    std::optional<HistoryEntry> PendingHistory;
    double PendingHistoryClock = 0.0;
    [[nodiscard]] Snapshot CaptureSnapshot() const;
    void RestoreSnapshot(const Snapshot &State);
    void CommitHistory(const std::string &Label, const std::string &Detail, const std::string &Kind = "layer");
    void QueueHistory(const std::string &Label, const std::string &Detail, const std::string &Kind = "layer");
    void Undo();
    void Redo();
    void RestoreHistoryEntry(int Index);
    [[nodiscard]] bool CanUndo() const noexcept { return HistoryIndex > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return HistoryIndex >= 0 && HistoryIndex + 1 < int(HistoryEntries.size()); }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 PROJECT SAVE / DRAFT
    //--------------------------------------------------------------------------------------------------------------
    std::filesystem::path ProjectDirectory;
    bool Dirty = false;
    double ClockSeconds = 0.0;
    double LastEditClock = -100.0;
    [[nodiscard]] std::string SerializeProject() const;
    bool WriteProjectFile(const std::filesystem::path &Path) const;
    void SaveProject();
    void OpenProject();
    void MarkDirty() noexcept { Dirty = true; LastEditClock = ClockSeconds; }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 TEAPOT GEOMETRY + UV TOPOLOGY
    //--------------------------------------------------------------------------------------------------------------
    DemoBakeAssets Assets;
    struct TopologyTriangle
    {
        uint32_t VertexIndex[3]{0, 0, 0};
        int Part = 0;   // 0 body, 1 lid, 2 handle, 3 spout (textureSurfaceModel.js TEXTURE_PARTS order)
        int Chart = 0;  // 0-based teapot patch
        int Index = 0;  // triangle ordinal
        float AuthoringUv[3][2]{}; // UDIM authoring set (U offset by part tile)
        float SourceUv[3][2]{};    // original overlapping patch UV0
        float CentroidUv[2]{0, 0};
        float Position[3][3]{};    // world-space corners (decal projection evaluates per-texel world points)
        float Normal[3]{};         // outward face normal
        float CentroidWorld[3]{};
        float BoundsRadius = 0;    // world bounding-sphere radius about CentroidWorld
    };
    std::vector<TopologyTriangle> Topology;
    std::vector<float> VertexCurvature;
    std::vector<float> VertexOcclusion;
    std::vector<float> VertexThickness;
    float MeshBoundsMin[3]{-.5f, -.5f, -.5f};
    float MeshBoundsMax[3]{.5f, .5f, .5f};
    void BuildTopology();
    [[nodiscard]] static int PartForPatch(int Patch) noexcept;
    void AuthoringUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept;
    void SourceUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 PREVIEW COMPOSITE + PAINT INPUT
    //--------------------------------------------------------------------------------------------------------------
    [[nodiscard]] static bool FillMatches(const PaintFill &Fill, const TopologyTriangle &Triangle) noexcept;
    [[nodiscard]] float MaskCoverageAt(const LayerState &Layer, const TopologyTriangle &Triangle, const float Uv[2],
        const std::vector<int32_t> *DabCandidates = nullptr) const noexcept;
    [[nodiscard]] std::array<float, 3> EvaluatePreviewColour(const TopologyTriangle &Triangle) const noexcept;
    // One layer's contribution at an explicit authoring-UV point (the per-layer block of
    // EvaluatePreviewColour, shared with the texel compositor). Candidate lists optionally restrict
    // the dab loops to one spatial-grid cell - same result, dabs outside the cell cannot reach here.
    void ApplyPreviewLayer(std::array<float, 3> &Base, const LayerState &Layer, const TopologyTriangle &Triangle,
        const float Uv[2], const std::vector<int32_t> *DabCandidates, const std::vector<int32_t> *MaskDabCandidates,
        bool VisibilityPrechecked = false) const noexcept;
    struct PaintPbrTexel
    {
        std::array<float, 3> BaseColor{.92f, .91f, .88f};
        float Roughness = .38f;
        float Metalness = 0.f;
        float Height = 0.f;
        float NormalScale = 1.f;
        float ExplicitNormalX = 0.f, ExplicitNormalY = 0.f;
        float Occlusion = 1.f;
        float Opacity = 1.f;
        std::array<float, 3> Emission{0.f, 0.f, 0.f};
        float Anisotropy = 0.f;
        float AnisotropyAngle = 0.f;
        float Clearcoat = .12f;
        float Transmission = 0.f;
        float RefractionIndex = 1.5f;
        std::array<float, 3> Sheen{0.f, 0.f, 0.f};
        std::array<float, 3> Subsurface{0.f, 0.f, 0.f};
    };
    void ApplyPreviewPbrLayer(PaintPbrTexel &Texel, const LayerState &Layer, const TopologyTriangle &Triangle,
        const float Uv[2], const std::vector<int32_t> *DabCandidates, const std::vector<int32_t> *MaskDabCandidates,
        bool VisibilityPrechecked = false) const noexcept;
    void EnsurePreviewCanvas();
    void CompositePreviewCanvas();
    void BuildPreviewCanvasOwners();
    [[nodiscard]] uint64_t PreviewCanvasFingerprint() const noexcept;
    [[nodiscard]] bool PreviewCanvasTextured() const;
    [[nodiscard]] std::array<float, 2> PreviewCanvasUv(int Part, float U, float V) const noexcept;
    [[nodiscard]] bool DigitalPaintAvailable() const noexcept;
    void ApplyPaintAt(const float Uv[2], int Part, int Chart, int Triangle, float RadiusUv);
    void FinishStroke();
    // 🖌️ PaintWorkshop stamp engine (HarborPaintWorkshop.html port): a 2048x512 raster canvas over the 4-tile
    // UDIM strip. One layer owns the canvas at a time (the last layer painted with a physical tool); dabs stay
    // the document/undo truth, the canvas is the preview surface for its owning layer.
    PaintStampEngine StampEngine;
    bool StampCanvasConfigured = false;
    std::string StampCanvasLayerId;
    bool StampStrokeActive = false;
    void EnsureStampEngine();
    [[nodiscard]] bool StampToolSelected() const noexcept;
    void FeedStampEngine(const LayerState &Layer, const float Uv[2], int Triangle, float RadiusUv, bool Erasing);
    [[nodiscard]] bool BuildStampHit(const float Uv[2], int Triangle, float RadiusUv, StampHit &Out, float &WorldRadius) const noexcept;
    void SyncStampParams(float WorldRadius) noexcept;
    void ClearStampCanvas() noexcept;
    // 🖨️ Stamp library + decal placement (StampLibrary.h). The picker is a right-click popup in the
    // viewport while the Stamp author tool is active; placements live on layers like dabs/fills do.
    StampLibrary StampLib;
    std::filesystem::path StampLibraryDirectory;
    std::string ActiveStampId;
    // 🖨️ "New stamp channels" (DecalComponentsPanel's stamp brush): the armed stamp's seeded
    // targets, cached per stamp/library generation, under per-aspect sticky overrides - the
    // first edit to the channels, the values or the sources freezes that whole aspect over
    // every later stamp's own seed (Alloy's effective-snapshot brush edits). Tool state only:
    // not in History snapshots, not serialized.
    std::string StampBrushSeedId;
    uint64_t StampBrushSeedGeneration = 0;
    bool StampBrushSeedValid = false;
    StampPlacement StampBrushSeed;
    bool StampBrushChannelsOverridden = false;
    std::vector<std::string> StampBrushChannels;
    bool StampBrushValuesOverridden = false;
    std::map<std::string, float> StampBrushValues;
    std::map<std::string, std::array<float, 4>> StampBrushColours;
    bool StampBrushSourcesOverridden = false;
    std::map<std::string, std::string> StampBrushModes;
    std::map<std::string, std::string> StampBrushGenerators;
    std::map<std::string, std::map<std::string, float>> StampBrushGeneratorParams;
    bool StampLibraryScanned = false;
    bool StampRightDownValid = false;
    float StampRightDownX = 0, StampRightDownY = 0;
    // Material-catalogue-style picker state: All / Presets / Saved filter + name search, plus a
    // 48-block thumbnail cache so the picker samples each stamp once instead of every frame.
    int StampFilter = 0;
    char StampSearch[64]{};
    std::map<std::string, std::vector<uint32_t>> StampThumbCache;
    void EnsureStampLibrary(bool ForceReload = false);
    [[nodiscard]] const StampLibraryEntry *ActiveStamp() const noexcept;
    void PlaceStampAt(const float Uv[2]);
    // 🎚️ A stamp design's own channel targets: seed a placement's channels/values from the
    // entry's vector source (neutral defaults remain when it carries none).
    void SeedPlacementChannels(const StampLibraryEntry &Entry, StampPlacement &Placement) const;
    void ConstructStampPicker();
    // 📚 Stamp Studio's library page as a docked window here as well: ONE shared catalogue body
    // (shortcut pills, heading + count, search, filter pills, THE STAMP COLLECTION card grid,
    // footer) feeds both the right-click picker popup and this dock, so the decal library reads
    // identically in both workspaces. The difference is the click: Stamp Studio opens the design
    // for editing; here a click SELECTS the stamp and arms the Stamp tool.
    void ConstructStampLibraryDock();
    void ConstructStampCatalogue(bool Docked);
    // 🖨️ Full-fidelity stamp rasters: the library entries hold small picker-sized rasters (256px
    // presets / saved PNGs), but the ACTUAL stamp source is resolution-independent - builtin presets
    // are signed-distance fields and stamps saved from the Stamp workspace keep their vector document
    // in a .stamp.toml sidecar. Placements therefore resolve to a raster rendered at an extent matched
    // to their footprint on the target texture (diameter on the 512px tile, 2x supersampled, bucketed
    // to 256/512/1024/2048), cached per (stamp, extent). Raster-only stamps without a vector source
    // fall back to their stored raster - that IS their actual stamp.
    std::map<std::pair<std::string, int>, StampLibraryEntry> StampRasterCache;
    std::set<std::pair<std::string, int>> StampRasterFallbacks; // (stamp, extent) already known to have no better source
    [[nodiscard]] static int StampRasterExtentFor(float Radius) noexcept;
    void ResolveStampRasters();
    // Resolve ONE library entry at an extent (builtin field re-render / vector sidecar re-rasterize),
    // verified against the stored raster so a stale sidecar can never override the art the library
    // shows. Returns &Entry when the stored raster is already the best source. Warms the caches.
    [[nodiscard]] const StampLibraryEntry *ResolveStampEntry(const StampLibraryEntry &Entry, int Extent);
    // Placement anchoring handoff: the viewports set these immediately before PlaceStampAt so the new
    // stamp is born projected (surface triangle + which base frame to align to).
    const TopologyTriangle *PendingAnchorTriangle = nullptr;
    bool PendingAnchorCamera = false;
    [[nodiscard]] const StampLibraryEntry *ResolvedStampRaster(const StampPlacement &Placement) const noexcept;
    // 🅰️ Text decals: the Text author tool stamps typed text exactly like a library stamp (hover
    // ghost, projector, own Decal layer, gizmo, per-decal PBR). A text placement's StampId IS its
    // content ("text:<text>"), so its entry is synthesized on demand from real DM Sans glyph
    // outlines - white, tintable art fitted to the stamp square, rasterized at any extent - and
    // saved projects round-trip with no extra data. FindStampEntry is the one lookup placements,
    // hover and the Inspector share (library first, synthesized text second).
    std::filesystem::path ContentRoot;
    char TextBuffer[192]{};
    bool TextFocusPending = false; // clicking the Text pill drops the keyboard straight into the field
    mutable std::map<std::string, StampLibraryEntry> TextEntryCache; // synthesized 256px base entries, by "text:" id
    mutable std::map<std::string, float> TextPointSizes;              // fitted point size per content
    mutable std::mutex TextSynthesisLock; // ResolvedStampRaster runs on composite workers; synthesis stays serialized
    char InspectorTextBuffer[192]{};
    std::string InspectorTextForId; // placement id the inspector text buffer was seeded from
    [[nodiscard]] bool DecalToolActive() const noexcept { return Tool == AuthorTool::Stamp || Tool == AuthorTool::Text; }
    [[nodiscard]] std::string TextContent() const;
    [[nodiscard]] const StampLibraryEntry *FindStampEntry(const std::string &Id) const noexcept;
    bool SynthesizeTextEntry(const std::string &Id, int Extent, StampLibraryEntry &Out) const noexcept;
    // 🅰 The Text tool's own properties (its right-click menu): an ink colour and a typeface,
    // independent of the paint brush. A placed text's stamp id carries its face -
    // "text:<face id>:<content>" (legacy "text:<content>" still parses, as the default face).
    std::string TextFontId{"DMSans/DMSans-Regular"};
    std::array<float, 4> TextInk{1.f, 1.f, 1.f, 1.f};
    bool TextPropertiesPending = false; // Text-pill right-click asks the viewport scope to open the popup
    bool TextFontListOpen = false;
    bool InspectorFontListOpen = false;
    std::map<std::string, std::vector<uint8_t>> FontPreviewCache; // face id -> 256px preview square
    std::string TextPreviewKey;                                    // face + content the live preview shows
    std::vector<uint8_t> TextPreviewPixels;
    double TextPreviewBuiltAt = 0;
    [[nodiscard]] bool ParseTextStampId(const std::string &Id, std::string &FontId, std::string &Content) const;
    void ConstructTextProperties();
    bool ConstructFontList(const std::string &CurrentId, std::string &PickedId, float Width);
    void DrawTextPreviewStrip(const std::vector<uint8_t> &Pixels, uint32_t Extent,
        float MinX, float MinY, float MaxX, float MaxY, const float Tint[3]);
    // 🧲 Stamp dragging (Transform tool, and grabbing an existing stamp with the Stamp tool): one
    // placement is manipulated at a time; the gesture mutates live and commits one history step on
    // release. Modes: 0 move, 1 scale (corner), 2 rotate (top handle, UV view).
    struct StampDragState
    {
        bool Active = false;
        int Mode = 0;
        std::string LayerId;
        size_t Index = 0;
        float GrabDu = 0, GrabDv = 0; // move: placement centre minus grab point (authoring UV)
        float GrabAngle = 0;          // rotate/scale: pointer angle/distance at grab
        float GrabDistance = 0;
        float GrabLocal[2]{};            // 3D move: grab point in projector-local coords (keeps the grab spot glued)
        float GrabScreenCentre[2]{};     // 3D scale/rotate: gizmo centre on screen at grab
        bool SceneGesture = false;       // drag lives in the 3D viewport (screen-space scale/rotate)
        StampPlacement Original;
        bool Moved = false;
    } StampDrag;
    std::string StampFocusLayerId; // layer + index the gizmo/inspector stamp controls act on
    int StampFocusIndex = 0;
    // Decal child-row press/drag in the layer stack (a Decal layer lists its stamps beneath it):
    // a press arms here, dragging past the threshold reorders the placements live, and the top of
    // ConstructLayersDock resolves the release (focus on click, history when the order changed).
    std::string DecalPressLayerId;
    int DecalPressStamp = -1; // storage index being pressed/dragged (follows the placement as it moves)
    float DecalPressY = 0;
    bool DecalDragArmed = false, DecalDragMoved = false;
    // Hit-test stamps topmost-first at an authoring UV; on success returns layer/index and the zone
    // (0 body/move, 1 corner/scale, 2 rotate handle; HandleSlop in authoring-UV units, 0 = no handles).
    [[nodiscard]] bool HitTestStamp(const float Uv[2], float HandleSlop, std::string &OutLayerId, size_t &OutIndex, int &OutZone) const noexcept;
    [[nodiscard]] StampPlacement *FocusedStamp(LayerState *&OutLayer) noexcept;
    void BeginStampDrag(const std::string &LayerId, size_t Index, int Mode, const float Uv[2]);
    void UpdateStampDrag(const float Uv[2]);
    void FinishStampDrag();
    // 🎯 Surface-projection plumbing. Anchors (re)build a placement's projector frame on a surface
    // triangle at an authoring UV: UV-tangent base frame (stamp reads aligned with the UVs) or
    // camera-aligned base frame (stamp reads upright to the view it was placed from). Both keep the
    // U/V/Radius mirrors coherent. Weights come from the authoring-UV barycentric of the point.
    static void SyncProjectorAxes(StampPlacement &Placement) noexcept;
    void AnchorProjector(StampPlacement &Placement, const TopologyTriangle &Triangle, const float Uv[2], const float RightDirection[3]) const noexcept;
    void AnchorStampFromUv(StampPlacement &Placement, const TopologyTriangle &Triangle, const float Uv[2]) const noexcept;
    void AnchorStampFromCamera(StampPlacement &Placement, const TopologyTriangle &Triangle, const float Uv[2]) const noexcept;
    [[nodiscard]] const TopologyTriangle *TriangleAtAuthoringUv(const float Uv[2], float Weights[3]) const noexcept;
    [[nodiscard]] static bool BarycentricWeights(const TopologyTriangle &Triangle, const float Uv[2], float Weights[3]) noexcept;
    [[nodiscard]] static float WorldPerUvAt(const TopologyTriangle &Triangle, const float Weights[3]) noexcept;
    // 👻 Hover ghost: with the Stamp tool armed, the picked stamp is live-projected onto the surface
    // under the cursor (composited at reduced strength, no click needed), exactly as a click would
    // drop it. Built fresh each frame from the current pick; never part of the layer document.
    StampPlacement HoverStamp;
    bool HoverStampActive = false;
    void RefreshHoverStamp(const TopologyTriangle *Hit, const float Uv[2], bool CameraAligned);
    // Last drawn 3D camera frame (for camera-aligned anchors between the interaction and draw phases).
    float SceneRight[3]{1, 0, 0}, SceneUp[3]{0, 1, 0};
    bool SceneFrameValid = false;
    // 🧲 3D stamp gizmo, cached from the last draw for this frame's hit-testing (screen points:
    // 0-3 corners, 4 rotate grip, 5 centre).
    float StampGizmoScreen[6][2]{};
    bool StampGizmoValid = false;
    std::string StampGizmoLayerId;
    size_t StampGizmoIndex = 0;
    void BeginStampDragScene(const std::string &LayerId, size_t Index, int Mode, const TopologyTriangle *Hit, const float Uv[2], float MouseX, float MouseY);
    void DragStampScene(const TopologyTriangle *Hit, const float Uv[2], float MouseX, float MouseY);
    // 🎨 Preview-canvas state (see QueryPaintCanvasImage above). Scratch/Owners/Pixels all share the
    // atlas-page layout (PaintCanvasExtent², tiles inset by the 1px gutter); owners map each texel
    // to the Topology triangle that owns it so region fills/masks keep their per-face identity at
    // texel fidelity. Recomposited only when the content fingerprint moves.
    struct PaintLoadedTexture
    {
        int Width = 0, Height = 0;
        std::vector<uint8_t> Rgba;
    };
    struct PaintLoadedMaterial
    {
        std::string Id;
        std::string Name;
        std::string Category;
        std::array<float, 3> BaseAlbedo{.8f, .8f, .8f};
        float BaseRoughness = .5f;
        float BaseMetallic = 0.f;
        float BaseCoat = .12f;
        float BaseIor = 1.5f;
        AutomotivePaint Flakes{};
        PaintLoadedTexture AlbedoMap, OrmMap, NormalMap;
    };
    mutable std::vector<PaintLoadedMaterial> LoadedMaterials;
    mutable std::map<std::string, PaintLoadedTexture> LoadedChannelTextures;
    mutable std::vector<std::string> AvailableTextureNames;
    mutable bool ArchiveCatalogueScanned = false;
    void EnsureArchiveCatalogue() const;
    [[nodiscard]] const PaintLoadedMaterial *FindLoadedMaterial(const std::string &NameOrId) const;
    [[nodiscard]] const PaintLoadedTexture *FindLoadedChannelTexture(const std::string &TextureName) const;

    std::vector<uint8_t> PaintCanvasPixels;
    std::vector<std::array<float, 3>> PaintCanvasScratch;
    std::vector<PaintPbrTexel> PaintPbrScratch;
    std::vector<uint8_t> PaintEmissivePixels;
    float CompositedAvgClearcoat = .12f;
    float CompositedAvgIor = 1.5f;
    float CompositedAvgAnisotropy = 0.f;
    float CompositedAvgAnisotropyAngle = 0.f;
    float CompositedMaxTransmission = 0.f;
    bool CompositedHasEmission = false;
    std::vector<int32_t> PaintCanvasOwners;
    std::vector<uint8_t> PaintCanvasFilled;
    uint64_t PaintCanvasRevision = 0;
    uint64_t PaintCanvasFingerprintCache = 0;
    uint64_t StampLibraryGeneration = 0;
    bool PaintCanvasComposited = false;
    bool PaintCanvasOwnersBuilt = false;
    size_t PaintCanvasOwnersTopology = 0;
    double PaintCanvasLastComposite = -1.0;
    bool StrokeActive = false;
    bool StrokeChanged = false;
    bool StrokeTargetMask = false;
    std::string StrokeTargetLayerId;
    float LastDabUv[2]{0, 0};
    float StabilizedUv[2]{0, 0};
    bool LastDabValid = false;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 VIEWPORT STATE
    //--------------------------------------------------------------------------------------------------------------
    enum class ViewMode { Lit, Wireframe, Uv };
    ViewMode View = ViewMode::Lit;
    int TargetPart = -1; // -1 = All parts
    bool UvSource = false;
    bool Grid = true;
    enum class AuthorTool { Navigate, Stamp, Transform, Text };
    AuthorTool Tool = AuthorTool::Navigate;
    bool DigitalActive = false;
    bool MaskPreview = false; // authoring-bar Material/Mask preview tabs

    // 📝 Same orbit camera + lag the Baking/Material viewports use (MaterialPreviewConfiguration).
    MaterialPreviewConfiguration Camera{.Yaw = .7f, .Pitch = .16f};
    MaterialPreviewConfiguration SmoothedCamera;
    bool SmoothedCameraReady = false;
    void SynchronizeCamera();
    void FrameCamera() noexcept;

    // 🧭 UV camera: Screen = RegionMin + Offset + (U * Scale, (1 - V) * Scale).
    float UvScale = 0.f, UvOffsetX = 0.f, UvOffsetY = 0.f, UvFrameScale = 1.f;
    bool UvFrameRequested = true;
    std::string UvHoverText;
    void FrameUv(float RegionWidth, float RegionHeight);

    // 🎯 Last frame's projected triangles, kept so this frame's pointer can pick/paint the surface.
    struct PickTriangle
    {
        float Screen[3][2]{};
        float Uv[3][2]{};
        float Depth = 0;
        int Part = -1, Chart = -1, Index = -1;
    };
    std::vector<PickTriangle> LastPickTriangles;
    [[nodiscard]] const PickTriangle *PickScene(float X, float Y, float OutUv[2]) const noexcept;

    // 🖱️ Click-vs-orbit discrimination for part picking (a drag orbits, a click selects the hit part).
    bool PointerDownValid = false;
    float PointerDownX = 0, PointerDownY = 0;

    struct MaskEditorState
    {
        enum class Tool { Brush, Select, Navigate };
        Tool ActiveTool = Tool::Brush;
        RegionScope Scope = RegionScope::Object;
        bool InkWhite = true;
        float SizePercent = 4.f;
        float Opacity = 100.f;
        float Hardness = 80.f;
        bool OptionsOpen = true;
        struct Region
        {
            RegionScope Scope = RegionScope::Object;
            int Part = -1, Chart = -1, Triangle = -1;
        };
        std::optional<Region> SelectedRegion;
    };
    MaskEditorState MaskEditor;
    [[nodiscard]] std::string MaskRegionLabel() const;
    void FillMaskRegion();

    void ConstructSceneViewport(PlaneExtent Region);
    void ConstructUvViewport(PlaneExtent Region);
    void HandlePaintPointer(bool Hovered, bool IsUv);

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    enum class RightPanelTab { Inspector, History };
    RightPanelTab RightTab = RightPanelTab::Inspector;
    bool HistoryStrokesOnly = false;
    int HistoryChosen = -1;
    // 📝 ChannelPropertyPanel's picker + per-channel fold state (Alloy starts these six channels folded).
    bool ChannelPickerOpen = false;
    std::set<std::string> ChannelFolded{"height", "normal", "occlusion", "anisotropy", "anisotropyAngle", "clearcoat"};
    // 📝 TextureMaskPropertyPanel's collapsible Mask section head.
    bool MaskCardCollapsed = false;
    char LayerQuery[64]{};
    enum class LayerFilter { All, Visible, Hidden };
    LayerFilter Filter = LayerFilter::All;
    bool AddMenuOpen = false;
    bool SystemBaseOpen = false;
    bool HelpOpen = false;
    bool BrushStudioOpen = false;
    bool BrushStudioChannels = false;
    // 📝 DigitalChannelProperties' single-open accordion (one channel card at a time).
    std::string BrushChannelOpened = "baseColor";
    char LayerNameBuffer[96]{};
    std::string LayerNameSelectionId;
    std::string Status = "Local layer project";
    std::string Error;
    std::string Toast;
    double ToastClock = -100.0;
    void ShowToast(const std::string &Text) { Toast = Text; ToastClock = ClockSeconds; }

    std::vector<MaterialAsset> MaterialAssets;

    void ConstructHeader();
    void ConstructLayersDock();
    void ConstructViewportDock();
    void ConstructInspectorDock();
    void ConstructInspector();
    void ConstructHistory();
    void ConstructBrushStudio();
    void ConstructStatusBar();
    void DrawLayerThumbnail(float MinimumX, float MinimumY, float Size, const LayerState &Layer) const;
    void DrawMaskIcon(float MinimumX, float MinimumY, float Size, const MaskState &Mask) const;
    // 🧩 Shared channel-property rendering (ChannelPropertyPanel / ChannelTargetsPanel /
    // DigitalChannelProperties): the targets card and the per-channel cards over flattened
    // channelSettings-shaped maps, so layer, decal and brush channels all edit identically.
    bool AmountSliderRow(const char *Label, float &Value, float Minimum, float Maximum, float DisplayScale, const char *Unit);
    void SmallNoteRow(const char *Text);
    enum class ChannelTargetStyle { Picker, Checklist, Toggles };
    struct ChannelCardOptions
    {
        bool Locked = false;
        bool GradientMode = false;      // Gradient pill (Base color only) + placeholder body
        bool AlwaysShowValue = false;   // keep Colour/Amount (and a value preview) under every source
        bool ArtworkBaseColor = false;  // Base color shows the artwork note instead of a colour editor
        bool MaterialGenerator = false; // offer Material studio; otherwise it only shows when inherited
        bool SingleOpen = false;        // one unfolded card at a time (BrushChannelOpened)
        const char *FoldPrefix = "";    // ChannelFolded key prefix ("" = inspector layer cards)
        const char *PreviewCaption = nullptr; // nullptr = "{Mode} preview"
        const char *DerivedNote = nullptr;
        const char *TextureNote = nullptr;
        const char *GeneratorNoteSuffix = " · source configuration.";
        const char *MaterialNote = nullptr;
        const char *GradientNote = nullptr; // inherited-gradient explanation when GradientMode is false
    };
    // Targets card: heading + enable/disable affordances. Writes only PendingToggle/PendingClear;
    // the caller applies them after the card so iteration state stays valid during the render.
    void ConstructChannelTargetsCard(const std::vector<std::string> &Channels, bool Locked, ChannelTargetStyle Style,
        bool &PickerOpen, std::string &PendingToggle, bool &PendingClear, const char *Heading = "Channel targets");
    // One folded card per enabled channel. OnChanged fires after every mutation (queue history there);
    // ColourOf/SetColour default to the colour map - the brush studio routes Base color to Brush.Colour.
    void ConstructChannelCards(const std::vector<std::string> &Channels,
        std::map<std::string, std::string> &Modes, std::map<std::string, float> &Values,
        std::map<std::string, std::array<float, 4>> &Colours, std::map<std::string, std::string> &Generators,
        std::map<std::string, std::map<std::string, float>> &GeneratorParams,
        const ChannelCardOptions &Options,
        const std::function<void(const std::string &, const char *)> &OnChanged,
        const std::function<std::array<float, 4>(const std::string &)> &ColourOf = {},
        const std::function<void(const std::string &, const std::array<float, 4> &)> &SetColour = {});
    [[nodiscard]] bool PillButton(const char *Id, const char *Label, float Width, bool Selected, const char *IconPath = nullptr, uint32_t Accent = 0);
    [[nodiscard]] bool IconButton(const char *Id, const char *IconPath, float Size, bool Selected = false, uint32_t Colour = 0x9a9a8e);
    void ApplyBrushPreset(const std::string &PresetId);
    [[nodiscard]] const char *BrushPresetName() const noexcept;

    PanelDockspace Panels;
    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;

    size_t CachedSceneTopologySize = 0;
    int CachedSceneTargetPart = -2;
    SurfaceGeometry CachedSceneGeometry;
    uint64_t CachedSceneMaterialRevision = 0;
    int CachedSceneMaterialTargetPart = -2;
    CompiledSurfaceMaterial CachedSceneMaterial;
    WorkspaceViewportOrbitCamera SentSceneCamera{};
    bool SentSceneCameraReady = false;
    uint32_t SentSceneWidth = 0, SentSceneHeight = 0;
    WorkspaceViewportOrbitCamera CachedSceneCpuOrbit{};
    uint64_t CachedSceneCpuRevision = 0;
    int CachedSceneCpuTargetPart = -2;
    uint32_t CachedSceneCpuWidth = 0, CachedSceneCpuHeight = 0;
    std::vector<uint8_t> CachedSceneCpuRgba;
    std::vector<uint8_t> PaintOrmPixels;
    std::vector<uint8_t> PaintNormalPixels;
};
} // namespace Frontier
