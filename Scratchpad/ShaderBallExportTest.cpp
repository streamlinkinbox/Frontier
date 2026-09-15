// R4b row-3 harness: re-export the Cornell box with the specular_weight = 0 pin, build + export the shader-ball level,
//    decode both back and report counts / flags / material summaries.
#include "ContentInterchange/SceneCodec.h"
#include "ContentInterchange/ShaderBallStructure.h"
#include "ContentInterchange/TextureIndex.h"
#include "ContentInterchange/MaterialIndex.h"
#include "DisplayPresentation/ReSTIRIntegrator.h"
#include "GeometricRaster/SceneStructure.h"
#include <cstdio>

using namespace Frontier;

int main(int argc, char** argv)
{
    const char* CornellPath = argc > 1 ? argv[1] : "/tmp/CornellBox.gltf";
    const char* BallPath    = argc > 2 ? argv[2] : "/tmp/ShaderBall.gltf";
    std::string Err;
    {
        ProjectZero::RayTracingSolver Scene;
        auto Materials = ReSTIRIntegrator::BuildMaterialDescriptors(Scene);
        std::printf("[1] Cornell: %zu materials, specular_weight = ", Materials.size());
        for (auto& M : Materials) std::printf("%g ", M.Slabs[0].SpecularWeight);
        bool Ok = SceneCodec::Encode(CornellPath, ReSTIRIntegrator::BuildTriangleIndex(Scene), Materials, &Err);
        std::printf("\n    export %s -> %s %s\n", CornellPath, Ok ? "ok" : "FAILED", Err.c_str());
    }
    {
        ShaderBallStructure Ball; Ball.Construct();
        std::printf("[2] ShaderBall: %zu triangles, %zu corner normals, %zu materials\n", Ball.QueryTriangles().size(), Ball.QueryCornerNormals().size(), Ball.QueryMaterials().size());
        bool Ok = Ball.Export(BallPath, &Err);
        std::printf("    export %s -> %s %s\n", BallPath, Ok ? "ok" : "FAILED", Err.c_str());
    }
    for (const char* Path : { CornellPath, BallPath })
    {
        SceneStructure L; TextureIndex T; SceneDecodeConfiguration C{};
        bool Ok = SceneCodec::Decode(Path, L, &T, C, &Err);
        const Vector3 Lo = L.QueryBoundsMinimum(), Hi = L.QueryBoundsMaximum();
        std::printf("[3] decode %s: %s %s\n    %u triangles, %zu instances, %zu clusters, %u materials, %zu luminaires, bounds [%.2f %.2f %.2f]..[%.2f %.2f %.2f]\n",
                    Path, Ok ? "ok" : "FAILED", Err.c_str(), L.QueryTriangleCount(), L.QueryInstances().size(), L.QueryClusters().size(),
                    L.QueryMaterials().QueryCount(), L.QueryLuminaires().size(), Lo.x, Lo.y, Lo.z, Hi.x, Hi.y, Hi.z);
        uint32_t AlphaMasked = 0u;
        for (const MaterialRecord& R : L.QueryMaterials().QueryRecords()) if (R.Flags & MaterialFlagAlphaMask) ++AlphaMasked;
        std::printf("    alpha-masked materials: %u\n", AlphaMasked);
        const auto& Slabs = L.QueryMaterials().QuerySlabRecords();
        for (size_t I = 0; I < L.QueryMaterials().QueryRecords().size() && I < 26; ++I)
        {
            const MaterialRecord& R = L.QueryMaterials().QueryRecords()[I];
            const MaterialSlabRecord& S = Slabs[R.SlabOffset];
            std::printf("    %2zu base(%.2f %.2f %.2f) metal %.1f specW %.1f rough %.2f coat %.1f fuzz %.1f film %.1f haze %.1f eonR %.1f emis %.0f opac %.1f flags %u\n",
                        I, S.BaseColorR, S.BaseColorG, S.BaseColorB, S.BaseMetalness, S.SpecularWeight, S.SpecularRoughness, S.CoatWeight, S.FuzzWeight,
                        S.ThinFilmWeight, S.SlateHazinessWeight, S.BaseDiffuseRoughness, S.EmissionLuminance, S.GeometryOpacity, R.Flags);
        }
    }
    return 0;
}
