//============================================================================================================================================
//                                                     SHOWROOMLIGHTTEST.CPP
//============================================================================================================================================
// 🧩 The same light-contribution check against the REAL showroom, decoded through the real codec, rather than a
//    synthetic panel. Confirms the proxy joins the room's existing luminaires instead of replacing them, and that
//    it lands inside the room.
//
//    Build: bash Scratchpad/CheckLightProjection.sh

// Does the REAL showroom gain a panel luminaire?
#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Projects/Project-Zero/Source/InterfaceTrialSequence.h"
#include "Engine/SpatialInterface/InterfaceLightProjection.h"
#include "Engine/SpatialInterface/InterfaceSequence.h"
#include "Engine/DisplayPresentation/MotionIntegrator.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/ContentInterchange/TextureIndex.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
int main(){
    const char* P="/tmp/lightscene.gltf";
    ProjectZero::ShowroomStructure S; S.Construct(0u);
    std::string E; if(!S.Export(P,&E)){printf("export fail\n");return 1;}
    SceneStructure L; TextureIndex T; SceneDecodeConfiguration C;
    if(!SceneCodec::Decode(P,L,&T,C,&E)){printf("decode fail\n");return 1;}
    const size_t Before = L.QueryLuminaires().size();
    const uint32_t TrisBefore = L.QueryTriangleCount();
    printf("showroom as decoded: %u tris, %zu luminaires\n", TrisBefore, Before);

    // Panel at rest
    InterfaceStructure F; MotionIntegrator M; ProjectZero::InterfaceTrialSequence Tr;
    PlanePlacement Pl;
    const Vector3 A = ProjectZero::ShowroomStructure::QueryPanelOrigin();
    Pl.Origin=PlaneOrigin{A.x,A.y,A.z};
    Pl.RotationX=1.57079633f+ProjectZero::ShowroomStructure::QueryPanelTilt();
    Pl.Scale=2.2f;
    Tr.AssignPanelPlacement(Pl); Tr.Construct(F,M); Tr.AdvanceTrial(F,M,1.5,true);
    InterfaceSequence Comp; InterfaceViewConfiguration V;
    V.EyeY=-1.70f; V.EyeZ=1.45f; V.ForwardY=1.0f;
    Comp.AssignView(V); Comp.Advance(F,1.5);

    const float HW=0.115f*2.2f, HH=0.072f*2.2f;
    const float Tilt=ProjectZero::ShowroomStructure::QueryPanelTilt();
    PanelProxyRequest R;
    R.Tier=InterfaceFidelityTier::Low;
    R.CentreX=A.x; R.CentreY=A.y; R.CentreZ=A.z;
    R.RightX=HW; R.UpY=-HH*std::sin(Tilt); R.UpZ=HH*std::cos(Tilt);
    R.Gain=26.0f;
    const PanelRadiance Rad = InterfaceLightProjection::MeasureRadiance(F,Comp,4.0f*HW*HH);
    printf("panel radiance rgb (%.4f %.4f %.4f) from %u figures, coverage %.1f%%\n",
        Rad.Red,Rad.Green,Rad.Blue,Rad.Contributors,Rad.Coverage()*100.0);
    const uint32_t Inst = InterfaceLightProjection::ComposeProxy(L,R,Rad);
    if(Inst==0xFFFFFFFFu){printf("FAIL no proxy registered\n");return 1;}
    L.Finalise(64u,nullptr);
    printf("after proxy:         %u tris, %zu luminaires\n", L.QueryTriangleCount(), L.QueryLuminaires().size());
    int fail=0;
    if(L.QueryLuminaires().size()!=Before+2){printf("FAIL expected +2 luminaires\n");fail=1;}
    if(L.QueryTriangleCount()!=TrisBefore+2){printf("FAIL expected +2 triangles\n");fail=1;}
    // the panel must be INSIDE the room
    const Vector3 lo=L.QueryBoundsMinimum(), hi=L.QueryBoundsMaximum();
    printf("scene bounds X[%.2f %.2f] Y[%.2f %.2f] Z[%.2f %.2f]\n",lo.x,hi.x,lo.y,hi.y,lo.z,hi.z);
    if(!(lo.x>=-2.01f&&hi.x<=2.01f&&hi.z<=3.01f)){printf("FAIL proxy escaped the room\n");fail=1;}
    printf(fail?">>> FAILURES\n":">>> the panel is a light in the showroom\n");
    return fail;
}
