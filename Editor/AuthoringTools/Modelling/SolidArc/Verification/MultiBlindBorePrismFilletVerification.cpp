// Phase 32n — scale exact blind-cavity preservation to a bounded set of three through eight separated cylinders.
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
#include <vector>
using namespace Frontier;
namespace {
struct Cavity { bool High=true; double Y=0,Z=0,Radius=1,Depth=1; };
Deliver<BrepBody> Fixture(const std::vector<Cavity>& Cavities)
{
    auto Working=BrepBody::Box({0,0,0},{20,16,12});
    if(!Working)return Working;
    for(const Cavity& C:Cavities)
    {
        auto Cutter=BrepBody::Cylinder(C.High?Vec3{22,C.Y,C.Z}:Vec3{-2,C.Y,C.Z},
                                      C.High?Vec3{-1,0,0}:Vec3{1,0,0},C.Radius,C.Depth+2);
        if(!Cutter)return Cutter;
        auto Next=IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);
        if(!Next)return Next;
        Working=std::move(Next);
    }
    return Working;
}
std::vector<int> Rails(const BrepBody& B)
{
    std::vector<int> Result;
    for(size_t I=0;I<B.Edges.size();++I)
    {
        const BrepEdge& E=B.Edges[I];
        if(E.Curve.Classification!=CurveClassification::Line||E.VertexStart<0||E.VertexEnd<0)continue;
        Vec3 P=B.Vertices[E.VertexStart].Point,Q=B.Vertices[E.VertexEnd].Point;
        if(std::fabs(std::fabs((Q-P).Normalised().Dot({1,0,0}))-1)>1e-8)continue;
        if((std::fabs(P.Y)<1e-8||std::fabs(P.Y-16)<1e-8)&&
           (std::fabs(P.Z)<1e-8||std::fabs(P.Z-12)<1e-8))Result.push_back(static_cast<int>(I));
    }
    return Result;
}
struct Evidence { int Exact=0,Fitted=0; };
Evidence SourceEvidence(const BrepBody& B)
{
    Evidence Result;
    for(const BrepEdge& E:B.Edges)if(E.Closed())
    {
        Result.Exact+=E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
        Result.Fitted+=E.Curve.Classification==CurveClassification::Freeform&&!E.Curve.Rational();
    }
    return Result;
}
struct Inspection
{
    bool Topology=false,Supports=false,Circles=false,Dimensions=false;
    int InnerLoops=0;
    std::vector<int> EndCapLoops;
};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities)
{
    Inspection Q;const size_t N=Cavities.size();auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==16+2*N&&B.Edges.size()==24+3*N&&
        B.Coedges.size()==48+6*N&&B.Loops.size()==10+3*N&&B.Faces.size()==10+2*N;
    int Planes=0,Rolls=0,CavitySurfaces=0,ExactCircles=0;
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& S=F.Surface;
        if(S.Classification==SurfaceClassification::Plane)
        {
            ++Planes;
            if(F.Loops.size()>1){Q.InnerLoops+=static_cast<int>(F.Loops.size()-1);Q.EndCapLoops.push_back(static_cast<int>(F.Loops.size()));}
        }
        if(S.Classification==SurfaceClassification::Cylinder&&S.Rational())
        {
            Rolls+=std::fabs(S.RadiusMajor-1.5)<1e-9;
            for(const Cavity& C:Cavities)
                if(std::fabs(S.RadiusMajor-C.Radius)<1e-9&&std::fabs(S.Origin.Y-C.Y)<1e-8&&std::fabs(S.Origin.Z-C.Z)<1e-8)
                    ++CavitySurfaces;
        }
    }
    Q.Dimensions=true;
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    for(const Cavity& C:Cavities)
    {
        int Rims=0;double Entry=C.High?20:0,Floor=C.High?20-C.Depth:C.Depth;
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
        {
            Vec3 Sample=E.Curve.Sample(E.Curve.DomainStart());
            if(std::fabs(std::hypot(Sample.Y-C.Y,Sample.Z-C.Z)-C.Radius)<1e-7&&
               (std::fabs(Sample.X-Entry)<1e-7||std::fabs(Sample.X-Floor)<1e-7))++Rims;
        }
        Q.Dimensions&=Rims==2;
    }
    std::sort(Q.EndCapLoops.begin(),Q.EndCapLoops.end());
    Q.Supports=Planes==static_cast<int>(6+N)&&Rolls==4&&CavitySurfaces==static_cast<int>(N);
    Q.Circles=ExactCircles==static_cast<int>(2*N);
    return Q;
}
double ExactVolume(const std::vector<Cavity>& Cavities)
{
    constexpr double Fillet=1.5;
    double Volume=20*(16*12-4*Fillet*Fillet*(1-ScalarCriteria::Pi/4));
    for(const Cavity& C:Cavities)Volume-=ScalarCriteria::Pi*C.Radius*C.Radius*C.Depth;
    return Volume;
}
double PairDistance(const Cavity& A,const Cavity& B)
{
    double ALow=A.High?20-A.Depth:0,AHigh=A.High?20:A.Depth;
    double BLow=B.High?20-B.Depth:0,BHigh=B.High?20:B.Depth;
    double Axial=std::max({0.0,ALow-BHigh,BLow-AHigh});
    double Radial=std::max(0.0,std::hypot(A.Y-B.Y,A.Z-B.Z)-A.Radius-B.Radius);
    return std::hypot(Axial,Radial);
}
std::vector<Cavity> EightCavities()
{
    std::vector<Cavity> Result;const double Y[]{3,6.5,10,13.5};
    for(int Z=0;Z<2;++Z)for(int B=0;B<4;++B)
    {
        int I=Z*4+B;Result.push_back({I%2==0,Y[B],Z?8.0:4.0,.6,5.0+(I%3)});
    }
    return Result;
}
std::vector<Cavity> NineCavities()
{
    std::vector<Cavity> Result;
    for(int Z=0;Z<3;++Z)for(int B=0;B<3;++B)
    {
        int I=Z*3+B;Result.push_back({true,4.0+B*4.0,3.0+Z*3.0,.4,5.0+(I%2)});
    }
    return Result;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32n · Multi-Blind-Bore Rounded-Prism Verification");
    const std::vector<Cavity> Three{{true,4,3,.6,8},{false,8,8,.75,7},{true,12,4,.8,10}};
    auto S=Fixture(Three);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();auto SE=SourceEvidence(S.Payload);
    P.Section("Three separated finite cavities");
    P.Expect("Three-cavity source is canonical V14/E21/C42/L15/F12 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==14&&S.Payload.Edges.size()==21&&S.Payload.Coedges.size()==42&&S.Payload.Loops.size()==15&&S.Payload.Faces.size()==12);
    P.Expect("Three exact floors and three fitted entrance rims identify the source",SE.Exact==3&&SE.Fitted==3);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,1.5,&Applied);
    P.Expect("All three separated cavities commit with the four outer rolls",R&&Applied==4);
    auto Q=R?Inspect(R.Payload,Three):Inspection{};
    P.Expect("Three-cavity output reaches exact V22/E33/C66/L19/F16 topology",Q.Topology);
    P.Expect("Nine planes, four rolls, and three rational cavity cylinders remain",Q.Supports);
    P.Expect("Opposite entry caps retain the expected two-loop and three-loop profiles",Q.InnerLoops==3&&Q.EndCapLoops==std::vector<int>({2,3}));
    P.Expect("All six cavity rims are exact rational circles",Q.Circles);
    P.Expect("All three radii, offsets, entry directions, and depths remain exact",Q.Dimensions);
    P.Within("Three-cavity volume follows the exact finite-cylinder formula",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Three)):1e9,.5);
    P.Expect("Rounded bounds and the sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==12);

    P.Section("Eight-cavity cap and pairwise finite clearance");
    const std::vector<Cavity> Eight=EightCavities();auto M=Fixture(Eight);auto ME=SourceEvidence(M.Payload);auto MRails=Rails(M.Payload);
    auto MSource=M.Payload.Validate();
    P.Expect("Eight-cavity source reaches canonical V24/E36/C72/L30/F22 topology",M&&MSource.Solid()&&MSource.Hulls==1&&MSource.Genus==0&&
        M.Payload.Vertices.size()==24&&M.Payload.Edges.size()==36&&M.Payload.Coedges.size()==72&&M.Payload.Loops.size()==30&&M.Payload.Faces.size()==22);
    P.Expect("Eight exact floors and eight fitted entrances survive source construction",ME.Exact==8&&ME.Fitted==8);
    int MA=-1;auto Multi=BlendSolver::FilletEdges(M.Payload,MRails,1.5,&MA);auto MQ=Multi?Inspect(Multi.Payload,Eight):Inspection{};
    P.Expect("The bounded eight-cavity maximum commits all four outer rolls",Multi&&MA==4);
    P.Expect("Eight-cavity output reaches exact V32/E48/C96/L34/F26 topology",MQ.Topology);
    P.Expect("Fourteen planes, four rolls, and eight rational cavity cylinders remain",MQ.Supports);
    P.Expect("All sixteen floor and entrance rims are exact rational circles",MQ.Circles);
    P.Expect("All eight finite depths and transverse profiles remain unchanged",MQ.Dimensions);
    P.Expect("Four entries on each end produce two five-loop end caps",MQ.InnerLoops==8&&MQ.EndCapLoops==std::vector<int>({5,5}));
    P.Within("Eight-cavity volume follows all eight exact removals",Multi?std::fabs(Multi.Payload.Validate().Volume-ExactVolume(Eight)):1e9,.5);
    double Minimum=std::numeric_limits<double>::max();int Pairs=0;
    for(size_t I=0;I<Eight.size();++I)for(size_t J=I+1;J<Eight.size();++J){Minimum=std::min(Minimum,PairDistance(Eight[I],Eight[J]));++Pairs;}
    P.Expect("All 28 finite-cylinder pairs retain positive radial, axial, or combined clearance",Pairs==28&&Minimum>ScalarCriteria::MergeTolerance);

    P.Section("Order, transforms, bounded refusal, and prior routes");
    std::vector<int> Reordered{MRails[3],MRails[0],MRails[2],MRails[1],MRails[3]};int RA=-1;
    auto RR=BlendSolver::FilletEdges(M.Payload,Reordered,1.5,&RA);
    P.Expect("Repeated reordered seeds preserve deterministic multi-cavity output",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-Multi.Payload.Validate().Volume)<1e-9);
    Mat4 Transform=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=M.Payload.Transformed(Transform);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,MRails,1.5,&TA);
    P.Expect("Rigid transforms preserve the eight-cavity classifier",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==26);
    P.Within("Rigid transforms preserve multi-cavity volume",Multi&&TR?std::fabs(Multi.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto WallSet=Eight;WallSet.front()={true,.75,.75,.6,5};auto Wall=Fixture(WallSet);int Refused=99;
    auto WR=BlendSolver::FilletEdges(Wall.Payload,Rails(Wall.Payload),1.5,&Refused);
    P.Expect("One wall-crossing cavity rejects the entire eight-cavity transaction",Wall&&!WR&&Refused==0);
    auto Nine=Fixture(NineCavities());Refused=99;auto NineR=BlendSolver::FilletEdges(Nine.Payload,Rails(Nine.Payload),1.5,&Refused);
    P.Expect("Nine blind cavities remain outside the explicit bounded route",Nine&&Nine.Payload.Validate().Solid()&&!NineR&&Refused==0);
    auto Single=Fixture({Three[0]});int SA=-1;auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),1.5,&SA);
    P.Expect("The established single-cavity route remains intact",SingleR&&SA==4&&SingleR.Payload.Faces.size()==12);
    auto Dual=Fixture({Three[0],Three[2]});int DA=-1;auto DualR=BlendSolver::FilletEdges(Dual.Payload,Rails(Dual.Payload),1.5,&DA);
    P.Expect("The established dual-cavity route remains intact",DualR&&DA==4&&DualR.Payload.Faces.size()==14);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("MultiBlind",M.Payload);F.Matcap=6;
    std::string List;for(int I:MRails){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet MultiBlind 1.5 --edges="+List+" --name=MultiBlindRound");auto* Output=H.Document().Find("MultiBlindRound");
    P.Expect("Console fillet commits one exact eight-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==26);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32n_MultiBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("ThreeSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("ThreeRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("ThreeEntries",R.Payload,3)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("EightRound",Multi.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("EightEntries",Multi.Payload,5)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32n_MultiBlindBorePrism");
    P.Expect("C++ multi-blind-bore proof commands complete",Rendered);
    P.Expect("C++ multi-blind-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
