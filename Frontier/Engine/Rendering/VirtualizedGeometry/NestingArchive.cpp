/*==========================================================================================================================================
                                                          NESTINGARCHIVE.CPP
==========================================================================================================================================*/
// 🧩 Encoding and decoding of the stored cluster nestings (<stem>.nestingspace).
#include "NestingArchive.h"
#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
namespace Frontier::VirtualizedGeometry
{
namespace
{
constexpr uint32_t ArchiveVersion = 1;

class ArchiveWriter
{
  public:
    void U8(uint8_t Value) { Bytes.push_back(Value); }
    void U32(uint32_t Value)
    {
        for (int Shift = 0; Shift < 32; Shift += 8) Bytes.push_back(static_cast<uint8_t>(Value >> Shift));
    }
    void U64(uint64_t Value)
    {
        for (int Shift = 0; Shift < 64; Shift += 8) Bytes.push_back(static_cast<uint8_t>(Value >> Shift));
    }
    void F64(double Value) { U64(std::bit_cast<uint64_t>(Value)); }
    void Triple(const Point3& Value)
    {
        for (double Axis : Value) F64(Axis);
    }
    void Sphere(const NestingSphere& Value)
    {
        Triple(Value.Center);
        F64(Value.Radius);
    }
    void List(const std::vector<uint32_t>& Values)
    {
        U32(static_cast<uint32_t>(Values.size()));
        for (uint32_t Value : Values) U32(Value);
    }
    void Doubles(const std::vector<double>& Values)
    {
        U32(static_cast<uint32_t>(Values.size()));
        for (double Value : Values) F64(Value);
    }
    void Bytes8(const std::vector<uint8_t>& Values)
    {
        U32(static_cast<uint32_t>(Values.size()));
        Bytes.insert(Bytes.end(), Values.begin(), Values.end());
    }
    std::vector<uint8_t> Bytes{};
};

class ArchiveReader
{
  public:
    explicit ArchiveReader(const std::vector<uint8_t>& Bytes) : Bytes_(Bytes) {}
    [[nodiscard]] bool Failed() const noexcept { return Failed_; }
    [[nodiscard]] size_t Cursor() const noexcept { return Cursor_; }
    uint8_t U8() { return Take(1) ? Bytes_[Cursor_ - 1] : 0; }
    uint32_t U32()
    {
        if (!Take(4)) return 0;
        uint32_t Value = 0;
        for (int Byte = 0; Byte < 4; ++Byte) Value |= static_cast<uint32_t>(Bytes_[Cursor_ - 4 + Byte]) << (Byte * 8);
        return Value;
    }
    uint64_t U64()
    {
        if (!Take(8)) return 0;
        uint64_t Value = 0;
        for (int Byte = 0; Byte < 8; ++Byte) Value |= static_cast<uint64_t>(Bytes_[Cursor_ - 8 + Byte]) << (Byte * 8);
        return Value;
    }
    double F64() { return std::bit_cast<double>(U64()); }
    Point3 Triple() { return {F64(), F64(), F64()}; }
    NestingSphere Sphere()
    {
        NestingSphere Value;
        Value.Center = Triple();
        Value.Radius = F64();
        return Value;
    }
    std::vector<uint32_t> List()
    {
        const uint32_t Count = U32();
        if (!Fits(static_cast<size_t>(Count) * 4)) return {};
        std::vector<uint32_t> Values(Count);
        for (uint32_t& Value : Values) Value = U32();
        return Values;
    }
    std::vector<double> Doubles()
    {
        const uint32_t Count = U32();
        if (!Fits(static_cast<size_t>(Count) * 8)) return {};
        std::vector<double> Values(Count);
        for (double& Value : Values) Value = F64();
        return Values;
    }
    std::vector<uint8_t> Bytes8()
    {
        const uint32_t Count = U32();
        if (!Take(Count)) return {};
        return std::vector<uint8_t>(Bytes_.begin() + static_cast<std::ptrdiff_t>(Cursor_ - Count), Bytes_.begin() + static_cast<std::ptrdiff_t>(Cursor_));
    }
  private:
    [[nodiscard]] bool Fits(size_t Count) { return !Failed_ && Cursor_ + Count <= Bytes_.size() ? true : (Failed_ = true, false); }
    bool Take(size_t Count)
    {
        if (!Fits(Count)) return false;
        Cursor_ += Count;
        return true;
    }
    const std::vector<uint8_t>& Bytes_;
    size_t                      Cursor_ = 0;
    bool                        Failed_ = false;
};

void EncodeNesting(ArchiveWriter& Out, const GeometryNesting& Nesting)
{
    Out.U32(Nesting.SourceTriangles);
    Out.U32(Nesting.WeldedVertices);
    Out.U32(Nesting.HasUvs ? 1u : 0u);
    Out.U32(static_cast<uint32_t>(Nesting.Clusters.size()));
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        Out.U32(Node.Identity);
        Out.U32(Node.Tier);
        Out.List(Node.Finer);
        Out.List(Node.Coarser);
        Out.U32(static_cast<uint32_t>(Node.GroupIdentity));
        Out.U32(Node.FirstTriangle);
        Out.List(Node.Triangles);
        Out.Triple(Node.Minimum);
        Out.Triple(Node.Maximum);
        Out.Sphere(Node.Sphere);
        Out.Triple(Node.Cone.Axis);
        Out.F64(Node.Cone.Cutoff);
        Out.Triple(Node.TightMinimum);
        Out.Triple(Node.TightMaximum);
        Out.Sphere(Node.TightSphere);
        Out.F64(Node.Error);
        Out.F64(Node.CoarserError);
        Out.Sphere(Node.CoarserSphere);
        Out.U32(Node.ClassMask);
        Out.U32(Node.SharpEdgesInCluster);
    }
    Out.U32(static_cast<uint32_t>(Nesting.Tiers.size()));
    for (const NestingTier& Tier : Nesting.Tiers)
    {
        Out.U32(Tier.Tier);
        Out.List(Tier.Clusters);
        Out.U32(Tier.SharpEdgeCount);
        Out.U32(Tier.UvSeamEdgeCount);
        Out.Doubles(Tier.Positions);
        Out.List(Tier.Triangles);
        Out.List(Tier.Families);
        Out.Doubles(Tier.CornerUvs);
        Out.Bytes8(Tier.SharpMask);
    }
}

GeometryNesting DecodeNesting(ArchiveReader& In)
{
    GeometryNesting Nesting;
    Nesting.SourceTriangles = In.U32();
    Nesting.WeldedVertices  = In.U32();
    Nesting.HasUvs          = In.U32() != 0;
    const uint32_t ClusterCount = In.U32();
    for (uint32_t Index = 0; Index < ClusterCount && !In.Failed(); ++Index)
    {
        NestedCluster Node;
        Node.Identity      = In.U32();
        Node.Tier          = In.U32();
        Node.Finer         = In.List();
        Node.Coarser       = In.List();
        Node.GroupIdentity = static_cast<int32_t>(In.U32());
        Node.FirstTriangle = In.U32();
        Node.Triangles     = In.List();
        Node.TriangleCount = static_cast<uint32_t>(Node.Triangles.size());
        Node.Minimum       = In.Triple();
        Node.Maximum       = In.Triple();
        Node.Sphere        = In.Sphere();
        Node.Cone.Axis     = In.Triple();
        Node.Cone.Cutoff   = In.F64();
        Node.TightMinimum  = In.Triple();
        Node.TightMaximum  = In.Triple();
        Node.TightSphere   = In.Sphere();
        Node.Error         = In.F64();
        Node.CoarserError  = In.F64();
        Node.CoarserSphere = In.Sphere();
        Node.ClassMask     = In.U32();
        Node.SharpEdgesInCluster = In.U32();
        Nesting.Clusters.push_back(std::move(Node));
    }
    const uint32_t TierCount = In.U32();
    for (uint32_t Index = 0; Index < TierCount && !In.Failed(); ++Index)
    {
        NestingTier Tier;
        Tier.Tier            = In.U32();
        Tier.Clusters        = In.List();
        Tier.SharpEdgeCount  = In.U32();
        Tier.UvSeamEdgeCount = In.U32();
        Tier.Positions       = In.Doubles();
        Tier.Triangles       = In.List();
        Tier.Families        = In.List();
        Tier.CornerUvs       = In.Doubles();
        Tier.SharpMask       = In.Bytes8();
        Nesting.Tiers.push_back(std::move(Tier));
    }
    return Nesting;
}
} // namespace

