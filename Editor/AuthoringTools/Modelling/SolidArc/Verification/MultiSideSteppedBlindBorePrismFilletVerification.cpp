// Phase 32x — scale separated side-entering two-stage blind cavities to a bounded set of three through eight.
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
std::vector<Cavity> Three()
{
    return {{1,false,5,3.5,{{.7,3},{.3,7}}},{1,false,10,8,{{.8,4},{.35,8}}},{1,true,15,5,{{.75,3.5},{.32,7}}}};
}
std::vector<Cavity> Eight()
{
    std::vector<Cavity> Result;const std::vector<double>X{3,7.5,12.5,17},Cross{3.2,8.8};int I=0;
    for(double Z:Cross)for(double A:X)
    {
        double Outer=.55+.025*I,Inner=.24+.01*I,Shoulder=2.5+.15*(I%4),Floor=6+.2*(I%4);
        Result.push_back({1,false,A,Z,{{Outer,Shoulder},{Inner,Floor}}});++I;
    }
    return Result;
}
std::vector<Cavity> Nine()
{
    std::vector<Cavity> Result;const std::vector<double>X{2.5,6.25,10,13.75,17.5},Cross{3.2,8.8};
    for(double Z:Cross)for(double A:X)if(Result.size()<9)Result.push_back({1,false,A,Z,{{.45,2.5},{.2,6}}});
    return Result;
}
Deliver<BrepBody> AxialDualSteppedFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    struct Axial{double Y,Z;std::vector<Stage>Stages;};
    for(const Axial& C:std::vector<Axial>{{5,4,{{1.2,4},{.5,9}}},{12,8,{{1,5},{.4,10}}}})for(const Stage& S:C.Stages)
    {
        auto Cutter=BrepBody::Cylinder({-2,C.Y,C.Z},{1,0,0},S.Radius,S.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
std::vector<int> Rails(const BrepBody& B)
{
    std::vector<int> Result;for(size_t I=0;I<B.Edges.size();++I)
    {
        const BrepEdge& E=B.Edges[I];if(E.Curve.Classification!=CurveClassification::Line||E.VertexStart<0||E.VertexEnd<0)continue;
        Vec3 P=B.Vertices[E.VertexStart].Point,Q=B.Vertices[E.VertexEnd].Point;
        if(std::fabs(std::fabs((Q-P).Normalised().Dot({1,0,0}))-1)>1e-8)continue;
        if((std::fabs(P.Y)<1e-8||std::fabs(P.Y-16)<1e-8)&&(std::fabs(P.Z)<1e-8||std::fabs(P.Z-12)<1e-8))Result.push_back(static_cast<int>(I));
    }
    return Result;
}
struct Inspection{bool Topology=false,Supports=false,Circles=false,Dimensions=false;int InnerLoops=0,Annular=0,MaximumLoops=0;};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities,double FilletRadius)
{
    Inspection Q;size_t N=Cavities.size();auto Report=B.Validate();Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&
        B.Vertices.size()==16+4*N&&B.Edges.size()==24+6*N&&B.Coedges.size()==48+12*N&&B.Loops.size()==10+6*N&&B.Faces.size()==10+4*N;
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
        double Length=C.Along==1?16:12,Entry=C.High?Length:0,Shoulder=C.High?Length-C.Stages[0].Depth:C.Stages[0].Depth;
        double Floor=C.High?Length-C.Stages[1].Depth:C.Stages[1].Depth;std::vector<std::pair<double,double>>Expected{
            {Entry,C.Stages[0].Radius},{Shoulder,C.Stages[0].Radius},{Shoulder,C.Stages[1].Radius},{Floor,C.Stages[1].Radius}};
        for(const auto& [T,Radius]:Expected)
        {
            Vec3 Centre=C.Along==1?Vec3{C.X,T,C.Cross}:Vec3{C.X,C.Cross,T};int Matches=0;
            for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
                Matches+=E.Curve.Centre.Distance(Centre)<1e-8&&std::fabs(E.Curve.RadiusMajor-Radius)<1e-9&&
                    std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(SideAxis))-1)<1e-8;
            Q.Dimensions&=Matches==1;
        }
    }
    Q.Supports=Planes==static_cast<int>(6+2*N)&&Rolls==4&&CavityCylinders==static_cast<int>(2*N);
    Q.Circles=ExactCircles==static_cast<int>(4*N);return Q;
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
        std::vector<Band> Result;double Length=C.Along==1?16:12,Previous=0;
        for(const Stage& S:C.Stages){Result.push_back(C.High?Band{Length-S.Depth,Length-Previous,S.Radius}:Band{Previous,S.Depth,S.Radius});Previous=S.Depth;}return Result;
    };
    double Result=std::numeric_limits<double>::max();
    for(size_t I=0;I<Cavities.size();++I)for(size_t J=I+1;J<Cavities.size();++J)
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
    VerificationPanel P("SolidArc · Phase 32x · Multi Side-Stepped Blind-Bore Rounded-Prism Verification");const double Radius=1.5;
    const auto C3=Three();auto S=Fixture(C3);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Three separated side counterbores");
    P.Expect("Three-chain source is canonical V20/E30/C60/L24/F18 topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==20&&S.Payload.Edges.size()==30&&S.Payload.Coedges.size()==60&&S.Payload.Loops.size()==24&&S.Payload.Faces.size()==18);
    int Exact=0,Fitted=0;for(const BrepEdge& X:S.Payload.Edges)if(X.Closed()){Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Six exact and six fitted source rims identify all stages",Exact==6&&Fitted==6);
    P.Expect("Exactly four selected-axis rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,Radius,&Applied);auto Q=R?Inspect(R.Payload,C3,Radius):Inspection{};
    P.Expect("Three side counterbores commit with four outer rolls",R&&Applied==4);
    P.Expect("Three-chain output reaches V28/E42/C84/L28/F22 topology",Q.Topology);
    P.Expect("Twelve planes, four rolls, and six cavity cylinders remain",Q.Supports);
    P.Expect("All six planar levels preserve six independent inner loops",Q.InnerLoops==6);
    P.Expect("All twelve entrance, shoulder, and floor rims are exact",Q.Circles);
    P.Expect("Every three-chain centre, radius, side, and depth remains exact",Q.Dimensions);
    P.Within("Three-chain volume follows all six exact axial bands",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(C3,Radius)):1e9,.5);
    P.Expect("All three-chain stage pairs retain finite clearance",MinimumPairClearance(C3)>ScalarCriteria::MergeTolerance);
    P.Expect("Outer bounds and sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==18);

    P.Section("Eight-cavity bound, directions, invariance, and refusal");
    const auto C8=Eight();auto M=Fixture(C8);auto MSource=M.Payload.Validate();auto MRails=Rails(M.Payload);
    P.Expect("Eight-chain source reaches V40/E60/C120/L54/F38 topology",M&&MSource.Solid()&&MSource.Genus==0&&M.Payload.Vertices.size()==40&&
        M.Payload.Edges.size()==60&&M.Payload.Coedges.size()==120&&M.Payload.Loops.size()==54&&M.Payload.Faces.size()==38);
    int MA=-1;auto Multi=BlendSolver::FilletEdges(M.Payload,MRails,Radius,&MA);auto MQ=Multi?Inspect(Multi.Payload,C8,Radius):Inspection{};
    P.Expect("The bounded eight-counterbore maximum commits all four rolls",Multi&&MA==4);
    P.Expect("Eight-chain output reaches V48/E72/C144/L58/F42 topology",MQ.Topology);
    P.Expect("Twenty-two planes, four rolls, and sixteen cavity cylinders remain",MQ.Supports);
    P.Expect("One nine-loop entrance and eight shoulders retain sixteen inner loops",MQ.InnerLoops==16&&MQ.MaximumLoops==9&&MQ.Annular==8);
    P.Expect("All thirty-two eight-chain rims are exact rational circles",MQ.Circles);
    P.Expect("All eight side counterbores preserve exact dimensions",MQ.Dimensions);
    P.Within("Eight-chain volume follows all sixteen exact bands",Multi?std::fabs(Multi.Payload.Validate().Volume-ExactVolume(C8,Radius)):1e9,.5);
    P.Expect("Every pair among eight counterbores retains clearance",MinimumPairClearance(C8)>ScalarCriteria::MergeTolerance);
    const std::vector<Cavity>Z3{{2,false,6,5,{{1.3,4},{.6,8}}},{2,false,14,10,{{1.1,3.5},{.5,7}}},{2,false,10,13,{{.8,3},{.3,6}}}};
    auto Z=Fixture(Z3);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),Radius,&ZA);
    P.Expect("Three counterbores parallel to Z preserve side direction",ZR&&ZA==4&&Inspect(ZR.Payload,Z3,Radius).Dimensions);
    auto ReversedSpecs=C8;std::reverse(ReversedSpecs.begin(),ReversedSpecs.end());auto Reversed=Fixture(ReversedSpecs);int ReverseApplied=-1;
    auto ReverseR=BlendSolver::FilletEdges(Reversed.Payload,Rails(Reversed.Payload),Radius,&ReverseApplied);
    P.Expect("Reversed construction order yields identical rounded volume",ReverseR&&ReverseApplied==4&&std::fabs(ReverseR.Payload.Validate().Volume-Multi.Payload.Validate().Volume)<1e-9);
    std::vector<int>Reordered{MRails[3],MRails[0],MRails[2],MRails[1],MRails[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(M.Payload,Reordered,Radius,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-Multi.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6});BrepBody T=S.Payload.Transformed(Transform);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,E,Radius,&TA);
    P.Expect("Rigid translations preserve bounded side-step classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==22);
    P.Within("Rigid translations preserve three-chain volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto N=Fixture(Nine());P.Expect("A ninth two-stage side cavity remains outside the bound",N&&TransactionallyRefuses(N,Radius));
    auto Intersecting=C3;Intersecting[1].X=5.5;Intersecting[1].Cross=3.5;auto IS=Fixture(Intersecting);P.Expect("Intersecting stage bands refuse transactionally",TransactionallyRefuses(IS,Radius));
    auto Mixed=C3;Mixed[2].Along=2;Mixed[2].X=16;Mixed[2].Cross=10;auto MixedSource=Fixture(Mixed);P.Expect("Mixed Y/Z side-step axes remain unsupported",MixedSource&&TransactionallyRefuses(MixedSource,Radius));
    auto Wall=C3;Wall[1].Cross=2.1;auto WallSource=Fixture(Wall);P.Expect("Any outer disk crossing a rounded strip refuses",WallSource&&TransactionallyRefuses(WallSource,Radius));
    auto End=C3;End[1].X=.800000005;auto EndSource=Fixture(End);P.Expect("Any outer disk touching an end cap refuses",EndSource&&TransactionallyRefuses(EndSource,Radius));
    auto Eccentric=C3;Eccentric[1].Stages[1].XOffset=.3;auto EccentricSource=Fixture(Eccentric);P.Expect("An eccentric stage in the set refuses",EccentricSource&&TransactionallyRefuses(EccentricSource,Radius));
    auto Through=C3;Through[2].Stages[1].Depth=16;auto ThroughSource=Fixture(Through);P.Expect("Any final stage reaching the opposite wall refuses",ThroughSource&&TransactionallyRefuses(ThroughSource,Radius));
    auto MixedStages=C3;MixedStages[1].Stages.push_back({.2,11});auto MixedStageSource=Fixture(MixedStages);int MixedStageApplied=-1;
    auto MixedStageR=BlendSolver::FilletEdges(MixedStageSource.Payload,Rails(MixedStageSource.Payload),Radius,&MixedStageApplied);
    P.Expect("A multistage member delegates to the bounded mixed-stage route",MixedStageSource&&MixedStageR&&MixedStageApplied==4&&MixedStageR.Payload.Faces.size()==24);
    auto Dual=Fixture({C3[0],C3[1]});int DA=-1;auto DualR=BlendSolver::FilletEdges(Dual.Payload,Rails(Dual.Payload),Radius,&DA);
    P.Expect("The established dual side-counterbore route remains intact",DualR&&DA==4&&DualR.Payload.Faces.size()==18);
    auto Single=Fixture({C3[0]});int SA=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),Radius,&SA);
    P.Expect("The established single side-counterbore route remains intact",SingleR&&SA==4&&SingleR.Payload.Faces.size()==14);
    std::vector<Cavity>Simple;for(const Cavity& C:C8)Simple.push_back({C.Along,C.High,C.X,C.Cross,{{C.Stages[1].Radius,C.Stages[1].Depth}}});auto SimpleSource=Fixture(Simple);int SimpleApplied=-1;
    auto SimpleR=BlendSolver::FilletEdges(SimpleSource.Payload,Rails(SimpleSource.Payload),Radius,&SimpleApplied);
    P.Expect("The established eight simple-side-cavity route remains intact",SimpleR&&SimpleApplied==4&&SimpleR.Payload.Faces.size()==26);
    auto Axial=AxialDualSteppedFixture();int AA=-1;auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),Radius,&AA);
    P.Expect("The selected-axis dual-stepped route remains intact",AxialR&&AA==4&&AxialR.Payload.Faces.size()==18);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("SideStepSet",M.Payload);F.Matcap=6;std::string List;
    for(int I:MRails){if(!List.empty())List+=",";List+=std::to_string(I);}bool OK=H.Execute("fillet SideStepSet 1.5 --edges="+List+" --name=SideStepSetRound");auto* Output=H.Document().Find("SideStepSetRound");
    P.Expect("Console fillet commits all eight side counterbores",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==42);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32x_MultiSideSteppedBlindBorePrism.png";std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("ThreeSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&Add("ThreeRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("ThreeEntrances",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("EightSideSteps",Multi.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("EightEntrances",Multi.Payload,5)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&V.Execute("render sheet finalize Phase32x_MultiSideSteppedBlindBorePrism");
    P.Expect("C++ multi-side-stepped proof commands complete",Rendered);
    P.Expect("C++ multi-side-stepped proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
