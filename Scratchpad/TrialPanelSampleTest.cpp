//============================================================================================================================================
//                                                   TRIALPANELSAMPLETEST.CPP
//============================================================================================================================================
// 🧩 The High-tier sampler against the REAL trial panel rather than a synthetic one, rendered as a reflection
//    would see it. Writes Diagnostics/SpatialInterface_HighTier_Trial.png, which shows the needle, tick ring,
//    seven-segment readout and progress bar — the layout a Low-tier proxy replaces with one averaged colour.
//
//    Build: bash Scratchpad/CheckPanelSample.sh

// The REAL trial panel, sampled as a reflection would see it.
#include "GlslShim.h"
#include "/tmp/InterfaceSignedDistance.port.inc"
#include "SpatialInterface/InterfaceLayoutCodec.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"
#include "DisplayPresentation/MotionIntegrator.h"
#include "../Projects/Project-Zero/Source/InterfaceTrialSequence.h"
#include "PngWriteShim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
std::vector<Frontier::InterfaceInstanceFigure> InterfacePanelFigures;
static vec3 Lin(uint32_t P){
    auto C=[](float e){return e<=0.04045f?e/12.92f:std::pow((e+0.055f)/1.055f,2.4f);};
    return vec3(C((P&0xFF)/255.0f),C(((P>>8)&0xFF)/255.0f),C(((P>>16)&0xFF)/255.0f));}
struct S_{vec3 E;float C;uint32_t W;};
static S_ Sample(vec2 uv,float hw,float hh,uint32_t n,float px){
    S_ R{vec3(0,0,0),0.0f,0u};
    vec2 L((uv.x-0.5f)*2*hw,(uv.y-0.5f)*2*hh);
    for(uint32_t i=0;i<n;++i){
        const auto&F=InterfacePanelFigures[i]; ++R.W;
        vec3 Rt(F.RowXx,F.RowYx,F.RowZx), Up(F.RowXy,F.RowYy,F.RowZy);
        float s=std::sqrt(Rt.x*Rt.x+Rt.y*Rt.y+Rt.z*Rt.z); if(s<1e-9f)continue;
        float ul=std::max(std::sqrt(Up.x*Up.x+Up.y*Up.y+Up.z*Up.z),1e-9f);
        vec3 T(F.RowXw,F.RowYw,F.RowZw);
        vec2 c((T.x*Rt.x+T.y*Rt.y+T.z*Rt.z)/s,(T.x*Up.x+T.y*Up.y+T.z*Up.z)/ul);
        vec2 p((L.x-c.x)/s,(L.y-c.y)/s);
        float d=DistanceFigure(F.CategoryPalette>>24,p,vec2(F.HalfWidth/s,F.HalfHeight/s),
                               F.CornerRadius/s,F.ScalarAlpha,F.ScalarBeta);
        float a=CoverageFromDistance(d,std::max(px,1e-6f))*std::clamp(F.Opacity,0.0f,1.0f);
        if(a<=0)continue;
        vec3 t=Lin(F.Tint); float w=std::clamp(F.EmissiveWeight,0.0f,1.0f);
        R.E=vec3(R.E.x+(t.x*w-R.E.x)*a,R.E.y+(t.y*w-R.E.y)*a,R.E.z+(t.z*w-R.E.z)*a);
        R.C=R.C+(1-R.C)*a;}
    return R;}
int main(){
    using namespace Frontier;
    InterfaceStructure F; MotionIntegrator M; ProjectZero::InterfaceTrialSequence T;
    T.Construct(F,M); T.AdvanceTrial(F,M,1.5,true);
    InterfaceSequence C; InterfaceViewConfiguration V; V.EyeZ=1.0f; V.ForwardY=1.0f;
    C.AssignView(V); C.Advance(F,1.5);
    InterfacePanelFigures.assign(C.QueryInstances(),C.QueryInstances()+C.QueryInstanceCount());
    printf("real trial panel: %u figures\n",C.QueryInstanceCount());
    const float hw=0.115f, hh=0.072f;
    const uint32_t W=384,H=240;
    std::vector<unsigned char> Px(size_t(W)*H*3,6);
    uint32_t lit=0;
    for(uint32_t y=0;y<H;++y)for(uint32_t x=0;x<W;++x){
        S_ s=Sample(vec2((x+0.5f)/W,1.0f-(y+0.5f)/H),hw,hh,C.QueryInstanceCount(),(2*hw)/W);
        if(s.E.x+s.E.y+s.E.z>0.02f)++lit;
        auto E=[](float l){float c=std::clamp(l,0.0f,1.0f);
            float e=c<=0.0031308f?c*12.92f:1.055f*std::pow(c,1/2.4f)-0.055f;
            return (unsigned char)(e*255+0.5f);};
        size_t o=(size_t(y)*W+x)*3; Px[o]=E(s.E.x);Px[o+1]=E(s.E.y);Px[o+2]=E(s.E.z);}
    printf("lit texels: %.1f%%\n",100.0*lit/(W*H));
    PngWriteShim::WritePng("Diagnostics/SpatialInterface_HighTier_Trial.png",W,H,3,Px.data(),W*3);
    printf(lit>0?">>> the real panel samples as layout\n":">>> FAIL nothing lit\n");
    return lit>0?0:1;}