uint32_t NestingProfileBits(const NestingOptions& Options) noexcept
{
    return (Options.MeasuredDeviation ? 1u : 0u) | (Options.NeedleGuard ? 2u : 0u) | (Options.ClassBorderCollapse ? 4u : 0u) |
           (Options.TriangleLimit == 128 && Options.VertexLimit == 256 && Options.GroupLimit == 8 && Options.TargetRatio == 0.5 && Options.TierLimit == 32
                ? 0u
                : 8u);
}

uint64_t DigestSurfaceSoup(const SurfaceSoup& Soup) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    auto Mix = [&](const void* Data, size_t Size) {
        const auto* Bytes = static_cast<const uint8_t*>(Data);
        for (size_t Index = 0; Index < Size; ++Index) Hash = (Hash ^ Bytes[Index]) * 1099511628211ull;
    };
    const uint64_t Count = Soup.Facets.size();
    Mix(&Count, sizeof(Count));
    for (const SoupFacet& Facet : Soup.Facets)
    {
        Mix(Facet.Corners.data(), sizeof(Facet.Corners));
        Mix(&Facet.Family, sizeof(Facet.Family));
        Mix(&Facet.SharpEdges, sizeof(Facet.SharpEdges));
        const uint8_t Flags = (Facet.CarriesUv ? 1u : 0u) | (Facet.CarriesNormals ? 2u : 0u);
        Mix(&Flags, 1);
        if (Facet.CarriesUv) Mix(Facet.CornerUv.data(), sizeof(Facet.CornerUv));
        if (Facet.CarriesNormals) Mix(Facet.Normals.data(), sizeof(Facet.Normals));
    }
    return Hash;
}

