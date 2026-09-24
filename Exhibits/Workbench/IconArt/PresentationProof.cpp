#include "IconPresentation.h"
#include "CpuDraw.h"
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <stdexcept>
using namespace Frontier;
static int Checks = 0;
static void Check(bool V, const char* Why) { ++Checks; if (!V) throw std::runtime_error(Why); }
int main() {
    ImGui::CreateContext();
    auto& IO = ImGui::GetIO(); IO.IniFilename = nullptr; IO.DisplaySize = ImVec2(128,128);
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IconPresentation::Attach(); IconPresentation::Attach();
    Check(ImGui::GetCurrentContext()->Hooks.Size == 2, "idempotent attach");
    IconArt Reference("EngineContent/Icons");
    int Ready = 0, Blocked = 0;
    for (int Scale : {1,2}) {
        IO.DisplayFramebufferScale = ImVec2(float(Scale),float(Scale));
        // Let any prior atlas retire before comparing requested scale.
        for (int N=0; N<6; ++N) { ImGui::NewFrame(); ImGui::Render(); FrontierProof::AcknowledgeTextures(); }
        auto* Stable = ImGui::GetCurrentContext()->UserTextures.back();
        Check(Stable->Width == 16*(24*Scale+4),"requested physical size");
        for (int I=0; I<static_cast<int>(IconSymbol::Count); ++I) {
            auto S = static_cast<IconSymbol>(I);
            ImGui::NewFrame();
            Check(ImGui::GetCurrentContext()->UserTextures.back() == Stable,"same atlas reused across rows/frames");
            auto* List = ImGui::GetBackgroundDrawList();
            Check(IconPresentation::Draw(List,S,ImVec2(10,10),24,0.5f), "draw admitted");
            auto Raster = Reference.Rasterize(S,24,24,float(Scale));
            Check(IconPresentation::Result(S) == Raster->Result, "strict result preserved");
            if (Scale == 1) { if (Raster->Result == IconResult::Ready) ++Ready; else ++Blocked; }
            ImGui::Render(); FrontierProof::AcknowledgeTextures();
            int Width = 128*Scale;
            std::vector<unsigned char> Pixels(Width*Width*3,17);
            FrontierProof::Draw(List,Pixels.data(),Width,Width,{0,0},{float(Scale),float(Scale)});
            int Worst = 0;
            for (int Y=0; Y<24*Scale; ++Y) for (int X=0; X<24*Scale; ++X) {
                auto* P = Raster->Rgba.data()+(Y*24*Scale+X)*4;
                float Alpha = P[3]/255.0f*(128/255.0f);
                for (int C=0; C<3; ++C) {
                    int Expected = int(std::lround(P[C]*Alpha+17*(1-Alpha)));
                    int Actual = Pixels[((Y+10*Scale)*Width+X+10*Scale)*3+C];
                    Worst = std::max(Worst,std::abs(Expected-Actual));
                }
            }
            Check(Worst <= 1,"atlas UV, colour, alpha and no double-blended diagonal");
            Check(Pixels[0] == 17,"outside icon unchanged");
        }
    }
    Check(Ready == 82 && Blocked == 69,"all 151 strict symbols accounted for");
    for (int N=0; N<90; ++N) {
        IO.DisplayFramebufferScale = ImVec2(float(1+N%5),float(1+N%5));
        ImGui::NewFrame();
        auto& Textures = ImGui::GetCurrentContext()->UserTextures;
        Check(Textures.Size <= 2,"DPI churn bounded to two atlases");
        int Bytes=0; for (auto* T : Textures) Bytes += T->GetSizeInBytes();
        Check(Bytes <= 12800000,"atlas pixels bounded to 12.8MB");
        ImGui::Render(); FrontierProof::AcknowledgeTextures();
    }
    // Deterministic fixture: managed RGBA, clipping, non-zero display origin and 2x scale.
    IO.DisplayFramebufferScale = ImVec2(2,2);
    ImTextureData Fixture; Fixture.Create(ImTextureFormat_RGBA32,2,2);
    for (int I=0; I<4; ++I) { Fixture.Pixels[I*4]=200; Fixture.Pixels[I*4+1]=100; Fixture.Pixels[I*4+2]=50; Fixture.Pixels[I*4+3]=128; }
    ImGui::NewFrame();
    auto* L = ImGui::GetBackgroundDrawList();
    L->PushClipRect(ImVec2(12,12),ImVec2(18,18),true);
    L->AddImage(Fixture.GetTexRef(),ImVec2(10,10),ImVec2(20,20),{0,0},{1,1},IM_COL32(128,255,255,128));
    L->PopClipRect();
    ImGui::Render();
    for (int I=0; I<4; ++I) L->VtxBuffer.push_back(L->VtxBuffer[I]);
    for (auto& Cmd : L->CmdBuffer) Cmd.VtxOffset = 4;
    for (int I=0; I<4; ++I) L->VtxBuffer[I].pos = ImVec2(-100,-100);
    std::vector<unsigned char> Pixels(64*64*3,0);
    FrontierProof::Draw(L,Pixels.data(),64,64,{5,5},{2,2});
    for (int Y=0; Y<64; ++Y) for (int X=0; X<64; ++X) {
        int Expected = X>=14 && X<26 && Y>=14 && Y<26 ? 25 : 0;
        Check(std::abs(Pixels[(Y*64+X)*3]-Expected)<=1,"clip/scale/origin/tint fixture");
    }
    const unsigned char Alpha[] = {128};
    auto Sample = FrontierProof::Sample({Alpha,1,1,1},0.5f,0.5f);
    Check(Sample[0]==1 && std::abs(Sample[3]-128/255.0f)<0.0001f,"Alpha8 sample");
    // CPU backend owns no GPU allocations; imitate device shutdown before context shutdown.
    for (auto* T : ImGui::GetPlatformIO().Textures) { T->SetTexID(ImTextureID_Invalid); T->SetStatus(ImTextureStatus_Destroyed); }
    ImGui::DestroyContext();
    std::printf("PASS %d checks; 151 symbols at 1x/2x; 82 ready / 69 blocked; DPI retirement bounded; context destroyed\n",Checks);
}
