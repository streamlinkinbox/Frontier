/*==========================================================================================================================================
                                                          RAYTRACINGSPACE.CPP
==========================================================================================================================================*/
// 🧩 Two-level ray-tracing acceleration structure: per-refresh TLAS over active occupants + binned-SAH BLAS (RayTracingSpace.js).

#include "RayTracingSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr uint32_t BlasLeafCapacity = 4;    // [-] triangles per BLAS leaf
constexpr int      BlasBinCount     = 12;   // [-] SAH bins for static BLAS construction
constexpr int      TlasBinCount     = 8;    // [-] SAH bins for per-refresh TLAS construction

double RoundHalfUp(double Scalar) { return std::floor(Scalar + 0.5); }
double LesserOf(double Left, double Right) { return Right < Left ? Right : Left; }
double GreaterOf(double Left, double Right) { return Right > Left ? Right : Left; }

void StoreEntry(std::vector<uint8_t>& Bytes, uint32_t Index, const std::array<double, 3>& Minimum, uint32_t LeftOrFirst,
                const std::array<double, 3>& Maximum, uint32_t Count)
{
    float    Scalars[8] = {static_cast<float>(Minimum[0]), static_cast<float>(Minimum[1]), static_cast<float>(Minimum[2]), 0.0f,
                           static_cast<float>(Maximum[0]), static_cast<float>(Maximum[1]), static_cast<float>(Maximum[2]), 0.0f};
    uint32_t Words[8];
    std::memcpy(Words, Scalars, sizeof(Scalars));
    Words[3] = LeftOrFirst;
    Words[7] = Count;
    std::memcpy(Bytes.data() + static_cast<size_t>(Index) * 32, Words, 32);
}

float LoadScalar(const std::vector<uint8_t>& Bytes, size_t ScalarIndex)
{
    float Value;
    std::memcpy(&Value, Bytes.data() + ScalarIndex * 4, 4);
    return Value;
}

void StoreScalar(std::vector<uint8_t>& Bytes, size_t ScalarIndex, double Value)
{
    const float Single = static_cast<float>(Value);
    std::memcpy(Bytes.data() + ScalarIndex * 4, &Single, 4);
}

void StoreWord(std::vector<uint8_t>& Bytes, size_t ScalarIndex, uint32_t Value)
{
    std::memcpy(Bytes.data() + ScalarIndex * 4, &Value, 4);
}

} // namespace

