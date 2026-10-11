// Phase 32v — scale one exact side-entering stepped blind cavity to three through eight decreasing diameters.
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
struct Stage{double Radius=1,Depth=1,XOffset=0,CrossOffset=0;};
struct SideCounterbore{int Along=1;bool High=false;double X=10,Cross=6;std::vector<Stage> Stages;};
Deliver<BrepBody> Fixture(const SideCounterbore& C)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const Stage& S:C.Stages)
    {
        double Length=C.Along==1?16:12,Start=C.High?Length-S.Depth:-2;Vec3 Origin,Axis;
        if(C.Along==1){Origin={C.X+S.XOffset,Start,C.Cross+S.CrossOffset};Axis={0,1,0};}
        else{Origin={C.X+S.XOffset,C.Cross+S.CrossOffset,Start};Axis={0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,S.Radius,S.Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
Deliver<BrepBody> BandedFixture(const SideCounterbore& C)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;double Previous=0;
    for(size_t I=0;I<C.Stages.size();++I)
    {
        const Stage& S=C.Stages[I];double Length=C.Along==1?16:12,Overlap=.1;
        double Start=C.High?Length-S.Depth:(I==0?-2:Previous-Overlap),Height=I==0?S.Depth+2:S.Depth-Previous+Overlap;Vec3 Origin,Axis;
        if(C.High&&I>0)Start=Length-S.Depth;
        if(C.Along==1){Origin={C.X+S.XOffset,Start,C.Cross+S.CrossOffset};Axis={0,1,0};}
        else{Origin={C.X+S.XOffset,C.Cross+S.CrossOffset,Start};Axis={0,0,1};}
        auto Cutter=BrepBody::Cylinder(Origin,Axis,S.Radius,Height);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);Previous=S.Depth;
    }
    return Working;
}
Deliver<BrepBody> MultipleSteppedFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(double X:std::vector<double>{7,13})for(const auto& [Radius,Depth]:std::vector<std::pair<double,double>>{{1.5,4},{.7,9}})
    {
        auto Cutter=BrepBody::Cylinder({X,-2,6},{0,1,0},Radius,Depth+2);if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;Working=std::move(Next);
    }
    return Working;
}
Deliver<BrepBody> ObliqueSideFixture()
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});auto Cutter=BrepBody::Cylinder({10,-2,5.7},{0,1,.08},.7,10);
    if(!Box||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> SimpleSideFixture()
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});auto Cutter=BrepBody::Cylinder({10,-2,6},{0,1,0},1,10);
    if(!Box||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> AxialMultiStageFixture()
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});
    for(const auto& S:std::vector<std::pair<double,double>>{{1.8,4},{1.2,8},{.6,12}})
    {
        auto Cutter=BrepBody::Cylinder({-2,8,6},{1,0,0},S.first,S.second+2);if(!Working||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
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
struct Evidence{int Exact=0,Fitted=0;};
Evidence SourceEvidence(const BrepBody& B)
{
    Evidence Q;for(const BrepEdge& E:B.Edges)if(E.Closed()){Q.Exact+=E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();Q.Fitted+=E.Curve.Classification==CurveClassification::Freeform&&!E.Curve.Rational();}return Q;
}
struct Inspection{bool Topology=false,Supports=false,Annuli=false,Circles=false,Dimensions=false;};
Inspection Inspect(const BrepBody& B,const SideCounterbore& C)
{
    Inspection Q;size_t N=C.Stages.size();auto Report=B.Validate();Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&
        B.Vertices.size()==16+2*N&&B.Edges.size()==24+3*N&&B.Coedges.size()==48+6*N&&B.Loops.size()==10+3*N&&B.Faces.size()==10+2*N;
    int Planes=0,Rolls=0,CavityCylinders=0,AnnularPlanes=0,ExactCircles=0;Vec3 Axis=C.Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;if(Surface.Classification==SurfaceClassification::Plane){++Planes;AnnularPlanes+=F.Loops.size()==2;}
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-2)<1e-9&&std::fabs(std::fabs(Surface.Axis.Normalised().Dot({1,0,0}))-1)<1e-8;
            double X=Surface.Origin.X,Cross=C.Along==1?Surface.Origin.Z:Surface.Origin.Y;
            for(const Stage& S:C.Stages)CavityCylinders+=std::fabs(Surface.RadiusMajor-S.Radius)<1e-9&&std::fabs(X-C.X)<1e-8&&
                std::fabs(Cross-C.Cross)<1e-8&&std::fabs(std::fabs(Surface.Axis.Normalised().Dot(Axis))-1)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    double Length=C.Along==1?16:12;std::vector<std::pair<double,double>> Expected{{C.High?Length:0,C.Stages.front().Radius}};Expected.reserve(2*N);
    for(size_t I=0;I<N;++I){double T=C.High?Length-C.Stages[I].Depth:C.Stages[I].Depth;Expected.push_back({T,C.Stages[I].Radius});if(I+1<N)Expected.push_back({T,C.Stages[I+1].Radius});}
    Q.Dimensions=true;for(const auto& [T,Radius]:Expected)
    {
        Vec3 Centre=C.Along==1?Vec3{C.X,T,C.Cross}:Vec3{C.X,C.Cross,T};int Matches=0;
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
            Matches+=E.Curve.Centre.Distance(Centre)<1e-8&&std::fabs(E.Curve.RadiusMajor-Radius)<1e-9&&std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(Axis))-1)<1e-8;
        Q.Dimensions&=Matches==1;
    }
    Q.Supports=Planes==static_cast<int>(6+N)&&Rolls==4&&CavityCylinders==static_cast<int>(N);Q.Annuli=AnnularPlanes==static_cast<int>(N);
    Q.Circles=ExactCircles==static_cast<int>(2*N);return Q;
}
double ExactVolume(const SideCounterbore& C)
{
    double Volume=20*(16*12-4*4*(1-ScalarCriteria::Pi/4)),Previous=0;
    for(const Stage& S:C.Stages){Volume-=ScalarCriteria::Pi*S.Radius*S.Radius*(S.Depth-Previous);Previous=S.Depth;}return Volume;
}
SideCounterbore Three(int Along=1,bool High=false,double X=10,double Cross=6)
{return {Along,High,X,Cross,{{1.8,3.5},{1.2,7},{.6,11}}};}
SideCounterbore Eight()
{return {1,false,10,6,{{2,2},{1.7,4},{1.4,6},{1.15,8},{.9,10},{.7,12},{.5,13.5},{.3,15}}};}
SideCounterbore Nine()
{return {1,false,10,6,{{2,1.5},{1.8,3},{1.6,4.5},{1.4,6},{1.2,7.5},{1,9},{.8,10.5},{.6,12},{.4,13.5}}};}
bool TransactionallyRefuses(const Deliver<BrepBody>& Source)
{if(!Source)return true;int Applied=99;auto Result=BlendSolver::FilletEdges(Source.Payload,Rails(Source.Payload),2,&Applied);return !Result&&Applied==0;}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32v · Multistage Side Blind-Bore Rounded-Prism Verification");
    const SideCounterbore C3=Three();auto S=Fixture(C3);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();auto SE=SourceEvidence(S.Payload);
    P.Section("Three decreasing coaxial side stages");
    P.Expect("Three-stage side source is canonical V14/E21/C42/L15/F12 topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&S.Payload.Vertices.size()==14&&S.Payload.Edges.size()==21&&S.Payload.Coedges.size()==42&&S.Payload.Loops.size()==15&&S.Payload.Faces.size()==12);
    P.Expect("Three exact stage floors and three fitted rims identify the source",SE.Exact==3&&SE.Fitted==3);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,C3):Inspection{};
    P.Expect("All three decreasing side stages commit with four outer rolls",R&&Applied==4);
    P.Expect("Three-stage output reaches exact V22/E33/C66/L19/F16 topology",Q.Topology);
    P.Expect("Nine planes, four rolls, and three rational side cylinders remain",Q.Supports);
    P.Expect("The entrance and both side shoulders are independently annular",Q.Annuli);
    P.Expect("All six side-stage rims are exact rational circles",Q.Circles);
    P.Expect("Every side radius and cumulative depth remains unchanged",Q.Dimensions);
    P.Within("Three-stage side volume follows every exact axial band",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(C3)):1e9,.5);
    P.Expect("Outer bounds and sharp multistage side source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==12);

    P.Section("Eight-stage bound, directions, invariance, and refusal");
    const SideCounterbore C8=Eight();auto M=Fixture(C8);auto ME=SourceEvidence(M.Payload);auto MSource=M.Payload.Validate();auto MRails=Rails(M.Payload);
    P.Expect("Eight-stage side source reaches V24/E36/C72/L30/F22 topology",M&&MSource.Solid()&&MSource.Genus==0&&M.Payload.Vertices.size()==24&&M.Payload.Edges.size()==36&&M.Payload.Coedges.size()==72&&M.Payload.Loops.size()==30&&M.Payload.Faces.size()==22);
    P.Expect("Eight exact floors and eight fitted intersections survive construction",ME.Exact==8&&ME.Fitted==8);
    int MA=-1;auto Multi=BlendSolver::FilletEdges(M.Payload,MRails,2,&MA);auto MQ=Multi?Inspect(Multi.Payload,C8):Inspection{};
    P.Expect("The bounded eight-stage side maximum commits all four rolls",Multi&&MA==4);
    P.Expect("Eight-stage side output reaches V32/E48/C96/L34/F26 topology",MQ.Topology);
    P.Expect("Fourteen planes, four rolls, and eight rational stages remain",MQ.Supports);
    P.Expect("Eight annular levels preserve the entrance and seven shoulders",MQ.Annuli);
    P.Expect("All sixteen bounded side-stage rims are exact rational circles",MQ.Circles);
    P.Expect("All eight decreasing radii and increasing depths remain exact",MQ.Dimensions);
    P.Within("Eight-stage side volume follows all exact bands",Multi?std::fabs(Multi.Payload.Validate().Volume-ExactVolume(C8)):1e9,.5);
    auto HighSpec=Three(1,true);auto High=Fixture(HighSpec);int HA=-1;auto HighR=BlendSolver::FilletEdges(High.Payload,Rails(High.Payload),2,&HA);
    P.Expect("The opposite Y side supports the same multistage chain",HighR&&HA==4&&Inspect(HighR.Payload,HighSpec).Dimensions);
    auto ZSpec=Three(2,true,10,8);auto Z=Fixture(ZSpec);int ZA=-1;auto ZR=BlendSolver::FilletEdges(Z.Payload,Rails(Z.Payload),2,&ZA);
    P.Expect("A retained Z side supports the same multistage chain",ZR&&ZA==4&&Inspect(ZR.Payload,ZSpec).Dimensions);
    std::vector<int> Reordered{MRails[3],MRails[0],MRails[2],MRails[1],MRails[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(M.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-Multi.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=M.Payload.Transformed(Transform);int TA=-1;auto TR=BlendSolver::FilletEdges(T,MRails,2,&TA);
    P.Expect("Rigid transforms preserve the eight-stage side classifier",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==26);
    P.Within("Rigid transforms preserve multistage side volume",Multi&&TR?std::fabs(Multi.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto OffsetSpec=Three(1,false,6,5);auto Offset=Fixture(OffsetSpec);int OA=-1;auto OffsetR=BlendSolver::FilletEdges(Offset.Payload,Rails(Offset.Payload),2,&OA);
    P.Expect("Safe selected-axis and wall-strip offsets preserve every stage",OffsetR&&OA==4&&Inspect(OffsetR.Payload,OffsetSpec).Dimensions);
    SideCounterbore Wall=Three();Wall.Cross=3.7;auto WallSource=Fixture(Wall);P.Expect("An outer stage crossing a rounded corner strip refuses transactionally",WallSource&&TransactionallyRefuses(WallSource));
    SideCounterbore End=Three();End.X=1.800000005;auto EndSource=Fixture(End);P.Expect("An outer stage touching an end cap refuses transactionally",EndSource&&TransactionallyRefuses(EndSource));
    SideCounterbore Eccentric=C3;Eccentric.Stages[1].XOffset=.4;auto ES=Fixture(Eccentric);P.Expect("An eccentric intermediate side stage remains unsupported",ES&&TransactionallyRefuses(ES));
    SideCounterbore Undercut=C3;Undercut.Stages.back().Radius=1.5;auto US=BandedFixture(Undercut);
    P.Expect("A non-decreasing undercut stage refuses transactionally",US&&TransactionallyRefuses(US));
    SideCounterbore Consumed=C3;Consumed.Stages[1].Depth=Consumed.Stages[0].Depth+5e-10;auto ConsumedSource=BandedFixture(Consumed);
    P.Expect("A merge-tolerance consumed axial shoulder refuses transactionally",ConsumedSource&&TransactionallyRefuses(ConsumedSource));
    auto Oblique=ObliqueSideFixture();P.Expect("Oblique side-cylinder topology refuses transactionally",Oblique&&TransactionallyRefuses(Oblique));
    BrepBody Malformed=S.Payload;Malformed.Faces.pop_back();int MalformedApplied=99;
    auto MalformedResult=BlendSolver::FilletEdges(Malformed,Rails(Malformed),2,&MalformedApplied);
    P.Expect("Malformed side-cavity topology refuses without partial application",!MalformedResult&&MalformedApplied==0);
    auto Multiple=MultipleSteppedFixture();int MultipleApplied=-1;auto MultipleR=BlendSolver::FilletEdges(Multiple.Payload,Rails(Multiple.Payload),2,&MultipleApplied);
    P.Expect("Exactly two side counterbores delegate to the dual-chain route",Multiple&&MultipleR&&MultipleApplied==4&&MultipleR.Payload.Faces.size()==18);
    SideCounterbore Through=C3;Through.Stages.back().Depth=16;auto ThroughSource=Fixture(Through);P.Expect("A final side stage reaching the opposite wall remains unsupported",ThroughSource&&TransactionallyRefuses(ThroughSource));
    auto N=Fixture(Nine());P.Expect("Nine side diameters remain outside the explicit bound",N&&TransactionallyRefuses(N));
    SideCounterbore Two{1,false,10,6,{{1.5,5},{.75,10}}};auto D=Fixture(Two);int DA=-1;auto DR=BlendSolver::FilletEdges(D.Payload,Rails(D.Payload),2,&DA);
    P.Expect("The established two-stage side route remains intact",DR&&DA==4&&DR.Payload.Faces.size()==14);
    auto Simple=SimpleSideFixture();int SA=-1;auto SimpleR=BlendSolver::FilletEdges(Simple.Payload,Rails(Simple.Payload),2,&SA);
    P.Expect("The established simple side route remains intact",SimpleR&&SA==4&&SimpleR.Payload.Faces.size()==12);
    auto Axial=AxialMultiStageFixture();int AA=-1;auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),2,&AA);
    P.Expect("The selected-axis multistage route remains intact",AxialR&&AA==4&&AxialR.Payload.Faces.size()==16);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("SideMultiStep",M.Payload);F.Matcap=6;std::string List;
    for(int I:MRails){if(!List.empty())List+=",";List+=std::to_string(I);}bool OK=H.Execute("fillet SideMultiStep 2 --edges="+List+" --name=SideMultiStepRound");auto* Output=H.Document().Find("SideMultiStepRound");
    P.Expect("Console fillet commits one exact eight-stage side cavity",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==26);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32v_MultiStageSideBlindBorePrism.png";std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("SideThreeSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&Add("SideThreeRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("SideThreeRings",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("SideEightStages",Multi.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("SideEightRings",Multi.Payload,5)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&V.Execute("render sheet finalize Phase32v_MultiStageSideBlindBorePrism");
    P.Expect("C++ multistage-side proof commands complete",Rendered);
    P.Expect("C++ multistage-side proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
