/*==========================================================================================================================================
                                                        SURFACESOUPARCHIVE.CPP
==========================================================================================================================================*/
// 🧩 Corpus soup decoding (zlib through stb_image) and the SHA-256 digest of the parity sections.
#include "SurfaceSoupArchive.h"
#include <stb_image.h>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
namespace Frontier::VirtualizedGeometry
{
namespace
{
class ByteReader
{
  public:
    explicit ByteReader(const std::vector<uint8_t>& Bytes) : Bytes_(Bytes) {}
    [[nodiscard]] bool Has(size_t Count) const noexcept { return Cursor_ + Count <= Bytes_.size(); }
    uint8_t  U8() { return Bytes_[Cursor_++]; }
    uint32_t U32()
    {
        uint32_t Value = 0;
        for (int Shift = 0; Shift < 32; Shift += 8) Value |= static_cast<uint32_t>(Bytes_[Cursor_++]) << Shift;
        return Value;
    }
    double F64()
    {
        uint64_t Bits = 0;
        for (int Shift = 0; Shift < 64; Shift += 8) Bits |= static_cast<uint64_t>(Bytes_[Cursor_++]) << Shift;
        return std::bit_cast<double>(Bits);
    }
  private:
    const std::vector<uint8_t>& Bytes_;
    size_t                      Cursor_ = 0;
};
} // namespace

std::optional<SurfaceSoup> ReadSurfaceSoup(const std::filesystem::path& Path, std::string* Diagnostic)
{
    auto Fail = [&](const std::string& Reason) -> std::optional<SurfaceSoup> {
        if (Diagnostic) *Diagnostic = Path.string() + ": " + Reason;
        return std::nullopt;
    };
    std::ifstream File(Path, std::ios::binary);
    if (!File) return Fail("cannot open");
    const std::vector<char> Compressed((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    int   Length  = 0;
    char* Decoded = stbi_zlib_decode_malloc(Compressed.data(), static_cast<int>(Compressed.size()), &Length);
    if (!Decoded) return Fail("zlib stream is malformed");
    const std::vector<uint8_t> Bytes(reinterpret_cast<uint8_t*>(Decoded), reinterpret_cast<uint8_t*>(Decoded) + Length);
    std::free(Decoded);
    ByteReader Reader(Bytes);
    if (!Reader.Has(16) || std::memcmp(Bytes.data(), "FSU1", 4) != 0) return Fail("not an FSU1 soup");
    for (int Skip = 0; Skip < 4; ++Skip) Reader.U8();
    const uint32_t FacetCount = Reader.U32(), PositionCount = Reader.U32(), Flags = Reader.U32();
    const bool CarriesUv = (Flags & 1u) != 0, CarriesNormals = (Flags & 2u) != 0;
    const size_t FacetBytes = 17 + (CarriesUv ? 48 : 0) + (CarriesNormals ? 72 : 0);
    if (!Reader.Has(static_cast<size_t>(PositionCount) * 24 + static_cast<size_t>(FacetCount) * FacetBytes)) return Fail("truncated");
    std::vector<Point3> Positions(PositionCount);
    for (Point3& P : Positions)
        for (double& Axis : P) Axis = Reader.F64();
    SurfaceSoup Soup;
    Soup.Facets.resize(FacetCount);
    for (SoupFacet& Facet : Soup.Facets)
    {
        for (Point3& Corner : Facet.Corners)
        {
            const uint32_t Index = Reader.U32();
            if (Index >= PositionCount) return Fail("position index out of range");
            Corner = Positions[Index];
        }
        Facet.Family     = Reader.U32();
        Facet.SharpEdges = Reader.U8();
        Facet.CarriesUv  = CarriesUv;
        if (CarriesUv)
            for (Point2& Uv : Facet.CornerUv)
            {
                Uv[0] = Reader.F64();
                Uv[1] = Reader.F64();
            }
        Facet.CarriesNormals = CarriesNormals;
        if (CarriesNormals)
            for (Point3& Normal : Facet.Normals)
                for (double& Axis : Normal) Axis = Reader.F64();
    }
    return Soup;
}

std::filesystem::path LocateExhibitFolder()
{
    if (const char* Override = std::getenv("FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT")) return Override;
#ifdef FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT
    return FRONTIER_VIRTUALIZED_GEOMETRY_EXHIBIT;
#else
    return std::filesystem::path("Exhibits") / "VirtualizedGeometry";
#endif
}

void SectionStream::U32(uint32_t Value)
{
    for (int Shift = 0; Shift < 32; Shift += 8) Bytes.push_back(static_cast<uint8_t>(Value >> Shift));
}
void SectionStream::F64(double Value)
{
    const uint64_t Bits = std::bit_cast<uint64_t>(Value);
    for (int Shift = 0; Shift < 64; Shift += 8) Bytes.push_back(static_cast<uint8_t>(Bits >> Shift));
}
void SectionStream::U32List(const std::vector<uint32_t>& Values)
{
    U32(static_cast<uint32_t>(Values.size()));
    for (uint32_t Value : Values) U32(Value);
}
void SectionStream::F64Triple(const Point3& Values)
{
    for (double Value : Values) F64(Value);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHA-256 (FIPS 180-4)
//------------------------------------------------------------------------------------------------------------------------

std::string DigestSha256(const std::vector<uint8_t>& Bytes)
{
    static constexpr std::array<uint32_t, 64> RoundConstants = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<uint32_t, 8> Hash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> Message = Bytes;
    const uint64_t BitLength = static_cast<uint64_t>(Bytes.size()) * 8;
    Message.push_back(0x80);
    while (Message.size() % 64 != 56) Message.push_back(0);
    for (int Shift = 56; Shift >= 0; Shift -= 8) Message.push_back(static_cast<uint8_t>(BitLength >> Shift));
    for (size_t Block = 0; Block < Message.size(); Block += 64)
    {
        std::array<uint32_t, 64> W{};
        for (int T = 0; T < 16; ++T)
            W[T] = (static_cast<uint32_t>(Message[Block + T * 4]) << 24) | (static_cast<uint32_t>(Message[Block + T * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(Message[Block + T * 4 + 2]) << 8) | static_cast<uint32_t>(Message[Block + T * 4 + 3]);
        for (int T = 16; T < 64; ++T)
        {
            const uint32_t S0 = std::rotr(W[T - 15], 7) ^ std::rotr(W[T - 15], 18) ^ (W[T - 15] >> 3);
            const uint32_t S1 = std::rotr(W[T - 2], 17) ^ std::rotr(W[T - 2], 19) ^ (W[T - 2] >> 10);
            W[T] = W[T - 16] + S0 + W[T - 7] + S1;
        }
        std::array<uint32_t, 8> V = Hash;
        for (int T = 0; T < 64; ++T)
        {
            const uint32_t S1     = std::rotr(V[4], 6) ^ std::rotr(V[4], 11) ^ std::rotr(V[4], 25);
            const uint32_t Choice = (V[4] & V[5]) ^ (~V[4] & V[6]);
            const uint32_t First  = V[7] + S1 + Choice + RoundConstants[T] + W[T];
            const uint32_t S0     = std::rotr(V[0], 2) ^ std::rotr(V[0], 13) ^ std::rotr(V[0], 22);
            const uint32_t Major  = (V[0] & V[1]) ^ (V[0] & V[2]) ^ (V[1] & V[2]);
            const uint32_t Second = S0 + Major;
            V = {First + Second, V[0], V[1], V[2], V[3] + First, V[4], V[5], V[6]};
        }
        for (int Index = 0; Index < 8; ++Index) Hash[Index] += V[Index];
    }
    static constexpr char Hex[] = "0123456789abcdef";
    std::string Text;
    for (uint32_t Word : Hash)
        for (int Shift = 28; Shift >= 0; Shift -= 4) Text.push_back(Hex[(Word >> Shift) & 15u]);
    return Text;
}
} // namespace Frontier::VirtualizedGeometry
