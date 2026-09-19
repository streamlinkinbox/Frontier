// Phase 32p — scale one exact coaxial stepped blind cavity to a bounded chain of three through eight decreasing diameters.
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
struct Stage { double Radius=1,Depth=1,YOffset=0,ZOffset=0; };
struct Counterbore { bool High=true;double Y=8,Z=6;std::vector<Stage> Stages; };
Deliver<BrepBody> Fixture(const Counterbore& C)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});if(!Working)return Working;
    for(const Stage& S:C.Stages)
    {
        auto Cutter=BrepBody::Cylinder(C.High?Vec3{22,C.Y+S.YOffset,C.Z+S.ZOffset}:Vec3{-2,C.Y+S.YOffset,C.Z+S.ZOffset},
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
struct Evidence{int Exact=0,Fitted=0;};
Evidence SourceEvidence(const BrepBody& B)
{
    Evidence Q;
    for(const BrepEdge& E:B.Edges)if(E.Closed())
    {
        Q.Exact+=E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
        Q.Fitted+=E.Curve.Classification==CurveClassification::Freeform&&!E.Curve.Rational();
    }
    return Q;
}
struct Inspection{bool Topology=false,Supports=false,Annuli=false,Circles=false,Dimensions=false;};
Inspection Inspect(const BrepBody& B,const Counterbore& C)
{
    Inspection Q;size_t N=C.Stages.size();auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==16+2*N&&B.Edges.size()==24+3*N&&
        B.Coedges.size()==48+6*N&&B.Loops.size()==10+3*N&&B.Faces.size()==10+2*N;
    int Planes=0,Rolls=0,CavityCylinders=0,AnnularPlanes=0,ExactCircles=0;
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& Surface=F.Surface;
        if(Surface.Classification==SurfaceClassification::Plane){++Planes;AnnularPlanes+=F.Loops.size()==2;}
        if(Surface.Classification==SurfaceClassification::Cylinder&&Surface.Rational())
        {
            Rolls+=std::fabs(Surface.RadiusMajor-1.5)<1e-9;
            for(const Stage& S:C.Stages)CavityCylinders+=std::fabs(Surface.RadiusMajor-S.Radius)<1e-9&&
                std::fabs(Surface.Origin.Y-C.Y)<1e-8&&std::fabs(Surface.Origin.Z-C.Z)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    std::vector<std::pair<double,double>> Expected;Expected.reserve(2*N);
    Expected.push_back({C.High?20:0,C.Stages.front().Radius});
    for(size_t I=0;I<N;++I)
    {
        double X=C.High?20-C.Stages[I].Depth:C.Stages[I].Depth;Expected.push_back({X,C.Stages[I].Radius});
        if(I+1<N)Expected.push_back({X,C.Stages[I+1].Radius});
    }
    Q.Dimensions=true;
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
    Q.Supports=Planes==static_cast<int>(6+N)&&Rolls==4&&CavityCylinders==static_cast<int>(N);
    Q.Annuli=AnnularPlanes==static_cast<int>(N);Q.Circles=ExactCircles==static_cast<int>(2*N);
    return Q;
}
double ExactVolume(const Counterbore& C)
{
    double Volume=20*(16*12-4*1.5*1.5*(1-ScalarCriteria::Pi/4)),Previous=0;
    for(const Stage& S:C.Stages){Volume-=ScalarCriteria::Pi*S.Radius*S.Radius*(S.Depth-Previous);Previous=S.Depth;}
    return Volume;
}
Counterbore Three(bool High=true,double Y=8,double Z=6)
{
    return {High,Y,Z,{{2,4},{1.2,8},{.6,12}}};
}
Counterbore Eight()
{
    Counterbore C;for(int I=0;I<8;++I)C.Stages.push_back({2.4-.25*I,2.0+2*I});return C;
}
Counterbore Nine()
{
    Counterbore C;for(int I=0;I<9;++I)C.Stages.push_back({2.5-.22*I,1.8+1.8*I});return C;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32p · Multistage Blind-Bore Rounded-Prism Verification");
    const Counterbore C3=Three();auto S=Fixture(C3);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();auto SE=SourceEvidence(S.Payload);
    P.Section("Three decreasing coaxial stages");
    P.Expect("Three-stage source is canonical V14/E21/C42/L15/F12 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==14&&S.Payload.Edges.size()==21&&S.Payload.Coedges.size()==42&&S.Payload.Loops.size()==15&&S.Payload.Faces.size()==12);
    P.Expect("Three exact stage floors and three fitted Boolean rims identify the source",SE.Exact==3&&SE.Fitted==3);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,1.5,&Applied);
    P.Expect("All three decreasing stages commit with the four outer rolls",R&&Applied==4);
    auto Q=R?Inspect(R.Payload,C3):Inspection{};
    P.Expect("Three-stage output reaches exact V22/E33/C66/L19/F16 topology",Q.Topology);
    P.Expect("Nine planes, four rolls, and three rational cavity cylinders remain",Q.Supports);
    P.Expect("The entrance and both interior shoulders are independently annular",Q.Annuli);
    P.Expect("All six stage rims are exact rational circles",Q.Circles);
    P.Expect("Every radius and cumulative stage depth remains unchanged",Q.Dimensions);
    P.Within("Three-stage rounded volume follows all exact axial bands",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(C3)):1e9,.5);
    P.Expect("Outer bounds and the sharp multistage source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==12);

    P.Section("Eight-stage cap and exact linear topology");
    const Counterbore C8=Eight();auto M=Fixture(C8);auto ME=SourceEvidence(M.Payload);auto MSource=M.Payload.Validate();auto MRails=Rails(M.Payload);
    P.Expect("Eight-stage source reaches canonical V24/E36/C72/L30/F22 topology",M&&MSource.Solid()&&MSource.Hulls==1&&MSource.Genus==0&&
        M.Payload.Vertices.size()==24&&M.Payload.Edges.size()==36&&M.Payload.Coedges.size()==72&&M.Payload.Loops.size()==30&&M.Payload.Faces.size()==22);
    P.Expect("Eight exact floors and eight fitted intersections survive source construction",ME.Exact==8&&ME.Fitted==8);
    int MA=-1;auto Multi=BlendSolver::FilletEdges(M.Payload,MRails,1.5,&MA);auto MQ=Multi?Inspect(Multi.Payload,C8):Inspection{};
    P.Expect("The bounded eight-stage maximum commits all four outer rolls",Multi&&MA==4);
    P.Expect("Eight-stage output reaches exact V32/E48/C96/L34/F26 topology",MQ.Topology);
    P.Expect("Fourteen planes, four rolls, and eight rational stage cylinders remain",MQ.Supports);
    P.Expect("Eight annular levels preserve the entrance and seven shoulders",MQ.Annuli);
    P.Expect("All sixteen stage rims are exact rational circles",MQ.Circles);
    P.Expect("All eight decreasing radii and cumulative depths remain unchanged",MQ.Dimensions);
    P.Within("Eight-stage rounded volume follows all eight axial bands",Multi?std::fabs(Multi.Payload.Validate().Volume-ExactVolume(C8)):1e9,.5);
    bool Monotonic=true;
    for(size_t I=1;I<C8.Stages.size();++I)Monotonic&=C8.Stages[I].Radius<C8.Stages[I-1].Radius&&C8.Stages[I].Depth>C8.Stages[I-1].Depth;
    P.Expect("All bounded stages retain strict radial decrease and axial increase",Monotonic);

    P.Section("Direction, order, transforms, and refusal");
    auto LowSpec=Three(false);auto Low=Fixture(LowSpec);int LA=-1;auto LowR=BlendSolver::FilletEdges(Low.Payload,Rails(Low.Payload),1.5,&LA);
    P.Expect("The opposite prism end supports the same multistage chain",LowR&&LA==4&&Inspect(LowR.Payload,LowSpec).Dimensions);
    auto OffsetSpec=Three(true,5,4);auto Offset=Fixture(OffsetSpec);int OA=-1;auto OffsetR=BlendSolver::FilletEdges(Offset.Payload,Rails(Offset.Payload),1.5,&OA);
    P.Expect("A safe transverse offset preserves every stage",OffsetR&&OA==4&&Inspect(OffsetR.Payload,OffsetSpec).Dimensions);
    std::vector<int> Reordered{MRails[3],MRails[0],MRails[2],MRails[1],MRails[3]};int RA=-1;
    auto RR=BlendSolver::FilletEdges(M.Payload,Reordered,1.5,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-Multi.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=M.Payload.Transformed(Transform);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,MRails,1.5,&TA);
    P.Expect("Rigid transforms preserve the eight-stage classifier",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==26);
    P.Within("Rigid transforms preserve multistage cavity volume",Multi&&TR?std::fabs(Multi.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    Counterbore Wall{true,1.25,1.25,{{1.2,4},{.7,8},{.3,12}}};auto WallSource=Fixture(Wall);int Refused=99;
    auto WallR=BlendSolver::FilletEdges(WallSource.Payload,Rails(WallSource.Payload),1.5,&Refused);
    P.Expect("One outer stage crossing the rounded wall refuses transactionally",WallSource&&!WallR&&Refused==0);
    Counterbore Eccentric=C3;Eccentric.Stages[1].YOffset=.3;auto ES=Fixture(Eccentric);Refused=99;
    auto ER=BlendSolver::FilletEdges(ES.Payload,Rails(ES.Payload),1.5,&Refused);
    P.Expect("An eccentric intermediate stage remains outside the coaxial chain route",ES&&ES.Payload.Validate().Solid()&&!ER&&Refused==0);
    auto N=Fixture(Nine());Refused=99;auto NR=BlendSolver::FilletEdges(N.Payload,Rails(N.Payload),1.5,&Refused);
    P.Expect("Nine diameter stages remain outside the explicit bounded route",N&&N.Payload.Validate().Solid()&&!NR&&Refused==0);
    Counterbore Dual{true,8,6,{{1.6,6},{.8,12}}};auto D=Fixture(Dual);int DA=-1;auto DR=BlendSolver::FilletEdges(D.Payload,Rails(D.Payload),1.5,&DA);
    P.Expect("The established two-stage counterbore route remains intact",DR&&DA==4&&DR.Payload.Faces.size()==14);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("MultiStep",M.Payload);F.Matcap=6;
    std::string List;for(int I:MRails){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet MultiStep 1.5 --edges="+List+" --name=MultiStepRound");auto* Output=H.Document().Find("MultiStepRound");
    P.Expect("Console fillet commits one exact eight-stage cavity",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==26);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32p_MultiStageBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("ThreeSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("ThreeRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("ThreeStages",R.Payload,3)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("EightStages",Multi.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("EightRings",Multi.Payload,5)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32p_MultiStageBlindBorePrism");
    P.Expect("C++ multistage-blind-bore proof commands complete",Rendered);
    P.Expect("C++ multistage-blind-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