// 📝 Octahedral unit-vector packing matching PackingCodec (pack2x16snorm(EncodeOctahedral(Normal))).
uint32_t PackOctahedralHost(const Vector3& Normal) noexcept
{
    const double Nx = Normal[0], Ny = Normal[1], Nz = Normal[2];
    double L1 = std::fabs(Nx) + std::fabs(Ny) + std::fabs(Nz);
    if (L1 == 0.0 || std::isnan(L1)) L1 = 1.0;
    double Ox = Nx / L1;
    double Oy = Ny / L1;
    if (Nz < 0.0)
    {
        const double Fx = (1.0 - std::fabs(Oy)) * (Ox >= 0.0 ? 1.0 : -1.0);
        const double Fy = (1.0 - std::fabs(Ox)) * (Oy >= 0.0 ? 1.0 : -1.0);
        Ox = Fx;
        Oy = Fy;
    }
    const int32_t Sx = static_cast<int32_t>(std::max(-32767.0, std::min(32767.0, RoundHalfUp(Ox * 32767.0))));
    const int32_t Sy = static_cast<int32_t>(std::max(-32767.0, std::min(32767.0, RoundHalfUp(Oy * 32767.0))));
    return (static_cast<uint32_t>(Sx) & 0xFFFFu) | ((static_cast<uint32_t>(Sy) & 0xFFFFu) << 16);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BLAS CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

FacetBlas ConstructFacetBlas(const std::vector<FacetEntry>& Facets, uint32_t GlobalEntryOffset, uint32_t GlobalTriangleOffset)
{
    FacetBlas Result;
    const uint32_t FacetCount = static_cast<uint32_t>(Facets.size());
    if (FacetCount == 0) return Result;
    std::vector<float> Centroids(static_cast<size_t>(FacetCount) * 3), FacetMinimum(Centroids.size()), FacetMaximum(Centroids.size());
    for (uint32_t Facet = 0; Facet < FacetCount; ++Facet)
    {
        const auto& Corners = Facets[Facet].Corners;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const double A = Corners[0][Axis], B = Corners[1][Axis], C = Corners[2][Axis];
            FacetMinimum[Facet * 3 + Axis] = static_cast<float>(LesserOf(LesserOf(A, B), C));
            FacetMaximum[Facet * 3 + Axis] = static_cast<float>(GreaterOf(GreaterOf(A, B), C));
            Centroids[Facet * 3 + Axis]    = static_cast<float>((A + B + C) / 3.0);
        }
    }
    std::vector<uint32_t> Order(FacetCount);
    for (uint32_t Facet = 0; Facet < FacetCount; ++Facet) Order[Facet] = Facet;

    struct BlasEntry
    {
        std::array<double, 3> Minimum{0.0, 0.0, 0.0};
        std::array<double, 3> Maximum{0.0, 0.0, 0.0};
        uint32_t              LeftOrFirst = 0;
        uint32_t              Count       = 0;
    };
    std::vector<BlasEntry> Entries;
    auto AllocateEntry = [&Entries]() {
        Entries.push_back(BlasEntry{});
        return static_cast<uint32_t>(Entries.size() - 1);
    };
    auto FitBounds = [&](uint32_t Entry, uint32_t First, uint32_t Count) {
        double MinX = INFINITY, MinY = INFINITY, MinZ = INFINITY, MaxX = -INFINITY, MaxY = -INFINITY, MaxZ = -INFINITY;
        for (uint32_t Index = First; Index < First + Count; ++Index)
        {
            const uint32_t Base = Order[Index] * 3;
            if (FacetMinimum[Base] < MinX) MinX = FacetMinimum[Base];
            if (FacetMinimum[Base + 1] < MinY) MinY = FacetMinimum[Base + 1];
            if (FacetMinimum[Base + 2] < MinZ) MinZ = FacetMinimum[Base + 2];
            if (FacetMaximum[Base] > MaxX) MaxX = FacetMaximum[Base];
            if (FacetMaximum[Base + 1] > MaxY) MaxY = FacetMaximum[Base + 1];
            if (FacetMaximum[Base + 2] > MaxZ) MaxZ = FacetMaximum[Base + 2];
        }
        Entries[Entry].Minimum = {MinX - 1.0e-4, MinY - 1.0e-4, MinZ - 1.0e-4};
        Entries[Entry].Maximum = {MaxX + 1.0e-4, MaxY + 1.0e-4, MaxZ + 1.0e-4};
    };
    auto SurfaceArea = [](const std::array<double, 3>& Min, const std::array<double, 3>& Max) {
        const double Dx = std::max(0.0, Max[0] - Min[0]), Dy = std::max(0.0, Max[1] - Min[1]), Dz = std::max(0.0, Max[2] - Min[2]);
        return 2.0 * (Dx * Dy + Dy * Dz + Dz * Dx);
    };
    struct Bin
    {
        uint32_t              Count = 0;
        std::array<double, 3> Minimum{INFINITY, INFINITY, INFINITY};
        std::array<double, 3> Maximum{-INFINITY, -INFINITY, -INFINITY};
    };
    std::function<void(uint32_t, uint32_t, uint32_t)> Subdivide = [&](uint32_t Entry, uint32_t First, uint32_t Count) {
        FitBounds(Entry, First, Count);
        if (Count <= BlasLeafCapacity)
        {
            Entries[Entry].LeftOrFirst = GlobalTriangleOffset + First;
            Entries[Entry].Count       = Count;
            return;
        }
        int    BestAxis  = -1;
        double BestSplit = 0.0;
        double BestCost  = INFINITY;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            double Low = INFINITY, High = -INFINITY;
            for (uint32_t Index = First; Index < First + Count; ++Index)
            {
                const double Centroid = Centroids[Order[Index] * 3 + Axis];
                if (Centroid < Low) Low = Centroid;
                if (Centroid > High) High = Centroid;
            }
            if (High - Low < 1.0e-7) continue;
            std::array<Bin, BlasBinCount> Bins{};
            const double Scale = BlasBinCount / (High - Low);
            for (uint32_t Index = First; Index < First + Count; ++Index)
            {
                const uint32_t Facet = Order[Index];
                const double   Raw   = std::floor((static_cast<double>(Centroids[Facet * 3 + Axis]) - Low) * Scale);
                const int      Slot  = static_cast<int>(std::min<double>(BlasBinCount - 1, std::max(0.0, Raw)));
                Bin&           Entry = Bins[Slot];
                Entry.Count++;
                for (int Component = 0; Component < 3; ++Component)
                {
                    Entry.Minimum[Component] = LesserOf(Entry.Minimum[Component], FacetMinimum[Facet * 3 + Component]);
                    Entry.Maximum[Component] = GreaterOf(Entry.Maximum[Component], FacetMaximum[Facet * 3 + Component]);
                }
            }
            for (int Split = 1; Split < BlasBinCount; ++Split)
            {
                uint32_t LeftCount = 0, RightCount = 0;
                std::array<double, 3> LeftMin{INFINITY, INFINITY, INFINITY}, LeftMax{-INFINITY, -INFINITY, -INFINITY};
                std::array<double, 3> RightMin{INFINITY, INFINITY, INFINITY}, RightMax{-INFINITY, -INFINITY, -INFINITY};
                for (int Slot = 0; Slot < BlasBinCount; ++Slot)
                {
                    const Bin& Entry = Bins[Slot];
                    if (Entry.Count == 0) continue;
                    if (Slot < Split)
                    {
                        LeftCount += Entry.Count;
                        for (int Component = 0; Component < 3; ++Component)
                        {
                            if (Entry.Minimum[Component] < LeftMin[Component]) LeftMin[Component] = Entry.Minimum[Component];
                            if (Entry.Maximum[Component] > LeftMax[Component]) LeftMax[Component] = Entry.Maximum[Component];
                        }
                    }
                    else
                    {
                        RightCount += Entry.Count;
                        for (int Component = 0; Component < 3; ++Component)
                        {
                            if (Entry.Minimum[Component] < RightMin[Component]) RightMin[Component] = Entry.Minimum[Component];
                            if (Entry.Maximum[Component] > RightMax[Component]) RightMax[Component] = Entry.Maximum[Component];
                        }
                    }
                }
                if (LeftCount == 0 || RightCount == 0) continue;
                const double Cost = SurfaceArea(LeftMin, LeftMax) * LeftCount + SurfaceArea(RightMin, RightMax) * RightCount;
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestAxis  = Axis;
                    BestSplit = Low + Split / Scale;
                }
            }
        }
        const double LeafCost = SurfaceArea(Entries[Entry].Minimum, Entries[Entry].Maximum) * Count;
        uint32_t Middle = First;
        if (BestAxis >= 0 && BestCost < LeafCost)
        {
            int64_t Cursor = First, Tail = static_cast<int64_t>(First) + Count - 1;
            while (Cursor <= Tail)
            {
                if (static_cast<double>(Centroids[Order[Cursor] * 3 + BestAxis]) < BestSplit) ++Cursor;
                else
                {
                    std::swap(Order[Cursor], Order[Tail]);
                    --Tail;
                }
            }
            Middle = static_cast<uint32_t>(Cursor);
        }
        if (Middle == First || Middle == First + Count)
        {
            if (Count <= BlasLeafCapacity * 3)
            {
                Entries[Entry].LeftOrFirst = GlobalTriangleOffset + First;
                Entries[Entry].Count       = Count;
                return;
            }
            Middle = First + (Count >> 1);
        }
        const uint32_t Left = AllocateEntry();
        AllocateEntry();
        Entries[Entry].LeftOrFirst = GlobalEntryOffset + Left;
        Entries[Entry].Count       = 0;
        Subdivide(Left, First, Middle - First);
        Subdivide(Left + 1, Middle, First + Count - Middle);
    };
    const uint32_t Root = AllocateEntry();
    Subdivide(Root, 0, FacetCount);

    Result.EntryBytes.assign(Entries.size() * 32, 0u);
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        StoreEntry(Result.EntryBytes, static_cast<uint32_t>(Index), Entries[Index].Minimum, Entries[Index].LeftOrFirst, Entries[Index].Maximum,
                   Entries[Index].Count);
    }
    // 📝 64 bytes per triangle: A + family, E1 + packed NA, E2 + packed NB, Ng + packed NC.
    Result.TriangleBytes.assign(static_cast<size_t>(FacetCount) * 64, 0u);
    for (uint32_t Index = 0; Index < FacetCount; ++Index)
    {
        const FacetEntry& Facet = Facets[Order[Index]];
        const Vector3& A  = Facet.Corners[0];
        const Vector3  E1 = SubtractVector(Facet.Corners[1], A);
        const Vector3  E2 = SubtractVector(Facet.Corners[2], A);
        const Vector3  Ng = NormalizeVector(CrossVector(E1, E2));
        const Vector3  NA = Facet.NormalsAuthored ? NormalizeVector(Facet.Normals[0]) : Ng;
        const Vector3  NB = Facet.NormalsAuthored ? NormalizeVector(Facet.Normals[1]) : Ng;
        const Vector3  NC = Facet.NormalsAuthored ? NormalizeVector(Facet.Normals[2]) : Ng;
        const size_t   Base = static_cast<size_t>(Index) * 16;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            StoreScalar(Result.TriangleBytes, Base + Axis, A[Axis]);
            StoreScalar(Result.TriangleBytes, Base + 4 + Axis, E1[Axis]);
            StoreScalar(Result.TriangleBytes, Base + 8 + Axis, E2[Axis]);
            StoreScalar(Result.TriangleBytes, Base + 12 + Axis, Ng[Axis]);
        }
        StoreWord(Result.TriangleBytes, Base + 3, Facet.Family);
        StoreWord(Result.TriangleBytes, Base + 7, PackOctahedralHost(NA));
        StoreWord(Result.TriangleBytes, Base + 11, PackOctahedralHost(NB));
        StoreWord(Result.TriangleBytes, Base + 15, PackOctahedralHost(NC));
    }
    Result.RootEntry     = static_cast<int64_t>(GlobalEntryOffset) + Root;
    Result.EntryCount    = static_cast<uint32_t>(Entries.size());
    Result.TriangleCount = FacetCount;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RAY TRACING SPACE
