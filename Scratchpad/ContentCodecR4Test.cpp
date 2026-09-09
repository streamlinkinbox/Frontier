// R4a row-4 harness: FbxCodec / ObjCodec / ContentCodec against ufbx's test data.
//    Proof of axis + unit correctness: ufbx ships <name>.obj dumps of the same scenes in the source tool's NATIVE axes and
//    units (Blender: Z-up metres · Maya: Y-up centimetres · 3ds Max: Z-up inches), while the FBX headers declare
//    UpAxis/UnitScaleFactor. FbxCodec asks ufbx for right-handed Z-up METRES, so its world vertices must equal the OBJ
//    raw coordinates re-expressed in that frame. ObjCodec assumes Y-up input (glTF swap) with UniformScale for units,
//    so for Z-up tools the harness undoes the swap on the OBJ side: raw = (X, Z, −Y) of the engine-space vertex.
#include "ContentInterchange/ContentCodec.h"
#include "ContentInterchange/TextureIndex.h"
#include <cstdio>
#include <algorithm>
#include <functional>
#include <set>
#include <cmath>
using namespace Frontier;
static int Failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

static void Tree(const SceneStructure& L)
{
    std::function<void(uint32_t,int)> Dump = [&](uint32_t P, int Depth)
    {
        const PlacementRecord& R = L.QueryPlacements()[P];
        const float* W = &R.WorldTransform[12];
        std::printf("  %*s%-28s instances %u  world t=(%.2f %.2f %.2f)%s%s\n", Depth * 2, "", R.Name.c_str(), R.InstanceCount, W[0], W[1], W[2], R.Camera != kPlacementNone ? "  [camera]" : "", R.Luminaire != kPlacementNone ? "  [light]" : "");
        for (uint32_t D = R.FirstDescendant; D != kPlacementNone; D = L.QueryPlacements()[D].NextPeer) Dump(D, Depth + 1);
    };
    for (uint32_t P = 0; P < L.QueryPlacements().size(); ++P) if (L.QueryPlacements()[P].Ancestor == kPlacementNone) Dump(P, 0);
}

// World-space vertex multiset (rounded to mm) for cross-format comparison.
static std::multiset<std::array<int,3>> WorldVertices(const SceneStructure& L, bool UndoGltfSwap = false)
{
    std::multiset<std::array<int,3>> S;
    for (const InstanceRecord& I : L.QueryInstances())
    {
        std::set<uint32_t> Used;
        for (uint32_t K = 0; K < I.TriangleCount * 3u; ++K) Used.insert(L.QueryIndices()[I.FirstIndex + K]);
        for (uint32_t V : Used)
        {
            const VertexRecord& R = L.QueryVertices()[I.VertexOffset + V];
            const float* M = &I.World[0];
            float X = M[0]*R.SpatialLocation.x + M[4]*R.SpatialLocation.y + M[8]*R.SpatialLocation.z + M[12];
            float Y = M[1]*R.SpatialLocation.x + M[5]*R.SpatialLocation.y + M[9]*R.SpatialLocation.z + M[13];
            float Z = M[2]*R.SpatialLocation.x + M[6]*R.SpatialLocation.y + M[10]*R.SpatialLocation.z + M[14];
            if (UndoGltfSwap) { const float Ry = Z, Rz = -Y; Y = Ry; Z = Rz; }
            S.insert({ (int)std::lround(X*1000), (int)std::lround(Y*1000), (int)std::lround(Z*1000) });
        }
    }
    return S;
}

static bool Load(const char* Path, SceneStructure& L, TextureIndex& T, bool Verbose = true, float UniformScale = 1.0f)
{
    SceneDecodeConfiguration C; C.SlabLimit = 4u; C.UniformScale = UniformScale; std::string Err;
    const bool Ok = ContentCodec::Decode(Path, L, &T, C, &Err);
    std::printf("[%s] %s\n  %s: %u tris, %zu instances, %u materials, %zu placements, %zu cameras, %zu lights, %u textures registered%s%s\n",
        ContentCodec::NameOf(ContentCodec::Classify(Path)), Path, Ok ? "ok" : "FAILED", L.QueryTriangleCount(), L.QueryInstances().size(), L.QueryMaterials().QueryCount(), L.QueryPlacements().size(), L.QueryCameras().size(), L.QueryPunctualLuminaires().size(), T.QueryCount(), Err.empty() ? "" : "\n  note: ", Err.c_str());
    if (Ok && Verbose) Tree(L);
    return Ok;
}

