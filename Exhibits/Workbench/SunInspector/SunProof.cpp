#include "CelestialSequence.h"
#include "InspectorPanel.h"
#include "ControlPanel.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace Frontier;
using namespace Frontier::ProjectZero;
static void Check(bool V, const char* Message) { if (!V) throw std::runtime_error(Message); }
static EditorProperty& Find(EditorSheet& S,const char* Name) {
    for (unsigned G=0;G<S.GroupCount;++G) for(unsigned P=0;P<S.Groups[G].PropertyCount;++P)
        if (!std::strcmp(Name,S.Groups[G].Properties[P].Label)) return S.Groups[G].Properties[P];
    throw std::runtime_error(Name);
}
int main() {
    auto Sun=std::make_unique<CelestialSequence>();
    auto Sheet=std::make_unique<EditorSheet>();
    const float Camera[3]={0,0,0}; Sun->Tick(0,Camera,0);
    Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    Check(Sheet->GroupCount==6,"six cards");
    int Count=0;for(auto& G:Sheet->Groups) Count+=G.PropertyCount;
    Check(Count==15,"all original Sun properties retained");
    const char* Sliders[]={"Angular Diameter","Intensity","Direct","Local Hours","Latitude","Longitude","Day of Month","Month"};
    for(auto* Name:Sliders) {
        auto& P=Find(*Sheet,Name);const float Mid= P.Minimum+(P.Maximum-P.Minimum)*0.5f;
        const float Value=P.Decimals==0 ? std::floor(Mid) : Mid;
        P.Figure=Value;Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
        Check(std::abs(Find(*Sheet,Name).Figure-Value)<0.001f,"slider round trip");
    }
    Find(*Sheet,"Animate").On=true;Find(*Sheet,"Speed").Picked=3;
    Find(*Sheet,"Sun Tint").ColourTint[0]=0.35f;
    Sun->ApplySheet(CelestialEntity::Sun,*Sheet);Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
    Check(Find(*Sheet,"Animate").On && Find(*Sheet,"Speed").Picked==3,"clock round trip");
    Check(std::abs(Find(*Sheet,"Sun Tint").ColourTint[0]-0.35f)<0.001f,"tint round trip");
    Check(Find(*Sheet,"Elevation").Category==EditorPropertyCategory::Readout,"direction stays solved");
    auto Other=std::make_unique<EditorSheet>();Sun->BuildSheet(CelestialEntity::Stars,*Other);
    Check(!Other->Groups[0].StackedLabels && !Other->Groups[0].Clock24,"other inspectors unchanged");
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={480,900};
    IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;IO.DeltaTime=1.0f/60;
    ImGui::StyleColorsDark();
    auto Controls=std::make_unique<ControlPanel>();auto Inspector=std::make_unique<InspectorPanel>();
    Inspector->AssignControls(Controls.get());
    auto Row=std::make_unique<EditorInstance>();std::snprintf(Row->Label,sizeof(Row->Label),"Sun");Row->Category=EditorInstanceCategory::Light;
    std::vector<unsigned char> Pixels(480*900*3);
    auto Tick=[&]() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({480,900});
        Inspector->Record(Row.get(),0,Sheet.get());
        Sun->ApplySheet(CelestialEntity::Sun,*Sheet);
        Sun->Tick(0,Camera,0);
        Sun->BuildSheet(CelestialEntity::Sun,*Sheet);
        ImGui::Render();FrontierProof::AcknowledgeTextures();
    };
    for(int N=0;N<3;++N) Tick();
    const float Before=Find(*Sheet,"Angular Diameter").Figure;
    IO.AddMousePosEvent(400,263);Tick();IO.AddMouseButtonEvent(0,true);Tick();IO.AddMouseButtonEvent(0,false);Tick();
    Check(std::abs(Find(*Sheet,"Angular Diameter").Figure-Before)>0.1f,"native SliderPill pointer edit writes back");
    IO.AddMousePosEvent(-100,-100);
    for(int Page=0;Page<3;++Page) {
        for(int N=0;N<3;++N) {
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({480,900});
            Inspector->Record(Row.get(),0,Sheet.get());
            // Scroll the actual native property child, not a separate facsimile.
            for(auto* W:ImGui::GetCurrentContext()->Windows) if(std::strstr(W->Name,"##props"))
                ImGui::SetScrollY(W,float(Page*610));
            ImGui::Render();FrontierProof::AcknowledgeTextures();
        }
        std::fill(Pixels.begin(),Pixels.end(),20);
        for(auto* L:ImGui::GetDrawData()->CmdLists) FrontierProof::Draw(L,Pixels.data(),480,900,{0,0},{1,1});
        char Path[100];std::snprintf(Path,sizeof(Path),"Exhibits/Gallery/SunInspector/Sun-%d.png",Page+1);
        Check(stbi_write_png(Path,480,900,3,Pixels.data(),480*3)!=0,"screenshot write");
    }
    ImGui::DestroyContext();
    std::printf("PASS: native SliderPill pointer edit writes back; 15 properties; 8 slider round trips; tint, animation and speed round trips; read-only direction; three native inspector screenshots.\nsizeof(EditorSheet)=%zu sizeof(CelestialSequence)=%zu (both heap-owned in proof)\n",sizeof(EditorSheet),sizeof(CelestialSequence));
}
