#include "UI/SolidArc/Viewport/SolidArcSurfaceTracing.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Interface/SolidArcOptions.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Frontier;
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;
namespace {
int Checks = 0;
void Check(bool OK, const std::string& What) { if (!OK) throw std::runtime_error(What); ++Checks; std::cout << "PASS " << What << std::endl; }
SolidArcDocument Restore(SolidArcDocumentFile File) {
    SolidArcDocument D; D.Tree = std::move(File.Tree); D.Title = File.Title; D.Captions = File.Captions; D.Appearance = File.Appearance;
    RegenerateSolidArcDocument(D); return D;
}
}
int main(int argc, char** argv) {
try {
    SolidArcDocument D;
    SolidArcConstructRequest Request; Request.Kind = SolidArcConstructKind::Sphere; Request.Radius = 1;
    Check(ConstructSolidArcPrimitive(D, Request).AcceptedCondition, "spherical reference constructed");
    for (const auto& B : D.Regeneration.Bodies) D.Appearance.LockedFigures.insert(B.Key);
    const std::string Reference = EncodeSolidArcDocument(D);
    auto Scene = BuildSolidArcScene(D);
    auto Catalogue = CatalogueSolidArcSnaps(D, Scene);
    OrbitCamera Camera; Camera.Target = {0,0,0}; Camera.Distance = 5; Camera.SceneRadius = 2; Camera.OrthographicEnabled = true;
    PresentOrbitView(Camera, OrbitViewPreset::Top);
    const auto Frame = FrameOrbitCamera(Camera, 1000, 800);
    auto Hit = [&](double X, double Y, bool Ctrl = false) {
        double PX, PY; if (!ProjectSolidArcPoint(Frame, {X,Y,1}, PX, PY)) throw std::runtime_error("fixture projection");
        return SnapSolidArcTrace(Catalogue, D, Scene, Frame, PX, PY, Ctrl);
    };
    const auto A = Hit(-0.55,-0.15), B = Hit(0,0.25), C = Hit(0.55,-0.15);
    Check(A.Kind == SolidArcSnapKind::Face && B.Kind == SolidArcSnapKind::Face && C.Kind == SolidArcSnapKind::Face, "locked body still offers surface snaps without modifiers");
    Check(std::abs(Length(A.Position) - 1.0) < 1.0e-9, "snap refined onto exact sphere, not faceted triangle");
    Check(Hit(1.4,1.4).Kind == SolidArcSnapKind::None, "surface miss has no grid or sketch-plane fallback");
    Check(Hit(0.12,0.2,true).Kind == SolidArcSnapKind::Face, "Ctrl falls back to surface when no edge is within reach");
    for (const auto Form : {SketchSplineForm::Hermite, SketchSplineForm::Bezier, SketchSplineForm::Nurbs, SketchSplineForm::CubicSpline}) {
        auto Trace = ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,B.Position,C.Position},SolidArcDrawingKind::Curve,Form,false,true);
        Check(Trace.Refusal.empty(), std::string("project / fit ") + SketchSplineFormLabel(Form) + ": " + Trace.Refusal);
        double Error = 0;
        for (int I=0; I<=4096; ++I) Error = std::max(Error, std::abs(Length(EvaluateNurbsCurve(*Trace.Fit.Curve.Definition,double(I)/4096).Position)-1.0));
        Check(Error <= 0.0002, "independent 4097-point sphere conformance < 0.2 mm");
    }
    auto Trace = ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,B.Position,C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,false,true);
    auto Arc = ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,C.Position,B.Position},SolidArcDrawingKind::Arc,SketchSplineForm::CubicSpline,false,true);
    Check(Arc.Refusal.empty(), "3D three-point arc seed projects to editable surface NURBS: " + Arc.Refusal);
    auto TwoClosed=ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,true,true);
    Check(!TwoClosed.Refusal.empty(),"two-control degenerate closed curve refused");
    auto BadArc = ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,A.Position,C.Position},SolidArcDrawingKind::Arc,SketchSplineForm::CubicSpline,false,true);
    Check(!BadArc.Refusal.empty(), "degenerate arc refused");
    auto Gap = ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A.Position,{1.3,0.2,0},C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,false,true);
    Check(!Gap.Refusal.empty(), "curve crossing silhouette refused rather than bridging empty space");
    auto OppositeCamera=Camera; PresentOrbitView(OppositeCamera,OrbitViewPreset::Bottom);
    const auto Hidden=ProjectSolidArcTrace(Catalogue,D,Scene,FrameOrbitCamera(OppositeCamera,1000,800),{A.Position,B.Position,C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,false,true);
    Check(!Hidden.Refusal.empty(),"orbiting to the opposite side refuses hidden controls instead of moving them onto the front");
    Check(EncodeSolidArcDocument(D) == Reference, "projection / refusals never mutate reference geometry");
    const auto BeforeUndo = D.UndoEntries.size();
    Check(DrawSolidArcProjectedCurve(D,Trace.Fit.Curve,false).AcceptedCondition, "commit exact fitted poles and knots");
    Check(D.UndoEntries.size() == BeforeUndo+1 && D.Regeneration.Curves.size()==1, "one native SpaceCurve and one undo entry");
    const auto Label = D.Selection.Curves.front();
    const auto Bytes = EncodeSolidArcDocument(D);
    auto File = DecodeSolidArcDocument(Bytes);
    Check(File.Refusal.empty(), "native save decoder accepts surface curve");
    auto Roundtrip = Restore(std::move(File));
    Check(Roundtrip.Regeneration.Refused==0 && EncodeSolidArcDocument(Roundtrip)==Bytes, "save / load / regenerate exact byte roundtrip");
    Check(UndoSolidArcCommand(D) && D.Regeneration.Curves.empty(), "surface trace undo removes only the new curve");
    Check(RedoSolidArcCommand(D) && D.Regeneration.Curves.size()==1, "surface trace redo restores native curve");
    const auto Pole = Trace.Fit.Curve.Definition->Poles.front();
    Check(MoveSolidArcPoles(D,Label,{0},{Pole+Vector3{0.01,0.02,0.03}}).AcceptedCondition, "curve pole editable in all XYZ coordinates");
    const auto& Step = D.Tree.Steps.back();
    Check(Distance(Step.PoleEdits.front().Position,Pole+Vector3{0.01,0.02,0.03})<1e-12, "XYZ pole target persisted exactly");
    Check(UndoSolidArcCommand(D), "undo free pole move before loft");
    const auto A2=Hit(-0.55,-0.4), B2=Hit(0,-0.1), C2=Hit(0.55,-0.4);
    auto Section2=ProjectSolidArcTrace(Catalogue,D,Scene,Frame,{A2.Position,B2.Position,C2.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,false,true);
    Check(Section2.Refusal.empty() && DrawSolidArcProjectedCurve(D,Section2.Fit.Curve,false).AcceptedCondition,"second traced section remains an editable native curve");
    const auto Label2=D.Selection.Curves.front(); D.Selection.Curves={Label,Label2};
    Check(LoftSolidArcSelection(D).AcceptedCondition && D.Regeneration.Bodies.size()==2 && D.Regeneration.Refused==0,"two surface-traced curves loft into a separate native sheet");
    // A planar line endpoint previously could not leave its plane.
    SolidArcDocument LineDoc; SolidArcDrawingRequest Line; Line.Kind=SolidArcDrawingKind::Line; Line.Points={{0,0},{1,0}};
    Check(DrawSolidArcSketch(LineDoc,Line).AcceptedCondition, "planar line fixture");
    const auto LineLabel=LineDoc.Tree.Steps.back().Label;
    const auto LineCurve=LineDoc.Tree.Steps.back().Sketch.Curves.front();
    Check(LiftSolidArcSketchPoints(LineDoc,LineLabel,SolidArcSketchPlane(0),{LineCurve.End},{{1,0,0.3}}).AcceptedCondition, "line endpoint lifts off-plane into a native 3D curve");
    Check(LineDoc.Regeneration.Refused==0 && LineDoc.Regeneration.Curves.size()==1, "lifted line regenerates without planar constraint");
    // Box edge vs a nearby user curve: Ctrl must choose the reference's edge.
    SolidArcDocument Box; Request={}; Request.Width=2;Request.Depth=2;Request.Height=1;
    Check(ConstructSolidArcPrimitive(Box,Request).AcceptedCondition,"edge reference fixture");
    auto BoxScene=BuildSolidArcScene(Box); auto BoxCatalogue=CatalogueSolidArcSnaps(Box,BoxScene);
    Camera.Target={1,1,0}; const auto BoxFrame=FrameOrbitCamera(Camera,1000,800);
    double PX,PY; Check(ProjectSolidArcPoint(BoxFrame,{0.7,0,1},PX,PY),"edge fixture projection");
    const auto Edge=SnapSolidArcTrace(BoxCatalogue,Box,BoxScene,BoxFrame,PX,PY+2,true);
    Check(Edge.Kind==SolidArcSnapKind::Curve && std::abs(Edge.Position.Y)<1e-9,"Ctrl selects exact nearest visible body edge, not endpoints or centres");
    if (argc>1) {
        const auto File=LoadSolidArcDocumentFile(argv[1]); Check(File.Refusal.empty(),"Liger workspace native decoder");
        auto Liger=Restore(File); Check(Liger.Regeneration.Refused==0 && Liger.Regeneration.Bodies.size()==3,"Liger three reference sheets regenerate");
        Check(Liger.Appearance.LockedFigures.size()==3,"all three reference bodies are locked");
        auto LS=BuildSolidArcScene(Liger); auto LC=CatalogueSolidArcSnaps(Liger,LS);
        Camera={}; Camera.OrthographicEnabled=true; PresentOrbitView(Camera,OrbitViewPreset::Top); FitOrbitCamera(Camera,LS.BoundsLow,LS.BoundsHigh);
        const auto LF=FrameOrbitCamera(Camera,1400,1000);
        bool EdgeFound=false;
        for(const auto& EdgeCurve:LC.Curves) {
            if(EdgeCurve.Body.empty() || EdgeCurve.Points.empty()) continue;
            const auto Point=EdgeCurve.Points[EdgeCurve.Points.size()/2];
            if(Point.Z<0.8) continue;
            double X,Y; if(!ProjectSolidArcPoint(LF,Point,X,Y)) continue;
            const auto EH=SnapSolidArcTrace(LC,Liger,LS,LF,X,Y,true);
            if(EH.Kind!=SolidArcSnapKind::Curve || Distance(EH.Position,Point)>1e-5) continue;
            std::cout<<"LIGER_EDGE "<<EH.Position.X<<" "<<EH.Position.Y<<" "<<EH.Position.Z<<std::endl;
            EdgeFound=true; break;
        }
        Check(EdgeFound,"Ctrl snaps to an exact visible edge of the dense Liger reference");
        bool Made=false;
        for(int Y=350; Y<=650 && !Made; Y+=25) for(int X=450; X<=950 && !Made; X+=25) {
            const auto H1=SnapSolidArcTrace(LC,Liger,LS,LF,X,Y,false), H2=SnapSolidArcTrace(LC,Liger,LS,LF,X+12,Y+8,false), H3=SnapSolidArcTrace(LC,Liger,LS,LF,X+24,Y,false);
            if(H1.Kind==SolidArcSnapKind::None||H2.Kind==SolidArcSnapKind::None||H3.Kind==SolidArcSnapKind::None) continue;
            auto LT=ProjectSolidArcTrace(LC,Liger,LS,LF,{H1.Position,H2.Position,H3.Position},SolidArcDrawingKind::Curve,SketchSplineForm::CubicSpline,false,true);
            if(!LT.Refusal.empty()) continue;
            std::cout<<"LIGER_TRACE_CONTROLS "<<H1.Position.X<<" "<<H1.Position.Y<<" "<<H1.Position.Z<<"; "<<H2.Position.X<<" "<<H2.Position.Y<<" "<<H2.Position.Z<<"; "<<H3.Position.X<<" "<<H3.Position.Y<<" "<<H3.Position.Z<<" deviation_mm="<<LT.WorstSurfaceDeviation*1000<<std::endl;
            Made=DrawSolidArcProjectedCurve(Liger,LT.Fit.Curve,false).AcceptedCondition;
        }
        Check(Made && Liger.Regeneration.Curves.size()==1,"real dense Liger surface trace projects, fits, checks and commits");
        Check(Liger.Tree.Steps[0].Exchange==File.Tree.Steps[0].Exchange && Liger.Tree.Steps[1].Exchange==File.Tree.Steps[1].Exchange && Liger.Tree.Steps[2].Exchange==File.Tree.Steps[2].Exchange,"real reference STEP carriers unchanged by drawing");
    }
    for (int Count : {2,3,4,8,16}) for (auto Form : {SketchSplineForm::Nurbs,SketchSplineForm::Bezier}) {
        std::vector<Vector3> Points; for(int I=0;I<Count;++I) Points.push_back({double(I)*0.1,std::sin(I*0.3)*0.2,0.5+I*0.01});
        auto Control=MakeSolidArcControlTrace(Points,SolidArcDrawingKind::Curve,Form,false);
        Check(Control.Refusal.empty() && Control.Curve.Definition->Poles.size()==Points.size(),"point-only curve has exactly the clicked control count");
        double Error=0; for(int I=0;I<Count;++I) Error=std::max(Error,Distance(Points[I],Control.Curve.Definition->Poles[I]));
        Check(Error==0,"point-only curve preserves every snapped XYZ exactly");
        SolidArcDocument CDoc; Check(DrawSolidArcProjectedCurve(CDoc,Control.Curve,false).AcceptedCondition,"point-only curve commits without fitting");
        auto Round=Restore(DecodeSolidArcDocument(EncodeSolidArcDocument(CDoc)));
        Check(Round.Tree.Steps.back().Points.size()==Points.size() && Round.Regeneration.Curves.begin()->second.Curve.Definition->Poles.size()==Points.size(),"save/load keeps the exact control count");
    }
    std::vector<Vector3> Loop={A.Position,B.Position,C.Position,A.Position};
    auto LoopTrace=MakeSolidArcControlTrace(Loop,SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,true);
    Check(LoopTrace.Refusal.empty() && LoopTrace.Curve.Definition->Poles.size()==4,"closed curve preserves exactly the four explicit clicks, including closing click");
    SolidArcDocument LoopDoc;
    Check(DrawSolidArcProjectedCurve(LoopDoc,LoopTrace.Curve,true).AcceptedCondition && LoopDoc.Tree.Steps.back().Points.size()==4,"closed commit inserts no extra closure controls");
    auto SavedLoop=Restore(DecodeSolidArcDocument(EncodeSolidArcDocument(LoopDoc)));
    Check(SavedLoop.Tree.Steps.back().Points.size()==4 && SavedLoop.Regeneration.Curves.begin()->second.Curve.Definition->Poles.size()==4,"closed save/load keeps only authored controls");
    Loop.pop_back();
    Check(!MakeSolidArcControlTrace(Loop,SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,true).Refusal.empty(),"closing does not silently append an unplaced point");
    auto TooMany=Loop; while(TooMany.size()<17) TooMany.push_back(TooMany.back()+Vector3{0.1,0,0});
    Check(!MakeSolidArcControlTrace(TooMany,SolidArcDrawingKind::Curve,SketchSplineForm::Bezier,false).Refusal.empty(),"over-degree Bezier refuses rather than adding or replacing controls");
    Check(!MakeSolidArcControlTrace({A.Position,A.Position},SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false).Refusal.empty(),"coincident adjacent controls refused");
    auto Plain=MakeSolidArcControlTrace({A.Position,B.Position,C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false);
    const auto Middle=EvaluateNurbsCurve(*Plain.Curve.Definition,0.5).Position;
    Check(std::abs(Length(Middle)-1.0)>0.001,"spans are NOT projected or densified to follow the sphere");
    Check(!MakeSolidArcControlTrace({A.Position,B.Position,C.Position},SolidArcDrawingKind::Curve,SketchSplineForm::Hermite,false).Refusal.empty(),"extra tangent-pole forms refused in strict point-only mode");
    auto Poly=MakeSolidArcControlTrace({A.Position,B.Position,C.Position},SolidArcDrawingKind::Line,SketchSplineForm::Nurbs,false);
    Check(Poly.Curve.Definition->Degree==1 && Poly.Curve.Definition->Poles.size()==3,"polyline preserves only its three placed vertices");
    auto CircleArc=MakeSolidArcControlTrace({A.Position,C.Position,B.Position},SolidArcDrawingKind::Arc,SketchSplineForm::Nurbs,false);
    Check(CircleArc.Refusal.empty() && CircleArc.Arc.has_value(),"three-point arc stays analytic instead of surface fitting");
    std::cout<<"PASS "<<Checks<<" surface tracing checks\n"; return 0;
} catch(const std::exception& E) {std::cerr<<"FAIL "<<E.what()<<"\n";return 1;}
}
