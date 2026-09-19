//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/SceneDocument.h — Named geometry figure with stable identities (curves and surfaces for now; solids in Phase 6)
//============================================================================================================================================
#pragma once

#include "Kernel/SurfaceSpecification.h"
#include "Kernel/TopologySpecification.h"
#include "Kernel/ProfileSolver.h"
#include "Document/FigureRecipe.h"
#include <string>
#include <vector>

namespace Frontier
{

enum class FigureClassification : uint8_t { Curve, Surface, Body, Empty };

// A closed area of the sketch: one cell of the planar arrangement of all coplanar (workplane) curves. Derived — rebuilt after
//    every change to the curves — so it is never edited directly; only its Filled choice is user-owned and survives rebuilds by
//    matching the cell's centroid + area signature.
struct SketchArea
{
    uint32_t              Identity = 0;                                                 // [-] pick identity (own range)
    PlanarCell            Cell;                                                         // [-] outer + holes
    Vec3                  Centroid;                                                     // [m] of the outer loop's tessellation
    Vec3                  Normal = Vec3::UnitZ();                                       // [-] plane normal of the arrangement
    bool                  Filled = true;                                                // [-] semi-transparent fill → extrudes as a solid
    bool                  Selected = false;                                             // [-]
    std::vector<uint32_t> BoundingIdentities;                                             // [-] figures whose curves bound it
    [[nodiscard]] std::vector<NurbsCurve> Loops() const noexcept { return Cell.Loops(); }
};

enum class SelectMode : uint8_t { Control = 1, Edge = 2, Face = 3, Whole = 4 };           // Plasticity 1/2/3/4
[[nodiscard]] inline const char* SelectModeName(SelectMode M) noexcept { switch (M) { case SelectMode::Control: return "control"; case SelectMode::Edge: return "edge"; case SelectMode::Face: return "face"; default: return "whole"; } }

struct SceneFigure
{
    uint32_t     Identity = 0;                                                          // [-] stable, 1-based, doubles as pick identity
    FigureClassification     Classification = FigureClassification::Curve;                                                // [-]
    std::string  Name;                                                                  // [-] user-facing, unique
    NurbsCurve   Curve;                                                                 // valid when Classification == Curve
    NurbsSurface Surface;                                                               // valid when Classification == Surface
    BrepBody     Body;                                                                  // valid when Classification == Body
    bool         Construction = false;                                                  // [-] drawn dashed, never rendered as solid
    bool         Hidden = false;                                                        // [-]
    bool         Selected = false;                                                      // [-]
    uint8_t      Matcap = 0;                                                            // [-] studio layer (Plasticity: one per whole)
    float        Tint[3] = { 0.62f, 0.66f, 0.72f };                                     // [-] body colour
    std::vector<int> SelectedPoles;                                                     // [-] control-point selection (mode 1), pole indices
    std::vector<int> SelectedFaces;                                                     // [-] face selection (mode 3), body face indices
    std::vector<int> SelectedEdges;                                                     // [-] edge selection (mode 2), body edge indices
    FigureRecipe     Recipe;                                                            // [-] how this figure is derived (None = authored)
    // ---- Parametric source (Phase 13+) ----------------------------------------------------
    // The original creation parameters for this figure, kept alongside the rebuilt geometry so a
    //    `dim edit` on a live dim can re-derive the figure from scratch. Plasticity-style: dims on a
    //    figure represent the *input* values, not just measurements, so editing them rebuilds the
    //    model. Set to Form::None for figures that don't support parametric editing (e.g. booleans
    //    derived from a sketch with no recorded source — those are read-only).
    enum class ParametricForm : uint8_t
    {
        None = 0,
        Box, Sphere, Cylinder, Cone, Torus,                       // bodies
        Line, Circle, Arc, Ellipse, Polyline, Spline, Rectangle,  // curves
        Extrude, Revolve, Loft, Sweep, Pipe, Boolean,             // derived (Phase 16: live-edit for the rest)
        ChamferEdge,                                              // modifier
        Empty,                                                    // Phase 19: transform handle, no geometry
    };
    struct ParametricBlueprint
    {
        ParametricForm Form = ParametricForm::None;
        // Generic positional / size params. Each primitive uses only the subset it needs.
        Vec3   A, B, C, Axis, Normal, MajorDirection;              // [m] corners, points, axis/normal of rotation
        double R0 = 0, R1 = 0, R2 = 0, R3 = 0;                     // [m] generic radii / lengths / angles
        int    I0 = 0, I1 = 0;                                      // [-] generic integer slots (e.g. edge index for chamfer, degree for spline)
        std::vector<Vec3> PolylinePoints;                           // [-] points for polyline / spline
        bool   Closed = false;                                     // [-] closed flag
        // For modifiers (chamfer, future fillet, boolean), record the *input* body so a live edit
        //    re-applies the operation to a clean copy. Without this, re-chamfering a chamfered edge
        //    would fail or be a no-op.
        BrepBody PreOpBody;                                         // [-] for chamfer/fillet: the body the operation was applied to
    };
    ParametricBlueprint Blueprint;                                       // [-] the parametric blueprint for live dim editing (formerly ParametricSource.Source)
    [[nodiscard]] bool FaceSelected(int I) const noexcept { for (int F : SelectedFaces) if (F == I) return true; return false; }
    [[nodiscard]] bool EdgeSelected(int I) const noexcept { for (int E : SelectedEdges) if (E == I) return true; return false; }

