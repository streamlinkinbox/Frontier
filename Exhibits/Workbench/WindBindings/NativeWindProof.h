#pragma once
#include "OutlinerPanel.h"
#include "ControlPanel.h"
#include "WindBindingControls.h"
// Included by the existing native proof harness; executes actual panels, not HTML.
int RunWindBindings(){try{
 auto Sky=std::make_unique<CelestialSequence>();Sky->Prepare();
 auto Sheet=std::make_unique<EditorSheet>();auto Rows=std::make_unique<EditorInstance[]>(kMaxEditorInstances);
 uint32_t Count=Sky->AppendRoster(Rows.get(),0,kMaxEditorInstances);
 auto Feed=std::make_unique<EditorFeedSequence>();auto Camera=std::make_unique<FlyThroughSolver>();auto Level=std::make_unique<SceneStructure>();
 EditorInspectorSequence Session{*Feed,*Sky,*Camera,*Level,Level->QueryInstances(),Rows.get(),Count,*Sheet};
 auto Index=[&](uint64_t K){for(uint32_t I=0;I<Count;++I)if(Rows[I].InspectorKey==K)return I;throw std::runtime_error("missing wind row");};
 Check(Sky->ResolveWind(CelestialEntity::LocalCloud)==&Sky->Wind,"default is a reference to global wind");
 Sky->BuildSheet(CelestialEntity::CloudLayer,*Sheet);Find(*Sheet,"Own Wind").On=true;Sky->ApplySheet(CelestialEntity::CloudLayer,*Sheet);
 Check(Sky->WindSources[0]==1&&Sky->WindComponents[0].Present,"inspector creates and selects owned wind");
 Sky->SynchronizeWindRows(Rows.get(),Count,kMaxEditorInstances);
 uint32_t Owner=Index(Key(CelestialEntity::CloudLayer)),Child=Index(0x400000001ull);
 Check(Rows[Child].Depth==Rows[Owner].Depth+1&&Rows[Child].Component,"actual component row is nested beneath owner");
 Check(Rows[Owner].KidCount==2,"cloud owns precipitation and wind without corrupting preorder");
 Rows[Owner].Shut=true;std::snprintf(Rows[Child].Label,sizeof(Rows[Child].Label),"Storm flow");
 Session.Update(Child,false);Find(*Sheet,"Speed").Figure=23;Find(*Sheet,"Bearing").Figure=90;
 Session.Update(Child,true);Session.Update(Child,false);
 Check(Sheet->InspectorKey==0x400000001ull&&Find(*Sheet,"Speed").Figure==23,"native inspector exchange commits and reloads child settings");
 Check(Sky->WindComponents[0].Settings.Speed==23&&Sky->Wind.Speed==7,"child inspector edits independent wind, not global");
 Sky->BuildSheet(CelestialEntity::LocalFog,*Sheet);
 auto& Source=Find(*Sheet,"Wind Source");for(uint32_t I=0;I<Source.OptionCount;++I)if(Source.OptionValues[I]==1)Source.Picked=I;
 Sky->ApplySheet(CelestialEntity::LocalFog,*Sheet);
 Check(Sky->ResolveWind(CelestialEntity::LocalFog)==Sky->ResolveWind(CelestialEntity::CloudLayer),"shared source resolves identical settings pointer");
 Sky->SetOwnedWind(CelestialEntity::LocalCloud,true);Sky->WindComponents[1].Settings.Speed=11;
 Sky->SynchronizeWindRows(Rows.get(),Count,kMaxEditorInstances);
 Check(Rows[Index(Key(CelestialEntity::CloudLayer))].Shut,"inserting another component preserves collapse");
 Check(!std::strcmp(Rows[Index(0x400000001ull)].Label,"Storm flow"),"sync preserves component rename and identity");
 Sky->LocalCloud.Enabled=Sky->LocalFog.Enabled=true;Sky->WeatherSeconds=19;
 float Forward[]={0,1,0},Right[]={1,0,0},Up[]={0,0,1};
 auto Pack=[&](){return Sky->PackPostRecord(Forward,Right,Up,.5f,1,400,1);};
 auto P=Pack();Check(P.Weather.Rows[18][0]==23&&P.Weather.Rows[19][0]==11&&P.Weather.Rows[20][0]==23,"GPU receives three independently resolved winds");
 auto Raster=std::make_unique<VisibilityRaster>();Sky->ApplyTo(*Raster,Sky->Budget);
 const auto& Settings=Raster->QueryCelestial();Check(Settings.OverrideMediaWinds&&Settings.MediaWinds[0].Speed==23&&Settings.MediaWinds[1].Speed==11&&Settings.MediaWinds[2].Speed==23,"CPU receives same resolved winds as GPU");
 Session.Synchronize();Check(Pack().Weather.Rows[18][0]==23,"collapsed owner does not disable wind");
 Rows[Index(Key(CelestialEntity::CloudLayer))].Visible=false;Session.Synchronize();
 Check(Pack().Weather.Rows[20][0]==0&&Pack().Weather.Rows[19][0]==11,"owner visibility disables its shared wind but not independent wind");
 Rows[Index(Key(CelestialEntity::CloudLayer))].Visible=true;Session.Synchronize();
 Sky->Shown[uint32_t(CelestialEntity::Wind)]=false;Check(Pack().Weather.Rows[18][0]==23,"hidden global wind does not disable an owned source");
 Sky->WindComponents[0].Shown=false;P=Pack();Check(P.Weather.Rows[18][0]==0&&P.Weather.Rows[20][0]==0&&P.Weather.Rows[19][0]==11,"hidden shared wind stops its consumers only");
 Sky->WindComponents[0].Shown=true;
 Check(!Sky->BindWind(CelestialEntity::LocalFog,99),"invalid reference rejected");
 Sky->SetOwnedWind(CelestialEntity::CloudLayer,false);Sky->SynchronizeWindRows(Rows.get(),Count,kMaxEditorInstances);
 Check(Sky->WindSources[0]==0&&Sky->WindSources[2]==0&&Sky->ResolveWind(CelestialEntity::LocalFog)==&Sky->Wind,"removal clears shared references and falls back to global");
 Check(Rows[Index(Key(CelestialEntity::CloudLayer))].KidCount==1,"removal leaves precipitation child intact");
 for(uint32_t I=0;I<Count;++I)Check(Rows[I].InspectorKey!=0x400000001ull,"removed component row disappears");
 Sky->BindWind(CelestialEntity::LocalFog,2);
 const uint32_t DeletedOwner=Index(Key(CelestialEntity::LocalCloud));
 for(uint32_t I=DeletedOwner+1;I<Count;++I)Rows[I-1]=Rows[I];--Count;
 Sky->SynchronizeWindRows(Rows.get(),Count,kMaxEditorInstances);
 Check(!Sky->WindComponents[1].Present&&Sky->WindSources[2]==0,"external owner removal clears component and shared references");
 bool Orphan=false;for(uint32_t I=0;I<Count;++I)Orphan|=Rows[I].InspectorKey==0x400000002ull;
 Check(!Orphan,"external owner removal leaves no orphan wind row");
 // Real native mouse input into the outliner, isolated from the 3D renderer.
 ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={1000,1000};IO.DeltaTime=1.f/60;
 IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 ControlPanel Controls;OutlinerPanel Panel;Panel.AssignControls(&Controls);Panel.AssignCompact(true);
 auto Gui=std::make_unique<EditorInstance[]>(5);for(int I=0;I<5;++I){Gui[I].InspectorKey=I+1;Gui[I].Category=EditorInstanceCategory::Geometry;std::snprintf(Gui[I].Label,sizeof(Gui[I].Label),"row %d",I);}
 Gui[0].Category=EditorInstanceCategory::Folder;Gui[0].Depth=0;Gui[1].Depth=1;Gui[2].Depth=2;Gui[2].Component=true;Gui[3].Depth=2;Gui[4].Depth=0;
 Gui[0].KidCount=1;Gui[1].KidCount=2;
 uint32_t GuiCount=5;
 auto Tick=[&](){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({480,960});Panel.Record(Gui.get(),GuiCount);ImGui::Render();FrontierProof::AcknowledgeTextures();};
 auto Tree=[&](){for(auto* W:ImGui::GetCurrentContext()->Windows)if(W->Active&&std::strstr(W->Name,"##tree"))return W;throw std::runtime_error("no outliner tree");};
 auto Click=[&](ImVec2 At){IO.AddMousePosEvent(At.x,At.y);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();};
 Tick();Tick();Tick();auto Origin=Tree()->DC.CursorStartPos;
 Click({Origin.x+10+8+7,Origin.y+2+17});Check(Gui[0].Shut,"mouse chevron collapses folder");
 Check(Panel.QueryPicked()==kNoEditorInstance,"chevron does not steal selection");
 Click({Origin.x+25,Origin.y+19});Check(!Gui[0].Shut,"mouse chevron expands folder");
 Click({Origin.x+25+13,Origin.y+19+34});Check(Gui[1].Shut,"mouse chevron collapses entity with child components");
 Click({Origin.x+25+13,Origin.y+19+34});Check(!Gui[1].Shut,"mouse chevron expands entity");
 // A name click still selects; an eye click must not toggle collapse.
 Click({Origin.x+180,Origin.y+19+34*2});Check(Panel.QueryPicked()==2,"component row is selectable");
 const float EyeX=Tree()->Pos.x+Tree()->Size.x-10-6-9;
 Click({EyeX,Origin.y+19+34*2});Check(!Gui[2].Visible&&!Gui[1].Shut,"eye control remains independent of collapse");
 Gui[1].Shut=true;std::swap(Gui[1],Gui[4]);Tick();Check(Gui[4].Shut,"collapse state remains on row after external reorder");
 std::swap(Gui[2],Gui[3]);Tick();Check(Panel.QueryPicked()==3,"selection follows stable identity after roster insert/reorder");
 Gui[3]=Gui[4];GuiCount=4;Tick();Check(Panel.QueryPicked()==kNoEditorInstance,"removed selected component does not select the next row");
 for(auto* T:ImGui::GetPlatformIO().Textures){T->SetTexID(ImTextureID_Invalid);T->SetStatus(ImTextureStatus_Destroyed);}ImGui::DestroyContext();
 std::printf("PASS %u wind binding / native outliner checks; no Windows/GPU execution\n",Checks);return 0;
}catch(const std::exception& E){std::fprintf(stderr,"FAIL wind proof: %s\n",E.what());return 1;}}
