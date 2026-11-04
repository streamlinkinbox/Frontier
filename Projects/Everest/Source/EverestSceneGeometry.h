/*==========================================================================================================================================
                                                          EVERESTSCENEGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Project Everest's scene geometry: binds the level's Geometry records to the proving ground's occupants by name, resolves
//    each record's transform and modifier stack into occupant placements for the viewport, and reports what each resolved to.

#pragma once

#include "Frontier/Engine/Rendering/GlobalIllumination/OccupantRelocation.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/PickProjection.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"
#include "Frontier/Engine/Scene/GeometryLayout.h"
#include "Frontier/Engine/Scene/SceneWorld.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Everest
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

using GeometryResolutions = std::unordered_map<uint64_t, Frontier::GeometryConfiguration::Resolution>;

// 📝 Tolerances under which a resolved copy is the archive's own placement (the record still shows the archive exactly).
constexpr double GeometryLinearTolerance = 2.0e-4;   // [-] rotation-scale elements
constexpr double GeometryMetreTolerance  = 2.0e-3;   // [m]

//------------------------------------------------------------------------------------------------------------------------
//                                                    NAMES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The demo's material families (MaterialSpecification.js CourseMaterial), index-aligned; "Family N" past the table.
[[nodiscard]] std::string ResolveFamilyName(uint32_t Family);

// 📝 An occupant name matches a stem alone, the stem and digits ("Tunnel lamp N" + "0"), or the stem, one space and one token
//    that carries a digit or is one capital letter ("Boundary North -58", "Concrete pillar A"; not "Control tower cabin").
[[nodiscard]] bool MatchOccupantStem(std::string_view Name, std::string_view Stem) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE GEOMETRY OCCUPANCY
//------------------------------------------------------------------------------------------------------------------------

class SceneGeometryOccupancy
{
  public:
    // 📝 Takes the loaded scene (its occupants as loaded are the archive's), the material records and the engine shapes it was built
    //    with; the next ResolvePlacements binds every record and places all of their occupants. A null scene unbinds everything.
    void Reset(const Frontier::GlobalIllumination::SceneStructure* Scene, std::vector<uint8_t> MaterialProfiles,
               std::vector<Frontier::ShapeArchive> Shapes = {});

    // 📝 The placements of every Geometry record whose configuration changed since the last call (all of them after Reset): its
    //    archive copies, its copies beyond them from the reserve, and the archive occupants nobody claims any more (hidden while the
    //    level holds Geometry records, back at the archive's placements once it holds none).
    [[nodiscard]] std::vector<Frontier::GlobalIllumination::OccupantPlacement> ResolvePlacements(const Frontier::SceneWorld& World);

    // 📝 The material blocks (block, MaterialFamilyCount records) changed since the last call; the viewport writes them before it
    //    applies the placements, whose cards are captured again from them.
    [[nodiscard]] std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ConsumeMaterialBlocks();

    // 📝 What every record resolved to, replaced whenever any record changed (QueryGeneration advances with it).
    [[nodiscard]] std::shared_ptr<const GeometryResolutions> QueryResolutions() const noexcept { return Snapshot_; }
    [[nodiscard]] uint64_t                                   QueryGeneration() const noexcept { return Generation_; }

    // 📝 The archive transform of an occupant as the scene loaded it.
    [[nodiscard]] const Frontier::PlacementMatrix* QueryArchiveTransform(uint32_t Occupant) const noexcept;
    [[nodiscard]] const std::vector<Frontier::ShapeArchive>& QueryShapes() const noexcept { return Shapes_; }
    [[nodiscard]] uint32_t QueryReserveFree() const noexcept { return static_cast<uint32_t>(ReserveFree_.size()); }

    // 📝 GPU picking: the record that shows an occupant (every part and copy of every record; the ground is the Proving ground's),
    //    replaced with the resolutions; empty for the vehicle and occupants no record holds.
    [[nodiscard]] std::optional<uint64_t> QueryOwner(uint32_t Occupant) const;
    // 📝 The record a pick selects: the one drawn at the picked pixel, else the nearest one drawn within the pick's reach (thin parts
    //    stay easy to hit); empty where only the sky or occupants no record holds are near the pointer.
    [[nodiscard]] std::optional<uint64_t> ResolvePickedRecord(const Frontier::GlobalIllumination::OccupantPick& Pick) const;

  private:
    struct ShownOccupant
    {
        uint32_t                Occupant = 0;
        uint32_t                FacetSet = 0;
        Frontier::PlacementMatrix Placement{};
    };
    struct Occupancy
    {
        std::string                                         Signature{};   // [-] stems, range, shape and claim it was bound with
        uint64_t                                            Revision = ~0ull;
        std::vector<Frontier::GlobalIllumination::OccupantEntry> Templates{};   // [part] each part's shape and surface
        std::vector<std::vector<uint32_t>>                  Claimed{};     // [part][copy] archive occupants the record claims
        std::vector<Frontier::PlacementMatrix>              Anchors{};     // [base] part 0's archive transform (identity: a shape)
        std::vector<std::vector<Frontier::PlacementMatrix>> PartOffsets{}; // [part][base] a part relative to its copy's anchor
        std::vector<std::vector<uint32_t>>                  Reserve{};     // [part][copy beyond the archive's] reserve occupants held
        std::array<double, 3>                               Extent{};      // [m] part 0's local bounds
        int32_t                                             Block = -1;    // [-] material block, −1: the shared families
        std::string                                         BlockSignature{};
        std::vector<ShownOccupant>                          Shown{};
        Frontier::GeometryConfiguration::Resolution         Resolution{};
        bool                                                Present = false;
    };

    void Bind(Occupancy& Entry, const Frontier::GeometryConfiguration& Configuration) const;
    void Resolve(Occupancy& Entry, const Frontier::GeometryConfiguration& Configuration,
                 std::vector<Frontier::GlobalIllumination::OccupantPlacement>& Placements);
    void Release(Occupancy& Entry, std::vector<Frontier::GlobalIllumination::OccupantPlacement>& Placements);
    [[nodiscard]] bool     AcquireReserve(uint32_t FacetSet, uint32_t& OutOccupant);
    [[nodiscard]] uint32_t ResolveParticipation(const Frontier::GlobalIllumination::OccupantEntry& Base,
                                                const Frontier::GeometryConfiguration& Configuration) const;
    [[nodiscard]] int32_t  ResolveOverride(const Frontier::GlobalIllumination::OccupantEntry& Base, const Occupancy& Entry) const;

    const Frontier::GlobalIllumination::SceneStructure* Scene_ = nullptr;
    std::vector<uint8_t>                                Materials_{};      // [-] the family table: Materials.bin, then the shapes'
    uint32_t                                            FamilyCount_ = 0;
    uint32_t                                            ShapeFamilyFirst_ = 0;
    std::vector<Frontier::ShapeArchive>                 Shapes_{};
    std::vector<Frontier::PlacementMatrix>              Archive_{};        // [-] every occupant's transform as loaded
    std::vector<Frontier::GlobalIllumination::OccupantEntry> Entries_{};   // [-] every occupant as loaded (the templates)
    std::vector<std::vector<uint32_t>>                  SetFamilies_{};    // [-] material families of each facet set
    std::vector<uint8_t>                                Coverable_{};      // [-] archive occupants a record claims (drawn at load)
    std::vector<uint8_t>                                Hidden_{};         // [-] coverable occupants: 0 archive, 1 hidden, 2 claimed
    std::vector<uint32_t>                               ReserveFree_{};    // [-] reserve occupants nobody holds
    std::vector<int32_t>                                ReserveSet_{};     // [-] [occupant] the facet set a reserve occupant shows
    std::vector<uint8_t>                                BlockHeld_{};      // [-] material blocks in use
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> BlockWrites_{};
    std::unordered_map<uint64_t, Occupancy>             Occupancies_{};
    std::shared_ptr<const GeometryResolutions>          Snapshot_ = std::make_shared<const GeometryResolutions>();
    std::unordered_map<uint32_t, uint64_t>              Owners_{};         // [-] shown occupant → record token value
    uint64_t                                            Generation_ = 0;
};

} // namespace Everest