std::vector<uint8_t> EncodeNestingArchive(const NestingArchive& Archive)
{
    ArchiveWriter Out;
    for (char Character : std::string("FNA1")) Out.U8(static_cast<uint8_t>(Character));
    Out.U32(ArchiveVersion);
    Out.U32(Archive.Profile);
    Out.U32(static_cast<uint32_t>(Archive.Entries.size()));
    for (const NestingArchiveEntry& Entry : Archive.Entries)
    {
        Out.U32(Entry.FacetSet);
        Out.U32(Entry.FacetCount);
        Out.U64(Entry.Digest);
        ArchiveWriter Payload;
        EncodeNesting(Payload, Entry.Nesting);
        Out.U64(Payload.Bytes.size());
        Out.Bytes.insert(Out.Bytes.end(), Payload.Bytes.begin(), Payload.Bytes.end());
    }
    return Out.Bytes;
}

std::optional<NestingArchive> DecodeNestingArchive(const std::vector<uint8_t>& Bytes, std::string* Diagnostic)
{
    auto Fail = [&](const std::string& Reason) -> std::optional<NestingArchive> {
        if (Diagnostic) *Diagnostic = Reason;
        return std::nullopt;
    };
    if (Bytes.size() < 16 || std::memcmp(Bytes.data(), "FNA1", 4) != 0) return Fail("not a nesting archive");
    ArchiveReader In(Bytes);
    for (int Skip = 0; Skip < 4; ++Skip) In.U8();
    if (In.U32() != ArchiveVersion) return Fail("unsupported nesting archive version");
    NestingArchive Archive;
    Archive.Profile = In.U32();
    const uint32_t EntryCount = In.U32();
    for (uint32_t Index = 0; Index < EntryCount && !In.Failed(); ++Index)
    {
        NestingArchiveEntry Entry;
        Entry.FacetSet   = In.U32();
        Entry.FacetCount = In.U32();
        Entry.Digest     = In.U64();
        const uint64_t PayloadBytes = In.U64();
        const size_t   Start        = In.Cursor();
        Entry.Nesting = DecodeNesting(In);
        if (In.Failed() || In.Cursor() - Start != PayloadBytes) return Fail("nesting archive entry " + std::to_string(Index) + " is malformed");
        Archive.Entries.push_back(std::move(Entry));
    }
    if (In.Failed() || In.Cursor() != Bytes.size()) return Fail("nesting archive is truncated or carries trailing bytes");
    return Archive;
}

bool WriteNestingArchive(const std::filesystem::path& Path, const NestingArchive& Archive, std::string* Diagnostic)
{
    const std::vector<uint8_t> Bytes = EncodeNestingArchive(Archive);
    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    if (!File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size())))
    {
        if (Diagnostic) *Diagnostic = "cannot write " + Path.string();
        return false;
    }
    return true;
}

std::optional<NestingArchive> ReadNestingArchive(const std::filesystem::path& Path, std::string* Diagnostic)
{
    std::ifstream File(Path, std::ios::binary);
    if (!File)
    {
        if (Diagnostic) *Diagnostic = "no nesting archive at " + Path.string();
        return std::nullopt;
    }
    const std::vector<uint8_t> Bytes((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    return DecodeNestingArchive(Bytes, Diagnostic);
}
} // namespace Frontier::VirtualizedGeometry
