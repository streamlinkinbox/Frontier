#include "EditorInspectorSequence.h"
#include "EditorHost.h"
#include "GeometricRaster/VisibilityRaster.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <limits>
using namespace Frontier;using namespace Frontier::ProjectZero;
unsigned Checks=0;
void Check(bool V,const char* N){++Checks;if(!V)throw std::runtime_error(N);std::printf("PASS %s\n",N);}
EditorProperty& Find(EditorSheet& S,const char* N){for(unsigned G=0;G<S.GroupCount;++G)for(unsigned P=0;P<S.Groups[G].PropertyCount;++P)if(!std::strcmp(S.Groups[G].Properties[P].Label,N))return S.Groups[G].Properties[P];throw std::runtime_error(N);}
uint64_t Key(CelestialEntity E){return 0x200000001ull+unsigned(E);}
void Box(SceneStructure& L,const char* Name,float X,float Y,float Z,float SX,float SY,float SZ,float R,float G,float B,float Emission=0){
 MaterialDescriptor M;M.Name=Name;M.Slabs.emplace_back();auto& S=M.Slabs[0];S.BaseColor[0]=R;S.BaseColor[1]=G;S.BaseColor[2]=B;S.SpecularRoughness=.7f;S.EmissionLuminance=Emission;
 const auto Mat=L.RegisterMaterial(M);GeometryStructure Mesh;
 const float V[8][3]={{X-SX,Y-SY,Z},{X+SX,Y-SY,Z},{X+SX,Y+SY,Z},{X-SX,Y+SY,Z},{X-SX,Y-SY,Z+SZ},{X+SX,Y-SY,Z+SZ},{X+SX,Y+SY,Z+SZ},{X-SX,Y+SY,Z+SZ}};
 const unsigned Faces[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{3,7,6,2},{0,4,7,3},{1,2,6,5}};
 const float Normals[6][3]={{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0}};
 for(unsigned F=0;F<6;++F){VertexRecord A[4]{};for(unsigned I=0;I<4;++I){auto* P=V[Faces[F][I]];A[I].SpatialLocation={P[0],P[1],P[2]};A[I].NormalDirection={Normals[F][0],Normals[F][1],Normals[F][2]};}Mesh.AppendVertices(A,4);unsigned Base=F*4,Indices[]={Base,Base+1,Base+2,Base,Base+2,Base+3};Mesh.AppendIndices(Indices,6);}
 Matrix4x4 Identity;auto First=L.RegisterInstance(Mesh,Identity,Mat,InstanceFlagDoubleSided);auto Place=L.RegisterPlacement(Name,0xffffffffu,Identity,Identity);L.AttachInstances(Place,First,1);
}
int main(){try{
 auto Level=std::make_unique<SceneStructure>();
 Box(*Level,"Ground",0,500,-4,1500,1800,4,.25f,.29f,.24f);
 for(int I=0;I<7;++I){float Y=60.f+I*95;Box(*Level,"Distance columns left",-45-I*70,Y,0,7,9,27+I*3,.55f,.43f,.29f);Box(*Level,"Distance columns right",45+I*70,Y,0,7,9,40+I*4,.25f,.36f,.45f);}
 Box(*Level,"Studio fill outside view",-100,30,180,100,100,.1f,1,1,1,18);
 Box(*Level,"Centre plinth",0,145,0,22,22,12,.42f,.43f,.39f);Level->Finalise();
 auto Feed=std::make_unique<EditorFeedSequence>();auto Sky=std::make_unique<CelestialSequence>();Sky->Prepare();
 Sky->Cloud.Coverage=.34f;Sky->Cloud.Density=2.4f;Sky->Cloud.FollowWind=true;
 Sky->LocalCloud.Enabled=true;Sky->LocalFog.Enabled=true;Sky->LocalFog.Centre[0]=75;Sky->LocalFog.Centre[1]=190;Sky->LocalFog.Centre[2]=30;
 Sky->LocalFog.HalfSize[0]=55;Sky->LocalFog.HalfSize[1]=65;Sky->LocalFog.HalfSize[2]=28;Sky->LocalFog.Scale=25;Sky->LocalFog.Coverage=.65f;Sky->LocalFog.Density=.35f;
 Sky->Fog.HeightEnabled=true;Sky->Fog.HeightDensity=.001f;Sky->Fog.FalloffHeight=35;Sky->Fog.AerialEnabled=true;Sky->Fog.AerialDensity=.65f;
 for(auto& V:Sky->Shown)V=true;
 auto Camera=std::make_unique<FlyThroughSolver>();Camera->AssignSpatialLocation({0,-110,26});Camera->AssignOrientationEuler(.075f,0,0);Camera->AssignFieldOfView(58);Camera->AssignAspectRatio(1.3f);
 auto Sheet=std::make_unique<EditorSheet>();auto Rows=std::make_unique<EditorInstance[]>(kMaxEditorInstances);
 unsigned Count=Feed->FillRoster(Rows.get(),*Level),Folder=Count;Count+=Sky->AppendRoster(Rows.get(),Count,kMaxEditorInstances);
 EditorInspectorSequence Session{*Feed,*Sky,*Camera,*Level,Level->QueryInstances(),Rows.get(),Count,*Sheet};
 auto Index=[&](uint64_t K){for(unsigned I=0;I<Count;++I)if(Rows[I].InspectorKey==K)return I;throw std::runtime_error("key missing");};
 auto Edit=[&](CelestialEntity E,const char* Name,float V){auto I=Index(Key(E));Session.Update(I,false);Find(*Sheet,Name).Figure=V;Session.Update(I,true);Session.Update(I,false);Check(std::abs(Find(*Sheet,Name).Figure-V)<.0001f,"sheet commit/fetch roundtrip");};
 auto Switch=[&](CelestialEntity E,const char* Name,bool V){auto I=Index(Key(E));Session.Update(I,false);Find(*Sheet,Name).On=V;Session.Update(I,true);};
 auto Raster=std::make_unique<VisibilityRaster>();CelestialBudget Budget;Budget.AtmosphereSamples=16;Budget.AtmosphereLightSamples=6;Budget.Volumetrics.CloudSteps=128;Budget.Volumetrics.LocalSteps=40;
 constexpr unsigned W=520,H=400;std::vector<unsigned char> Pixels(W*H*4);std::vector<std::pair<std::string,double>> Deltas;
 auto Render=[&](const char* Name){Session.Synchronize();Sky->ApplyTo(*Raster,Budget);auto E=Camera->QuerySpatialLocation(),F=Camera->QueryForwardVector(),R=Camera->QueryRightVector(),U=Camera->QueryUpwardVector();float Eye[]={E.x,E.y,E.z},Forward[]={F.x,F.y,F.z},Right[]={R.x,R.y,R.z},Up[]={U.x,U.y,U.z};double Mean=0;Check(Raster->Render(*Level,Eye,Forward,Right,Up,Camera->QueryFieldOfViewRadians(),W,H,Pixels.data(),Mean),"real VisibilityRaster scene rendered");if(Name){std::string Path="Exhibits/Gallery/NativeBillboards/";Path+=Name;Path+=".png";std::vector<unsigned char> RGB(W*H*3);for(unsigned I=0;I<W*H;++I)for(unsigned K=0;K<3;++K)RGB[I*3+K]=Pixels[I*4+K];Check(stbi_write_png(Path.c_str(),W,H,3,RGB.data(),W*3)!=0,"render saved");}return Pixels;};
 auto Difference=[&](const char* Name,const auto& A,const auto& B,bool Change=true){double Sum=0;for(size_t I=0;I<A.size();++I)if(I%4!=3)Sum+=std::abs(int(A[I])-int(B[I]));const double Mean=Sum/(W*H*3);Deltas.push_back({Name,Mean});std::printf("DELTA %s %.6f /255\n",Name,Mean);Check(Change?Mean>.02:Mean==0,Name);};
 auto Base=Render("Scene");
 Edit(CelestialEntity::Sky,"Mie",4);auto Haze=Render("Atmosphere-after");Check(Raster->QueryCelestial().Medium.MieStrength==4,"inspector Mie reaches renderer settings");Difference("Atmosphere Mie",Base,Haze);Edit(CelestialEntity::Sky,"Mie",1);
 auto CloudBefore=Render("Clouds-before");Edit(CelestialEntity::CloudLayer,"Coverage",.65f);auto CloudAfter=Render("Clouds-after");Difference("Cloud coverage",CloudBefore,CloudAfter);Edit(CelestialEntity::CloudLayer,"Coverage",.34f);
 auto FogBefore=Render("Fog-before");Edit(CelestialEntity::HeightFog,"Density",.012f);auto FogAfter=Render("Fog-after");Difference("Height fog over real distance geometry",FogBefore,FogAfter);Edit(CelestialEntity::HeightFog,"Density",.001f);
 auto LocalBefore=Render("Local-fog-before");Edit(CelestialEntity::LocalFog,"Density",3);auto LocalAfter=Render("Local-fog-after");Difference("Local volumetric fog",LocalBefore,LocalAfter);Edit(CelestialEntity::LocalFog,"Density",.35f);
 auto LocalCloudBefore=Render("Local-cloud-before");Edit(CelestialEntity::LocalCloud,"Density",0);auto LocalCloudAfter=Render("Local-cloud-after");Difference("Local cloud density",LocalCloudBefore,LocalCloudAfter);Edit(CelestialEntity::LocalCloud,"Density",2.5f);
 Edit(CelestialEntity::Wind,"Speed",18);Sky->WeatherSeconds=0;auto WindBefore=Render("Wind-t0");const float Hours=Sky->Observation.LocalHours;float Eye[]={0,-110,26};Sky->Tick(45,Eye,0);Check(Sky->Observation.LocalHours==Hours,"Static Sun remains static while weather advances");auto WindAfter=Render("Wind-t45");Difference("Wind advection with Static Sun",WindBefore,WindAfter);Check(Raster->QueryCelestial().CloudTime==45&&Raster->QueryCelestial().Wind.Speed==18,"wind speed and simulation time reach march");
 Edit(CelestialEntity::Wind,"Speed",0);Sky->WeatherSeconds=0;auto Still=Render(nullptr);Sky->Tick(45,Eye,0);auto StillLater=Render(nullptr);Difference("Zero wind is stationary",Still,StillLater,false);
 Edit(CelestialEntity::Wind,"Speed",18);Rows[Index(Key(CelestialEntity::Wind))].Visible=false;Render(nullptr);Check(Raster->QueryCelestial().Wind.Speed==0,"hidden Wind does not advect media");Rows[Index(Key(CelestialEntity::Wind))].Visible=true;
 Sky->WeatherSeconds=0;Render(nullptr);
 EditorBillboardCamera C;EditorBillboard M;M.World[1]=10;C.Aspect=1.3f;auto P=ViewportBillboards::Project(M,C,520,400);Check(P.Visible&&P.X==260&&P.Y==200,"world projection centre");M.World[1]=-1;Check(!ViewportBillboards::Project(M,C,520,400).Visible,"behind-camera clipping");M.World[1]=.001f;Check(!ViewportBillboards::Project(M,C,520,400).Visible,"near clipping");M.World[1]=10;M.World[0]=1000;Check(!ViewportBillboards::Project(M,C,520,400).Visible,"offscreen clipping");M.World[0]=std::numeric_limits<float>::quiet_NaN();Check(!ViewportBillboards::Project(M,C,520,400).Visible,"nonfinite clipping");Check(!ViewportBillboards::Project(M,C,0,0).Visible,"zero viewport is safe");
 ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={1600,1000};IO.DeltaTime=1.f/60;IO.ConfigFlags|=ImGuiConfigFlags_DockingEnable;IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 auto Host=std::make_unique<EditorHost>();Host->ApplyTheme();Host->AssignInspectorWorkspace(false);Host->AssignInspectorExchange(&EditorInspectorSequence::Exchange,&Session);Host->AssignBillboardExchange(&EditorInspectorSequence::Billboards,&Session);Host->AssignView(Pixels.data(),W,H);
 bool BlockInput=false,CloseModal=false;
 auto Tick=[&](){ImGui::NewFrame();
 if(BlockInput)ImGui::OpenPopup("Input ownership proof");
 if(ImGui::BeginPopupModal("Input ownership proof",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Modal owns input");if(CloseModal)ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 Host->Record(Rows.get(),Count,Sheet.get());ImGui::Render();FrontierProof::AcknowledgeTextures();};
 auto Click=[&](ImVec2 At){Check(At.x>=0&&At.y>=0,"billboard or native control in view");IO.AddMousePosEvent(At.x,At.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();};
 auto Capture=[&](const char* Name){std::vector<unsigned char> RGB(1600*1000*3,24);for(auto* D:ImGui::GetDrawData()->CmdLists)FrontierProof::Draw(D,RGB.data(),1600,1000,{0,0},{1,1},ImTextureID(reinterpret_cast<uintptr_t>(Pixels.data())),{Pixels.data(),W,H,4});std::string Path="Exhibits/Gallery/NativeBillboards/";Path+=Name;Path+=".png";Check(stbi_write_png(Path.c_str(),1600,1000,3,RGB.data(),1600*3)!=0,"real native editor capture");};
 for(int I=0;I<5;++I)Tick();
 EditorBillboard TestMarkers[32];EditorBillboardCamera TestCamera;TestCamera.ViewWidth=800;TestCamera.ViewHeight=600;
 const auto MarkerCount=EditorInspectorSequence::Billboards(TestMarkers,32,TestCamera,&Session);
 for(unsigned K=0;K<MarkerCount;++K)Check(TestMarkers[K].Artwork!=IconSymbol::Count,"every billboard resolves a semantic icon");
 // The existing Markers header toggle now controls both painting and picking.
 auto* View=ImGui::FindWindowByName("Viewport");Check(View!=nullptr,"native viewport exists");
 const ImVec2 MarkerToggle(View->Pos.x+200,View->Pos.y+63);
 Click(MarkerToggle);Check(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)).x<0,"Markers toggle hides proxies");
 Click(MarkerToggle);Check(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)).x>=0,"Markers toggle restores proxies");
 for(auto Entity:{CelestialEntity::Wind,CelestialEntity::HeightFog,CelestialEntity::AtmosphericFog,CelestialEntity::CloudLayer,CelestialEntity::Sky,CelestialEntity::LocalCloud,CelestialEntity::LocalFog}){
  Click(Host->QueryBillboardCentre(Key(Entity)));Check(Rows[Host->QueryPickedInstance()].InspectorKey==Key(Entity),"billboard selects matching outliner row");Check(Sheet->InspectorKey==Key(Entity),"billboard mounts real matching inspector");float U,V;bool Add;Check(!Host->QueryViewTap(&U,&V,&Add),"billboard click consumed before mesh picker");Check(Host->TakeBillboardSelection(),"billboard cancels older asynchronous mesh selection");Check(!Host->TakeBillboardSelection(),"selection notification consumed once");
 }
 Click(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)));Capture("Editor-Wind");
 ImGuiWindow* Slider=nullptr;for(auto* Win:ImGui::GetCurrentContext()->Windows)if(Win->Active&&std::strstr(Win->Name,"/Speed_"))Slider=Win;Check(Slider!=nullptr,"actual native Wind slider mounted");float Old=Sky->Wind.Speed;Click({Slider->Pos.x+Slider->Size.x*.7f,Slider->Pos.y+15});Check(Sky->Wind.Speed!=Old,"native mouse edit commits Wind speed");Render(nullptr);Check(Raster->QueryCelestial().Wind.Speed==Sky->Wind.Speed,"native mouse edit propagated to rendered scene");Tick();Capture("Editor-Wind-edited");
 Click(Host->QueryBillboardCentre(Key(CelestialEntity::HeightFog)));Capture("Editor-Fog");
 auto FogMouseBefore=Render("Fog-mouse-before");
 Slider=nullptr;for(auto* Win:ImGui::GetCurrentContext()->Windows)if(Win->Active&&std::strstr(Win->Name,"/Density_"))Slider=Win;
 Check(Slider!=nullptr,"actual native Height Fog density slider mounted");
 Click({Slider->Pos.x+Slider->Size.x*.65f,Slider->Pos.y+15});auto FogMouseAfter=Render("Fog-mouse-after");
 Difference("Native mouse Fog edit changes rendered pixels",FogMouseBefore,FogMouseAfter);Tick();Capture("Editor-Fog-edited");Edit(CelestialEntity::HeightFog,"Density",.001f);Render(nullptr);Tick();
 Click(Host->QueryBillboardCentre(Key(CelestialEntity::Sky)));Capture("Editor-Atmosphere");
 Click(Host->QueryBillboardCentre(Key(CelestialEntity::LocalCloud)));Capture("Editor-Local-cloud");
 // A modal above the viewport owns its clicks, not the scene or the markers beneath it.
 BlockInput=true;Tick();Tick();const auto PickBefore=Host->QueryPickedInstance();
 Click(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)));Check(Host->QueryPickedInstance()==PickBefore,"modal blocks billboard selection");
 float TapU,TapV;bool Additive;Check(!Host->QueryViewTap(&TapU,&TapV,&Additive),"modal blocks mesh selection too");
 BlockInput=false;CloseModal=true;Tick();CloseModal=false;Tick();
 // Overlapping local markers: nearest visible proxy wins, independent of roster order.
 float CloudCentre[3],FogCentre[3];std::memcpy(CloudCentre,Sky->LocalCloud.Centre,sizeof(CloudCentre));std::memcpy(FogCentre,Sky->LocalFog.Centre,sizeof(FogCentre));
 const auto EF=Camera->QueryForwardVector(),EE=Camera->QuerySpatialLocation();
 Sky->LocalCloud.Centre[0]=EE.x+EF.x*120;Sky->LocalCloud.Centre[1]=EE.y+EF.y*120;Sky->LocalCloud.Centre[2]=EE.z+EF.z*120;
 Sky->LocalFog.Centre[0]=EE.x+EF.x*60;Sky->LocalFog.Centre[1]=EE.y+EF.y*60;Sky->LocalFog.Centre[2]=EE.z+EF.z*60;
 Tick();Click(Host->QueryBillboardCentre(Key(CelestialEntity::LocalCloud)));Check(Sheet->InspectorKey==Key(CelestialEntity::LocalFog),"nearest overlapping local billboard wins");
 std::memcpy(Sky->LocalCloud.Centre,CloudCentre,sizeof(CloudCentre));std::memcpy(Sky->LocalFog.Centre,FogCentre,sizeof(FogCentre));Tick();
 // Identity remains correct after reorder and rename; no sprite stores a row ordinal.
 auto A=Index(Key(CelestialEntity::Wind)),B=Index(Key(CelestialEntity::HeightFog));std::swap(Rows[A],Rows[B]);Tick();Click(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)));Check(Host->QueryPickedInstance()==B&&Sheet->InspectorKey==Key(CelestialEntity::Wind),"reordered billboard resolves stable identity");
 std::snprintf(Rows[B].Label,sizeof(Rows[B].Label),"North wind");Tick();Click(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)));Check(Sheet->InspectorKey==Key(CelestialEntity::Wind),"rename preserves identity");
 Rows[Folder].Visible=false;Tick();Check(Host->QueryBillboardCentre(Key(CelestialEntity::Wind)).x<0&&Host->QueryBillboardCentre(Key(CelestialEntity::LocalFog)).x<0,"hidden ancestor suppresses global and local proxies");Rows[Folder].Visible=true;Tick();
 // Move a local centre through the same inspector exchange, not a second marker transform.
 auto I=Index(Key(CelestialEntity::LocalFog));Session.Update(I,false);auto& Centre=Find(*Sheet,"Centre");Centre.Axes[0]=110;Session.Update(I,true);Tick();Check(Sky->LocalFog.Centre[0]==110,"local volume inspector owns marker position");
 Host.reset();for(auto* T:ImGui::GetPlatformIO().Textures){T->SetTexID(ImTextureID_Invalid);T->SetStatus(ImTextureStatus_Destroyed);}ImGui::DestroyContext();
 std::FILE* Report=std::fopen("Exhibits/Gallery/NativeBillboards/Deltas.tsv","w");for(const auto& [N,D]:Deltas)std::fprintf(Report,"%s\t%.6f\n",N.c_str(),D);std::fclose(Report);
 std::printf("PASS %u checks. Real native CPU renderer and ImGui controls; Windows/Vulkan execution not tested.\n",Checks);return 0;
}catch(const std::exception& E){std::fprintf(stderr,"FAIL after %u checks: %s\n",Checks,E.what());return 1;}}