//------------------------------------------------------------------------------------------------------------------------

RayTracingSpace::RayTracingSpace(DeviceExchange& Exchange) : Exchange_(Exchange)
{
}

void RayTracingSpace::BuildHostHierarchy(const SceneStructure& Scene)
{
    uint32_t EntryCursor    = TlasEntryCapacity;
    uint32_t TriangleCursor = 0;
    std::vector<FacetBlas> Results;
    FacetBlasRoots.assign(Scene.FacetSets.size(), -1);
    for (size_t SetIndex = 0; SetIndex < Scene.FacetSets.size(); ++SetIndex)
    {
        const FacetSet& Set = Scene.FacetSets[SetIndex];
        if (Set.Facets.empty()) continue;
        FacetBlas Blas = ConstructFacetBlas(Set.Facets, EntryCursor, TriangleCursor);
        FacetBlasRoots[SetIndex] = Blas.RootEntry;
        EntryCursor += Blas.EntryCount;
        TriangleCursor += Blas.TriangleCount;
        Results.push_back(std::move(Blas));
    }
    StaticEntryCount   = EntryCursor;
    StaticTriCount     = TriangleCursor;
    DynamicEntryOffset = EntryCursor;
    DynamicTriOffset   = TriangleCursor;
    TotalEntryCapacity = EntryCursor + DynamicBlasEntryCapacity;
    TotalTriCapacity   = TriangleCursor + DynamicTriangleCapacity;
    AllEntryBytes.assign(static_cast<size_t>(TotalEntryCapacity) * 32, 0u);
    AllTriangleBytes.assign(static_cast<size_t>(TotalTriCapacity) * 64, 0u);
    uint32_t WriteEntry = TlasEntryCapacity, WriteTriangle = 0;
    for (const FacetBlas& Blas : Results)
    {
        std::memcpy(AllEntryBytes.data() + static_cast<size_t>(WriteEntry) * 32, Blas.EntryBytes.data(), Blas.EntryBytes.size());
        std::memcpy(AllTriangleBytes.data() + static_cast<size_t>(WriteTriangle) * 64, Blas.TriangleBytes.data(), Blas.TriangleBytes.size());
        WriteEntry += Blas.EntryCount;
        WriteTriangle += Blas.TriangleCount;
    }
    OccupantBlasRoot.assign(Scene.Occupants.size(), -1);
    for (size_t Index = 0; Index < Scene.Occupants.size(); ++Index)
    {
        OccupantBlasRoot[Index] = FacetBlasRoots[Scene.Occupants[Index].FacetSet];
    }
}

