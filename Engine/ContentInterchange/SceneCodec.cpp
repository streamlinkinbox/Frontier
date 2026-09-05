//============================================================================================================================================
//                                                        SCENECODEC.CPP
//============================================================================================================================================
// 🧩 glTF 2.0 decode via cgltf and a minimal embedded-buffer encoder.

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "SceneCodec.h"
#include "MaterialCodec.h"
#include "../GeometricRaster/ClipProjection.h"
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        DECODE
//------------------------------------------------------------------------------------------------------------------------

namespace {

const cgltf_accessor* FindAttribute(const cgltf_primitive& Primitive, cgltf_attribute_type Type, int Set = 0) noexcept
{
    for (cgltf_size I = 0; I < Primitive.attributes_count; ++I)
        if (Primitive.attributes[I].type == Type && Primitive.attributes[I].index == Set) return Primitive.attributes[I].data;
    return nullptr;
}

// Registers a glTF texture's image in the TextureIndex: file URI (relative to the .gltf) or GLB buffer view.
uint32_t RegisterGltfTexture(const cgltf_texture_view& View, bool Linear, TextureIndex* Textures, const std::filesystem::path& Directory) noexcept
{
    if (!Textures || !View.texture || !View.texture->image) return kMaterialTextureNone;
    const cgltf_image& Image = *View.texture->image;
    if (Image.uri)
    {
        if (std::strncmp(Image.uri, "data:", 5) == 0) return kMaterialTextureNone;   // base64 data URI images: not supported yet (reported by caller)
        std::string Uri = Image.uri;
        cgltf_decode_uri(Uri.data()); Uri.resize(std::strlen(Uri.c_str()));
        return Textures->RegisterPath((Directory / Uri).lexically_normal().string(), Linear);
    }
    if (Image.buffer_view)
    {
        const uint8_t* Bytes = cgltf_buffer_view_data(Image.buffer_view);
        if (!Bytes) return kMaterialTextureNone;
        return Textures->RegisterEncoded(Image.name ? Image.name : ("image_" + std::to_string(Image.buffer_view->offset)), Bytes, Image.buffer_view->size, Linear);
    }
    return kMaterialTextureNone;
}

} // namespace

