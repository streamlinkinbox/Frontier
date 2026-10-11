// Phase 32t — preserve three through eight separated parallel side-entering blind cylindrical cavities while rounding a complete prism family.
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
Deliver<BrepBody> Fixture(const std::vector<SideCavity>& Cavities,bool FloorOutward=false)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const SideCavity& C:Cavities)
    {
        Vec3 Origin,Axis;
        if(C.Along==1){Origin=C.High?(FloorOutward?Vec3{C.X,16-C.Depth,C.Cross}:Vec3{C.X,18,C.Cross}):Vec3{C.X,-2,C.Cross};
            Axis=C.High&&!FloorOutward?Vec3{0,-1,0}:Vec3{0,1,0};}
        else{Origin=C.High?(FloorOutward?Vec3{C.X,C.Cross,12-C.Depth}:Vec3{C.X,C.Cross,14}):Vec3{C.X,C.Cross,-2};
            Axis=C.High&&!FloorOutward?Vec3{0,0,-1}:Vec3{0,0,1};}
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
struct Inspection{bool Topology=false,Supports=false,Caps=false,Circles=false,Dimensions=false;int FourLoop=0,FiveLoop=0,NineLoop=0;};
Inspection Inspect(const BrepBody& B,const std::vector<SideCavity>& Cavities)
{
    Inspection Q;const size_t N=Cavities.size();auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==16+2*N&&B.Edges.size()==24+3*N&&
        B.Coedges.size()==48+6*N&&B.Loops.size()==10+3*N&&B.Faces.size()==10+2*N;
    int Planes=0,Rolls=0,CavitySurfaces=0,InnerLoops=0,ExactCircles=0;
    Vec3 Axis=Cavities.front().Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& S=F.Surface;
        if(S.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.FourLoop+=F.Loops.size()==4;Q.FiveLoop+=F.Loops.size()==5;Q.NineLoop+=F.Loops.size()==9;
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
    Q.Supports=Planes==static_cast<int>(6+N)&&Rolls==4&&CavitySurfaces==static_cast<int>(N);
    Q.Caps=InnerLoops==static_cast<int>(N);Q.Circles=ExactCircles==static_cast<int>(2*N);return Q;
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
    VerificationPanel P("SolidArc · Phase 32t · Multi Side Blind-Bore Rounded-Prism Verification");
    const std::vector<SideCavity> Three{{1,false,5,4,.7,6},{1,false,10,8,.8,7},{1,false,15,4,.6,8}};
    auto S=Fixture(Three);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Three separated cavities entering one retained side");
    P.Expect("Triple-side source follows V14/E21/C42/L15/F12 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==14&&S.Payload.Edges.size()==21&&S.Payload.Coedges.size()==42&&S.Payload.Loops.size()==15&&S.Payload.Faces.size()==12);
    int Exact=0,Fitted=0;for(const BrepEdge& X:S.Payload.Edges)if(X.Closed())
    {Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Triple source contains three exact floors and three fitted entrances",Exact==3&&Fitted==3);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,Three):Inspection{};
    P.Expect("All three parallel side cavities commit with all four outer rolls",R&&Applied==4);
    P.Expect("Triple output reaches V22/E33/C66/L19/F16 genus-zero topology",Q.Topology&&R.Payload.Vertices.size()==22&&R.Payload.Faces.size()==16);
    P.Expect("Nine planes, four rolls, and three rational cavity cylinders remain",Q.Supports);
    P.Expect("One retained side carries exactly three entrance loops",Q.Caps&&Q.FourLoop==1);
    P.Expect("All six triple-cavity rims are exact rational circles",Q.Circles);
    P.Expect("Every triple-cavity centre, radius, direction, and depth remains exact",Q.Dimensions);
    P.Within("Triple-side volume follows three finite-cylinder removals",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Three)):1e9,.5);
    P.Expect("Outer bounds and the sharp triple source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==12);

    P.Section("Eight-cavity bound, alternate direction, invariance, and refusal");
    const std::vector<SideCavity> Eight{{1,false,4,4,.55,6},{1,false,8,8,.55,7},{1,false,12,4,.55,5},{1,false,16,8,.55,6},
        {1,true,4,8,.55,5},{1,true,8,4,.55,6},{1,true,12,8,.55,7},{1,true,16,4,.55,5}};
    auto M=Fixture(Eight,true);auto MRails=Rails(M.Payload);auto MSource=M.Payload.Validate();
    P.Expect("Eight-cavity source reaches the explicit V24/E36/C72/L30/F22 bound",M&&MSource.Solid()&&MSource.Genus==0&&M.Payload.Vertices.size()==24&&
        M.Payload.Edges.size()==36&&M.Payload.Coedges.size()==72&&M.Payload.Loops.size()==30&&M.Payload.Faces.size()==22);
    int MA=-1;auto MR=BlendSolver::FilletEdges(M.Payload,MRails,2,&MA);auto MQ=MR?Inspect(MR.Payload,Eight):Inspection{};
    P.Expect("Eight separated side cavities commit at the bounded maximum",MR&&MA==4&&MQ.Topology&&MR.Payload.Vertices.size()==32&&MR.Payload.Faces.size()==26);
    P.Expect("Four entrances on each opposite side retain two five-loop walls",MQ.Caps&&MQ.FiveLoop==2&&MQ.NineLoop==0);
    P.Expect("All sixteen bounded-set rims remain exact rational circles",MQ.Circles&&MQ.Dimensions);
    P.Expect("The bounded entrance set retains positive finite-cylinder ligaments",MQ.Supports);
    P.Within("Eight-cavity volume follows every finite removal",MR?std::fabs(MR.Payload.Validate().Volume-ExactVolume(Eight)):1e9,.5);
    const std::vector<SideCavity> OppositeThree{{1,false,5,4,.7,6},{1,false,10,8,.7,7},{1,true,15,4,.7,6}};
    auto Opposite=Fixture(OppositeThree);int OppositeApplied=-1;auto OppositeR=BlendSolver::FilletEdges(Opposite.Payload,Rails(Opposite.Payload),2,&OppositeApplied);
    P.Expect("A bounded set may span opposite parallel retained sides",OppositeR&&OppositeApplied==4&&Inspect(OppositeR.Payload,OppositeThree).Dimensions);
    const std::vector<SideCavity> AlongZ{{2,true,6,5,.8,5},{2,true,14,10,.9,7}};
    auto Z=Fixture(AlongZ);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),2,&ZA);
    P.Expect("The established alternate-direction side route remains intact",ZR&&ZA==4&&Inspect(ZR.Payload,AlongZ).Dimensions);
    std::vector<SideCavity> Reverse=Eight;std::reverse(Reverse.begin(),Reverse.end());auto ReverseSource=Fixture(Reverse,true);int ReverseApplied=-1;
    auto ReverseR=BlendSolver::FilletEdges(ReverseSource.Payload,Rails(ReverseSource.Payload),2,&ReverseApplied);
    P.Expect("Reversed eight-cavity construction order preserves deterministic volume",ReverseR&&ReverseApplied==4&&std::fabs(ReverseR.Payload.Validate().Volume-MR.Payload.Validate().Volume)<1e-9);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=M.Payload.Transformed(Transform);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,MRails,2,&TA);
    P.Expect("Rigid transforms preserve bounded side-cavity classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==26);
    P.Within("Rigid transforms preserve bounded side-cavity volume",MR&&TR?std::fabs(MR.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto Corner=Fixture({Three[0],Three[1],{1,false,15,2.5,1,8}});P.Expect("Any entrance crossing a rounded corner strip refuses transactionally",Corner&&TransactionallyRefuses(Corner));
    auto End=Fixture({Three[0],Three[1],{1,false,1.000000005,8,1,8}});P.Expect("Any entrance touching a selected-axis end cap refuses transactionally",End&&TransactionallyRefuses(End));
    auto Intersecting=Fixture({Three[0],{1,false,8,6,1.5,8},{1,false,9,6,1.5,7}});P.Expect("Any intersecting pair prevents partial multi-cavity commit",TransactionallyRefuses(Intersecting));
    auto Mixed=Fixture({Three[0],Three[1],{2,false,15,10,.7,5}});P.Expect("Mixed side-axis directions remain outside the parallel set route",Mixed&&TransactionallyRefuses(Mixed));
    std::vector<SideCavity> Nine;for(double X:{4.0,10.0,16.0})for(double C:{3.5,6.0,8.5})Nine.push_back({1,false,X,C,.35,5.0+static_cast<double>(Nine.size()%3)});
    auto NineSource=Fixture(Nine);P.Expect("A ninth side cavity remains outside the explicit bound",NineSource&&TransactionallyRefuses(NineSource));
    auto Pair=Fixture({Three[0],Three[1]});int PairApplied=-1;auto PairR=BlendSolver::FilletEdges(Pair.Payload,Rails(Pair.Payload),2,&PairApplied);
    P.Expect("The established dual-side route remains intact",PairR&&PairApplied==4&&PairR.Payload.Faces.size()==14);
    auto Single=Fixture({Three[0]});int SingleApplied=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),2,&SingleApplied);
    P.Expect("The established single-side route remains intact",SingleR&&SingleApplied==4&&SingleR.Payload.Faces.size()==12);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("MultiSide",S.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet MultiSide 2 --edges="+List+" --name=MultiSideRound");auto* Output=H.Document().Find("MultiSideRound");
    P.Expect("Console fillet commits one exact triple-side-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==16);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32t_MultiSideBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("MultiSideSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("MultiSideRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("TripleSide",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("EightSide",MR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("AlongZ",ZR.Payload,5)&&V.Execute("view top")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32t_MultiSideBlindBorePrism");
    P.Expect("C++ multi-side-bore proof commands complete",Rendered);
    P.Expect("C++ multi-side-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
