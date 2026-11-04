#include "UI/SolidArc/Viewport/SolidArcSurfaceTracing.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "Engine/GeometryProcessing/Surfacing/SurfaceDistanceCurve.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <numbers>
#include <iomanip>
#include <set>
using namespace Frontier;using namespace Frontier::SolidArc;using namespace Frontier::GeometryProcessing;
namespace {
int Checks=0;void Check(bool B,const char* S){if(!B)throw std::runtime_error(S);++Checks;std::cout<<"PASS "<<S<<std::endl;}
NurbsCurve Line(Vector3 A,Vector3 B){return MakeSolidArcControlTrace({A,B},SolidArcDrawingKind::Curve,SketchSplineForm::Nurbs,false).Curve;}
std::string Add(SolidArcDocument& D,Vector3 A,Vector3 B,bool Cut=false){auto C=Line(A,B);Check(DrawSolidArcProjectedCurve(D,C,false,Cut,.001).AcceptedCondition,"draw exact curve");return D.Selection.Curves.front();}
SolidArcDocument Reload(const SolidArcDocument& D){auto F=DecodeSolidArcDocument(EncodeSolidArcDocument(D));SolidArcDocument R;R.Tree=F.Tree;R.Captions=F.Captions;R.Appearance=F.Appearance;RegenerateSolidArcDocument(R);return R;}
PolygonComplex Grid(bool Cylinder){PolygonComplex M;int NX=Cylinder?128:40,NY=40;
 for(int J=0;J<=NY;++J)for(int I=0;I<=NX;++I){double X=-1+2.0*I/NX,Y=-1+2.0*J/NY;if(Cylinder){double A=2*std::numbers::pi*I/NX;M.Positions.push_back({std::cos(A),std::sin(A),Y});}else M.Positions.push_back({X,Y,0});}
 auto V=[&](int I,int J){return J*(NX+1)+I;};
 for(int J=0;J<NY;++J)for(int I=0;I<NX;++I){M.Triangles.push_back({V(I,J),V(I+1,J),V(I+1,J+1)});M.Triangles.push_back({V(I,J),V(I+1,J+1),V(I,J+1)});}return M;}
}
int main(int argc,char** argv){try {
 SolidArcDocument Cross;auto A=Add(Cross,{-1,0,0},{1,0,0});Add(Cross,{0,-1,0},{0,1,0});
 Check(Cross.Regeneration.Curves.size()==2,"auto split defaults off; clicked controls unchanged");
 Check(SplitSolidArcCurveCrossings(Cross,.001).AcceptedCondition,"explicit split existing cage");
 Check(Cross.Regeneration.Curves.size()==4,"two crossing curves become four retained pieces");
 int Ends=0;for(auto& [Name,F]:Cross.Regeneration.Curves)for(auto P:{F.Curve.Definition->Poles.front(),F.Curve.Definition->Poles.back()})if(Length(P)<1e-10)++Ends;
 Check(Ends==4,"crossing has four identical shared endpoints");
 Check(Cross.Regeneration.Curves.contains(A),"first piece retains original label");
 Check(UndoSolidArcCommand(Cross)&&Cross.Regeneration.Curves.size()==2,"entire cage split is one undo");
 Check(RedoSolidArcCommand(Cross)&&Cross.Regeneration.Curves.size()==4,"cage split redo");
 Check(Reload(Cross).Regeneration.Curves.size()==4,"split curves save/load");
 SolidArcDocument Auto;Add(Auto,{-1,0,0},{1,0,0});Add(Auto,{0,-1,0},{0,1,0},true);
 Check(Auto.Regeneration.Curves.size()==4,"auto split on new curve commit");
 Check(UndoSolidArcCommand(Auto)&&Auto.Regeneration.Curves.size()==1,"draw plus auto cut is one undo");
 SolidArcDocument Miss;Add(Miss,{-1,0,0},{1,0,0});Add(Miss,{0,-1,.05},{0,1,.05},true);
 Check(Miss.Regeneration.Curves.size()==2,"screen-space crossing at different depths is NOT cut");
 auto Plane=TraceSurfaceDistanceCurve(Grid(false),Line({0,-.8,0},{0,.8,0}),Line({.3,-.6,0},{.3,.6,0}),.2,.002);
 if(!Plane.Refusal.empty())std::cerr<<Plane.Refusal<<std::endl;
 Check(Plane.Refusal.empty()&&Plane.Curve.Definition,"intrinsic contour on plane");
 double Worst=0;for(int I=0;I<=100;++I){auto P=EvaluateNurbsCurve(*Plane.Curve.Definition,I/100.0).Position;Worst=std::max(Worst,std::abs(P.X-.2));}
 Check(Worst<.003,"plane distance equals requested 0.2 metres");
 auto Cylinder=TraceSurfaceDistanceCurve(Grid(true),Line({1,0,-.8},{1,0,.8}),Line({std::cos(1.2),std::sin(1.2),-.6},{std::cos(1.2),std::sin(1.2),.6}),1,.002);
 if(!Cylinder.Refusal.empty())std::cerr<<Cylinder.Refusal<<std::endl;
 Check(Cylinder.Refusal.empty()&&Cylinder.Curve.Definition,"intrinsic contour wraps around cylinder");
 auto P=EvaluateNurbsCurve(*Cylinder.Curve.Definition,.5).Position;
 Check(std::abs(std::atan2(P.Y,P.X)-1)<.006,"cylinder arc-length distance is one metre, not ambient XYZ offset");
 Check(std::hypot(P.X-1,P.Y)<.97,"surface distance is distinct from straight-line chord");
 auto Invalid=TraceSurfaceDistanceCurve(Grid(false),Line({0,-.8,.1},{0,.8,.1}),Line({.3,-.6,0},{.3,.6,0}),.2,.002);
 Check(!Invalid.Refusal.empty(),"off-reference guide refuses instead of silently projecting far away");
 SolidArcDocument Linked;SolidArcConstructRequest Block;Block.Width=2;Block.Depth=2;Block.Height=1;
 Check(ConstructSolidArcPrimitive(Linked,Block).AcceptedCondition,"live offset reference fixture");
 auto Guide=Add(Linked,{.3,.2,1},{.3,1.8,1});auto Target=Add(Linked,{.7,.3,1},{.7,1.7,1});const auto Body=Linked.Regeneration.Bodies.front().Key;
 auto Command=LinkSolidArcSurfaceOffset(Linked,Guide,Target,Body,.2,.01);if(!Command.AcceptedCondition)std::cerr<<Command.Refusal<<std::endl;
 Check(Command.AcceptedCondition,"existing target becomes a live geodesic offset");
 Check(std::abs(EvaluateNurbsCurve(*Linked.Regeneration.Curves.at(Target).Curve.Definition,.5).Position.X-.5)<.02,"live offset retains target side/extent");
 Check(MoveSolidArcPoles(Linked,Guide,{0,1},{{.4,.2,1},{.4,1.8,1}}).AcceptedCondition,"move guide regenerates linked target");
 Check(std::abs(EvaluateNurbsCurve(*Linked.Regeneration.Curves.at(Target).Curve.Definition,.5).Position.X-.6)<.02,"linked target follows guide change");
 auto Large=MoveSolidArcPoles(Linked,Guide,{0,1},{{.9,.2,1},{.9,1.8,1}});if(!Large.AcceptedCondition)std::cerr<<Large.Refusal<<std::endl;
 Check(Large.AcceptedCondition,"large guide translation stays linked");
 Check(std::abs(EvaluateNurbsCurve(*Linked.Regeneration.Curves.at(Target).Curve.Definition,.5).Position.X-1.1)<.02,"target does not jump to opposite side when guide passes old anchor");
 auto Before=EncodeSolidArcDocument(Linked);
 Check(!LinkSolidArcSurfaceOffset(Linked,Guide,Target,Body,-.1,.01).AcceptedCondition && EncodeSolidArcDocument(Linked)==Before,"invalid distance is atomic");
 auto Saved=Reload(Linked);Check(Saved.Tree.Steps.back().CurveOffsetGuide==Guide&&Saved.Regeneration.Refused==0,"live relation survives codec and regeneration");
 Check(!DeleteSolidArcCurvePoles(Linked,Target,{0}).AcceptedCondition,"derived controls protected until detach");
 Check(DetachSolidArcSurfaceOffset(Linked,Target).AcceptedCondition,"explicit detach keeps generated curve");
 Check(Linked.Tree.Steps.back().CurveOffsetGuide.empty(),"detached curve is independent");
 SolidArcDocument Reordered;Check(ConstructSolidArcPrimitive(Reordered,Block).AcceptedCondition,"history reorder reference fixture");
 auto Early=Add(Reordered,{.7,.3,1},{.7,1.7,1});auto Late=Add(Reordered,{.3,.2,1},{.3,1.8,1});
 Check(LinkSolidArcSurfaceOffset(Reordered,Late,Early,Reordered.Regeneration.Bodies.front().Key,.2,.01).AcceptedCondition,"existing earlier target is safely reordered after its guide");
 if(argc>3){SolidArcDocument F;Check(ConstructSolidArcPrimitive(F,Block).AcceptedCondition,"live offset UI fixture");Add(F,{.3,.2,1},{.3,1.8,1});Add(F,{.7,.3,1},{.7,1.7,1});F.Appearance.LockedFigures.insert(F.Regeneration.Bodies.front().Key);F.Appearance.HiddenCurves.insert(F.Tree.Steps.front().Label);std::ofstream(argv[3])<<EncodeSolidArcDocument(F);}
 if(argc>1){auto F=LoadSolidArcDocumentFile(argv[1]);Check(F.Refusal.empty(),"uploaded cage decodes");SolidArcDocument D;D.Tree=F.Tree;D.Captions=F.Captions;D.Appearance=F.Appearance;RegenerateSolidArcDocument(D);
 Check(D.Regeneration.Refused==0 && D.Regeneration.Curves.size()==23 && D.Regeneration.Bodies.size()==3,"uploaded cage: 23 curves and 3 reference sheets, no lofts");
 if(argc>4) {
  std::ofstream Report(argv[4]);Report<<"curve_a\tcurve_b\ttop_x_m\ttop_y_m\tseparation_m\tclassification\n"<<std::setprecision(9);
  for(auto A=D.Regeneration.Curves.begin();A!=D.Regeneration.Curves.end();++A)for(auto B=std::next(A);B!=D.Regeneration.Curves.end();++B){
   const auto& CA=*A->second.Curve.Definition;const auto& CB=*B->second.Curve.Definition;std::vector<std::pair<double,double>> Found;
   for(int I=0;I<64;++I)for(int J=0;J<64;++J){
    auto P=EvaluateNurbsCurve(CA,I/64.).Position,Q=EvaluateNurbsCurve(CB,J/64.).Position;
    auto U=EvaluateNurbsCurve(CA,(I+1)/64.).Position-P,V=EvaluateNurbsCurve(CB,(J+1)/64.).Position-Q,W=Q-P;
    double Det=U.X*V.Y-U.Y*V.X;if(std::abs(Det)<1e-14)continue;
    double S=(W.X*V.Y-W.Y*V.X)/Det,T=(W.X*U.Y-W.Y*U.X)/Det;if(S<0||S>1||T<0||T>1)continue;
    double X=(I+S)/64,Y=(J+T)/64;
    for(int N=0;N<20;++N){auto E=EvaluateNurbsCurveDerivatives(CA,X),F=EvaluateNurbsCurveDerivatives(CB,Y);auto R=E.Position-F.Position;double D=E.First.X*F.First.Y-E.First.Y*F.First.X;if(std::abs(D)<1e-15)break;double DX=(-R.X*F.First.Y+R.Y*F.First.X)/D,DY=(E.First.X*R.Y-E.First.Y*R.X)/D;X=std::clamp(X+DX,0.,1.);Y=std::clamp(Y+DY,0.,1.);if(std::abs(DX)+std::abs(DY)<1e-12)break;}
    auto E=EvaluateNurbsCurve(CA,X).Position,F=EvaluateNurbsCurve(CB,Y).Position;
    if(std::hypot(E.X-F.X,E.Y-F.Y)>1e-6 || std::any_of(Found.begin(),Found.end(),[&](auto P){return std::abs(P.first-X)+std::abs(P.second-Y)<1e-5;}))continue;
    Found.push_back({X,Y});double Gap=Length(E-F);Report<<A->first<<'\t'<<B->first<<'\t'<<(E.X+F.X)*.5<<'\t'<<(E.Y+F.Y)*.5<<'\t'<<Gap<<'\t'<<(Gap<=.001?"within 1 mm":"3D separation: do not auto-cut")<<'\n';
   }
  }
  auto Before=EncodeSolidArcDocument(D);auto Cut=SplitSolidArcCurveCrossings(D,.001);Report<<"# Dry-run cut: "<<Cut.AcceptedCondition<<"; curves="<<D.Regeneration.Curves.size()<<"; refusal="<<Cut.Refusal<<"\n";
  Check(Cut.AcceptedCondition && UndoSolidArcCommand(D) && EncodeSolidArcDocument(D)==Before,"supplied cage optional cut is undoable and leaves source intact");
 }
 }
 if(argc>2){SolidArcDocument F;Add(F,{-1,0,0},{1,0,0});Add(F,{0,-1,0},{0,1,0});std::ofstream(argv[2])<<EncodeSolidArcDocument(F);}
 std::cout<<"PASS "<<Checks<<" cage tool checks\n";return 0;
}catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<std::endl;return 1;}}
