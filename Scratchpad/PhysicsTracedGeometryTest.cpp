//============================================================================================================================================
//                                                PHYSICSTRACEDGEOMETRYTEST.CPP
//============================================================================================================================================
// 🧩 The D5 gate, end to end: Jolt poses → flat world-space triangles → acceleration-structure refit → a ray
//    actually finds the body where it is now drawn.
//
//    This is the check that distinguishes D5 from D4. After D4 the balls MOVE but the structure still holds their
//    launch positions, so shadows and reflections stay behind — the bodies float free of their own shadows. The
//    decisive assertions here are the last three: the ray hits the ball at its settled height, the ball has
//    genuinely moved, and NO phantom remains at the rest pose.
//
//    Build: bash Scratchpad/CheckTracedGeometry.sh

// End-to-end: Jolt poses -> flat triangles -> refit -> rays actually see the bodies where they now are.
#include "Projects/Project-Zero/Source/PhysicsInstanceSequence.h"
#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"
#include "Engine/GeometricRaster/TraversalIndex.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
int Fail=0;
void Check(const char*N,bool C){ printf("  %-62s %s\n",N,C?"PASS":"FAIL"); if(!C)++Fail; }
int main(){
    const char* P="Projects/Project-Zero/Content/Scenes/ShowroomDrop.gltf";
    ProjectZero::ShowroomStructure S; S.Construct(12u);
    std::string E; if(!S.Export(P,&E)){printf("export failed\n");return 1;}
    SceneStructure L; TextureIndex T; SceneDecodeConfiguration C;
    if(!SceneCodec::Decode(P,L,&T,C,&E)){printf("decode failed\n");return 1;}
    printf("scene: %u tris, %zu instances\n\n", L.QueryTriangleCount(), L.QueryInstances().size());

    auto Facets = L.QueryFlatTriangles();
    auto Rows   = L.QueryInstances();
    TraversalIndex Tr;
    Check("build refittable tree", Tr.BuildBottomLevel(Facets,false));
    Check("tree is refittable", Tr.IsRefittable());

    RigidBodyConfiguration SC; SC.FixedStepSeconds=1.0f/60.0f;
    RigidBodySolver Solver; Solver.Bring(SC);
    ProjectZero::PhysicsInstanceConfiguration BC;
    BC.DropCount=12; BC.FirstDropInstance=(uint32_t)Rows.size()-12;
    BC.BodyRadius=ProjectZero::ShowroomStructure::QueryDropRadius();
    ProjectZero::PhysicsInstanceSequence B;
    Check("bridge constructs", B.Construct(Solver,BC));

    // Ray straight down onto body 0's rest position: should hit the ball at rest.
    Vector3 R0 = ProjectZero::ShowroomStructure::QueryDropOrigin(0);
    auto TraceDown=[&](float x,float y)->float{
        float O[3]={x,y,2.95f}, D[3]={0,0,-1}; float dist; uint32_t prim;
        return Tr.TraceClosest(O,D,dist,prim)? 2.95f-dist : -999.f; };

    B.AdvancePhysics(Solver,Rows,0.0f);
    B.RefreshBodyFacets(Facets,Rows);
    Check("refit at rest", Tr.RefitBottomLevel(Facets));
    float atRest = TraceDown(R0.x,R0.y);
    printf("  ray down at body 0: surface z = %.3f (ball top should be ~%.3f)\n", atRest, R0.z+BC.BodyRadius);
    Check("ray sees the ball at its rest height", std::fabs(atRest-(R0.z+BC.BodyRadius))<0.10f);

    // Let them fall and settle, then trace again.
    for(int i=0;i<1200;++i){ B.AdvancePhysics(Solver,Rows,1.0f/60.0f); }
    B.RefreshBodyFacets(Facets,Rows);
    Check("refit after settling", Tr.RefitBottomLevel(Facets));
    printf("  refit cost %.3f ms\n", Tr.QueryRefitMilliseconds());

    // Where is body 0 now? Use its instance transform.
    const float* M = Rows[BC.FirstDropInstance].World;
    float wx=M[12]+R0.x, wy=M[13]+R0.y, wz=M[14]+R0.z;
    printf("  body 0 settled at (%.2f %.2f %.2f)\n",wx,wy,wz);
    float after = TraceDown(wx,wy);
    printf("  ray down there: surface z = %.3f (expect ~%.3f)\n", after, wz+BC.BodyRadius);
    Check("traced geometry FOLLOWED the body", std::fabs(after-(wz+BC.BodyRadius))<0.10f);
    Check("the ball is no longer at its old height", std::fabs(wz-R0.z)>0.3f);

    // And the old location should now be empty floor, not a phantom ball.
    float ghost = TraceDown(R0.x,R0.y);
    printf("  ray at the ORIGINAL spot: surface z = %.3f\n", ghost);
    Check("no phantom ball left behind at the rest pose", ghost < R0.z-0.2f);

    printf(Fail?"\n>>> %d FAILURE(S)\n":"\n>>> ALL PASS (0 failures)\n",Fail);
    return Fail?1:0;
}
