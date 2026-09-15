//============================================================================================================================================
//  rejectrate.cpp — how often does a ReSTIR spatial tap actually pass validation?
//============================================================================================================================================
// ReSTIRTapCost.cpp models the worst case: every tap valid. That is right for a budget, but it cannot say whether
//    an early-reject / narrow-load optimisation is worth anything, because that depends entirely on how many taps
//    are REJECTED and where the rejects come from. This measures that.
//
// The rule, transcribed from ReSTIRViewport.slang:963-978:
//    valid = M > 0 && Normal.w == ViewportWidth && dot(N, Nn) > cos(25 deg) && |t - tn| / t < 0.10
//
// Geometry: a Cornell-style box (the renderer's own reference scene) ray-cast analytically per pixel, so depth and
//    normals are exact and the harness needs no engine internals. Camera matches the proofs' framing.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

namespace {
constexpr float kNormalCos = 0.906308f;   // cos(25 deg)
constexpr float kDepthTol  = 0.10f;
constexpr float kRadMin    = 4.0f;
constexpr float kRadMax    = 16.0f;

uint32_t PcgHash(uint32_t v){ uint32_t s=v*747796405u+2891336453u; uint32_t w=((s>>((s>>28u)+4u))^s)*277803737u; return (w>>22u)^w; }
float RandFloat(uint32_t& s){ s=PcgHash(s); return float(s&0xFFFFFFu)/float(0x1000000u); }

struct Hit { float t; float n[3]; bool hit; };

// Axis-aligned Cornell box, walls at +-1 in x, y=1 back, z in [0,2], plus an angled block.
Hit Cast(const float o[3], const float d[3])
{
    Hit best{1e30f,{0,0,0},false};
    auto Plane=[&](float px,float py,float pz,float nx,float ny,float nz){
        const float dn=d[0]*nx+d[1]*ny+d[2]*nz;
        if(std::fabs(dn)<1e-9f) return;
        const float t=((px-o[0])*nx+(py-o[1])*ny+(pz-o[2])*nz)/dn;
        if(t<=1e-4f||t>=best.t) return;
        const float hx=o[0]+d[0]*t, hy=o[1]+d[1]*t, hz=o[2]+d[2]*t;
        if(hx<-1.001f||hx>1.001f||hy<-1.001f||hy>1.001f||hz<-0.001f||hz>2.001f) return;
        best.t=t; best.n[0]=nx; best.n[1]=ny; best.n[2]=nz; best.hit=true;
    };
    Plane(0,0,0,  0,0,1);    // floor
    Plane(0,0,2,  0,0,-1);   // ceiling
    Plane(0,1,0,  0,-1,0);   // back wall
    Plane(-1,0,0, 1,0,0);    // left
    Plane(1,0,0, -1,0,0);    // right
    // A box occluder, so the scene has depth discontinuities and non-axis normals.
    const float bmin[3]={-0.45f,-0.15f,0.0f}, bmax[3]={0.15f,0.45f,0.95f};
    float t0=-1e30f,t1=1e30f; int axis=-1; float sgn=1;
    for(int a=0;a<3;a++){
        if(std::fabs(d[a])<1e-9f){ if(o[a]<bmin[a]||o[a]>bmax[a]){t0=1e30f;break;} continue; }
        float ta=(bmin[a]-o[a])/d[a], tb=(bmax[a]-o[a])/d[a]; float s=-1;
        if(ta>tb){ float tmp=ta; ta=tb; tb=tmp; s=1; }
        if(ta>t0){ t0=ta; axis=a; sgn=s; }
        if(tb<t1) t1=tb;
    }
    if(t0<=t1 && t0>1e-4f && t0<best.t && axis>=0){
        best.t=t0; best.n[0]=best.n[1]=best.n[2]=0; best.n[axis]=sgn; best.hit=true;
    }
    return best;
}
}

