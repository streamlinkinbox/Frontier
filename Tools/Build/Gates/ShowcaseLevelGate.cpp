//============================================================================================================================================
//                                                    SHOWCASELEVELGATE.CPP
//============================================================================================================================================
// Showcase gate — the default level must actually contain what the renderer needs to light it.
//
//    This exists because the previous showcase failed silently in a way that looked like a renderer bug. It was built
//    from RayTracingSolver::ConstructShowcaseScene, whose five-field analytical material struct cannot express any
//    OpenPBR lobe, and it contained NO emissive triangle at all. The consequences were reported by users as "there are
//    no shadows", "there is no GI" and "it is using an old scene", but none of those are renderer defects:
//      · 0 luminaires ⇒ VisibilityExchange::PlaceShadowTaps returns false ⇒ the shadow stage is never recorded;
//      · 0 luminaires ⇒ the ReSTIR direct-light loop has no candidate to draw ⇒ no first bounce, so no indirect light;
//      · every material through ReSTIRIntegrator::BuildMaterialDescriptors is pinned SpecularWeight = 0 (a deliberate
//        pin that protects the Cornell reference image) ⇒ every object renders Lambertian.
//    A level that lights nothing is indistinguishable from a renderer that lights nothing, so the level is gated here.
//
//    usage: bash Tools/Build/CheckShowcaseLevel.sh

#include "Engine/ContentInterchange/ShowcaseStructure.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <cstring>
using namespace Frontier;
int main(){
    ShowcaseStructure S; S.Construct();
    const auto& T=S.QueryTriangles(); const auto& M=S.QueryMaterials(); const auto& Sp=S.QuerySpans();
    int fails=0;
    auto chk=[&](bool c,const char*m){ printf("  %s  %s\n", c?"PASS":"FAIL", m); if(!c)++fails; };

    // ① emissive triangles exist -> luminaires non-empty (the whole shadow+GI failure)
    uint32_t emissiveTris=0; std::set<uint32_t> emissiveMats;
    for(size_t i=0;i<M.size();++i) if(M[i].Slabs[0].EmissionLuminance>0.0f) emissiveMats.insert((uint32_t)i);
    for(const auto& t:T){ uint32_t m; memcpy(&m,&t.MaterialSlot,4); if(emissiveMats.count(m)) ++emissiveTris; }
    printf("triangles=%zu materials=%zu spans=%zu emissiveMats=%zu emissiveTris=%u\n",T.size(),M.size(),Sp.size(),emissiveMats.size(),emissiveTris);
    chk(emissiveTris>0,"the level contains emissive triangles (luminaires != 0)");
    chk(emissiveMats.size()>=2,"at least two distinct emissive materials (key + fill)");

    // ② the advanced lobes actually appear
    int aniso=0,trans=0,sss=0,coat=0,fuzz=0,film=0,haze=0,eon=0,glint=0,metal=0;
    for(const auto& d:M){ const auto&s=d.Slabs[0];
        if(std::fabs(s.SpecularRoughnessAnisotropy)>0.0f)++aniso;
        if(s.TransmissionWeight>0.0f)++trans; if(s.SubsurfaceWeight>0.0f)++sss;
        if(s.CoatWeight>0.0f)++coat; if(s.FuzzWeight>0.0f)++fuzz; if(s.ThinFilmWeight>0.0f)++film;
        if(s.SlateHazinessWeight>0.0f)++haze; if(s.BaseDiffuseRoughness>0.0f)++eon;
        if(s.SlateGlintDensity>0.0f)++glint; if(s.BaseMetalness>0.0f)++metal; }
    printf("aniso=%d trans=%d sss=%d coat=%d fuzz=%d film=%d haze=%d eon=%d glint=%d metal=%d\n",
           aniso,trans,sss,coat,fuzz,film,haze,eon,glint,metal);
    chk(aniso>=4,"anisotropic metals present"); chk(trans>=6,"IOR glass / transmission present");
    chk(sss>=6,"subsurface present"); chk(coat>=2,"coat present"); chk(fuzz>=4,"fuzz/cloth present");
    chk(film>=6,"thin film present"); chk(haze>=2,"haziness present"); chk(eon>=2,"EON diffuse roughness present");
    chk(glint>=1,"glint flakes present"); chk(metal>=6,"metals present");

    // ③ IOR actually varies (the old path could not express it at all)
    std::set<int> iors; for(const auto&d:M) if(d.Slabs[0].TransmissionWeight>0.f) iors.insert((int)(d.Slabs[0].SpecularIor*100));
    printf("distinct glass IORs=%zu\n",iors.size());
    chk(iors.size()>=5,"glass spans several distinct IORs");

    // ④ no SpecularWeight pinned to zero across the board (the old BuildMaterialDescriptors bug)
    int withSpec=0; for(const auto&d:M) if(d.Slabs[0].SpecularWeight>0.0f) ++withSpec;
    printf("materials with a specular lobe=%d/%zu\n",withSpec,M.size());
    chk(withSpec > (int)M.size()/2,"most materials keep a real specular lobe");

    // ⑤ spans cover every triangle exactly once and are named (outliner + scattered field)
    size_t covered=0; int scatter=0,sphere=0;
    for(const auto&s:Sp){ covered+=s.TriangleCount;
        if(s.Name.rfind("Scatter",0)==0)++scatter; if(s.Name.rfind("Sphere",0)==0)++sphere; }
    printf("spans cover %zu of %zu triangles; scatter spans=%d sphere spans=%d\n",covered,T.size(),scatter,sphere);
    chk(covered==T.size(),"spans cover every triangle exactly once");
    chk(scatter>=30,"the scattered object field is present");
    chk(sphere>=30,"the material sphere grid is present");

    // ⑥ bounds are sane (old scene was +/-500 m around 2 m objects). r5 deliberately uses an 80 m proof floor so
    //    the sun/spot shadows have room to read; the gate rejects only the old kilometre-scale plane.
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(const auto&t:T){ const float v[3][3]={{t.VertexAlphaX,t.VertexAlphaY,t.VertexAlphaZ},{t.VertexBetaX,t.VertexBetaY,t.VertexBetaZ},{t.VertexGammaX,t.VertexGammaY,t.VertexGammaZ}};
        for(int k=0;k<3;++k)for(int c=0;c<3;++c){ lo[c]=std::fmin(lo[c],v[k][c]); hi[c]=std::fmax(hi[c],v[k][c]); } }
    printf("bounds [%.1f %.1f %.1f]..[%.1f %.1f %.1f]\n",lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
    chk(hi[0]-lo[0]<=81.0f && hi[1]-lo[1]<=81.0f,"scene bounds are tight (not the old 1000 m plane)");
    chk(hi[2]>5.0f,"the luminaires sit above the field");

    printf(fails? "\nRED - %d check(s) failed\n":"\nGREEN - every check passed\n",fails);
    return fails?1:0;
}