    [[nodiscard]] int  PoleCount() const noexcept { return Classification == FigureClassification::Curve ? int(Curve.Poles.size()) : Classification == FigureClassification::Surface ? int(Surface.Poles.size()) : 0; }
    [[nodiscard]] Vec3 PolePosition(int Index) const noexcept { return (Classification == FigureClassification::Curve ? Curve.Poles[Index] : Surface.Poles[Index]).Divide(); }
    void MovePole(int Index, Vec3 P) noexcept
    {
        Vec4& H = Classification == FigureClassification::Curve ? Curve.Poles[Index] : Surface.Poles[Index];
        H.X = P.X * H.W; H.Y = P.Y * H.W; H.Z = P.Z * H.W;
    }
    [[nodiscard]] bool PoleSelected(int Index) const noexcept { for (int I : SelectedPoles) if (I == Index) return true; return false; }

    [[nodiscard]] Box3 Bounds() const noexcept
    {
        if (Classification == FigureClassification::Empty) { Box3 B; B.Include(Blueprint.A); return B; }
        return Classification == FigureClassification::Curve ? Curve.Bounds() : Classification == FigureClassification::Surface ? Surface.Bounds() : Body.Bounds();
    }
    void Transform(const Mat4& M) noexcept
    {
        if (Classification == FigureClassification::Curve) Curve = Curve.Transformed(M); else if (Classification == FigureClassification::Surface) Surface = Surface.Transformed(M); else Body = Body.Transformed(M);
    }
};

class SceneDocument
{
public:
    [[nodiscard]] SceneFigure& AddCurve(std::string Name, NurbsCurve Curve) noexcept;
    [[nodiscard]] SceneFigure& AddSurface(std::string Name, NurbsSurface Surface) noexcept;
    [[nodiscard]] SceneFigure& AddBody(std::string Name, BrepBody Body) noexcept;
    bool Remove(uint32_t Identity) noexcept;
    [[nodiscard]] SceneFigure*       Find(uint32_t Identity) noexcept;
    [[nodiscard]] SceneFigure*       Find(const std::string& Name) noexcept;
    [[nodiscard]] const std::vector<SceneFigure>& Figures() const noexcept { return Entries; }
    [[nodiscard]] std::vector<SceneFigure>&       Figures() noexcept { return Entries; }
    [[nodiscard]] Box3 Bounds(bool SelectedOnly = false) const noexcept;

    // Sketch areas (derived). RebuildAreas() runs the planar arrangement over every visible, non-construction curve lying in
    //    the given plane; fill flags are carried across by signature. Areas pick with identities ≥ AreaIdentityBase.
    static constexpr uint32_t AreaIdentityBase = 8000;                                  // [-] below the 14-bit pick limit
    void RebuildAreas(const Workplane& Plane) noexcept;
    [[nodiscard]] const std::vector<SketchArea>& Areas() const noexcept { return Cells; }
    [[nodiscard]] std::vector<SketchArea>&       Areas() noexcept { return Cells; }
    [[nodiscard]] SketchArea* FindArea(uint32_t Identity) noexcept;
    [[nodiscard]] const SketchArea* FindArea(uint32_t Identity) const noexcept;
    [[nodiscard]] const SketchArea* AreaBySignature(Vec3 Centroid) const noexcept;      // nearest centroid within tolerance

