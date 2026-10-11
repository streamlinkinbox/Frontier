#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
using namespace Frontier;
namespace {
int Side(const BrepBody& B) { for (size_t i=0;i<B.Faces.size();++i) if(B.Faces[i].Surface.Classification==SurfaceClassification::Cone) return int(i); return -1; }
bool Exact(const BrepBody& B, Vec3 O, Vec3 A, double R0, double R1, double H) {
 if(!B.Validate().Solid()||B.Vertices.size()!=2||B.Edges.size()!=3||B.Faces.size()!=3) return false;
 int F=Side(B); if(F<0)return false; const auto&S=B.Faces[F].Surface; A=A.Normalised();
 if(S.Origin.Distance(O)>1e-10||S.Axis.Normalised().Dot(A)<1-1e-12||std::fabs(S.RadiusMajor-R0)>1e-12||std::fabs(S.RadiusMinor-R1)>1e-12)return false;
 for(int i=0;i<7;++i) for(int j=0;j<5;++j){double u=S.DomainStartU()+(S.DomainEndU()-S.DomainStartU())*i/6.,v=S.DomainStartV()+(S.DomainEndV()-S.DomainStartV())*j/4.; Vec3 P=S.Sample(u,v);double z=(P-O).Dot(A),r=(P-(O+A*z)).Length();if(std::fabs(z-H*j/4.)>1e-10||std::fabs(r-(R0+(R1-R0)*j/4.))>1e-10)return false;}
 return true;
}
}
int main(){ VerificationPanel P("SolidArc · Phase 29 · Cone Side Push Verification — exact normal offsets"); constexpr double R0=10,R1=4,H=20,D=2; Vec3 O{},A{0,0,1}; BrepBody C=BrepBody::Cone(O,A,R0,R1,H).Payload;int F=Side(C);double Scale=std::sqrt(1+std::pow((R1-R0)/H,2));
 P.Section("Native conical side offset is a true normal-distance edit"); P.Expect("Native full frustum exposes its conical side",F>=0);
 for(auto [Name,d]: {std::pair{"outward",D},std::pair{"inward",-D}}){auto R=BlendSolver::PushFace(C,F,d); P.Expect((std::string("The ")+Name+" conical push is solid").c_str(),R&&R.Payload.Validate().Solid());P.Expect((std::string("The ")+Name+" push has exact normal-offset cone radii").c_str(),R&&Exact(R.Payload,O,A,R0+d*Scale,R1+d*Scale,H));}
 P.Section("Axis and limits"); Vec3 Q{2,-4,3},B{2,-1,4};BrepBody Ob=BrepBody::Cone(Q,B,8,3,15).Payload;auto OR=BlendSolver::PushFace(Ob,Side(Ob),1.5);double OS=std::sqrt(1+std::pow((3.-8.)/15.,2));P.Expect("Oblique conical side push is solid",OR&&OR.Payload.Validate().Solid());P.Expect("Oblique conical side push retains exact axis and normal distance",OR&&Exact(OR.Payload,Q,B,8+1.5*OS,3+1.5*OS,15));P.Expect("Push collapsing the small cap radius refuses",!BlendSolver::PushFace(C,F,-R1/Scale));
 P.Section("C++ visual proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER required
#endif
 std::filesystem::path Proof=std::filesystem::path(SOLIDARC_PROOF_FOLDER)/"Phase29_ConeSidePushes.png";std::error_code E;std::filesystem::remove(Proof,E);ConsoleHost Host(SOLIDARC_PROOF_FOLDER,1280,800);auto Run=[&](const char*s){return Host.Execute(s);};bool Ok=Run("gizmo off")&&Run("reset")&&Run("view iso")&&Run("cone (-15,0,0) 8 3 20 --name=Reference")&&Run("matcap Reference steel")&&Run("cone (15,0,0) 8 3 20 --name=Source")&&Run("push Source 2 --face=0 --name=Outward")&&Run("matcap Outward gold")&&Run("view fit")&&Run("render sheet 0")&&Run("reset")&&Run("view iso")&&Run("cone (-15,0,0) 8 3 20 --name=Reference")&&Run("matcap Reference steel")&&Run("cone (15,0,0) 8 3 20 --name=Source")&&Run("push Source -2 --face=0 --name=Inward")&&Run("matcap Inward copper")&&Run("view fit")&&Run("render sheet 1")&&Run("reset")&&Run("view top")&&Run("cone (-15,0,0) 8 3 20 --name=Reference")&&Run("matcap Reference steel")&&Run("cone (15,0,0) 8 3 20 --name=Source")&&Run("push Source 3 --face=0 --name=Outward")&&Run("matcap Outward plastic-blue")&&Run("view fit")&&Run("render sheet 2")&&Run("reset")&&Run("view iso")&&Run("cone (-15,0,0) 8 3 20 --axis=(2,-1,4) --name=Reference")&&Run("matcap Reference steel")&&Run("cone (15,0,0) 8 3 20 --axis=(2,-1,4) --name=Source")&&Run("push Source 2 --face=0 --name=Oblique")&&Run("matcap Oblique gold")&&Run("view fit")&&Run("render sheet 3")&&Run("render sheet finalize Phase29_ConeSidePushes");P.Expect("C++ conical-offset proof commands complete",Ok);P.Expect("C++ conical-offset proof PNG is written",std::filesystem::exists(Proof)&&std::filesystem::file_size(Proof,E)>100000);return P.Conclude(); }
