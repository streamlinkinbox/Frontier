/*====================================================================================================================================
                                                      CORPUSVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 8 gate (plan § 6 row 8): the old FeatureSuitability corpus A–H replayed on the remastered kernel — 0 refusals

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Feature/FeatureCatalogue.h"
#include "Engine/GeometryProcessing/Feature/PathSweep.h"
#include "Engine/GeometryProcessing/Local/FaceChange.h"
#include "Engine/GeometryProcessing/Local/FaceOperationCatalogue.h"
#include "Engine/GeometryProcessing/Local/RedundantTopology.h"
#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr double π = std::numbers::pi;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - R1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Pass condition (text)
        double      Worst    = 0.0;                                                          // [-] - Worst value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    GateTally RefusalGate      = { "R1", "Corpus rows refused (old kernel: 19 of 176)", "0" };
    GateTally VolumeGate       = { "R2", "Volume vs hand-derived closed form", "\\|V − V*\\| ≤ 10⁻⁹·max(1, V*)" };
    GateTally TopologyGate     = { "R3", "Audit sound · expected category · source bodies untouched", "all hold" };
    GateTally MaximalGate      = { "R4", "Edges between faces of one oriented carrier (G5: maximal faces)", "0 (ExtrudeFaces keeps its ring by definition)" };
    GateTally TessellationGate = { "R5", "Tessellation watertight · Euler equal · no inversions · \\|ΔV\\| ≤ sag·area", "all hold" };

    // 📝 What a row builds. Bodies lists every result body (an array makes several); Sources are argument bodies that
    //    must survive the operation unchanged.
    struct BuiltRow
    {
        std::vector<BodyToken> Bodies           = {};                                        // [-]    - Result bodies
        std::vector<BodyToken> Sources          = {};                                        // [-]    - Arguments (must stay intact)
        std::string            Refusal          = {};                                        // [-]    - Non-empty when refused
        double                 Expected         = std::nan("");                              // [len³] - Closed form (NaN: sheet)
        BodyCategory           ExpectedCategory = BodyCategory::Solid;                       // [-]    - Solid or sheet
        bool                   MaximalCondition = true;                                      // [-]    - R4 applies
        std::string            Route            = {};                                        // [-]    - Remastered verb used
    };

    struct CorpusEntry
    {
        int                                    Line          = 0;                            // [-] - Row # in FeatureSuitability.md (0: section E specimen)
        std::string                            Section       = {};                           // [-] - A … H, X (beyond the corpus)
        std::string                            Command       = {};                           // [-] - Corpus command
        std::string                            Legacy        = {};                           // [-] - Old kernel outcome
        std::function<BuiltRow(ModelPartition&)> Build       = {};                           // [-] - Replay
    };

    struct ResultRow
    {
        CorpusEntry  Entry        = {};                                                       // [-]    - Source row
        std::string  Outcome      = {};                                                       // [-]    - solid / sheet / refusal text
        std::string  Route        = {};                                                       // [-]    - Remastered verb
        int          Faces = 0, Edges = 0, Vertices = 0;                                      // [-]    - Result topology (summed)
        int          Redundant    = 0;                                                        // [-]    - R4 count
        double       Volume = 0.0, Expected = 0.0, Error = 0.0;                               // [len³] - Measured / closed form
        int64_t      Triangles    = 0;                                                        // [-]    - Tessellation size
        double       Milliseconds = 0.0;                                                      // [ms]   - Replay time
        bool         PassedCondition = false;                                                 // [-]    - All gates
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                REPLAY HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] OrthonormalFrame AxisFrame(Vector3 Origin, Vector3 Axis)
    {
        return ConstructFrame(Origin, Axis, std::fabs(Axis.Z) > 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 0.0, 1.0 });
    }

    const OrthonormalFrame GroundPlane = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };

    // Corpus "box (x0,y0,z0) (x1,y1,z1)".
    [[nodiscard]] BodyToken Box(ModelPartition& Partition, Vector3 Low, Vector3 High) { return ConstructBlock(Partition, Low, High - Low).Value; }

    // Corpus "cylinder (foot) r h [--axis]".
    [[nodiscard]] BodyToken Rod(ModelPartition& Partition, Vector3 Foot, double Radius, double Height, Vector3 Axis = { 0.0, 0.0, 1.0 })
    {
        return ConstructCylinder(Partition, AxisFrame(Foot, Axis), Radius, Height).Value;
    }

    [[nodiscard]] SketchProfile RectangleProfile(Vector2 Low, Vector2 High)
    {
        SketchModel Sketch;
        static_cast<void>(AppendSketchRectangle(Sketch, Low, High, 1));
        return ExtractSketchProfile(Sketch);
    }

    [[nodiscard]] SketchProfile CircleProfile(Vector2 Centre, double Radius)
    {
        SketchModel Sketch;
        static_cast<void>(AppendSketchCircle(Sketch, AppendSketchPoint(Sketch, Centre), Radius, 1));
        return ExtractSketchProfile(Sketch);
    }

    [[nodiscard]] BuiltRow Single(BodyToken Body, std::string Refusal, double Expected, std::string Route, std::vector<BodyToken> Sources = {})
    {
        BuiltRow Row;
        if (Refusal.empty()) Row.Bodies = { Body };
        Row.Refusal  = std::move(Refusal);
        Row.Expected = Expected;
        Row.Route    = std::move(Route);
        Row.Sources  = std::move(Sources);
        return Row;
    }

    [[nodiscard]] BuiltRow FromSweep(const SweepOutcome& Sweep, double Expected, std::string Route) { return Single(Sweep.Body, Sweep.Refusal, Expected, std::move(Route)); }

    [[nodiscard]] BuiltRow FromBoolean(ModelPartition& Partition, BodyToken First, BodyToken Second, BooleanOperation Operation, double Expected)
    {
        const BooleanOutcome Outcome = ComputeBoolean(Partition, First, Second, Operation);
        return Single(Outcome.Body, Outcome.Refusal, Expected, std::string("ComputeBoolean(") + BooleanOperationLabel(Operation) + ")", { First, Second });
    }

    // Planar faces whose outward normal is Direction.
    [[nodiscard]] std::vector<FaceToken> FacesFacing(const ModelPartition& Partition, BodyToken Body, Vector3 Direction)
    {
        std::vector<FaceToken> Faces;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const FaceProfile& Profile = Partition.Faces.Resolve(Face);
            const auto* Plane = std::get_if<PlaneSurface>(&Partition.Surfaces.Resolve(Profile.Surface));
            if (!Plane) continue;
            const Vector3 Outward = Plane->Frame.ZAxis * (Profile.Sense == Orientation::Forward ? 1.0 : -1.0);
            if (Dot(Outward, Direction) > 1.0 - 1.0e-12) Faces.push_back(Face);
        }
        return Faces;
    }

    // Edges whose end vertices both satisfy Within (straight edges), or circle edges whose centre does (Rims).
    [[nodiscard]] std::vector<EdgeToken> EdgesWhere(const ModelPartition& Partition, BodyToken Body, const std::function<bool(Vector3, Vector3, const CurveGeometry&)>& Within)
    {
        std::vector<EdgeToken> Edges;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (Within(Partition.Vertices.Resolve(Profile.Start).Position, Partition.Vertices.Resolve(Profile.End).Position, Partition.Curves.Resolve(Profile.Curve))) Edges.push_back(Edge);
        }
        return Edges;
    }

    [[nodiscard]] BuiltRow Blend(ModelPartition& Partition, BodyToken Body, const std::vector<EdgeToken>& Edges, BlendProfile Profile, double Size, double Expected, size_t EdgeCount)
    {
        if (Edges.size() != EdgeCount) return Single({}, "edge selection found " + std::to_string(Edges.size()) + " edges, expected " + std::to_string(EdgeCount), Expected, "BlendEdges");
        BlendSettings Settings;
        Settings.Profile = Profile;
        Settings.Size    = Size;
        const BlendOutcome Outcome = BlendEdges(Partition, Body, Edges, Settings);
        return Single(Outcome.Body, Outcome.Refusal, Expected, std::string("BlendEdges(") + BlendProfileLabel(Profile) + ", " + std::to_string(EdgeCount) + " edge" + (EdgeCount > 1 ? "s" : "") + ")", { Body });
    }

    [[nodiscard]] BuiltRow FaceOperation(ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings, double Expected)
    {
        const FaceOperationOutcome Outcome = ApplyFaceOperation(Partition, Body, Settings);
        return Single(Outcome.Body, Outcome.Refusal, Expected, std::string("ApplyFaceOperation(") + FaceOperationLabel(Settings.Kind) + ")", { Body });
    }

    // 📝 Closed forms, derived by hand.
    // Box a×b×c with all 12 edges rounded by r: core + 3 slabs + 3 quarter-cylinder sets + one sphere of corners.
    [[nodiscard]] double RoundedBoxVolume(double A, double B, double C, double R)
    {
        const double Ar = A - 2.0 * R, Br = B - 2.0 * R, Cr = C - 2.0 * R;
        return Ar * Br * Cr + 2.0 * R * (Ar * Br + Br * Cr + Ar * Cr) + π * R * R * (Ar + Br + Cr) + 4.0 / 3.0 * π * R * R * R;
    }

    // Fillet r on one straight convex 90° edge of length L removes the spandrel (1 − π/4)·r² along it.
    [[nodiscard]] double EdgeFilletLoss(double R, double L) { return (1.0 - π / 4.0) * R * R * L; }

    // Fillet r on the convex rim of a bore of radius ρ: spandrel revolved (Pappus), centroid r(10 − 3π)/(3(4 − π)) off each face.
    [[nodiscard]] double RimFilletLoss(double Bore, double R) { return 2.0 * π * (Bore + R * (10.0 - 3.0 * π) / (3.0 * (4.0 - π))) * (1.0 - π / 4.0) * R * R; }

    const double HoleSlabVolume = 3.0 * 2.0 * 0.6 - π * 0.45 * 0.45 * 0.6;                    // [len³] - Slab 3×2×0.6 − bore r 0.45

    [[nodiscard]] BodyToken HoleSlab(ModelPartition& Partition, Vector3 Corner)
    {
        const BodyToken Slab = Box(Partition, Corner, Corner + Vector3{ 3.0, 2.0, 0.6 });
        const BodyToken Bore = Rod(Partition, Corner + Vector3{ 1.5, 1.0, -0.5 }, 0.45, 2.0);
        return ComputeBoolean(Partition, Slab, Bore, BooleanOperation::Difference).Body;
    }

    // 📝 Section H through the feature tree: BlockFillet (sketch → extrude → fillet top-front edge) is the corpus
    //    DriverRect / DrivenBlock / beveladd e0 chain; "transform --scale=(2,1,1)" + "move (0,0,0.5)" on the sketch are a
    //    Width edit and a plane elevation, and the bevel must follow.
    [[nodiscard]] BuiltRow RegenerateDrivenBlock(ModelPartition& Partition, bool EditedCondition)
    {
        FeatureSpecimen Specimen = BuildFeatureSpecimen("BlockFillet");
        FeatureTree&    Tree     = Specimen.Tree;
        if (!AssignFeatureParameters(Tree, { { "Width", EditedCondition ? 4.0 : 2.0 }, { "Depth", 1.5 }, { "Height", 1.0 }, { "Radius", 0.2 } })) return Single({}, "BlockFillet parameters rejected", 0.0, "RegenerateFeatureTree");
        Tree.Steps.front().Plane.Origin = { 0.0, 100.0, 0.0 };
        if (EditedCondition) Tree.Steps.front().Elevation = LiteralValue(0.5);
        const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree);
        const double Width = EditedCondition ? 4.0 : 2.0;
        std::string Refusal;
        for (const FeatureStepReport& Step : Outcome.Steps) if (Step.Status != FeatureStepStatus::Built && Refusal.empty()) Refusal = Step.Label + ": " + Step.Refusal;
        if (Refusal.empty())
        {
            double Low = 1e300, High = -1e300, Left = 1e300, Right = -1e300;                // [len] - z and x extents
            for (VertexToken Vertex : Partition.CollectInventory(Outcome.Body).Vertices)
            {
                const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
                Low = std::min(Low, Position.Z); High = std::max(High, Position.Z); Left = std::min(Left, Position.X); Right = std::max(Right, Position.X);
            }
            const double Floor = EditedCondition ? 0.5 : 0.0;                                // [len] - Expected z₀
            if (std::fabs(Low - Floor) > 1e-12 || std::fabs(High - Floor - 1.0) > 1e-12 || std::fabs(Left) > 1e-12 || std::fabs(Right - Width) > 1e-12)
            {
                Refusal = "regenerated block misplaced: z [" + std::to_string(Low) + ", " + std::to_string(High) + "], x [" + std::to_string(Left) + ", " + std::to_string(Right) + "]";
            }
        }
        return Single(Outcome.Body, Refusal, Width * 1.5 - EdgeFilletLoss(0.2, Width), "RegenerateFeatureTree(BlockFillet)");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CORPUS TABLE
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<CorpusEntry> AuthorCorpus()
    {
        using Partition = ModelPartition;
        std::vector<CorpusEntry> Rows;
        const auto Add = [&](int Line, const char* Section, const char* Command, const char* Legacy, std::function<BuiltRow(Partition&)> Build)
        {
            Rows.push_back({ Line, Section, Command, Legacy, std::move(Build) });
        };

        // ---- A · Primitives.
        Add(2, "A", "box (0,0,0) (2,1.5,1)", "ran", [](Partition& P) { return Single(Box(P, { 0, 0, 0 }, { 2, 1.5, 1 }), "", 3.0, "ConstructBlock"); });
        Add(3, "A", "cylinder (5,1,0) 0.6 1", "ran", [](Partition& P) { return Single(Rod(P, { 5, 1, 0 }, 0.6, 1.0), "", π * 0.36, "ConstructCylinder"); });
        Add(4, "A", "cone (8,1,0) 0.6 0.3 1", "ran", [](Partition& P)
        {
            return Single(ConstructFrustum(P, AxisFrame({ 8, 1, 0 }, { 0, 0, 1 }), 0.6, 0.3, 1.0).Value, "", π / 3.0 * (0.36 + 0.18 + 0.09), "ConstructFrustum");
        });
        Add(5, "A", "sphere (11,1,0.5) 0.5", "ran (V err 1.7e-04)", [](Partition& P) { return Single(ConstructSphere(P, AxisFrame({ 11, 1, 0.5 }, { 0, 0, 1 }), 0.5).Value, "", π / 6.0, "ConstructSphere"); });
        Add(6, "A", "torus (14,1,0.3) 0.8 0.25", "ran", [](Partition& P)
        {
            return Single(ConstructTorus(P, AxisFrame({ 14, 1, 0.3 }, { 0, 0, 1 }), 0.8, 0.25).Value, "", 2.0 * π * π * 0.8 * 0.0625, "ConstructTorus");
        });

        // ---- B · Sketch to solid.
        Add(8, "B", "extrude ProfileRect 1", "ran", [](Partition& P) { return FromSweep(ExtrudeProfile(P, RectangleProfile({ 0, 4 }, { 2, 5.5 }), GroundPlane, 1.0), 3.0, "ExtrudeProfile"); });
        Add(10, "B", "extrude ProfileCircle 1", "ran", [](Partition& P) { return FromSweep(ExtrudeProfile(P, CircleProfile({ 5, 4.75 }, 0.6), GroundPlane, 1.0), π * 0.36, "ExtrudeProfile"); });
        Add(12, "B", "revolve RevolveProfile 360 --origin=(7.5,4,0) --axis=(0,1,0)", "ran", [](Partition& P)
        {
            return FromSweep(RevolveProfile(P, RectangleProfile({ 8, 4 }, { 8.5, 5 }), GroundPlane, { 7.5, 4 }, { 0, 1 }), 2.0 * π * 0.75 * 0.5, "RevolveProfile(2π)");
        });
        Add(14, "B", "revolve HalfRevolveProfile 180 --origin=(9.5,4,0) --axis=(0,1,0)", "ran", [](Partition& P)
        {
            return FromSweep(RevolveProfile(P, RectangleProfile({ 10, 4 }, { 10.5, 5 }), GroundPlane, { 9.5, 4 }, { 0, 1 }, π), π * 0.75 * 0.5, "RevolveProfile(π) — G2 partial");
        });
        Add(17, "B", "loft LoftFoot LoftHead", "ran", [](Partition& P)
        {
            return FromSweep(LoftCircles(P, { 12, 4.75, 0 }, { 0, 0, 1 }, 0.6, { 12, 4.75, 1 }, { 0, 0, 1 }, 0.3), π / 3.0 * (0.36 + 0.18 + 0.09), "LoftCircles → frustum");
        });
        Add(19, "B", "pipe PipeLine 0.2", "ran", [](Partition& P)
        {
            return FromSweep(PipeAlongPath(P, StraightLine{ { 0, 8, 0 }, { 1, 0, 0 } }, 0.0, 3.0, 0.2), π * 0.04 * 3.0, "PipeAlongPath(line) → cylinder");
        });
        Add(21, "B", "pipe PipeArc 0.2 (arc (6,8) 1 0 90)", "ran, freeform (V err 2.8e-06)", [](Partition& P)
        {
            const CircularArc Path{ GroundPlane, 1.0 };
            CircularArc Placed = Path;
            Placed.Frame.Origin = { 6, 8, 0 };
            return FromSweep(PipeAlongPath(P, Placed, 0.0, π / 2.0, 0.2), π / 2.0 * π * 0.04 * 1.0, "PipeAlongPath(arc) → partial torus (G2)");
        });
        Add(24, "B", "sweep SweepProfile SweepPath", "ran", [](Partition& P)
        {
            const OrthonormalFrame Plane{ { 10, 8, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };
            return FromSweep(SweepProfileAlongLine(P, CircleProfile({ 0, 0 }, 0.2), Plane, { 2, 0, 0 }), π * 0.04 * 2.0, "SweepProfileAlongLine → extrude");
        });

        // ---- C · Booleans.
        const auto BoxPair = [](Partition& P, BooleanOperation Operation, double Expected)
        {
            return FromBoolean(P, Box(P, { 0, 12, 0 }, { 4, 15, 2 }), Box(P, { 2, 12, 0 }, { 6, 15, 2 }), Operation, Expected);
        };
        Add(27, "C", "boolean union BoxA -- BoxB", "ran (14 faces)", [=](Partition& P) { return BoxPair(P, BooleanOperation::Union, 36.0); });
        Add(28, "C", "boolean subtract BoxA -- BoxB", "ran", [=](Partition& P) { return BoxPair(P, BooleanOperation::Difference, 12.0); });
        Add(29, "C", "boolean intersect BoxA -- BoxB", "ran", [=](Partition& P) { return BoxPair(P, BooleanOperation::Intersection, 12.0); });
        Add(32, "C", "boolean subtract SlabC -- BoreC", "ran", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 6, 40, 0 }, { 9, 42, 0.6 }), Rod(P, { 7.5, 41, -0.5 }, 0.45, 2.0), BooleanOperation::Difference, HoleSlabVolume);
        });
        Add(35, "C", "boolean union Plate -- Boss", "ran", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 13, 12, 0 }, { 16, 14, 0.6 }), Rod(P, { 14.5, 13, 0.3 }, 0.5, 1.0), BooleanOperation::Union, 3.6 + π * 0.25 * 0.7);
        });
        Add(38, "C", "boolean union Pedestal -- Dome", "ran (V err 2.3e-04)", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 6, 17, 0 }, { 8, 19, 1 }), ConstructSphere(P, AxisFrame({ 7, 18, 1 }, { 0, 0, 1 }), 0.5).Value, BooleanOperation::Union, 4.0 + π / 12.0);
        });
        Add(41, "C", "boolean subtract Socket -- Scoop", "ran (V err 1.1e-04)", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 10, 17, 0 }, { 12, 19, 1 }), ConstructSphere(P, AxisFrame({ 11, 18, 1 }, { 0, 0, 1 }), 0.5).Value, BooleanOperation::Difference, 4.0 - π / 12.0);
        });
        Add(44, "C", "boolean union CrossX -- CrossY (CrossUnequal)", "refused", [](Partition& P)
        {
            return FromBoolean(P, Rod(P, { 0, 18, 0 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, { 2, 16, 0 }, 0.3, 4.0, { 0, 1, 0 }), BooleanOperation::Union, 4.003195681354056);
        });
        Add(47, "C", "boolean union OffsetCrossX -- OffsetCrossY (CrossOffset)", "ran (4.009803 — wrong)", [](Partition& P)
        {
            return FromBoolean(P, Rod(P, { 0, 22, 0.1 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, { 2, 20, 0 }, 0.3, 4.0, { 0, 1, 0 }), BooleanOperation::Union, 4.009989052758214);
        });
        Add(50, "C", "boolean union SteinmetzX -- SteinmetzY", "refused", [](Partition& P)
        {
            return FromBoolean(P, Rod(P, { 6, 22, 0 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, { 8, 20, 0 }, 0.5, 4.0, { 0, 1, 0 }), BooleanOperation::Union, 2.0 * π - 16.0 / 3.0 * 0.125);
        });

        // ---- D · Blends.
        Add(52, "D", "beveladd 0.2 --mode=fillet --targets=BlendBlock:e0…e11", "ran", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 0, 26, 0 }, { 2, 27.5, 1 });
            return Blend(P, Block, P.CollectInventory(Block).Edges, BlendProfile::Fillet, 0.2, RoundedBoxVolume(2.0, 1.5, 1.0, 0.2), 12);
        });
        Add(54, "D", "fillet LegacyFilletBlock 0.2 --edges=0…11", "ran, audit 🔴 trace-on-edge ×4", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 3, 26, 0 }, { 5, 27.5, 1 });
            return Blend(P, Block, P.CollectInventory(Block).Edges, BlendProfile::Fillet, 0.2, RoundedBoxVolume(2.0, 1.5, 1.0, 0.2), 12);
        });
        Add(56, "D", "chamfer LegacyChamferBlock 0.15 --edges=0", "ran", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 6, 26, 0 }, { 8, 27.5, 1 });
            const auto      Edges = EdgesWhere(P, Block, [](Vector3 Start, Vector3 End, const CurveGeometry&) { return Start.Z == 1.0 && End.Z == 1.0 && Start.Y == 26.0 && End.Y == 26.0; });
            return Blend(P, Block, Edges, BlendProfile::Chamfer, 0.15, 3.0 - 0.5 * 0.15 * 0.15 * 2.0, 1);
        });
        Add(60, "D", "beveladd 0.1 --mode=fillet --targets=HoleSlabD:e{rim +z}", "ran, freeform", [](Partition& P)
        {
            const BodyToken Slab  = HoleSlab(P, { 12, 40, 0 });
            const auto      Edges = EdgesWhere(P, Slab, [](Vector3, Vector3, const CurveGeometry& Curve)
            {
                const auto* Arc = std::get_if<CircularArc>(&Curve);
                return Arc && std::fabs(Arc->Frame.Origin.Z - 0.6) < 1e-12;
            });
            return Blend(P, Slab, Edges, BlendProfile::Fillet, 0.1, HoleSlabVolume - RimFilletLoss(0.45, 0.1), 1);
        });

        // ---- E · Face operations: the CorpusE specimens (same bodies, sizes, face picks; closed forms in the catalogue).
        for (const std::string& Name : EnumerateFaceOperationSpecimens())
        {
            ModelPartition Probe;
            const FaceOperationSpecimen Specimen = ConstructFaceOperationSpecimen(Probe, Name);
            if (Specimen.Family != "CorpusE") continue;
            Rows.push_back({ 0, "E", Specimen.Corpus, Specimen.LegacyStatus, [Name](Partition& P)
            {
                const FaceOperationSpecimen Built = ConstructFaceOperationSpecimen(P, Name);
                BuiltRow Row;
                Row.Sources = { Built.Body };
                Row.Route   = std::string("ApplyFaceOperation(") + FaceOperationLabel(Built.Steps.back().Kind) + ")";
                BodyToken Cursor = Built.Body;
                for (const FaceOperationSettings& Step : Built.Steps)
                {
                    const FaceOperationOutcome Outcome = ApplyFaceOperation(P, Cursor, Step);
                    if (!Outcome.Refusal.empty()) { Row.Refusal = Outcome.Refusal; return Row; }
                    Cursor = Outcome.Body;
                }
                Row.Bodies           = { Cursor };
                Row.Expected         = Built.ExpectedVolume;
                Row.ExpectedCategory = Built.ExpectedCategory;
                Row.MaximalCondition = Built.Steps.back().Kind != FaceOperationKind::Extrude;   // ⚠️ ExtrudeFaces adds a side ring by definition
                return Row;
            } });
        }

        // ---- F · Patterning.
        Add(160, "F", "mirror HoleSlabMirror --across=yz", "ran", [](Partition& P)
        {
            const BodyToken Slab = HoleSlab(P, { 18, 58, 0 });
            const TransformOutcome Outcome = MirrorBody(P, Slab, { 0, 0, 0 }, { 1, 0, 0 });
            return Single(Outcome.Body, Outcome.Refusal, HoleSlabVolume, "MirrorBody", { Slab });
        });
        Add(162, "F", "array BlockArray --count=3 --step=(0,0,2)", "ran", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 24, 58, 0 }, { 26, 59.5, 1 });
            BuiltRow Row;
            Row.Route    = "ArrayBody (3 bodies, V summed)";
            Row.Expected = 9.0;
            Row.Bodies   = { Block };
            int Index = 0;
            for (const TransformOutcome& Copy : ArrayBody(P, Block, { 0, 0, 2 }, 3))
            {
                ++Index;
                if (!Copy.Refusal.empty()) { Row.Refusal = Copy.Refusal; Row.Bodies.clear(); return Row; }
                double Low = 1e300;
                for (VertexToken Vertex : P.CollectInventory(Copy.Body).Vertices) Low = std::min(Low, P.Vertices.Resolve(Vertex).Position.Z);
                if (std::fabs(Low - 2.0 * Index) > 1e-12) { Row.Refusal = "copy " + std::to_string(Index) + " misplaced (z₀ = " + std::to_string(Low) + ")"; Row.Bodies.clear(); return Row; }
                Row.Bodies.push_back(Copy.Body);
            }
            return Row;
        });

        // ---- G · Persistent naming bodies (names are gated in FeatureValidation F4; here the operations themselves).
        Add(164, "G", "beveladd 0.2 --mode=fillet --targets=BlockNamingFillet:e0", "ran, freeform", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 30, 58, 0 }, { 32, 59.5, 1 });
            const auto      Edges = EdgesWhere(P, Block, [](Vector3 Start, Vector3 End, const CurveGeometry&) { return Start.Z == 1.0 && End.Z == 1.0 && Start.Y == 58.0 && End.Y == 58.0; });
            return Blend(P, Block, Edges, BlendProfile::Fillet, 0.2, 3.0 - EdgeFilletLoss(0.2, 2.0), 1);
        });
        Add(167, "G", "boolean subtract NamingSlab -- NamingBore", "ran", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 36, 58, 0 }, { 39, 60, 0.6 }), Rod(P, { 37.5, 59, -0.5 }, 0.45, 2.0), BooleanOperation::Difference, HoleSlabVolume);
        });
        Add(169, "G", "tweak BlockNamingTweak (0,0,0.5) --face=+z", "ran", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 42, 58, 0 }, { 44, 59.5, 1 });
            FaceOperationSettings Settings;
            Settings.Kind   = FaceOperationKind::Tweak;
            Settings.Faces  = FacesFacing(P, Block, { 0, 0, 1 });
            Settings.Vector = { 0, 0, 0.5 };
            return FaceOperation(P, Block, Settings, 4.5);
        });
        Add(171, "G", "push BlockNamingPush 0.5 --face=+z", "ran", [](Partition& P)
        {
            const BodyToken Block = Box(P, { 48, 58, 0 }, { 50, 59.5, 1 });
            FaceOperationSettings Settings;
            Settings.Kind     = FaceOperationKind::Push;
            Settings.Faces    = FacesFacing(P, Block, { 0, 0, 1 });
            Settings.Distance = 0.5;
            return FaceOperation(P, Block, Settings, 4.5);
        });

        // ---- H · Regeneration.
        Add(173, "H", "extrude DriverRect 1 → beveladd 0.2 e0", "ran, freeform", [](Partition& P) { return RegenerateDrivenBlock(P, false); });
        Add(175, "H", "transform DriverRect --scale=(2,1,1) · move (0,0,0.5) → regenerate", "ran", [](Partition& P) { return RegenerateDrivenBlock(P, true); });

        // ---- X · Beyond the corpus: the G4 / G5 scenes the corpus could not express.
        const auto Steinmetz = [](Partition& P, BooleanOperation Operation, double Expected)
        {
            return FromBoolean(P, Rod(P, { 6, 22, 0 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, { 8, 20, 0 }, 0.5, 4.0, { 0, 1, 0 }), Operation, Expected);
        };
        Add(0, "X", "Steinmetz ∩ (G4: 2 singular vertices)", "refused", [=](Partition& P) { return Steinmetz(P, BooleanOperation::Intersection, 16.0 / 3.0 * 0.125); });
        Add(0, "X", "Steinmetz − (G4: two shells)", "refused", [=](Partition& P) { return Steinmetz(P, BooleanOperation::Difference, π - 16.0 / 3.0 * 0.125); });
        Add(0, "X", "Steinmetz 60° ∩ (oblique, 16r³ / 3 sin θ)", "refused", [](Partition& P)
        {
            const Vector3 Along{ 0.5, std::sqrt(3.0) / 2.0, 0.0 };
            return FromBoolean(P, Rod(P, { 6, 22, 0 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, Vector3{ 8, 22, 0 } - Along * 2.0, 0.5, 4.0, Along), BooleanOperation::Intersection, 16.0 * 0.125 / (3.0 * std::sqrt(3.0) / 2.0));
        });
        Add(0, "X", "StepUnion (G5: coplanar back / sides / bottom)", "–", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 0, 0, 0 }, { 4, 3, 2 }), Box(P, { 2, 0, 0 }, { 6, 3, 1 }), BooleanOperation::Union, 30.0);
        });
        Add(0, "X", "FlushSlotDiff (G5: slot through flush faces)", "–", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 0, 0, 0 }, { 3, 3, 2 }), Box(P, { 1, -1, 1 }, { 2, 4, 2 }), BooleanOperation::Difference, 15.0);
        });
        Add(0, "X", "CornerUnion (G5: L-shaped maximal faces)", "–", [](Partition& P)
        {
            return FromBoolean(P, Box(P, { 0, 0, 0 }, { 2, 2, 2 }), Box(P, { 1, 1, 0 }, { 3, 3, 2 }), BooleanOperation::Union, 14.0);
        });
        Add(0, "X", "MirrorCross (traced edges under a reflection)", "–", [](Partition& P)
        {
            const BodyToken Cross = ComputeBoolean(P, Rod(P, { 0, 18, 0 }, 0.5, 4.0, { 1, 0, 0 }), Rod(P, { 2, 16, 0 }, 0.3, 4.0, { 0, 1, 0 }), BooleanOperation::Union).Body;
            const TransformOutcome Outcome = MirrorBody(P, Cross, { 0, 0, 0 }, { 1, 1, 0 });
            return Single(Outcome.Body, Outcome.Refusal, 4.003195681354056, "MirrorBody", { Cross });
        });
        Add(0, "X", "PipeArc full turn (closed torus)", "–", [](Partition& P)
        {
            CircularArc Path{ GroundPlane, 1.0 };
            Path.Frame.Origin = { 6, 8, 0 };
            return FromSweep(PipeAlongPath(P, Path, 0.3, 0.3 + 2.0 * π, 0.2), 2.0 * π * π * 0.04, "PipeAlongPath(arc, 2π) → torus");
        });
        return Rows;
    }

    [[nodiscard]] bool MeasureTessellation(const ModelPartition& Partition, BodyToken Body, bool SheetCondition, int64_t& Triangles)
    {
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, {});
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        Triangles += static_cast<int64_t>(Outcome.Complex.Triangles.size());
        const double VolumeRatio = SheetCondition ? 0.0 : std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea);
        const bool   PassedCondition = Outcome.Refusal.empty() && Report.UnpairedSides == Report.LaminarSides && Report.EulerComplex == Report.EulerBrep && Report.Inverted == 0 &&
                                     VolumeRatio <= 1.0 && Report.MissingEdgeSides == 0;
        Tally(TessellationGate, VolumeRatio, PassedCondition);
        return PassedCondition;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = Clock::now();
    std::string OnlySection;                                                                 // --only: section letter
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlySection = Arguments[++Index];
    }
    std::vector<ResultRow> Rows;
    for (const CorpusEntry& Entry : AuthorCorpus())
    {
        if (!OnlySection.empty() && Entry.Section != OnlySection) continue;
        ModelPartition Partition;
        ResultRow      Row{ Entry };
        const auto     RowStart = Clock::now();
        const BuiltRow Built    = Entry.Build(Partition);
        Row.Milliseconds = Elapsed(RowStart);
        Row.Route        = Built.Route;
        Tally(RefusalGate, Built.Refusal.empty() ? 0.0 : 1.0, Built.Refusal.empty());
        if (!Built.Refusal.empty() || Built.Bodies.empty())
        {
            Row.Outcome = Built.Refusal.empty() ? "no body" : Built.Refusal;
            Rows.push_back(Row);
            continue;
        }
        // Arguments must still audit sound after the operation (operations build new records).
        bool SourcesIntact = true;
        for (BodyToken Source : Built.Sources)
        {
            if (!Partition.Bodies.Authenticate(Source)) continue;                            // Consumed argument (booleans may reuse)
            SourcesIntact = SourcesIntact && AuditBody(Partition, Source).SoundCondition();
        }
        bool SoundCondition = SourcesIntact, TessellationCondition = true, SheetCondition = false;
        double Volume = 0.0;
        for (BodyToken Body : Built.Bodies)
        {
            const AuditReport   Audit     = AuditBody(Partition, Body);
            const BodyInventory Inventory = Partition.CollectInventory(Body);
            const BodyCategory  Category  = Partition.Bodies.Resolve(Body).Category;
            SheetCondition = Category == BodyCategory::Sheet;
            SoundCondition = SoundCondition && Audit.SoundCondition() && Category == Built.ExpectedCategory;
            Row.Faces += static_cast<int>(Inventory.Faces.size());
            Row.Edges += static_cast<int>(Inventory.Edges.size());
            Row.Vertices += static_cast<int>(Inventory.Vertices.size());
            Row.Redundant += CountRedundantEdges(Partition, Body);
            if (!SheetCondition) Volume += IntegrateBodyVolume(Partition, Body);
            TessellationCondition = MeasureTessellation(Partition, Body, SheetCondition, Row.Triangles) && TessellationCondition;
        }
        Tally(TopologyGate, SoundCondition ? 0.0 : 1.0, SoundCondition);
        bool VolumeCondition = true;
        if (!SheetCondition)
        {
            Row.Volume   = Volume;
            Row.Expected = Built.Expected;
            Row.Error    = std::fabs(Volume - Built.Expected);
            VolumeCondition = Row.Error <= 1.0e-9 * std::max(1.0, std::fabs(Built.Expected));
            Tally(VolumeGate, Row.Error / std::max(1.0, std::fabs(Built.Expected)), VolumeCondition);
        }
        bool MaximalCondition = true;
        if (Built.MaximalCondition)
        {
            MaximalCondition = Row.Redundant == 0;
            Tally(MaximalGate, Row.Redundant, MaximalCondition);
        }
        Row.Outcome         = SheetCondition ? "sheet" : "solid";
        Row.PassedCondition = VolumeCondition && SoundCondition && MaximalCondition && TessellationCondition;
        Rows.push_back(Row);
    }

    // ---- Report.
    const GateTally* Gates[] = { &RefusalGate, &VolumeGate, &TopologyGate, &MaximalGate, &TessellationGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    int Corpus = 0, LegacyRefused = 0, LegacyRefusedPassed = 0, Passed = 0;
    for (const ResultRow& Row : Rows)
    {
        Passed += Row.PassedCondition ? 1 : 0;
        if (Row.Entry.Section == "X") continue;
        ++Corpus;
        if (Row.Entry.Legacy.rfind("refused", 0) == 0) { ++LegacyRefused; LegacyRefusedPassed += Row.PassedCondition ? 1 : 0; }
    }
    std::printf("# Corpus validation (step 8 — kernel gaps G2–G5)\n\n");
    if (!OnlySection.empty()) std::printf("> 🔍 Filtered run: `--only %s`\n\n", OnlySection.c_str());
    std::printf("**%s** · %d / %zu rows pass · corpus result rows %d (sections A–H) · old-kernel refusals among them %d → now passing %d · refused now %ld · %.0f ms\n\n",
                PassedCondition ? "🟢 PASS" : "🔴 FAIL", Passed, Rows.size(), Corpus, LegacyRefused, LegacyRefusedPassed, RefusalGate.Failures, Elapsed(Start));
    std::printf("> 📝 One row per corpus command that produces a body (primitive arguments of booleans and face operations are built inside their row). "
                "Section E rows are the `CorpusE` face-operation specimens (bodies at the origin, same sizes and picks). Section X adds the G4 / G5 scenes the corpus could not express.\n\n");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Rows\n\n| | § | # | Corpus command | Old kernel | Remastered route | Result | F / E / V | R4 | V | V* (closed form) | \\|error\\| | Triangles | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const ResultRow& Row : Rows)
    {
        const std::string Line = Row.Entry.Line > 0 ? std::to_string(Row.Entry.Line) : "–";
        if (Row.Outcome != "solid" && Row.Outcome != "sheet")
        {
            std::printf("| 🔴 | %s | %s | `%s` | %s | %s | refused: %s | – | – | – | – | – | – | %.1f |\n", Row.Entry.Section.c_str(), Line.c_str(), Row.Entry.Command.c_str(), Row.Entry.Legacy.c_str(), Row.Route.c_str(), Row.Outcome.c_str(), Row.Milliseconds);
            continue;
        }
        if (Row.Outcome == "sheet")
        {
            std::printf("| %s | %s | %s | `%s` | %s | %s | sheet | %d / %d / %d | %d | – | – | – | %lld | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Entry.Section.c_str(), Line.c_str(), Row.Entry.Command.c_str(), Row.Entry.Legacy.c_str(),
                        Row.Route.c_str(), Row.Faces, Row.Edges, Row.Vertices, Row.Redundant, static_cast<long long>(Row.Triangles), Row.Milliseconds);
            continue;
        }
        std::printf("| %s | %s | %s | `%s` | %s | %s | solid | %d / %d / %d | %d | %.10f | %.10f | %.1e | %lld | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Entry.Section.c_str(), Line.c_str(), Row.Entry.Command.c_str(),
                    Row.Entry.Legacy.c_str(), Row.Route.c_str(), Row.Faces, Row.Edges, Row.Vertices, Row.Redundant, Row.Volume, Row.Expected, Row.Error, static_cast<long long>(Row.Triangles), Row.Milliseconds);
    }
    std::fflush(stdout);
    return PassedCondition ? 0 : 1;
}
