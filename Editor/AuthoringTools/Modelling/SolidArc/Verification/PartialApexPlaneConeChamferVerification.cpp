#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
using namespace Frontier;
namespace {
std::vector<NurbsSurface> SplitAngular(const NurbsSurface& S) noexcept { auto H=S.SplitU(.5*(S.DomainStartU()+S.DomainEndU())); return {std::move(H.first),std::move(H.second)}; }
bool CloseCaps(BrepBody& Body, Vec3 A, Vec3 B, Vec3 R, double Sweep, double Extent) noexcept
{
    Vec3 Axis=(B-A).Normalised(); R=(R-Axis*R.Dot(Axis)).Normalised(); Vec3 RE=R*std::cos(Sweep)+Axis.Cross(R)*std::sin(Sweep);
    std::vector<int> S,E; for(size_t I=0;I<Body.Edges.size();++I){if(Body.Edges[I].Coedges.size()!=1)continue;Vec3 M=Body.Edges[I].Curve.Sample(.5*(Body.Edges[I].Curve.DomainStart()+Body.Edges[I].Curve.DomainEnd()));Vec3 Q=M-(A+Axis*(M-A).Dot(Axis));if(Q.Length()<=1e-9)return false;Q=Q.Normalised();(std::fabs(Q.Dot(R))>=std::fabs(Q.Dot(RE))?S:E).push_back((int)I);} if(S.empty()||E.empty())return false;
    auto AC=[&](std::vector<int> Es,Vec3 Rad){Deliver<NurbsCurve> L=NurbsCurve::Line(A,B);if(!L)return false;int AE=Body.AddEdge(L.Payload,ScalarCriteria::MergeTolerance),AV=Body.AddVertex(A,ScalarCriteria::MergeTolerance),BV=Body.AddVertex(B,ScalarCriteria::MergeTolerance);std::vector<std::pair<int,bool>> P;int Cur=AV;while(Cur!=BV){auto It=std::find_if(Es.begin(),Es.end(),[&](int X){return Body.Edges[X].VertexStart==Cur||Body.Edges[X].VertexEnd==Cur;});if(It==Es.end())return false;int X=*It;bool Rev=Body.Edges[X].VertexEnd==Cur;Cur=Rev?Body.Edges[X].VertexStart:Body.Edges[X].VertexEnd;P.push_back({X,Rev});Es.erase(It);}if(!Es.empty())return false;double Pad=.01*std::max(Extent,A.Distance(B))+ScalarCriteria::MergeTolerance;Deliver<NurbsSurface> Pl=NurbsSurface::Plane(A-Rad*Pad-Axis*Pad,Rad,Axis,Extent+2*Pad,A.Distance(B)+2*Pad);if(!Pl)return false;int F=Body.AddFace(std::move(Pl.Payload));Body.Faces[F].Natural=false;int Loop=Body.AddLoop(F,true);for(auto [X,Rev]:P)Body.AddCoedge(X,Rev,F,Loop);Body.AddCoedge(AE,Body.Edges[AE].VertexStart==BV,F,Loop);return true;};
    if(!AC(S,R)||!AC(E,RE))return false;
    Body.Orient();
    return Body.Validate().Solid();
}
Deliver<BrepBody> SectorApex(Vec3 Base,Vec3 Axis,double Outer,double Shoulder,double Foot,double Height,double Sweep) noexcept
{
    Axis=Axis.Normalised();Vec3 R=Workplane::FromNormal(Base,Axis).AxisX;Vec3 Sh=Base+Axis*Shoulder, Apex=Sh+Axis*Height;
    std::vector<std::pair<Vec3,Vec3>> P{{Base,Base+R*Outer},{Base+R*Outer,Sh+R*Outer},{Sh+R*Outer,Sh+R*Foot},{Sh+R*Foot,Apex}};std::vector<NurbsSurface> F;
    for(size_t I=0;I<P.size();++I){auto L=NurbsCurve::Line(P[I].first,P[I].second);auto S=L?NurbsSurface::Revolution(L.Payload,Base,Axis,Sweep):Deliver<NurbsSurface>::Reject(L.Denial.Reason,L.Denial.Detail);if(!S)return Deliver<BrepBody>::Reject(S.Denial.Reason,S.Denial.Detail);if(I==1){S.Payload.Classification=SurfaceClassification::Cylinder;S.Payload.Origin=Base;S.Payload.Axis=Axis;S.Payload.RadiusMajor=S.Payload.RadiusMinor=Outer;}if(I==3){S.Payload.Classification=SurfaceClassification::Cone;S.Payload.Origin=Sh;S.Payload.Axis=Axis;S.Payload.RadiusMajor=Foot;S.Payload.RadiusMinor=0;}for(auto& Q:SplitAngular(S.Payload))F.push_back(std::move(Q));}
    auto B=BrepBody::Sew(F);if(!B)return B;if(ScalarCriteria::WithinAngularTolerance(std::fabs(Sweep),ScalarCriteria::Pi))return B;if(!CloseCaps(B.Payload,Base,Apex,R,Sweep,Outer))return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"caps");return B;
}
bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After=Body.Validate();
    return After.Vertices==Before.Vertices&&After.Edges==Before.Edges&&After.Faces==Before.Faces&&
           After.OpenEdges==Before.OpenEdges&&After.NonManifoldEdges==Before.NonManifoldEdges&&
           After.MisorientedEdges==Before.MisorientedEdges&&std::fabs(After.Volume-Before.Volume)<1e-9;
}
}
int main()
{
    VerificationPanel P("SolidArc · Phase 36n · partial apex plane–cone root chamfer");
    constexpr double Outer=10.0, Shoulder=8.0, Foot=5.0, Height=7.0, Sweep=2.0, Back=0.5;
    const Vec3 Base{0,0,0}, Axis{0,0,1};
    const Deliver<BrepBody> SourceResult=SectorApex(Base,Axis,Outer,Shoulder,Foot,Height,Sweep);
    const BrepBody Source=SourceResult.Payload; const BodyReport Before=Source.Validate();
    std::vector<int> Members;
    for(int E=0;E<(int)Source.Edges.size();++E){const BrepEdge& X=Source.Edges[E];if(X.Closed()||X.Coedges.size()!=2||X.Curve.Classification!=CurveClassification::Arc)continue;int C=0,R=0;for(int Ce:X.Coedges){int F=Source.Coedges[Ce].Face;if(F>=0&&F<(int)Source.Faces.size()){C+=Source.Faces[F].Surface.Classification==SurfaceClassification::Cone;R+=Source.Faces[F].Surface.Classification==SurfaceClassification::Revolution;}}Vec3 M=X.Curve.Sample(.5*(X.Curve.DomainStart()+X.Curve.DomainEnd()));if(C==1&&R==1&&std::fabs(M.Z-Shoulder)<1e-8&&std::fabs(std::hypot(M.X,M.Y)-Foot)<1e-8)Members.push_back(E);}
    P.Section("Canonical partial apex sector");
    P.Expect("The sector source closes as one V11/E19/F10 solid",SourceResult&&Before.Solid()&&Before.Hulls==1&&Before.Genus==0&&Source.Vertices.size()==11&&Source.Edges.size()==19&&Source.Coedges.size()==38&&Source.Loops.size()==10&&Source.Faces.size()==10);
    P.Expect("The apex root has two open rational arc members",Members.size()==2);
    Deliver<std::vector<int>> Chain=!Members.empty()?BlendSolver::TangentChain(Source,Members.front()):Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported,"root unavailable");
    P.Expect("The selected apex root propagates through the complete chain",Chain&&Chain.Payload.size()==2);
    const Deliver<BrepBody> HalfSourceResult=SectorApex(Base,Axis,Outer,Shoulder,Foot,Height,ScalarCriteria::Pi);
    const BrepBody HalfSource=HalfSourceResult.Payload; const BodyReport HalfBefore=HalfSource.Validate();
    P.Expect("The half-turn apex source closes as one V11/E18/F9 solid",HalfSourceResult&&HalfBefore.Solid()&&HalfSource.Vertices.size()==11&&HalfSource.Edges.size()==18&&HalfSource.Coedges.size()==36&&HalfSource.Loops.size()==9&&HalfSource.Faces.size()==9);
    std::vector<int> HalfMembers;for(int E=0;E<(int)HalfSource.Edges.size();++E){const BrepEdge& X=HalfSource.Edges[E];if(X.Closed()||X.Coedges.size()!=2||X.Curve.Classification!=CurveClassification::Arc)continue;int C=0,R=0;for(int Ce:X.Coedges){int F=HalfSource.Coedges[Ce].Face;if(F>=0&&F<(int)HalfSource.Faces.size()){C+=HalfSource.Faces[F].Surface.Classification==SurfaceClassification::Cone;R+=HalfSource.Faces[F].Surface.Classification==SurfaceClassification::Revolution;}}Vec3 M=X.Curve.Sample(.5*(X.Curve.DomainStart()+X.Curve.DomainEnd()));if(C==1&&R==1&&std::fabs(M.Z-Shoulder)<1e-8&&std::fabs(std::hypot(M.X,M.Y)-Foot)<1e-8)HalfMembers.push_back(E);}
    int HalfApplied=0;auto HalfChamfer=!HalfMembers.empty()?BlendSolver::ChamferEdges(HalfSource,{HalfMembers.front()},Back,&HalfApplied):Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"half root unavailable");
    P.Expect("The half-turn apex chamfer reaches exact V10/E14/F6 topology",HalfChamfer&&HalfApplied==1&&HalfChamfer.Payload.Validate().Solid()&&HalfChamfer.Payload.Vertices.size()==10&&HalfChamfer.Payload.Edges.size()==14&&HalfChamfer.Payload.Coedges.size()==28&&HalfChamfer.Payload.Loops.size()==6&&HalfChamfer.Payload.Faces.size()==6);
    P.Section("Exact partial apex reconstruction");
    int Applied=0;auto Result=!Members.empty()?BlendSolver::ChamferEdges(Source,{Members.front()},Back,&Applied):Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"root unavailable");
    P.Expect("The partial apex chamfer commits one complete chain",Result&&Applied==1&&Result.Payload.Validate().Solid());
    if(Result){auto After=Result.Payload.Validate();P.Expect("The general apex result reaches exact V10/E15/F7 topology",After.Hulls==1&&After.Genus==0&&After.OpenEdges==0&&After.NonManifoldEdges==0&&After.MisorientedEdges==0&&Result.Payload.Vertices.size()==10&&Result.Payload.Edges.size()==15&&Result.Payload.Coedges.size()==30&&Result.Payload.Loops.size()==7&&Result.Payload.Faces.size()==7);double Slant=std::hypot(Height,Foot),Ax=Back*Height/Slant,Contact=Foot*(1-Ax/Height),Expected=std::fabs(Sweep)/ScalarCriteria::TwoPi*(ScalarCriteria::Pi*Ax/3*((Foot+Back)*(Foot+Back)+(Foot+Back)*Contact-Foot*Foot-Foot*Contact));P.Within("The apex sector wedge follows the exact meridian integral",std::fabs((After.Volume-Before.Volume)-Expected)/Expected,2e-3);P.Expect("The radial caps remain closed and the source remains immutable",SameSource(Source,Before));}
    P.Section("Transactional refusal boundaries");
    P.Expect("Zero and consuming setbacks refuse without mutation",!Members.empty()&&!BlendSolver::ChamferEdge(Source,Members.front(),0.0)&&!BlendSolver::ChamferEdge(Source,Members.front(),Height)&&SameSource(Source,Before));
    auto Torus=BrepBody::Torus({20,0,0},Axis,4,1);P.Expect("An arbitrary torus edge remains an explicit refusal",Torus&&!BlendSolver::ChamferEdge(Torus.Payload,0,Back));
    P.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER,1600,900);bool Added=!Members.empty()&&Host.Document().AddBody("PartialApexPlaneConeSource",Source).Identity>0;bool Commit=Added&&Host.Execute("chamfer PartialApexPlaneConeSource 0.5 --edges="+std::to_string(Members.front())+" --name=PartialApexPlaneConeChamfer")&&Host.Document().Find("PartialApexPlaneConeChamfer")&&Host.Document().Find("PartialApexPlaneConeChamfer")->Body.Validate().Solid();P.Expect("The console commits the bounded partial apex route",Commit);
    std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase36n_PartialApexPlaneConeChamfer.png";std::error_code Error;std::filesystem::remove(Proof,Error);ConsoleHost PH(SOLIDARC_PROOF_FOLDER,1600,900);bool Rendered=SourceResult&&Result&&PH.Document().AddBody("SharpPartialApex",Source.Transformed(Mat4::Translation({-11,0,0}))).Identity>0&&PH.Document().AddBody("ChamferedPartialApex",Result.Payload.Transformed(Mat4::Translation({11,0,0}))).Identity>0&&PH.Execute("view top")&&PH.Execute("view fit")&&PH.Execute("render Phase36n_PartialApexPlaneConeChamfer");P.Expect("The partial apex source/result proof render completes",Rendered);P.Expect("The partial apex proof PNG is visible",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,Error)>100000);
    return P.Conclude();
}
