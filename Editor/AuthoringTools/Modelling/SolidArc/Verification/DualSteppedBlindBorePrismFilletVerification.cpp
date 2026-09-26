// Phase 32q — preserve exactly two separated coaxial two-stage blind cavities while rounding one complete prism family.
#include "Kernel/BlendSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>
using namespace Frontier;
namespace {
struct Stage { double Radius=1,Depth=1; };
struct Cavity { bool High=true;double Y=8,Z=6;Stage Outer,Inner; };
Deliver<BrepBody> Fixture(const std::vector<Cavity>& Cavities)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const Cavity& C:Cavities)for(const Stage& S:std::vector<Stage>{C.Outer,C.Inner})
    {
        auto Cutter=BrepBody::Cylinder(C.High?Vec3{22,C.Y,C.Z}:Vec3{-2,C.Y,C.Z},
                                      C.High?Vec3{-1,0,0}:Vec3{1,0,0},S.Radius,S.Depth+2);
        if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
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
struct Inspection
{
    bool Topology=false,Supports=false,Circles=false,Dimensions=false;int InnerLoops=0,AnnularPlanes=0,TriplePlanes=0;
};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities)
{
    Inspection Q;auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==24&&B.Edges.size()==36&&
        B.Coedges.size()==72&&B.Loops.size()==22&&B.Faces.size()==18;
    int Planes=0,Rolls=0,CavityCylinders=0,ExactCircles=0;
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.AnnularPlanes+=F.Loops.size()==2;Q.TriplePlanes+=F.Loops.size()==3;
            if(F.Loops.size()>1)Q.InnerLoops+=static_cast<int>(F.Loops.size()-1);
        }
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-1.5)<1e-9;
            for(const Cavity& C:Cavities)for(const Stage& S:std::vector<Stage>{C.Outer,C.Inner})
                CavityCylinders+=std::fabs(Surface.RadiusMajor-S.Radius)<1e-9&&std::fabs(Surface.Origin.Y-C.Y)<1e-8&&
                    std::fabs(Surface.Origin.Z-C.Z)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    Q.Dimensions=true;
    for(const Cavity& C:Cavities)
    {
        double Entry=C.High?20:0,Shoulder=C.High?20-C.Outer.Depth:C.Outer.Depth,Floor=C.High?20-C.Inner.Depth:C.Inner.Depth;
        const std::vector<std::pair<double,double>> Expected{{Entry,C.Outer.Radius},{Shoulder,C.Outer.Radius},
                                                             {Shoulder,C.Inner.Radius},{Floor,C.Inner.Radius}};
        for(const auto& [X,Radius]:Expected)
        {
            int Matches=0;
            for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
            {
                Vec3 Sample=E.Curve.Sample(E.Curve.DomainStart());
                Matches+=std::fabs(Sample.X-X)<1e-7&&std::fabs(std::hypot(Sample.Y-C.Y,Sample.Z-C.Z)-Radius)<1e-7;
            }
            Q.Dimensions&=Matches==1;
        }
    }
    Q.Supports=Planes==10&&Rolls==4&&CavityCylinders==4;Q.Circles=ExactCircles==8;return Q;
}
double ExactVolume(const std::vector<Cavity>& Cavities)
{
    double Volume=20*(16*12-4*1.5*1.5*(1-ScalarCriteria::Pi/4));
    for(const Cavity& C:Cavities)Volume-=ScalarCriteria::Pi*(C.Outer.Radius*C.Outer.Radius*C.Outer.Depth+
        C.Inner.Radius*C.Inner.Radius*(C.Inner.Depth-C.Outer.Depth));
    return Volume;
}
double PairClearance(const Cavity& A,const Cavity& B)
{
    struct Band{double Low,High,Radius;};
    auto Bands=[](const Cavity& C)
    {
        std::vector<Band> Result;double Previous=0;
        for(const Stage& S:std::vector<Stage>{C.Outer,C.Inner})
        {
            Result.push_back(C.High?Band{20-S.Depth,20-Previous,S.Radius}:Band{Previous,S.Depth,S.Radius});Previous=S.Depth;
        }
        return Result;
    };
    double Result=std::numeric_limits<double>::max(),Centres=std::hypot(A.Y-B.Y,A.Z-B.Z);
    for(const Band& X:Bands(A))for(const Band& Y:Bands(B))
    {
        double Axial=std::max({0.0,X.Low-Y.High,Y.Low-X.High});double Radial=std::max(0.0,Centres-X.Radius-Y.Radius);
        Result=std::min(Result,std::hypot(Axial,Radial));
    }
    return Result;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32q · Dual Stepped Blind-Bore Rounded-Prism Verification");
    const std::vector<Cavity> Same{{true,5,4,{1.4,5},{.6,11}},{true,11,8,{1.2,6},{.5,10}}};
    auto S=Fixture(Same);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Two same-end counterbores");
    P.Expect("Dual-stepped source is canonical V16/E24/C48/L18/F14 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==16&&S.Payload.Edges.size()==24&&S.Payload.Coedges.size()==48&&S.Payload.Loops.size()==18&&S.Payload.Faces.size()==14);
    int Planes=0,Cylinders=0,Exact=0,Fitted=0;
    for(const BrepFace& F:S.Payload.Faces){Planes+=F.Surface.Classification==SurfaceClassification::Plane;Cylinders+=F.Surface.Classification==SurfaceClassification::Cylinder;}
    for(const BrepEdge& X:S.Payload.Edges)if(X.Closed())
    {
        Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();
        Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();
    }
    P.Expect("Source contains ten planes and four rational cylindrical walls",Planes==10&&Cylinders==4);
    P.Expect("Four exact floors and four fitted Boolean rims identify both steps",Exact==4&&Fitted==4);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,1.5,&Applied);auto Q=R?Inspect(R.Payload,Same):Inspection{};
    P.Expect("Both separated counterbores commit with all four outer rolls",R&&Applied==4);
    P.Expect("Output reaches exact V24/E36/C72/L22/F18 genus-zero topology",Q.Topology);
    P.Expect("Ten planes, four rolls, and four rational cavity cylinders remain",Q.Supports);
    P.Expect("One triple-loop entry cap plus two shoulders retain four inner loops",Q.InnerLoops==4&&Q.TriplePlanes==1&&Q.AnnularPlanes==2);
    P.Expect("All eight entrance, shoulder, and floor rims are exact rational circles",Q.Circles);
    P.Expect("Both counterbores preserve both radii and cumulative depths",Q.Dimensions);
    P.Within("Same-end dual-stepped volume follows all four axial bands",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Same)):1e9,.5);
    P.Expect("Same-end stage bands retain positive radial clearance",PairClearance(Same[0],Same[1])>ScalarCriteria::MergeTolerance);
    P.Expect("Outer bounds and the sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==14);

    P.Section("Opposite ends, axial clearance, transforms, and refusal");
    const std::vector<Cavity> Opposite{{false,8,6,{1.4,4},{.7,7}},{true,8,6,{1.2,4},{.6,7}}};
    auto O=Fixture(Opposite);int OA=-1;auto OR=BlendSolver::FilletEdges(O.Payload,Rails(O.Payload),1.5,&OA);auto OQ=OR?Inspect(OR.Payload,Opposite):Inspection{};
    P.Expect("Coaxial counterbores entering opposite ends use the same exact route",OR&&OA==4&&OQ.Topology&&OQ.Dimensions);
    P.Expect("Opposite entries and both shoulders form four annular planes",OQ.InnerLoops==4&&OQ.AnnularPlanes==4&&OQ.TriplePlanes==0);
    P.Expect("Opposite coaxial stage bands preserve a positive axial ligament",PairClearance(Opposite[0],Opposite[1])>ScalarCriteria::MergeTolerance);
    P.Within("Opposite-end dual-stepped volume follows all four bands",OR?std::fabs(OR.Payload.Validate().Volume-ExactVolume(Opposite)):1e9,.5);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,1.5,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=S.Payload.Transformed(M);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,E,1.5,&TA);
    P.Expect("Rigid transforms preserve dual-stepped classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==18);
    P.Within("Rigid transforms preserve dual-stepped volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto WallSpecs=Same;WallSpecs.front()={true,1.1,1.1,{1,5},{.4,11}};auto Wall=Fixture(WallSpecs);int Refused=99;
    auto WR=BlendSolver::FilletEdges(Wall.Payload,Rails(Wall.Payload),1.5,&Refused);
    P.Expect("Either counterbore crossing the rounded wall refuses transactionally",Wall&&!WR&&Refused==0);
    const std::vector<Cavity> Three{{true,4,3,{1,4},{.4,10}},{true,8,8,{1,5},{.4,11}},{true,12,3,{1,6},{.4,12}}};
    auto ThreeSource=Fixture(Three);Refused=99;auto ThreeR=BlendSolver::FilletEdges(ThreeSource.Payload,Rails(ThreeSource.Payload),1.5,&Refused);
    P.Expect("A third stepped cavity remains outside this exact dual route",ThreeSource&&ThreeSource.Payload.Validate().Solid()&&!ThreeR&&Refused==0);
    auto SingleSource=Fixture({Same[0]});int SA=-1;auto SingleR=BlendSolver::FilletEdges(SingleSource.Payload,Rails(SingleSource.Payload),1.5,&SA);
    P.Expect("The established single-counterbore route remains intact",SingleR&&SA==4&&SingleR.Payload.Faces.size()==14);
    auto SimpleBox=BrepBody::Box({0,0,0},{20,16,12});auto C0=BrepBody::Cylinder({22,5,4},{-1,0,0},.6,10);auto C1=BrepBody::Cylinder({22,11,8},{-1,0,0},.5,11);
    auto B0=IntersectionSolver::Combine(SimpleBox.Payload,C0.Payload,BodyOperation::Subtract);auto B1=IntersectionSolver::Combine(B0.Payload,C1.Payload,BodyOperation::Subtract);int BA=-1;
    auto BR=BlendSolver::FilletEdges(B1.Payload,Rails(B1.Payload),1.5,&BA);
    P.Expect("The established dual simple-cavity route remains intact",BR&&BA==4&&BR.Payload.Faces.size()==14);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("DualStep",S.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet DualStep 1.5 --edges="+List+" --name=DualStepRound");auto* Output=H.Document().Find("DualStepRound");
    P.Expect("Console fillet commits one exact dual-stepped solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==18);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32q_DualSteppedBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("DualStepSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("DualStepRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("SameEndSteps",R.Payload,3)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("OppositeSteps",OR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("CoaxialLigament",OR.Payload,5)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32q_DualSteppedBlindBorePrism");
    P.Expect("C++ dual-stepped-bore proof commands complete",Rendered);
    P.Expect("C++ dual-stepped-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