    // Derived figures: rebuild every recipe whose inputs changed. Returns the names regenerated (and fills Complaint on
    //    figures whose recipe can no longer be satisfied — their geometry is left as it was).
    std::vector<std::string> Regenerate(const Workplane& Plane) noexcept;
    [[nodiscard]] std::vector<const SceneFigure*> DerivedFrom(uint32_t Identity) const noexcept;   // figures whose recipe uses Identity
    [[nodiscard]] SketchArea* AreaAt(Vec3 P) noexcept;                                  // innermost area containing P
    [[nodiscard]] std::vector<SketchArea*> AreasOf(uint32_t FigureIdentity) noexcept;   // areas bounded by that curve
    [[nodiscard]] int SelectedAreaCount() const noexcept { int N = 0; for (const SketchArea& A : Cells) if (A.Selected) ++N; return N; }
    [[nodiscard]] std::string UniqueName(const std::string& Stem) const noexcept;
    void Clear() noexcept { Entries.clear(); NextIdentity = 1; }
    [[nodiscard]] int  SelectedCount() const noexcept { int N = 0; for (const SceneFigure& I : Entries) if (I.Selected) ++N; return N; }
    [[nodiscard]] int  SelectedPoleCount() const noexcept { int N = 0; for (const SceneFigure& I : Entries) N += int(I.SelectedPoles.size()); return N; }
    [[nodiscard]] int  SelectedFaceCount() const noexcept { int N = 0; for (const SceneFigure& I : Entries) N += int(I.SelectedFaces.size()); return N; }
    [[nodiscard]] int  SelectedEdgeCount() const noexcept { int N = 0; for (const SceneFigure& I : Entries) N += int(I.SelectedEdges.size()); return N; }
    void ClearSelection() noexcept { for (SketchArea& A : Cells) A.Selected = false; for (SceneFigure& I : Entries) { I.Selected = false; I.SelectedPoles.clear(); I.SelectedFaces.clear(); I.SelectedEdges.clear(); } }
    // Pick identities: low 14 bits figure identity, bits 14-15 the part (0 body, 1 pole, 2 face, 3 edge), high 16 bits sub index + 1.
    enum class PickPart : uint8_t { Figure = 0, Pole = 1, Face = 2, Edge = 3 };
    [[nodiscard]] static uint32_t PickOf(uint32_t Identity, int Pole = -1) noexcept { return PickOf(Identity, Pole < 0 ? PickPart::Figure : PickPart::Pole, Pole); }
    [[nodiscard]] static uint32_t PickOf(uint32_t Identity, PickPart Part, int Index) noexcept { return (Identity & 0x3FFFu) | (uint32_t(Part) << 14) | (uint32_t(Index + 1) << 16); }
    [[nodiscard]] static uint32_t IdentityOf(uint32_t Pick) noexcept { return Pick & 0x3FFFu; }
    [[nodiscard]] static PickPart PartOf(uint32_t Pick) noexcept { return static_cast<PickPart>((Pick >> 14) & 3u); }
    [[nodiscard]] static int      SubIndexOf(uint32_t Pick) noexcept { return int(Pick >> 16) - 1; }
    [[nodiscard]] static int      PoleOf(uint32_t Pick) noexcept { return PartOf(Pick) == PickPart::Pole ? SubIndexOf(Pick) : -1; }
    [[nodiscard]] static int      FaceOf(uint32_t Pick) noexcept { return PartOf(Pick) == PickPart::Face ? SubIndexOf(Pick) : -1; }
    [[nodiscard]] static int      EdgeOf(uint32_t Pick) noexcept { return PartOf(Pick) == PickPart::Edge ? SubIndexOf(Pick) : -1; }
    // Duplicate an figure (new identity, unique name); returns the copy.
    [[nodiscard]] SceneFigure& Duplicate(const SceneFigure& Original) noexcept;

private:
    std::vector<SceneFigure> Entries;
    std::vector<SketchArea>  Cells;                                                     // derived
    struct FillChoice { Vec3 Centroid; double Area; bool Filled; };
    std::vector<FillChoice>  Fills;                                                     // fill choices, matched by signature
    uint32_t NextIdentity = 1;
};

} // namespace Frontier
