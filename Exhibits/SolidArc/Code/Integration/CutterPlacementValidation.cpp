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
        Options.RecoveryPath = (std::filesystem::temp_directory_path() / "SolidArcBatchRecovery.solidarc").string();
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
void Box(HeadlessUiHarness& U,OrbitViewPreset View,bool Ortho) {
    SolidArcConstructRequest B;B.Kind=SolidArcConstructKind::Block;B.Position={3,4,5};B.Width=2;B.Depth=2;B.Height=2;
    if(!ConstructSolidArcPrimitive(U.State.Document,B).AcceptedCondition)throw std::runtime_error("box fixture");
    RefreshSolidArcScene(U.State,false);FitSolidArcCamera(U.State.Camera,U.State.Scene);PresentOrbitView(U.State.Camera,View);U.State.Camera.OrthographicEnabled=Ortho;
    U.State.Picker.FaceOrientEnabled=false;U.State.Picker.GridEnabled=false;U.State.SurfaceTraceEnabled=true;U.State.DrawingCurveSnap=false;U.State.DrawingOptionsVisible=true;
    U.State.Picker.Lock=SolidArcSnapLockKind::X; // Stale lock from the previous sketch.
    U.State.Knife.PreferredShape=SolidArcDrawingKind::Rectangle;
    U.StepFrame();U.StepFrame();
}
void Profile(OrbitViewPreset View,Vector3 P,bool Ortho) {
    HeadlessUiHarness U;Box(U,View,Ortho);
    const double Volume=U.State.Document.Summary.Volume;
    Script(U,"key K Shift\nframes 2\n"+World("move",P));
    const auto Plane=U.State.Drawing.FacePlane;
    const std::string Tag=std::string(Ortho?"ortho ":"perspective ")+std::to_string(int(View));
    Check(U.State.Drawing.ActiveCondition && !U.State.Drawing.SurfaceCondition && U.State.Knife.DrawPendingCondition,Tag+" rectangle starts despite tracing preference");
    Check(U.State.Drawing.FacePlaneCondition && !U.State.Drawing.FaceName.empty() && std::abs(Dot(P-Plane.Origin,Plane.ZAxis))<1e-8,Tag+" hovered surface replaces origin grid plane with face plane");
    Script(U,World("click",P));
    Check(U.State.Drawing.Points.size()==1,Tag+" first click freezes cutter plane");
    const Vector3 Q=P+Plane.XAxis*.35+Plane.YAxis*.25;
    Script(U,World("move",Q));
    Check(Length(U.State.Drawing.FacePlane.Origin-Plane.Origin)<1e-10 && Dot(U.State.Drawing.FacePlane.ZAxis,Plane.ZAxis)>1-1e-10,Tag+" outline stays on first face plane");
    Script(U,World("click",Q));
    Check(!U.State.Knife.Sketch.empty() && !U.State.Knife.Face.empty(),Tag+" completed profile retains face target");
    // World-script pointers are quantized to screen pixels. Compare the Boolean
    // against the actually clicked outline, not an unachievable subpixel rectangle.
    const auto Points=U.State.Drawing.Points;
    if(Points.size()!=2) throw std::runtime_error("rectangle fixture points");
    const double Area=std::abs((Points[1].X-Points[0].X)*(Points[1].Y-Points[0].Y));
    if(Area<.08 || Area>.095) throw std::runtime_error("rectangle click size outside two-pixel bounds");
    const double ExpectedRemoved=Area*.25;
    Script(U,"key 0\nkey Period\nkey 2\nkey 5\nframes 2\nkey Enter\nframes 2\n");
    const double Removed=Volume-U.State.Document.Summary.Volume;
    if(U.State.Knife.Commits!=1 || std::abs(Removed-ExpectedRemoved)>=1e-6)
        std::cerr<<std::setprecision(17)<<"DETAIL removed="<<Removed<<" expected="<<ExpectedRemoved<<" commits="<<U.State.Knife.Commits<<" depth="<<U.State.Knife.Depth<<" status="<<U.State.Status<<" refusal="<<U.State.Knife.Refusal<<"\n";
    Check(U.State.Knife.Commits==1 && U.State.Document.Regeneration.Refused==0 && std::abs(Removed-ExpectedRemoved)<1e-6,Tag+" depth cuts inward from actual face, exact pocket volume");
    Check(U.State.SurfaceTraceEnabled && !U.State.Picker.FaceOrientEnabled,Tag+" no user preference was changed");
    Script(U,"key Escape\nkey Z Ctrl\nframes 2\n");
    Check(std::abs(U.State.Document.Summary.Volume-Volume)<1e-9,Tag+" undo restores uncut body");
}
void Shaped(bool Snap,bool Edge=false) {
    HeadlessUiHarness U;Box(U,OrbitViewPreset::Right,true);
    U.State.Knife.PreferredSolidCondition=true;U.State.Knife.PreferredSolid=SolidArcConstructKind::Cone;
    Script(U,"key K Shift\nframes 2\n"+World("click",{5,Edge?4.0:5.0,6},Snap?"Ctrl":""));
    Check(U.State.Drawing.SolidCondition && U.State.Drawing.FacePlaneCondition && !U.State.Drawing.StartFace.empty(),Snap?"Ctrl-snapped shaped cutter keeps surface frame and target":"shaped cutter auto-orients on side face with preference off");
    Check(std::abs(U.State.Drawing.FacePlane.Origin.X-5)<1e-8 && U.State.Drawing.FacePlane.ZAxis.X>.99,"shaped cutter uses outward side normal, not XY grid");
}
void OrdinaryDrawing() {
    HeadlessUiHarness U;Box(U,OrbitViewPreset::Top,true);
    U.State.SurfaceTraceEnabled=false;U.State.Picker.Lock=SolidArcSnapLockKind::None;
    U.State.Drawing={};U.State.Drawing.ActiveCondition=true;U.State.Drawing.Kind=SolidArcDrawingKind::Rectangle;U.State.Drawing.GridPlane=0;
    Script(U,World("move",{4,5,7}));
    Check(!U.State.Drawing.FacePlaneCondition && !U.State.Knife.ActiveCondition && !U.State.Picker.FaceOrientEnabled,"ordinary drawing still respects disabled face orientation and its chosen grid");
}
void Menu() {
    HeadlessUiHarness U;Box(U,OrbitViewPreset::Right,true);
    Script(U,"click @SketchKnifeTool\nframes 2\n");
    Check(U.State.Widgets.Find("KnifeToolRectangle") && U.State.Widgets.Find("KnifeToolCircle") && U.State.Widgets.Find("KnifeToolPolygon"),"one canonical outline choice for box, cylinder and prism cuts");
    Check(!U.State.Widgets.Find("KnifeToolBlock") && !U.State.Widgets.Find("KnifeToolCylinder") && !U.State.Widgets.Find("KnifeToolPrism"),"duplicate basic solid entries removed from cutter menu");
    Check(U.State.Widgets.Find("KnifeToolCone") && U.State.Widgets.Find("KnifeToolSphere") && U.State.Widgets.Find("KnifeToolTorus") && U.State.Widgets.Find("KnifeToolWedge") && U.State.Widgets.Find("KnifeToolTube"),"distinct shaped cutters retained");
    Check(U.State.Widgets.Find("KnifeFlatRectangle") && U.State.Widgets.Find("KnifeToolObject") && U.State.Widgets.Find("KnifeToolInsert0"),"imprint, object and compound-library capabilities retained");
    Script(U,"click @KnifeToolRectangle\nframes 2\n");
    Check(U.State.Drawing.ActiveCondition && !U.State.Drawing.SolidCondition && U.State.Drawing.Kind==SolidArcDrawingKind::Rectangle,"canonical menu starts profile plus depth workflow");
    Check(!U.State.Widgets.Find("KnifeDrawBlock") && !U.State.Widgets.Find("KnifeDrawCylinder") && !U.State.Widgets.Find("KnifeDrawPrism"),"inspector also removes duplicate basic solids");
    Check(!U.State.Widgets.Find("ToolSurface"),"retopology popup cannot override cutter placement");
}
void Curved() {
    HeadlessUiHarness U;
    SolidArcConstructRequest B;B.Kind=SolidArcConstructKind::Sphere;B.Position={3,4,5};B.Radius=1;
    if(!ConstructSolidArcPrimitive(U.State.Document,B).AcceptedCondition)throw std::runtime_error("sphere fixture");
    RefreshSolidArcScene(U.State,false);FitSolidArcCamera(U.State.Camera,U.State.Scene);PresentOrbitView(U.State.Camera,OrbitViewPreset::Right);U.State.Camera.OrthographicEnabled=true;
    U.State.Picker.FaceOrientEnabled=false;U.State.SurfaceTraceEnabled=true;U.State.Knife.PreferredShape=SolidArcDrawingKind::Circle;
    U.StepFrame();U.StepFrame();Script(U,"key K Shift\nframes 2\n"+World("click",{4,4,5}));
    const auto P=U.State.Drawing.FacePlane;
    Check(U.State.Drawing.FacePlaneCondition && !U.State.Drawing.FaceName.empty() && std::abs(Dot(Vector3{4,4,5}-P.Origin,P.ZAxis))<.01,"curved surface supplies tangent plane with face preference off");
    Check(P.ZAxis.X>.99,"curved cutter normal points out; negative depth goes into body");
}
}
int main(int argc,char** argv) {
 try {
    for(bool Ortho:{true,false}) {
      Profile(OrbitViewPreset::Top,{4,5,7},Ortho);Profile(OrbitViewPreset::Bottom,{4,5,5},Ortho);
      Profile(OrbitViewPreset::Right,{5,5,6},Ortho);Profile(OrbitViewPreset::Left,{3,5,6},Ortho);
      Profile(OrbitViewPreset::Front,{4,4,6},Ortho);Profile(OrbitViewPreset::Back,{4,6,6},Ortho);
    }
    Shaped(false);Shaped(true);Shaped(true,true);Curved();Menu();OrdinaryDrawing();
    if(argc>1) { HeadlessUiHarness U;Box(U,OrbitViewPreset::Right,true);std::ofstream(argv[1])<<EncodeSolidArcDocument(U.State.Document); }
    std::cout<<"PASS "<<Checks<<" surface cutter checks\n";return 0;
 }catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<"\n";return 1;}
}
