/*==========================================================================================================================================
                                                          SCENESTRUCTURE.CPP
==========================================================================================================================================*/
// 🧩 Scene topology runtime — facet sets, occupants, draw runs, the distance list and their device encodings (FacetStructure.js).

#include "SceneStructure.h"

#include <cstring>
#include <fstream>

namespace Frontier::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FACETSPACE DECODING
//------------------------------------------------------------------------------------------------------------------------

namespace
{

class FacetSpaceReader
{
  public:
    explicit FacetSpaceReader(std::vector<uint8_t> Bytes) : Bytes_(std::move(Bytes)) {}

    bool     Valid() const noexcept { return Valid_; }
    uint32_t U32() { uint32_t Value = 0; Read(&Value, 4); return Value; }
    int32_t  I32() { int32_t Value = 0; Read(&Value, 4); return Value; }
    double   F64() { double Value = 0.0; Read(&Value, 8); return Value; }
    Vector3  V3() { return {F64(), F64(), F64()}; }
    std::string Text()
    {
        const uint32_t Length = U32();
        std::string Value(Length, '\0');
        Read(Value.data(), Length);
        Cursor_ += (4 - (Length % 4)) % 4;
        return Value;
    }
    void Read(void* Target, size_t Count)
    {
        if (Cursor_ + Count > Bytes_.size())
        {
            Valid_ = false;
            std::memset(Target, 0, Count);
            return;
        }
        std::memcpy(Target, Bytes_.data() + Cursor_, Count);
        Cursor_ += Count;
    }

  private:
    std::vector<uint8_t> Bytes_;
    size_t               Cursor_ = 0;
    bool                 Valid_  = true;
};

void StoreFloat(std::vector<uint8_t>& Bytes, size_t ScalarIndex, double Value)
{
    const float Single = static_cast<float>(Value);
    std::memcpy(Bytes.data() + ScalarIndex * 4, &Single, 4);
}

void StoreUnsigned(std::vector<uint8_t>& Bytes, size_t ScalarIndex, uint32_t Value)
{
    std::memcpy(Bytes.data() + ScalarIndex * 4, &Value, 4);
}

} // namespace

