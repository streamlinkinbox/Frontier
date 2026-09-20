// Phase 32e — exact equal-radius orthogonal three-face corner patch.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
using namespace Frontier;

namespace
{
std::vector<int> Incident(const BrepBody& Body, int Vertex)
{
    std::vector<int> Out;
    for (size_t E=0; E<Body.Edges.size(); ++E)
        if (Body.Edges[E].VertexStart==Vertex || Body.Edges[E].VertexEnd==Vertex) Out.push_back(static_cast<int>(E));
    return Out;
}

bool Circle(const NurbsCurve& C, Vec3& Centre, double& Radius)
{
    if (C.Classification!=CurveClassification::Arc && C.Classification!=CurveClassification::Circle) return false;
    double A=C.DomainStart(), B=C.DomainEnd(); Vec3 P0=C.Sample(A), P1=C.Sample((A+B)*0.5), P2=C.Sample(B);
    Vec3 U=P1-P0,V=P2-P0,N=U.Cross(V); double Den=2*N.LengthSquared(); if(Den<1e-14)return false;
    Centre=P0+(N.Cross(U)*V.LengthSquared()+V.Cross(N)*U.LengthSquared())/Den; Radius=Centre.Distance(P0); return true;
}

struct Check { bool Topology=false, Supports=false, ExactSphere=false, ExactEdges=false; double SphereResidual=1e9, SeamBreak=1e9; };
Check Inspect(const BrepBody& Body, Vec3 Centre, double Radius)
{
    Check R; BodyReport Q=Body.Validate();
    R.Topology=Q.Solid()&&Q.Hulls==1&&Q.Genus==0&&Body.Vertices.size()==13&&Body.Edges.size()==21&&
        Body.Coedges.size()==42&&Body.Loops.size()==10&&Body.Faces.size()==10;
    int Sphere=-1, Planes=0, Cylinders=0; std::vector<int> CylinderFaces;
    for(size_t F=0;F<Body.Faces.size();++F){auto K=Body.Faces[F].Surface.Classification;
        if(K==SurfaceClassification::Plane)++Planes;
        if(K==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(F));}
        if(K==SurfaceClassification::Sphere)Sphere=static_cast<int>(F);}
    R.Supports=Planes==6&&Cylinders==3&&Sphere>=0;
    if(Sphere<0)return R;
    const NurbsSurface&S=Body.Faces[Sphere].Surface;
    R.ExactSphere=S.Rational()&&S.Origin.Distance(Centre)<1e-9&&std::fabs(S.RadiusMajor-Radius)<1e-10;
    R.SphereResidual=0; for(int I=0;I<=8;++I)for(int J=0;J<=8;++J){double U=S.DomainStartU()+(S.DomainEndU()-S.DomainStartU())*I/8.0;
        double V=S.DomainStartV()+(S.DomainEndV()-S.DomainStartV())*J/8.0;R.SphereResidual=std::max(R.SphereResidual,std::fabs(S.Sample(U,V).Distance(Centre)-Radius));}
    int RadiusArcs=0, SphereSeams=0; R.SeamBreak=0;
    for(size_t E=0;E<Body.Edges.size();++E){Vec3 C;double RR=0;if(Circle(Body.Edges[E].Curve,C,RR)&&std::fabs(RR-Radius)<1e-8)++RadiusArcs;
        bool OnSphere=false;int Cylinder=-1;for(int Ce:Body.Edges[E].Coedges){int F=Body.Coedges[Ce].Face;if(F==Sphere)OnSphere=true;if(std::find(CylinderFaces.begin(),CylinderFaces.end(),F)!=CylinderFaces.end())Cylinder=F;}
        if(OnSphere&&Cylinder>=0){++SphereSeams;const NurbsCurve&K=Body.Edges[E].Curve;Vec3 P=K.Sample((K.DomainStart()+K.DomainEnd())*.5);
            Vec3 NS=(P-Centre).Normalised();const NurbsSurface&CY=Body.Faces[Cylinder].Surface;Vec3 A=CY.Axis.Normalised();Vec3 NC=(P-(CY.Origin+A*(P-CY.Origin).Dot(A))).Normalised();
            R.SeamBreak=std::max(R.SeamBreak,1.0-std::fabs(NS.Dot(NC)));}}
    R.ExactEdges=RadiusArcs==6&&SphereSeams==3&&Body.Faces[Sphere].Loops.size()==1&&Body.Loops[Body.Faces[Sphere].Loops[0]].Coedges.size()==3;
    return R;
}

