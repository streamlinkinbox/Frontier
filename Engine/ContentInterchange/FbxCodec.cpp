//============================================================================================================================================
//                                                         FBXCODEC.CPP
//============================================================================================================================================
// 🧩 ufbx decode (ufbx.c is its own translation unit in the build lists).

#include "FbxCodec.h"
#include "MaterialCodec.h"
#include "../GeometricRaster/ClipProjection.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace Frontier {

namespace {

Matrix4x4 ProjectionFromUfbx(const ufbx_matrix& M) noexcept
{
    const float Columns[16] =
    {
        static_cast<float>(M.m00), static_cast<float>(M.m10), static_cast<float>(M.m20), 0.0f,
        static_cast<float>(M.m01), static_cast<float>(M.m11), static_cast<float>(M.m21), 0.0f,
        static_cast<float>(M.m02), static_cast<float>(M.m12), static_cast<float>(M.m22), 0.0f,
        static_cast<float>(M.m03), static_cast<float>(M.m13), static_cast<float>(M.m23), 1.0f,
    };
    return ProjectionFromColumns(Columns);
}

// Registers a ufbx texture: embedded content first (FBX 7.x can carry PNG/JPEG bytes), else the resolved filename.
uint32_t RegisterFbxTexture(const ufbx_texture* T, bool Linear, TextureIndex* Textures) noexcept
{
    if (!Textures || !T) return kMaterialTextureNone;
    if (T->content.size > 0u && T->content.data)
        return Textures->RegisterEncoded(std::string(T->name.data, T->name.length), static_cast<const uint8_t*>(T->content.data), T->content.size, Linear);
    if (T->filename.length > 0u)
        return Textures->RegisterPath(std::string(T->filename.data, T->filename.length), Linear);
    if (T->absolute_filename.length > 0u)
        return Textures->RegisterPath(std::string(T->absolute_filename.data, T->absolute_filename.length), Linear);
    return kMaterialTextureNone;
}

} // namespace

