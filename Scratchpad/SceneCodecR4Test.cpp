// R4a row-2 harness: Cornell export byte-identity, Sponza decode (materials, textures, placements tree).
#include "ContentInterchange/SceneCodec.h"
#include "ContentInterchange/TextureIndex.h"
#include "ContentInterchange/MaterialCodec.h"
#include "DisplayPresentation/ReSTIRIntegrator.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <functional>
using namespace Frontier;
static int Failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)
static std::string ReadAll(const char* P) { std::ifstream F(P, std::ios::binary); std::stringstream S; S << F.rdbuf(); return S.str(); }

int main(int argc, char** argv)
{
    const char* Cornell = argv[1]; const char* Sponza = argc > 2 ? argv[2] : nullptr;
    std::printf("[1] Cornell export byte-identity (analytical solver -> MaterialDescriptor -> SceneCodec::Encode)\n");
    {
        ProjectZero::RayTracingSolver Scene;
        std::string Err;
        CHECK(SceneCodec::Encode("/tmp/CornellRoundTrip.gltf", ReSTIRIntegrator::BuildTriangleIndex(Scene), ReSTIRIntegrator::BuildMaterialDescriptors(Scene), &Err));
        std::string A = ReadAll(Cornell), B = ReadAll("/tmp/CornellRoundTrip.gltf");
        std::printf("  repo file %zu B, re-export %zu B, %s\n", A.size(), B.size(), A == B ? "BYTE-IDENTICAL" : "DIFFER");
        if (A != B) { size_t i = 0; while (i < A.size() && i < B.size() && A[i] == B[i]) ++i; std::printf("  first difference at %zu: '%.60s' vs '%.60s'\n", i, A.c_str() + i, B.c_str() + i); }
        CHECK(A == B);
        SceneStructure L; TextureIndex T; SceneDecodeConfiguration C;
        CHECK(SceneCodec::Decode(Cornell, L, &T, C, &Err));
        std::printf("  decode: %u tris, %zu instances, %u materials, %zu luminaires, %zu placements, warnings '%s'\n", L.QueryTriangleCount(), L.QueryInstances().size(), L.QueryMaterials().QueryCount(), L.QueryLuminaires().size(), L.QueryPlacements().size(), Err.c_str());
        const TriangleIndex& F = L.QueryFlatTriangles()[0];
        std::printf("  flat tri 0 uv: a(%.2f %.2f) b(%.2f %.2f) g(%.2f %.2f)\n", F.TextureAlphaU, F.TextureAlphaV, F.TextureBetaU, F.TextureBetaV, F.TextureGammaU, F.TextureGammaV);
    }
    if (Sponza)
    {
        std::printf("[2] Sponza decode: %s\n", Sponza);
        SceneStructure L; TextureIndex T; SceneDecodeConfiguration C; std::string Err;
        CHECK(SceneCodec::Decode(Sponza, L, &T, C, &Err));
        std::printf("  %u tris, %zu instances, %u materials, %zu luminaires, %zu placements, %zu cameras, %zu lights; warnings '%s'\n", L.QueryTriangleCount(), L.QueryInstances().size(), L.QueryMaterials().QueryCount(), L.QueryLuminaires().size(), L.QueryPlacements().size(), L.QueryCameras().size(), L.QueryPunctualLuminaires().size(), Err.c_str());
        std::vector<std::string> Report;
        uint32_t Fail = T.Decode(2048u, &Report);
        for (auto& R : Report) std::printf("  %s\n", R.c_str());
        CHECK(Fail == 0u);
        std::printf("  %-44s %9s %5s %8s %s\n", "texture", "size", "mips", "KB", "encoding");
        size_t Shown = 0; uint64_t Bytes = 0;
        for (const TextureDescriptor& D : T.QueryTextures())
        {
            Bytes += D.Texels.size();
            if (Shown++ < 12 || Shown == T.QueryCount()) std::printf("  %-44s %4ux%-4u %5u %8zu %s\n", D.Name.substr(D.Name.find_last_of('/') + 1).c_str(), D.Width, D.Height, D.LevelCount, D.Texels.size() / 1024, D.Encoding == TextureEncoding::Srgb8 ? "sRGB8" : D.Encoding == TextureEncoding::Linear8 ? "linear8" : "half");
            else if (Shown == 13) std::printf("  ...\n");
        }
        std::printf("  total %.1f MB resident for %u textures\n", Bytes / 1048576.0, T.QueryCount());
        // mip sanity: level-1 average of a checker should stay mid grey — test synthetic
        TextureDescriptor S; S.Width = S.Height = 2; S.Encoding = TextureEncoding::Srgb8; S.Texels = { 0,0,0,255, 255,255,255,255, 255,255,255,255, 0,0,0,255 };
        TextureIndex::ConstructLevels(S);
        std::printf("  sRGB-aware mip: 2x2 black/white checker -> 1x1 = %u (linear-correct 188, naive 128)\n", S.Texels[S.LevelOffsets[1]]);
        CHECK(S.Texels[S.LevelOffsets[1]] == 188u);
        // placements tree (first 3 levels)
        std::printf("  placement tree:\n");
        std::function<void(uint32_t,int)> Dump = [&](uint32_t P, int Depth)
        {
            const PlacementRecord& R = L.QueryPlacements()[P];
            if (Depth < 3) std::printf("  %*s%s  [instances %u..+%u]%s%s\n", Depth * 2, "", R.Name.c_str(), R.FirstInstance == kPlacementNone ? 0u : R.FirstInstance, R.InstanceCount, R.Camera != kPlacementNone ? " camera" : "", R.Luminaire != kPlacementNone ? " light" : "");
            for (uint32_t D = R.FirstDescendant; D != kPlacementNone; D = L.QueryPlacements()[D].NextPeer) Dump(D, Depth + 1);
        };
        for (uint32_t P = 0; P < L.QueryPlacements().size(); ++P) if (L.QueryPlacements()[P].Ancestor == kPlacementNone) Dump(P, 0);
        uint32_t Attached = 0; for (auto& P : L.QueryPlacements()) Attached += P.InstanceCount;
        CHECK(Attached == L.QueryInstances().size());
        std::printf("  %u/%zu instances attached to placements\n", Attached, L.QueryInstances().size());
        const MaterialRecord& M = L.QueryMaterials().QueryRecords()[0];
        std::printf("  material 0 header: baseTex slot %u normalTex slot %u -> '%s'\n", M.BaseColourTexture, M.NormalTexture, T.QueryTextures()[M.BaseColourTexture].Name.substr(T.QueryTextures()[M.BaseColourTexture].Name.find_last_of('/') + 1).c_str());
    }
    std::printf("%s (%d failures)\n", Failures ? "FAILED" : "ALL CHECKS PASSED", Failures);
    return Failures;
}
