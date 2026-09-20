// Phase 32u — preserve one exact orthogonal two-diameter stepped blind cavity entering a retained planar side.
#include "Kernel/BlendSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
using namespace Frontier;
namespace {
struct SideStep{int Along=1;bool High=false;double X=10,Cross=6,OuterRadius=1.5,InnerRadius=.75,ShoulderDepth=5,TotalDepth=10;};
Deliver<BrepBody> Fixture(const SideStep& S,double InnerXOffset=0,double InnerCrossOffset=0)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    auto Cut=[&](double Radius,double Depth,double X,double Cross)
    {
        double Length=S.Along==1?16:12,Start=S.High?Length-Depth:-2;Vec3 Origin,Axis;
        if(S.Along==1){Origin={X,Start,Cross};Axis={0,1,0};}else{Origin={X,Cross,Start};Axis={0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,Radius,Depth+2);if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
        return IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);
    };
    auto Outer=Cut(S.OuterRadius,S.ShoulderDepth,S.X,S.Cross);if(!Outer)return Outer;Working=std::move(Outer);
    return Cut(S.InnerRadius,S.TotalDepth,S.X+InnerXOffset,S.Cross+InnerCrossOffset);
}
Deliver<BrepBody> TripleFixture(const SideStep& S)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    struct Stage{double Radius,Depth;};
    for(const Stage& StageValue:std::vector<Stage>{{1.8,3.5},{1.2,7},{.6,10}})
    {
        double Length=S.Along==1?16:12,Start=S.High?Length-StageValue.Depth:-2;Vec3 Origin,Axis;
        if(S.Along==1){Origin={S.X,Start,S.Cross};Axis={0,1,0};}else{Origin={S.X,S.Cross,Start};Axis={0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,StageValue.Radius,StageValue.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
Deliver<BrepBody> UndercutFixture(const SideStep& S)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    auto Outer=BrepBody::Cylinder({S.X,-2,S.Cross},{0,1,0},S.OuterRadius,S.ShoulderDepth+2);if(!Outer)return Outer;
    auto First=IntersectionSolver::Combine(Working.Payload,Outer.Payload,BodyOperation::Subtract);if(!First)return First;Working=std::move(First);
    auto Inner=BrepBody::Cylinder({S.X,S.ShoulderDepth,S.Cross},{0,1,0},S.InnerRadius,S.TotalDepth-S.ShoulderDepth);if(!Inner)return Inner;
    return IntersectionSolver::Combine(Working.Payload,Inner.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> SimpleSideFixture()
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});auto Cutter=BrepBody::Cylinder({10,-2,6},{0,1,0},1,10);
    if(!Box||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> AxialSteppedFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});
    for(const auto& Stage:std::vector<std::pair<double,double>>{{1.5,5},{.75,10}})
    {
        auto Cutter=BrepBody::Cylinder({-2,8,6},{1,0,0},Stage.first,Stage.second+2);if(!Working||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
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
        if((std::fabs(P.Y)<1e-8||std::fabs(P.Y-16)<1e-8)&&(std::fabs(P.Z)<1e-8||std::fabs(P.Z-12)<1e-8))Result.push_back(static_cast<int>(I));
    }
    return Result;
}
struct Inspection{bool Topology=false,Supports=false,Annuli=false,Circles=false,Dimensions=false;};
Inspection Inspect(const BrepBody& B,const SideStep& S)
{
    Inspection Q;auto Report=B.Validate();Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&
        B.Vertices.size()==20&&B.Edges.size()==30&&B.Coedges.size()==60&&B.Loops.size()==16&&B.Faces.size()==14;
    int Planes=0,Rolls=0,CavityCylinders=0,AnnularPlanes=0,Closed=0;Vec3 Axis=S.Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane){++Planes;AnnularPlanes+=F.Loops.size()==2;}
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-2)<1e-9;double X=Surface.Origin.X,Cross=S.Along==1?Surface.Origin.Z:Surface.Origin.Y;
            CavityCylinders+=(std::fabs(Surface.RadiusMajor-S.OuterRadius)<1e-9||std::fabs(Surface.RadiusMajor-S.InnerRadius)<1e-9)&&
                std::fabs(X-S.X)<1e-8&&std::fabs(Cross-S.Cross)<1e-8&&std::fabs(std::fabs(Surface.Axis.Normalised().Dot(Axis))-1)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)Closed+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    double Length=S.Along==1?16:12,Entry=S.High?Length:0,Shoulder=S.High?Length-S.ShoulderDepth:S.ShoulderDepth;
    double Floor=S.High?Length-S.TotalDepth:S.TotalDepth;std::vector<std::pair<double,double>> Expected{{Entry,S.OuterRadius},
        {Shoulder,S.OuterRadius},{Shoulder,S.InnerRadius},{Floor,S.InnerRadius}};Q.Dimensions=true;
    for(const auto& [T,Radius]:Expected)
    {
        Vec3 Centre=S.Along==1?Vec3{S.X,T,S.Cross}:Vec3{S.X,S.Cross,T};int Matches=0;
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
            Matches+=E.Curve.Centre.Distance(Centre)<1e-8&&std::fabs(E.Curve.RadiusMajor-Radius)<1e-9&&
                std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(Axis))-1)<1e-8;
        Q.Dimensions&=Matches==1;
    }
    Q.Supports=Planes==8&&Rolls==4&&CavityCylinders==2;Q.Annuli=AnnularPlanes==2;Q.Circles=Closed==4;return Q;
}
double ExactVolume(const SideStep& S)
{
    double Outer=20*(16*12-4*4*(1-ScalarCriteria::Pi/4));
    return Outer-ScalarCriteria::Pi*(S.OuterRadius*S.OuterRadius*S.ShoulderDepth+
        S.InnerRadius*S.InnerRadius*(S.TotalDepth-S.ShoulderDepth));
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
    VerificationPanel P("SolidArc · Phase 32u · Side Stepped Blind-Bore Rounded-Prism Verification");
    const SideStep S;auto SourceBody=Fixture(S);auto E=Rails(SourceBody.Payload);auto Source=SourceBody.Payload.Validate();
    P.Section("Exact two-diameter cavity through a retained side");
    P.Expect("Side-stepped source is canonical V12/E18/C36/L12/F10 genus-zero topology",SourceBody&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        SourceBody.Payload.Vertices.size()==12&&SourceBody.Payload.Edges.size()==18&&SourceBody.Payload.Coedges.size()==36&&SourceBody.Payload.Loops.size()==12&&SourceBody.Payload.Faces.size()==10);
    int Planes=0,Cylinders=0,Exact=0,Fitted=0;for(const BrepFace& F:SourceBody.Payload.Faces){Planes+=F.Surface.Classification==SurfaceClassification::Plane;Cylinders+=F.Surface.Classification==SurfaceClassification::Cylinder;}
    for(const BrepEdge& X:SourceBody.Payload.Edges)if(X.Closed()){Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Source contains eight planes and two coaxial side cylinders",Planes==8&&Cylinders==2);
    P.Expect("Source evidence contains two exact and two fitted circular rims",Exact==2&&Fitted==2);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(SourceBody.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,S):Inspection{};
    P.Expect("The side counterbore commits with all four outer rolls",R&&Applied==4);
    P.Expect("Output reaches exact V20/E30/C60/L16/F14 genus-zero topology",Q.Topology);
    P.Expect("Eight planes, four rolls, and both rational side cylinders remain",Q.Supports);
    P.Expect("The side entrance and interior shoulder are independently annular",Q.Annuli);
    P.Expect("All four entrance, shoulder, and floor rims are exact rational circles",Q.Circles);
    P.Expect("Both side radii and both finite depths remain unchanged",Q.Dimensions);
    P.Within("Rounded side-counterbore volume follows both exact removals",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(S)):1e9,.5);
    P.Expect("Outer bounds and the sharp side-stepped source remain unchanged",R&&R.Payload.Bounds().Low.Distance(SourceBody.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(SourceBody.Payload.Bounds().High)<1e-9&&SourceBody.Payload.Faces.size()==10);

    P.Section("Entry side, alternate axis, transforms, and refusal");
    SideStep High=S;High.High=true;auto HighSource=Fixture(High);int HA=-1;auto HighR=BlendSolver::FilletEdges(HighSource.Payload,Rails(HighSource.Payload),2,&HA);
    P.Expect("The opposite retained Y side preserves the stepped cavity",HighR&&HA==4&&Inspect(HighR.Payload,High).Dimensions);
    SideStep AlongZ{2,true,10,8,1.3,.6,4,8};auto ZSource=Fixture(AlongZ);int ZA=-1;auto ZR=BlendSolver::FilletEdges(ZSource.Payload,Rails(ZSource.Payload),2,&ZA);
    P.Expect("A stepped cavity entering the high Z side is preserved",ZR&&ZA==4&&Inspect(ZR.Payload,AlongZ).Dimensions);
    SideStep LowZ=AlongZ;LowZ.High=false;auto LowZSource=Fixture(LowZ);int LowZA=-1;auto LowZR=BlendSolver::FilletEdges(LowZSource.Payload,Rails(LowZSource.Payload),2,&LowZA);
    P.Expect("The opposite low Z side preserves the same stepped cavity",LowZR&&LowZA==4&&Inspect(LowZR.Payload,LowZ).Dimensions);
    SideStep Offset{1,false,6,5,1.2,.5,4,11};auto OffsetSource=Fixture(Offset);int OA=-1;auto OffsetR=BlendSolver::FilletEdges(OffsetSource.Payload,Rails(OffsetSource.Payload),2,&OA);
    P.Expect("Safe selected-axis and wall-strip offsets preserve both stages",OffsetR&&OA==4&&Inspect(OffsetR.Payload,Offset).Dimensions);
    P.Within("Offset side-counterbore volume follows its exact dimensions",OffsetR?std::fabs(OffsetR.Payload.Validate().Volume-ExactVolume(Offset)):1e9,.5);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(SourceBody.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=SourceBody.Payload.Transformed(M);int TA=-1;auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve side-stepped classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==14);
    P.Within("Rigid transforms preserve side-counterbore volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    SideStep Corner=S;Corner.Cross=2.6;auto CornerSource=Fixture(Corner);P.Expect("An outer entrance crossing a rounded corner strip refuses transactionally",CornerSource&&TransactionallyRefuses(CornerSource));
    SideStep End=S;End.X=1.500000005;auto EndSource=Fixture(End);P.Expect("An outer entrance touching a selected-axis end cap refuses transactionally",EndSource&&TransactionallyRefuses(EndSource));
    SideStep Through=S;Through.TotalDepth=16;auto ThroughSource=Fixture(Through);
    P.Expect("A final stage reaching the opposite wall remains outside the blind route",ThroughSource&&TransactionallyRefuses(ThroughSource));
    SideStep Undercut=S;Undercut.InnerRadius=1.8;auto UndercutSource=UndercutFixture(Undercut);
    P.Expect("An increasing-radius undercut cannot enter the exact route",TransactionallyRefuses(UndercutSource));
    auto Eccentric=Fixture(S,.5,0);P.Expect("An eccentric inner side stage remains unsupported",Eccentric&&TransactionallyRefuses(Eccentric));
    auto Triple=TripleFixture(S);int TripleApplied=-1;auto TripleR=BlendSolver::FilletEdges(Triple.Payload,Rails(Triple.Payload),2,&TripleApplied);
    P.Expect("A third side diameter delegates to the bounded multistage route",Triple&&TripleR&&TripleApplied==4&&TripleR.Payload.Faces.size()==16);
    auto Simple=SimpleSideFixture();int SimpleApplied=-1;auto SimpleR=BlendSolver::FilletEdges(Simple.Payload,Rails(Simple.Payload),2,&SimpleApplied);
    P.Expect("The established simple side-cavity route remains intact",SimpleR&&SimpleApplied==4&&SimpleR.Payload.Faces.size()==12);
    auto Axial=AxialSteppedFixture();int AxialApplied=-1;auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),2,&AxialApplied);
    P.Expect("The established selected-axis stepped route remains intact",AxialR&&AxialApplied==4&&AxialR.Payload.Faces.size()==14);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("SideCounterbore",SourceBody.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet SideCounterbore 2 --edges="+List+" --name=SideCounterboreRound");auto* Output=H.Document().Find("SideCounterboreRound");
    P.Expect("Console fillet commits one exact side-counterbore solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==14);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32u_SideSteppedBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("SideStepSharp",SourceBody.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&Add("SideStepRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&
        V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&Reset()&&Add("SideStepEntrance",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("SideStepOpposite",HighR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("SideStepZ",ZR.Payload,5)&&V.Execute("view top")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&V.Execute("render sheet finalize Phase32u_SideSteppedBlindBorePrism");
    P.Expect("C++ side-counterbore proof commands complete",Rendered);
    P.Expect("C++ side-counterbore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