void RayTracingSpace::ConstructResources(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& Materials,
                                         const std::shared_ptr<DeviceRegion>& Luminaires)
{
    BuildHostHierarchy(Scene);
    MaterialRegion  = Materials;
    LuminaireRegion = Luminaires;
    EntryRegion     = Exchange_.ConstructRegionFrom(AllEntryBytes.data(), AllEntryBytes.size(), "Ray tracing entries (TLAS + BLAS)");
    TriangleRegion  = Exchange_.ConstructRegionFrom(AllTriangleBytes.data(), AllTriangleBytes.size(), "Ray tracing BLAS triangles");
    TextureSpecification Fallback;
    Fallback.Label  = "Fallback HMI screen texture";
    Fallback.Format = TextureFormat::Rgba8UnormSrgb;
    Fallback.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget;
    DefaultHmiTexture = Exchange_.ConstructTexture(Fallback);
    auto DefaultView  = Exchange_.ConstructView(DefaultHmiTexture);
    const std::vector<DescriptorSlot> Slots = {{DescriptorCode::StorageRead}, {DescriptorCode::StorageRead}, {DescriptorCode::StorageRead},
                                               {DescriptorCode::Uniform}, {DescriptorCode::Texture2D}, {DescriptorCode::Texture2D}};
    MarchLayout = Exchange_.ConstructDescriptorLayout(Slots, "Ray tracing march");
    AttachHmiTextures(DefaultView, DefaultView);
    auto& MutableScene = const_cast<SceneStructure&>(Scene);
    RefreshTlas(MutableScene, nullptr);
}

