//============================================================================================================================================
//                                                    SHADERBALLGEOMETRY.CPP
//============================================================================================================================================
// 📦 Reads the SBM1 mesh container the ShaderBall converter writes.

#include "ShaderBallGeometry.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier {

namespace {

// 'SBM1' little-endian. Bumping this is how a format change announces itself instead of reading as garbage.
constexpr uint32_t kShaderBallMagic = 0x314D4253u;

// px py pz · nx ny nz · u v, all f32. 32 bytes, matching the converter's struct.Struct("<8f").
constexpr size_t kAssetVertexFloats = 8u;

struct FileCloser
{
    std::FILE* Handle = nullptr;
    ~FileCloser() noexcept { if (Handle) std::fclose(Handle); }
};

bool Fail(std::string* Error, const std::string& Reason) noexcept
{
    if (Error) *Error = Reason;
    return false;
}

} // namespace

bool ShaderBallGeometry::Load(const std::string& Path, GeometryStructure& Mesh, std::string* Error) noexcept
{
    FileCloser File;
#if defined(_MSC_VER)
    if (fopen_s(&File.Handle, Path.c_str(), "rb") != 0) File.Handle = nullptr;
#else
    File.Handle = std::fopen(Path.c_str(), "rb");
#endif
    if (!File.Handle) return Fail(Error, "cannot open '" + Path + "'");

    uint32_t Header[3]{};
    float    Height = 0.0f;
    if (std::fread(Header, sizeof(uint32_t), 3u, File.Handle) != 3u
     || std::fread(&Height, sizeof(float), 1u, File.Handle) != 1u)
        return Fail(Error, "'" + Path + "' is truncated in its header");
    if (Header[0] != kShaderBallMagic)
        return Fail(Error, "'" + Path + "' is not an SBM1 mesh (bad magic)");

    const uint32_t VertexCount = Header[1];
    const uint32_t IndexCount  = Header[2];
    if (VertexCount == 0u || IndexCount == 0u || IndexCount % 3u != 0u)
        return Fail(Error, "'" + Path + "' declares an empty or non-triangular mesh");

    std::vector<float> Raw(static_cast<size_t>(VertexCount) * kAssetVertexFloats);
    if (std::fread(Raw.data(), sizeof(float), Raw.size(), File.Handle) != Raw.size())
        return Fail(Error, "'" + Path + "' is truncated in its vertex block");

    std::vector<uint32_t> Indices(IndexCount);
    if (std::fread(Indices.data(), sizeof(uint32_t), Indices.size(), File.Handle) != Indices.size())
        return Fail(Error, "'" + Path + "' is truncated in its index block");

    // Every index must land inside the vertex block. A file that passes the magic but indexes past the end would
    //    otherwise read whatever follows the allocation during the first cluster build.
    for (uint32_t Index : Indices)
        if (Index >= VertexCount)
            return Fail(Error, "'" + Path + "' has an index past the end of its vertex block");

    std::vector<VertexRecord> Vertices(VertexCount);
    for (uint32_t V = 0u; V < VertexCount; ++V)
    {
        const float* Source = Raw.data() + static_cast<size_t>(V) * kAssetVertexFloats;
        VertexRecord& Target = Vertices[V];
        Target.SpatialLocation     = Vector3{ Source[0], Source[1], Source[2] };
        Target.NormalDirection     = Vector3{ Source[3], Source[4], Source[5] };
        Target.TextureCoordinateU  = Source[6];
        Target.TextureCoordinateV  = Source[7];
        // Tangents are not authored in the container. SceneCodec leaves the same default for a glTF without
        //    them, and ResolveMaterial derives a frame from the UV gradient when the tangent is degenerate.
        Target.TangentDirection    = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };
    }

    Mesh.AppendVertices(Vertices.data(), Vertices.size());
    Mesh.AppendIndices(Indices.data(), Indices.size());
    return true;
}

bool ShaderBallGeometry::LoadResolved(const std::string& Path, GeometryStructure& Mesh,
                                      std::string* Resolved, std::string* Error) noexcept
{
    // A harness may run from the repository root, from a build directory, or from a seated engine checkout.
    //    Rather than making each one pass a path it computed, try the obvious roots in order.
    static const char* const kRoots[] = { "", "../", "../../", "../../../", "../../../../" };
    std::string LastError;
    for (const char* Root : kRoots)
    {
        const std::string Candidate = std::string(Root) + Path;
        std::string Reason;
        if (Load(Candidate, Mesh, &Reason))
        {
            if (Resolved) *Resolved = Candidate;
            return true;
        }
        if (LastError.empty()) LastError = Reason;
    }
    return Fail(Error, LastError.empty() ? ("cannot resolve '" + Path + "'") : LastError);
}

} // namespace Frontier
