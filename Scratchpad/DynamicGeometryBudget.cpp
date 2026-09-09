//============================================================================================================================================
//                                                    DYNAMICGEOMETRYBUDGET.CPP
//============================================================================================================================================
// 🧩 Measures the per-frame cost of keeping traced geometry in step with the rigid bodies, on the REAL drop
//    scene rather than a synthetic stand-in, and fails if it exceeds the frame budget.
//
//    This exists because the dominant cost is not where intuition puts it. Refitting the tree is cheap (~0.08 ms);
//    re-emitting the packed CWBVH blob is not (~2.7 ms), and it scales with the number of triangles that MOVE.
//    That makes tessellation of the dynamic bodies a per-frame cost, not a memory one — the single biggest lever
//    available without restructuring the acceleration structure.
//
//    Measured, 12 bodies, pre-AVX host:
//        16×32 → 960 tris/ball → 7.46 ms/frame · 0.27 px silhouette error
//        12×24 → 552 tris/ball → 2.57 ms/frame · 0.47 px   ← shipped: sub-pixel, 2.9× cheaper
//         8×16 → 224 tris/ball → 1.77 ms/frame · 1.06 px   — rejected, visibly facetted
//
//    Build: bash Scratchpad/CheckDynamicGeometryBudget.sh

// Real showroom drop scene: how long does the actual per-frame refit chain take?
#include "Projects/Project-Zero/Source/PhysicsInstanceSequence.h"
#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"
#include "Engine/GeometricRaster/TraversalIndex.h"
#include <cstdio>
#include <chrono>
using namespace Frontier;
using Clock=std::chrono::high_resolution_clock;
int main(){
    const char* P="/tmp/d6scene.gltf";
    ProjectZero::ShowroomStructure S; S.Construct(12u);
    std::string E; if(!S.Export(P,&E)){printf("export fail\n");return 1;}
    SceneStructure L; TextureIndex T; SceneDecodeConfiguration C;
    if(!SceneCodec::Decode(P,L,&T,C,&E)){printf("decode fail\n");return 1;}
    auto Facets=L.QueryFlatTriangles(); auto Rows=L.QueryInstances();
    uint32_t dyn=0; for(size_t i=Rows.size()-12;i<Rows.size();++i) dyn+=Rows[i].TriangleCount;
    printf("scene %u tris total, %u dynamic (%u per ball)\n",L.QueryTriangleCount(),dyn,dyn/12);
    TraversalIndex Tr; Tr.BuildBottomLevel(Facets,false);
    RigidBodyConfiguration SC; SC.FixedStepSeconds=1.f/60.f;
    RigidBodySolver Sv; Sv.Bring(SC);
    ProjectZero::PhysicsInstanceConfiguration BC;
    BC.DropCount=12; BC.FirstDropInstance=(uint32_t)Rows.size()-12;
    BC.BodyRadius=ProjectZero::ShowroomStructure::QueryDropRadius();
    ProjectZero::PhysicsInstanceSequence B; B.Construct(Sv,BC);
    // warm
    B.AdvancePhysics(Sv,Rows,1.f/60.f); B.RefreshBodyFacets(Facets,Rows); Tr.RefitBottomLevel(Facets);
    const int reps=60; double tot=0,peak=0;
    for(int f=0;f<reps;++f){
        B.AdvancePhysics(Sv,Rows,1.f/60.f);
        auto t0=Clock::now();
        B.RefreshBodyFacets(Facets,Rows);
        Tr.RefitBottomLevel(Facets);
        auto t1=Clock::now();
        double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
        tot+=ms; if(ms>peak)peak=ms;
    }
    const double Mean = tot/reps;
    printf("per-frame refresh+refit: mean %.3f ms  peak %.3f ms  (%.1f%% of 16.7 ms)\n",
        Mean,peak,100*Mean/16.7);

    // Budget guard. 5 ms is a third of a 60 Hz frame and is generous for 12 bodies; crossing it means either the
    //    dynamic tessellation crept up or the refit path regressed, and both are worth failing a build over.
    int Failed = 0;
    if (Mean > 5.0) { printf("  FAIL mean refit %.3f ms exceeds the 5.0 ms budget\n", Mean); Failed = 1; }
    if (dyn > 8000u) { printf("  FAIL %u dynamic triangles exceeds the 8000 budget\n", dyn); Failed = 1; }
    printf(Failed ? "\n>>> BUDGET EXCEEDED\n" : "\n>>> WITHIN BUDGET\n");
    return Failed;
}
