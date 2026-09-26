#include "WeatherInspectorPanel.h"
#include "ControlPanel.h"
#include "SunReferenceDraw.h"
#include "WeatherDiagnostics.h"
#include "InspectorReferenceDraw.h"
#include "SunColourTemperature.h"
#include <array>
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
namespace Frontier { namespace {
using namespace SunReference;
constexpr ImU32 Ink=IM_COL32(233,233,233,255),Muted=IM_COL32(145,145,145,255);
EditorProperty* Find(EditorSheet& S,const char* N){for(auto& G:S.Groups)for(unsigned I=0;I<G.PropertyCount;++I)if(!std::strcmp(G.Properties[I].Label,N))return &G.Properties[I];return nullptr;}
struct Panel {
 ControlPanel& Controls;EditorSheet& Sheet;ImDrawList* D;ImVec2 O;ImFont* Font;
 ImVec2 At(float X,float Y){return {O.x+X,O.y+Y};}
 void Text(float X,float Y,const char* T,float Size=12,ImU32 C=Ink){D->AddText(Font,Size,At(X,Y),C,T);}
 void Wrap(float X,float Y,float W,const char* T){D->AddText(Font,11,At(X,Y),Muted,T,nullptr,W);}
 void Card(float X,float Y,float W,float H,const char* Title){int Start=D->VtxBuffer.Size;D->AddRectFilled(At(X,Y),At(X+W,Y+H),IM_COL32_WHITE,22);ImGui::ShadeVertsLinearColorGradientKeepAlpha(D,Start,D->VtxBuffer.Size,At(X,Y),At(X+W,Y+H),IM_COL32(37,37,37,255),IM_COL32(32,32,32,255));D->AddRect(At(X,Y),At(X+W,Y+H),IM_COL32(52,52,52,255),22);Text(X+24,Y+23,Title);}
 void Slider(float X,float Y,float W,const char* N){auto& P=*Find(Sheet,N);Text(X,Y,N,11,Muted);ImGui::SetCursorScreenPos(At(X,Y+22));ImGui::PushID(N);ImGui::BeginChild(N,{W,30},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);Controls.SliderPill("##value",&P.Figure,P.Minimum,P.Maximum,P.Decimals,P.Unit,false,false,true);ImGui::EndChild();ImGui::PopID();}
 void Tile(float X,float Y,float W,const char* N,bool* Value){ImGui::SetCursorScreenPos(At(X,Y));ImGui::BeginDisabled(!Value);if(ImGui::InvisibleButton(N,{W,65})&&Value)*Value=!*Value;ImGui::EndDisabled();bool On=Value&&*Value;ImU32 C=!Value?Muted:On?IM_COL32(131,207,158,255):IM_COL32(221,137,137,255);D->AddRectFilled(At(X,Y),At(X+W,Y+65),IM_COL32(34,34,34,255),13);D->AddRect(At(X,Y),At(X+W,Y+65),IM_COL32(56,56,56,255),13);if(Value){D->AddCircle(At(X+W/2,Y+20),7,C,20,1.4f);D->AddLine(At(X+W/2,Y+10),At(X+W/2,Y+18),C,1.5f);}else{float M=X+W/2;D->AddRect(At(M-7,Y+15),At(M+7,Y+28),C,2);D->AddLine(At(M-11,Y+12),At(M+2,Y+12),C,1.4f);D->AddLine(At(M-2,Y+8),At(M+2,Y+12),C,1.4f);D->AddLine(At(M-2,Y+16),At(M+2,Y+12),C,1.4f);}float L=Font->CalcTextSizeA(10,10000,0,N).x;Text(X+(W-L)/2,Y+42,N,10,C);}
};
constexpr float Pi=3.14159265359f;
constexpr ImU32 Accent=IM_COL32(161,207,183,255);
float Value(Panel& U,const char* N){return Find(U.Sheet,N)->Figure;}
WindSettings WindOf(Panel& U,bool Authored){WindSettings W;const auto& P=U.Sheet.WeatherPreview;W.Speed=P.Wind[0];W.Bearing=P.Wind[1];W.Shear=P.Wind[2];W.Veer=P.Wind[3];W.Gust=P.Wind[4];W.Turbulence=P.Wind[5];W.Steadiness=P.Wind[6];W.GustPhase=P.Wind[7];if(Authored){W.Speed=Value(U,"Speed");W.Bearing=Value(U,"Bearing");W.Shear=Value(U,"Shear");W.Veer=Value(U,"Veer");W.Gust=Value(U,"Gust");W.Turbulence=Value(U,"Turbulence");W.Steadiness=Value(U,"Steadiness");}return W;}
void Arrow(Panel& U,float X,float Y,float DX,float DY,ImU32 C){float L=std::sqrt(DX*DX+DY*DY);U.D->AddLine(U.At(X,Y),U.At(X+DX,Y+DY),C,2);if(L>5){float UX=DX/L,UY=DY/L;U.D->AddTriangleFilled(U.At(X+DX,Y+DY),U.At(X+DX-UX*9-UY*4,Y+DY-UY*9+UX*4),U.At(X+DX-UX*9+UY*4,Y+DY-UY*9-UX*4),C);}}
void End(Panel& U,float W,float Y,const char* Note){U.Wrap(0,Y,W,Note);ImGui::SetCursorScreenPos(U.At(0,Y+88));ImGui::Dummy({W,1});}
void WindPanel(Panel& U,float W){
 U.Text(0,83,"WIND / AIR · shared advection field",10,Muted);char T[160];
 bool Wide=W>=760;float CW=Wide?(W-16)/2:W,X2=Wide?CW+16:0,Y2=Wide?110:656;
 U.Card(0,110,CW,530,"Direction + magnitude");float R=std::min(142.f,(CW-76)*.5f),CX=CW*.5f,CY=342;
 ImGui::SetCursorScreenPos(U.At(CX-R,CY-R));ImGui::InvisibleButton("##wind-vector",{2*R,2*R},ImGuiButtonFlags_EnableNav);
 auto& Speed=Find(U.Sheet,"Speed")->Figure;auto& Bearing=Find(U.Sheet,"Bearing")->Figure;
 if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){float DX=ImGui::GetIO().MousePos.x-U.At(CX,CY).x,DY=U.At(CX,CY).y-ImGui::GetIO().MousePos.y;float Length=std::sqrt(DX*DX+DY*DY);Speed=std::clamp(Length/R*40,0.f,40.f);if(Length>1)Bearing=std::fmod(std::atan2(DX,DY)*180/Pi+360,360.f);}
 if(ImGui::IsItemFocused()){if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))Bearing=std::fmod(Bearing+359,360.f);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))Bearing=std::fmod(Bearing+1,360.f);if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))Speed=std::min(40.f,Speed+.5f);if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))Speed=std::max(0.f,Speed-.5f);if(ImGui::IsKeyPressed(ImGuiKey_Home))Speed=0;}
 WindSettings A=WindOf(U,true);for(int I=1;I<=4;++I)U.D->AddCircle(U.At(CX,CY),R*I/4,IM_COL32(91,121,110,55),64,1);
 U.D->AddLine(U.At(CX-R,CY),U.At(CX+R,CY),IM_COL32(91,121,110,55));U.D->AddLine(U.At(CX,CY-R),U.At(CX,CY+R),IM_COL32(91,121,110,55));
 U.Text(CX-4,CY-R-23,"N",12,Accent);U.Text(CX+R+10,CY-6,"E",11,Muted);U.Text(CX-4,CY+R+12,"S",11,Muted);U.Text(CX-R-21,CY-6,"W",11,Muted);
 float Rad=Bearing*Pi/180,Length=Speed/40*R;Arrow(U,CX,CY,std::sin(Rad)*Length,-std::cos(Rad)*Length,Accent);U.D->AddCircleFilled(U.At(CX,CY),4,Accent);
 std::snprintf(T,sizeof(T),"%.1f m/s   /   %.0f°",double(Speed),double(Bearing));U.Text(24,163,T,24);
 U.Wrap(24,535,CW-48,"Drag outward for speed, around for bearing. Blows TOWARD N = +Y, E = +X. Outer ring = 40 m/s.");U.Wrap(24,587,CW-48,"Arrow keys: bearing / speed · Home: calm");
 U.Card(X2,Y2,CW,530,"Flow");const char* Names[]={"Speed","Bearing","Shear","Veer"};for(int I=0;I<4;++I)U.Slider(X2+24,Y2+65+I*83,CW-48,Names[I]);unsigned Force=WindField::BeaufortForce(Speed);std::snprintf(T,sizeof(T),"Beaufort %u · %s",Force,WindField::BeaufortName(Force));U.Wrap(X2+24,Y2+435,CW-48,T);U.Wrap(X2+24,Y2+477,CW-48,"Shear changes speed with altitude; veer turns the flow clockwise per kilometre.");
 float Y=Y2+546,Y3=Wide?Y:Y+388;U.Card(0,Y,CW,372,"Variation");const char* Variation[]={"Gust","Turbulence","Steadiness"};for(int I=0;I<3;++I)U.Slider(24,Y+65+I*83,CW-48,Variation[I]);
 U.Card(X2,Y3,CW,372,"Gust envelope");float L=X2+24,RR=X2+CW-24,Top=Y3+82,Bottom=Y3+220;float Previous=0;for(int I=0;I<=96;++I){auto Q=A;Q.GustPhase=I/96.f*2*Pi;float G=WindField::SampleGust(Q);float YY=Bottom-(G-.0f)/2*(Bottom-Top);if(I)U.D->AddLine(U.At(L+(RR-L)*(I-1)/96,Previous),U.At(L+(RR-L)*I/96,YY),Accent,1.5f);Previous=YY;}
 U.Wrap(L,Y3+242,CW-48,"Actual shared gust model · phase 0–2π · vertical scale 0–2×. Steadiness scales variation in this backend: 0 removes gust and swirl.");float V[3];WindField::SampleStep(A,1000,V);std::snprintf(T,sizeof(T),"1 km flow: X %.1f / Y %.1f m/s",double(V[0]),double(V[1]));U.Wrap(L,Y3+315,CW-48,T);
 End(U,W,Y3+398,"One shared wind field drives cloud advection and precipitation. The compass edits authored flow; the envelope is a model diagnostic, not a scene-camera preview.");
}
void Particle(Panel& U,float X,float Y,float Radius,uint32_t Kind){
 if(Kind==3){for(int I=0;I<6;++I){float A=I*Pi/3;float DX=std::sin(A),DY=std::cos(A);U.D->AddLine(U.At(X,Y),U.At(X+DX*Radius,Y+DY*Radius),IM_COL32(218,230,235,255),1.6f);for(int K:{-1,1})U.D->AddLine(U.At(X+DX*Radius*.6f,Y+DY*Radius*.6f),U.At(X+DX*Radius*.4f+DY*Radius*.2f*K,Y+DY*Radius*.4f-DX*Radius*.2f*K),IM_COL32(218,230,235,255),1.2f);}}
 else if(Kind==2||Kind==4){ImVec2 P[7];for(int I=0;I<7;++I){float A=I*2*Pi/7,R=Radius*(I%2?.86f:1.f);P[I]=U.At(X+std::cos(A)*R,Y+std::sin(A)*R);}U.D->AddConvexPolyFilled(P,7,IM_COL32(206,216,219,255));U.D->AddCircleFilled(U.At(X-Radius*.25f,Y-Radius*.25f),Radius*.3f,IM_COL32(236,240,240,150),16);}
 else{U.D->AddEllipseFilled(U.At(X,Y),{Radius,Radius*.8f},IM_COL32(116,163,184,150),0,32);U.D->AddEllipse(U.At(X,Y),{Radius,Radius*.8f},IM_COL32(172,219,233,255),0,32,1.3f);U.D->AddLine(U.At(X-Radius*.4f,Y-Radius*.25f),U.At(X+Radius*.2f,Y-Radius*.45f),IM_COL32(222,244,248,230),1.8f);}
}
void PrecipPanel(Panel& U,float W){
 U.Text(0,83,"CLOUDS / PRECIPITATION · world-space simulation",10,Muted);char T[200];
 int Columns=W>=760?4:2;float TileW=(W-48-(Columns-1)*10)/Columns,H=Columns==4?151:224;
 U.Card(0,110,W,H,"Emission + collision");const char* Switches[]={"Enabled","Follow Wind","Spawn from Clouds","Ground Collision"};for(int I=0;I<4;++I)U.Tile(24+(I%Columns)*(TileW+10),164+(I/Columns)*73,TileW,Switches[I],&Find(U.Sheet,Switches[I])->On);
 float Y=110+H+16;U.Card(0,Y,W,270,"Precipitation type");auto& Type=Find(U.Sheet,"Precipitation")->Picked;const unsigned Order[]={0,3,2,1,4};const char* Names[]={"Rain","Drizzle","Hail","Snow","Sleet"};float TW=(W-64)/3;
 for(int I=0;I<5;++I){float X=24+(I%3)*(TW+8),YY=Y+60+(I/3)*94;unsigned K=Order[I];ImGui::SetCursorScreenPos(U.At(X,YY));if(ImGui::InvisibleButton(Names[K],{TW,84}))Type=K;U.D->AddRectFilled(U.At(X,YY),U.At(X+TW,YY+84),IM_COL32(29,32,33,255),12);U.D->AddRect(U.At(X,YY),U.At(X+TW,YY+84),Type==K?Accent:IM_COL32(59,62,63,255),12);InspectorReference::WeatherType(Fit(U.D,U.At(X+TW*.5f-12,YY+17),{24,24},24,24),K,Type==K?IM_COL32(189,213,239,255):IM_COL32(139,155,173,255));float TextW=U.Font->CalcTextSizeA(11,10000,0,Names[K]).x;U.Text(X+(TW-TextW)/2,YY+59,Names[K],11,Type==K?Accent:Muted);}
 bool Wide=W>=760;float CW=Wide?(W-16)/2:W,X2=Wide?CW+16:0;Y+=286;float Y2=Wide?Y:Y+462;
 U.Card(0,Y,CW,446,"Fall + density");const char* Fields[]={"Intensity","Density","Particle Size","Wind Drift"};for(int I=0;I<4;++I)U.Slider(24,Y+65+I*83,CW-48,Fields[I]);
 U.Card(X2,Y2,CW,446,"Particle scale");auto P=PhysicsFor(static_cast<PrecipitationCategory>(Type));float Diameter=P.RadiusMetres*Value(U,"Particle Size")*2000;
 std::snprintf(T,sizeof(T),"%.2f mm",double(Diameter));U.Text(X2+24,Y2+66,T,29);U.Wrap(X2+24,Y2+108,CW-48,"Nominal diameter · type radius × size scale");Particle(U,X2+CW*.5f,Y2+195,Diameter/30*60,Type);
 float L=X2+CW*.5f-60,R=X2+CW*.5f+60;U.D->AddLine(U.At(L,Y2+265),U.At(R,Y2+265),IM_COL32(151,173,180,110));for(int I=0;I<=6;++I)U.D->AddLine(U.At(L+(R-L)*I/6,Y2+260),U.At(L+(R-L)*I/6,Y2+272),IM_COL32(151,173,180,150));U.Text(L,Y2+282,"0",10,Muted);U.Text(R-36,Y2+282,"30 mm",10,Muted);
 std::snprintf(T,sizeof(T),"Terminal speed %.1f m/s · drag %.1f /s",double(P.TerminalVelocity),double(P.DragRate));U.Wrap(X2+24,Y2+323,CW-48,T);U.Wrap(X2+24,Y2+367,CW-48,"Magnified physical-size reference, not a particle count preview. Size scale changes drawn size; the backend uses type-specific fall speeds.");
 Y=Y2+462;U.Card(0,Y,W,290,"Simulation + settling");U.Slider(24,Y+64,std::min(390.f,W-48),"Accumulation");const auto& Live=U.Sheet.WeatherPreview;std::snprintf(T,sizeof(T),"%u live particles · deepest settled field %.3f m",Live.Alive,double(Live.SnowDepth));U.Wrap(24,Y+137,W-48,T);auto Wind=WindOf(U,false);float Velocity[3];WindField::SampleStep(Wind,2,Velocity);float Gain=Find(U.Sheet,"Follow Wind")->On?Value(U,"Wind Drift"):0;std::snprintf(T,sizeof(T),"Base wind target at 2 m: X %.2f / Y %.2f m/s",double(Velocity[0]*Gain),double(Velocity[1]*Gain));U.Wrap(24,Y+173,W-48,T);U.Wrap(24,Y+212,W-48,Live.AboveWeather?"Above weather ceiling: emitter gated off.":"Cloud-source and altitude gates apply in the simulation. Accumulation uses a bounded depth field, not permanently retained particles. No surface-wetness control.");
 End(U,W,Y+313,"Actual CPU particle simulation and telemetry. Native particle rendering in the scene viewport is not verified here; these cards do not pretend to be a live weather-camera render.");
}
struct BowCache {std::array<float,7> Key{};ImTextureData Image;bool Valid=false;BowCache(){Image.Create(ImTextureFormat_RGBA32,768,448);Image.UseColors=true;std::memset(Image.Pixels,0,768*448*4);ImGui::RegisterUserTexture(&Image);}};
void Cleanup(ImGuiContext*,ImGuiContextHook* H){auto* C=static_cast<BowCache*>(H->UserData);ImGui::UnregisterUserTexture(&C->Image);delete C;}
BowCache& Cache(){constexpr ImGuiID Owner=0x57454154;for(auto& H:ImGui::GetCurrentContext()->Hooks)if(H.Owner==Owner)return *static_cast<BowCache*>(H.UserData);auto* P=new BowCache;ImGuiContextHook H;H.Owner=Owner;H.Type=ImGuiContextHookType_Shutdown;H.Callback=Cleanup;H.UserData=P;ImGui::AddContextHook(ImGui::GetCurrentContext(),&H);return *P;}
void RainbowPanel(Panel& U,float W){
 U.Text(0,83,"RAINBOW · liquid-water optics",10,Muted);char T[160];
 U.Card(0,110,W,W<420?228:167,"Bake / image");float TW=W<420?(W-56)/2:std::min(160.f,(W-64)/3);U.Tile(24,164,TW,"Bake",nullptr);U.Tile(32+TW,164,TW,"Use baked image",nullptr);U.Wrap(24,W<420?248:239,W-48,"No Rainbow bake or image-playback path exists in this target.");
 float Y=W<420?354:293;U.Card(0,Y,W,154,"Visibility");float BW=std::min(180.f,(W-58)/2);U.Tile(24,Y+54,BW,"Enabled",&Find(U.Sheet,"Enabled")->On);U.Tile(34+BW,Y+54,BW,"Alexander's Band",&Find(U.Sheet,"Alexander's Band")->On);
 RainbowSettings B;B.Enabled=Find(U.Sheet,"Enabled")->On;B.AlexanderBand=Find(U.Sheet,"Alexander's Band")->On;B.Intensity=Value(U,"Intensity");B.Width=Value(U,"Width");B.SecondaryGain=Value(U,"Secondary");B.MinimumPathMetres=Value(U,"Minimum Path");
 Y+=170;float PH=(W-48)*112/192;U.Card(0,Y,W,PH+163,"Optical preview");auto& C=Cache();std::array<float,7> Key={float(B.Enabled),B.Intensity,B.Width,B.SecondaryGain,float(B.AlexanderBand),B.MinimumPathMetres,1};if(!C.Valid||C.Key!=Key){C.Key=Key;C.Valid=true;for(int J=0;J<C.Image.Height;++J)for(int I=0;I<C.Image.Width;++I){float RGB[3];WeatherDiagnostics::RainbowPixel(B,(I+.5f)/C.Image.Width,(J+.5f)/C.Image.Height,RGB);auto* P=C.Image.Pixels+(J*C.Image.Width+I)*4;for(int K=0;K<3;++K)P[K]=static_cast<unsigned char>(255*SunColourTemperature::DisplayChannel(RGB[K])+.5f);P[3]=255;}ImTextureDataQueueUpload(&C.Image,0,0,C.Image.Width,C.Image.Height);}
 U.D->AddImageRounded(C.Image.GetTexRef(),U.At(24,Y+59),U.At(W-24,Y+59+PH),{0,0},{1,1},IM_COL32_WHITE,12);
 U.Wrap(24,Y+PH+77,W-48,"Shared spectral kernel · sun 10° behind viewer · rain visibility 1 · rain path 500 m. Fixed test conditions, not the scene camera.");
 Y+=PH+179;U.Card(0,Y,W,458,"Bow response");float SW=std::min(540.f,W-48);const char* Fields[]={"Intensity","Width","Secondary","Minimum Path"};for(int I=0;I<4;++I)U.Slider(24,Y+66+I*78,SW,Fields[I]);std::snprintf(T,sizeof(T),"Current authored rain visibility: %.0f%%",double(U.Sheet.WeatherPreview.RainVisibility*100));U.Wrap(24,Y+391,W-48,T);
 End(U,W,Y+482,"Rain and drizzle feed the existing GPU Rainbow post record; snow, hail and sleet do not. CPU optical preview and shader compilation are verified separately from GPU execution. Bake stays unavailable.");
}
}
void RecordWeatherInspector(ControlPanel& Controls,EditorInstance&,EditorSheet& Sheet){
 auto* Font=ImGui::GetFont();for(auto* F:ImGui::GetIO().Fonts->Fonts)if(!std::strcmp(F->GetDebugName(),"Sun reference / regular"))Font=F;
 ImGui::PushFont(Font,14);ImGui::PushID(static_cast<int>(Sheet.Appearance));ImVec2 O=ImGui::GetCursorScreenPos();O.x+=20;O.y+=20;float W=std::max(240.f,ImGui::GetContentRegionAvail().x-40);Panel U{Controls,Sheet,ImGui::GetWindowDrawList(),O,Font};
 U.Text(0,8,"Inspector / Environment",8,Muted);U.Text(0,48,Sheet.Appearance==EditorSheetAppearance::Wind?"Wind":Sheet.Appearance==EditorSheetAppearance::Precipitation?"Precipitation":"Rainbow",25);
 if(Sheet.Appearance==EditorSheetAppearance::Wind)WindPanel(U,W);else if(Sheet.Appearance==EditorSheetAppearance::Precipitation)PrecipPanel(U,W);else RainbowPanel(U,W);
 ImGui::PopID();ImGui::PopFont();
}
}
