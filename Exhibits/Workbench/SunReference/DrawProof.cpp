#include "SunReferenceDraw.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace Frontier::SunReference;
int main() {
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;
    IO.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf",16);
    const float Cases[][4]={{135,38,12,110},{230,-25,3,20},{45,0,6,0},{90,89,18,150}};
    unsigned Checks=0;
    for(int Case=0;Case<4;++Case)for(int Kind=0;Kind<3;++Kind)for(int Scale:{1,2}) {
        const int W=Kind==1?500:360,H=Kind==1?123:Kind==2?100:280;
        IO.DisplaySize={float(W),float(H)};IO.DisplayFramebufferScale={float(Scale),float(Scale)};
        IO.DeltaTime=1.0f/60;ImGui::NewFrame();auto* D=ImGui::GetBackgroundDrawList();
        Canvas C{D,{0,0},1};if(Kind==1)Day(C,Cases[Case][2]);else if(Kind==2)Illuminance(C,Cases[Case][3]);else Orbit(C,Cases[Case][0],Cases[Case][1]);
        ImGui::Render();FrontierProof::AcknowledgeTextures();
        std::vector<unsigned char> Pixels(W*H*Scale*Scale*3,35);
        FrontierProof::Draw(D,Pixels.data(),W*Scale,H*Scale,{0,0},{float(Scale),float(Scale)});
        char Name[128];std::snprintf(Name,sizeof(Name),"Exhibits/Gallery/SunReference/Native-%s-%d-%dx.png",Kind==1?"Day":Kind==2?"Illuminance":"Orbit",Case,Scale);
        if(!stbi_write_png(Name,W*Scale,H*Scale,3,Pixels.data(),W*Scale*3))throw std::runtime_error("image write failed");
        ++Checks;
    }
    // Geometric anchors independent of raster coverage: the same SVG coordinate space.
    if(std::abs(OrbitPoint(Pi/2,135).y-40)>0.001f || std::abs(DayHeight(12)-18)>0.001f || std::abs(DayHeight(0)-102)>0.001f)
        throw std::runtime_error("reference anchor changed");
    ImGui::DestroyContext();
    std::printf("PASS %u native captures at 1x/2x; SVG coordinate anchors; context shutdown. No claim of completed inspector layout.\n",Checks);
}
