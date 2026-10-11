// Phase 32w — preserve exactly two separated two-stage side-entering blind cavities while rounding one prism family.
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
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
        Working=std::move(Next);
    }
    return Working;
}
Deliver<BrepBody> AxialDualSteppedFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    struct Axial{bool High;double Y,Z;std::vector<Stage>Stages;};
    for(const Axial& C:std::vector<Axial>{{false,5,4,{{1.2,4},{.5,9}}},{false,12,8,{{1,5},{.4,10}}}})
        for(const Stage& S:C.Stages)
        {
            auto Cutter=BrepBody::Cylinder(C.High?Vec3{22,C.Y,C.Z}:Vec3{-2,C.Y,C.Z},C.High?Vec3{-1,0,0}:Vec3{1,0,0},S.Radius,S.Depth+2);
            if(!Cutter)return Cutter;
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
struct Inspection{bool Topology=false,Supports=false,Circles=false,Dimensions=false;int InnerLoops=0,Annular=0,Triple=0;};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities)
{
    Inspection Q;auto Report=B.Validate();Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&
        B.Vertices.size()==24&&B.Edges.size()==36&&B.Coedges.size()==72&&B.Loops.size()==22&&B.Faces.size()==18;
    int Planes=0,Rolls=0,CavityCylinders=0,ExactCircles=0;Vec3 SideAxis=Cavities.front().Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.Annular+=F.Loops.size()==2;Q.Triple+=F.Loops.size()==3;
            if(F.Loops.size()>1)Q.InnerLoops+=static_cast<int>(F.Loops.size()-1);
        }
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-2)<1e-9&&std::fabs(std::fabs(Surface.Axis.Normalised().Dot({1,0,0}))-1)<1e-8;
            for(const Cavity& C:Cavities)for(const Stage& S:C.Stages)
            {
                double X=Surface.Origin.X,Cross=C.Along==1?Surface.Origin.Z:Surface.Origin.Y;
                CavityCylinders+=std::fabs(Surface.RadiusMajor-S.Radius)<1e-9&&std::fabs(X-C.X)<1e-8&&std::fabs(Cross-C.Cross)<1e-8&&
                    std::fabs(std::fabs(Surface.Axis.Normalised().Dot(SideAxis))-1)<1e-8;
            }
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    Q.Dimensions=true;
    for(const Cavity& C:Cavities)
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
    Q.Supports=Planes==10&&Rolls==4&&CavityCylinders==4;Q.Circles=ExactCircles==8;return Q;
}
double ExactVolume(const std::vector<Cavity>& Cavities)
{
    double Volume=20*(16*12-4*4*(1-ScalarCriteria::Pi/4));
    for(const Cavity& C:Cavities){double Previous=0;for(const Stage& S:C.Stages){Volume-=ScalarCriteria::Pi*S.Radius*S.Radius*(S.Depth-Previous);Previous=S.Depth;}}
    return Volume;
}
double PairClearance(const Cavity& A,const Cavity& B)
{
    struct Band{double Low,High,Radius;};auto Bands=[](const Cavity& C)
    {
        std::vector<Band> Result;double Length=C.Along==1?16:12,Previous=0;
        for(const Stage& S:C.Stages){Result.push_back(C.High?Band{Length-S.Depth,Length-Previous,S.Radius}:Band{Previous,S.Depth,S.Radius});Previous=S.Depth;}return Result;
    };
    double Result=std::numeric_limits<double>::max(),Centres=std::hypot(A.X-B.X,A.Cross-B.Cross);
    for(const Band& X:Bands(A))for(const Band& Y:Bands(B))
    {
        double Axial=std::max({0.0,X.Low-Y.High,Y.Low-X.High});double Radial=std::max(0.0,Centres-X.Radius-Y.Radius);
        Result=std::min(Result,std::hypot(Axial,Radial));
    }
    return Result;
}
bool TransactionallyRefuses(const Deliver<BrepBody>& Source)
{if(!Source)return true;int Applied=99;auto Result=BlendSolver::FilletEdges(Source.Payload,Rails(Source.Payload),2,&Applied);return !Result&&Applied==0;}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32w · Dual Side-Stepped Blind-Bore Rounded-Prism Verification");
    const std::vector<Cavity> Same{{1,false,6,4,{{.9,4},{.4,9}}},{1,false,14,8,{{1.1,5},{.5,10}}}};
    auto S=Fixture(Same);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Two separated counterbores entering one retained side");
    P.Expect("Dual side-stepped source is canonical V16/E24/C48/L18/F14 topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==16&&S.Payload.Edges.size()==24&&S.Payload.Coedges.size()==48&&S.Payload.Loops.size()==18&&S.Payload.Faces.size()==14);
    int Planes=0,Cylinders=0,Exact=0,Fitted=0;for(const BrepFace& F:S.Payload.Faces){Planes+=F.Surface.Classification==SurfaceClassification::Plane;Cylinders+=F.Surface.Classification==SurfaceClassification::Cylinder;}
    for(const BrepEdge& X:S.Payload.Edges)if(X.Closed()){Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();}
    P.Expect("Source contains ten planes and four rational cylindrical walls",Planes==10&&Cylinders==4);
    P.Expect("Four exact and four fitted rims identify both side chains",Exact==4&&Fitted==4);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,Same):Inspection{};
    P.Expect("Both side counterbores commit with all four outer rolls",R&&Applied==4);
    P.Expect("Output reaches exact V24/E36/C72/L22/F18 genus-zero topology",Q.Topology);
    P.Expect("Ten planes, four rolls, and four rational cavity cylinders remain",Q.Supports);
    P.Expect("One triple-loop entry and two shoulders retain four inner loops",Q.InnerLoops==4&&Q.Triple==1&&Q.Annular==2);
    P.Expect("All eight entrance, shoulder, and floor rims are exact circles",Q.Circles);
    P.Expect("Both side chains preserve centres, radii, directions, and depths",Q.Dimensions);
    P.Within("Same-side rounded volume follows all four exact bands",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Same)):1e9,.5);
    P.Expect("Same-side stage bands retain positive radial clearance",PairClearance(Same[0],Same[1])>ScalarCriteria::MergeTolerance);
    P.Expect("Outer bounds and sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==14);

    P.Section("Opposite sides, alternate direction, invariance, and refusal");
    const std::vector<Cavity> Opposite{{1,false,6,5,{{1,4},{.5,8}}},{1,true,14,7,{{1.2,3.5},{.6,8}}}};
    auto O=Fixture(Opposite);int OA=-1;auto OR=BlendSolver::FilletEdges(O.Payload,Rails(O.Payload),2,&OA);auto OQ=OR?Inspect(OR.Payload,Opposite):Inspection{};
    P.Expect("Counterbores entering opposite parallel sides use the exact route",OR&&OA==4&&OQ.Topology&&OQ.Dimensions);
    P.Expect("Opposite entrances and both shoulders form four annular planes",OQ.InnerLoops==4&&OQ.Annular==4&&OQ.Triple==0);
    P.Within("Opposite-side rounded volume follows all four bands",OR?std::fabs(OR.Payload.Validate().Volume-ExactVolume(Opposite)):1e9,.5);
    P.Expect("Opposite noncoaxial stage bands retain finite clearance",PairClearance(Opposite[0],Opposite[1])>ScalarCriteria::MergeTolerance);
    const std::vector<Cavity> Coaxial{{1,false,10,6,{{1,3},{.5,6}}},{1,true,10,6,{{1,3},{.5,6}}}};
    auto C=Fixture(Coaxial);int CA=-1;auto CR=BlendSolver::FilletEdges(C.Payload,Rails(C.Payload),2,&CA);
    P.Expect("Coaxial opposite-side counterbores survive across a ligament",CR&&CA==4&&Inspect(CR.Payload,Coaxial).Dimensions);
    std::vector<double>RimY;if(CR)for(const BrepEdge& X:CR.Payload.Edges)if(X.Closed())RimY.push_back(X.Curve.Centre.Y);std::sort(RimY.begin(),RimY.end());
    P.Expect("Coaxial side floors preserve the requested four-unit ligament",RimY.size()==8&&std::fabs(RimY[3]-6)<1e-8&&std::fabs(RimY[4]-10)<1e-8);
    const std::vector<Cavity> AlongZ{{2,false,6,5,{{1.3,4},{.6,8}}},{2,false,14,10,{{1.1,3.5},{.5,7}}}};
    auto Z=Fixture(AlongZ);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),2,&ZA);
    P.Expect("Two stepped cavities parallel to Z preserve side direction",ZR&&ZA==4&&Inspect(ZR.Payload,AlongZ).Dimensions);
    auto Reversed=Fixture({Same[1],Same[0]});int ReverseApplied=-1;auto ReverseR=BlendSolver::FilletEdges(Reversed.Payload,Rails(Reversed.Payload),2,&ReverseApplied);
    P.Expect("Reversed cavity construction order yields identical volume",ReverseR&&ReverseApplied==4&&std::fabs(ReverseR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    std::vector<int>Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({0,0,1},.4);BrepBody T=S.Payload.Transformed(M);int TA=-1;auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve dual side-stepped classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==18);
    P.Within("Rigid transforms preserve dual side-stepped volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    const std::vector<Cavity> Offset{{1,false,5,4,{{.8,4},{.3,8}}},{1,false,12,7,{{1,5},{.4,10}}}};auto OffsetSource=Fixture(Offset);int FA=-1;
    auto OffsetR=BlendSolver::FilletEdges(OffsetSource.Payload,Rails(OffsetSource.Payload),2,&FA);
    P.Expect("Safe selected-axis and side-strip offsets preserve both chains",OffsetR&&FA==4&&Inspect(OffsetR.Payload,Offset).Dimensions);
    auto Corner=Fixture({Same[0],{1,false,14,2.8,{{.9,4},{.4,9}}}});P.Expect("Either outer disk crossing a rounded corner refuses",Corner&&TransactionallyRefuses(Corner));
    auto End=Fixture({Same[0],{1,false,1.000000005,8,{{1,4},{.4,9}}}});P.Expect("Either outer disk touching an end cap refuses",End&&TransactionallyRefuses(End));
    auto Intersecting=Fixture({{1,false,8,6,{{1.5,4},{.7,9}}},{1,false,9,6,{{1.5,5},{.6,10}}}});P.Expect("Intersecting side-stage bands cannot partially commit",TransactionallyRefuses(Intersecting));
    auto Mixed=Fixture({{1,false,5,4,{{.7,3},{.3,6}}},{2,false,15,10,{{.8,3},{.35,6}}}});P.Expect("Mixed Y/Z side-step axes remain unsupported",Mixed&&TransactionallyRefuses(Mixed));
    auto EccentricSpecs=Same;EccentricSpecs[1].Stages[1].XOffset=.3;auto Eccentric=Fixture(EccentricSpecs);P.Expect("An eccentric inner stage refuses transactionally",Eccentric&&TransactionallyRefuses(Eccentric));
    auto ThroughSpecs=Same;ThroughSpecs[1].Stages[1].Depth=16;auto Through=Fixture(ThroughSpecs);P.Expect("Either final stage reaching the opposite wall refuses",Through&&TransactionallyRefuses(Through));
    const Cavity Third{1,false,10,6,{{.7,4},{.3,8}}};auto Three=Fixture({Same[0],Same[1],Third});int ThreeApplied=-1;
    auto ThreeR=BlendSolver::FilletEdges(Three.Payload,Rails(Three.Payload),2,&ThreeApplied);
    P.Expect("A third two-stage side cavity delegates to the bounded-set route",Three&&ThreeR&&ThreeApplied==4&&ThreeR.Payload.Faces.size()==22);
    auto MixedStages=Same;MixedStages[1].Stages.push_back({.2,12});auto FiveStageSource=Fixture(MixedStages);int FiveStageApplied=-1;
    auto FiveStageR=BlendSolver::FilletEdges(FiveStageSource.Payload,Rails(FiveStageSource.Payload),2,&FiveStageApplied);
    P.Expect("A multistage plus two-stage pair delegates to the mixed-stage route",FiveStageSource&&FiveStageR&&FiveStageApplied==4&&FiveStageR.Payload.Faces.size()==20);
    auto Single=Fixture({Same[0]});int SA=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),2,&SA);
    P.Expect("The established single side-counterbore route remains intact",SingleR&&SA==4&&SingleR.Payload.Faces.size()==14);
    auto Simple=Fixture({{1,false,6,4,{{.8,8}}},{1,false,14,8,{{1,10}}}});int SimpleApplied=-1;auto SimpleR=BlendSolver::FilletEdges(Simple.Payload,Rails(Simple.Payload),2,&SimpleApplied);
    P.Expect("The established dual simple-side route remains intact",SimpleR&&SimpleApplied==4&&SimpleR.Payload.Faces.size()==14);
    auto Axial=AxialDualSteppedFixture();int AA=-1;auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),2,&AA);
    P.Expect("The selected-axis dual-stepped route remains intact",AxialR&&AA==4&&AxialR.Payload.Faces.size()==18);
    auto MultiSpec=Same[0];MultiSpec.Stages.push_back({.2,12});auto Multi=Fixture({MultiSpec});int MA=-1;auto MultiR=BlendSolver::FilletEdges(Multi.Payload,Rails(Multi.Payload),2,&MA);
    P.Expect("The single multistage side route remains intact",MultiR&&MA==4&&MultiR.Payload.Faces.size()==16);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("DualSideStep",S.Payload);F.Matcap=6;std::string List;
    for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}bool OK=H.Execute("fillet DualSideStep 2 --edges="+List+" --name=DualSideStepRound");auto* Output=H.Document().Find("DualSideStepRound");
    P.Expect("Console fillet commits two exact side counterbores",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==18);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32w_DualSideSteppedBlindBorePrism.png";std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("DualSideStepSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&Add("DualSideStepRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("SameSideSteps",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("OppositeSteps",OR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("CoaxialLigament",CR.Payload,5)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&V.Execute("render sheet finalize Phase32w_DualSideSteppedBlindBorePrism");
    P.Expect("C++ dual-side-stepped proof commands complete",Rendered);
    P.Expect("C++ dual-side-stepped proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