double Removal(double A,double B,double C,double R)
{
    double Strip=R*R*(1.0-ScalarCriteria::Pi/4.0);
    return Strip*(A+B+C-3.0*R)+R*R*R*(1.0-ScalarCriteria::Pi/6.0);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32e · Orthogonal Three-Face Corner Verification");
    auto SourceResult=BrepBody::Box({0,0,0},{20,16,12}); BrepBody Source=SourceResult.Payload; BodyReport Before=Source.Validate();
    std::vector<int> Corner=Incident(Source,0);
    Panel.Section("Exact trihedral corner construction");
    Panel.Expect("The source corner has three incident straight edges",Corner.size()==3);
    int Applied=-1;auto Rounded=BlendSolver::FilletEdges(Source,Corner,1.0,&Applied);
    Panel.Expect("Three orthogonal incident edges commit as one transaction",Rounded&&Applied==3);
    Check Inspection=Rounded?Inspect(Rounded.Payload,{1,1,1},1):Check{};
    Panel.Expect("The corner result is one-hull V13/E21/C42/L10/F10",Inspection.Topology);
    Panel.Expect("Six planes, three exact cylinders, and one sphere remain",Inspection.Supports);
    Panel.Expect("The corner patch retains rational analytic sphere identity",Inspection.ExactSphere);
    Panel.Within("Spherical octant radius residual",Inspection.SphereResidual,1e-9);
    Panel.Expect("Three sphere seams and six exact R1 end/seam arcs remain",Inspection.ExactEdges);
    Panel.Within("Sphere-to-cylinder G1 break",Inspection.SeamBreak,1e-10);
    Panel.Expect("The rounded body retains the source extents",Rounded&&Rounded.Payload.Bounds().Low.Distance(Source.Bounds().Low)<1e-9&&Rounded.Payload.Bounds().High.Distance(Source.Bounds().High)<1e-9);
    double Expected=Before.Volume-Removal(20,16,12,1);
    Panel.Expect("The convex trihedral blend removes material",Rounded&&Rounded.Payload.Validate().Volume<Before.Volume);
    Panel.Within("Trihedral volume follows the exact strips-plus-octant formula",Rounded?std::fabs(Rounded.Payload.Validate().Volume-Expected):1e9,0.2);
    Panel.Expect("The source body remains unchanged",Source.Vertices.size()==8&&Source.Edges.size()==12&&std::fabs(Source.Validate().Volume-Before.Volume)<1e-12);

    Panel.Section("Determinism, transforms, and refusal boundary");
    int ReorderedCount=-1;auto Reordered=BlendSolver::FilletEdges(Source,{Corner[2],Corner[0],Corner[1],Corner[2]},1.0,&ReorderedCount);
    Panel.Expect("Input order and a repeated seed still apply exactly three chains",Reordered&&ReorderedCount==3);
    Panel.Within("Reordered and canonical corner volumes are identical",Rounded&&Reordered?std::fabs(Rounded.Payload.Validate().Volume-Reordered.Payload.Validate().Volume):1e9,1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,2})*Mat4::Rotation({1,2,3},0.63);BrepBody Tilted=Source.Transformed(Transform);
    auto TiltCorner=Incident(Tilted,0);int TiltCount=-1;auto TiltRoll=BlendSolver::FilletEdges(Tilted,TiltCorner,1.0,&TiltCount);
    Panel.Expect("A rigidly transformed orthogonal box retains exact corner topology",TiltRoll&&TiltCount==3&&TiltRoll.Payload.Validate().Solid()&&TiltRoll.Payload.Faces.size()==10);
    Panel.Within("Rigid transformation preserves rounded volume within integration tolerance",Rounded&&TiltRoll?std::fabs(Rounded.Payload.Validate().Volume-TiltRoll.Payload.Validate().Volume):1e9,0.2);
    int Refused=99;auto Pair=BlendSolver::FilletEdges(Source,{Corner[0],Corner[1]},1.0,&Refused);
    Panel.Expect("A two-edge corner still refuses without inventing a transition",!Pair&&Refused==0&&std::string(Pair.Denial.Detail)=="multi-edge fillet chains share a vertex (corner resolution is not supported)");
    Refused=99;auto Path=BlendSolver::FilletEdges(Source,{0,1,2},1.0,&Refused);
    Panel.Expect("Three edges without one common vertex refuse transactionally",!Path&&Refused==0);
    Refused=99;auto Huge=BlendSolver::FilletEdges(Source,Corner,12.0,&Refused);
    Panel.Expect("A radius consuming a selected box edge refuses cleanly",!Huge&&Refused==0&&std::string(Huge.Denial.Detail)=="corner fillet radius consumes one of the three selected box edges");

    Panel.Section("Console transaction and proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied
#endif
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER,1280,800);bool ConsoleOkay=Host.Execute("box (0,0,0) (20,16,12) --name=CornerBox")&&
        Host.Execute("fillet CornerBox 1 --edges="+std::to_string(Corner[0])+","+std::to_string(Corner[1])+","+std::to_string(Corner[2])+" --name=Trihedral");
    const SceneFigure* ConsoleResult=Host.Document().Find("Trihedral");
    Panel.Expect("The C++ console commits the three-edge corner transaction",ConsoleOkay);
    Panel.Expect("The console result contains one exact spherical corner patch",ConsoleResult&&Inspect(ConsoleResult->Body,{1,1,1},1).ExactSphere);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32e_CornerFillet.png";std::error_code Error;std::filesystem::remove(Proof,Error);
    ConsoleHost P(SOLIDARC_PROOF_FOLDER,1280,800);auto Reset=[&](){return P.Execute("reset")&&P.Execute("gizmo off");};
    auto Add=[&](const char*N,BrepBody B,uint8_t M){auto&F=P.Document().AddBody(N,std::move(B));F.Matcap=M;return true;};
    bool Rendered=Reset()&&Add("Sharp",Source.Transformed(Mat4::Translation({-24,0,0})),0)&&Add("Rounded",Rounded.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&P.Execute("view iso")&&P.Execute("view fit")&&P.Execute("render sheet 0")&&
        Reset()&&Add("CornerClose",Rounded.Payload,3)&&P.Execute("view iso")&&P.Execute("view fit")&&P.Execute("view dolly 0.65")&&P.Execute("render sheet 1")&&
        Reset()&&Add("CornerTop",Rounded.Payload,2)&&P.Execute("view top")&&P.Execute("view fit")&&P.Execute("render sheet 2")&&
        Reset()&&Add("TiltedCorner",TiltRoll.Payload,5)&&P.Execute("view iso")&&P.Execute("view fit")&&P.Execute("render sheet 3")&&P.Execute("render sheet finalize Phase32e_CornerFillet");
    Panel.Expect("C++ corner proof commands complete",Rendered);
    Panel.Expect("C++ corner proof is written and non-trivial",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return Panel.Conclude();
}