bool SceneCodec::Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept
{
    Out.Clear();
    const std::filesystem::path Directory = std::filesystem::path(Path).parent_path();

    cgltf_options Options{};
    cgltf_data*   Data = nullptr;
    cgltf_result  Result = cgltf_parse_file(&Options, Path.c_str(), &Data);
    if (Result != cgltf_result_success) { if (Error) *Error = "cgltf_parse_file failed (" + std::to_string(static_cast<int>(Result)) + ")"; return false; }
    Result = cgltf_load_buffers(&Options, Data, Path.c_str());
    if (Result != cgltf_result_success) { if (Error) *Error = "cgltf_load_buffers failed (" + std::to_string(static_cast<int>(Result)) + ")"; cgltf_free(Data); return false; }

    // Materials: glTF order, plus one fallback slot at the end for primitives without a material.
    MaterialDecodeConfiguration MaterialConfig;
    MaterialConfig.EmissiveRadiance = Config.EmissiveRadiance;
    const GltfTextureResolver Resolve = [&](const cgltf_texture_view& View, bool Linear) { return RegisterGltfTexture(View, Linear, Textures, Directory); };
    std::vector<uint32_t> MaterialSlot(Data->materials_count);
    for (cgltf_size I = 0; I < Data->materials_count; ++I)
        MaterialSlot[I] = Out.RegisterMaterial(MaterialCodec::DecodeGltf(&Data->materials[I], MaterialConfig, Resolve));
    const uint32_t FallbackSlot = Out.RegisterMaterial(MaterialCodec::DecodeGltf(nullptr, MaterialConfig, nullptr));

    // Cameras and punctual lights, glTF order (attached to placements below).
    for (cgltf_size I = 0; I < Data->cameras_count; ++I)
    {
        const cgltf_camera& C = Data->cameras[I];
        CameraRecord R; R.Name = C.name ? C.name : "";
        if (C.type == cgltf_camera_type_perspective)
        {
            R.VerticalFieldOfView = C.data.perspective.yfov;
            R.AspectRatio = C.data.perspective.has_aspect_ratio ? C.data.perspective.aspect_ratio : 0.0f;
            R.NearPlane = C.data.perspective.znear * Config.UniformScale;
            R.FarPlane  = C.data.perspective.has_zfar ? C.data.perspective.zfar * Config.UniformScale : 0.0f;
        }
        else
        {
            R.Orthographic = true; R.OrthographicHalfHeight = C.data.orthographic.ymag * Config.UniformScale;
            R.NearPlane = C.data.orthographic.znear * Config.UniformScale; R.FarPlane = C.data.orthographic.zfar * Config.UniformScale;
        }
        (void)Out.RegisterCamera(R, kPlacementNone);
    }
    for (cgltf_size I = 0; I < Data->lights_count; ++I)
    {
        const cgltf_light& L = Data->lights[I];
        PunctualLuminaireRecord R; R.Name = L.name ? L.name : "";
        R.Category = L.type == cgltf_light_type_directional ? PunctualLuminaireCategory::Directional : L.type == cgltf_light_type_spot ? PunctualLuminaireCategory::Spot : PunctualLuminaireCategory::Point;
        std::memcpy(R.Colour, L.color, sizeof(R.Colour));
        R.Intensity = L.intensity; R.Range = L.range * Config.UniformScale;
        R.InnerConeAngle = L.spot_inner_cone_angle; R.OuterConeAngle = L.spot_outer_cone_angle;
        (void)Out.RegisterPunctualLuminaire(R, kPlacementNone);
    }

    // Meshes are decoded once into GeometryStructures (object space, engine axes) and instanced per node.
    struct DecodedPrimitive { GeometryStructure Geometry; uint32_t Material; uint32_t Flags; };
    std::map<const cgltf_primitive*, DecodedPrimitive> Decoded;

    const Matrix4x4 AxisSwap = ConstructGltfToWorldProjection();
    Matrix4x4 Scale;
    Scale.Columns[0][0] = Scale.Columns[1][1] = Scale.Columns[2][2] = Config.UniformScale;
    const Matrix4x4 Root = MultiplyProjection(Scale, AxisSwap);

    // Placements: every node in scene order (ancestors first, so the ancestor's row exists when the descendant links).
    std::vector<uint32_t> PlacementOf(Data->nodes_count, kPlacementNone);
    std::vector<const cgltf_node*> Order; Order.reserve(Data->nodes_count);
    {
        std::vector<const cgltf_node*> Stack;
        const cgltf_scene* Scene = Data->scene ? Data->scene : (Data->scenes_count ? &Data->scenes[0] : nullptr);
        if (Scene) for (cgltf_size I = Scene->nodes_count; I-- > 0;) Stack.push_back(Scene->nodes[I]);
        else       for (cgltf_size I = Data->nodes_count; I-- > 0;) if (!Data->nodes[I].parent) Stack.push_back(&Data->nodes[I]);
        while (!Stack.empty())
        {
            const cgltf_node* N = Stack.back(); Stack.pop_back();
            Order.push_back(N);
            for (cgltf_size I = N->children_count; I-- > 0;) Stack.push_back(N->children[I]);
        }
    }
    uint32_t Skipped = 0u, DataUriImages = 0u;
    for (const cgltf_node* NodePointer : Order)
    {
        const cgltf_node& Node = *NodePointer;
        const cgltf_size N = static_cast<cgltf_size>(&Node - Data->nodes);

        float WorldColumns[16], LocalColumns[16];
        cgltf_node_transform_world(&Node, WorldColumns);
        cgltf_node_transform_local(&Node, LocalColumns);
        const Matrix4x4 World = MultiplyProjection(Root, ProjectionFromColumns(WorldColumns));
        const uint32_t Ancestor = Node.parent ? PlacementOf[static_cast<size_t>(Node.parent - Data->nodes)] : kPlacementNone;
        // Local transform in engine axes: Root · L · Root⁻¹ for roots is the same as World; for descendants the local
        //    stays in the ancestor's frame, which is already engine-space after the ancestor's own swap.
        const Matrix4x4 Local = Ancestor == kPlacementNone ? World : ProjectionFromColumns(LocalColumns);
        const uint32_t Placement = Out.RegisterPlacement(Node.name ? Node.name : ("node_" + std::to_string(N)), Ancestor, Local, World);
        PlacementOf[N] = Placement;
        if (Node.camera) Out.AttachCamera(Placement, static_cast<uint32_t>(Node.camera - Data->cameras));
        if (Node.light)  Out.AttachPunctualLuminaire(Placement, static_cast<uint32_t>(Node.light - Data->lights));
        if (!Node.mesh) continue;
        const uint32_t FirstInstance = static_cast<uint32_t>(Out.QueryInstances().size());

        for (cgltf_size P = 0; P < Node.mesh->primitives_count; ++P)
        {
            const cgltf_primitive& Primitive = Node.mesh->primitives[P];
            if (Primitive.type != cgltf_primitive_type_triangles) { ++Skipped; continue; }

            auto Found = Decoded.find(&Primitive);
            if (Found == Decoded.end())
            {
                const cgltf_accessor* Position = FindAttribute(Primitive, cgltf_attribute_type_position);
                if (!Position) { ++Skipped; continue; }
                const cgltf_accessor* Normal   = FindAttribute(Primitive, cgltf_attribute_type_normal);
                const cgltf_accessor* Tangent  = FindAttribute(Primitive, cgltf_attribute_type_tangent);
                const cgltf_accessor* Texcoord = FindAttribute(Primitive, cgltf_attribute_type_texcoord);

                DecodedPrimitive& D = Decoded[&Primitive];
                std::vector<VertexRecord> Vertices(Position->count);
                for (cgltf_size V = 0; V < Position->count; ++V)
                {
                    VertexRecord& R = Vertices[V];
                    float Tmp[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    cgltf_accessor_read_float(Position, V, Tmp, 3);
                    R.SpatialLocation = Vector3{ Tmp[0], Tmp[1], Tmp[2] };   // glTF axes; World carries the swap
                    Tmp[0] = 0.0f; Tmp[1] = 1.0f; Tmp[2] = 0.0f;
                    if (Normal) cgltf_accessor_read_float(Normal, V, Tmp, 3);
                    R.NormalDirection = Vector3{ Tmp[0], Tmp[1], Tmp[2] };
                    Tmp[0] = 1.0f; Tmp[1] = 0.0f; Tmp[2] = 0.0f; Tmp[3] = 1.0f;
                    if (Tangent) cgltf_accessor_read_float(Tangent, V, Tmp, 4);
                    R.TangentDirection = Vector4{ Tmp[0], Tmp[1], Tmp[2], Tmp[3] };
                    Tmp[0] = 0.0f; Tmp[1] = 0.0f;
                    if (Texcoord) cgltf_accessor_read_float(Texcoord, V, Tmp, 2);
                    R.TextureCoordinateU = Tmp[0];
                    R.TextureCoordinateV = Tmp[1];
                }
                D.Geometry.AppendVertices(Vertices.data(), Vertices.size());

                std::vector<uint32_t> Indices;
                if (Primitive.indices)
                {
                    Indices.resize(Primitive.indices->count);
                    cgltf_accessor_unpack_indices(Primitive.indices, Indices.data(), sizeof(uint32_t), Indices.size());
                }
                else
                {
                    Indices.resize(Position->count);
                    for (cgltf_size I = 0; I < Position->count; ++I) Indices[I] = static_cast<uint32_t>(I);
                }
                Indices.resize(Indices.size() - Indices.size() % 3u);
                D.Geometry.AppendIndices(Indices.data(), Indices.size());

                D.Material = Primitive.material ? MaterialSlot[static_cast<size_t>(Primitive.material - Data->materials)] : FallbackSlot;
                D.Flags    = (Primitive.material && Primitive.material->double_sided) ? InstanceFlagDoubleSided : 0u;
                Found = Decoded.find(&Primitive);
            }

            (void)Out.RegisterInstance(Found->second.Geometry, World, Found->second.Material, Found->second.Flags);
        }
        const uint32_t InstanceCount = static_cast<uint32_t>(Out.QueryInstances().size()) - FirstInstance;
        if (InstanceCount) Out.AttachInstances(Placement, FirstInstance, InstanceCount);
    }
    for (cgltf_size I = 0; I < Data->images_count; ++I) if (Data->images[I].uri && std::strncmp(Data->images[I].uri, "data:", 5) == 0) ++DataUriImages;

    cgltf_free(Data);
    std::vector<std::string> Report;
    Out.Finalise(Config.SlabLimit, &Report);

    if (Out.QueryTriangleCount() == 0u) { if (Error) *Error = "no triangle primitives found"; return false; }
    if (Error)
    {
        std::string Warning;
        if (Skipped)       Warning += std::to_string(Skipped) + " non-triangle primitive(s) skipped; ";
        if (DataUriImages) Warning += std::to_string(DataUriImages) + " data-URI image(s) not decoded (placeholder); ";
        for (const std::string& Line : Report) Warning += Line + "; ";
        if (!Warning.empty()) Warning.resize(Warning.size() - 2u);
        *Error = Warning;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ENCODE
//------------------------------------------------------------------------------------------------------------------------
// One mesh, one primitive per material (flat-shaded: three unique vertices per triangle so per-face normals survive).

namespace {

std::string Base64(const std::vector<uint8_t>& Bytes)
{
    static const char* Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Out;
    Out.reserve((Bytes.size() + 2u) / 3u * 4u);
    for (size_t I = 0; I < Bytes.size(); I += 3)
    {
        const uint32_t B0 = Bytes[I];
        const uint32_t B1 = I + 1 < Bytes.size() ? Bytes[I + 1] : 0u;
        const uint32_t B2 = I + 2 < Bytes.size() ? Bytes[I + 2] : 0u;
        const uint32_t Triple = (B0 << 16) | (B1 << 8) | B2;
        Out.push_back(Alphabet[(Triple >> 18) & 63u]);
        Out.push_back(Alphabet[(Triple >> 12) & 63u]);
        Out.push_back(I + 1 < Bytes.size() ? Alphabet[(Triple >> 6) & 63u] : '=');
        Out.push_back(I + 2 < Bytes.size() ? Alphabet[Triple & 63u] : '=');
    }
    return Out;
}

void AppendFloats(std::vector<uint8_t>& Bytes, const float* F, size_t Count)
{
    const size_t Offset = Bytes.size();
    Bytes.resize(Offset + Count * sizeof(float));
    std::memcpy(Bytes.data() + Offset, F, Count * sizeof(float));
}

std::string Number(float F)
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "%.9g", static_cast<double>(F));
    std::string S = Buffer;
    if (S.find_first_of(".einf") == std::string::npos) S += ".0";
    return S;
}

} // namespace

bool SceneCodec::Encode(const std::string& Path, const std::vector<TriangleIndex>& Triangles,
                        const std::vector<MaterialDescriptor>& Materials, std::string* Error,
                        const SceneEncodeConfiguration& Configuration) noexcept
{
    std::vector<uint8_t> Buffer;
    std::ostringstream Views, Accessors, Primitives;
    uint32_t ViewIndex = 0u, AccessorIndex = 0u;
    bool FirstPrimitive = true;
    const std::string Name = Configuration.Name.empty() ? std::string("CornellBox") : Configuration.Name;
    const bool Smooth = Configuration.CornerNormals && Configuration.CornerNormals->size() == Triangles.size() * 3u;

    for (uint32_t M = 0u; M < Materials.size(); ++M)
    {
        std::vector<float> Positions, Normals, Texcoords;
        std::vector<uint32_t> Indices;
        float Minimum[3] = {  1e30f,  1e30f,  1e30f };
        float Maximum[3] = { -1e30f, -1e30f, -1e30f };
        for (size_t TriangleSlot = 0u; TriangleSlot < Triangles.size(); ++TriangleSlot)
        {
            const TriangleIndex& T = Triangles[TriangleSlot];
            uint32_t Slot; std::memcpy(&Slot, &T.MaterialSlot, sizeof(Slot));
            if (Slot != M) continue;
            const Vector3 Corners[3] = { WorldToGltf(Vector3{ T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ }),
                                         WorldToGltf(Vector3{ T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  }),
                                         WorldToGltf(Vector3{ T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ }) };
            const Vector3 A = Vector3{ T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ }, B = Vector3{ T.VertexBetaX, T.VertexBetaY, T.VertexBetaZ }, C = Vector3{ T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
            const Vector3 Cross = OrientationClassifier::CrossProduct(B - A, C - A);
            const float   Len   = Cross.Length();
            const Vector3 N = WorldToGltf(Len > 0.0f ? Cross / Len : Vector3{ 0.0f, 0.0f, 1.0f });
            const float Uv[3][2] = { { T.TextureAlphaU, T.TextureAlphaV }, { T.TextureBetaU, T.TextureBetaV }, { T.TextureGammaU, T.TextureGammaV } };
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const Vector3& Corner = Corners[K];
                const Vector3 Ns = Smooth ? WorldToGltf((*Configuration.CornerNormals)[TriangleSlot * 3u + K]) : N;
                Indices.push_back(static_cast<uint32_t>(Positions.size() / 3u));
                Positions.insert(Positions.end(), { Corner.x, Corner.y, Corner.z });
                Normals.insert(Normals.end(), { Ns.x, Ns.y, Ns.z });
                if (Configuration.WriteTexcoords) Texcoords.insert(Texcoords.end(), { Uv[K][0], Uv[K][1] });
                Minimum[0] = std::min(Minimum[0], Corner.x); Minimum[1] = std::min(Minimum[1], Corner.y); Minimum[2] = std::min(Minimum[2], Corner.z);
                Maximum[0] = std::max(Maximum[0], Corner.x); Maximum[1] = std::max(Maximum[1], Corner.y); Maximum[2] = std::max(Maximum[2], Corner.z);
            }
        }
        if (Indices.empty()) continue;

        const auto EmitView = [&](size_t ByteOffset, size_t ByteLength, int Target)
        {
            if (ViewIndex) Views << ",";
            Views << "{\"buffer\":0,\"byteOffset\":" << ByteOffset << ",\"byteLength\":" << ByteLength << ",\"target\":" << Target << "}";
            return ViewIndex++;
        };

        size_t Offset = Buffer.size();
        AppendFloats(Buffer, Positions.data(), Positions.size());
        const uint32_t PositionView = EmitView(Offset, Positions.size() * 4u, 34962);
        Offset = Buffer.size();
        AppendFloats(Buffer, Normals.data(), Normals.size());
        const uint32_t NormalView = EmitView(Offset, Normals.size() * 4u, 34962);
        uint32_t TexcoordView = 0u;
        if (Configuration.WriteTexcoords)
        {
            Offset = Buffer.size();
            AppendFloats(Buffer, Texcoords.data(), Texcoords.size());
            TexcoordView = EmitView(Offset, Texcoords.size() * 4u, 34962);
        }
        Offset = Buffer.size();
        Buffer.resize(Offset + Indices.size() * 4u);
        std::memcpy(Buffer.data() + Offset, Indices.data(), Indices.size() * 4u);
        const uint32_t IndexView = EmitView(Offset, Indices.size() * 4u, 34963);

        const uint32_t Count = static_cast<uint32_t>(Positions.size() / 3u);
        if (AccessorIndex) Accessors << ",";
        Accessors << "{\"bufferView\":" << PositionView << ",\"componentType\":5126,\"count\":" << Count << ",\"type\":\"VEC3\""
                  << ",\"min\":[" << Number(Minimum[0]) << "," << Number(Minimum[1]) << "," << Number(Minimum[2]) << "]"
                  << ",\"max\":[" << Number(Maximum[0]) << "," << Number(Maximum[1]) << "," << Number(Maximum[2]) << "]}";
        const uint32_t PositionAccessor = AccessorIndex++;
        Accessors << ",{\"bufferView\":" << NormalView << ",\"componentType\":5126,\"count\":" << Count << ",\"type\":\"VEC3\"}";
        const uint32_t NormalAccessor = AccessorIndex++;
        uint32_t TexcoordAccessor = 0u;
        if (Configuration.WriteTexcoords)
        {
            Accessors << ",{\"bufferView\":" << TexcoordView << ",\"componentType\":5126,\"count\":" << Count << ",\"type\":\"VEC2\"}";
            TexcoordAccessor = AccessorIndex++;
        }
        Accessors << ",{\"bufferView\":" << IndexView << ",\"componentType\":5125,\"count\":" << Indices.size() << ",\"type\":\"SCALAR\"}";
        const uint32_t IndexAccessor = AccessorIndex++;

        if (!FirstPrimitive) Primitives << ",";
        FirstPrimitive = false;
        Primitives << "{\"attributes\":{\"POSITION\":" << PositionAccessor << ",\"NORMAL\":" << NormalAccessor;
        if (Configuration.WriteTexcoords) Primitives << ",\"TEXCOORD_0\":" << TexcoordAccessor;
        Primitives << "},\"indices\":" << IndexAccessor
                   << ",\"material\":" << M << ",\"mode\":4}";
    }

    std::ostringstream MaterialsJson;
    std::vector<std::string> ExtensionsUsed;
    for (uint32_t M = 0u; M < Materials.size(); ++M)
    {
        if (M) MaterialsJson << ",";
        MaterialsJson << MaterialCodec::EncodeGltf(Materials[M], ExtensionsUsed, nullptr);
    }
    std::ostringstream ExtensionsJson;
    for (size_t I = 0; I < ExtensionsUsed.size(); ++I) ExtensionsJson << (I ? "," : "") << "\"" << ExtensionsUsed[I] << "\"";

    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    if (!File) { if (Error) *Error = "cannot open " + Path + " for writing"; return false; }
    File << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Frontier SceneCodec\"},"
         << "\"extensionsUsed\":[" << ExtensionsJson.str() << "],"
         << "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"" << Name << "\"}],"
         << "\"meshes\":[{\"name\":\"" << Name << "\",\"primitives\":[" << Primitives.str() << "]}],"
         << "\"materials\":[" << MaterialsJson.str() << "],"
         << "\"accessors\":[" << Accessors.str() << "],"
         << "\"bufferViews\":[" << Views.str() << "],"
         << "\"buffers\":[{\"byteLength\":" << Buffer.size() << ",\"uri\":\"data:application/octet-stream;base64," << Base64(Buffer) << "\"}]}\n";
    return static_cast<bool>(File);
}

} // namespace Frontier