bool FbxCodec::Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept
{
    Out.Clear();

    ufbx_load_opts Options{};
    Options.target_axes              = ufbx_axes_right_handed_z_up;
    Options.target_unit_meters       = 1.0f;
    Options.space_conversion         = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;   // bake the swap/scale into geometry + root, not a mirror
    Options.generate_missing_normals = true;
    Options.ignore_animation         = true;
    Options.load_external_files      = false;                                   // textures are decoded by TextureIndex, not ufbx
    ufbx_error LoadError{};
    ufbx_scene* Scene = ufbx_load_file(Path.c_str(), &Options, &LoadError);
    if (!Scene)
    {
        if (Error) { char Buffer[512]; ufbx_format_error(Buffer, sizeof(Buffer), &LoadError); *Error = std::string("ufbx_load_file failed: ") + Buffer; }
        return false;
    }

    // Materials: ufbx order + fallback slot.
    MaterialDecodeConfiguration MaterialConfig;
    MaterialConfig.EmissiveRadiance = Config.EmissiveRadiance;
    const FbxTextureResolver Resolve = [&](const void* T, bool Linear) { return RegisterFbxTexture(static_cast<const ufbx_texture*>(T), Linear, Textures); };
    std::unordered_map<const ufbx_material*, uint32_t> MaterialSlot;
    for (size_t I = 0; I < Scene->materials.count; ++I)
        MaterialSlot[Scene->materials.data[I]] = Out.RegisterMaterial(MaterialCodec::DecodeFbx(Scene->materials.data[I], MaterialConfig, Resolve));
    const uint32_t FallbackSlot = Out.RegisterMaterial(MaterialCodec::DecodeFbx(nullptr, MaterialConfig, nullptr));

    // Cameras and lights (ufbx order), attached below.
    std::unordered_map<const ufbx_camera*, uint32_t> CameraSlot;
    for (size_t I = 0; I < Scene->cameras.count; ++I)
    {
        const ufbx_camera& C = *Scene->cameras.data[I];
        CameraRecord R; R.Name = std::string(C.name.data, C.name.length);
        R.AspectRatio = static_cast<float>(C.aspect_ratio);
        R.NearPlane   = static_cast<float>(C.near_plane) * Config.UniformScale;
        R.FarPlane    = static_cast<float>(C.far_plane)  * Config.UniformScale;
        if (C.projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) { R.Orthographic = true; R.OrthographicHalfHeight = static_cast<float>(C.orthographic_size.y) * 0.5f * Config.UniformScale; }
        else R.VerticalFieldOfView = static_cast<float>(C.field_of_view_deg.y) * 3.14159265358979f / 180.0f;
        CameraSlot[&C] = Out.RegisterCamera(R, kPlacementNone);
    }
    std::unordered_map<const ufbx_light*, uint32_t> LightSlot;
    for (size_t I = 0; I < Scene->lights.count; ++I)
    {
        const ufbx_light& L = *Scene->lights.data[I];
        PunctualLuminaireRecord R; R.Name = std::string(L.name.data, L.name.length);
        R.Category = L.type == UFBX_LIGHT_DIRECTIONAL ? PunctualLuminaireCategory::Directional : L.type == UFBX_LIGHT_SPOT ? PunctualLuminaireCategory::Spot : PunctualLuminaireCategory::Point;
        R.Colour[0] = static_cast<float>(L.color.x); R.Colour[1] = static_cast<float>(L.color.y); R.Colour[2] = static_cast<float>(L.color.z);
        R.Intensity = static_cast<float>(L.intensity);   // ufbx already divides "Intensity" by 100 → ~[-] multiplier; ⚠️ not photometric (FBX has no unit)
        R.InnerConeAngle = static_cast<float>(L.inner_angle) * 3.14159265358979f / 180.0f;
        R.OuterConeAngle = static_cast<float>(L.outer_angle) * 3.14159265358979f / 180.0f;
        LightSlot[&L] = Out.RegisterPunctualLuminaire(R, kPlacementNone);
    }

    // Meshes: one GeometryStructure per (mesh, material part), object space (ufbx has already applied the axis/unit
    //    conversion to the vertex data under MODIFY_GEOMETRY).
    struct DecodedPart { GeometryStructure Geometry; uint32_t MaterialPart = 0u; uint32_t Flags = 0u; };   // GeometryStructure is non-copyable → deque
    std::map<const ufbx_mesh*, std::deque<DecodedPart>> Decoded;
    uint32_t Skipped = 0u;
    auto DecodeMesh = [&](const ufbx_mesh& Mesh) -> std::deque<DecodedPart>&
    {
        auto Existing = Decoded.find(&Mesh);
        if (Existing != Decoded.end()) return Existing->second;
        std::deque<DecodedPart>& Parts = Decoded[&Mesh];
        std::vector<uint32_t> Scratch(std::max<size_t>(Mesh.max_face_triangles * 3u, 3u));
        for (size_t P = 0; P < Mesh.material_parts.count; ++P)
        {
            const ufbx_mesh_part& Part = Mesh.material_parts.data[P];
            if (Part.num_triangles == 0u) continue;
            std::vector<VertexRecord> Vertices; Vertices.reserve(Part.num_triangles * 3u);
            std::vector<uint32_t>     Indices;  Indices.reserve(Part.num_triangles * 3u);
            // Weld identical (position, normal, uv, tangent) tuples so the raster's vertex buffer stays compact.
            std::map<std::array<float, 12>, uint32_t> Weld;
            for (size_t F = 0; F < Part.face_indices.count; ++F)
            {
                const ufbx_face Face = Mesh.faces.data[Part.face_indices.data[F]];
                if (Face.num_indices < 3u) { ++Skipped; continue; }
                const uint32_t TriangleCount = ufbx_triangulate_face(Scratch.data(), Scratch.size(), &Mesh, Face);
                for (uint32_t T = 0; T < TriangleCount * 3u; ++T)
                {
                    const uint32_t Corner = Scratch[T];
                    const ufbx_vec3 Pos = ufbx_get_vertex_vec3(&Mesh.vertex_position, Corner);
                    const ufbx_vec3 Nor = Mesh.vertex_normal.exists ? ufbx_get_vertex_vec3(&Mesh.vertex_normal, Corner) : ufbx_vec3{ 0.0, 0.0, 1.0 };
                    const ufbx_vec2 Uv  = Mesh.vertex_uv.exists ? ufbx_get_vertex_vec2(&Mesh.vertex_uv, Corner) : ufbx_vec2{ 0.0, 0.0 };
                    const ufbx_vec3 Tan = Mesh.vertex_tangent.exists ? ufbx_get_vertex_vec3(&Mesh.vertex_tangent, Corner) : ufbx_vec3{ 1.0, 0.0, 0.0 };
                    std::array<float, 12> Key = { static_cast<float>(Pos.x), static_cast<float>(Pos.y), static_cast<float>(Pos.z),
                                                  static_cast<float>(Nor.x), static_cast<float>(Nor.y), static_cast<float>(Nor.z),
                                                  static_cast<float>(Uv.x),  1.0f - static_cast<float>(Uv.y),   // FBX UV origin bottom-left → glTF/engine top-left
                                                  static_cast<float>(Tan.x), static_cast<float>(Tan.y), static_cast<float>(Tan.z), 1.0f };
                    auto Found = Weld.find(Key);
                    if (Found == Weld.end())
                    {
                        VertexRecord R{};
                        R.SpatialLocation    = Vector3{ Key[0], Key[1], Key[2] };
                        R.NormalDirection    = Vector3{ Key[3], Key[4], Key[5] };
                        R.TextureCoordinateU = Key[6]; R.TextureCoordinateV = Key[7];
                        R.TangentDirection   = Vector4{ Key[8], Key[9], Key[10], Key[11] };
                        Found = Weld.emplace(Key, static_cast<uint32_t>(Vertices.size())).first;
                        Vertices.push_back(R);
                    }
                    Indices.push_back(Found->second);
                }
            }
            if (Indices.size() < 3u) continue;
            Parts.emplace_back();
            Parts.back().MaterialPart = Part.index;
            Parts.back().Geometry.AppendVertices(Vertices.data(), Vertices.size());
            Parts.back().Geometry.AppendIndices(Indices.data(), Indices.size());
        }
        return Parts;
    };

    // Placements: every node in tree order (ufbx lists nodes so that parents precede children; the root is node 0).
    Matrix4x4 Scale;
    Scale.Columns[0][0] = Scale.Columns[1][1] = Scale.Columns[2][2] = Config.UniformScale;
    std::unordered_map<const ufbx_node*, uint32_t> PlacementOf;
    for (size_t N = 0; N < Scene->nodes.count; ++N)
    {
        const ufbx_node& Node = *Scene->nodes.data[N];
        if (Node.is_root) continue;   // the ufbx root carries only the axis conversion; engine roots are its children
        const uint32_t Ancestor = (Node.parent && !Node.parent->is_root) ? PlacementOf[Node.parent] : kPlacementNone;
        const Matrix4x4 World = MultiplyProjection(Scale, ProjectionFromUfbx(Node.node_to_world));
        const Matrix4x4 Local = Ancestor == kPlacementNone ? World : ProjectionFromUfbx(Node.node_to_parent);
        const uint32_t Placement = Out.RegisterPlacement(std::string(Node.name.data, Node.name.length), Ancestor, Local, World);
        PlacementOf[&Node] = Placement;
        if (Node.camera) Out.AttachCamera(Placement, CameraSlot[Node.camera]);
        if (Node.light)  Out.AttachPunctualLuminaire(Placement, LightSlot[Node.light]);
        if (!Node.mesh) continue;

        const uint32_t FirstInstance = static_cast<uint32_t>(Out.QueryInstances().size());
        const Matrix4x4 GeometryWorld = MultiplyProjection(Scale, ProjectionFromUfbx(Node.geometry_to_world));   // includes the geometric (pivot) transform
        for (DecodedPart& Part : DecodeMesh(*Node.mesh))
        {
            // Per-node material override: node.materials[] wins over mesh.materials[] when present.
            const ufbx_material* Material = nullptr;
            if (Part.MaterialPart < Node.materials.count)       Material = Node.materials.data[Part.MaterialPart];
            else if (Part.MaterialPart < Node.mesh->materials.count) Material = Node.mesh->materials.data[Part.MaterialPart];
            const auto Slot = Material ? MaterialSlot.find(Material) : MaterialSlot.end();
            const uint32_t MaterialIndex = Slot != MaterialSlot.end() ? Slot->second : FallbackSlot;
            const uint32_t Flags = (Material && Material->features.double_sided.enabled) ? InstanceFlagDoubleSided : 0u;
            (void)Out.RegisterInstance(Part.Geometry, GeometryWorld, MaterialIndex, Flags);
        }
        const uint32_t InstanceCount = static_cast<uint32_t>(Out.QueryInstances().size()) - FirstInstance;
        if (InstanceCount) Out.AttachInstances(Placement, FirstInstance, InstanceCount);
    }

    ufbx_free_scene(Scene);
    std::vector<std::string> Report;
    Out.Finalise(Config.SlabLimit, &Report);

    if (Out.QueryTriangleCount() == 0u) { if (Error) *Error = "no triangle faces found"; return false; }
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