int main()
{
    const char* D = "/home/user/ext/ufbx/data/";
    std::string P;

    std::printf("=== 1. Hierarchy + pivots: FBX vs ufbx's own OBJ world-space reference ===\n");
    struct PairSource { const char* Fbx; const char* Obj; const char* Tool; bool ObjIsZUp; float ObjUnitMetres; };
    const PairSource Pairs[] =
    {
        { "blender_279_ball_7400_binary.fbx", "blender_279_ball_0_obj.obj", "Blender (Z-up, m)",       true,  1.0f    },
        { "maya_child_pivots_7700_ascii.fbx", "maya_child_pivots.obj",      "Maya (Y-up, cm)",         false, 0.01f   },
        { "max2009_blob_6100_binary.fbx",     "max2009_blob.obj",           "3ds Max (Z-up, inches)",  true,  0.0254f },
    };
    for (const PairSource& Pair : Pairs)
    {
        SceneStructure F, O; TextureIndex TF, TO;
        std::string Fbx = std::string(D) + Pair.Fbx, Obj = std::string(D) + Pair.Obj;
        std::printf("--- %s ---\n", Pair.Tool);
        CHECK(Load(Fbx.c_str(), F, TF));
        CHECK(Load(Obj.c_str(), O, TO, false, Pair.ObjUnitMetres));
        auto A = WorldVertices(F), B = WorldVertices(O, Pair.ObjIsZUp);
        // Compare the unique position sets (per-material-part splits weld corners differently, so multiplicities differ).
        std::set<std::array<int,3>> UA(A.begin(), A.end()), UB(B.begin(), B.end());
        size_t Match = 0; for (auto& V : UA) if (UB.count(V)) ++Match;
        std::printf("  world-space unique positions FBX %zu / OBJ %zu, matching %zu (mm-rounded)%s\n\n", UA.size(), UB.size(), Match, UA == UB ? "  IDENTICAL" : "");
        CHECK(UA == UB);
    }

    std::printf("=== 2. Materials + textures ===\n");
    for (const char* Name : { "blender_293_textures_7400_binary.fbx", "blender_293_embedded_textures_7400_binary.fbx", "max_physical_material_textures_7500_binary.fbx", "blender_402_material_chart_7400_binary.fbx" })
    {
        SceneStructure L; TextureIndex T;
        if (!Load((std::string(D) + Name).c_str(), L, T, false)) { ++Failures; continue; }
        for (uint32_t M = 0; M < std::min(6u, L.QueryMaterials().QueryCount()); ++M)
        {
            const MaterialRecord& R = L.QueryMaterials().QueryRecords()[M];
            const MaterialDescriptor& Dsc = L.QueryMaterials().QueryDescriptors()[M];
            std::printf("  material %u '%s': base (%.2f %.2f %.2f) rough %.2f metal %.2f emissive %.1f  slabs %u  baseTex %s  normalTex %s\n", M, Dsc.Name.c_str(), R.AlbedoR, R.AlbedoG, R.AlbedoB, R.Roughness, R.Metalness, R.EmissiveR, R.SlabCount,
                R.BaseColourTexture == kMaterialTextureNone ? "-" : T.QueryTextures()[R.BaseColourTexture].Name.substr(T.QueryTextures()[R.BaseColourTexture].Name.find_last_of("/\\") + 1).c_str(),
                R.NormalTexture == kMaterialTextureNone ? "-" : T.QueryTextures()[R.NormalTexture].Name.substr(T.QueryTextures()[R.NormalTexture].Name.find_last_of("/\\") + 1).c_str());
        }
        std::vector<std::string> Report; T.Decode(2048u, &Report);
        for (auto& S : Report) std::printf("  %s\n", S.c_str());
        std::printf("\n");
    }

    std::printf("=== 3. Cameras + lights ===\n");
    for (const char* Name : { "blender_279_default_7400_binary.fbx", "max2009_blob_6100_binary.fbx" })
    {
        SceneStructure L; TextureIndex T;
        std::string Path = std::string(D) + Name;
        if (!Load(Path.c_str(), L, T, false)) { ++Failures; std::printf("\n"); continue; }
        for (auto& C : L.QueryCameras()) std::printf("  camera '%s': vfov %.1f°, aspect %.2f, near %.2f far %.1f%s\n", C.Name.c_str(), C.VerticalFieldOfView * 57.2958f, C.AspectRatio, C.NearPlane, C.FarPlane, C.Orthographic ? " ortho" : "");
        for (auto& G : L.QueryPunctualLuminaires()) std::printf("  light '%s': %s colour (%.2f %.2f %.2f) intensity %.2f cone %.1f°/%.1f°\n", G.Name.c_str(), G.Category == PunctualLuminaireCategory::Directional ? "directional" : G.Category == PunctualLuminaireCategory::Spot ? "spot" : "point", G.Colour[0], G.Colour[1], G.Colour[2], G.Intensity, G.InnerConeAngle * 57.2958f, G.OuterConeAngle * 57.2958f);
        std::printf("\n");
    }

    std::printf("=== 4. OBJ + MTL (textures, groups) ===\n");
    for (const char* Name : { "blender_279_ball_0_obj.obj", "synthetic_color_suzanne_1_obj.obj", "blender_331_space texture_0_obj.obj" })
    {
        SceneStructure L; TextureIndex T;
        if (!Load((std::string(D) + Name).c_str(), L, T)) { ++Failures; continue; }
        for (uint32_t M = 0; M < L.QueryMaterials().QueryCount(); ++M)
        {
            const MaterialRecord& R = L.QueryMaterials().QueryRecords()[M];
            std::printf("  material %u '%s': base (%.2f %.2f %.2f) rough %.2f metal %.2f  baseTex %s\n", M, L.QueryMaterials().QueryDescriptors()[M].Name.c_str(), R.AlbedoR, R.AlbedoG, R.AlbedoB, R.Roughness, R.Metalness, R.BaseColourTexture == kMaterialTextureNone ? "-" : T.QueryTextures()[R.BaseColourTexture].Name.c_str());
        }
        std::printf("\n");
    }

    std::printf("=== 5. Dispatcher ===\n");
    { SceneStructure L; TextureIndex T; SceneDecodeConfiguration C; std::string E; CHECK(!ContentCodec::Decode("thing.blend", L, &T, C, &E)); std::printf("  thing.blend → '%s'\n", E.c_str()); }
    CHECK(ContentCodec::Classify("A/B.GLB") == ContentFormatCategory::Gltf);
    CHECK(ContentCodec::Classify("x.FBX") == ContentFormatCategory::Fbx);

    std::printf("%s (%d failures)\n", Failures ? "FAILED" : "ALL CHECKS PASSED", Failures);
    return Failures;
}
