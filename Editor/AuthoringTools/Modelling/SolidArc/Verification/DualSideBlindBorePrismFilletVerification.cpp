// Phase 32s — preserve exactly two separated parallel side-entering blind cylindrical cavities while rounding a complete prism family.
#include "Kernel/BlendSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
using namespace Frontier;
namespace {
struct SideCavity { int Along=1;bool High=false;double X=10,Cross=6,Radius=1,Depth=8; };
Deliver<BrepBody> Fixture(const std::vector<SideCavity>& Cavities)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const SideCavity& C:Cavities)
    {
        Vec3 Origin,Axis;
        if(C.Along==1){Origin=C.High?Vec3{C.X,18,C.Cross}:Vec3{C.X,-2,C.Cross};Axis=C.High?Vec3{0,-1,0}:Vec3{0,1,0};}
        else{Origin=C.High?Vec3{C.X,C.Cross,14}:Vec3{C.X,C.Cross,-2};Axis=C.High?Vec3{0,0,-1}:Vec3{0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,C.Radius,C.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
        Working=std::move(Next);
    }
    return Working;
}
std::vector<int> Rails(const BrepBody& B)
{
    std::vector<int> Result;
    for(size_t I=0;I<B.Edges.size();++I)
    {
        const BrepEdge& E=B.Edges[I];if(E.Curve.Classification!=CurveClassification::Line||E.VertexStart<0||E.VertexEnd<0)continue;
        Vec3 P=B.Vertices[E.VertexStart].Point,Q=B.Vertices[E.VertexEnd].Point;
        if(std::fabs(std::fabs((Q-P).Normalised().Dot({1,0,0}))-1)>1e-8)continue;
        if((std::fabs(P.Y)<1e-8||std::fabs(P.Y-16)<1e-8)&&(std::fabs(P.Z)<1e-8||std::fabs(P.Z-12)<1e-8))
            Result.push_back(static_cast<int>(I));
    }
    return Result;
}
struct Inspection{bool Topology=false,Supports=false,Caps=false,Circles=false,Dimensions=false;int Annular=0,Triple=0;};
Inspection Inspect(const BrepBody& B,const std::vector<SideCavity>& Cavities)
{
    Inspection Q;auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==20&&B.Edges.size()==30&&
        B.Coedges.size()==60&&B.Loops.size()==16&&B.Faces.size()==14;
    int Planes=0,Rolls=0,CavitySurfaces=0,InnerLoops=0,ExactCircles=0;
    Vec3 Axis=Cavities.front().Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& S=F.Surface;
        if(S.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.Annular+=F.Loops.size()==2;Q.Triple+=F.Loops.size()==3;
            if(F.Loops.size()>1)InnerLoops+=static_cast<int>(F.Loops.size()-1);
        }
        if(S.Classification==SurfaceClassification::Cylinder&&S.Rational())
        {
            Rolls+=std::fabs(S.RadiusMajor-2)<1e-9;
            for(const SideCavity& C:Cavities)
            {
                double X=S.Origin.X,Cross=C.Along==1?S.Origin.Z:S.Origin.Y;
                CavitySurfaces+=std::fabs(S.RadiusMajor-C.Radius)<1e-9&&std::fabs(X-C.X)<1e-8&&
                    std::fabs(Cross-C.Cross)<1e-8&&std::fabs(std::fabs(S.Axis.Normalised().Dot(Axis))-1)<1e-8;
            }
        }
    }
    Q.Dimensions=true;
    for(const BrepEdge& E:B.Edges)if(E.Closed())ExactCircles+=E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    for(const SideCavity& C:Cavities)
    {
        double Length=C.Along==1?16:12,Entry=C.High?Length:0,Floor=C.High?Length-C.Depth:C.Depth;int Rims=0;
        Vec3 EntryCentre=C.Along==1?Vec3{C.X,Entry,C.Cross}:Vec3{C.X,C.Cross,Entry};
        Vec3 FloorCentre=C.Along==1?Vec3{C.X,Floor,C.Cross}:Vec3{C.X,C.Cross,Floor};
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
            Rims+=(E.Curve.Centre.Distance(EntryCentre)<1e-8||E.Curve.Centre.Distance(FloorCentre)<1e-8)&&
                  std::fabs(E.Curve.RadiusMajor-C.Radius)<1e-9&&std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(Axis))-1)<1e-8;
        Q.Dimensions&=Rims==2;
    }
    Q.Supports=Planes==8&&Rolls==4&&CavitySurfaces==2;Q.Caps=InnerLoops==2;Q.Circles=ExactCircles==4;
    return Q;
}
double ExactVolume(const std::vector<SideCavity>& Cavities)
{
    double Volume=20*(16*12-4*4*(1-ScalarCriteria::Pi/4));
    for(const SideCavity& C:Cavities)Volume-=ScalarCriteria::Pi*C.Radius*C.Radius*C.Depth;
    return Volume;
}
bool TransactionallyRefuses(const Deliver<BrepBody>& Source)
{
    if(!Source)return true;
    int Applied=99;auto Result=BlendSolver::FilletEdges(Source.Payload,Rails(Source.Payload),2,&Applied);
    return !Result&&Applied==0;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32s · Dual Side Blind-Bore Rounded-Prism Verification");
    const std::vector<SideCavity> Same{{1,false,6,4,.8,8},{1,false,14,8,1,10}};
    auto S=Fixture(Same);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Two separated cavities entering one retained side");
    P.Expect("Dual-side source is canonical V12/E18/C36/L12/F10 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==12&&S.Payload.Edges.size()==18&&S.Payload.Coedges.size()==36&&S.Payload.Loops.size()==12&&S.Payload.Faces.size()==10);
    int Exact=0,Fitted=0;for(const BrepEdge& X:S.Payload.Edges)if(X.Closed())
    {Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Source contains two exact floors and two fitted side-wall entrances",Exact==2&&Fitted==2);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,Same):Inspection{};
    P.Expect("Both parallel side cavities commit with all four outer rolls",R&&Applied==4);
    P.Expect("Output reaches exact V20/E30/C60/L16/F14 genus-zero topology",Q.Topology);
    P.Expect("Eight planes, four rolls, and two rational side-cavity cylinders remain",Q.Supports);
    P.Expect("One retained side carries both entrance loops",Q.Caps&&Q.Triple==1&&Q.Annular==0);
    P.Expect("All four side-cavity rims are exact rational circles",Q.Circles);
    P.Expect("Both side centres, radii, entry directions, and depths remain unchanged",Q.Dimensions);
    P.Within("Same-side rounded volume follows the exact two-cylinder removal",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Same)):1e9,.5);
    P.Expect("Outer bounds and the sharp dual-side source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==10);

    P.Section("Opposite sides, finite clearance, invariance, and refusal");
    const std::vector<SideCavity> Opposite{{1,false,6,5,1,7},{1,true,14,7,1.2,8}};
    auto O=Fixture(Opposite);int OA=-1;auto OR=BlendSolver::FilletEdges(O.Payload,Rails(O.Payload),2,&OA);auto OQ=OR?Inspect(OR.Payload,Opposite):Inspection{};
    P.Expect("Separated cavities entering opposite parallel sides use the exact route",OR&&OA==4&&OQ.Topology&&OQ.Dimensions);
    P.Expect("Opposite retained sides remain separately annular",OQ.Caps&&OQ.Annular==2&&OQ.Triple==0);
    P.Within("Opposite-side volume retains both finite depths",OR?std::fabs(OR.Payload.Validate().Volume-ExactVolume(Opposite)):1e9,.5);
    const std::vector<SideCavity> Coaxial{{1,false,10,6,1,6},{1,true,10,6,1,6}};
    auto C=Fixture(Coaxial);int CA=-1;auto CR=BlendSolver::FilletEdges(C.Payload,Rails(C.Payload),2,&CA);
    P.Expect("Coaxial opposite-side cavities survive across a positive axial ligament",CR&&CA==4&&Inspect(CR.Payload,Coaxial).Dimensions);
    std::vector<double> RimY;if(CR)for(const BrepEdge& X:CR.Payload.Edges)if(X.Closed())RimY.push_back(X.Curve.Centre.Y);std::sort(RimY.begin(),RimY.end());
    P.Expect("Coaxial side floors preserve the requested four-unit ligament",RimY.size()==4&&std::fabs(RimY[1]-6)<1e-8&&std::fabs(RimY[2]-10)<1e-8);
    const std::vector<SideCavity> AlongZ{{2,true,6,5,.8,5},{2,true,14,10,.9,7}};
    auto Z=Fixture(AlongZ);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),2,&ZA);
    P.Expect("Two cavities parallel to Z preserve the orthogonal side direction",ZR&&ZA==4&&Inspect(ZR.Payload,AlongZ).Dimensions);
    auto Reversed=Fixture({Same[1],Same[0]});int ReverseApplied=-1;auto ReverseR=BlendSolver::FilletEdges(Reversed.Payload,Rails(Reversed.Payload),2,&ReverseApplied);
    P.Expect("Reversed Boolean construction order yields the same rounded volume",ReverseR&&ReverseApplied==4&&std::fabs(ReverseR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=S.Payload.Transformed(M);int TA=-1;auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve dual-side classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==14);
    P.Within("Rigid transforms preserve dual-side volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto Corner=Fixture({Same[0],{1,false,14,2.5,1,8}});P.Expect("Either entrance crossing a rounded corner strip refuses transactionally",Corner&&TransactionallyRefuses(Corner));
    auto End=Fixture({Same[0],{1,false,1.000000005,8,1,8}});P.Expect("Either entrance touching a selected-axis end cap refuses transactionally",End&&TransactionallyRefuses(End));
    auto Intersecting=Fixture({{1,false,8,6,1.5,8},{1,false,9,6,1.5,7}});P.Expect("Intersecting side cavities cannot partially commit",TransactionallyRefuses(Intersecting));
    auto Mixed=Fixture({{1,false,5,4,.7,6},{2,false,15,10,.8,5}});P.Expect("Mixed Y/Z side-cavity axes remain outside the parallel pair route",Mixed&&TransactionallyRefuses(Mixed));
    auto Through=Fixture({Same[0],{1,false,14,8,1,16}});P.Expect("A side cavity reaching the opposite wall remains outside the blind pair route",Through&&TransactionallyRefuses(Through));
    auto Three=Fixture({{1,false,4,4,.6,6},{1,false,10,8,.6,7},{1,false,16,4,.6,8}});int ThreeApplied=-1;
    auto ThreeR=BlendSolver::FilletEdges(Three.Payload,Rails(Three.Payload),2,&ThreeApplied);
    P.Expect("A third parallel side cavity delegates to the bounded-set route",Three&&ThreeR&&ThreeApplied==4&&ThreeR.Payload.Faces.size()==16);
    auto Single=Fixture({Same[0]});int SingleApplied=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),2,&SingleApplied);
    P.Expect("The established single side-cavity route remains intact",SingleR&&SingleApplied==4&&SingleR.Payload.Faces.size()==12);
    auto AxialBox=BrepBody::Box({0,0,0},{20,16,12});auto AxialCut=BrepBody::Cylinder({22,8,6},{-1,0,0},1,10);
    auto Axial=IntersectionSolver::Combine(AxialBox.Payload,AxialCut.Payload,BodyOperation::Subtract);int AxialApplied=-1;
    auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),2,&AxialApplied);
    P.Expect("The established selected-axis blind route remains intact",AxialR&&AxialApplied==4&&AxialR.Payload.Faces.size()==12);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("DualSide",S.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet DualSide 2 --edges="+List+" --name=DualSideRound");auto* Output=H.Document().Find("DualSideRound");
    P.Expect("Console fillet commits one exact dual-side-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==14);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32s_DualSideBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("DualSideSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("DualSideRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("SameSide",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("OppositeSides",OR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("AlongZ",ZR.Payload,5)&&V.Execute("view top")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32s_DualSideBlindBorePrism");
    P.Expect("C++ dual-side-bore proof commands complete",Rendered);
    P.Expect("C++ dual-side-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
