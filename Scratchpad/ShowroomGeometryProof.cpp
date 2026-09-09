#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include <cstdio>
#include <cmath>
#include <cstring>
namespace Frontier {
bool SceneCodec::Encode(const std::string&, const std::vector<TriangleIndex>&, const std::vector<MaterialDescriptor>&, std::string*, const SceneEncodeConfiguration&) noexcept { return true; }
}
using namespace Frontier; using namespace Frontier::ProjectZero;
static int Fail = 0;
static void CheckTrue(const char* N, bool C){ printf("%-52s %s\n", N, C?"PASS":"FAIL"); if(!C) ++Fail; }
int main(){
    ShowroomStructure S; S.Construct();
    const auto& T = S.QueryTriangles(); const auto& N = S.QueryCornerNormals(); const auto& M = S.QueryMaterials();
    printf("triangles=%zu normals=%zu materials=%zu\n", T.size(), N.size(), M.size());
    CheckTrue("normals = 3 x triangles", N.size() == T.size()*3);
    CheckTrue("materials = 11", M.size() == 11);
    CheckTrue("triangle count plausible (2500..8000)", T.size() > 2500 && T.size() < 8000);
    // all material slots in range
    bool slots = true; float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f,minz=1e9f,maxz=-1e9f;
    for (const auto& t : T){ uint32_t m; memcpy(&m,&t.MaterialSlot,4); if(m>=M.size()) slots=false;
        const float xs[3]={t.VertexAlphaX,t.VertexBetaX,t.VertexGammaX};
        const float ys[3]={t.VertexAlphaY,t.VertexBetaY,t.VertexGammaY};
        const float zs[3]={t.VertexAlphaZ,t.VertexBetaZ,t.VertexGammaZ};
        for(int i=0;i<3;++i){minx=std::fmin(minx,xs[i]);maxx=std::fmax(maxx,xs[i]);miny=std::fmin(miny,ys[i]);maxy=std::fmax(maxy,ys[i]);minz=std::fmin(minz,zs[i]);maxz=std::fmax(maxz,zs[i]);} }
    CheckTrue("all material slots in range", slots);
    printf("bounds X[%.3f %.3f] Y[%.3f %.3f] Z[%.3f %.3f]\n",minx,maxx,miny,maxy,minz,maxz);
    CheckTrue("geometry inside room shell", minx>=-2.001f&&maxx<=2.001f&&miny>=-2.001f&&maxy<=3.001f&&minz>=-0.001f&&maxz<=3.001f);
    // no degenerate triangles, unit normals
    bool degen=false; for(const auto&t:T){ Vector3 a{t.VertexAlphaX,t.VertexAlphaY,t.VertexAlphaZ},b{t.VertexBetaX,t.VertexBetaY,t.VertexBetaZ},c{t.VertexGammaX,t.VertexGammaY,t.VertexGammaZ};
        if(OrientationClassifier::CrossProduct(b-a,c-a).Length() < 1e-9f) degen=true; }
    CheckTrue("no degenerate triangles", !degen);
    bool unitn=true; for(const auto&n:N) if(std::fabs(n.Length()-1.0f)>1e-3f) unitn=false;
    CheckTrue("all corner normals unit length", unitn);
    // emissive last
    uint32_t lastm; memcpy(&lastm,&T.back().MaterialSlot,4);
    CheckTrue("last triangle is a luminaire", M[lastm].Slabs[0].EmissionLuminance > 0.0f);
    int emissiveMats=0; for(const auto&m:M) if(m.Slabs[0].EmissionLuminance>0.0f) ++emissiveMats;
    CheckTrue("exactly 2 emissive materials", emissiveMats==2);
    // floor normal points up
    Vector3 f0 = N[0]; CheckTrue("floor normal is +Z", f0.z > 0.99f);
    // panel anchor sits inside the room, above the plinth, in front of rear wall
    Vector3 P = ShowroomStructure::QueryPanelOrigin();
    CheckTrue("panel origin inside room", P.x>-2.f&&P.x<2.f&&P.y<3.f&&P.z>0.f&&P.z<3.f);
    printf(Fail? "\n>>> %d FAILURE(S)\n" : "\n>>> ALL PASS (0 failures)\n", Fail);
    return Fail?1:0;
}
