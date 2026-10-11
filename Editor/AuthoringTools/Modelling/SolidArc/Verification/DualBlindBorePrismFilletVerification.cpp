// Phase 32m — preserve exactly two separated axis-parallel blind cylindrical cavities while rounding a complete prism family.
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
struct Inspection
{
    bool Topology=false,Supports=false,Caps=false,Circles=false,Dimensions=false;
    int AnnularCaps=0,TripleCaps=0,InnerLoops=0;
};
Inspection Inspect(const BrepBody& B,const std::vector<Cavity>& Cavities)
{
    Inspection Q;auto Report=B.Validate();
    Q.Topology=Report.Solid()&&Report.Hulls==1&&Report.Genus==0&&B.Vertices.size()==20&&B.Edges.size()==30&&
        B.Coedges.size()==60&&B.Loops.size()==16&&B.Faces.size()==14;
    int Planes=0,Rolls=0,CavitySurfaces=0,Closed=0;
    for(const BrepFace& F:B.Faces)
    {
        const NurbsSurface& S=F.Surface;
        if(S.Classification==SurfaceClassification::Plane)
        {
            ++Planes;Q.AnnularCaps+=F.Loops.size()==2;Q.TripleCaps+=F.Loops.size()==3;
            if(F.Loops.size()>1)Q.InnerLoops+=static_cast<int>(F.Loops.size()-1);
        }
        if(S.Classification==SurfaceClassification::Cylinder&&S.Rational())
        {
            Rolls+=std::fabs(S.RadiusMajor-2)<1e-9;
            for(const Cavity& C:Cavities)
                if(std::fabs(S.RadiusMajor-C.Radius)<1e-9&&std::fabs(S.Origin.Y-C.Y)<1e-8&&std::fabs(S.Origin.Z-C.Z)<1e-8)
                    ++CavitySurfaces;
        }
    }
    Q.Dimensions=true;
    for(const BrepEdge& E:B.Edges)if(E.Closed())
    {
        ++Closed;
        Q.Circles|=E.Curve.Classification!=CurveClassification::Circle||!E.Curve.Rational();
    }
    for(const Cavity& C:Cavities)
    {
        int Rims=0;
        const double Entry=C.High?20:0,Floor=C.High?20-C.Depth:C.Depth;
        for(const BrepEdge& E:B.Edges)if(E.Closed()&&E.Curve.Classification==CurveClassification::Circle&&E.Curve.Rational())
        {
            Vec3 Sample=E.Curve.Sample(E.Curve.DomainStart());
            double Radial=std::hypot(Sample.Y-C.Y,Sample.Z-C.Z);
            if(std::fabs(Radial-C.Radius)<1e-7&&(std::fabs(Sample.X-Entry)<1e-7||std::fabs(Sample.X-Floor)<1e-7))++Rims;
        }
        Q.Dimensions&=Rims==2;
    }
    Q.Circles=!Q.Circles&&Closed==4;
    Q.Supports=Planes==8&&Rolls==4&&CavitySurfaces==2;
    Q.Caps=Q.InnerLoops==2;
    return Q;
}
double ExactVolume(const std::vector<Cavity>& Cavities)
{
    double Volume=20*(16*12-4*4*(1-ScalarCriteria::Pi/4));
    for(const Cavity& C:Cavities)Volume-=ScalarCriteria::Pi*C.Radius*C.Radius*C.Depth;
    return Volume;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 32m · Dual Blind-Bore Rounded-Prism Verification");
    const std::vector<Cavity> Same{{true,5,4,1,12},{true,11,8,1.5,10}};
    auto S=Fixture(Same);auto E=Rails(S.Payload);auto Source=S.Payload.Validate();
    P.Section("Two cavities entering one end");
    P.Expect("Dual-blind source is canonical V12/E18/C36/L12/F10 genus-zero topology",S&&Source.Solid()&&Source.Hulls==1&&Source.Genus==0&&
        S.Payload.Vertices.size()==12&&S.Payload.Edges.size()==18&&S.Payload.Coedges.size()==36&&S.Payload.Loops.size()==12&&S.Payload.Faces.size()==10);
    int SourceExact=0,SourceFitted=0;
    for(const BrepEdge& X:S.Payload.Edges)if(X.Closed())
    {
        SourceExact+=X.Curve.Classification==CurveClassification::Circle&&X.Curve.Rational();
        SourceFitted+=X.Curve.Classification==CurveClassification::Freeform&&!X.Curve.Rational();
    }
    P.Expect("Source evidence has two exact floor rims and two fitted Boolean entrance rims",SourceExact==2&&SourceFitted==2);
    P.Expect("Exactly four outer axial rails remain selectable",E.size()==4);
    int Applied=-1;auto R=BlendSolver::FilletEdges(S.Payload,E,2,&Applied);
    P.Expect("Both separated blind cavities commit with all four outer rolls",R&&Applied==4);
    auto Q=R?Inspect(R.Payload,Same):Inspection{};
    P.Expect("Output reaches exact V20/E30/C60/L16/F14 genus-zero topology",Q.Topology);
    P.Expect("Eight planes, four rolls, and two rational cavity cylinders remain",Q.Supports);
    P.Expect("One entrance cap carries exactly two inner cavity loops",Q.Caps&&Q.TripleCaps==1&&Q.AnnularCaps==0);
    P.Expect("All four cavity rims are exact rational circles",Q.Circles);
    P.Expect("Both radii, offsets, entry directions, and depths remain unchanged",Q.Dimensions);
    P.Within("Same-end rounded volume follows the exact two-cavity formula",R?std::fabs(R.Payload.Validate().Volume-ExactVolume(Same)):1e9,.5);
    P.Expect("Outer bounds and sharp source remain unchanged",R&&R.Payload.Bounds().Low.Distance(S.Payload.Bounds().Low)<1e-9&&
        R.Payload.Bounds().High.Distance(S.Payload.Bounds().High)<1e-9&&S.Payload.Faces.size()==10);

    P.Section("Opposite ends, finite clearance, transforms, and refusal");
    const std::vector<Cavity> Opposite{{false,5,4,1,10},{true,11,8,1.5,9}};
    auto O=Fixture(Opposite);int OA=-1;auto OR=BlendSolver::FilletEdges(O.Payload,Rails(O.Payload),2,&OA);
    auto OQ=OR?Inspect(OR.Payload,Opposite):Inspection{};
    P.Expect("Separated cavities entering opposite ends use the same exact route",OR&&OA==4&&OQ.Topology&&OQ.Dimensions);
    P.Expect("Opposite entry ends retain two separately annular caps",OQ.Caps&&OQ.AnnularCaps==2&&OQ.TripleCaps==0);
    P.Within("Opposite-end rounded volume follows both finite depths",OR?std::fabs(OR.Payload.Validate().Volume-ExactVolume(Opposite)):1e9,.5);
    const std::vector<Cavity> Coaxial{{false,8,6,1,7},{true,8,6,1,7}};
    auto C=Fixture(Coaxial);int CA=-1;auto CR=BlendSolver::FilletEdges(C.Payload,Rails(C.Payload),2,&CA);
    P.Expect("Coaxial opposite-end cavities are supported when an axial ligament remains",CR&&CA==4&&Inspect(CR.Payload,Coaxial).Dimensions);
    std::vector<double> CoaxialX;
    if(CR)for(const BrepEdge& X:CR.Payload.Edges)if(X.Closed())CoaxialX.push_back(X.Curve.Sample(X.Curve.DomainStart()).X);
    std::sort(CoaxialX.begin(),CoaxialX.end());
    P.Expect("Coaxial entrance and floor rims preserve the positive axial ligament",CoaxialX.size()==4&&std::fabs(CoaxialX[1]-7)<1e-7&&std::fabs(CoaxialX[2]-13)<1e-7);
    std::vector<int> Reordered{E[3],E[0],E[2],E[1],E[3]};int RA=-1;
    auto RR=BlendSolver::FilletEdges(S.Payload,Reordered,2,&RA);
    P.Expect("Repeated reordered rail seeds remain deterministic",RR&&RA==4&&std::fabs(RR.Payload.Validate().Volume-R.Payload.Validate().Volume)<1e-9);
    Mat4 M=Mat4::Translation({3,-4,6})*Mat4::Rotation({1,3,2},.59);BrepBody T=S.Payload.Transformed(M);int TA=-1;
    auto TR=BlendSolver::FilletEdges(T,E,2,&TA);
    P.Expect("Rigid transforms preserve dual-cavity classification",TR&&TA==4&&TR.Payload.Validate().Genus==0&&TR.Payload.Faces.size()==14);
    P.Within("Rigid transforms preserve the dual-cavity volume",R&&TR?std::fabs(R.Payload.Validate().Volume-TR.Payload.Validate().Volume):1e9,.5);
    auto Wall=Fixture({Same[0],{true,1.2,1.2,1,9}});int Refused=99;
    auto WR=BlendSolver::FilletEdges(Wall.Payload,Rails(Wall.Payload),2,&Refused);
    P.Expect("Either cavity crossing a rounded wall refuses transactionally",Wall&&!WR&&Refused==0);
    auto Crossing=Fixture({{true,7,6,2,10},{true,9,6,2,8}});
    P.Expect("Intersecting finite cavity construction is rejected before blend dispatch",!Crossing);
    auto Three=Fixture({{true,4,4,.75,8},{true,8,8,.75,9},{true,12,4,.75,10}});int ThreeApplied=-1;
    auto ThreeR=BlendSolver::FilletEdges(Three.Payload,Rails(Three.Payload),2,&ThreeApplied);
    P.Expect("Three separated blind cavities delegate to the bounded extension",ThreeR&&ThreeApplied==4&&ThreeR.Payload.Faces.size()==16);
    auto Single=Fixture({Same[0]});int SingleApplied=-1;
    auto SingleR=BlendSolver::FilletEdges(Single.Payload,Rails(Single.Payload),2,&SingleApplied);
    P.Expect("The established single-cavity route remains intact",SingleR&&SingleApplied==4&&SingleR.Payload.Faces.size()==12);

    P.Section("Console and deterministic proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error proof folder required
#endif
    ConsoleHost H(SOLIDARC_PROOF_FOLDER,1280,800);auto& F=H.Document().AddBody("DualBlind",S.Payload);F.Matcap=6;
    std::string List;for(int I:E){if(!List.empty())List+=",";List+=std::to_string(I);}
    bool OK=H.Execute("fillet DualBlind 2 --edges="+List+" --name=DualBlindRound");auto* Output=H.Document().Find("DualBlindRound");
    P.Expect("Console fillet commits one exact dual-blind-cavity solid",OK&&Output&&Output->Body.Validate().Genus==0&&Output->Body.Faces.size()==14);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase32m_DualBlindBorePrism.png";
    std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost V(SOLIDARC_PROOF_FOLDER,1280,800);
    auto Reset=[&](){return V.Execute("reset")&&V.Execute("gizmo off");};
    auto Add=[&](const char* Name,BrepBody B,uint8_t Colour){auto& G=V.Document().AddBody(Name,std::move(B));G.Matcap=Colour;return true;};
    bool Rendered=Reset()&&Add("DualSharp",S.Payload.Transformed(Mat4::Translation({-24,0,0})),6)&&
        Add("DualRound",R.Payload.Transformed(Mat4::Translation({8,0,0})),3)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 0")&&
        Reset()&&Add("SameEnd",R.Payload,3)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 1")&&
        Reset()&&Add("OppositeEnds",OR.Payload,2)&&V.Execute("view iso")&&V.Execute("view fit")&&V.Execute("render sheet 2")&&
        Reset()&&Add("CoaxialLigament",CR.Payload,5)&&V.Execute("view right")&&V.Execute("view fit")&&V.Execute("render sheet 3")&&
        V.Execute("render sheet finalize Phase32m_DualBlindBorePrism");
    P.Expect("C++ dual-blind-bore proof commands complete",Rendered);
    P.Expect("C++ dual-blind-bore proof is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
