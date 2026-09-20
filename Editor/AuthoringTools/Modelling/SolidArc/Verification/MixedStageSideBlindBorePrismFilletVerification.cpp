// Phase 32y — preserve bounded separated side-entering stepped-cavity sets with mixed stage counts.
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
struct Stage{double Radius=1,Depth=1,XOffset=0,CrossOffset=0;};
struct Cavity{int Along=1;bool High=false;double X=10,Cross=6;std::vector<Stage> Stages;};
Deliver<BrepBody> Fixture(const std::vector<Cavity>& Cavities)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const Cavity& C:Cavities)for(const Stage& S:C.Stages)
    {
        double Length=C.Along==1?16:12,Start=C.High?Length-S.Depth:-2;Vec3 Origin,Axis;
        if(C.Along==1){Origin={C.X+S.XOffset,Start,C.Cross+S.CrossOffset};Axis={0,1,0};}
        else{Origin={C.X+S.XOffset,C.Cross+S.CrossOffset,Start};Axis={0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,S.Radius,S.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
Deliver<BrepBody> NonDecreasingFixture(const std::vector<Cavity>& Cavities)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(size_t CavityIndex=0;CavityIndex<Cavities.size();++CavityIndex)
    {
        const Cavity& C=Cavities[CavityIndex];double PreviousDepth=0.0;
        for(size_t StageIndex=0;StageIndex<C.Stages.size();++StageIndex)
        {
            const Stage& S=C.Stages[StageIndex];double Length=C.Along==1?16:12;
            bool Undercut=CavityIndex==1&&StageIndex==1;double Start=C.High?Length-S.Depth:(Undercut?PreviousDepth:-2);
            double CutterLength=Undercut?S.Depth-PreviousDepth:S.Depth+2;double CutterRadius=Undercut?C.Stages.front().Radius+.2:S.Radius;
            Vec3 Origin,Axis;if(C.Along==1){Origin={C.X,Start,C.Cross};Axis={0,1,0};}else{Origin={C.X,C.Cross,Start};Axis={0,0,1};}
            auto Cutter=BrepBody::Cylinder(Origin,Axis,CutterRadius,CutterLength);if(!Cutter)return Cutter;
            auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);PreviousDepth=S.Depth;
        }
    }
    return Working;
}
std::vector<Cavity> Mixed()
{
    return {{1,false,5,3.5,{{.8,3},{.5,6},{.25,9}}},{1,false,10,8,{{.9,4},{.4,8}}},
            {1,true,15,5,{{.75,2.5},{.55,5},{.35,7.5},{.18,10}}}};
}
std::vector<Stage> EightStages(double Offset=0)
{
    const std::vector<double>R{1,.85,.72,.6,.48,.37,.26,.15},D{1,2,3,4,5,6,6.8,7.4};std::vector<Stage>Result;
    for(size_t I=0;I<R.size();++I)Result.push_back({R[I]-Offset,D[I]});
    return Result;
}
std::vector<Cavity> Maximum(){return {{1,false,10,6,EightStages()},{1,true,10,6,EightStages(.04)}};}
std::vector<Cavity> NineChains()
{
    std::vector<Cavity>Result;const std::vector<double>X{2.5,6.25,10,13.75,17.5},Cross{3.2,8.8};
    for(double Z:Cross)for(double A:X)if(Result.size()<9)Result.push_back({1,false,A,Z,{{.45,2.5},{.2,6}}});
    return Result;
}
Deliver<BrepBody> AxialDualSteppedFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    struct Axial{double Y=0.0,Z=0.0;std::vector<Stage>Stages;};
    for(const Axial& C:std::vector<Axial>{{5,4,{{1.2,4},{.5,9}}},{12,8,{{1,5},{.4,10}}}})for(const Stage& S:C.Stages)
    {
        auto Cutter=BrepBody::Cylinder({-2,C.Y,C.Z},{1,0,0},S.Radius,S.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
std::vector<int> Rails(const BrepBody& B)
{
    std::vector<int>Result;for(size_t I=0;I<B.Edges.size();++I)
    {
        const BrepEdge& E=B.Edges[I];if(E.Curve.Classification!=CurveClassification::Line||E.VertexStart<0||E.VertexEnd<0)continue;
        Vec3 P=B.Vertices[E.VertexStart].Point,Q=B.Vertices[E.VertexEnd].Point;
        if(std::fabs(std::fabs((Q-P).Normalised().Dot({1,0,0}))-1)>1e-8)continue;
        if((std::fabs(P.Y)<1e-8||std::fabs(P.Y-16)<1e-8)&&(std::fabs(P.Z)<1e-8||std::fabs(P.Z-12)<1e-8))Result.push_back(static_cast<int>(I));
    }
    return Result;
}
size_t TotalStages(const std::vector<Cavity>& Cavities)
{size_t Result=0;for(const Cavity& C:Cavities)Result+=C.Stages.size();return Result;}
struct Inspection{bool Topology=false,Supports=false,Circles=false,Dimensions=false;int InnerLoops=0,Annular=0,MaximumLoops=0;};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities,double FilletRadius)
{
    Inspection Q;size_t M=TotalStages(Cavities);auto Report=B.Validate();Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&
        B.Vertices.size()==16+2*M&&B.Edges.size()==24+3*M&&B.Coedges.size()==48+6*M&&B.Loops.size()==10+3*M&&B.Faces.size()==10+2*M;
    int Planes=0,Rolls=0,CavityCylinders=0,ExactCircles=0;Vec3 SideAxis=Cavities.front().Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.Annular+=F.Loops.size()==2;Q.MaximumLoops=std::max(Q.MaximumLoops,static_cast<int>(F.Loops.size()));
            if(F.Loops.size()>1)Q.InnerLoops+=static_cast<int>(F.Loops.size()-1);
        }
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-FilletRadius)<1e-9&&std::fabs(std::fabs(Surface.Axis.Normalised().Dot({1,0,0}))-1)<1e-8;
            for(const Cavity& C:Cavities)for(const Stage& S:C.Stages)
            {
                double X=Surface.Origin.X,Cross=C.Along==1?Surface.Origin.Z:Surface.Origin.Y;
                CavityCylinders+=std::fabs(Surface.RadiusMajor-S.Radius)<1e-9&&std::fabs(X-C.X)<1e-8&&std::fabs(Cross-C.Cross)<1e-8&&
                    std::fabs(std::fabs(Surface.Axis.Normalised().Dot(SideAxis))-1)<1e-8;
            }
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    Q.Dimensions=true;for(const Cavity& C:Cavities)
    {
        double Length=C.Along==1?16:12,Entry=C.High?Length:0;std::vector<std::pair<double,double>>Expected{{Entry,C.Stages.front().Radius}};
        for(size_t I=0;I<C.Stages.size();++I)
        {
            double T=C.High?Length-C.Stages[I].Depth:C.Stages[I].Depth;Expected.push_back({T,C.Stages[I].Radius});
            if(I+1<C.Stages.size())Expected.push_back({T,C.Stages[I+1].Radius});
        }
        for(const auto& [T,Radius]:Expected)
        {
            Vec3 Centre=C.Along==1?Vec3{C.X,T,C.Cross}:Vec3{C.X,C.Cross,T};int Matches=0;
            for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
                Matches+=E.Curve.Centre.Distance(Centre)<1e-8&&std::fabs(E.Curve.RadiusMajor-Radius)<1e-9&&
                    std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(SideAxis))-1)<1e-8;
            Q.Dimensions&=Matches==1;
        }
    }
    Q.Supports=Planes==static_cast<int>(6+M)&&Rolls==4&&CavityCylinders==static_cast<int>(M);Q.Circles=ExactCircles==static_cast<int>(2*M);return Q;
}
double ExactVolume(const std::vector<Cavity>& Cavities,double FilletRadius)
{
    double Volume=20*(16*12-4*FilletRadius*FilletRadius*(1-ScalarCriteria::Pi/4));
    for(const Cavity& C:Cavities){double Previous=0;for(const Stage& S:C.Stages){Volume-=ScalarCriteria::Pi*S.Radius*S.Radius*(S.Depth-Previous);Previous=S.Depth;}}return Volume;
}
double MinimumPairClearance(const std::vector<Cavity>& Cavities)
{
    struct Band{double Low,High,Radius;};auto Bands=[](const Cavity& C)
    {
        std::vector<Band>Result;double Length=C.Along==1?16:12,Previous=0;
        for(const Stage& S:C.Stages){Result.push_back(C.High?Band{Length-S.Depth,Length-Previous,S.Radius}:Band{Previous,S.Depth,S.Radius});Previous=S.Depth;}return Result;
    };
    double Result=std::numeric_limits<double>::max();for(size_t I=0;I<Cavities.size();++I)for(size_t J=I+1;J<Cavities.size();++J)
    {
        double Centres=std::hypot(Cavities[I].X-Cavities[J].X,Cavities[I].Cross-Cavities[J].Cross);
        for(const Band& A:Bands(Cavities[I]))for(const Band& B:Bands(Cavities[J]))
        {
            double Axial=std::max({0.0,A.Low-B.High,B.Low-A.High});double Radial=std::max(0.0,Centres-A.Radius-B.Radius);
            Result=std::min(Result,std::hypot(Axial,Radial));
        }
    }
    return Result;
}
bool TransactionallyRefuses(const Deliver<BrepBody>& Source,double Radius=1.5)
{if(!Source)return true;int Applied=99;auto Result=BlendSolver::FilletEdges(Source.Payload,Rails(Source.Payload),Radius,&Applied);return !Result&&Applied==0;}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32y · Mixed-Stage Side Blind-Bore Rounded-Prism Verification");const double Radius=1.5;
    const auto Mix=Mixed();const size_t MixStages=TotalStages(Mix);auto S=Fixture(Mix);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Three separated cavities with mixed stage counts");
    P.Expect("Mixed 3+2+4 source is canonical V26/E39/C78/L33/F24 topology",S&&MixStages==9&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==26&&S.Payload.Edges.size()==39&&S.Payload.Coedges.size()==78&&S.Payload.Loops.size()==33&&S.Payload.Faces.size()==24);
    int Exact=0,Fitted=0;for(const BrepEdge& X:S.Payload.Edges)if(X.Closed()){Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Nine exact and nine fitted source rims identify every stage",Exact==9&&Fitted==9);
    P.Expect("Exactly four selected-axis rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,Radius,&Applied);auto Q=R?Inspect(R.Payload,Mix,Radius):Inspection{};
    P.Expect("All three mixed-stage cavities commit with four outer rolls",R&&Applied==4);
    P.Expect("Mixed-stage output reaches V34/E51/C102/L37/F28 topology",Q.Topology);
    P.Expect("Fifteen planes, four rolls, and nine cavity cylinders remain",Q.Supports);
    P.Expect("Nine planar levels preserve nine independent inner loops",Q.InnerLoops==9);
    P.Expect("All eighteen mixed-stage rims are exact rational circles",Q.Circles);
    P.Expect("Every mixed-stage centre, radius, side, and depth remains exact",Q.Dimensions);
    P.Within("Mixed-stage volume follows all nine exact axial bands",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Mix,Radius)):1e9,.5);
    P.Expect("Every mixed-stage cavity pair retains finite-band clearance",MinimumPairClearance(Mix)>ScalarCriteria::MergeTolerance);
    P.Expect("Outer bounds and sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==24);

    P.Section("Sixteen-stage budget, directions, invariance, and refusal");
    const auto Max=Maximum();auto M=Fixture(Max);auto MSource=M.Payload.Validate();auto MRails=Rails(M.Payload);
    P.Expect("Two eight-stage source chains reach V40/E60/C120/L54/F38 topology",M&&TotalStages(Max)==16&&MSource.Solid()&&MSource.Genus==0&&
        M.Payload.Vertices.size()==40&&M.Payload.Edges.size()==60&&M.Payload.Coedges.size()==120&&M.Payload.Loops.size()==54&&M.Payload.Faces.size()==38);
    int MA=-1;auto MaximumR=BlendSolver::FilletEdges(M.Payload,MRails,Radius,&MA);auto MQ=MaximumR?Inspect(MaximumR.Payload,Max,Radius):Inspection{};
    P.Expect("The sixteen-stage construction budget commits all four rolls",MaximumR&&MA==4);
    P.Expect("Sixteen-stage output reaches V48/E72/C144/L58/F42 topology",MQ.Topology);
    P.Expect("Twenty-two planes, four rolls, and sixteen cavity cylinders remain",MQ.Supports);
    P.Expect("Opposite entrances and fourteen shoulders form sixteen annuli",MQ.InnerLoops==16&&MQ.Annular==16&&MQ.MaximumLoops==2);
    P.Expect("All thirty-two budget-limit rims are exact rational circles",MQ.Circles);
    P.Expect("Both eight-stage chains preserve all dimensions",MQ.Dimensions);
    P.Within("Budget-limit volume follows all sixteen exact bands",MaximumR?std::fabs(MaximumR.Payload.Validate().Volume-ExactVolume(Max,Radius)):1e9,.5);
    P.Expect("Coaxial opposite-side chains retain a positive axial ligament",MinimumPairClearance(Max)>ScalarCriteria::MergeTolerance);
    const std::vector<Cavity>ZMix{{2,false,6,5,{{1.1,3},{.7,5},{.3,8}}},{2,false,14,10,{{1,3.5},{.45,7}}}};
    auto Z=Fixture(ZMix);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),Radius,&ZA);
    P.Expect("Mixed-stage cavities parallel to Z preserve side direction",ZR&&ZA==4&&Inspect(ZR.Payload,ZMix,Radius).Dimensions);
    auto Reversed=Mix;std::reverse(Reversed.begin(),Reversed.end());auto ReversedSource=Fixture(Reversed);int ReverseApplied=-1;
    auto ReversedR=BlendSolver::FilletEdges(ReversedSource.Payload,Rails(ReversedSource.Payload),Radius,&ReverseApplied);
    P.Expect("Reversed chain construction order yields identical volume",ReversedR&&ReverseApplied==4&&std::fabs(ReversedR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    std::vector<int>Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,Radius,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6});BrepBody T=S.Payload.Transformed(Transform);int TA=-1;auto TR=BlendSolver::FilletEdges(T,E,Radius,&TA);
    P.Expect("Rigid translations preserve mixed-stage classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==28);
    P.Within("Rigid translations preserve mixed-stage volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto TooManyStages=Mix;TooManyStages[0].Stages=EightStages();TooManyStages[1].Stages=std::vector<Stage>{{.9,1},{.75,2},{.6,3},{.48,4},{.37,5},{.27,6},{.18,7}};TooManyStages[2].Stages.resize(2);
    auto BudgetSource=Fixture(TooManyStages);P.Expect("Seventeen total stages remain outside the construction budget",BudgetSource&&TransactionallyRefuses(BudgetSource,Radius));
    auto NineStageMember=Mix;NineStageMember.resize(2);NineStageMember[0].Stages=EightStages();NineStageMember[0].Stages.push_back({.08,8});auto NineStageSource=Fixture(NineStageMember);
    P.Expect("A ninth stage in any member remains outside the per-chain bound",NineStageSource&&TransactionallyRefuses(NineStageSource,Radius));
    auto NineCavitySource=Fixture(NineChains());P.Expect("A ninth stepped cavity remains outside the cavity bound",NineCavitySource&&TransactionallyRefuses(NineCavitySource,Radius));
    auto OneStage=Mix;OneStage[1].Stages.resize(1);auto OneStageSource=Fixture(OneStage);P.Expect("A one-stage member does not masquerade as a stepped set",OneStageSource&&TransactionallyRefuses(OneStageSource,Radius));
    auto Eccentric=Mix;Eccentric[1].Stages[1].XOffset=.3;auto EccentricSource=Fixture(Eccentric);auto NonDecreasingSource=NonDecreasingFixture({Mix[0],Mix[1]});
    P.Expect("Eccentric and non-decreasing mixed-set stages both refuse transactionally",EccentricSource&&TransactionallyRefuses(EccentricSource,Radius)&&TransactionallyRefuses(NonDecreasingSource,Radius));
    auto Intersecting=Mix;Intersecting[1].X=5.5;Intersecting[1].Cross=3.5;auto IntersectingSource=Fixture(Intersecting);P.Expect("Intersecting mixed-stage bands cannot partially commit",TransactionallyRefuses(IntersectingSource,Radius));
    const std::vector<Cavity>MixedAxes{{1,false,5,4,{{.7,3},{.3,6}}},{2,false,15,10,{{.8,3},{.35,6}}}};auto MixedAxisSource=Fixture(MixedAxes);
    P.Expect("Mixed Y/Z chain axes remain unsupported",MixedAxisSource&&TransactionallyRefuses(MixedAxisSource,Radius));
    auto Wall=Mix;Wall[1].Cross=2.2;auto WallSource=Fixture(Wall);P.Expect("Any outer disk crossing a rounded strip refuses",WallSource&&TransactionallyRefuses(WallSource,Radius));
    auto End=Mix;End[1].X=.900000005;auto EndSource=Fixture(End);P.Expect("Any outer disk touching an end cap refuses",EndSource&&TransactionallyRefuses(EndSource,Radius));
    auto Through=Mix;Through[2].Stages.back().Depth=16;auto ThroughSource=Fixture(Through);P.Expect("Any final stage reaching the opposite wall refuses",ThroughSource&&TransactionallyRefuses(ThroughSource,Radius));
    auto TwoStageSet=NineChains();TwoStageSet.resize(8);auto TwoStageSource=Fixture(TwoStageSet);int TwoStageApplied=-1;auto TwoStageR=BlendSolver::FilletEdges(TwoStageSource.Payload,Rails(TwoStageSource.Payload),Radius,&TwoStageApplied);
    P.Expect("The established eight two-stage side route remains intact",TwoStageR&&TwoStageApplied==4&&TwoStageR.Payload.Faces.size()==42);
    auto Single=Fixture({Mix[0]});int SA=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),Radius,&SA);
    P.Expect("The established single multistage side route remains intact",SingleR&&SA==4&&SingleR.Payload.Faces.size()==16);
    auto Axial=AxialDualSteppedFixture();int AA=-1;auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),Radius,&AA);
    P.Expect("The selected-axis dual-stepped route remains intact",AxialR&&AA==4&&AxialR.Payload.Faces.size()==18);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("MixedSideSteps",M.Payload);F.Matcap=6;std::string List;
    for(int I:MRails){if(!List.empty())List+=",";List+=std::to_string(I);}bool OK=H.Execute("fillet MixedSideSteps 1.5 --edges="+List+" --name=MixedSideStepsRound");auto* Output=H.Document().Find("MixedSideStepsRound");
    P.Expect("Console fillet commits both eight-stage side cavities",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==42);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32y_MixedStageSideBlindBorePrism.png";std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("MixedSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&Add("MixedRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("MixedEntrances",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("SixteenStages",MaximumR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("OppositeEightStages",MaximumR.Payload,5)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&V.Execute("render sheet finalize Phase32y_MixedStageSideBlindBorePrism");
    P.Expect("C++ mixed-stage-side proof commands complete",Rendered);
    P.Expect("C++ mixed-stage-side proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