int main()
{
    const uint32_t W=1280,H=960;   // full resolution: the tap radius is the real 4-16 px, not scaled down
    const float Eye[3]={0.0f,-3.2f,1.0f};
    const float Fwd[3]={0,1,0}, Rgt[3]={1,0,0}, Up[3]={0,0,1};
    const float FovY=50.0f*3.14159265f/180.0f;
    const float TanY=std::tan(FovY*0.5f), Aspect=float(W)/float(H);

    std::vector<float> Depth(size_t(W)*H,-1.0f), Norm(size_t(W)*H*3,0.0f);
    for(uint32_t y=0;y<H;y++) for(uint32_t x=0;x<W;x++){
        const float sx=(2.0f*((x+0.5f)/W)-1.0f)*TanY*Aspect;
        const float sy=(1.0f-2.0f*((y+0.5f)/H))*TanY;
        float d[3]={Fwd[0]+Rgt[0]*sx+Up[0]*sy, Fwd[1]+Rgt[1]*sx+Up[1]*sy, Fwd[2]+Rgt[2]*sx+Up[2]*sy};
        const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); d[0]/=l;d[1]/=l;d[2]/=l;
        const Hit h=Cast(Eye,d);
        const size_t p=size_t(y)*W+x;
        if(h.hit){ Depth[p]=h.t; Norm[p*3]=h.n[0]; Norm[p*3+1]=h.n[1]; Norm[p*3+2]=h.n[2]; }
    }
    uint64_t Surface=0; for(size_t i=0;i<Depth.size();++i) if(Depth[i]>0) ++Surface;

    std::printf("\nReSTIR spatial tap validation — measured reject rate\n");
    std::printf("%s\n\n",std::string(100,'=').c_str());
    std::printf("Scene: Cornell box + block, %ux%u, %llu shaded pixels (%.1f%% coverage)\n",
        W,H,(unsigned long long)Surface,100.0*double(Surface)/double(W*H));
    std::printf("Rule: M>0, stride match, dot(N,Nn) > cos(25 deg), |dt|/t < 10%%\n\n");
    std::printf("  %-6s %10s %10s %11s   %s\n","taps","tested","rejected","rejected%","reject cause breakdown");

    for(uint32_t Taps:{1u,2u,3u,4u}){
        uint64_t Tested=0,Rej=0,Off=0,NoSurf=0,NrmF=0,DepF=0;
        for(uint32_t y=0;y<H;y++) for(uint32_t x=0;x<W;x++){
            const size_t p=size_t(y)*W+x; if(Depth[p]<=0) continue;
            uint32_t seed=PcgHash((x*1973u+y*9277u+1u)^0x9E3779B9u);
            const float scale=float(W)/1280.0f;
            const float ang=RandFloat(seed)*6.28318531f;
            const float rad=(kRadMin+(kRadMax-kRadMin)*RandFloat(seed))*scale;
            for(uint32_t t=0;t<Taps;++t){
                const float th=ang+float(t)*(6.28318531f/float(Taps));
                const int ox=int(std::lround(rad*std::cos(th))), oy=int(std::lround(rad*std::sin(th)));
                if(ox==0&&oy==0) continue;              // the kernel's own skip
                const int nx=int(x)+ox, ny=int(y)+oy;
                ++Tested;
                if(nx<0||ny<0||nx>=int(W)||ny>=int(H)){ ++Rej; ++Off; continue; }
                const size_t q=size_t(ny)*W+nx;
                if(Depth[q]<=0){ ++Rej; ++NoSurf; continue; }
                const float dn=Norm[q*3]*Norm[p*3]+Norm[q*3+1]*Norm[p*3+1]+Norm[q*3+2]*Norm[p*3+2];
                const float dt=std::fabs(Depth[p]-Depth[q])/std::fmax(Depth[p],1e-3f);
                const bool nf=!(dn>kNormalCos), df=!(dt<kDepthTol);
                if(nf||df){ ++Rej; if(nf)++NrmF; else ++DepF; }
            }
        }
        std::printf("  %-6u %10llu %10llu %10.1f%%   offscreen %.1f%% / no-surface %.1f%% / normal %.1f%% / depth %.1f%%\n",
            Taps,(unsigned long long)Tested,(unsigned long long)Rej,
            100.0*double(Rej)/double(Tested?Tested:1),
            100.0*double(Off)/double(Tested?Tested:1),
            100.0*double(NoSurf)/double(Tested?Tested:1),
            100.0*double(NrmF)/double(Tested?Tested:1),
            100.0*double(DepF)/double(Tested?Tested:1));
    }
    std::printf("\n");
    return 0;
}
