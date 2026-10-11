// Phase 32o — preserve one exact coaxial two-diameter stepped blind cavity while rounding a complete prism family.
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
struct Step
{
    bool High=true;double Y=8,Z=6,OuterRadius=1.6,InnerRadius=.8,ShoulderDepth=6,TotalDepth=12;
};
Deliver<BrepBody> Fixture(const Step& S,double InnerYOffset=0,double InnerZOffset=0)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    auto Cut=[&](double Radius,double Depth,double Y,double Z)
    {
        auto Cutter=BrepBody::Cylinder(S.High?Vec3{22,Y,Z}:Vec3{-2,Y,Z},S.High?Vec3{-1,0,0}:Vec3{1,0,0},Radius,Depth+2);
        if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
        return IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);
    };
    auto Outer=Cut(S.OuterRadius,S.ShoulderDepth,S.Y,S.Z);if(!Outer)return Outer;Working=std::move(Outer);
    auto Inner=Cut(S.InnerRadius,S.TotalDepth,S.Y+InnerYOffset,S.Z+InnerZOffset);if(!Inner)return Inner;
    return Inner;
}
Deliver<BrepBody> TripleFixture(bool High=true)
{
    Step S{High,8,6,2,1.4,4,8};auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    struct CutSpec{double Radius,Depth;};
    for(const CutSpec& C:std::vector<CutSpec>{{2,4},{1.4,8},{.7,12}})
    {
        auto Cutter=BrepBody::Cylinder(High?Vec3{22,S.Y,S.Z}:Vec3{-2,S.Y,S.Z},High?Vec3{-1,0,0}:Vec3{1,0,0},C.Radius,C.Depth+2);
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
struct Inspection{bool Topology=false,Supports=false,Annuli=false,Circles=false,Dimensions=false;};
Inspection Inspect(const BrepBody& B,const Step& S)
{
    Inspection Q;auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==20&&B.Edges.size()==30&&
        B.Coedges.size()==60&&B.Loops.size()==16&&B.Faces.size()==14;
    int Planes=0,Rolls=0,CavityCylinders=0,AnnularPlanes=0,Closed=0;
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane){++Planes;AnnularPlanes+=F.Loops.size()==2;}
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-2)<1e-9;
            CavityCylinders+=(std::fabs(Surface.RadiusMajor-S.OuterRadius)<1e-9||std::fabs(Surface.RadiusMajor-S.InnerRadius)<1e-9)&&
                std::fabs(Surface.Origin.Y-S.Y)<1e-8&&std::fabs(Surface.Origin.Z-S.Z)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)Closed+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    const double Entry=S.High?20:0,Shoulder=S.High?20-S.ShoulderDepth:S.ShoulderDepth,Floor=S.High?20-S.TotalDepth:S.TotalDepth;
    const std::vector<std::pair<double,double>> Expected{{Entry,S.OuterRadius},{Shoulder,S.OuterRadius},
                                                         {Shoulder,S.InnerRadius},{Floor,S.InnerRadius}};
    Q.Dimensions=true;
    for(const auto& [X,Radius]:Expected)
    {
        int Matches=0;
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
        {
            Vec3 Sample=E.Curve.Sample(E.Curve.DomainStart());
            Matches+=std::fabs(Sample.X-X)<1e-7&&std::fabs(std::hypot(Sample.Y-S.Y,Sample.Z-S.Z)-Radius)<1e-7;
        }
        Q.Dimensions&=Matches==1;
    }
    Q.Supports=Planes==8&&Rolls==4&&CavityCylinders==2;Q.Annuli=AnnularPlanes==2;Q.Circles=Closed==4;
    return Q;
}
double ExactVolume(const Step& S)
{
    double Outer=20*(16*12-4*4*(1-ScalarCriteria::Pi/4));
    return Outer-ScalarCriteria::Pi*(S.OuterRadius*S.OuterRadius*S.ShoulderDepth+
        S.InnerRadius*S.InnerRadius*(S.TotalDepth-S.ShoulderDepth));
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32o · Stepped Blind-Bore Rounded-Prism Verification");
    const Step S;auto SourceBody=Fixture(S);auto E=Rails(SourceBody.Payload);auto Source=SourceBody.Payload.Validate();
    P.Section("Exact two-diameter stepped cavity");
    P.Expect("Stepped source is canonical V12/E18/C36/L12/F10 genus-zero topology",SourceBody&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        SourceBody.Payload.Vertices.size()==12&&SourceBody.Payload.Edges.size()==18&&SourceBody.Payload.Coedges.size()==36&&
        SourceBody.Payload.Loops.size()==12&&SourceBody.Payload.Faces.size()==10);
    int Planes=0,Cylinders=0,Exact=0,Fitted=0;
    for(const BrepFace& F:SourceBody.Payload.Faces){Planes+=F.Surface.Classification==SurfaceClassification::Plane;Cylinders+=F.Surface.Classification==SurfaceClassification::Cylinder;}
    for(const BrepEdge& X:SourceBody.Payload.Edges)if(X.Closed())
    {
        Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();
        Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();
    }
    P.Expect("Source contains eight planes and two coaxial cylindrical walls",Planes==8&&Cylinders==2);
    P.Expect("Source evidence contains two exact and two fitted circular rims",Exact==2&&Fitted==2);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(SourceBody.Payload,E,2,&Applied);
    P.Expect("The stepped cavity commits with all four outer rolls",R&&Applied==4);
    auto Q=R?Inspect(R.Payload,S):Inspection{};
    P.Expect("Output reaches exact V20/E30/C60/L16/F14 genus-zero topology",Q.Topology);
    P.Expect("Eight planes, four rolls, and both rational cavity cylinders remain",Q.Supports);
    P.Expect("The entrance cap and interior shoulder are independently annular",Q.Annuli);
    P.Expect("All four entrance, shoulder, and floor rims are exact rational circles",Q.Circles);
    P.Expect("Both radii and both axial depths remain unchanged",Q.Dimensions);
    P.Within("Rounded stepped-cavity volume follows both exact removals",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(S)):1e9,.5);
    P.Expect("Outer bounds and the sharp stepped source remain unchanged",R&&R.Payload.Bounds().Low.Distance(SourceBody.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(SourceBody.Payload.Bounds().High)<1e-9&&SourceBody.Payload.Faces.size()==10);

    P.Section("Entry direction, offset, transforms, and bounded refusal");
    Step Low=S;Low.High=false;auto LowSource=Fixture(Low);int LA=-1;auto LowR=BlendSolver::FilletEdges(LowSource.Payload,Rails(LowSource.Payload),2,&LA);
    P.Expect("The opposite prism end preserves the same stepped topology",LowR&&LA==4&&Inspect(LowR.Payload,Low).Topology&&Inspect(LowR.Payload,Low).Dimensions);
    Step Offset{true,5,4,1.3,.55,5,13};auto OffsetSource=Fixture(Offset);int OA=-1;
    auto OffsetR=BlendSolver::FilletEdges(OffsetSource.Payload,Rails(OffsetSource.Payload),2,&OA);
    P.Expect("A safe offset counterbore preserves both requested diameters and depths",OffsetR&&OA==4&&Inspect(OffsetR.Payload,Offset).Dimensions);
    P.Within("Offset stepped-cavity volume follows its exact dimensions",OffsetR?std::fabs(OffsetR.Payload.Validate().Volume-ExactVolume(Offset)):1e9,.5);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(SourceBody.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=SourceBody.Payload.Transformed(M);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve stepped-cylinder classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==14);
    P.Within("Rigid transforms preserve stepped-cavity volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    Step Wall{true,1.5,1.5,1.4,.6,5,11};auto WallSource=Fixture(Wall);int Refused=99;
    auto WallR=BlendSolver::FilletEdges(WallSource.Payload,Rails(WallSource.Payload),2,&Refused);
    P.Expect("A counterbore crossing the rounded wall refuses transactionally",WallSource&&!WallR&&Refused==0);
    auto Eccentric=Fixture(S,.6,0);Refused=99;auto EccentricR=BlendSolver::FilletEdges(Eccentric.Payload,Rails(Eccentric.Payload),2,&Refused);
    P.Expect("An eccentric second stage remains outside the coaxial route",Eccentric&&Eccentric.Payload.Validate().Solid()&&!EccentricR&&Refused==0);
    auto Triple=TripleFixture();int TripleApplied=-1;auto TripleR=BlendSolver::FilletEdges(Triple.Payload,Rails(Triple.Payload),2,&TripleApplied);
    P.Expect("A third decreasing diameter delegates to the bounded multistage route",TripleR&&TripleApplied==4&&TripleR.Payload.Faces.size()==16);
    auto Plain=BrepBody::Box({0,0,0},{20,16,12});int PA=-1;auto PlainR=BlendSolver::FilletEdges(Plain.Payload,Rails(Plain.Payload),2,&PA);
    P.Expect("The established unperforated rounded-prism route remains intact",PlainR&&PA==4&&PlainR.Payload.Faces.size()==10);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("Counterbore",SourceBody.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet Counterbore 2 --edges="+List+" --name=CounterboreRound");auto* Output=H.Document().Find("CounterboreRound");
    P.Expect("Console fillet commits one exact stepped-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==14);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32o_SteppedBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("StepSharp",SourceBody.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("StepRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("StepEntrance",R.Payload,3)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("StepOpposite",LowR.Payload,2)&&V.Execute("view left")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("StepOffset",OffsetR.Payload,5)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32o_SteppedBlindBorePrism");
    P.Expect("C++ stepped-blind-bore proof commands complete",Rendered);
    P.Expect("C++ stepped-blind-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
