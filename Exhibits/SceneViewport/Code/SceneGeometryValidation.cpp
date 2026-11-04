/*==========================================================================================================================================
                                                        SCENEGEOMETRYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The scene geometry against the scene archive (no device): Project Everest's Geometry records bind every static
//    occupant of the proving ground once, reproduce the archive's placements exactly, and move, hide and re-lay them as edited;
//    then the editor's own objects: the baked engine shapes, the reserve that copies and constructed objects take, the
//    per-object shadow / indirect / dynamic switches, material blocks, removal and the distance field's scale bound.

#include "EverestSceneGeometry.h"

#include "Frontier/Engine/Geometry/ShapeArchive.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"
#include "Frontier/Engine/Scene/GeometryLayout.h"
#include "Frontier/Engine/Scene/SceneWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{

using Frontier::GlobalIllumination::OccupantPlacement;
using Frontier::GlobalIllumination::SceneStructure;

int Failures = 0;
int Checks   = 0;

void Report(bool Passed, const std::string& Line)
{
    ++Checks;
    Failures += Passed ? 0 : 1;
    std::printf("%s  %s\n", Passed ? "PASS" : "FAIL", Line.c_str());
}

Frontier::GeometryRecord* FindGeometry(Frontier::SceneWorld& World, const std::string& Name)
{
    for (const Frontier::SceneRecordBase* Entry : World.FindAllByType("Geometry"))
        if (Entry->QueryRecordName() == Name) return dynamic_cast<Frontier::GeometryRecord*>(World.FindRecord(Entry->QueryToken()));
    throw std::runtime_error("no Geometry record named " + Name);
}

double Translation(const Frontier::PlacementMatrix& Matrix, int Axis)
{
    return Matrix[static_cast<size_t>(12 + Axis)];
}

std::map<uint32_t, OccupantPlacement> Index(const std::vector<OccupantPlacement>& Placements)
{
    std::map<uint32_t, OccupantPlacement> Indexed;
    for (const OccupantPlacement& Placement : Placements) Indexed[Placement.Occupant] = Placement;
    return Indexed;
}

std::vector<uint32_t> Matches(const SceneStructure& Scene, const std::string& Stem)
{
    std::vector<uint32_t> Found;
    for (uint32_t Occupant = 0; Occupant < Scene.Occupants.size(); ++Occupant)
        if (Everest::MatchOccupantStem(Scene.Occupants[Occupant].Name, Stem)) Found.push_back(Occupant);
    return Found;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    try
    {
        if (ArgumentCount < 3)
            throw std::runtime_error("usage: SceneGeometryValidation <Projects/Everest> <EnvironmentArchives/ProvingGround> [scratch] [ShapeArchives]");
        const std::filesystem::path Project = Arguments[1];
        const std::filesystem::path Archive = Arguments[2];
        const std::filesystem::path Scratch = ArgumentCount > 3 ? std::filesystem::path(Arguments[3]) : std::filesystem::temp_directory_path();
        std::filesystem::create_directories(Scratch);

        // ① the level and the scene archive, as the editor loads them
        Frontier::SceneWorld World;
        std::string          Diagnostic;
        if (!World.LoadLevelSpace(Project / "Content" / "Levels" / "Default.levelspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
        SceneStructure Scene;
        if (!Scene.LoadFacetSpace(Archive / "ProvingGround.facetspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
        std::vector<uint8_t> Materials;
        if (std::ifstream Stream(Archive / "Materials.bin", std::ios::binary); Stream)
            Materials.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
        const size_t Records = World.FindAllByType("Geometry").size();
        Report(Records == 37, "the level holds " + std::to_string(Records) + " Geometry records (37)");

        // ② binding: every record binds, and the static occupants are bound once each
        Everest::SceneGeometryOccupancy Occupancy;
        Occupancy.Reset(&Scene, Materials);
        const std::vector<OccupantPlacement> Initial = Occupancy.ResolvePlacements(World);
        const auto                           Resolutions = Occupancy.QueryResolutions();
        size_t Bound = 0;
        for (const auto& [Token, Resolution] : *Resolutions)
        {
            Bound += Resolution.Bound ? 1 : 0;
            if (!Resolution.Bound) std::printf("      not bound: %s\n", Resolution.Diagnostic.c_str());
        }
        Report(Bound == Records, std::to_string(Bound) + " of " + std::to_string(Records) + " records bind to the archive");
        std::set<uint32_t> Covered;
        size_t             Duplicates = 0;
        for (const OccupantPlacement& Placement : Initial) Duplicates += Covered.insert(Placement.Occupant).second ? 0 : 1;
        size_t Outside = 0, Unexplained = 0;
        for (uint32_t Occupant = 0; Occupant < Scene.Occupants.size(); ++Occupant)
        {
            if (Covered.count(Occupant)) continue;
            const std::string& Name = Scene.Occupants[Occupant].Name;
            const bool Vehicle  = static_cast<int32_t>(Occupant) == Scene.VehicleBody || Occupant < 5;
            const bool Fragment = Name.find(" fragment ") != std::string::npos;
            const bool Stress   = Name.rfind("Stress ", 0) == 0;
            const bool Ground   = Name.rfind("Ground ", 0) == 0;
            ++Outside;
            if (!Vehicle && !Fragment && !Stress && !Ground)
            {
                ++Unexplained;
                std::printf("      unbound: %s\n", Name.c_str());
            }
        }
        Report(Duplicates == 0 && Covered.size() == 504 && Unexplained == 0,
               std::to_string(Covered.size()) + " occupants bound once each (504); the other " + std::to_string(Outside) +
                   " are the vehicle, fragment pools, stress pool and ground tiles");

        // ③ the default level shows the archive exactly
        size_t Exact = 0, Active = 0;
        for (const OccupantPlacement& Placement : Initial)
        {
            Exact += Placement.Current == Scene.Occupants[Placement.Occupant].Current ? 1 : 0;
            Active += Placement.Active ? 1 : 0;
        }
        Report(Exact == Initial.size() && Active == Initial.size(),
               std::to_string(Exact) + " of " + std::to_string(Initial.size()) + " placements are the archive's transforms bit for bit, all active");
        Report(Occupancy.ResolvePlacements(World).empty(), "an unchanged level resolves no placements again");

        // ④ a smaller array hides the copies past its count
        auto& North = FindGeometry(World, "Boundary north")->AccessConfiguration();
        North.Modifiers.front().Count = 20;
        North.AdvanceRevision();
        {
            const auto Placed = Index(Occupancy.ResolvePlacements(World));
            const auto Copies = Matches(Scene, "Boundary North");
            size_t     Hidden = 0, Kept = 0;
            for (size_t Copy = 0; Copy < Copies.size(); ++Copy)
            {
                const OccupantPlacement& Placement = Placed.at(Copies[Copy]);
                if (Copy < 20) Kept += Placement.Active && Placement.Current == Scene.Occupants[Copies[Copy]].Current ? 1 : 0;
                else Hidden += Placement.Active ? 0 : 1;
            }
            Report(Kept == 20 && Hidden == 34, "Boundary north at count 20: 20 copies in place, " + std::to_string(Hidden) + " of 34 hidden");
        }

        // ⑤ a wider step re-lays every copy along the object's X
        North.Modifiers.front().Count             = 54;
        North.Modifiers.front().ConstantMetres[0] = 5.0f;
        North.AdvanceRevision();
        {
            const auto Placed = Index(Occupancy.ResolvePlacements(World));
            const auto Copies = Matches(Scene, "Boundary North");
            double     Worst  = 0.0;
            for (size_t Copy = 0; Copy < Copies.size(); ++Copy)
            {
                const auto& Placement = Placed.at(Copies[Copy]).Current;
                Worst = std::max({Worst, std::fabs(Translation(Placement, 0) - (-58.0 + 5.0 * static_cast<double>(Copy))),
                                  std::fabs(Translation(Placement, 1) - 58.5), std::fabs(Translation(Placement, 2))});
            }
            char Line[160];
            std::snprintf(Line, sizeof(Line), "Boundary north at a 5 m step: copy n at x = -58 + 5n (worst error %.2e m)", Worst);
            Report(Worst < 1.0e-4, Line);
        }

        // ⑥ moving an object moves all its parts rigidly
        auto& Kerbs = FindGeometry(World, "Kerbs")->AccessConfiguration();
        Kerbs.Spatial.PositionMetres[1] += 2.0f;
        Kerbs.AdvanceRevision();
        {
            const auto Placed = Occupancy.ResolvePlacements(World);
            double     Worst  = 0.0;
            for (const OccupantPlacement& Placement : Placed)
            {
                const auto& Before = Scene.Occupants[Placement.Occupant].Current;
                for (int Element = 0; Element < 12; ++Element) Worst = std::max(Worst, std::fabs(Placement.Current[static_cast<size_t>(Element)] - Before[static_cast<size_t>(Element)]));
                Worst = std::max({Worst, std::fabs(Translation(Placement.Current, 0) - Before[12]), std::fabs(Translation(Placement.Current, 1) - Before[13] - 2.0),
                                  std::fabs(Translation(Placement.Current, 2) - Before[14])});
            }
            char Line[160];
            std::snprintf(Line, sizeof(Line), "Kerbs moved 2 m in Y: %zu occupants (both strips) moved exactly (worst %.2e)", Placed.size(), Worst);
            Report(Placed.size() == 36 && Worst < 1.0e-5, Line);
        }

        // ⑦ a radial array turns about the ring's centre
        auto& Ring = FindGeometry(World, "Skidpad ring")->AccessConfiguration();
        Ring.Spatial.OrientationDegrees[2] = 10.0f;
        Ring.AdvanceRevision();
        {
            const auto     Placed = Occupancy.ResolvePlacements(World);
            double         Worst  = 0.0;
            const double   θ      = 10.0 * 3.14159265358979323846 / 180.0;
            for (const OccupantPlacement& Placement : Placed)
            {
                const auto&  Before = Scene.Occupants[Placement.Occupant].Current;
                const double X = Before[12] + 30.0, Y = Before[13] - 30.0;
                Worst = std::max({Worst, std::fabs(Translation(Placement.Current, 0) - (-30.0 + X * std::cos(θ) - Y * std::sin(θ))),
                                  std::fabs(Translation(Placement.Current, 1) - (30.0 + X * std::sin(θ) + Y * std::cos(θ)))});
            }
            char Line[160];
            std::snprintf(Line, sizeof(Line), "Skidpad ring turned 10\xC2\xB0: %zu cones turn about (-30, 30) (worst %.2e m)", Placed.size(), Worst);
            Report(Placed.size() == 24 && Worst < 1.0e-4, Line);
        }

        // ⑧ hiding an object deactivates every part of every copy
        auto& Lamps = FindGeometry(World, "Street lamps")->AccessConfiguration();
        Lamps.Visible = false;
        Lamps.AdvanceRevision();
        {
            const auto Placed   = Occupancy.ResolvePlacements(World);
            size_t     Inactive = 0;
            for (const OccupantPlacement& Placement : Placed) Inactive += Placement.Active ? 0 : 1;
            Report(Placed.size() == 40 && Inactive == 40, "Street lamps hidden: " + std::to_string(Inactive) + " of 40 poles and heads inactive");
        }

        // ⑨ an Array on a single object makes copies the archive does not hold: only the first is shown
        auto& Tower = FindGeometry(World, "Control tower")->AccessConfiguration();
        Tower.Modifiers.push_back(Frontier::ArrayModifierConfiguration{});
        Tower.AdvanceRevision();
        (void)Occupancy.ResolvePlacements(World);
        {
            const auto Resolved = Occupancy.QueryResolutions();
            const auto Entry    = Resolved->find(FindGeometry(World, "Control tower")->QueryToken().Value);
            Report(Entry != Resolved->end() && Entry->second.Copies == 2 && Entry->second.Shown == 1 && Entry->second.Parts == 3,
                   "Control tower with an Array of 2: 2 copies made, 1 shown (the archive holds one tower of 3 parts)");
        }

        // ⑩ the record survives the level file
        const std::filesystem::path Saved = Scratch / "SceneGeometryValidation.levelspace";
        if (!World.SaveLevelSpace(Saved, &Diagnostic)) throw std::runtime_error(Diagnostic);
        Frontier::SceneWorld Reloaded;
        if (!Reloaded.LoadLevelSpace(Saved, &Diagnostic)) throw std::runtime_error(Diagnostic);
        size_t Same = 0;
        for (const Frontier::SceneRecordBase* Entry : World.FindAllByType("Geometry"))
        {
            const auto& Before = dynamic_cast<const Frontier::GeometryRecord*>(Entry)->QueryConfiguration();
            const auto& After  = FindGeometry(Reloaded, Entry->QueryRecordName())->QueryConfiguration();
            bool Equal = Before.OccupantStems == After.OccupantStems && Before.OccupantFirst == After.OccupantFirst && Before.OccupantCount == After.OccupantCount &&
                         Before.BaseCopies == After.BaseCopies && Before.Visible == After.Visible && Before.Modifiers.size() == After.Modifiers.size() &&
                         Before.GroupName == After.GroupName && Before.Fracture.Enabled == After.Fracture.Enabled;
            for (int Axis = 0; Axis < 3; ++Axis)
                Equal = Equal && std::fabs(Before.Spatial.PositionMetres[Axis] - After.Spatial.PositionMetres[Axis]) < 1.0e-4f &&
                        std::fabs(Before.ArchiveOriginMetres[Axis] - After.ArchiveOriginMetres[Axis]) < 1.0e-4f;
            for (size_t Modifier = 0; Equal && Modifier < Before.Modifiers.size(); ++Modifier)
                Equal = Before.Modifiers[Modifier].Count == After.Modifiers[Modifier].Count &&
                        std::fabs(Before.Modifiers[Modifier].ConstantMetres[0] - After.Modifiers[Modifier].ConstantMetres[0]) < 1.0e-4f &&
                        Before.Modifiers[Modifier].RadialPivot == After.Modifiers[Modifier].RadialPivot;
            Same += Equal ? 1 : 0;
        }
        Report(Same == Records, std::to_string(Same) + " of " + std::to_string(Records) + " records read back from the saved level as written");

        // ⑪ the placement algebra
        {
            const Frontier::PlacementMatrix Matrix = Frontier::ConstructPlacement({3.0, -2.0, 1.5}, {12.0, -34.0, 145.0}, {1.5, 0.5, 2.0});
            std::array<double, 3> Position{}, Rotation{}, Scale{};
            Frontier::DecomposePlacement(Matrix, Position, Rotation, Scale);
            const Frontier::PlacementMatrix Rebuilt  = Frontier::ConstructPlacement(Position, Rotation, Scale);
            const Frontier::PlacementMatrix Identity = Frontier::MultiplyPlacement(Frontier::InvertPlacement(Matrix), Matrix);
            Report(Frontier::ConfirmProximity(Matrix, Rebuilt, 1.0e-9, 1.0e-9) &&
                       Frontier::ConfirmProximity(Identity, Frontier::ConstructIdentityPlacement(), 1.0e-9, 1.0e-9),
                   "placements decompose into position, XYZ Euler and scale and invert exactly");
        }
        // ⑫ the engine shapes: six archives in EngineContent/ShapeArchives, each with its baked distance volume
        const std::filesystem::path ShapeFolder = ArgumentCount > 4 ? std::filesystem::path(Arguments[4]) : Archive.parent_path().parent_path() / "ShapeArchives";
        std::vector<std::string>             ShapeDiagnostics;
        const std::vector<Frontier::ShapeArchive> Shapes = Frontier::ReadShapeArchives(ShapeFolder, &ShapeDiagnostics);
        std::string ShapeNames;
        size_t      Baked = 0;
        for (const Frontier::ShapeArchive& Shape : Shapes)
        {
            Baked += Shape.Distance ? 1 : 0;
            ShapeNames += (ShapeNames.empty() ? "" : ", ") + Shape.Name;
            if (!Shape.Distance) std::printf("      %s: %s\n", Shape.Name.c_str(), Shape.Diagnostic.c_str());
        }
        Report(Shapes.size() == 6 && Baked == 6, std::to_string(Shapes.size()) + " engine shapes (" + ShapeNames + "), " + std::to_string(Baked) +
                                                     " with their baked distance volumes");
        const auto ShapeIndex = [&](const std::string& Name) {
            for (size_t Index = 0; Index < Shapes.size(); ++Index)
                if (Shapes[Index].Name == Name) return static_cast<uint32_t>(Index);
            throw std::runtime_error("no engine shape " + Name);
        };

        // ⑬ the reserve: the scene as the viewport loads it (shapes appended, the stress pool's first 512 occupants reserved)
        Frontier::SceneWorld Editor;
        if (!Editor.LoadLevelSpace(Project / "Content" / "Levels" / "Default.levelspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
        SceneStructure Reserved;
        if (!Reserved.LoadFacetSpace(Archive / "ProvingGround.facetspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
        Reserved.AppendShapeSets(Shapes, static_cast<uint32_t>(Materials.size() / 48));
        Reserved.ConvertStressReserve(512);
        Everest::SceneGeometryOccupancy Objects;
        Objects.Reset(&Reserved, Materials, Shapes);
        {
            const auto Start = Objects.ResolvePlacements(Editor);
            size_t     Same  = 0, FromReserve = 0;
            for (const OccupantPlacement& Placement : Start)
            {
                Same += Placement.Active && Placement.Current == Reserved.Occupants[Placement.Occupant].Current ? 1 : 0;
                FromReserve += Reserved.QueryReserved(Placement.Occupant) ? 1 : 0;
            }
            char Line[220];
            std::snprintf(Line, sizeof(Line), "a reserve of %u occupants (#%u-#%u, below the distance field's identity ceiling 2044): the default level "
                                              "still resolves %zu placements exactly, none from the reserve",
                          Reserved.ReserveCount, Reserved.ReserveFirst, Reserved.ReserveFirst + Reserved.ReserveCount - 1, Start.size());
            Report(Reserved.ReserveCount == 512 && Objects.QueryReserveFree() == 512 && Reserved.ReserveFirst + Reserved.ReserveCount <= 2044 &&
                       Start.size() == 504 && Same == Start.size() && FromReserve == 0,
                   Line);
        }

        // ⑭ an array past the archive's copies: the reserve lays out the rest at the same step, with the object's shape
        auto&                     NorthEdit  = FindGeometry(Editor, "Boundary north")->AccessConfiguration();
        const Frontier::RecordToken NorthToken = FindGeometry(Editor, "Boundary north")->QueryToken();
        NorthEdit.Modifiers.front().Count = 60;
        NorthEdit.AdvanceRevision();
        {
            const auto                     Placed  = Objects.ResolvePlacements(Editor);
            const auto                     Archived = Matches(Reserved, "Boundary North");
            std::vector<OccupantPlacement> Extra;
            for (const OccupantPlacement& Placement : Placed)
                if (Reserved.QueryReserved(Placement.Occupant) && Placement.Active) Extra.push_back(Placement);
            std::sort(Extra.begin(), Extra.end(), [](const OccupantPlacement& Left, const OccupantPlacement& Right) { return Left.Current[12] < Right.Current[12]; });
            const auto& First  = Reserved.Occupants[Archived[0]].Current;
            const auto& Second = Reserved.Occupants[Archived[1]].Current;
            double      Worst  = Extra.size() == 6 ? 0.0 : 1.0;
            bool        Shaped = Extra.size() == 6;
            for (size_t Copy = 0; Copy < Extra.size(); ++Copy)
            {
                const double Step = static_cast<double>(54 + Copy);
                for (int Axis = 0; Axis < 3; ++Axis)
                    Worst = std::max(Worst, std::fabs(Extra[Copy].Current[static_cast<size_t>(12 + Axis)] -
                                                      (First[static_cast<size_t>(12 + Axis)] + Step * (Second[static_cast<size_t>(12 + Axis)] - First[static_cast<size_t>(12 + Axis)]))));
                Shaped = Shaped && Extra[Copy].Template && Extra[Copy].Template->FacetSet == Reserved.Occupants[Archived[0]].FacetSet;
            }
            const auto& Resolution = Objects.QueryResolutions()->at(NorthToken.Value);
            char        Line[220];
            std::snprintf(Line, sizeof(Line), "Boundary north at count 60: %u copies shown, %u from the reserve at the archive's step (worst %.2e m), each with "
                                              "the boundary's shape",
                          Resolution.Shown, Resolution.Reserved, Worst);
            Report(Resolution.Shown == 60 && Resolution.Reserved == 6 && Worst < 1.0e-4 && Shaped, Line);
        }

        // ⑮ an engine shape: a record that names the Cube takes one reserve occupant with the shape's facet set
        const Frontier::RecordToken CubeToken = Editor.ConstructRecord("Geometry", "Cube");
        auto& CubeEdit = dynamic_cast<Frontier::GeometryRecord*>(Editor.FindRecord(CubeToken))->AccessConfiguration();
        CubeEdit.OccupantStems.clear();
        CubeEdit.ClaimsArchive = false;
        CubeEdit.ShapeName     = "Cube";
        const float CubeAt[3] = {-44.0f, -21.0f, 1.5f};
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            CubeEdit.Spatial.PositionMetres[Axis] = CubeAt[Axis];
            CubeEdit.Spatial.Scale[Axis]          = 3.0f;
        }
        CubeEdit.Spatial.OrientationDegrees[2] = 20.0f;
        CubeEdit.AdvanceRevision();
        uint32_t CubeOccupant = 0;
        {
            const auto Placed   = Objects.ResolvePlacements(Editor);
            const auto Expected = Frontier::ConstructPlacement({-44.0, -21.0, 1.5}, {0.0, 0.0, 20.0}, {3.0, 3.0, 3.0});
            bool       Found    = false;
            // 📝 A reserve occupant already showing the shape (the reserve lends each shape one at bring-up) needs no template.
            for (const OccupantPlacement& Placement : Placed)
                if ((Placement.Template ? Placement.Template->FacetSet : Reserved.Occupants[Placement.Occupant].FacetSet) ==
                    Reserved.ShapeSetFirst + ShapeIndex("Cube"))
                {
                    Found        = Reserved.QueryReserved(Placement.Occupant) && Placement.Active && Frontier::ConfirmProximity(Placement.Current, Expected, 1.0e-5, 1.0e-5);
                    CubeOccupant = Placement.Occupant;
                }
            const auto& Resolution = Objects.QueryResolutions()->at(CubeToken.Value);
            Report(Found && Resolution.Bound && Resolution.Shape && Resolution.Shown == 1 && Resolution.Facets == Shapes[ShapeIndex("Cube")].Facets.size(),
                   "Construct > Cube at (-44, -21), 3 m, turned 20\xC2\xB0: reserve occupant #" + std::to_string(CubeOccupant) + " with the Cube's facet set (" +
                       std::to_string(Resolution.Facets) + " triangles) at exactly that placement");
        }

        // ⑯ a duplicate: the control tower's three parts again from the reserve, 16 m along X
        const Frontier::RecordToken CopyToken = Editor.ConstructRecord("Geometry", "Control tower copy");
        {
            auto& CopyEdit = dynamic_cast<Frontier::GeometryRecord*>(Editor.FindRecord(CopyToken))->AccessConfiguration();
            CopyEdit       = FindGeometry(Editor, "Control tower")->QueryConfiguration();
            CopyEdit.ClaimsArchive = false;
            CopyEdit.Spatial.PositionMetres[0] += 16.0f;
            CopyEdit.AdvanceRevision();
            const auto Placed = Objects.ResolvePlacements(Editor);
            std::set<uint32_t> TowerSets;
            for (const char* Stem : {"Control tower", "Control tower cabin", "Control tower roof"})
                for (const uint32_t Occupant : Matches(Reserved, Stem)) TowerSets.insert(Reserved.Occupants[Occupant].FacetSet);
            size_t Parts = 0;
            double Worst = 0.0;
            for (const OccupantPlacement& Placement : Placed)
            {
                if (!Reserved.QueryReserved(Placement.Occupant) || !Placement.Template || Placement.Template->FacetSet == Reserved.ShapeSetFirst + ShapeIndex("Cube"))
                    continue;
                if (!TowerSets.count(Placement.Template->FacetSet)) continue;
                ++Parts;
                const auto& Source = Placement.Template->Current;
                Worst = std::max({Worst, std::fabs(Placement.Current[12] - Source[12] - 16.0), std::fabs(Placement.Current[13] - Source[13]),
                                  std::fabs(Placement.Current[14] - Source[14])});
            }
            char Line[200];
            std::snprintf(Line, sizeof(Line), "Duplicate of the control tower: %zu parts from the reserve, each the original's placement moved 16 m in X (worst %.2e m)",
                          Parts, Worst);
            Report(Parts == 3 && Worst < 1.0e-4, Line);
        }

        // ⑰ the switches: Cast shadows off, GI off and Dynamic on reach every part's participation, and come back
        auto& TowerEdit = FindGeometry(Editor, "Control tower")->AccessConfiguration();
        {
            namespace Bits = Frontier::GlobalIllumination::OccupantParticipation;
            TowerEdit.CastShadows        = false;
            TowerEdit.GlobalIllumination = false;
            TowerEdit.Dynamic            = true;
            TowerEdit.AdvanceRevision();
            size_t Set = 0, Cleared = 0;
            for (const OccupantPlacement& Placement : Objects.ResolvePlacements(Editor))
                Set += Placement.Participation && (*Placement.Participation & (Bits::ShadowExempt | Bits::IndirectExempt | Bits::Dynamic)) ==
                                                      (Bits::ShadowExempt | Bits::IndirectExempt | Bits::Dynamic)
                           ? 1
                           : 0;
            TowerEdit.CastShadows        = true;
            TowerEdit.GlobalIllumination = true;
            TowerEdit.Dynamic            = false;
            TowerEdit.AdvanceRevision();
            for (const OccupantPlacement& Placement : Objects.ResolvePlacements(Editor))
                Cleared += Placement.Participation && (*Placement.Participation & (Bits::ShadowExempt | Bits::IndirectExempt | Bits::Dynamic)) == 0 ? 1 : 0;
            Report(Set == 3 && Cleared == 3, "Control tower with Cast shadows and GI off and Dynamic on: " + std::to_string(Set) +
                                                 " of 3 parts carry ShadowExempt | IndirectExempt | Dynamic, and " + std::to_string(Cleared) +
                                                 " lose them again");
        }

        // ⑱ a surface of its own: the cube's first family copied into a material block, its cards captured again
        {
            const auto& Resolution = Objects.QueryResolutions()->at(CubeToken.Value);
            Frontier::SurfaceSlotConfiguration Slot;
            Slot.Family   = Resolution.Families.front();
            Slot.Channels = Resolution.FamilyChannels.front();
            Slot.Channels[0] = 0.62f;
            Slot.Channels[1] = 0.05f;
            Slot.Channels[2] = 0.04f;
            CubeEdit.SurfaceSlots.push_back(Slot);
            CubeEdit.AdvanceRevision();
            const auto Placed = Objects.ResolvePlacements(Editor);
            const auto Blocks = Objects.ConsumeMaterialBlocks();
            bool       Overridden = false;
            for (const OccupantPlacement& Placement : Placed)
                if (Placement.Occupant == CubeOccupant)
                    Overridden = Placement.MaterialOverride && (static_cast<uint32_t>(*Placement.MaterialOverride) & Frontier::GlobalIllumination::MaterialBlockBit) != 0 &&
                                 Placement.Recapture;
            float Red = -1.0f;
            if (Blocks.size() == 1 && Blocks.front().second.size() >= (Slot.Family + 1) * 48u)
                std::memcpy(&Red, Blocks.front().second.data() + static_cast<size_t>(Slot.Family) * 48u, sizeof(float));
            const auto& After = Objects.QueryResolutions()->at(CubeToken.Value);
            Report(Blocks.size() == 1 && std::fabs(Red - 0.62f) < 1.0e-6f && Overridden && After.OwnSurfaces,
                   "the cube's own surface: 1 material block written (family #" + std::to_string(Slot.Family) + " albedo red 0.62), the cube's override names "
                   "the block and its cards are captured again");
            // 📝 The override reaches the shaders through the occupant profile (OccupantProfile.MaterialOverride, word 71) unchanged.
            uint32_t Stored = 0, Expected = 0;
            for (const OccupantPlacement& Placement : Placed)
                if (Placement.Occupant == CubeOccupant && Placement.MaterialOverride)
                {
                    Reserved.Occupants[CubeOccupant].MaterialOverride = *Placement.MaterialOverride;
                    Expected = static_cast<uint32_t>(*Placement.MaterialOverride);
                }
            const std::vector<uint8_t> Profile = Reserved.EncodeOccupantRange(nullptr, CubeOccupant, 1);
            std::memcpy(&Stored, Profile.data() + 71 * 4, sizeof(Stored));
            char Line[160];
            std::snprintf(Line, sizeof(Line), "the cube's occupant profile carries the block override 0x%08X (expected 0x%08X)", Stored, Expected);
            Report(Expected != 0 && Stored == Expected, Line);
        }

        // ⑲ removing an archive object: the jump ramp's occupant is hidden while the level holds Geometry records
        {
            const auto Ramp = Matches(Reserved, "Jump ramp");
            Report(Editor.DestroyRecord(FindGeometry(Editor, "Jump ramp")->QueryToken()), "the jump ramp record is removed");
            const auto Placed = Index(Objects.ResolvePlacements(Editor));
            size_t     Hidden = 0;
            for (const uint32_t Occupant : Ramp) Hidden += Placed.count(Occupant) && !Placed.at(Occupant).Active ? 1 : 0;
            Report(!Ramp.empty() && Hidden == Ramp.size(), std::to_string(Hidden) + " of " + std::to_string(Ramp.size()) + " jump ramp occupants hidden after its removal");
        }

        // ⑳ exhaustion: more copies than the reserve holds are reported, and the reserve comes back when they go
        {
            NorthEdit.Modifiers.front().Count = 654;
            NorthEdit.AdvanceRevision();
            (void)Objects.ResolvePlacements(Editor);
            const auto Exhausted = Objects.QueryResolutions()->at(NorthToken.Value);
            const uint32_t FreeWhenExhausted = Objects.QueryReserveFree();
            NorthEdit.Modifiers.front().Count = 54;
            NorthEdit.AdvanceRevision();
            (void)Objects.ResolvePlacements(Editor);
            const uint32_t FreeAfter = Objects.QueryReserveFree();
            char Line[220];
            std::snprintf(Line, sizeof(Line), "Boundary north at count 654: %u of %u copies shown with the reserve exhausted (%u free); back at 54 the reserve "
                                              "has %u free (the cube and the tower copy hold 4)",
                          Exhausted.Shown, Exhausted.Copies, FreeWhenExhausted, FreeAfter);
            Report(Exhausted.Copies == 654 && Exhausted.Shown == 54 + 508 && FreeWhenExhausted == 0 && FreeAfter == 508, Line);
        }

        // ㉑ the distance field's scale bound (DistanceUnion.slang ResolveDistanceScale): exact for a uniform scale, never above the
        //    smallest singular value otherwise
        {
            const auto DistanceScale = [](const Frontier::PlacementMatrix& Placement) {
                const Frontier::PlacementMatrix Inverse = Frontier::InvertPlacement(Placement);
                double                          Bound   = 0.0;
                for (int Row = 0; Row < 3; ++Row)
                {
                    double Sum = 0.0;
                    for (int Column = 0; Column < 3; ++Column)
                    {
                        double Gram = 0.0;
                        for (int Inner = 0; Inner < 3; ++Inner) Gram += Inverse[static_cast<size_t>(Row * 4 + Inner)] * Inverse[static_cast<size_t>(Column * 4 + Inner)];
                        Sum += std::fabs(Gram);
                    }
                    Bound = std::max(Bound, Sum);
                }
                return 1.0 / std::sqrt(std::max(Bound, 1.0e-20));
            };
            const double Uniform   = DistanceScale(Frontier::ConstructPlacement({1.0, 2.0, 3.0}, {10.0, 20.0, 30.0}, {2.0, 2.0, 2.0}));
            const double Stretched = DistanceScale(Frontier::ConstructPlacement({1.0, 2.0, 3.0}, {10.0, 20.0, 30.0}, {3.0, 1.0, 0.5}));
            const double Rigid     = DistanceScale(Frontier::ConstructPlacement({-5.0, 4.0, 0.0}, {0.0, 0.0, 73.0}, {1.0, 1.0, 1.0}));
            char         Line[200];
            std::snprintf(Line, sizeof(Line), "distance scale: %.12f for a uniform 2 (exact 2), %.4f for (3, 1, 0.5) (never above 0.5), %.12f for a rigid placement",
                          Uniform, Stretched, Rigid);
            Report(std::fabs(Uniform - 2.0) < 1.0e-9 && Stretched <= 0.5 + 1.0e-12 && Stretched > 0.05 && std::fabs(Rigid - 1.0) < 1.0e-9, Line);
        }

        // ㉒ GPU picking's records: every shown part names its record (the ground names the Proving ground), the vehicle and removed
        //    objects name none, and a pick takes the record drawn at the picked pixel, else the nearest drawn within its reach
        {
            using Frontier::GlobalIllumination::OccupantPick;
            const auto North      = Matches(Reserved, "Boundary North");
            const auto Ramp       = Matches(Reserved, "Jump ramp");
            const auto Ground     = static_cast<uint32_t>(Reserved.GroundOccupant);
            const auto GroundOwner = Objects.QueryOwner(Ground);
            size_t     NorthOwned = 0;
            for (const uint32_t Occupant : North) NorthOwned += Objects.QueryOwner(Occupant) == NorthToken.Value ? 1 : 0;
            bool Unowned = !Objects.QueryOwner(0u);
            for (const uint32_t Occupant : Ramp) Unowned = Unowned && !Objects.QueryOwner(Occupant);
            Report(NorthOwned == North.size() && North.size() == 54 && Objects.QueryOwner(CubeOccupant) == CubeToken.Value &&
                       GroundOwner == FindGeometry(Editor, "Proving ground")->QueryToken().Value && Unowned,
                   "pick owners: " + std::to_string(NorthOwned) + " of " + std::to_string(North.size()) + " Boundary north parts, the cube's reserve " +
                       "occupant and the ground (the Proving ground) name their records; the vehicle and the removed jump ramp name none");
            const auto Synthetic = [](std::initializer_list<std::tuple<int32_t, int32_t, uint32_t>> Texels) {
                OccupantPick Pick;
                Pick.Reach = 4;
                Pick.Occupants.assign(81, OccupantPick::NoOccupant);
                for (const auto& [OffsetX, OffsetY, Occupant] : Texels) Pick.Occupants[static_cast<size_t>((4 + OffsetY) * 9 + 4 + OffsetX)] = Occupant;
                return Pick;
            };
            // 📝 Occupant 0 is the vehicle, which no record owns: a click on it, or on the sky, looks around for the nearest record.
            const auto Drawn   = Objects.ResolvePickedRecord(Synthetic({{0, 0, CubeOccupant}, {1, 0, North[0]}, {-1, 0, Ground}}));
            const auto Surface = Objects.ResolvePickedRecord(Synthetic({{0, 0, Ground}, {1, 0, CubeOccupant}}));
            const auto Near    = Objects.ResolvePickedRecord(Synthetic({{0, 0, 0u}, {3, 0, North[0]}, {1, 2, CubeOccupant}}));
            const auto Beyond  = Objects.ResolvePickedRecord(Synthetic({{0, 0, 0u}, {4, 1, CubeOccupant}}));
            const auto Sky     = Objects.ResolvePickedRecord(Synthetic({}));
            Report(Drawn == CubeToken.Value && Surface == GroundOwner && Near == CubeToken.Value && !Beyond && !Sky,
                   "pick resolution: the record drawn at the pixel wins over its neighbours (the cube over the boundary, the ground over the "
                   "cube beside it); off any record the nearest within 4 px (1, 2 before 3, 0); none at (4, 1), outside the reach, or on the sky");
        }

        std::printf("SceneGeometryValidation: %d of %d checks passed\n", Checks - Failures, Checks);
        return Failures == 0 ? 0 : 1;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "SceneGeometryValidation FAILED: %s\n", Failure.what());
        return 1;
    }
}
