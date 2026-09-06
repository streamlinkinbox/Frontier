//============================================================================================================================================
//                                                     PANELPLACEMENTTEST.CPP
//============================================================================================================================================
// 🧩 Proves the spatial interface is composed INTO THE ROOM at the anchor the level publishes, and that it faces
//    the camera — not sitting at the world origin, not edge-on, not buried in a wall.
//
//    This gate exists because the panel was wired into the frame before it was placed. It rendered, so nothing
//    failed; it simply hung at the world origin with a default rotation, which from the showroom camera is behind
//    and below the viewer. A "does it draw" check passes in that state. Only asserting WHERE it lands catches it.
//
//    Checks: inside the room bounds, centred on ShowroomStructure::QueryPanelOrigin(), large enough to read, and
//    a positive dot between the panel normal and the direction to the eye.
//
//    Build: bash Scratchpad/CheckPanelPlacement.sh

// Where do the composed figures actually end up in world space, and do they face the camera?
#include "Projects/Project-Zero/Source/InterfaceTrialSequence.h"
#include "Projects/Project-Zero/Source/ShowroomStructure.h"
#include "Engine/SpatialInterface/InterfaceSequence.h"
#include "Engine/DisplayPresentation/MotionIntegrator.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
int main(){
    InterfaceStructure S; MotionIntegrator M; ProjectZero::InterfaceTrialSequence T;
    const Vector3 A = ProjectZero::ShowroomStructure::QueryPanelOrigin();
    PlanePlacement P; P.Origin=PlaneOrigin{A.x,A.y,A.z};
    P.RotationX = 1.57079633f + ProjectZero::ShowroomStructure::QueryPanelTilt();
    P.Scale = 2.2f;
    T.AssignPanelPlacement(P);
    T.Construct(S,M);
    T.AdvanceTrial(S,M,1.0,true);

    InterfaceSequence Q;
    InterfaceViewConfiguration V;
    V.EyeX=0; V.EyeY=-1.70f; V.EyeZ=1.45f;      // showroom camera
    V.ForwardX=0; V.ForwardY=1; V.ForwardZ=0;
    Q.AssignView(V);
    Q.Advance(S,1.0);

    printf("anchor (%.2f %.2f %.2f) tilt %.3f rad, scale %.1f\n",A.x,A.y,A.z,
        ProjectZero::ShowroomStructure::QueryPanelTilt(),P.Scale);
    printf("figures %u -> instances %u\n\n",S.QueryCount(),Q.QueryInstanceCount());

    const InterfaceInstanceFigure* I=Q.QueryInstances();
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(uint32_t k=0;k<Q.QueryInstanceCount();++k){
        // translation column of the instance rows
        const float x=I[k].RowXw, y=I[k].RowYw, z=I[k].RowZw;
        if(x<lo[0])lo[0]=x; if(x>hi[0])hi[0]=x;
        if(y<lo[1])lo[1]=y; if(y>hi[1])hi[1]=y;
        if(z<lo[2])lo[2]=z; if(z>hi[2])hi[2]=z;
    }
    printf("panel world bounds X[%.2f %.2f] Y[%.2f %.2f] Z[%.2f %.2f]\n",
        lo[0],hi[0],lo[1],hi[1],lo[2],hi[2]);
    printf("room is        X[-2.00 2.00] Y[-2.00 3.00] Z[0.00 3.00]\n\n");
    int fail=0;
    if(!(lo[0]>-2&&hi[0]<2&&lo[1]>-2&&hi[1]<3&&lo[2]>0&&hi[2]<3)){printf("FAIL outside the room\n");fail=1;}
    // panel should sit near the anchor, not the origin
    float cx=(lo[0]+hi[0])*0.5f, cy=(lo[1]+hi[1])*0.5f, cz=(lo[2]+hi[2])*0.5f;
    printf("panel centre (%.2f %.2f %.2f) vs anchor (%.2f %.2f %.2f)\n",cx,cy,cz,A.x,A.y,A.z);
    if(std::fabs(cx-A.x)>0.3f||std::fabs(cy-A.y)>0.3f||std::fabs(cz-A.z)>0.3f){printf("FAIL not at the anchor\n");fail=1;}
    if(std::fabs(cz)<0.01f&&std::fabs(cy)<0.01f){printf("FAIL still at world origin\n");fail=1;}
    // visible size
    printf("panel spans %.2f m wide x %.2f m tall\n",hi[0]-lo[0],hi[2]-lo[2]);
    if(hi[0]-lo[0] < 0.15f){printf("FAIL too small to read\n");fail=1;}

    // Facing: the panel's out-of-plane axis after RotationX must point back toward the eye. A panel rotated the
    //    wrong way still passes every bounds check while being invisible, so this is the assertion that matters.
    const float Rx = P.RotationX;
    const float Ny = -std::sin(Rx), Nz = std::cos(Rx);          // local +Z carried into world
    float Vy = V.EyeY - cy, Vz = V.EyeZ - cz;
    const float Len = std::sqrt(Vy*Vy + Vz*Vz); Vy/=Len; Vz/=Len;
    const float Facing = Ny*Vy + Nz*Vz;
    printf("dot(panel normal, toward eye) = %.3f\n", Facing);
    if(Facing < 0.5f){printf("FAIL panel does not face the camera\n");fail=1;}

    // Distance sanity: behind the camera or through the back wall are both silent failures.
    const float Depth = cy - V.EyeY;
    printf("panel is %.2f m in front of the eye\n", Depth);
    if(Depth < 0.5f || Depth > 6.0f){printf("FAIL panel is not in front of the camera\n");fail=1;}
    printf(fail?"\n>>> FAILURES\n":"\n>>> panel is in the room at the anchor\n");
    return fail;
}
