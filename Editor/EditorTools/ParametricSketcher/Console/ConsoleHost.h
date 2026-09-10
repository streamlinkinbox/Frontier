//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Console/ConsoleHost.h — The SolidArc console: command dispatch over a SceneDocument, camera and raster
//============================================================================================================================================
// Every command prints what it did as a chart row; `render <file>` writes Proofs/<file>.png through the RasterExchange.
//    The same host runs `.arc` scripts (line by line, halting on the first refusal unless --continue) and a REPL.
#pragma once

#include "CommandCodec.h"
#include "Document/SceneDocument.h"
#include "Document/UndoSequence.h"
#include "Interaction/CameraProjection.h"
#include "Interaction/ToolSession.h"
#include "Interaction/TransformGizmo.h"
#include "Presentation/SoftwareRaster.h"
#include "Kernel/ConstraintGraph.h"
#include "Kernel/MirrorSolver.h"
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace Frontier
{

class ConsoleHost
{
public:
    // Phase 19: a parsed mirror spec — either a plane (origin + normal) or an axis (origin + direction).
    //    Filled in by ParseMirrorSpec. The actual reflection is done by MirrorSolver.cpp.
    struct MirrorSpec
    {
        enum class Kind : uint8_t { Plane, Axis };
        Kind        Kind         = Kind::Plane;
        Vec3        Origin       = Vec3{};
        Vec3        NormalOrDir  = Vec3::UnitZ();
        std::string Label;                                                  // [-] source token for error messages
    };


public:
    explicit ConsoleHost(std::string ProofFolder, uint32_t Width = 1280, uint32_t Height = 800) noexcept;

    // Returns false on refusal; the reason is printed. Multiple commands per line are allowed.
    bool Execute(std::string_view Line) noexcept;
    bool RunScript(const std::string& Path, bool ContinueOnRefusal) noexcept;
    int  RunInteractive(std::FILE* In) noexcept;

    [[nodiscard]] SceneDocument&    Document() noexcept { return Scene; }
    [[nodiscard]] CameraProjection& Camera() noexcept { return View; }
    [[nodiscard]] int               RefusalCount() const noexcept { return Refusals; }
    [[nodiscard]] const TransformGizmo& Gizmo() const noexcept { return GizmoRig; }
    [[nodiscard]] const UndoSequence&  Timeline() const noexcept { return Undo; }
    [[nodiscard]] SelectMode            CurrentSelectMode() const noexcept { return Mode; }

    // ---- Dimension public types (Phase 13) --------------------------------------------------
    // The free function that formats dim labels lives in ConsoleHost.cpp's anonymous namespace and needs
    //    to take these by reference, so the types have to be public.
    enum class DimensionForm : uint8_t { Linear, Angle, Radius, Diameter, ArcLength, Bbox };
    struct DimensionEntry
    {
        uint32_t        Id        = 0;                                                 // [-] unique within Host
        DimensionForm   Form      = DimensionForm::Linear;
        uint32_t        Anchor    = 0;                                                 // [-] 0 = free / not tied to a figure
        std::string     AnchorName;                                                   // [-] human-readable anchor (figure name + axis etc.)
        Vec3            A, B, N;                                                      // [m] world-space endpoints and orientation
        double          Value     = 0.0;                                               // [m] or [rad] depending on Form
        std::string     Label;                                                         // [-] formatted text (e.g. "3.50" or "90.0°")
        bool            Auto      = true;                                              // [-] true = auto-emitted (may be re-emitted), false = user-added (preserved across re-emit)
        bool            Hidden    = false;                                             // [-] draw flag
        // ---- Phase 13 redo: live dim editing (Plasticity-style) ----------------------------
        // Live dims are tied to a slot on the figure's ParametricBlueprint. The slot names a specific
        //    input (R0..R3 or A.X..A.Z or B.X..B.Z). Editing a live dim modifies that slot and
        //    rebuilds the figure. Slot = -1 means a free-form measurement (read-only).
        int             Slot      = -1;                                                // [-] -1 = not live, 0..N = slot index
        SceneFigure::ParametricForm BlueprintForm = SceneFigure::ParametricForm::None;     // [-] which builder this dim is tied to
        // ---- Phase 17: sub-entity anchors -------------------------------------------------
        // A dim can be tied to a specific face or edge of the body, not just the whole figure.
        //    AnchorFace = the face index into Body.Faces; AnchorEdge = the edge index into Body.Edges.
        //    Both are -1 when the dim is whole-figure. A dim with AnchorFace ≥ 0 was created by
        //    `dim <figure> face <F>` (or auto-emitted by `dim sub <figure>`); a dim with AnchorEdge
        //    ≥ 0 was created by `dim <figure> edge <E>`. Sub-entity dims are read-only measurements
        //    (Slot = -1) — their value is recomputed from the geometry every time the body rebuilds.
        int             AnchorFace = -1;                                              // [-] -1 = whole figure, ≥ 0 = face index
        int             AnchorEdge = -1;                                              // [-] -1 = whole figure, ≥ 0 = edge index
        // ---- Phase 17: leader dims --------------------------------------------------------
        // A leader is a free-floating dim: a line from a feature point (A) to a label position
        //    (B), with the label drawn at B. Set Leader = true and the renderer draws a leader
        //    line + dot + offset label instead of the standard dim line + extension lines. The
        //    label text is taken from D.Label; if D.Label is empty the renderer formats D.Value
        //    (so `dim leader B (x,y,z) M6 hole` produces a leader with the text "M6 hole", and
        //    a leader without a custom label still shows the measurement).
        bool            Leader    = false;                                             // [-] true = leader dim (line + dot + label), false = standard dim
    };
    [[nodiscard]] const std::vector<DimensionEntry>& AllDimensions() const noexcept { return Dimensions; }
    [[nodiscard]] const std::vector<SceneFigure>& AllFigures() const noexcept { return Scene.Figures(); }
    [[nodiscard]] const std::vector<ConstraintEntry>& AllConstraints() const noexcept { return CGraph.AllConstraints(); }
    [[nodiscard]] const std::vector<ConstraintAnchor>& AllConstraintAnchors() const noexcept { return CGraph.AllAnchors(); }

    // Renders the current document into the raster (no file); public so verification can inspect pixels.
    void Render() noexcept;
    [[nodiscard]] const RasterExchange& Raster() const noexcept { return *Surface; }
    [[nodiscard]] const Workplane&     WorkPlane() const noexcept { return Plane; }

private:
    using Command = std::function<bool(const CommandLine&)>;
    void Register() noexcept;
    bool Refuse(const char* Format, ...) noexcept;
    void Row(const char* Format, ...) noexcept;
    void DescribeFigure(const SceneFigure& Figure) noexcept;
    bool AddCurve(const CommandLine& C, const char* Stem, Deliver<NurbsCurve> Result) noexcept;
    bool AddSurface(const CommandLine& C, const char* Stem, Deliver<NurbsSurface> Result) noexcept;
    // Overloads that record a ParametricBlueprint on the figure (Phase 13 redo: live dim editing).
    bool AddCurve(const CommandLine& C, const char* Stem, Deliver<NurbsCurve> Result, SceneFigure::ParametricBlueprint Source) noexcept;
    bool AddBody(const CommandLine& C, const char* Stem, Deliver<BrepBody> Result, SceneFigure::ParametricBlueprint Source) noexcept;
    [[nodiscard]] SceneFigure* Resolve(const std::string& Token) noexcept;
    [[nodiscard]] std::vector<SceneFigure*> ResolveMany(const CommandLine& C, size_t FirstIndex) noexcept;
    [[nodiscard]] Workplane ActivePlane() const noexcept { return Plane; }

    void RegisterInteraction() noexcept;                                                // Phase 3 commands
    void RegisterSelection() noexcept;                                                  // Phase 4 commands
    void DrawControlPoints(const SceneFigure& Figure) noexcept;
    void DrawBody(const SceneFigure& Figure) noexcept;
    void DrawAreas() noexcept;                                       // faces + edges with sub-pick ids
    bool AddBody(const CommandLine& C, const char* Stem, Deliver<BrepBody> Result) noexcept;
    bool AddDerived(const CommandLine& C, const char* Stem, FigureRecipe Recipe) noexcept;                             // cage + poles with per-pole pick ids
    bool AddDerived(const CommandLine& C, const char* Stem, FigureRecipe Recipe, SceneFigure::ParametricBlueprint Source) noexcept; // Phase 16: live-edit Blueprint for derived ops
    bool SelectAtPixel(double X, double Y, bool Toggle) noexcept;                       // click-select honouring the mode
    int  SelectInRectangle(double X0, double Y0, double X1, double Y1, bool Toggle, bool Subtract) noexcept;
    void HoverAtPixel(double X, double Y) noexcept;
    [[nodiscard]] Vec3 SelectionPivot() const noexcept;                                 // figure bounds centre or selected-pole centroid
    void ApplyDeltaToSelection(const Mat4& Delta) noexcept;
    void DrawToolPreview() noexcept;
    // ---- Dimension overlay (Phase 13) --------------------------------------------------------
    void DrawDimensions() noexcept;                                                     // render every non-hidden dim on the overlay pass
    void AutoEmitDimensions(const SceneFigure& Figure) noexcept;                        // auto-emit the standard dim set for a freshly-added figure
    void ReemitAllDimensions() noexcept;                                                // Phase 15: re-emit dims for every figure (called by undo/redo so the dim tree matches the rolled-back scene)
    [[nodiscard]] uint32_t EmitDimension(DimensionForm Form, uint32_t Anchor, const std::string& AnchorName, Vec3 A, Vec3 B, Vec3 N, double Value, bool Auto = true) noexcept;
    [[nodiscard]] int32_t  FindDimensionAtPixel(double X, double Y) const noexcept;     // [-] dim id (0 = none)
    [[nodiscard]] Vec2     WorldToScreen(Vec3 P) const noexcept;                        // [px] world point → NDC-like pixel
    void DeleteAutoDimensionsFor(uint32_t Anchor) noexcept;
    // Phase 20: return a world-space normal / direction flipped to point at the camera. Used by
    //    AutoEmitDimensions to keep dim lines on the side of the body that the viewer actually sees,
    //    regardless of orbit / pan / dolly. `N` must be a unit vector; the returned vector is also unit.
    [[nodiscard]] Vec3 CameraFacingSide(Vec3 N) const noexcept;
    // Live dim editing: a `dim edit` on a live dim reaches into the source figure's ParametricBlueprint
    //    slot, mutates the field, then calls RebuildFromSource to regenerate the geometry.
    bool ApplyLiveEdit(DimensionEntry& D, double NewValue) noexcept;
    // Phase 18: constraint graph helpers. These are used by the `constraint` verb. ParsePointRef decodes
    //    tokens like "L1.start" / "C1.centre" / "P0.vertex3" into a (PointRef, figure, slot, sub, comp) tuple.
    //    ReadBlueprintPoint and WriteBlueprintPoint move a 3D world point in and out of a Blueprint cell.
    //    SolveConstraintGraph materialises the graph, runs Newton, and writes back. RebuildFigureFromBlueprint
    //    reconstructs a figure (line / polyline / circle / rectangle) from its Blueprint after editing.
    [[nodiscard]] bool   ParsePointRef(const std::string& Tok, PointRef& Out, std::string& Figure, int& Slot, int& SubIndex, int& Component) const noexcept;
    [[nodiscard]] bool   ParseLineRef(const std::string& Tok, std::string& Figure) const noexcept;
    [[nodiscard]] Vec3   ReadBlueprintPoint(SceneFigure& F, int Slot, int SubIndex, int Component) const noexcept;
    void                 WriteBlueprintPoint(SceneFigure& F, int Slot, int SubIndex, int Component, Vec3 NewWorld) noexcept;
    bool                 SolveConstraintGraph() noexcept;
    void                 RebuildFigureFromBlueprint(SceneFigure& F) noexcept;

    // Phase 19: mirror + radial + empty helpers. ParseMirrorSpec decodes a token like "xy" / "P_top"
    //    / "(1,2,3)" / "((0,0,0),(0,0,1))" into a plane or axis spec. ApplyMirrors composes a list
    //    of specs. ReflectBlueprint / RotateBlueprint mutate a figure's Blueprint cells. MirrorFigureCopy
    //    / RadialFigureCopy produce a new figure. AddEmpty creates a transform handle.
    [[nodiscard]] bool                ParseMirrorSpec(const std::string& Tok, MirrorSpec& Out) const noexcept;
    [[nodiscard]] Vec3                ApplyMirrors(Vec3 P, const std::vector<MirrorSpec>& Specs) const noexcept;
    [[nodiscard]] bool                ReflectBlueprint(SceneFigure& F, const std::vector<MirrorSpec>& Specs) const noexcept;
    [[nodiscard]] bool                RotateBlueprint(SceneFigure& F, MirrorAxis Axis, double ThetaRadians) const noexcept;
    [[nodiscard]] SceneFigure&        MirrorFigureCopy(const SceneFigure& Source, const std::vector<MirrorSpec>& Specs, const std::string& NewName) noexcept;
    [[nodiscard]] SceneFigure&        RadialFigureCopy(const SceneFigure& Source, MirrorAxis Axis, double ThetaRadians, const std::string& NewName) noexcept;
    [[nodiscard]] SceneFigure&        AddEmpty(Vec3 Position, const std::string& Name) noexcept;
    // Toggle dim display (also flips Hidden on every dim). Used by tests + scripts.
public:
    void SetDimensionsVisible(bool Visible) noexcept { ShowDimensions = Visible; for (auto& D : Dimensions) D.Hidden = !Visible; }
private:
    bool Dispatch(const InputEvent& Event) noexcept;                                    // tool first, then hotkey chart
    void OnToolResult(const ToolResult& Result) noexcept;
    [[nodiscard]] ToolSession::Context ToolContext() const noexcept;

    std::string                          Proofs;
    ToolSession                          Tool;
    TransformGizmo                       GizmoRig;
    bool                                 GizmoShown = true;                             // [-] drawn whenever a selection exists
    std::vector<std::pair<uint32_t, SceneFigure>> GizmoOriginals;                         // [-] figure as they were when the drag began
    void RefreshGizmoPivot() noexcept;
    void ApplyGizmoDelta(const Mat4& Delta) noexcept;
    SnapSettings                         Snap;
    HotkeyChart                          Hotkeys = HotkeyChart::Defaults();
    double                               PointerX = 0.0, PointerY = 0.0;                // [px] synthetic pointer
    std::string                          LastCommand;                                   // [-] for Shift+R
    bool                                 ToolReportedRefusal = false;
    SceneDocument                        Scene;
    UndoSequence                        Undo;
    SelectMode                           Mode = SelectMode::Whole;
    uint32_t                             HoverPick = 0;                                 // [-] pick id under the pointer (last HoverAtPixel)
    bool                                 Recording = false;                            // [-] guards nested Execute during undo
    CameraProjection                     View;
    Workplane                            Plane = Workplane::XY();
    std::map<std::string, Workplane>     NamedPlanes;                                  // [-] named construction planes recalled by `workplane <name>`
    std::vector<DimensionEntry>          Dimensions;                                   // [-] all live dims (public types are above; this is the live storage)
    uint32_t                             NextDimensionId = 1;                          // [-] monotonic
    // Phase 18: persistent constraint graph. Built by `constraint` and resolved by `constraint solve`
    //    (or implicitly by `dim edit` on a constrained figure). The graph is purely 2D — figures are
    //    projected to the workplane for the solve and back into Blueprints afterward.
    ConstraintGraph                      CGraph;                                       // [-] 2D constraint network on the active workplane
    int32_t                              HoverDimensionId = 0;                         // [-] dim id under the pointer, for click-to-edit
    int32_t                              EditDimensionId   = 0;                         // [-] dim awaiting a new value (numeric input in REPL)

    std::unique_ptr<SoftwareRaster>      Surface;
    std::map<std::string, Command>       Commands;
    std::map<std::string, std::string>   Usage;
    int                                  Refusals = 0;
    int                                  LineNumber = 0;
    bool                                 ShowControlCages = false;
    bool                                 ShowIsoCurves = true;
    bool                                 ShowDimensions = false;        // [Phase 13] dims hidden by default until the renderer is polished
    SurfaceShading                       Shading = SurfaceShading::Matcap;

    // Phase 10 contact sheet: four RasterImage buffers captured by `render sheet N` and tiled by
    // `render sheet finalize <name>`. 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
    // The captured tile keeps the raster's current Width/Height at capture time so a script that
    // changes size between tiles is still composited correctly.
    struct Tile { bool Captured = false; uint32_t W = 0; uint32_t H = 0; std::vector<uint8_t> Pixels; };
    Tile                                 SheetTiles[4];
};

} // namespace Frontier