bool SceneStructure::LoadFacetSpace(const std::filesystem::path& Path, std::string* OutDiagnostic)
{
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream)
    {
        if (OutDiagnostic) *OutDiagnostic = "facetspace missing: " + Path.string();
        return false;
    }
    std::vector<uint8_t> Bytes((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
    FacetSpaceReader Reader(std::move(Bytes));
    char Magic[4] = {};
    Reader.Read(Magic, 4);
    if (std::memcmp(Magic, "FSP1", 4) != 0 || Reader.U32() != 1u)
    {
        if (OutDiagnostic) *OutDiagnostic = "not an FSP1 facetspace: " + Path.string();
        return false;
    }
    const uint32_t SetCount      = Reader.U32();
    const uint32_t OccupantCount = Reader.U32();
    VehicleBody = Reader.I32();
    for (int32_t& Wheel : VehicleWheels) Wheel = Reader.I32();
    GroundOccupant   = Reader.I32();
    GroundTilesFirst = Reader.I32();
    for (int32_t& Strip : GroundStrips) Strip = Reader.I32();
    DistanceCount     = Reader.U32();
    BaseDistanceCount = Reader.U32();
    DynamicCount      = Reader.U32();
    VehicleBodySet    = Reader.U32();
    StressFirst       = Reader.U32();
    StressCapacity    = Reader.U32();
    ConeFirst         = Reader.U32();
    ConeCount         = Reader.U32();

    FacetSets.resize(SetCount);
    for (FacetSet& Set : FacetSets)
    {
        Set.Name                 = Reader.Text();
        const uint32_t FacetCount = Reader.U32();
        Set.DistanceVoxel        = Reader.F64();
        Set.CardDensity          = Reader.F64();
        Set.LocalMinimum         = Reader.V3();
        Set.LocalMaximum         = Reader.V3();
        Set.VertexCapacity       = Reader.U32();
        const uint32_t Flags     = Reader.U32();
        Set.Fragmented           = (Flags & 1u) != 0;
        const bool Authored      = (Flags & 2u) != 0;
        Set.Facets.resize(FacetCount);
        for (FacetEntry& Facet : Set.Facets)
        {
            for (Vector3& Corner : Facet.Corners) Corner = Reader.V3();
            Facet.Family         = Reader.U32();
            Facet.OccupantOffset = Reader.U32();
            if (Authored)
            {
                Facet.NormalsAuthored = Reader.U32() != 0u;
                for (Vector3& Normal : Facet.Normals) Normal = Reader.V3();
            }
        }
        Set.FirstVertex = 0;
        Set.VertexCount = FacetCount * 3u;
        Set.VolumeIndex = -1;
    }
    Occupants.resize(OccupantCount);
    for (OccupantEntry& Occupant : Occupants)
    {
        Occupant.Name             = Reader.Text();
        Occupant.FacetSet         = Reader.U32();
        Occupant.Participation    = Reader.U32();
        Occupant.MaterialOverride = Reader.I32();
        for (double& Element : Occupant.Current) Element = Reader.F64();
        for (double& Element : Occupant.Previous) Element = Reader.F64();
        Occupant.CardMinimum = Reader.V3();
        Occupant.CardMaximum = Reader.V3();
        Occupant.CardDensity = Reader.F64();
        const uint32_t Flags = Reader.U32();
        const Vector3 LocalMinimum = Reader.V3();
        const Vector3 LocalMaximum = Reader.V3();
        const uint32_t Fallback    = Reader.U32();
        Occupant.Cone       = Reader.I32();
        Occupant.StressSlot = Reader.I32();
        Occupant.Inactive  = (Flags & 1u) != 0;
        Occupant.Fragment  = (Flags & 2u) != 0;
        Occupant.Obstacle  = (Flags & 8u) != 0;
        Occupant.Breakable = (Flags & 16u) != 0;
        // 📝 'Moving' is only ever defined (false at rest) for cones and fragments; older captures lack bit 512.
        const bool MovingDefined = (Flags & 512u) != 0 || Occupant.Cone >= 0 || Occupant.Fragment;
        if (MovingDefined) Occupant.Moving = (Flags & 4u) != 0;
        if (Flags & 32u)
        {
            Occupant.LocalMinimum = LocalMinimum;
            Occupant.LocalMaximum = LocalMaximum;
        }
        if (Flags & 64u) Occupant.FallbackAlbedo = Fallback;
    }
    if (!Reader.Valid())
    {
        if (OutDiagnostic) *OutDiagnostic = "truncated facetspace: " + Path.string();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DRAW RUNS & DISTANCE LIST
//------------------------------------------------------------------------------------------------------------------------

// 📝 Consecutive rastered occupants of one facet set become one instanced draw; the shadow runs leave out the ground.
void SceneStructure::ConstructDrawRuns()
{
    std::vector<DrawRun> Runs;
    std::vector<DrawRun> Shadows;
    int64_t OpenRun    = -1;
    int64_t OpenShadow = -1;
    for (uint32_t Index = 0; Index < Occupants.size(); ++Index)
    {
        const OccupantEntry& Occupant = Occupants[Index];
        const bool Drawn = (Occupant.Participation & OccupantParticipation::Raster) && !Occupant.Inactive;
        if (Drawn && OpenRun >= 0 && Runs[OpenRun].FacetSet == Occupant.FacetSet && Runs[OpenRun].First + Runs[OpenRun].Count == Index)
        {
            Runs[OpenRun].Count++;
        }
        else if (Drawn)
        {
            Runs.push_back({Occupant.FacetSet, Index, 1u});
            OpenRun = static_cast<int64_t>(Runs.size()) - 1;
        }
        else
        {
            OpenRun = -1;
        }
        const bool Shadowed = Drawn && static_cast<int32_t>(Index) != GroundOccupant &&
                              !(Occupant.Participation & OccupantParticipation::ShadowExempt);
        if (Shadowed && OpenShadow >= 0 && Shadows[OpenShadow].FacetSet == Occupant.FacetSet &&
            Shadows[OpenShadow].First + Shadows[OpenShadow].Count == Index)
        {
            Shadows[OpenShadow].Count++;
        }
        else if (Shadowed)
        {
            Shadows.push_back({Occupant.FacetSet, Index, 1u});
            OpenShadow = static_cast<int64_t>(Shadows.size()) - 1;
        }
        else
        {
            OpenShadow = -1;
        }
    }
    DrawRuns   = std::move(Runs);
    ShadowRuns = std::move(Shadows);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENGINE SHAPES & RESERVE
//------------------------------------------------------------------------------------------------------------------------

void SceneStructure::AppendShapeSets(const std::vector<ShapeArchive>& Shapes, uint32_t FirstFamily)
{
    ShapeSetFirst = static_cast<uint32_t>(FacetSets.size());
    ShapeSetCount = static_cast<uint32_t>(Shapes.size());
    for (uint32_t Index = 0; Index < Shapes.size(); ++Index)
    {
        const ShapeArchive& Shape = Shapes[Index];
        FacetSet            Set;
        Set.Name          = "Shape " + Shape.Name;
        Set.DistanceVoxel = Shape.VoxelExtent;
        Set.CardDensity   = Shape.CardDensity;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Set.LocalMinimum[Axis] = -0.5 * Shape.Size[Axis];
            Set.LocalMaximum[Axis] = 0.5 * Shape.Size[Axis];
        }
        for (const ShapeFacet& Source : Shape.Facets)
        {
            FacetEntry Facet;
            for (int Corner = 0; Corner < 3; ++Corner)
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Facet.Corners[Corner][Axis] = Source.Corners[Corner][Axis];
                    Facet.Normals[Corner][Axis] = Source.Normals[Corner][Axis];
                }
            Facet.Family          = FirstFamily + Index;
            Facet.NormalsAuthored = true;
            Set.Facets.push_back(Facet);
        }
        Set.VertexCount = static_cast<uint32_t>(Set.Facets.size()) * 3u;
        if (Shape.Distance) Set.BakedVolume = std::make_shared<const ShapeDistanceVolume>(*Shape.Distance);
        FacetSets.push_back(std::move(Set));
    }
}

void SceneStructure::ConvertStressReserve(uint32_t Capacity)
{
    ReserveFirst = StressFirst;
    ReserveCount = std::min(Capacity, StressCapacity);
    for (uint32_t Slot = 0; Slot < StressCapacity && StressFirst + Slot < Occupants.size(); ++Slot)
    {
        OccupantEntry& Occupant = Occupants[StressFirst + Slot];
        const OccupantEntry Former = Occupant;
        Occupant = OccupantEntry{};
        Occupant.Name     = "Reserve " + std::to_string(Slot);
        Occupant.FacetSet = Former.FacetSet;
        Occupant.Current  = ConstructIdentityTransform();
        Occupant.Previous = Occupant.Current;
        Occupant.Inactive = true;
        if (Slot >= ReserveCount) continue;
        // 📝 Each reserve occupant starts on an engine shape (round robin), so every shape owns a distance volume and the cards are
        //    laid out for a shape; a template reassigns it before it is shown.
        Occupant.Participation = OccupantParticipation::Raster | OccupantParticipation::Distance | OccupantParticipation::Cards;
        if (ShapeSetCount > 0) Occupant.FacetSet = ShapeSetFirst + Slot % ShapeSetCount;
        const FacetSet& Set   = FacetSets[Occupant.FacetSet];
        Occupant.CardMinimum = Set.LocalMinimum;
        Occupant.CardMaximum = Set.LocalMaximum;
        Occupant.CardDensity = Set.CardDensity > 0.0 ? Set.CardDensity : Former.CardDensity;
    }
    StressCapacity = 0;
}

void SceneStructure::AssignReserveTemplate(uint32_t Index, const OccupantEntry& Template)
{
    OccupantEntry& Occupant = Occupants[Index];
    Occupant.FacetSet         = Template.FacetSet;
    Occupant.Participation    = Template.Participation & ~(OccupantParticipation::Dynamic | OccupantParticipation::Shredded);
    Occupant.MaterialOverride = Template.MaterialOverride;
    Occupant.CardMinimum      = Template.CardMinimum;
    Occupant.CardMaximum      = Template.CardMaximum;
    Occupant.CardDensity      = Template.CardDensity;
    Occupant.FallbackAlbedo   = Template.FallbackAlbedo;
    Occupant.Obstacle         = Template.Obstacle;
    Occupant.Moving.reset();
    Occupant.UnionTransform.reset();
}

bool SceneStructure::ConstructDistanceList()
{
    std::vector<uint32_t> List;
    for (uint32_t Index = 0; Index < DistanceCount; ++Index)
    {
        const OccupantEntry& Occupant = Occupants[Index];
        if ((Occupant.Participation & OccupantParticipation::Distance) && !Occupant.Inactive)
        {
            List.push_back(Index);
        }
    }
    const bool Changed = List != DistanceList;
    if (Changed) DistanceList = std::move(List);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   VERTEX ENCODING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Non-indexed facet corners, 32 bytes each: flat sets repeat the facet normal, authored sets carry corner normals.
std::vector<uint8_t> SceneStructure::EncodeVertexStream()
{
    uint32_t VertexTotal = 0;
    for (FacetSet& Set : FacetSets)
    {
        Set.FirstVertex = VertexTotal;
        VertexTotal += std::max(Set.VertexCount, Set.VertexCapacity);
    }
    std::vector<uint8_t> Bytes(static_cast<size_t>(VertexTotal) * VertexStride, 0u);
    for (const FacetSet& Set : FacetSets)
    {
        uint32_t Cursor = Set.FirstVertex;
        for (const FacetEntry& Facet : Set.Facets)
        {
            const Vector3 FacetNormal = NormalizeVector(CrossVector(SubtractVector(Facet.Corners[1], Facet.Corners[0]),
                                                                    SubtractVector(Facet.Corners[2], Facet.Corners[0])));
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                const Vector3& Normal = Facet.NormalsAuthored ? Facet.Normals[Corner] : FacetNormal;
                const size_t   Offset = static_cast<size_t>(Cursor) * 8;
                StoreFloat(Bytes, Offset + 0, Facet.Corners[Corner][0]);
                StoreFloat(Bytes, Offset + 1, Facet.Corners[Corner][1]);
                StoreFloat(Bytes, Offset + 2, Facet.Corners[Corner][2]);
                StoreUnsigned(Bytes, Offset + 3, Facet.Family);
                StoreFloat(Bytes, Offset + 4, Normal[0]);
                StoreFloat(Bytes, Offset + 5, Normal[1]);
                StoreFloat(Bytes, Offset + 6, Normal[2]);
                StoreUnsigned(Bytes, Offset + 7, Facet.OccupantOffset);
                ++Cursor;
            }
        }
    }
    return Bytes;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BOUNDS & TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

WorldBounds SceneStructure::QueryWorldBounds(uint32_t Index) const
{
    const OccupantEntry& Occupant = Occupants[Index];
    const FacetSet&      Set      = FacetSets[Occupant.FacetSet];
    if (Occupant.LocalMinimum) return TransformBounds(Occupant.Current, *Occupant.LocalMinimum, *Occupant.LocalMaximum);
    if ((Occupant.Participation & OccupantParticipation::Raster) || (Occupant.Participation & OccupantParticipation::Distance))
    {
        return TransformBounds(Occupant.Current, Set.LocalMinimum, Set.LocalMaximum);
    }
    return TransformBounds(Occupant.Current, Occupant.CardMinimum, Occupant.CardMaximum);
}

std::vector<WorldBounds> SceneStructure::QueryObstacleBounds() const
{
    std::vector<WorldBounds> Bounds;
    for (uint32_t Index = 0; Index < Occupants.size(); ++Index)
    {
        const OccupantEntry& Occupant = Occupants[Index];
        if (Occupant.Obstacle && !Occupant.Inactive && !(Occupant.Participation & OccupantParticipation::Dynamic))
        {
            Bounds.push_back(QueryWorldBounds(Index));
        }
    }
    return Bounds;
}

void SceneStructure::AdvanceOccupantTransform(uint32_t Index, const Transform& Next)
{
    Occupants[Index].Previous = Occupants[Index].Current;
    Occupants[Index].Current  = Next;
}

void SceneStructure::RetainOccupantTransform(uint32_t Index)
{
    Occupants[Index].Previous = Occupants[Index].Current;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OCCUPANT ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> SceneStructure::EncodeOccupantRange(const std::vector<OccupantVolume>* Volumes, uint32_t First, uint32_t Count) const
{
    std::vector<uint8_t> Bytes(static_cast<size_t>(std::max(1u, Count)) * OccupantStride, 0u);
    for (uint32_t Local = 0; Local < Count && First + Local < Occupants.size(); ++Local)
    {
        const uint32_t       Index    = First + Local;
        const OccupantEntry& Occupant = Occupants[Index];
        const FacetSet&      Set      = FacetSets[Occupant.FacetSet];
        const size_t         Offset   = static_cast<size_t>(Local) * (OccupantStride / 4);
        const Transform      Inverse  = InvertTransform(Occupant.Current);
        const Transform      PriorInverse = InvertTransform(Occupant.Previous);
        for (int Element = 0; Element < 16; ++Element)
        {
            StoreFloat(Bytes, Offset + Element, Occupant.Current[Element]);
            StoreFloat(Bytes, Offset + 16 + Element, Inverse[Element]);
            StoreFloat(Bytes, Offset + 32 + Element, Occupant.Previous[Element]);
            StoreFloat(Bytes, Offset + 48 + Element, PriorInverse[Element]);
        }
        // 📝 An inactive occupant carries empty bounds: the union's overlap test can never pass.
        const WorldBounds Bounds = Occupant.Inactive ? WorldBounds{{1.0e9, 1.0e9, 1.0e9}, {-1.0e9, -1.0e9, -1.0e9}} : QueryWorldBounds(Index);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            StoreFloat(Bytes, Offset + 64 + Axis, Bounds.Minimum[Axis]);
            StoreFloat(Bytes, Offset + 68 + Axis, Bounds.Maximum[Axis]);
        }
        StoreUnsigned(Bytes, Offset + 67, Occupant.Participation);
        // 📝 −1 keeps the per-facet families; a material block's override carries MaterialBlockBit (negative as int32) and is stored as is.
        StoreUnsigned(Bytes, Offset + 71, Occupant.MaterialOverride != -1 ? static_cast<uint32_t>(Occupant.MaterialOverride) : 0xFFFFFFFFu);
        const OccupantVolume* Volume = Occupant.Volume ? &*Occupant.Volume
                                     : ((Volumes != nullptr && Set.VolumeIndex >= 0) ? &(*Volumes)[static_cast<size_t>(Set.VolumeIndex)] : nullptr);
        if (Volume != nullptr)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                StoreFloat(Bytes, Offset + 72 + Axis, Volume->Minimum[Axis]);
                StoreFloat(Bytes, Offset + 76 + Axis, static_cast<double>(Volume->TexelOrigin[Axis]));
                StoreFloat(Bytes, Offset + 80 + Axis, static_cast<double>(Volume->Resolution[Axis]));
            }
            StoreFloat(Bytes, Offset + 75, Volume->VoxelExtent);
        }
        // 📝 A card-less occupant may carry its albedo packed where its first card would be (participation Fallback).
        const bool Fallback = Occupant.CardCount == 0 && Occupant.FallbackAlbedo.has_value();
        StoreUnsigned(Bytes, Offset + 79, Fallback ? *Occupant.FallbackAlbedo : Occupant.FirstCard);
        StoreUnsigned(Bytes, Offset + 83, Occupant.CardCount);
        if (Fallback) StoreUnsigned(Bytes, Offset + 67, Occupant.Participation | OccupantParticipation::Fallback);
    }
    return Bytes;
}

} // namespace Frontier::GlobalIllumination
