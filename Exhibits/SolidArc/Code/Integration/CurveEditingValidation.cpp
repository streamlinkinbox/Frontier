#include "UI/SolidArc/Viewport/SolidArcSurfaceTracing.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
using namespace Frontier;
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;
namespace {
int Checks=0;
void Check(bool B,const std::string& S){if(!B)throw std::runtime_error(S);++Checks;std::cout<<"PASS "<<S<<std::endl;}
std::string Add(SolidArcDocument& D,std::vector<Vector3> P){auto C=MakeSolidArcControlTrace(P,SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false);Check(C.Refusal.empty()&&DrawSolidArcProjectedCurve(D,C.Curve,false).AcceptedCondition,"create exact authored-control curve");return D.Tree.Steps.back().Label;}
SolidArcDocument Reload(const SolidArcDocument& D){auto F=DecodeSolidArcDocument(EncodeSolidArcDocument(D));SolidArcDocument R;R.Tree=F.Tree;R.Captions=F.Captions;R.Appearance=F.Appearance;RegenerateSolidArcDocument(R);return R;}
}
int main(int argc,char** argv){try{
    const std::vector<Vector3> P={{0,0,1},{0.2,0.4,1.1},{0.6,-0.2,0.9},{1,0,1}};
    SolidArcDocument D;auto Name=Add(D,P);
    Check(DeleteSolidArcCurvePoles(D,Name,{1}).AcceptedCondition,"delete one control, not the spline");
    Check(D.Regeneration.Curves.size()==1&&D.Tree.Steps.front().Points.size()==3,"one curve and three controls survive");
    Check(Distance(D.Tree.Steps.front().Points[0],P[0])==0&&Distance(D.Tree.Steps.front().Points[1],P[2])==0&&Distance(D.Tree.Steps.front().Points[2],P[3])==0,"unselected controls retain XYZ");
    Check(UndoSolidArcCommand(D)&&D.Tree.Steps.front().Points.size()==4,"point delete undo");
    Check(RedoSolidArcCommand(D)&&D.Tree.Steps.front().Points.size()==3,"point delete redo");
    auto Saved=Reload(D);Check(Saved.Tree.Steps.front().Points.size()==3,"point delete save/load");
    const auto Before=EncodeSolidArcDocument(D);
    Check(!DeleteSolidArcCurvePoles(D,Name,{0,1,2}).AcceptedCondition&&EncodeSolidArcDocument(D)==Before,"minimum-control refusal leaves entire object unchanged");
    Check(!DeleteSolidArcCurvePoles(D,Name,{99}).AcceptedCondition&&EncodeSolidArcDocument(D)==Before,"invalid point selection cannot delete parent");
    auto Old=*D.Regeneration.Curves.at(Name).Curve.Definition;
    Check(SubdivideSolidArcCurve(D,Name,{0}).AcceptedCondition,"subdivide a selected control span");
    auto New=*D.Regeneration.Curves.at(Name).Curve.Definition;
    Check(New.Poles.size()==Old.Poles.size()+1,"explicit subdivision adds one control");
    double Worst=0;for(int I=0;I<=1000;++I)Worst=std::max(Worst,Distance(EvaluateNurbsCurve(Old,I/1000.0).Position,EvaluateNurbsCurve(New,I/1000.0).Position));
    Check(Worst<1e-10,"exact knot insertion preserves complete curve shape");
    Check(UndoSolidArcCommand(D)&&D.Tree.Steps.front().Points.size()==3,"subdivision undo");
    Check(RedoSolidArcCommand(D)&&D.Tree.Steps.front().Points.size()==4,"subdivision redo");
    Saved=Reload(D);Check(Saved.Tree.Steps.front().Points.size()==4&&Saved.Tree.Steps.front().CurveKnots==D.Tree.Steps.front().CurveKnots,"subdivision poles/knots survive save/load");
    Check(MoveSolidArcPole(D,Name,1,D.Regeneration.Curves.at(Name).Curve.Definition->Poles[1]+Vector3{0,0,.07}).AcceptedCondition,"XYZ edit before subdivision");
    Old=*D.Regeneration.Curves.at(Name).Curve.Definition;
    Check(SubdivideSolidArcCurve(D,Name,{0}).AcceptedCondition,"subdivide folds existing XYZ pole edits into exact controls");
    New=*D.Regeneration.Curves.at(Name).Curve.Definition;Worst=0;
    for(int I=0;I<=1000;++I)Worst=std::max(Worst,Distance(EvaluateNurbsCurve(Old,I/1000.0).Position,EvaluateNurbsCurve(New,I/1000.0).Position));
    Check(Worst<1e-10&&D.Tree.Steps.front().PoleEdits.empty(),"moved curve shape retained, stale pole indices cleared");
    SolidArcDocument SketchDoc;SolidArcDrawingRequest Request;Request.Kind=SolidArcDrawingKind::Curve;Request.Form=SketchSplineForm::Nurbs;Request.Points={{0,0},{.2,.4},{.6,-.2},{1,0}};
    Check(DrawSolidArcSketch(SketchDoc,Request).AcceptedCondition,"ordinary sketch spline fixture");
    const auto SketchName=SketchDoc.Tree.Steps.front().Label;
    Check(DeleteSolidArcSketchPoints(SketchDoc,SketchName,{1}).AcceptedCondition,"ordinary sketch spline point deletion is local too");
    Check(SketchDoc.Tree.Steps.size()==1&&SketchDoc.Regeneration.Sketches.at(SketchName).Curves.size()==1&&SketchDoc.Regeneration.Sketches.at(SketchName).Points.size()==3,"sketch spline and unselected controls survive");
    SolidArcDocument Cut;auto CutName=Add(Cut,P);Old=*Cut.Regeneration.Curves.at(CutName).Curve.Definition;
    const auto T=SolidArcCurveControlParameters(Old);
    Check(DeleteSolidArcCurveSpans(Cut,CutName,{1}).AcceptedCondition,"delete only the middle spline span");
    Check(Cut.Regeneration.Curves.size()==2,"two unselected curve pieces remain");
    const auto& Left=*Cut.Regeneration.Curves.at(CutName).Curve.Definition;
    const auto Other=std::find_if(Cut.Regeneration.Curves.begin(),Cut.Regeneration.Curves.end(),[&](auto& E){return E.first!=CutName;});
    const auto& Right=*Other->second.Curve.Definition;Worst=0;
    for(int I=0;I<=100;++I){double U=I/100.0;Worst=std::max(Worst,Distance(EvaluateNurbsCurve(Left,U).Position,EvaluateNurbsCurve(Old,T[0]+U*(T[1]-T[0])).Position));Worst=std::max(Worst,Distance(EvaluateNurbsCurve(Right,U).Position,EvaluateNurbsCurve(Old,T[2]+U*(T[3]-T[2])).Position));}
    Check(Worst<1e-10,"span removal preserves both unselected intervals exactly");
    Check(UndoSolidArcCommand(Cut)&&Cut.Regeneration.Curves.size()==1,"span deletion is one undo entry");
    Check(RedoSolidArcCommand(Cut)&&Cut.Regeneration.Curves.size()==2,"span deletion redo");
    Saved=Reload(Cut);Check(Saved.Regeneration.Curves.size()==2,"split pieces survive save/load");
    SolidArcDocument Loft;
    auto A=Add(Loft,{{-1,-0.3,1},{1,-0.3,1}}), B=Add(Loft,{{-1,0.3,1},{1,0.3,1}});
    Check(LoftSolidArcCurves(Loft,{B,A},0,false).AcceptedCondition,"loft from reversed picking order");
    auto Scene=BuildSolidArcScene(Loft);double NormalZ=0;for(auto C:Scene.Corners)NormalZ+=C.Normal[2];
    Check(NormalZ>0,"new loft defaults to positive dominant normal, not inverted");
    Check(Loft.Tree.Steps.back().Sections==std::vector<std::string>{B,A},"normal correction preserves user section order");
    Saved=Reload(Loft);Scene=BuildSolidArcScene(Saved);NormalZ=0;for(auto C:Scene.Corners)NormalZ+=C.Normal[2];
    Check(NormalZ>0,"loft orientation survives save/load");
    Check(!DeleteSolidArcCurveSpans(Loft,A,{0}).AcceptedCondition&&Loft.Regeneration.Bodies.size()==1,"dependent loft is protected from spline-span deletion");
    // A locked, downward-facing reference must override the generic +Z fallback.
    Loft.Tree.Steps.back().ReverseNormals=!Loft.Tree.Steps.back().ReverseNormals;
    RegenerateSolidArcDocument(Loft);
    for(const auto& Body:BuildSolidArcScene(Loft).Bodies) Loft.Appearance.LockedFigures.insert(Body.Key);
    auto C=Add(Loft,{{-1,-.3,1.05},{1,-.3,1.05}}), E=Add(Loft,{{-1,.3,1.05},{1,.3,1.05}});
    Check(LoftSolidArcCurves(Loft,{C,E},0,false).AcceptedCondition,"loft near locked reference");
    Scene=BuildSolidArcScene(Loft); NormalZ=0;
    for(auto Corner:Scene.Corners) if(Scene.Faces[Corner.Face].Name.starts_with(Loft.Tree.Steps.back().Label+".")) NormalZ+=Corner.Normal[2];
    Check(NormalZ<0,"new loft follows locked reference normals rather than arbitrary hemisphere");
    SolidArcDocument Tube;
    auto Low=Add(Tube,{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,0}});
    auto High=Add(Tube,{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,1}});
    Check(LoftSolidArcCurves(Tube,{High,Low},0,false).AcceptedCondition,"closed-section open tube from reversed pick order");
    Scene=BuildSolidArcScene(Tube); double Radial=0;
    for(const auto& Corner:Scene.Corners) Radial+=Corner.Position[0]*Corner.Normal[0]+Corner.Position[1]*Corner.Normal[1];
    Check(Radial>0,"tube normals point radially outside");
    SolidArcConstructRequest Sphere;Sphere.Kind=SolidArcConstructKind::Sphere;Sphere.Radius=1;
    SolidArcDocument Snap;Check(ConstructSolidArcPrimitive(Snap,Sphere).AcceptedCondition,"sphere reference");
    Add(Snap,{{-.5,0,.75},{.5,0,.75}});Scene=BuildSolidArcScene(Snap);auto Catalogue=CatalogueSolidArcSnaps(Snap,Scene);
    OrbitCamera Camera;Camera.Target={0,0,0};Camera.Distance=5;Camera.SceneRadius=2;Camera.OrthographicEnabled=true;PresentOrbitView(Camera,OrbitViewPreset::Top);
    auto Frame=FrameOrbitCamera(Camera,1000,800);double X,Y;Check(ProjectSolidArcPoint(Frame,{0,0,.75},X,Y),"curve snap fixture");
    auto Hit=SnapSolidArcTrace(Catalogue,Snap,Scene,Frame,X,Y,true);
    Check(Hit.Kind==SolidArcSnapKind::Curve&&std::abs(Hit.Position.Z-.75)<1e-10,"Ctrl prefers authored curve visibly overlaid behind reference, not the surface");
    Hit=SnapSolidArcTrace(Catalogue,Snap,Scene,Frame,X,Y,false);
    Check(Hit.Kind==SolidArcSnapKind::Face&&Hit.Position.Z>.99,"unmodified placement still snaps to exact surface");
    SolidArcSnapQuery Q;Q.Frame=Frame;Q.PixelX=X;Q.PixelY=Y;Q.CurveReach=12;
    Q.Filter={false,false,false,false,false,false,false,true,false};
    Q.FreeCurvesOnly=true;Q.OverlayCurvesVisible=true;
    Check(FindSolidArcSnap(Catalogue,Snap,Scene,Q).Kind==SolidArcSnapKind::Curve,"authored overlay remains snappable without face occlusion");
    SolidArcSectionClip Clip;Clip.Plane={0,0,-1,.5};Q.Section=&Clip;
    Check(FindSolidArcSnap(Catalogue,Snap,Scene,Q).Kind==SolidArcSnapKind::None,"overlay priority never bypasses section clipping");
    if(argc>1)
    {
        SolidArcDocument Block;SolidArcConstructRequest R;R.Kind=SolidArcConstructKind::Block;R.Width=2;R.Depth=2;R.Height=2;
        Check(ConstructSolidArcPrimitive(Block,R).AcceptedCondition,"block topology-delete UI fixture");
        // Hide the consumed construction sketch so this fixture tests actual CAD vertices.
        Block.Appearance.HiddenCurves.insert(Block.Tree.Steps.front().Label);
        std::ofstream(argv[1])<<EncodeSolidArcDocument(Block);
    }
    std::cout<<"PASS "<<Checks<<" curve editing checks\n";return 0;
}catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<"\n";return 1;}}
