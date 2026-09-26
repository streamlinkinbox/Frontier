//============================================================================================================================================
//                                                         OBJCODEC.CPP
//============================================================================================================================================
// 🧩 fast_obj decode. The only TU that defines FAST_OBJ_IMPLEMENTATION.

#define FAST_OBJ_IMPLEMENTATION
#include <fast_obj.h>

#include "ObjCodec.h"
#include "MaterialCodec.h"
#include "../GeometricRaster/ClipProjection.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <vector>

namespace Frontier {

bool ObjCodec::Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept
{
    Out.Clear();
    fastObjMesh* Mesh = fast_obj_read(Path.c_str());
    if (!Mesh) { if (Error) *Error = "fast_obj_read failed (file missing or unreadable)"; return false; }
    const std::filesystem::path Directory = std::filesystem::path(Path).parent_path();

    // Materials: fast_obj order (slot 0 is fast_obj's default material when the file has none) + fallback slot.
    MaterialDecodeConfiguration MaterialConfig;
    MaterialConfig.EmissiveRadiance = Config.EmissiveRadiance;
    const PathTextureResolver Resolve = [&](const std::string& TexturePath, bool Linear)
    {
        if (!Textures || TexturePath.empty()) return kMaterialTextureNone;
        const std::filesystem::path P(TexturePath);
        return Textures->RegisterPath((P.is_absolute() ? P : Directory / P).lexically_normal().string(), Linear);
    };
    auto TexturePath = [&](unsigned int Slot) -> std::string { return (Slot < Mesh->texture_count && Mesh->textures[Slot].path) ? Mesh->textures[Slot].path : std::string(); };
    std::vector<uint32_t> MaterialSlot(Mesh->material_count);
    for (unsigned int I = 0; I < Mesh->material_count; ++I)
    {
        const fastObjMaterial& M = Mesh->materials[I];
        MaterialCodec::ObjMaterialSource S;
        S.Name = M.name ? M.name : ("material_" + std::to_string(I));
        std::copy(M.Kd, M.Kd + 3, S.Kd); std::copy(M.Ks, M.Ks + 3, S.Ks); std::copy(M.Ke, M.Ke + 3, S.Ke); std::copy(M.Tf, M.Tf + 3, S.Tf);
        S.Ns = M.Ns; S.Ni = M.Ni; S.d = M.d; S.Illum = M.illum;
        S.MapKd = TexturePath(M.map_Kd); S.MapKs = TexturePath(M.map_Ks); S.MapKe = TexturePath(M.map_Ke);
        S.MapD  = TexturePath(M.map_d);  S.MapBump = TexturePath(M.map_bump); S.MapNs = TexturePath(M.map_Ns);
        MaterialSlot[I] = Out.RegisterMaterial(MaterialCodec::DecodeObj(S, MaterialConfig, Resolve));
    }
    const uint32_t FallbackSlot = Out.RegisterMaterial(MaterialCodec::DecodeObj(MaterialCodec::ObjMaterialSource{ "fallback" }, MaterialConfig, nullptr));

    const Matrix4x4 AxisSwap = ConstructGltfToWorldProjection();
    Matrix4x4 Scale;
    Scale.Columns[0][0] = Scale.Columns[1][1] = Scale.Columns[2][2] = Config.UniformScale;
    const Matrix4x4 World = MultiplyProjection(Scale, AxisSwap);

    // Groups: objects when the file has them, else groups, else one implicit group over every face.
    const bool UseObjects = Mesh->object_count > 1u || (Mesh->object_count == 1u && Mesh->objects[0].name && Mesh->objects[0].name[0]);
    const fastObjGroup* Groups = UseObjects ? Mesh->objects : Mesh->groups;
    unsigned int GroupCount = UseObjects ? Mesh->object_count : Mesh->group_count;
    fastObjGroup Whole{}; Whole.face_count = Mesh->face_count;
    if (GroupCount == 0u) { Groups = &Whole; GroupCount = 1u; }

    uint32_t Skipped = 0u;
    for (unsigned int G = 0; G < GroupCount; ++G)
    {
        const fastObjGroup& Group = Groups[G];
        if (Group.face_count == 0u) continue;
        const std::string Name = (Group.name && Group.name[0]) ? Group.name : ("group_" + std::to_string(G));
        const uint32_t Placement = Out.RegisterPlacement(Name, kPlacementNone, World, World);
        const uint32_t FirstInstance = static_cast<uint32_t>(Out.QueryInstances().size());

        // Split the group by material; weld identical (p, t, n) tuples inside each split.
        struct Split { std::vector<VertexRecord> Vertices; std::vector<uint32_t> Indices; std::map<std::array<unsigned int, 3>, uint32_t> Weld; };
        std::map<unsigned int, Split> Splits;
        unsigned int Index = Group.index_offset;
        for (unsigned int F = 0; F < Group.face_count; ++F)
        {
            const unsigned int Face = Group.face_offset + F;
            const unsigned int Corners = Mesh->face_vertices[Face];
            const unsigned int Material = Mesh->face_materials[Face];
            if (Corners < 3u) { ++Skipped; Index += Corners; continue; }
            Split& S = Splits[Material];
            auto Corner = [&](unsigned int C) -> uint32_t
            {
                const fastObjIndex& I = Mesh->indices[Index + C];
                const std::array<unsigned int, 3> Key = { I.p, I.t, I.n };
                auto Found = S.Weld.find(Key);
                if (Found != S.Weld.end()) return Found->second;
                VertexRecord R{};
                R.SpatialLocation    = Vector3{ Mesh->positions[3u * I.p], Mesh->positions[3u * I.p + 1u], Mesh->positions[3u * I.p + 2u] };   // OBJ axes; World carries the swap
                R.NormalDirection    = I.n ? Vector3{ Mesh->normals[3u * I.n], Mesh->normals[3u * I.n + 1u], Mesh->normals[3u * I.n + 2u] } : Vector3{ 0.0f, 1.0f, 0.0f };
                R.TextureCoordinateU = Mesh->texcoords[2u * I.t];
                R.TextureCoordinateV = 1.0f - Mesh->texcoords[2u * I.t + 1u];   // OBJ v is bottom-up
                R.TangentDirection   = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };
                const uint32_t Slot = static_cast<uint32_t>(S.Vertices.size());
                S.Vertices.push_back(R); S.Weld.emplace(Key, Slot);
                return Slot;
            };
            // Fan-triangulate (OBJ polygons are planar-convex by convention).
            const uint32_t V0 = Corner(0u);
            uint32_t Previous = Corner(1u);
            for (unsigned int C = 2u; C < Corners; ++C)
            {
                const uint32_t Current = Corner(C);
                S.Indices.push_back(V0); S.Indices.push_back(Previous); S.Indices.push_back(Current);
                Previous = Current;
            }
            Index += Corners;
        }
        for (auto& [Material, S] : Splits)
        {
            if (S.Indices.size() < 3u) continue;
            // Faces without normals get flat normals from the triangle winding (fast_obj leaves n = 0 → dummy).
            bool HasNormals = false;
            for (const auto& Key : S.Weld) if (Key.first[2] != 0u) { HasNormals = true; break; }
            if (!HasNormals)
            {
                for (size_t T = 0; T + 2 < S.Indices.size(); T += 3)
                {
                    VertexRecord& A = S.Vertices[S.Indices[T]]; VertexRecord& B = S.Vertices[S.Indices[T + 1]]; VertexRecord& C = S.Vertices[S.Indices[T + 2]];
                    const Vector3 N = OrientationClassifier::CrossProduct(B.SpatialLocation - A.SpatialLocation, C.SpatialLocation - A.SpatialLocation).Normalized();
                    A.NormalDirection = B.NormalDirection = C.NormalDirection = N;   // shared verts take the last face — acceptable for flat-lit OBJ
                }
            }
            GeometryStructure Geometry;
            Geometry.AppendVertices(S.Vertices.data(), S.Vertices.size());
            Geometry.AppendIndices(S.Indices.data(), S.Indices.size());
            const uint32_t MaterialIndex = Material < MaterialSlot.size() ? MaterialSlot[Material] : FallbackSlot;
            (void)Out.RegisterInstance(Geometry, World, MaterialIndex, 0u);
        }
        const uint32_t InstanceCount = static_cast<uint32_t>(Out.QueryInstances().size()) - FirstInstance;
        if (InstanceCount) Out.AttachInstances(Placement, FirstInstance, InstanceCount);
    }

    fast_obj_destroy(Mesh);
    std::vector<std::string> Report;
    Out.Finalise(Config.SlabLimit, &Report);

    if (Out.QueryTriangleCount() == 0u) { if (Error) *Error = "no faces found"; return false; }
    if (Error)
    {
        std::string Warning;
        if (Skipped) Warning += std::to_string(Skipped) + " degenerate face(s) skipped; ";
        for (const std::string& Line : Report) Warning += Line + "; ";
        if (!Warning.empty()) Warning.resize(Warning.size() - 2u);
        *Error = Warning;
    }
    return true;
}

} // namespace Frontier
