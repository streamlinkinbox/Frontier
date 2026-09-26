#pragma once
#include "EditorInstance.h"
#include "../DisplayPresentation/IconPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier {
// Editor proxies only. These never contribute radiance or mutate a physical volume's centre.
struct EditorBillboard {
    uint64_t Key=0;
    float World[3]={};
    IconSymbol Artwork=IconSymbol::Wind;
    char Label[64]={};
    bool Global=false, Selected=false;
};
struct EditorBillboardCamera {
    float Eye[3]={}, Forward[3]={0,1,0}, Right[3]={1,0,0}, Up[3]={0,0,1};
    float Fov=1.04719755f, Aspect=1, Near=.05f, ViewWidth=1, ViewHeight=1;
};
using BillboardExchange=uint32_t (*)(EditorBillboard*,uint32_t,EditorBillboardCamera&,void*) noexcept;
struct BillboardPoint { float X=0,Y=0,Depth=0; bool Visible=false; };
class ViewportBillboards {
public:
    static constexpr unsigned Capacity=32;
    static constexpr float Radius=18;
    static BillboardPoint Project(const EditorBillboard& M,const EditorBillboardCamera& C,float W,float H) noexcept {
        BillboardPoint P;
        if(!(W>0&&H>0&&C.Aspect>0&&C.Fov>0&&C.Fov<3.14159f)||!std::isfinite(C.Aspect)||!std::isfinite(C.Near))return P;
        float X=0,Y=0;
        for(unsigned I=0;I<3;++I){const float D=M.World[I]-C.Eye[I];X+=D*C.Right[I];Y+=D*C.Up[I];P.Depth+=D*C.Forward[I];}
        if(!std::isfinite(X)||!std::isfinite(Y)||!std::isfinite(P.Depth)||P.Depth<=std::max(.01f,C.Near))return P;
        const float T=std::tan(C.Fov*.5f);
        // Camera aspect, not panel aspect: the framebuffer may be stretched or render-scaled.
        P.X=(.5f+.5f*X/(P.Depth*T*C.Aspect))*W;P.Y=(.5f-.5f*Y/(P.Depth*T))*H;
        P.Visible=std::isfinite(P.X)&&std::isfinite(P.Y)&&P.X>=0&&P.Y>=0&&P.X<W&&P.Y<H;
        return P;
    }
    void Assign(BillboardExchange Fn,void* Context) noexcept {Exchange_=Fn;Context_=Context;}
    uint64_t TakePick() noexcept {const auto K=Pick_;Pick_=0;return K;}
    ImVec2 Centre(uint64_t Key) const noexcept {for(unsigned I=0;I<Count_;++I)if(Markers_[I].Key==Key&&Points_[I].Visible)return {Origin_.x+Points_[I].X,Origin_.y+Points_[I].Y};return {-1,-1};}
    void ClearFrame() noexcept {Count_=0;Pick_=0;}
    void Select(uint64_t Key) noexcept {Selected_=Key;}
    bool Draw(ImDrawList* D,ImVec2 Min,ImVec2 Max,bool InputAllowed) noexcept {
        Count_=0;Origin_=Min;
        if(!Exchange_)return false;
        EditorBillboardCamera Camera;Camera.ViewWidth=Max.x-Min.x;Camera.ViewHeight=Max.y-Min.y;
        Count_=std::min(Capacity,Exchange_(Markers_,Capacity,Camera,Context_));
        std::sort(Markers_,Markers_+Count_,[&](const auto& A,const auto& B){
            const auto PA=Project(A,Camera,Max.x-Min.x,Max.y-Min.y),PB=Project(B,Camera,Max.x-Min.x,Max.y-Min.y);
            if(PA.Visible!=PB.Visible)return !PA.Visible;
            if(!PA.Visible)return A.Key>B.Key;
            return PA.Depth==PB.Depth?A.Key>B.Key:PA.Depth>PB.Depth;
        });
        int Hot=-1;const auto Mouse=ImGui::GetIO().MousePos;
        const bool Own=InputAllowed&&ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(Min,Max)&&!ImGui::IsAnyItemActive();
        for(unsigned I=0;I<Count_;++I){
            auto& P=Points_[I];P=Project(Markers_[I],Camera,Max.x-Min.x,Max.y-Min.y);
            if(!P.Visible)continue;
            const float X=Mouse.x-Min.x-P.X,Y=Mouse.y-Min.y-P.Y;
            if(Own&&X*X+Y*Y<=Radius*Radius)Hot=int(I);
        }
        D->PushClipRect(Min,Max,true);
        for(unsigned I=0;I<Count_;++I){
            const auto& P=Points_[I];if(!P.Visible)continue;
            const auto& M=Markers_[I];const ImVec2 At(Min.x+P.X,Min.y+P.Y);
            const bool Selected=M.Key==Selected_,Hover=int(I)==Hot;
            D->AddCircleFilled(At,Radius,IM_COL32(42,48,53,235));
            D->AddCircle(At,Radius,Selected?IM_COL32(113,219,170,255):Hover?IM_COL32(235,240,245,255):IM_COL32(105,118,130,190),0,Selected?2.5f:1.f);
            IconPresentation::Draw(D,M.Artwork,{At.x-13,At.y-13},26);
            if(Selected){
                const auto Size=ImGui::CalcTextSize(M.Label);const ImVec2 Text(At.x-Size.x*.5f,At.y+Radius+5);
                D->AddRectFilled({Text.x-5,Text.y-2},{Text.x+Size.x+5,Text.y+Size.y+2},IM_COL32(17,23,26,230),3);
                D->AddText(Text,IM_COL32(210,238,224,255),M.Label);
            }
        }
        D->PopClipRect();
        if(Hot>=0){
            const auto& M=Markers_[Hot];
            ImGui::SetTooltip("%s\n%s",M.Label,M.Global?"Global system • editor proxy, not a physical position":"Local volume • actual world-space centre");
            if(ImGui::IsMouseClicked(0)){Pick_=M.Key;Selected_=M.Key;}
        }
        return Hot>=0;
    }
private:
    BillboardExchange Exchange_=nullptr;void* Context_=nullptr;
    EditorBillboard Markers_[Capacity]{};BillboardPoint Points_[Capacity]{};
    unsigned Count_=0;uint64_t Pick_=0,Selected_=0;ImVec2 Origin_{};
};
}