void RayTracingSpace::AttachHmiTextures(const std::shared_ptr<DeviceTextureView>& TabletView, const std::shared_ptr<DeviceTextureView>& ClusterView)
{
    MarchGroup = Exchange_.ConstructDescriptorGroup(MarchLayout, {EntryRegion, TriangleRegion, MaterialRegion, LuminaireRegion, TabletView, ClusterView},
                                                    "Ray tracing march");
}

// 📝 Rebuilds the TLAS over every active, drawable occupant (ground first when present) and uploads entries [0, used).
void RayTracingSpace::RefreshTlas(SceneStructure& Scene, const std::array<bool, 4>* ShreddedWheels)
{
    if (ShreddedWheels != nullptr)
    {
        for (int Wheel = 0; Wheel < 4; ++Wheel)
        {
            const int32_t Index = Scene.VehicleWheels[Wheel];
            if (Index < 0) continue;
            if ((*ShreddedWheels)[Wheel]) Scene.Occupants[Index].Participation |= OccupantParticipation::Shredded;
            else Scene.Occupants[Index].Participation &= ~OccupantParticipation::Shredded;
        }
    }
    const auto&   Occupants = Scene.Occupants;
    const int32_t GroundIndex = Scene.GroundOccupant;
    const size_t  Capacity  = Occupants.size();
    std::vector<uint32_t> Order(Capacity), OccupantIndices(Capacity);
    std::vector<float> MinX(Capacity), MinY(Capacity), MinZ(Capacity), MaxX(Capacity), MaxY(Capacity), MaxZ(Capacity);
    std::vector<float> CentX(Capacity), CentY(Capacity), CentZ(Capacity);
    uint32_t ObjectCount = 0;
    for (uint32_t Index = 0; Index < Occupants.size(); ++Index)
    {
        if (static_cast<int32_t>(Index) == GroundIndex) continue;
        const OccupantEntry& Occupant = Occupants[Index];
        if (Occupant.Inactive) continue;
        if (OccupantBlasRoot[Index] < 0) continue;
        const bool Drawable = (Occupant.Participation & OccupantParticipation::Raster) != 0 || Occupant.Fragment;
        if (!Drawable) continue;
        const WorldBounds Bounds = Scene.QueryWorldBounds(Index);
        const uint32_t    K      = ObjectCount++;
        Order[K]           = K;
        OccupantIndices[K] = Index;
        MinX[K] = static_cast<float>(Bounds.Minimum[0]);
        MinY[K] = static_cast<float>(Bounds.Minimum[1]);
        MinZ[K] = static_cast<float>(Bounds.Minimum[2]);
        MaxX[K] = static_cast<float>(Bounds.Maximum[0]);
        MaxY[K] = static_cast<float>(Bounds.Maximum[1]);
        MaxZ[K] = static_cast<float>(Bounds.Maximum[2]);
        CentX[K] = static_cast<float>(0.5 * (Bounds.Minimum[0] + Bounds.Maximum[0]));
        CentY[K] = static_cast<float>(0.5 * (Bounds.Minimum[1] + Bounds.Maximum[1]));
        CentZ[K] = static_cast<float>(0.5 * (Bounds.Minimum[2] + Bounds.Maximum[2]));
    }
    auto WriteLeafEntry = [&](uint32_t EntryIndex, uint32_t K) {
        const uint32_t OccupantIndex = OccupantIndices[K];
        StoreEntry(AllEntryBytes, EntryIndex, {MinX[K], MinY[K], MinZ[K]}, static_cast<uint32_t>(OccupantBlasRoot[OccupantIndex]),
                   {MaxX[K], MaxY[K], MaxZ[K]}, TlasLeafBit | OccupantIndex);
    };
    auto WriteOccupantLeaf = [&](uint32_t EntryIndex, uint32_t OccupantIndex) {
        const WorldBounds Bounds = Scene.QueryWorldBounds(OccupantIndex);
        StoreEntry(AllEntryBytes, EntryIndex, Bounds.Minimum, static_cast<uint32_t>(OccupantBlasRoot[OccupantIndex]), Bounds.Maximum,
                   TlasLeafBit | OccupantIndex);
    };
    const bool HasGround = GroundIndex >= 0 && !Occupants[GroundIndex].Inactive && OccupantBlasRoot[GroundIndex] >= 0;
    ActiveInstances = ObjectCount + (HasGround ? 1u : 0u);
    if (ObjectCount == 0 && !HasGround)
    {
        std::fill(AllEntryBytes.begin(), AllEntryBytes.begin() + 32, 0u);
        StoreWord(AllEntryBytes, 3, EmptyTlasSentinel);
        StoreWord(AllEntryBytes, 7, 0u);
        UsedTlasEntries = 1;
        UploadTlas();
        return;
    }
    if (ObjectCount == 0 && HasGround)
    {
        WriteOccupantLeaf(0, static_cast<uint32_t>(GroundIndex));
        UsedTlasEntries = 1;
        UploadTlas();
        return;
    }
    uint32_t NextEntry = 1;
    uint32_t ObjectsRoot = 0;
    if (HasGround)
    {
        WriteOccupantLeaf(1, static_cast<uint32_t>(GroundIndex));
        ObjectsRoot = 2;
        NextEntry   = 3;
    }
    std::function<void(uint32_t, uint32_t, uint32_t)> SubdivideObjects = [&](uint32_t EntryIndex, uint32_t First, uint32_t Count) {
        if (Count == 1)
        {
            WriteLeafEntry(EntryIndex, Order[First]);
            return;
        }
        double BoxMinX = INFINITY, BoxMinY = INFINITY, BoxMinZ = INFINITY, BoxMaxX = -INFINITY, BoxMaxY = -INFINITY, BoxMaxZ = -INFINITY;
        double CMinX = INFINITY, CMinY = INFINITY, CMinZ = INFINITY, CMaxX = -INFINITY, CMaxY = -INFINITY, CMaxZ = -INFINITY;
        for (uint32_t I = First; I < First + Count; ++I)
        {
            const uint32_t K = Order[I];
            if (MinX[K] < BoxMinX) BoxMinX = MinX[K];
            if (MinY[K] < BoxMinY) BoxMinY = MinY[K];
            if (MinZ[K] < BoxMinZ) BoxMinZ = MinZ[K];
            if (MaxX[K] > BoxMaxX) BoxMaxX = MaxX[K];
            if (MaxY[K] > BoxMaxY) BoxMaxY = MaxY[K];
            if (MaxZ[K] > BoxMaxZ) BoxMaxZ = MaxZ[K];
            if (CentX[K] < CMinX) CMinX = CentX[K];
            if (CentY[K] < CMinY) CMinY = CentY[K];
            if (CentZ[K] < CMinZ) CMinZ = CentZ[K];
            if (CentX[K] > CMaxX) CMaxX = CentX[K];
            if (CentY[K] > CMaxY) CMaxY = CentY[K];
            if (CentZ[K] > CMaxZ) CMaxZ = CentZ[K];
        }
        const size_t Base = static_cast<size_t>(EntryIndex) * 8;
        StoreScalar(AllEntryBytes, Base + 0, BoxMinX);
        StoreScalar(AllEntryBytes, Base + 1, BoxMinY);
        StoreScalar(AllEntryBytes, Base + 2, BoxMinZ);
        StoreScalar(AllEntryBytes, Base + 4, BoxMaxX);
        StoreScalar(AllEntryBytes, Base + 5, BoxMaxY);
        StoreScalar(AllEntryBytes, Base + 6, BoxMaxZ);
        const double SpanX = CMaxX - CMinX, SpanY = CMaxY - CMinY, SpanZ = CMaxZ - CMinZ;
        double Low = CMinX, High = CMaxX;
        const std::vector<float>* Centres = &CentX;
        if (SpanY >= SpanX && SpanY >= SpanZ)
        {
            Low = CMinY; High = CMaxY; Centres = &CentY;
        }
        else if (SpanZ >= SpanX && SpanZ >= SpanY)
        {
            Low = CMinZ; High = CMaxZ; Centres = &CentZ;
        }
        uint32_t Middle = First + (Count >> 1);
        if (High - Low > 1.0e-5 && Count >= 4)
        {
            // 📝 The demo's bins are Float32Array, so bin bounds are float values; counts and costs are doubles.
            std::array<uint32_t, TlasBinCount> BinCounts{};
            std::array<float, TlasBinCount> BinMinX, BinMinY, BinMinZ, BinMaxX, BinMaxY, BinMaxZ;
            BinMinX.fill(INFINITY); BinMinY.fill(INFINITY); BinMinZ.fill(INFINITY);
            BinMaxX.fill(-INFINITY); BinMaxY.fill(-INFINITY); BinMaxZ.fill(-INFINITY);
            const double Scale = TlasBinCount / (High - Low);
            for (uint32_t I = First; I < First + Count; ++I)
            {
                const uint32_t K = Order[I];
                const int B = static_cast<int>(std::min<double>(TlasBinCount - 1, std::max(0.0, std::floor((static_cast<double>((*Centres)[K]) - Low) * Scale))));
                BinCounts[B]++;
                if (MinX[K] < BinMinX[B]) BinMinX[B] = MinX[K];
                if (MinY[K] < BinMinY[B]) BinMinY[B] = MinY[K];
                if (MinZ[K] < BinMinZ[B]) BinMinZ[B] = MinZ[K];
                if (MaxX[K] > BinMaxX[B]) BinMaxX[B] = MaxX[K];
                if (MaxY[K] > BinMaxY[B]) BinMaxY[B] = MaxY[K];
                if (MaxZ[K] > BinMaxZ[B]) BinMaxZ[B] = MaxZ[K];
            }
            double BestCost  = INFINITY;
            double BestSplit = 0.5 * (Low + High);
            for (int Split = 1; Split < TlasBinCount; ++Split)
            {
                uint32_t LC = 0, RC = 0;
                double LMinX = INFINITY, LMinY = INFINITY, LMinZ = INFINITY, LMaxX = -INFINITY, LMaxY = -INFINITY, LMaxZ = -INFINITY;
                double RMinX = INFINITY, RMinY = INFINITY, RMinZ = INFINITY, RMaxX = -INFINITY, RMaxY = -INFINITY, RMaxZ = -INFINITY;
                for (int B = 0; B < TlasBinCount; ++B)
                {
                    if (BinCounts[B] == 0) continue;
                    if (B < Split)
                    {
                        LC += BinCounts[B];
                        if (BinMinX[B] < LMinX) LMinX = BinMinX[B];
                        if (BinMinY[B] < LMinY) LMinY = BinMinY[B];
                        if (BinMinZ[B] < LMinZ) LMinZ = BinMinZ[B];
                        if (BinMaxX[B] > LMaxX) LMaxX = BinMaxX[B];
                        if (BinMaxY[B] > LMaxY) LMaxY = BinMaxY[B];
                        if (BinMaxZ[B] > LMaxZ) LMaxZ = BinMaxZ[B];
                    }
                    else
                    {
                        RC += BinCounts[B];
                        if (BinMinX[B] < RMinX) RMinX = BinMinX[B];
                        if (BinMinY[B] < RMinY) RMinY = BinMinY[B];
                        if (BinMinZ[B] < RMinZ) RMinZ = BinMinZ[B];
                        if (BinMaxX[B] > RMaxX) RMaxX = BinMaxX[B];
                        if (BinMaxY[B] > RMaxY) RMaxY = BinMaxY[B];
                        if (BinMaxZ[B] > RMaxZ) RMaxZ = BinMaxZ[B];
                    }
                }
                if (LC == 0 || RC == 0) continue;
                const double Lx = std::max(0.0, LMaxX - LMinX), Ly = std::max(0.0, LMaxY - LMinY), Lz = std::max(0.0, LMaxZ - LMinZ);
                const double Rx = std::max(0.0, RMaxX - RMinX), Ry = std::max(0.0, RMaxY - RMinY), Rz = std::max(0.0, RMaxZ - RMinZ);
                const double Cost = (Lx * Ly + Ly * Lz + Lz * Lx) * LC + (Rx * Ry + Ry * Rz + Rz * Rx) * RC;
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestSplit = Low + Split / Scale;
                }
            }
            int64_t Cursor = First, Tail = static_cast<int64_t>(First) + Count - 1;
            while (Cursor <= Tail)
            {
                if (static_cast<double>((*Centres)[Order[Cursor]]) < BestSplit) ++Cursor;
                else
                {
                    std::swap(Order[Cursor], Order[Tail]);
                    --Tail;
                }
            }
            if (Cursor > First && Cursor < static_cast<int64_t>(First) + Count) Middle = static_cast<uint32_t>(Cursor);
        }
        else if (High - Low > 1.0e-5)
        {
            const double Pivot  = 0.5 * (Low + High);
            int64_t      Cursor = First, Tail = static_cast<int64_t>(First) + Count - 1;
            while (Cursor <= Tail)
            {
                if (static_cast<double>((*Centres)[Order[Cursor]]) < Pivot) ++Cursor;
                else
                {
                    std::swap(Order[Cursor], Order[Tail]);
                    --Tail;
                }
            }
            if (Cursor > First && Cursor < static_cast<int64_t>(First) + Count) Middle = static_cast<uint32_t>(Cursor);
        }
        const uint32_t LeftEntry  = NextEntry;
        const uint32_t RightEntry = NextEntry + 1;
        NextEntry += 2;
        StoreWord(AllEntryBytes, Base + 3, LeftEntry);
        StoreWord(AllEntryBytes, Base + 7, 0u);
        SubdivideObjects(LeftEntry, First, Middle - First);
        SubdivideObjects(RightEntry, Middle, First + Count - Middle);
    };
    SubdivideObjects(ObjectsRoot, 0, ObjectCount);
    if (HasGround)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            StoreScalar(AllEntryBytes, Axis, std::min(LoadScalar(AllEntryBytes, 8 + Axis), LoadScalar(AllEntryBytes, 16 + Axis)));
            StoreScalar(AllEntryBytes, 4 + Axis, std::max(LoadScalar(AllEntryBytes, 12 + Axis), LoadScalar(AllEntryBytes, 20 + Axis)));
        }
        StoreWord(AllEntryBytes, 3, 1u);
        StoreWord(AllEntryBytes, 7, 0u);
    }
    UsedTlasEntries = NextEntry;
    UploadTlas();
}

void RayTracingSpace::UploadTlas()
{
    if (EntryRegion && UsedTlasEntries > 0)
    {
        Exchange_.WriteRegion(EntryRegion, 0, AllEntryBytes.data(), static_cast<VkDeviceSize>(UsedTlasEntries) * 32);
    }
}

} // namespace Frontier::GlobalIllumination
