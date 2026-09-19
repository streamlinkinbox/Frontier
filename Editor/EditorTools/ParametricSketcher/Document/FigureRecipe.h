//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/FigureRecipe.h — How a derived figure was made, so it can be made again when its sources move
//============================================================================================================================================
// Plasticity keeps the sketch curves after a loft / sweep / extrude and the result follows them. Here that link is a
//    recipe stored on the derived figure: the operation, its options, and the identities of the input figures (curves,
//    sketch areas by their bounding curves, or body edges). After every command the document regenerates each recipe
//    whose inputs' geometry fingerprint changed; a recipe that can no longer be satisfied keeps its last geometry and
//    reports why. Sources are ordinary figures — move / rotate / pull poles with the gizmo and the result updates.
#pragma once
#include "Kernel/FairPatchSolver.h"
#include "Kernel/SkinSolver.h"
#include <string>
#include <vector>

namespace Frontier
{

class SceneDocument;
struct Workplane;

enum class RecipeOperation : uint8_t { None = 0, Extrude, Revolve, Loft, Sweep, Pipe, Patch, FairPatch };
[[nodiscard]] const char* Describe(RecipeOperation Operation) noexcept;

// One input: a curve figure, a sketch area (its bounding curves + the centroid that identifies the cell), or a body edge.
struct RecipeInput
{
    enum class Form : uint8_t { Curve, Area, Edge } Shape = Form::Curve;                // [-]
    std::vector<uint32_t> Figures;                                                      // [-] curve identity, area bounding curves, or the body
    Vec3                  Centroid;                                                     // [m] area cell signature
    int                   Edge = -1;                                                    // [-] body edge index
    RimContinuity         Continuity = RimContinuity::Position;                         // [-] fair patch: continuity against the rim's support
    double                Tension = 1.0;                                                // [-] fair patch
    uint32_t              Support = 0;                                                  // [-] fair patch: surface / body figure the rim lies on (0 = the edge's own body)
    int                   Face = -1;                                                    // [-] fair patch: which face of the edge's body (−1 = the one the fill continues flush)
    [[nodiscard]] std::string Label(const SceneDocument& Scene) const noexcept;
};

struct FigureRecipe
{
    RecipeOperation          Operation = RecipeOperation::None;                         // [-]
    std::vector<RecipeInput> Sections;                                                  // [-] profiles in flow order (patch: boundaries)
    RecipeInput              Path;                                                      // [-] sweep / pipe path
    std::vector<RecipeInput> Guides;                                                    // [-] fair patch: interior curves to pass through
    FairPatchOptions         Fair;                                                      // [-] fair patch
    LoftOptions              Loft;                                                      // [-]
    LoftGuideOptions         LoftGuides;                                                // [-] loft: interior curves to pass through (resolved)
    std::vector<RecipeInput> LoftGuideInputs;                                           // [-] loft: guide inputs (FigureRecipe identities, resolved per build)
    SweepOptions             Sweep;                                                     // [-]
    Vec3                     Direction = Vec3::UnitZ();                                 // [-] extrude
    double                   Length = 0.0;                                              // [m] extrude
    Vec3                     AxisOrigin;                                                // [m] revolve
    Vec3                     Axis = Vec3::UnitY();                                      // [-] revolve
    double                   Angle = 0.0;                                               // [rad] revolve
    double                   Radius = 0.0;                                              // [m] pipe
    bool                     Sheet = false;                                             // [-] force sheet output
    uint64_t                 InputFingerprint = 0;                                      // [-] geometry of the inputs at the last build
    std::string              Complaint;                                                 // [-] why the last regeneration failed (empty = fine)

    [[nodiscard]] bool Live() const noexcept { return Operation != RecipeOperation::None; }
    // Resolve every input against the document. Multi-loop sections come back as several curves per station.
    [[nodiscard]] static Deliver<std::vector<NurbsCurve>> ResolveInput(const RecipeInput& In, const SceneDocument& Scene, const Workplane& Work) noexcept;
    // The surface a fair-patch rim must be tangent to: the adjacent face of the edge's body, or the named figure.
    [[nodiscard]] static const NurbsSurface* ResolveSupport(const RecipeInput& In, const SceneDocument& Scene, Vec3 Hint) noexcept;
    [[nodiscard]] Vec3 Hint(const SceneDocument& Scene, const Workplane& Work) const noexcept;    // centroid of the rim samples
    [[nodiscard]] uint64_t FingerprintInputs(const SceneDocument& Scene, const Workplane& Work) const noexcept;
    // Build the figure's geometry. Exactly one of the outputs is filled, chosen by the recipe.
    struct Product { NurbsSurface Sheet; BrepBody Body; std::vector<NurbsSurface> Sheets; bool IsBody = false; };
    [[nodiscard]] Deliver<Product> Produce(const SceneDocument& Scene, const Workplane& Work, FairPatchReport* Report = nullptr) const noexcept;
    [[nodiscard]] std::string Summary(const SceneDocument& Scene) const noexcept;
};

} // namespace Frontier
