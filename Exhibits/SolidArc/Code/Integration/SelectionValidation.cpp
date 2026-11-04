#include "UI/SolidArc/Viewport/SolidArcSurfaceTracing.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;
namespace {
int Checks = 0;
void Check(bool Passed, const char* Message)
{
    if (!Passed) throw std::runtime_error(Message);
    ++Checks; std::cout << "PASS " << Message << std::endl;
}
std::string Add(SolidArcDocument& D, double Y)
{
    const auto C = MakeSolidArcControlTrace({{-.8,Y,0},{0,Y+.1,0},{.8,Y,0}}, SolidArcDrawingKind::Curve, SketchSplineForm::Nurbs, false);
    if (!DrawSolidArcProjectedCurve(D, C.Curve, false).AcceptedCondition) throw std::runtime_error("curve fixture");
    return D.Tree.Steps.back().Label;
}
}
int main(int argc, char** argv)
{
    try
    {
        SolidArcDocument D;
        const auto A=Add(D,-.5), B=Add(D,.5), C=Add(D,1);
        if (argc>2) std::ofstream(argv[2]) << EncodeSolidArcDocument(D);
        if (argc>3)
        {
            SolidArcDocument Cross;
            for (const auto& Points : std::vector<std::vector<Vector3>>{{{-1,0,0},{1,0,0}},{{0,-1,0},{0,1,0}}})
            {
                const auto Fit=MakeSolidArcControlTrace(Points,SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false);
                if (!DrawSolidArcProjectedCurve(Cross,Fit.Curve,false).AcceptedCondition) throw std::runtime_error("crossing selection fixture");
            }
            std::ofstream(argv[3]) << EncodeSolidArcDocument(Cross);
        }
        if (argc>4)
        {
            SolidArcDocument Sketches;
            SolidArcDrawingRequest Request;Request.Kind=SolidArcDrawingKind::Curve;Request.Form=SketchSplineForm::Nurbs;
            for (const auto& Points : std::vector<std::vector<Vector2>>{{{-1,0},{1,0}},{{0,-1},{0,1}}})
            {
                Request.Points=Points;
                if (!DrawSolidArcSketch(Sketches,Request).AcceptedCondition) throw std::runtime_error("sketch selection fixture");
            }
            std::ofstream(argv[4]) << EncodeSolidArcDocument(Sketches);
        }
        D.Selection.Curves={C,A,B};
        const auto Before=EncodeSolidArcDocument(D);
        Check(DeleteSolidArcCurves(D,D.Selection.Curves).AcceptedCondition,"batch delete accepts the live selection vector in arbitrary click order");
        Check(D.Tree.Steps.empty() && D.Regeneration.Curves.empty() && D.Selection.Curves.empty(),"all selected curves removed, not only the active curve");
        Check(UndoSolidArcCommand(D) && EncodeSolidArcDocument(D)==Before,"one undo restores the complete curve batch exactly");
        Check(RedoSolidArcCommand(D) && D.Tree.Steps.empty(),"one redo removes the complete curve batch");
        UndoSolidArcCommand(D);
        D.Appearance.LockedFigures.insert(B);
        const auto Locked=EncodeSolidArcDocument(D);
        Check(!DeleteSolidArcCurves(D,{A,B,C}).AcceptedCondition && EncodeSolidArcDocument(D)==Locked,"locked member refuses the entire batch atomically");
        D.Appearance.LockedFigures.clear();
        Check(LoftSolidArcCurves(D,{A,B},0,false).AcceptedCondition,"downstream loft fixture");
        const auto Bodies=D.Regeneration.Bodies.size();
        Check(DeleteSolidArcCurves(D,{A,B}).AcceptedCondition && D.Regeneration.Refused==0,"selected source curves remain valid inputs to unselected downstream features");
        Check(D.Regeneration.Bodies.size()==Bodies && D.Appearance.RemovedCurves.contains(A) && D.Appearance.RemovedCurves.contains(B),"retiring sources never deletes the unselected loft");
        SolidArcDocument Link;
        SolidArcConstructRequest Block; Block.Width=2;Block.Depth=2;Block.Height=1;
        Check(ConstructSolidArcPrimitive(Link,Block).AcceptedCondition,"linked-curve reference fixture");
        auto Put=[&](double X){auto Fit=MakeSolidArcControlTrace({{X,.2,1},{X,1.8,1}},SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false);if(!DrawSolidArcProjectedCurve(Link,Fit.Curve,false).AcceptedCondition)throw std::runtime_error("link curve fixture");return Link.Tree.Steps.back().Label;};
        const auto Guide=Put(.3), Target=Put(.7);
        Check(LinkSolidArcSurfaceOffset(Link,Guide,Target,Link.Regeneration.Bodies.front().Key,.2,.01).AcceptedCondition,"live guide and derived target fixture");
        Check(DeleteSolidArcCurves(Link,{Guide,Target}).AcceptedCondition,"guide plus target delete without detaching first");
        Check(Link.Regeneration.Curves.empty() && Link.Tree.Steps.size()==2 && Link.Regeneration.Bodies.size()==1,"batch retires dependents first; no phantom guide or removed reference body");
        if (argc>1)
        {
            auto File=LoadSolidArcDocumentFile(argv[1]);
            Check(File.Refusal.empty(),"supplied progress project decodes");
            SolidArcDocument Cage;Cage.Title=File.Title;Cage.Tree=File.Tree;Cage.Captions=File.Captions;Cage.Appearance=File.Appearance;
            RegenerateSolidArcDocument(Cage);
            Check(Cage.Regeneration.Curves.size()==23 && Cage.Regeneration.Bodies.size()==3,"supplied cage contains 23 curves and three reference bodies");
            for (const auto& [Name,Fit]:Cage.Regeneration.Curves) Cage.Selection.Curves.push_back(Name);
            const auto Source=EncodeSolidArcDocument(Cage);
            Check(DeleteSolidArcCurves(Cage,Cage.Selection.Curves).AcceptedCondition,"delete all 23 selected supplied-cage curves");
            Check(Cage.Tree.Steps.size()==3 && Cage.Regeneration.Curves.empty() && Cage.Regeneration.Bodies.size()==3 && Cage.Regeneration.Refused==0,"reference imports survive complete cage deletion with no refused steps");
            Check(UndoSolidArcCommand(Cage) && EncodeSolidArcDocument(Cage)==Source,"one undo restores all 23 curves, controls, pole edits and references exactly");
        }
        std::cout << "PASS " << Checks << " selection and batch deletion checks\n";
        return 0;
    }
    catch (const std::exception& E) { std::cerr << "FAIL " << E.what() << '\n'; return 1; }
}
