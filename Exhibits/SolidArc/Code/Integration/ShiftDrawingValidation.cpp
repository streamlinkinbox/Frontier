#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Interface/SolidArcScript.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Viewport/SolidArcCamera.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <sstream>
#include <iomanip>
using namespace Frontier;
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;
namespace {
int Checks=0;
void Check(bool Ok,const std::string& Label) { if(!Ok)throw std::runtime_error(Label);++Checks;std::cout<<"PASS "<<Label<<std::endl; }
struct HeadlessUiHarness
{
    ImGuiContext*                        Ctx = nullptr;
    Frontier::SolidArc::SolidArcOptions  Options{};
    Frontier::SolidArc::SolidArcWorkspaceState State{};
    ImTextureID                          NextTexId = ImTextureID(100);

    HeadlessUiHarness()
    {
        IMGUI_CHECKVERSION();
        Ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(Ctx);
        ImGuiIO& Io = ImGui::GetIO();
        Io.DisplaySize = ImVec2(1366.0f, 768.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.IniFilename = nullptr;
        Io.ConfigInputTrickleEventQueue = true;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        Options.WindowWidth  = 1366;
        Options.WindowHeight = 768;
        Options.RecoveryPath = (std::filesystem::temp_directory_path() / "SolidArcShiftDrawingRecovery.solidarc").string();
        std::error_code Ignore;
        std::filesystem::remove(Options.RecoveryPath, Ignore);
        Frontier::SolidArc::ConfigureSolidArcInterface(Options);
        Frontier::SolidArc::InitialiseSolidArcWorkspace(State, Options);
        State.WelcomeOpen = false;
    }

    ~HeadlessUiHarness()
    {
        if (Ctx) ImGui::DestroyContext(Ctx);
        std::error_code Ignore;
        std::filesystem::remove(Options.RecoveryPath, Ignore);
    }

    void SyncTextures()
    {
        if (ImDrawData* DrawData = ImGui::GetDrawData(); DrawData && DrawData->Textures)
        {
            for (ImTextureData* Tex : *DrawData->Textures)
            {
                if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
                {
                    if (Tex->TexID == ImTextureID_Invalid) Tex->SetTexID(NextTexId++);
                    Tex->SetStatus(ImTextureStatus_OK);
                }
                else if (Tex->Status == ImTextureStatus_WantDestroy)
                {
                    Tex->SetTexID(ImTextureID_Invalid);
                    Tex->SetStatus(ImTextureStatus_Destroyed);
                }
            }
        }
    }

    void StepFrame()
    {
        ImGui::GetIO().DisplaySize = ImVec2(1366.0f, 768.0f);
        ImGui::GetIO().DeltaTime   = 1.0f / 60.0f;
        // Populate CPU HoverIdentity when pointer is inside the viewport so viewport clicks match GPU ID readback.
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        if (Mouse.x >= State.ViewportRect[0] && Mouse.x <= State.ViewportRect[0] + State.ViewportRect[2]
            && Mouse.y >= State.ViewportRect[1] && Mouse.y <= State.ViewportRect[1] + State.ViewportRect[3]
            && !State.Scene.Faces.empty())
        {
            const Frontier::OrbitCameraFrame CamFrame = Frontier::FrameOrbitCamera(State.Camera, std::max(1.0f, State.ViewportRect[2]), std::max(1.0f, State.ViewportRect[3]));
            const Frontier::OrbitRay Ray = Frontier::CastOrbitRay(CamFrame, Mouse.x - State.ViewportRect[0], Mouse.y - State.ViewportRect[1]);
            const auto Hit = Frontier::SolidArc::PickSolidArcScene(State.Scene, Ray, 8.0 * CamFrame.WorldPerPixel, State.Style.XRayEnabled);
            State.HoverIdentity = Hit ? Hit->Identity : 0u;
        }
        ImGui::NewFrame();
        Frontier::SolidArc::DrawSolidArcInterface(State, ImTextureID(1), 1.0 / 60.0);
        ImGui::Render();
        SyncTextures();
    }

    bool RunScript(const std::string& ScriptText, std::string& FailureOut)
    {
        Frontier::SolidArc::SolidArcScript Script = Frontier::SolidArc::ParseSolidArcScript(ScriptText);
        if (!Script.Refusal.empty()) { FailureOut = Script.Refusal; return false; }
        while (!Frontier::SolidArc::FinishedSolidArcScript(Script))
        {
            (void)Frontier::SolidArc::AdvanceSolidArcScript(Script, State);
            StepFrame();
        }
        for (const Frontier::SolidArc::SolidArcExpectation& Exp : Script.Expectations)
        {
            if (!Exp.Passed)
            {
                char Msg[768];
                std::snprintf(Msg, sizeof(Msg), "line %d: %s (observed '%s', status='%s')", Exp.Line, Exp.Text.c_str(), Exp.Observed.c_str(), State.Status.c_str());
                FailureOut = Msg;
                return false;
            }
        }
        return true;
    }
};


std::string World(const char* Verb,Vector3 P,const char* Mod="") {
    std::ostringstream S;S<<std::setprecision(17)<<Verb<<" world "<<P.X<<" "<<P.Y<<" "<<P.Z<<" "<<Mod<<"\nframes 2\n";return S.str();
}
void Script(HeadlessUiHarness& U,const std::string& Text) {std::string Err;if(!U.RunScript(Text,Err))throw std::runtime_error(Err);}
OrthonormalFrame Arm(HeadlessUiHarness& U,SolidArcDrawingKind Kind,int Plane,bool Ortho=true,bool Segment=true) {
    auto& S=U.State;S.SurfaceTraceEnabled=false;S.DrawingCurveSnap=false;S.DrawingOptionsVisible=false;S.Picker.FaceOrientEnabled=false;S.Picker.GridEnabled=false;
    PresentOrbitView(S.Camera,Plane==0?OrbitViewPreset::Top:Plane==1?OrbitViewPreset::Front:OrbitViewPreset::Right);
    S.Camera.OrthographicEnabled=Ortho;S.Camera.Distance=5;S.Camera.Target={0,0,0};
    S.Drawing={};S.Drawing.ActiveCondition=true;S.Drawing.Kind=Kind;S.Drawing.GridPlane=Plane;S.Drawing.Elevation=.7;S.Drawing.SegmentCondition=Segment;S.Drawing.Form=SketchSplineForm::Nurbs;
    U.StepFrame();U.StepFrame();auto F=SolidArcSketchPlane(Plane);F.Origin=F.Origin+F.ZAxis*.7;return F;
}
Vector3 At(const OrthonormalFrame& F,Vector2 P) {return F.Origin+F.XAxis*P.X+F.YAxis*P.Y;}
void Line(int Plane,bool Ortho,Vector2 Delta) {
    HeadlessUiHarness U;auto F=Arm(U,SolidArcDrawingKind::Line,Plane,Ortho);
    Script(U,World("click",At(F,{.23,-.17})));
    auto A=U.State.Drawing.Points.front();
    Script(U,World("move",At(F,A+Delta),"Shift"));
    auto P=U.State.Drawing.Pointer;bool X=std::abs(Delta.X)>std::abs(Delta.Y);
    Check(U.State.Picker.Current.Kind==SolidArcSnapKind::Axis && U.State.Picker.Current.Source.find("Shift straight")!=std::string::npos,"Shift straight preview in plane "+std::to_string(Plane)+(Ortho?" ortho":" perspective"));
    Check(X?P.Y==A.Y:P.X==A.X,"nearest axis keeps transverse coordinate exactly equal to clicked reference");
    Script(U,World("click",At(F,A+Delta),"Shift"));
    const auto& Points=U.State.Drawing.Points;
    Check(Points.size()==2 && (X?Points[1].Y==Points[0].Y:Points[1].X==Points[0].X),"committed line equals constrained preview");
    Check(!U.State.Drawing.ActiveCondition && U.State.Document.Tree.Steps.size()==1 && U.State.Document.Regeneration.Refused==0,"straight line saves valid geometry");
    Check(U.State.Picker.Lock==SolidArcSnapLockKind::None,"temporary Shift does not leave a sticky direction lock");
}
void ArcSlot(int Plane,SolidArcDrawingKind Kind,bool Vertical) {
    HeadlessUiHarness U;auto F=Arm(U,Kind,Plane);
    Script(U,World("click",At(F,{-.4,-.3})));
    auto A=U.State.Drawing.Points.front();auto Delta=Vertical?Vector2{.12,.9}:Vector2{.9,.12};
    Script(U,World("click",At(F,A+Delta),"Shift"));
    auto B=U.State.Drawing.Points.back();
    Check(U.State.Drawing.Points.size()==2 && (Vertical?B.X==A.X:B.Y==A.Y),std::string(Kind==SolidArcDrawingKind::Arc?"arc endpoints":"slot centres")+" align in plane "+std::to_string(Plane));
    auto C=Kind==SolidArcDrawingKind::Arc?(A+B)*.5+(Vertical?Vector2{.45,0}:Vector2{0,.45}):B+Vector2{.21,.19};
    Script(U,World("move",At(F,C),"Shift"));
    Check(U.State.Picker.Current.Source.find("Shift straight")==std::string::npos,"bulge / width stage stays free even with Shift held");
    Script(U,World("click",At(F,C),"Shift"));
    Check(!U.State.Drawing.ActiveCondition && U.State.Document.Tree.Steps.size()==1 && U.State.Document.Regeneration.Refused==0,"aligned arc / slot finishes without collinear collapse");
}
void Polyline() {
    HeadlessUiHarness U;auto F=Arm(U,SolidArcDrawingKind::Line,0,true,false);
    Script(U,World("click",At(F,{-.5,-.4}))+World("click",At(F,{.4,-.3}),"Shift"));
    auto& D=U.State.Drawing;Check(D.Points.size()==2 && D.Points[1].Y==D.Points[0].Y,"polyline horizontal leg snaps from previous click");
    Script(U,World("click",At(F,{.5,.4}),"Shift"));
    Check(D.Points.size()==3 && D.Points[2].X==D.Points[1].X,"polyline next leg uses new reference, not first point");
    Script(U,World("move",At(F,{.8,.7})));
    Check(U.State.Picker.Current.Source.find("Shift straight")==std::string::npos && std::abs(D.Pointer.X-D.Points.back().X)>.1 && std::abs(D.Pointer.Y-D.Points.back().Y)>.1,"releasing Shift immediately returns to free drawing");
    Script(U,World("move",At(F,D.Points.front()),"Shift"));
    Check(Distance(D.Pointer,D.Points.front())>1e-6,"near-first-point closure cannot override an incompatible Shift axis");
    Script(U,"key Escape\nframes 2\n");
}
void Excluded(SolidArcDrawingKind Kind) {
    HeadlessUiHarness U;auto F=Arm(U,Kind,0);
    Script(U,World("click",At(F,{-.4,-.3}))+World("move",At(F,{.5,-.15}),"Shift"));
    auto& D=U.State.Drawing;
    Check(std::abs(D.Pointer.Y-D.Points.front().Y)>.1 && U.State.Picker.Current.Source.find("Shift straight")==std::string::npos,Kind==SolidArcDrawingKind::Curve?"freeform curves remain unconstrained":"rectangle / circle sizing is not collapsed by Shift");
}
void LockedAndSnapped() {
    HeadlessUiHarness U;auto F=Arm(U,SolidArcDrawingKind::Line,0,true,false);
    SolidArcDrawingRequest R;R.Kind=SolidArcDrawingKind::Line;R.Points={{.6,.18},{.9,.18}};
    if(!DrawSolidArcSketch(U.State.Document,R).AcceptedCondition)throw std::runtime_error("snap fixture");
    RefreshSolidArcScene(U.State,false);U.State.DrawingCurveSnap=true;
    Script(U,World("click",At(F,{0,0}))+World("move",At(F,{.6,.18}),"Ctrl Shift"));
    Check(U.State.Drawing.Pointer.Y==U.State.Drawing.Points.back().Y,"Shift alignment wins over an off-axis Ctrl point snap");
    Script(U,"key X\nframes 1\n"+World("move",At(F,{.15,.8}),"Shift"));
    Check(U.State.Picker.Lock==SolidArcSnapLockKind::X && U.State.Picker.Current.Source.find("Lock")!=std::string::npos,"explicit X direction lock retains precedence over temporary Shift");
}
void Tilted() {
    HeadlessUiHarness U;Arm(U,SolidArcDrawingKind::Line,0,true,false);
    auto N=Normalize(Vector3{1,1,1}),X=Normalize(Vector3{1,-1,0});OrthonormalFrame F{{.2,.3,.4},X,Cross(N,X),N};
    U.State.Drawing.FacePlaneCondition=true;U.State.Drawing.FacePlane=F;U.State.Drawing.Points={{0,0}};
    U.State.Camera.Azimuth=std::atan2(N.Y,N.X);U.State.Camera.Elevation=std::asin(N.Z);U.State.Camera.Target={.2,.3,.4};
    U.StepFrame();Script(U,World("move",At(F,{.7,.14}),"Shift"));
    Check(U.State.Drawing.Pointer.Y==0 && std::abs(Dot(U.State.Picker.Current.Position-F.Origin,N))<1e-10,"arbitrary face plane constrains in its own U/V axes without leaving that plane");
}
void Cutter(bool Imprint) {
    HeadlessUiHarness U;Arm(U,SolidArcDrawingKind::Line,2);
    U.State.Drawing.ActiveCondition=false;
    SolidArcConstructRequest B;B.Position={3,4,5};B.Width=B.Depth=B.Height=2;
    if(!ConstructSolidArcPrimitive(U.State.Document,B).AcceptedCondition)throw std::runtime_error("cutter fixture");
    RefreshSolidArcScene(U.State,false);FitSolidArcCamera(U.State.Camera,U.State.Scene);PresentOrbitView(U.State.Camera,OrbitViewPreset::Right);U.State.Camera.OrthographicEnabled=true;
    U.State.SurfaceTraceEnabled=true;U.State.Knife.PreferredShape=SolidArcDrawingKind::Line;U.State.Knife.PreferredPolylineCondition=true;U.State.Knife.PreferredImprintCondition=Imprint;
    U.StepFrame();Script(U,"key K Shift\nframes 2\n"+World("click",{5,4.4,5.4})+World("click",{5,5.3,5.55},"Shift"));
    auto& D=U.State.Drawing;
    Check(D.Points.size()==2 && D.Points[0].Y==D.Points[1].Y,std::string(Imprint?"imprint":"Knife")+" polyline uses the same Shift alignment");
    Check(D.FacePlaneCondition && D.FacePlane.ZAxis.X>.99 && !D.SurfaceCondition,"cutter Shift stays in clicked surface frame despite tracing preference");
}
void Drag(int Plane) {
    HeadlessUiHarness U;Arm(U,SolidArcDrawingKind::Line,Plane);
    Script(U,World("move",{0,0,0})+"drag Left viewport 0.45 0.45 by 100 30 6 Shift\nframes 2\n");
    auto& D=U.State.Drawing;
    Check(D.Points.size()==2 && D.Points[0].Y==D.Points[1].Y && Distance(D.Points[0],D.Points[1])>.1,"Shift click-drag commits a straight segment in plane "+std::to_string(Plane));
    Check(!D.ActiveCondition && U.State.Document.Regeneration.Refused==0,"click-drag and click-click share preview/commit constraint");
}
void Surface(int Axis) {
    HeadlessUiHarness U;Arm(U,SolidArcDrawingKind::Line,0);
    SolidArcConstructRequest B;B.Position={3,4,5};B.Width=B.Depth=B.Height=2;
    if(!ConstructSolidArcPrimitive(U.State.Document,B).AcceptedCondition)throw std::runtime_error("surface fixture");
    RefreshSolidArcScene(U.State,false);FitSolidArcCamera(U.State.Camera,U.State.Scene);
    PresentOrbitView(U.State.Camera,Axis==0?OrbitViewPreset::Top:Axis==1?OrbitViewPreset::Right:OrbitViewPreset::Front);U.State.Camera.OrthographicEnabled=true;
    U.State.SurfaceTraceEnabled=true;U.State.FullXYZEnabled=true;U.State.Drawing.SurfaceCondition=true;
    Vector3 A=Axis==0?Vector3{4,5,7}:Axis==1?Vector3{5,5,6}:Vector3{4,4,6};
    Vector3 Bp=A+(Axis==0?Vector3{.6,.1,0}:Axis==1?Vector3{0,.6,.1}:Vector3{.1,0,.6});
    U.StepFrame();Script(U,World("click",A)+World("click",Bp,"Shift"));
    auto& D=U.State.Drawing;
    Check(D.Lifted.size()==2 && D.Lifted[0] && D.Lifted[1],"surface line saves exactly two clicked XYZ controls");
    auto V=*D.Lifted[1]-*D.Lifted[0];
    Check(Axis==0?(V.Y==0 && V.Z==0):Axis==1?(V.X==0 && V.Z==0):(V.X==0 && V.Y==0),"surface Shift keeps two world coordinates exact on dominant axis "+std::to_string(Axis));
    Check(!D.ActiveCondition && U.State.Document.Regeneration.Refused==0,"surface-aligned line completes without fitted or inserted points");
}
}
int main(){try{
 for(int Plane=0;Plane<3;++Plane)for(bool Ortho:{true,false})for(auto Delta:{Vector2{.7,.14},Vector2{-.7,.14},Vector2{.14,.7},Vector2{.14,-.7}})Line(Plane,Ortho,Delta);
 for(int Plane=0;Plane<3;++Plane)for(bool V:{true,false}) {ArcSlot(Plane,SolidArcDrawingKind::Arc,V);ArcSlot(Plane,SolidArcDrawingKind::Slot,V);}
 Polyline();Excluded(SolidArcDrawingKind::Curve);Excluded(SolidArcDrawingKind::Rectangle);Excluded(SolidArcDrawingKind::Circle);LockedAndSnapped();Tilted();
 Cutter(false);Cutter(true);for(int Plane=0;Plane<3;++Plane)Drag(Plane);
 for(int Axis=0;Axis<3;++Axis)Surface(Axis);
 std::cout<<"PASS "<<Checks<<" Shift drawing checks\n";return 0;
}catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<"\n";return 1;}}
