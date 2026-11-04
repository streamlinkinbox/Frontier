/*==========================================================================================================================================
                                                         EVERESTSCENEGEOMETRY.CPP
==========================================================================================================================================*/
// 🧩 Project Everest's scene geometry: binds the level's Geometry records to the proving ground's occupants by name, resolves
//    each record's transform and modifier stack into occupant placements for the viewport, and reports what each resolved to.

#include "EverestSceneGeometry.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationHost.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <utility>

namespace Everest
{

namespace
{

// 📝 MaterialSpecification.js CourseMaterial, index-aligned (79 RimScrape, 80 TabletHousing, 81 RimBase).
constexpr const char* FamilyIdentifiers[] = {
    "CheckerLight",   "CheckerDark",     "Surround",      "Ramp",           "Bump",          "Cone",            "BodyCoat",
    "VehicleGlass",   "VehiclePlastic",  "Tyre",          "Hub",            "Brake",         "VehicleTrim",     "Headlight",
    "Taillight",      "VehicleDefault",  "WallCrimson",   "WallViridian",   "WallChalk",     "WallCobalt",      "TunnelConcrete",
    "StressAmber",    "StressTeal",      "StressViolet",  "StressIvory",    "StressLamp",    "Asphalt",         "AsphaltPatch",
    "ApronConcrete",  "PaintWhite",      "PaintYellow",   "KerbRed",        "TyreRubber",    "Grass",           "Gravel",
    "Cladding",       "ContainerOrange", "ContainerBlue", "ContainerGreen", "ContainerWhite", "ContainerRed",   "SteelDark",
    "LampWarm",       "LampCool",        "BarrierConcrete", "SeatBlue",     "Wood",          "WoodInterior",    "Glass",
    "GlassEdge",      "Concrete",        "ConcreteInterior", "Stone",       "StoneInterior", "Plastic",         "PlasticInterior",
    "Tempered",       "TemperedEdge",    "PaintGreen",    "PaintBlue",      "ConeOrange",    "ConeBand",        "ConeBase",
    "DerezGlow",      "DerezHot",        "DerezGlitch",   "GateLamp",       "GateLampRed",   "SignBoard",       "SignLetter",
    "CabinLeather",   "CabinTrim",       "CabinAccent",   "SteeringWheel",  "DisplayGlass",  "MirrorGlass",     "SeatFabric",
    "CabinCarpet",    "MirrorTrim",      "RimScrape",     "TabletHousing",  "RimBase"};

std::string ComposeSignature(const Frontier::GeometryConfiguration& Configuration)
{
    std::string Signature;
    for (const std::string& Stem : Configuration.OccupantStems) Signature += Stem + '\x1F';
    Signature += std::to_string(Configuration.OccupantFirst) + '/' + std::to_string(Configuration.OccupantCount);
    Signature += '/' + Configuration.ShapeName + (Configuration.ClaimsArchive ? "/claims" : "/copies");
    return Signature;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    NAMES
//------------------------------------------------------------------------------------------------------------------------

std::string ResolveFamilyName(uint32_t Family)
{
    if (Family >= std::size(FamilyIdentifiers)) return "Family " + std::to_string(Family);
    // 📝 PascalCase to words, the first capitalised: "BarrierConcrete" → "Barrier concrete".
    std::string Words;
    for (const char* Cursor = FamilyIdentifiers[Family]; *Cursor != '\0'; ++Cursor)
    {
        const bool Capital = std::isupper(static_cast<unsigned char>(*Cursor)) != 0;
        if (Capital && !Words.empty()) Words += ' ';
        Words += Capital && !Words.empty() ? static_cast<char>(std::tolower(static_cast<unsigned char>(*Cursor))) : *Cursor;
    }
    return Words;
}

bool MatchOccupantStem(std::string_view Name, std::string_view Stem) noexcept
{
    if (Stem.empty() || Name.size() < Stem.size() || Name.compare(0, Stem.size(), Stem) != 0) return false;
    const std::string_view Remainder = Name.substr(Stem.size());
    if (Remainder.empty()) return true;
    const auto Digit = [](char Letter) { return std::isdigit(static_cast<unsigned char>(Letter)) != 0; };
    if (Remainder.front() != ' ') return std::all_of(Remainder.begin(), Remainder.end(), Digit);
    const std::string_view Token = Remainder.substr(1);
    if (Token.empty() || Token.find(' ') != std::string_view::npos) return false;
    return std::any_of(Token.begin(), Token.end(), Digit) || (Token.size() == 1 && std::isupper(static_cast<unsigned char>(Token.front())) != 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE GEOMETRY OCCUPANCY
//------------------------------------------------------------------------------------------------------------------------

void SceneGeometryOccupancy::Reset(const Frontier::GlobalIllumination::SceneStructure* Scene, std::vector<uint8_t> MaterialProfiles,
                                   std::vector<Frontier::ShapeArchive> Shapes)
{
    using namespace Frontier::GlobalIllumination;
    Scene_  = Scene;
    Shapes_ = std::move(Shapes);
    Materials_ = std::move(MaterialProfiles);
    Materials_.resize(Materials_.size() / MaterialStride * MaterialStride);
    ShapeFamilyFirst_ = static_cast<uint32_t>(Materials_.size() / MaterialStride);
    for (const Frontier::ShapeArchive& Shape : Shapes_)
    {
        const auto* Channels = reinterpret_cast<const uint8_t*>(Shape.Surface.data());
        Materials_.insert(Materials_.end(), Channels, Channels + MaterialStride);
    }
    FamilyCount_ = static_cast<uint32_t>(Materials_.size() / MaterialStride);
    Archive_.clear();
    Entries_.clear();
    SetFamilies_.clear();
    Coverable_.clear();
    Hidden_.clear();
    ReserveFree_.clear();
    ReserveSet_.clear();
    BlockHeld_.assign(MaterialBlockCapacity, 0u);
    BlockWrites_.clear();
    Occupancies_.clear();
    if (Scene_ != nullptr)
    {
        Archive_.reserve(Scene_->Occupants.size());
        for (const auto& Occupant : Scene_->Occupants) Archive_.push_back(Occupant.Current);
        Entries_ = Scene_->Occupants;
        for (const auto& Set : Scene_->FacetSets)
        {
            std::set<uint32_t> Families;
            for (const auto& Facet : Set.Facets) Families.insert(Facet.Family);
            SetFamilies_.emplace_back(Families.begin(), Families.end());
        }
        // 📝 A record may claim what the archive draws at load: the static occupants and the ground, not the vehicle, the
        //    fragment pools, the reserve or the card-only ground tiles.
        Coverable_.assign(Entries_.size(), 0u);
        Hidden_.assign(Entries_.size(), 0u);
        ReserveSet_.assign(Entries_.size(), -1);
        for (uint32_t Index = 0; Index < Entries_.size(); ++Index)
        {
            const auto& Occupant = Entries_[Index];
            const bool  Vehicle  = static_cast<int32_t>(Index) == Scene_->VehicleBody ||
                                 std::find(Scene_->VehicleWheels.begin(), Scene_->VehicleWheels.end(), static_cast<int32_t>(Index)) !=
                                     Scene_->VehicleWheels.end();
            Coverable_[Index] = !Vehicle && !Occupant.Inactive && !Occupant.Fragment && !Scene_->QueryReserved(Index) &&
                                (Occupant.Participation & OccupantParticipation::Raster) != 0;
            if (Scene_->QueryReserved(Index)) ReserveSet_[Index] = static_cast<int32_t>(Occupant.FacetSet);
        }
        for (uint32_t Slot = Scene_->ReserveCount; Slot > 0; --Slot) ReserveFree_.push_back(Scene_->ReserveFirst + Slot - 1);
    }
    Snapshot_ = std::make_shared<const GeometryResolutions>();
    Owners_.clear();
    ++Generation_;
}

const Frontier::PlacementMatrix* SceneGeometryOccupancy::QueryArchiveTransform(uint32_t Occupant) const noexcept
{
    return Occupant < Archive_.size() ? &Archive_[Occupant] : nullptr;
}

std::optional<uint64_t> SceneGeometryOccupancy::QueryOwner(uint32_t Occupant) const
{
    const auto Found = Owners_.find(Occupant);
    return Found != Owners_.end() ? std::optional<uint64_t>(Found->second) : std::nullopt;
}

std::optional<uint64_t> SceneGeometryOccupancy::ResolvePickedRecord(const Frontier::GlobalIllumination::OccupantPick& Pick) const
{
    // 📝 Nearest first within a disc of the pick's reach; the picked pixel itself is distance 0, so what is drawn there always wins.
    std::optional<uint64_t> Nearest;
    const int32_t           Reach = static_cast<int32_t>(Pick.Reach);
    int32_t                 Best  = Reach * Reach + 1;
    for (int32_t OffsetY = -Reach; OffsetY <= Reach; ++OffsetY)
        for (int32_t OffsetX = -Reach; OffsetX <= Reach; ++OffsetX)
        {
            const int32_t Distance = OffsetX * OffsetX + OffsetY * OffsetY;
            if (Distance >= Best) continue;
            const uint32_t Occupant = Pick.QueryOccupant(OffsetX, OffsetY);
            if (Occupant == Frontier::GlobalIllumination::OccupantPick::NoOccupant) continue;
            if (const auto Owner = QueryOwner(Occupant))
            {
                Nearest = Owner;
                Best    = Distance;
            }
        }
    return Nearest;
}

std::vector<std::pair<uint32_t, std::vector<uint8_t>>> SceneGeometryOccupancy::ConsumeMaterialBlocks()
{
    return std::exchange(BlockWrites_, {});
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BINDING
//------------------------------------------------------------------------------------------------------------------------

void SceneGeometryOccupancy::Bind(Occupancy& Entry, const Frontier::GeometryConfiguration& Configuration) const
{
    using namespace Frontier;
    using namespace Frontier::GlobalIllumination;
    Entry.Templates.clear();
    Entry.Claimed.clear();
    Entry.Anchors.clear();
    Entry.PartOffsets.clear();
    Entry.Resolution = {};
    auto& Resolution = Entry.Resolution;
    if (Scene_ == nullptr)
    {
        Resolution.Diagnostic = "The scene archive is not loaded yet.";
        return;
    }
    std::set<uint32_t> Families;
    // 📝 An engine shape: one part, one base copy at the object's origin, the shape's surface as its family.
    if (!Configuration.ShapeName.empty())
    {
        const auto Found = std::find_if(Shapes_.begin(), Shapes_.end(), [&](const ShapeArchive& Shape) { return Shape.Name == Configuration.ShapeName; });
        if (Found == Shapes_.end() || Scene_->ShapeSetCount == 0)
        {
            Resolution.Diagnostic = "No engine shape \"" + Configuration.ShapeName + "\" in EngineContent/ShapeArchives.";
            return;
        }
        const uint32_t ShapeIndex = static_cast<uint32_t>(Found - Shapes_.begin());
        const uint32_t SetIndex   = Scene_->ShapeSetFirst + ShapeIndex;
        const FacetSet& Set       = Scene_->FacetSets[SetIndex];
        OccupantEntry Template;
        Template.FacetSet      = SetIndex;
        Template.Participation = OccupantParticipation::Raster | OccupantParticipation::Distance | OccupantParticipation::Cards;
        Template.CardMinimum   = Set.LocalMinimum;
        Template.CardMaximum   = Set.LocalMaximum;
        Template.CardDensity   = Set.CardDensity;
        Entry.Templates   = {Template};
        Entry.Anchors     = {ConstructIdentityPlacement()};
        Entry.PartOffsets = {{ConstructIdentityPlacement()}};
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Entry.Extent[Axis]      = Set.LocalMaximum[Axis] - Set.LocalMinimum[Axis];
            Resolution.Extent[Axis] = static_cast<float>(Entry.Extent[Axis]);
        }
        Resolution.Bound  = true;
        Resolution.Shape  = true;
        Resolution.Layout = 1;
        Resolution.Parts  = 1;
        Resolution.Shapes = {Set.Name};
        Resolution.Facets = static_cast<uint32_t>(Set.Facets.size());
        Families.insert(ShapeFamilyFirst_ + ShapeIndex);
    }
    else
    {
        if (Configuration.OccupantStems.empty())
        {
            Resolution.Diagnostic = "The record names no occupants of the scene archive and no engine shape.";
            return;
        }
        const auto&                        Occupants = Scene_->Occupants;
        std::vector<std::vector<uint32_t>> Parts;
        for (const std::string& Stem : Configuration.OccupantStems)
        {
            std::vector<uint32_t> Matches;
            for (uint32_t Index = 0; Index < Occupants.size(); ++Index)
                if (!Scene_->QueryReserved(Index) && MatchOccupantStem(Entries_[Index].Name, Stem)) Matches.push_back(Index);
            const size_t First = std::min<size_t>(Configuration.OccupantFirst, Matches.size());
            const size_t Count = Configuration.OccupantCount == 0 ? Matches.size() - First
                                                                   : std::min<size_t>(Configuration.OccupantCount, Matches.size() - First);
            Parts.emplace_back(Matches.begin() + static_cast<std::ptrdiff_t>(First), Matches.begin() + static_cast<std::ptrdiff_t>(First + Count));
        }
        const size_t Placements = Parts.front().size();
        for (size_t Part = 0; Part < Parts.size(); ++Part)
        {
            if (Parts[Part].size() == Placements && Placements > 0) continue;
            Resolution.Diagnostic = "\"" + Configuration.OccupantStems[Part] + "\" matches " + std::to_string(Parts[Part].size()) +
                                    " occupants where the first part matches " + std::to_string(Placements) + ".";
            return;
        }
        for (const uint32_t Occupant : Parts.front()) Entry.Anchors.push_back(Archive_[Occupant]);
        for (const auto& Part : Parts)
        {
            Entry.Templates.push_back(Entries_[Part.front()]);
            std::vector<PlacementMatrix> Offsets;
            for (size_t Copy = 0; Copy < Part.size(); ++Copy)
                Offsets.push_back(MultiplyPlacement(InvertPlacement(Entry.Anchors[Copy]), Archive_[Part[Copy]]));
            Entry.PartOffsets.push_back(std::move(Offsets));
        }
        if (Configuration.ClaimsArchive) Entry.Claimed = Parts;
        const auto& Leading = Scene_->FacetSets[Entries_[Parts.front().front()].FacetSet];
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Entry.Extent[Axis]      = Leading.LocalMaximum[Axis] - Leading.LocalMinimum[Axis];
            Resolution.Extent[Axis] = static_cast<float>(Entry.Extent[Axis]);
        }
        Resolution.Bound    = true;
        Resolution.Authored = Configuration.ClaimsArchive ? static_cast<uint32_t>(Placements) : 0u;
        Resolution.Layout   = static_cast<uint32_t>(Placements);
        Resolution.Parts    = static_cast<uint32_t>(Parts.size());
        for (const auto& Part : Parts)
        {
            const auto& Set = Scene_->FacetSets[Entries_[Part.front()].FacetSet];
            Resolution.Shapes.push_back(Set.Name);
            Resolution.Facets += static_cast<uint32_t>(Set.Facets.size());
            for (const uint32_t Occupant : Part)
            {
                const auto& Entrant = Entries_[Occupant];
                if (Entrant.MaterialOverride >= 0) Families.insert(static_cast<uint32_t>(Entrant.MaterialOverride));
                else Families.insert(SetFamilies_[Entrant.FacetSet].begin(), SetFamilies_[Entrant.FacetSet].end());
                Resolution.Breakable = Resolution.Breakable || Entrant.Breakable;
            }
        }
        // 📝 A breakable prop's fragment pool: the occupants named "<prop> fragment N" (the first copy's).
        if (Resolution.Breakable)
        {
            const std::string Prefix = Entries_[Parts.front().front()].Name + " fragment ";
            for (const auto& Occupant : Entries_)
                if (Occupant.Name.rfind(Prefix, 0) == 0) ++Resolution.FragmentCapacity;
        }
        for (const std::string& Stem : Configuration.OccupantStems)
            Resolution.Luminous = Resolution.Luminous || Stem == "Light pole head" || Stem == "Point lamp lantern";
    }
    for (const uint32_t Family : Families)
    {
        Resolution.Families.push_back(Family);
        Resolution.FamilyNames.push_back(Family >= ShapeFamilyFirst_ && Family - ShapeFamilyFirst_ < Shapes_.size()
                                             ? Shapes_[Family - ShapeFamilyFirst_].SurfaceName
                                             : ResolveFamilyName(Family));
        std::array<float, 12> Channels{};
        if (static_cast<size_t>(Family + 1) * MaterialStride <= Materials_.size())
            std::memcpy(Channels.data(), Materials_.data() + static_cast<size_t>(Family) * MaterialStride, MaterialStride);
        Resolution.FamilyChannels.push_back(Channels);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESERVE AND SURFACES
//------------------------------------------------------------------------------------------------------------------------

bool SceneGeometryOccupancy::AcquireReserve(uint32_t FacetSet, uint32_t& OutOccupant)
{
    if (ReserveFree_.empty()) return false;
    // 📝 A free occupant already showing the shape needs no new cards; else the most recently freed one.
    auto Chosen = std::find_if(ReserveFree_.rbegin(), ReserveFree_.rend(),
                               [&](uint32_t Occupant) { return ReserveSet_[Occupant] == static_cast<int32_t>(FacetSet); });
    if (Chosen == ReserveFree_.rend()) Chosen = ReserveFree_.rbegin();
    OutOccupant = *Chosen;
    ReserveFree_.erase(std::next(Chosen).base());
    return true;
}

uint32_t SceneGeometryOccupancy::ResolveParticipation(const Frontier::GlobalIllumination::OccupantEntry& Base,
                                                      const Frontier::GeometryConfiguration& Configuration) const
{
    using namespace Frontier::GlobalIllumination;
    uint32_t Mask = Base.Participation & ~(OccupantParticipation::Dynamic | OccupantParticipation::ShadowExempt | OccupantParticipation::IndirectExempt);
    // 📝 A shape that no occupant unioned at bring-up has no distance volume to union.
    if (Base.FacetSet < Scene_->FacetSets.size() && Scene_->FacetSets[Base.FacetSet].VolumeIndex < 0) Mask &= ~OccupantParticipation::Distance;
    if (Configuration.Dynamic) Mask |= OccupantParticipation::Dynamic;
    if (!Configuration.CastShadows) Mask |= OccupantParticipation::ShadowExempt;
    if (!Configuration.GlobalIllumination) Mask |= OccupantParticipation::IndirectExempt;
    return Mask;
}

int32_t SceneGeometryOccupancy::ResolveOverride(const Frontier::GlobalIllumination::OccupantEntry& Base, const Occupancy& Entry) const
{
    using namespace Frontier::GlobalIllumination;
    if (Entry.Block < 0) return Base.MaterialOverride;
    const uint32_t BlockBase = FamilyCount_ * (1u + static_cast<uint32_t>(Entry.Block));
    // 📝 An occupant drawn in one family keeps that one family, now the block's copy; the rest offset every facet's family.
    if (Base.MaterialOverride >= 0) return static_cast<int32_t>(BlockBase + static_cast<uint32_t>(Base.MaterialOverride));
    return static_cast<int32_t>(MaterialBlockBit | BlockBase);
}

void SceneGeometryOccupancy::Release(Occupancy& Entry, std::vector<Frontier::GlobalIllumination::OccupantPlacement>& Placements)
{
    for (auto& Part : Entry.Reserve)
    {
        for (const uint32_t Occupant : Part)
        {
            Placements.push_back({Occupant, Archive_[Occupant], false});
            ReserveFree_.push_back(Occupant);
        }
        Part.clear();
    }
    if (Entry.Block >= 0) BlockHeld_[static_cast<size_t>(Entry.Block)] = 0u;
    Entry.Block = -1;
    Entry.BlockSignature.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

void SceneGeometryOccupancy::Resolve(Occupancy& Entry, const Frontier::GeometryConfiguration& Configuration,
                                     std::vector<Frontier::GlobalIllumination::OccupantPlacement>& Placements)
{
    using namespace Frontier;
    using namespace Frontier::GlobalIllumination;
    auto& Resolution = Entry.Resolution;
    Entry.Shown.clear();
    if (!Resolution.Bound || Entry.Anchors.empty())
    {
        Release(Entry, Placements);
        return;
    }
    // ① the object's origin (an archive object: authored, or the first placement's position and turn; a shape: the world's) and
    //    where it is now
    PlacementMatrix Reference = ConstructIdentityPlacement();
    if (!Resolution.Shape)
    {
        std::array<double, 3> Origin{}, Rotation{}, Scale{};
        DecomposePlacement(Entry.Anchors.front(), Origin, Rotation, Scale);
        double Yaw = Rotation[2];
        if (Configuration.ArchiveOriginAuthored)
        {
            Origin = {Configuration.ArchiveOriginMetres[0], Configuration.ArchiveOriginMetres[1], Configuration.ArchiveOriginMetres[2]};
            Yaw    = Configuration.ArchiveYawDegrees;
        }
        Reference = ConstructPlacement(Origin, {0.0, 0.0, Yaw}, {1.0, 1.0, 1.0});
    }
    const PlacementMatrix Current = ResolveSpatialPlacement(Configuration.Spatial);
    const PlacementMatrix Object  = ConfirmProximity(Current, Reference, GeometryLinearTolerance, GeometryMetreTolerance) ? Reference : Current;
    const PlacementMatrix Inverse = InvertPlacement(Reference);
    // ② the base copies in the object's frame, then the modifier stack
    const size_t BaseAvailable = Entry.Anchors.size();
    const size_t BaseCount     = Configuration.BaseCopies == 0 ? BaseAvailable : std::min<size_t>(Configuration.BaseCopies, BaseAvailable);
    std::vector<PlacementMatrix> Bases;
    for (size_t Copy = 0; Copy < BaseCount; ++Copy) Bases.push_back(MultiplyPlacement(Inverse, Entry.Anchors[Copy]));
    const std::vector<PlacementMatrix> Copies = EvaluateModifierStack(Bases, Configuration.Modifiers, Entry.Extent);
    Resolution.Copies = static_cast<uint32_t>(Copies.size());
    // ③ the surfaces: a record that owns surfaces holds a material block, written whenever they change
    std::string BlockSignature;
    for (const SurfaceSlotConfiguration& Slot : Configuration.SurfaceSlots)
    {
        BlockSignature += std::to_string(Slot.Family) + ':';
        BlockSignature.append(reinterpret_cast<const char*>(Slot.Channels.data()), sizeof(Slot.Channels));
    }
    bool Resurfaced = false;
    if (BlockSignature.empty() && Entry.Block >= 0)
    {
        BlockHeld_[static_cast<size_t>(Entry.Block)] = 0u;
        Entry.Block = -1;
        Entry.BlockSignature.clear();
    }
    else if (!BlockSignature.empty() && BlockSignature != Entry.BlockSignature)
    {
        if (Entry.Block < 0)
        {
            const auto Free = std::find(BlockHeld_.begin(), BlockHeld_.end(), 0u);
            if (Free != BlockHeld_.end())
            {
                *Free       = 1u;
                Entry.Block = static_cast<int32_t>(Free - BlockHeld_.begin());
            }
        }
        if (Entry.Block >= 0)
        {
            std::vector<uint8_t> Records = Materials_;
            for (const SurfaceSlotConfiguration& Slot : Configuration.SurfaceSlots)
                if (Slot.Family < FamilyCount_) std::memcpy(Records.data() + static_cast<size_t>(Slot.Family) * MaterialStride, Slot.Channels.data(), MaterialStride);
            BlockWrites_.emplace_back(static_cast<uint32_t>(Entry.Block), std::move(Records));
            Entry.BlockSignature = BlockSignature;
            Resurfaced           = true;
        }
    }
    Resolution.OwnSurfaces = Entry.Block >= 0;
    for (size_t Index = 0; Index < Resolution.Families.size(); ++Index)
        for (const SurfaceSlotConfiguration& Slot : Configuration.SurfaceSlots)
            if (Slot.Family == Resolution.Families[Index] && Entry.Block >= 0) Resolution.FamilyChannels[Index] = Slot.Channels;
    if (!BlockSignature.empty() && Entry.Block < 0)
        Resolution.Diagnostic = "Every material block is in use (" + std::to_string(MaterialBlockCapacity) + "); this object shows the shared surfaces.";
    // ④ the copies beyond the archive's come from the reserve: holdings follow the copy count, part by part
    const size_t Authored = Entry.Claimed.empty() ? 0u : Entry.Claimed.front().size();
    const size_t Wanted   = Copies.size() > Authored ? Copies.size() - Authored : 0u;
    Entry.Reserve.resize(Entry.Templates.size());
    for (size_t Part = 0; Part < Entry.Templates.size(); ++Part)
    {
        auto& Held = Entry.Reserve[Part];
        while (Held.size() > Wanted)
        {
            Placements.push_back({Held.back(), Archive_[Held.back()], false});
            ReserveFree_.push_back(Held.back());
            Held.pop_back();
        }
        uint32_t Occupant = 0;
        while (Held.size() < Wanted && AcquireReserve(Entry.Templates[Part].FacetSet, Occupant)) Held.push_back(Occupant);
    }
    size_t Reserved = Wanted;
    for (const auto& Held : Entry.Reserve) Reserved = std::min(Reserved, Held.size());
    Resolution.Reserved = static_cast<uint32_t>(Reserved);
    const size_t Usable = std::min(Copies.size(), Authored) + Reserved;
    Resolution.Shown    = Configuration.Visible ? static_cast<uint32_t>(Usable) : 0u;
    if (Reserved < Wanted && Resolution.Diagnostic.empty())
        Resolution.Diagnostic = "The reserve is exhausted: " + std::to_string(Usable) + " of " + std::to_string(Copies.size()) + " copies can be shown.";
    // ⑤ placements: an archive copy at the archive's own placement keeps the archive's transforms exactly; a reserve copy takes its
    //    template's shape when it shows another
    GlobalIllumination::WorldBounds Bounds{{1.0e30, 1.0e30, 1.0e30}, {-1.0e30, -1.0e30, -1.0e30}};
    auto Emit = [&](uint32_t Occupant, const OccupantEntry& Base, const PlacementMatrix& Placement, bool Active, bool Reserve, size_t Part) {
        OccupantPlacement Entrant{Occupant, Placement, Active};
        Entrant.Participation    = ResolveParticipation(Base, Configuration);
        Entrant.MaterialOverride = ResolveOverride(Base, Entry);
        Entrant.Recapture        = Resurfaced;
        if (Reserve && ReserveSet_[Occupant] != static_cast<int32_t>(Entry.Templates[Part].FacetSet))
        {
            Entrant.Template     = Entry.Templates[Part];
            ReserveSet_[Occupant] = static_cast<int32_t>(Entry.Templates[Part].FacetSet);
        }
        Placements.push_back(std::move(Entrant));
        if (!Active) return;
        const uint32_t  SetIndex = Reserve ? Entry.Templates[Part].FacetSet : Base.FacetSet;
        const FacetSet& Set      = Scene_->FacetSets[SetIndex];
        Entry.Shown.push_back({Occupant, SetIndex, Placement});
        const auto Extent = GlobalIllumination::TransformBounds(Placement, Set.LocalMinimum, Set.LocalMaximum);
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Bounds.Minimum[Axis] = std::min(Bounds.Minimum[Axis], Extent.Minimum[Axis]);
            Bounds.Maximum[Axis] = std::max(Bounds.Maximum[Axis], Extent.Maximum[Axis]);
        }
    };
    for (size_t Copy = 0; Copy < Authored; ++Copy)
    {
        const bool      Active = Copy < Copies.size() && Copy < Resolution.Shown;
        bool            Exact  = true;
        PlacementMatrix Delta  = ConstructIdentityPlacement();
        if (Copy < Copies.size())
        {
            const PlacementMatrix Placed = MultiplyPlacement(Object, Copies[Copy]);
            Exact = ConfirmProximity(Placed, Entry.Anchors[Copy], GeometryLinearTolerance, GeometryMetreTolerance);
            if (!Exact) Delta = MultiplyPlacement(Placed, InvertPlacement(Entry.Anchors[Copy]));
        }
        for (size_t Part = 0; Part < Entry.Claimed.size(); ++Part)
        {
            const uint32_t Occupant = Entry.Claimed[Part][Copy];
            Emit(Occupant, Entries_[Occupant], Exact ? Archive_[Occupant] : MultiplyPlacement(Delta, Archive_[Occupant]), Active, false, Part);
        }
    }
    for (size_t Extra = 0; Extra < Reserved; ++Extra)
    {
        const size_t          Copy   = Authored + Extra;
        const bool            Active = Copy < Resolution.Shown;
        const PlacementMatrix Placed = MultiplyPlacement(Object, Copies[Copy]);
        const size_t          Base   = BaseCount > 0 ? Copy % BaseCount : 0u;
        for (size_t Part = 0; Part < Entry.Templates.size(); ++Part)
            Emit(Entry.Reserve[Part][Extra], Entry.Templates[Part], MultiplyPlacement(Placed, Entry.PartOffsets[Part][Base]), Active, true, Part);
    }
    // 📝 Held occupants past the usable copies (another part ran out of reserve) stay hidden.
    for (size_t Part = 0; Part < Entry.Reserve.size(); ++Part)
        for (size_t Extra = Reserved; Extra < Entry.Reserve[Part].size(); ++Extra)
            Placements.push_back({Entry.Reserve[Part][Extra], Archive_[Entry.Reserve[Part][Extra]], false});
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        Resolution.BoundsMinimum[Axis] = Resolution.Shown > 0 ? static_cast<float>(Bounds.Minimum[Axis]) : 0.0f;
        Resolution.BoundsMaximum[Axis] = Resolution.Shown > 0 ? static_cast<float>(Bounds.Maximum[Axis]) : 0.0f;
    }
}

std::vector<Frontier::GlobalIllumination::OccupantPlacement> SceneGeometryOccupancy::ResolvePlacements(const Frontier::SceneWorld& World)
{
    std::vector<Frontier::GlobalIllumination::OccupantPlacement> Placements;
    if (Scene_ == nullptr) return Placements;
    bool Changed = false;
    for (auto& [Token, Entry] : Occupancies_) Entry.Present = false;
    const auto Records = World.FindAllByType("Geometry");
    for (const Frontier::SceneRecordBase* Record : Records)
    {
        const auto* Geometry = dynamic_cast<const Frontier::GeometryRecord*>(Record);
        if (Geometry == nullptr) continue;
        const Frontier::GeometryConfiguration& Configuration = Geometry->QueryConfiguration();
        Occupancy&                             Entry         = Occupancies_[Record->QueryToken().Value];
        Entry.Present = true;
        if (const std::string Signature = ComposeSignature(Configuration); Signature != Entry.Signature)
        {
            Bind(Entry, Configuration);
            Entry.Signature = Signature;
            Entry.Revision  = ~0ull;
        }
        if (Entry.Revision == Configuration.Revision) continue;
        Entry.Revision = Configuration.Revision;
        Resolve(Entry, Configuration, Placements);
        Changed = true;
    }
    // 📝 A record that left the level hands back its reserve occupants and its material block; its archive occupants are unclaimed.
    for (auto Cursor = Occupancies_.begin(); Cursor != Occupancies_.end();)
    {
        if (Cursor->second.Present)
        {
            ++Cursor;
            continue;
        }
        Release(Cursor->second, Placements);
        Cursor  = Occupancies_.erase(Cursor);
        Changed = true;
    }
    // 📝 The archive occupants nobody claims: hidden while the level holds Geometry records (a removed object stays removed), at the
    //    archive's placements once it holds none.
    std::vector<uint8_t> Claimed(Entries_.size(), 0u);
    for (const auto& [Token, Entry] : Occupancies_)
        for (const auto& Part : Entry.Claimed)
            for (const uint32_t Occupant : Part) Claimed[Occupant] = 1u;
    // 📝 Hidden_ holds what an occupant was last handed: 0 the archive's placement, 1 hidden, 2 a claiming record's placement.
    const bool Authoritative = !Records.empty();
    for (uint32_t Index = 0; Index < Entries_.size(); ++Index)
    {
        if (!Coverable_[Index]) continue;
        const uint8_t Target = Claimed[Index] ? 2u : (Authoritative ? 1u : 0u);
        if (Hidden_[Index] == Target) continue;
        Hidden_[Index] = Target;
        if (Target == 2u) continue;
        Placements.push_back({Index, Archive_[Index], Target == 0u});
        Changed = true;
    }
    if (Changed)
    {
        std::unordered_map<uint32_t, uint32_t> Users;
        for (const auto& [Token, Entry] : Occupancies_)
            if (Entry.Block < 0)
                for (const uint32_t Family : Entry.Resolution.Families) ++Users[Family];
        auto Snapshot = std::make_shared<GeometryResolutions>();
        for (const auto& [Token, Entry] : Occupancies_)
        {
            Frontier::GeometryConfiguration::Resolution Resolution = Entry.Resolution;
            for (const uint32_t Family : Resolution.Families) Resolution.FamilyUsers.push_back(Entry.Block >= 0 ? 1u : Users[Family]);
            Resolution.ReserveFree = static_cast<uint32_t>(ReserveFree_.size());
            Snapshot->emplace(Token, std::move(Resolution));
        }
        Snapshot_ = std::move(Snapshot);
        Owners_.clear();
        for (const auto& [Token, Entry] : Occupancies_)
            for (const ShownOccupant& Shown : Entry.Shown) Owners_[Shown.Occupant] = Token;
        ++Generation_;
    }
    return Placements;
}

} // namespace Everest
