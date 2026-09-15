//============================================================================================================================================
//                                                  SCENETRAVERSALIDENTITYTEST.CPP
//============================================================================================================================================
// 🧩 The D1 gate applied to REAL scenes rather than a synthetic room: decodes an actual glTF through the real
//    SceneCodec, builds the CWBVH through both Build() and BuildBottomLevel(), and requires the node/leaf blobs
//    to hash identically.
//
//    CornellBox.gltf is the R0 bit-identity reference, so this is the check that the two-level split has not
//    disturbed the renderer's baseline. Showroom.gltf is the D4 target scene.
//
//    Build: bash Scratchpad/CheckTraversalIdentity.sh   (builds and runs this too)

// Loads the REAL CornellBox.gltf through the real SceneCodec and builds the CWBVH both ways.
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"
#include "Engine/GeometricRaster/TraversalIndex.h"
#include <cstdio>
#include <vector>
#include <cmath>
static uint64_t Hash(const std::vector<float>& V){
    uint64_t h=1469598103934665603ull; auto*b=reinterpret_cast<const unsigned char*>(V.data());
    for(size_t i=0;i<V.size()*sizeof(float);++i){h^=b[i];h*=1099511628211ull;} return h;}
int main(int argc,char**argv){
    const char* Path = argc>1?argv[1]:"Projects/Project-Zero/Content/Scenes/CornellBox.gltf";
    Frontier::SceneStructure Level; Frontier::TextureIndex Tex;
    Frontier::SceneDecodeConfiguration Cfg; std::string Err;
    if(!Frontier::SceneCodec::Decode(Path,Level,&Tex,Cfg,&Err)){printf("decode failed: %s\n",Err.c_str());return 1;}
    const auto& Facets = Level.QueryFlatTriangles();
    printf("%s: %zu flat triangles\n", Path, Facets.size());
    const bool HQ = Level.QueryTriangleCount() <= 2000000u;
    Frontier::TraversalIndex A,B;
    A.Build(Facets,HQ);
    B.BuildBottomLevel(Facets,HQ);
    const auto&MA=A.QueryMetrics(); const auto&MB=B.QueryMetrics();
    printf("  Build()           nodes %u  SAH %.6f  node %016llx leaf %016llx\n",MA.NodeCount,(double)MA.SahCost,
           (unsigned long long)Hash(A.QueryNodeBlob()),(unsigned long long)Hash(A.QueryLeafBlob()));
    printf("  BuildBottomLevel  nodes %u  SAH %.6f  node %016llx leaf %016llx\n",MB.NodeCount,(double)MB.SahCost,
           (unsigned long long)Hash(B.QueryNodeBlob()),(unsigned long long)Hash(B.QueryLeafBlob()));
    // SAH is deliberately NOT compared for equality. tinybvh's multi-threaded builder sums the cost in a
    //    non-deterministic order, so the value wobbles in its last digit between two runs of the SAME builder --
    //    verified by running this harness three times. The BLOBS are the artifact that reaches the GPU and they
    //    hash identically every time; SAH is only a diagnostic. Requiring exact SAH equality here would be a
    //    flaky gate that fails for a reason unrelated to the change under test.
    const double SahDelta = std::fabs(double(MA.SahCost) - double(MB.SahCost));
    const bool ok = Hash(A.QueryNodeBlob())==Hash(B.QueryNodeBlob())
                 && Hash(A.QueryLeafBlob())==Hash(B.QueryLeafBlob())
                 && MA.NodeCount==MB.NodeCount
                 && MA.TriangleCount==MB.TriangleCount
                 && SahDelta < 1e-3;
    printf(ok?"  >>> BIT-IDENTICAL\n":"  >>> DIVERGED\n");
    return ok?0:1;
}
