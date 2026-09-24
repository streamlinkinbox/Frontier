// Phase 32r — preserve one exact orthogonal blind cavity entering a retained planar side of a rounded prism.
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
struct SideCavity { int Along=1;bool High=false;double X=10,Cross=6,Radius=1.25,Depth=8; };
Deliver<BrepBody> Fixture(const SideCavity& C)
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});if(!Box)return Box;
    Vec3 Origin,Axis;
    if(C.Along==1){Origin=C.High?Vec3{C.X,18,C.Cross}:Vec3{C.X,-2,C.Cross};Axis=C.High?Vec3{0,-1,0}:Vec3{0,1,0};}
    else{Origin=C.High?Vec3{C.X,C.Cross,14}:Vec3{C.X,C.Cross,-2};Axis=C.High?Vec3{0,0,-1}:Vec3{0,0,1};}
    auto Cutter=BrepBody::Cylinder(Origin,Axis,C.Radius,C.Depth+2);if(!Cutter)return Cutter;
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> ThroughFixture(int Along)
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});
    auto Cutter=Along==1?BrepBody::Cylinder({10,-2,6},{0,1,0},1,20):BrepBody::Cylinder({10,8,-2},{0,0,1},1,16);
    if(!Box||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> DualSideFixture()
{
    auto Working=Fixture({1,false,6,4,.7,8});if(!Working)return Working;
    auto Cutter=BrepBody::Cylinder({14,-2,8},{0,1,0},.7,11);if(!Cutter)return Cutter;
    return IntersectionSolver::Combine(Working.Payload,Cutter.Payload,BodyOperation::Subtract);
}
Deliver<BrepBody> ObliqueSideFixture()
{
    auto Box=BrepBody::Box({0,0,0},{20,16,12});auto Cutter=BrepBody::Cylinder({10,-2,5},{0,1,.1},.7,10);
    if(!Box||!Cutter)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"fixture");
    return IntersectionSolver::Combine(Box.Payload,Cutter.Payload,BodyOperation::Subtract);
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
struct Inspection{bool Topology=false,Supports=false,Cap=false,Circles=false,Dimensions=false;};
Inspection Inspect(const BrepBody& B,const SideCavity& C)
{
    Inspection Q;auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==18&&B.Edges.size()==27&&
        B.Coedges.size()==54&&B.Loops.size()==13&&B.Faces.size()==12;
    int Planes=0,Rolls=0,CavityCylinders=0,Annular=0,ExactCircles=0;
    Vec3 ExpectedAxis=C.Along==1?Vec3{0,1,0}:Vec3{0,0,1};
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& S=F.Surface;
        if(S.Classification==SurfaceClassification::Plane){++Planes;Annular+=F.Loops.size()==2;}
        if(S.Classification==SurfaceClassification::Cylinder&&S.Rational())
        {
            Rolls+=std::fabs(S.RadiusMajor-2)<1e-9;
            CavityCylinders+=std::fabs(S.RadiusMajor-C.Radius)<1e-9&&std::fabs(std::fabs(S.Axis.Normalised().Dot(ExpectedAxis))-1)<1e-8;
        }
    }
    for(const BrepEdge& E:B.Edges)ExactCircles+=E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational();
    double Length=C.Along==1?16:12,Entry=C.High?Length:0,Floor=C.High?Length-C.Depth:C.Depth;int Rims=0;
    Vec3 EntryCentre=C.Along==1?Vec3{C.X,Entry,C.Cross}:Vec3{C.X,C.Cross,Entry};
    Vec3 FloorCentre=C.Along==1?Vec3{C.X,Floor,C.Cross}:Vec3{C.X,C.Cross,Floor};
    for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
        Rims+=(E.Curve.Centre.Distance(EntryCentre)<1e-8||E.Curve.Centre.Distance(FloorCentre)<1e-8)&&
              std::fabs(E.Curve.RadiusMajor-C.Radius)<1e-9&&std::fabs(std::fabs(E.Curve.AxisZ.Normalised().Dot(ExpectedAxis))-1)<1e-8;
    Q.Supports=Planes==7&&Rolls==4&&CavityCylinders==1;Q.Cap=Annular==1;Q.Circles=ExactCircles==2;Q.Dimensions=Rims==2;
    return Q;
}
double ExactVolume(const SideCavity& C)
{
    return 20*(16*12-4*4*(1-ScalarCriteria::Pi/4))-ScalarCriteria::Pi*C.Radius*C.Radius*C.Depth;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32r · Side Blind-Bore Rounded-Prism Verification");
    const SideCavity C;auto S=Fixture(C);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Orthogonal cavity through a retained Y-side plane");
    P.Expect("Side-blind source is canonical V10/E15/C30/L9/F8 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==10&&S.Payload.Edges.size()==15&&S.Payload.Coedges.size()==30&&S.Payload.Loops.size()==9&&S.Payload.Faces.size()==8);
    int Exact=0,Fitted=0;
    for(const BrepEdge& X:S.Payload.Edges)if(X.Closed())
    {
        Exact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();
        Fitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();
    }
    P.Expect("Source contains one exact floor and one fitted side-wall entrance rim",Exact==1&&Fitted==1);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);auto Q=R?Inspect(R.Payload,C):Inspection{};
    P.Expect("The orthogonal side cavity commits with all four outer rolls",R&&Applied==4);
    P.Expect("Output reaches exact V18/E27/C54/L13/F12 genus-zero topology",Q.Topology);
    P.Expect("Seven planes, four rolls, and one rational side-cavity cylinder remain",Q.Supports);
    P.Expect("Only the retained entrance side is annular",Q.Cap);
    P.Expect("Both side-cavity rims are exact rational circles",Q.Circles);
    P.Expect("Side, axis, radius, offset, and finite depth remain unchanged",Q.Dimensions);
    P.Within("Side-cavity rounded volume follows its exact finite-cylinder removal",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(C)):1e9,.5);
    P.Expect("Outer bounds and the sharp side source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==8);

    P.Section("All retained sides, offsets, transforms, and refusal");
    SideCavity HighY=C;HighY.High=true;auto HY=Fixture(HighY);int HYA=-1;auto HYR=BlendSolver::FilletEdges(HY.Payload,Rails(HY.Payload),2,&HYA);
    P.Expect("The opposite Y side preserves the same exact cavity",HYR&&HYA==4&&Inspect(HYR.Payload,HighY).Dimensions);
    SideCavity LowZ{2,false,10,8,1,6};auto LZ=Fixture(LowZ);int LZA=-1;auto LZR=BlendSolver::FilletEdges(LZ.Payload,Rails(LZ.Payload),2,&LZA);
    P.Expect("A cavity entering the low Z side is preserved exactly",LZR&&LZA==4&&Inspect(LZR.Payload,LowZ).Dimensions);
    SideCavity HighZ=LowZ;HighZ.High=true;auto HZ=Fixture(HighZ);int HZA=-1;auto HZR=BlendSolver::FilletEdges(HZ.Payload,Rails(HZ.Payload),2,&HZA);
    P.Expect("A cavity entering the high Z side is preserved exactly",HZR&&HZA==4&&Inspect(HZR.Payload,HighZ).Dimensions);
    SideCavity Offset{1,false,6,5,1,9};auto OS=Fixture(Offset);int OA=-1;auto OR=BlendSolver::FilletEdges(OS.Payload,Rails(OS.Payload),2,&OA);
    P.Expect("Safe axial and wall-strip offsets preserve the requested side depth",OR&&OA==4&&Inspect(OR.Payload,Offset).Dimensions);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=S.Payload.Transformed(M);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve orthogonal side-cavity classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==12);
    P.Within("Rigid transforms preserve side-cavity volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    SideCavity Corner{1,false,10,2.5,1,8};auto CornerSource=Fixture(Corner);int Refused=99;
    auto CornerR=BlendSolver::FilletEdges(CornerSource.Payload,Rails(CornerSource.Payload),2,&Refused);
    P.Expect("A side entrance crossing a rounded corner strip refuses transactionally",CornerSource&&!CornerR&&Refused==0);
    SideCavity EndTouch{1,false,1.000000005,6,1,8};auto EndSource=Fixture(EndTouch);int EndApplied=99;bool EndRefusal=false;
    if(EndSource){auto EndR=BlendSolver::FilletEdges(EndSource.Payload,Rails(EndSource.Payload),2,&EndApplied);EndRefusal=!EndR&&EndApplied==0;}
    P.Expect("A side entrance touching a selected-axis end cap refuses transactionally",EndSource&&EndSource.Payload.Validate().Solid()&&EndRefusal);
    auto Oblique=ObliqueSideFixture();int ObliqueApplied=99;bool ObliqueRefusal=false;
    if(Oblique){auto ObliqueR=BlendSolver::FilletEdges(Oblique.Payload,Rails(Oblique.Payload),2,&ObliqueApplied);ObliqueRefusal=!ObliqueR&&ObliqueApplied==0;}
    P.Expect("An oblique side-entering blind cavity remains unsupported",Oblique&&Oblique.Payload.Validate().Solid()&&ObliqueRefusal);
    auto Through=ThroughFixture(1);Refused=99;auto ThroughR=BlendSolver::FilletEdges(Through.Payload,Rails(Through.Payload),2,&Refused);
    P.Expect("An orthogonal side through-hole remains outside the blind route",Through&&Through.Payload.Validate().Solid()&&!ThroughR&&Refused==0);
    auto Dual=DualSideFixture();int DualApplied=-1;auto DualR=BlendSolver::FilletEdges(Dual.Payload,Rails(Dual.Payload),2,&DualApplied);
    P.Expect("Two parallel side cavities delegate to the bounded pair route",Dual&&Dual.Payload.Validate().Solid()&&DualR&&DualApplied==4&&DualR.Payload.Faces.size()==14);
    auto AxialBox=BrepBody::Box({0,0,0},{20,16,12});auto AxialCut=BrepBody::Cylinder({22,8,6},{-1,0,0},1,10);
    auto Axial=IntersectionSolver::Combine(AxialBox.Payload,AxialCut.Payload,BodyOperation::Subtract);int AA=-1;
    auto AxialR=BlendSolver::FilletEdges(Axial.Payload,Rails(Axial.Payload),2,&AA);
    P.Expect("The established selected-axis blind route remains intact",AxialR&&AA==4&&AxialR.Payload.Faces.size()==12);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("SideBlind",S.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet SideBlind 2 --edges="+List+" --name=SideBlindRound");auto* Output=H.Document().Find("SideBlindRound");
    P.Expect("Console fillet commits one exact side-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==12);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32r_SideBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("SideSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("SideRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("YEntrance",R.Payload,3)&&V.Execute("view front")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("ZEntrance",HZR.Payload,2)&&V.Execute("view top")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("SideOffset",OR.Payload,5)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32r_SideBlindBorePrism");
    P.Expect("C++ side-blind-bore proof commands complete",Rendered);
    P.Expect("C++ side-blind-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
