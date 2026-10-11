//============================================================================================================================================
// 📦 Projects/Project-Drive/Source/SurfelReference.cpp — headless Surfel-GI reference render of the drive scene
//============================================================================================================================================
//
//    Project-Drive's headless CPU reference for the RENDER half. It is the same split the engine uses on the GTX path and
//    the standalone SurfelGI/ reference proved: a visibility pass builds the G-buffer, DIRECT light stays sharp (sun NEE +
//    sky), and a persistent field of world-space SURFELS carries only the INDIRECT (bounced) light, temporally averaged
//    through a world-space hash grid so it converges and holds still (no boil).
//
//    Unlike a closed-room reference, the geometry here is a TRIANGLE SOUP (checker pad + ramp + bumps + cones from
//    DriveCourse, plus the real ControlVehicle shell and procedural wheels), so this file adds a compact BVH over the
//    triangles for closest-hit and shadow queries. Output → Diagnostics/: drive_gi.ppm (tonemapped) + surfel_timing.log.
//
//    Build (plain g++ + pthreads, no GPU): see Projects/Project-Drive/Makefile (target: SurfelReference).

#include "DriveCourse.h"
#include "VehicleGeometry.h"
#include "DriveSceneAuthor.h"
#include "ContentInterchange/MaterialIndex.h"
// ONE material model for every CPU render path -- see the header of this file for why this is not optional.
#include "ContentInterchange/UnifiedMaterialEvaluation.h"
#include "CelestialSequence.h"
#include "GeometricRaster/VisibilityRaster.h"
#include "DisplayPresentation/AtmosphereModel.h"
#include "DisplayPresentation/CelestialTier.h"
#include "ControlVehicleMesh.inl"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace DC = Frontier::Drive;

//------------------------------------------------------------------------------------------------------------------------ vec math
struct V { float x=0,y=0,z=0; };
static inline V operator+(V a,V b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
static inline V operator-(V a,V b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static inline V operator*(V a,float s){return{a.x*s,a.y*s,a.z*s};}
static inline V operator*(V a,V b){return{a.x*b.x,a.y*b.y,a.z*b.z};}
static inline float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static inline V cross(V a,V b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static inline float len(V a){return std::sqrt(dot(a,a));}
static inline V norm(V a){float l=len(a);return l>0?a*(1.0f/l):a;}
static inline V vmin(V a,V b){return{std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
static inline V vmax(V a,V b){return{std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
static const float PI=3.14159265358979f, INV_PI=1.0f/PI;

struct RNG{ uint32_t s; RNG(uint32_t seed){s=seed?seed:0x9e3779b9u;} float f(){s^=s<<13;s^=s>>17;s^=s<<5;return (s&0xFFFFFF)/16777216.0f;} };
static inline uint32_t hashi(uint32_t a){a^=a>>16;a*=0x7feb352du;a^=a>>15;a*=0x846ca68bu;a^=a>>16;return a;}

//------------------------------------------------------------------------------------------------------------------------ triangles + materials
struct Tri{ V a,b,c,n; uint32_t mat; };
// ── the engine's material model, indexed by family ──────────────────────────────────────────────────────────
// This used to be a switch returning one flat RGB constant per family, shaded as pure Lambert.  That made the
// Surfel column of the render-mode comparison differ from the other two by MATERIAL MODEL as well as by light
// transport, which is the one variable such a comparison has to hold fixed.  The slabs now come from
// DriveSceneAuthor::AuthorMaterials -- the very same authored descriptors the raster and ReSTIR paths read --
// pushed through MaterialIndex so the flatten and the record construction are the engine's, not a hand copy.
struct MaterialTable
{
    Frontier::MaterialIndex Index;
    std::vector<ShadingRecord> Shading;

    void Build()
    {
        Frontier::Drive::DriveSceneAuthor Author;
        Author.Construct(/*StaticPose=*/true);
        for (const auto& Descriptor : Author.QueryMaterials())
            Index.Register(Descriptor);
        Index.Finalise(1u);
        Frontier::UnifiedMaterial::EnsureTables();
        const auto& Records = Index.QueryRecords();
        const auto& Slabs   = Index.QuerySlabRecords();
        Shading.reserve(Records.size());
        for (const auto& R : Records)
        {
            const uint32_t Selection = (R.Flags >> Frontier::kMaterialReflectanceShift) & 0xFu;
            Shading.push_back(R.SlabOffset < Slabs.size()
                ? Frontier::UnifiedMaterial::Transcribe(Slabs[R.SlabOffset], Selection)
                : ShadingRecord{});
        }
    }
};
static MaterialTable g_Materials;

static const ShadingRecord& MaterialFor(uint32_t m)
{
    static const ShadingRecord Fallback{};
    return (m < g_Materials.Shading.size()) ? g_Materials.Shading[m] : Fallback;
}

// Kept only for the surfel field's colour-bleed weight, which needs a single scalar albedo per surfel rather
// than a full BSDF evaluation; the BSDF itself is what shades the pixel.
static V MaterialAlbedo(uint32_t m)
{
    const ShadingRecord& R = MaterialFor(m);
    return V{ R.BaseColor.x, R.BaseColor.y, R.BaseColor.z };
}

// Emissive families, so the surfel field can bounce the lamps' light into the scene rather than treating them
// as dark paint.  Values are the authored Emission Color x Emission Strength from the .blend node tree.
static V MaterialEmission(uint32_t m)
{
    switch(m){
        case DC::MatHeadlight: return {48.699997f, 48.699997f, 48.699997f};
        case DC::MatTaillight: return {6.5f, 0.0f, 0.106873f};
        case DC::MatHub:       return {0.0f, 0.277313f, 1.0f};
        default:               return {0.0f, 0.0f, 0.0f};
    }
}

//------------------------------------------------------------------------------------------------------------------------ scene build
// The per-face family arrives with the mesh: ControlVehicleMesh.inl carries kTriangleFamily, recovered from the
// .blend `material_index` attribute.  The positional classifier that used to stand here is deleted, exactly as it
// was in DriveSceneAuthor -- two independent copies of the same guess is how the raster and the GI reference came
// to disagree about which faces were glass.
static void EmitCar(std::vector<Tri>& tris, V offset)
{
    using namespace Frontier::Drive::ControlVehicleMesh;
    auto Raw=[&](uint32_t i){ return V{kPositions[i*3+0],kPositions[i*3+1],kPositions[i*3+2]}; };
    for(uint32_t t=0;t<kTriangleCount;++t){ V a=Raw(kTriangles[t*3+0]),b=Raw(kTriangles[t*3+1]),c=Raw(kTriangles[t*3+2]);
        Tri T; T.a=a+offset; T.b=b+offset; T.c=c+offset; T.n=norm(cross(T.b-T.a,T.c-T.a));
        T.mat=kTriangleFamily[t]; tris.push_back(T);
    }
}
static void EmitWheel(std::vector<Tri>& tris, V centre, float R, float hw, uint32_t seg)
{
    const float yL=centre.y-hw, yR=centre.y+hw, hubR=R*0.45f;
    auto addq=[&](V a,V b,V c,V d,uint32_t m){ Tri t1{a,b,c,norm(cross(b-a,c-a)),m}; Tri t2{a,c,d,norm(cross(c-a,d-a)),m};
        tris.push_back(t1); tris.push_back(t2); };
    for(uint32_t s=0;s<seg;++s){
        float a0=2*PI*s/seg, a1=2*PI*(s+1)/seg;
        V o0{centre.x+R*std::cos(a0),0,centre.z+R*std::sin(a0)}, o1{centre.x+R*std::cos(a1),0,centre.z+R*std::sin(a1)};
        addq({o0.x,yL,o0.z},{o1.x,yL,o1.z},{o1.x,yR,o1.z},{o0.x,yR,o0.z}, DC::MatTyre);            // tread
        V h0{centre.x+hubR*std::cos(a0),0,centre.z+hubR*std::sin(a0)}, h1{centre.x+hubR*std::cos(a1),0,centre.z+hubR*std::sin(a1)};
        Tri f0{{centre.x,yR,centre.z},{h0.x,yR,h0.z},{h1.x,yR,h1.z},{0,1,0},DC::MatHub}; tris.push_back(f0);
        Tri f1{{centre.x,yL,centre.z},{h1.x,yL,h1.z},{h0.x,yL,h0.z},{0,-1,0},DC::MatHub}; tris.push_back(f1);
        addq({h0.x,yR,h0.z},{o0.x,yR,o0.z},{o1.x,yR,o1.z},{h1.x,yR,h1.z}, DC::MatTyre);              // R sidewall
        addq({h0.x,yL,h0.z},{h1.x,yL,h1.z},{o1.x,yL,o1.z},{o0.x,yL,o0.z}, DC::MatTyre);              // L sidewall
    }
}
static std::vector<Tri> BuildScene()
{
    std::vector<Tri> tris; tris.reserve(8000);
    DC::EmitCourseTriangles([&](float ax,float ay,float az,float bx,float by,float bz,float cx,float cy,float cz,uint32_t m){
        Tri t; t.a={ax,ay,az}; t.b={bx,by,bz}; t.c={cx,cy,cz}; t.n=norm(cross(t.b-t.a,t.c-t.a)); t.mat=m; tris.push_back(t);
    });
    // Placement comes from VehicleGeometry, NOT from hand-copied constants.  The previous literals (spawnZ
    // 0.2486, wheel radius 0.34, half-width 0.1175) pre-dated the frame-contract fix to TyreRadius 0.50855 and
    // left the car sitting ~0.17 m low on quarter-size wheels -- a separate scene from the one the other three
    // render modes draw, which makes a side-by-side comparison meaningless.
    Frontier::Vehicle::VehicleGeometry geo;
    const float spawnZ = geo.ModelGroundOffset();
    EmitCar(tris, V{0,0,spawnZ});
    for(uint32_t w=0;w<4u;++w){
        const Frontier::Vehicle::Vec3 hub = geo.AxleMountLocal(w);
        EmitWheel(tris, V{hub.x, hub.y, hub.z + geo.CoMHeight + spawnZ},
                  geo.TyreRadius, 0.5f*geo.TyreWidth, 32u);
    }
    return tris;
}

//------------------------------------------------------------------------------------------------------------------------ compact BVH
struct Node{ V lo,hi; int left=-1,right=-1,first=0,count=0; };
struct BVH{
    std::vector<Node> nodes; std::vector<int> idx; const std::vector<Tri>* T=nullptr;
    void build(const std::vector<Tri>& tris){ T=&tris; idx.resize(tris.size()); for(size_t i=0;i<tris.size();++i) idx[i]=(int)i;
        nodes.reserve(tris.size()*2); makeNode(0,(int)tris.size()); }
    static V triLo(const Tri& t){ return vmin(vmin(t.a,t.b),t.c);} static V triHi(const Tri& t){ return vmax(vmax(t.a,t.b),t.c);}
    int makeNode(int first,int count){
        int ni=(int)nodes.size(); nodes.push_back({});
        V lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
        for(int i=first;i<first+count;++i){ lo=vmin(lo,triLo((*T)[idx[i]])); hi=vmax(hi,triHi((*T)[idx[i]])); }
        nodes[ni].lo=lo; nodes[ni].hi=hi;
        if(count<=6){ nodes[ni].first=first; nodes[ni].count=count; return ni; }
        V ext=hi-lo; int ax=(ext.x>ext.y&&ext.x>ext.z)?0:(ext.y>ext.z?1:2);
        int mid=first+count/2;
        std::nth_element(idx.begin()+first, idx.begin()+mid, idx.begin()+first+count,
            [&](int A,int B){ V ca=triLo((*T)[A])+triHi((*T)[A]); V cb=triLo((*T)[B])+triHi((*T)[B]);
                              return (ax==0?ca.x:ax==1?ca.y:ca.z) < (ax==0?cb.x:ax==1?cb.y:cb.z); });
        int l=makeNode(first,mid-first); int r=makeNode(mid,first+count-mid);
        nodes[ni].left=l; nodes[ni].right=r; return ni;
    }
    static bool slab(const Node& n,V ro,V inv,float tmax){
        float t0=0,t1=tmax;
        for(int a=0;a<3;++a){ float o=(a==0?ro.x:a==1?ro.y:ro.z), i=(a==0?inv.x:a==1?inv.y:inv.z);
            float lo=(a==0?n.lo.x:a==1?n.lo.y:n.lo.z), hi=(a==0?n.hi.x:a==1?n.hi.y:n.hi.z);
            float ta=(lo-o)*i, tb=(hi-o)*i; if(ta>tb) std::swap(ta,tb); t0=std::max(t0,ta); t1=std::min(t1,tb); if(t1<t0) return false; }
        return true;
    }
    static bool triHit(const Tri& tr,V ro,V rd,float& t){
        V e1=tr.b-tr.a, e2=tr.c-tr.a, p=cross(rd,e2); float det=dot(e1,p); if(std::fabs(det)<1e-8f) return false;
        float inv=1.0f/det; V tv=ro-tr.a; float u=dot(tv,p)*inv; if(u<0||u>1) return false;
        V q=cross(tv,e1); float v=dot(rd,q)*inv; if(v<0||u+v>1) return false; float tt=dot(e2,q)*inv; if(tt<1e-4f||tt>=t) return false; t=tt; return true;
    }
    bool closest(V ro,V rd,float tmax,int& outTri,float& outT) const {
        V inv{1.0f/rd.x,1.0f/rd.y,1.0f/rd.z}; float t=tmax; int best=-1; int stack[64],sp=0; stack[sp++]=0;
        while(sp){ const Node& n=nodes[stack[--sp]]; if(!slab(n,ro,inv,t)) continue;
            if(n.count){ for(int i=n.first;i<n.first+n.count;++i){ int ti=idx[i]; if(triHit((*T)[ti],ro,rd,t)) best=ti; } }
            else { stack[sp++]=n.left; stack[sp++]=n.right; } }
        outTri=best; outT=t; return best>=0;
    }
    bool anyHit(V ro,V rd,float dist) const {
        V inv{1.0f/rd.x,1.0f/rd.y,1.0f/rd.z}; int stack[64],sp=0; stack[sp++]=0;
        while(sp){ const Node& n=nodes[stack[--sp]]; if(!slab(n,ro,inv,dist)) continue;
            if(n.count){ for(int i=n.first;i<n.first+n.count;++i){ int ti=idx[i]; float t=dist; if(triHit((*T)[ti],ro,rd,t)) return true; } }
            else { stack[sp++]=n.left; stack[sp++]=n.right; } }
        return false;
    }
};

//------------------------------------------------------------------------------------------------------------------------ lighting
// ── the real sun and sky, not a hand-picked one ────────────────────────────────────────────────────────────
// These were hardcoded: SUN_DIR = norm(-0.35, -0.30, 0.88) and SUN_COL = (1, .95, .85) * 3.
//
// That sun is almost straight up (z = 0.88) and weak. The engine's actual sun at the Drive scene's hour is
// nearly HORIZONTAL -- CelestialSequence solves (-0.496, 0.868, 0.012) for Johannesburg at 13:00 -- and far
// brighter. An overhead sun lights every face of a car at a similar angle, which is precisely why the body
// read as flat untextured colour with no shading gradient, no specular and no real contact shadow, while the
// raster and ReSTIR sheets of the same car had all three.
//
// It also explains why this looked fine in a Cornell box and terrible here: a closed box is lit by an area
// light inside it, so the sky and the sun never enter the picture. Outdoors they ARE the picture. And the
// standing instruction for this project is explicit that Project-Drive must not have a flat sky.
static V   SUN_DIR = norm(V{-0.496f, 0.868f, 0.012f});   // replaced at startup by the solved direction
static V   SUN_COL = V{1.0f,0.95f,0.85f}*3.0f;           // replaced at startup by the solved radiance
// ── the engine's own sky, baked into a dome LUT ────────────────────────────────────────────────────────────
// Previously an analytic two-colour gradient. The engine integrates a real atmosphere -- Rayleigh, Mie, ozone,
//    planet shadow -- through AtmosphereModel::Integrate, and that is a plain static function, so nothing had
//    to be extracted: the only thing the surfel path was missing was the Medium/Light parameters. Those come
//    out of CelestialSequence::ApplyTo via VisibilityRaster::QueryCelestial, which is how the raster gets them,
//    so both paths now march the SAME air.
//
//    Marching per ray would be ruinous here (29k surfels x 8 rays x 64 frames, each a nested
//    sample x light-sample integral), so it is baked once into a lat-long dome and sampled bilinearly. That
//    is exactly the trade the engine itself offers as its baked sky dome.
static Frontier::VisibilityRaster::CelestialSettings g_Sky;
static bool g_SkyBaked = false;
static constexpr int kDomeU = 128, kDomeV = 64;
static std::vector<V> g_Dome;

static V DomeFetch(V d){
    const float u = (std::atan2(d.y, d.x) + PI) * (1.0f/(2.0f*PI));
    const float v = std::acos(std::max(-1.0f, std::min(1.0f, d.z))) * (1.0f/PI);
    float fu = u*(kDomeU-1), fv = v*(kDomeV-1);
    int iu=(int)fu, iv=(int)fv; float tu=fu-iu, tv=fv-iv;
    int iu1=(iu+1)%kDomeU, iv1=std::min(iv+1,kDomeV-1);
    iu=std::max(0,std::min(iu,kDomeU-1)); iv=std::max(0,std::min(iv,kDomeV-1));
    const V a=g_Dome[iv*kDomeU+iu], b=g_Dome[iv*kDomeU+iu1];
    const V c=g_Dome[iv1*kDomeU+iu], e=g_Dome[iv1*kDomeU+iu1];
    return (a*(1-tu)+b*tu)*(1-tv) + (c*(1-tu)+e*tu)*tv;
}

static void BakeDome(){
    g_Dome.assign((size_t)kDomeU*kDomeV, V{0,0,0});
    for(int iv=0; iv<kDomeV; ++iv) for(int iu=0; iu<kDomeU; ++iu){
        const float phi = (iu/(float)(kDomeU-1))*2.0f*PI - PI;
        const float th  = (iv/(float)(kDomeV-1))*PI;
        const float st=std::sin(th);
        float dir[3]={st*std::cos(phi), st*std::sin(phi), std::cos(th)};
        const Frontier::AtmosphereSample S = Frontier::AtmosphereModel::Integrate(
            g_Sky.Medium, g_Sky.Light, g_Sky.CameraHeight, dir, g_Sky.SampleCount, g_Sky.LightSampleCount);
        g_Dome[(size_t)iv*kDomeU+iu] = V{S.Radiance[0], S.Radiance[1], S.Radiance[2]};
    }
    g_SkyBaked = true;
}

static V SkyColour(V d){
    if (g_SkyBaked) return DomeFetch(norm(d));
    const float t = std::max(0.0f, d.z);                 // fallback only if the sky never came up
    return V{0.35f,0.5f,0.85f}*t + V{0.7f,0.75f,0.8f}*(1.0f-t);
}
// `wo` points towards the viewer.  The BSDF is the engine's; only the transport around it is this file's.
static V DirectLight(const BVH& bvh, V P, V N, uint32_t mat, V wo){
    const ShadingRecord& m = MaterialFor(mat);
    V c{0,0,0};
    const float ndl=dot(N,SUN_DIR);
    if(ndl>0 && !bvh.anyHit(P+N*0.002f, SUN_DIR, 1e4f)){
        const vec3 f = Frontier::UnifiedMaterial::EvaluateWorld(m, vec3(N.x,N.y,N.z),
                                                                vec3(wo.x,wo.y,wo.z),
                                                                vec3(SUN_DIR.x,SUN_DIR.y,SUN_DIR.z));
        c = V{f.x,f.y,f.z} * (SUN_COL*ndl);                    // sun, direct + shadowed
    }
    // NO analytic sky ambient here. It used to add an UNOCCLUDED sky term to every surface, and the resolve
    //    then added the surfel indirect on top -- but the surfel field already carries the sky, because a
    //    surfel ray that hits nothing returns SkyColour(d). So the sky was counted twice, and the half that
    //    was counted without occlusion filled every shadow and flattened the whole frame. That is the main
    //    reason the car read as untextured colour with no shading gradient.
    //
    //    RaytraceToggle/CpuMirror/ModeMatrix.cpp draws the line in the same place: sky ambient belongs to the
    //    PLAIN-RASTER mode (`amb = dalb*SKY_AMBIENT`), while surfel mode takes its ambient from the field
    //    (`lit = direct + dalb*INV_PI*E`). One or the other, never both.
    const vec3 e = m.Emission;
    return c + V{e.x,e.y,e.z};
}
static V hemiUniform(V N, float u1, float u2){ float z=u1, rr=std::sqrt(std::max(0.0f,1.0f-z*z)), th=2*PI*u2;
    V t=norm(std::fabs(N.x)>0.9f?cross(N,{0,1,0}):cross(N,{1,0,0})); V b=cross(N,t);
    return norm(t*(rr*std::cos(th))+b*(rr*std::sin(th))+N*z); }
static V hemi(V N, float u1, float u2){ float r=std::sqrt(u1), th=2*PI*u2; V t=norm(std::fabs(N.x)>0.9f?cross(N,{0,1,0}):cross(N,{1,0,0})); V b=cross(N,t);
    return norm(t*(r*std::cos(th))+b*(r*std::sin(th))+N*std::sqrt(std::max(0.0f,1-u1))); }

//------------------------------------------------------------------------------------------------------------------------ surfels
// GIBS (SIGGRAPH 2021, the basis of the W298/SurfelGI reference) stores DIRECTIONAL irradiance per surfel as
//    L1 spherical harmonics -- 4 coefficients per channel -- not a single RGB. That is the difference between
//    a surfel that lights every nearby normal identically and one that lights a wall and the floor beneath it
//    differently. With a flat value the only directionality left is the dot(N, surfel.n) weight, which is why
//    the bounce reads as a uniform wash over curved bodywork.
struct Sh3 { V c[4]{{0,0,0},{0,0,0},{0,0,0},{0,0,0}}; };
inline Sh3 operator*(const Sh3& a, float k){ Sh3 r; for(int i=0;i<4;++i) r.c[i]=a.c[i]*k; return r; }
inline Sh3 operator+(const Sh3& a, const Sh3& b){ Sh3 r; for(int i=0;i<4;++i) r.c[i]=a.c[i]+b.c[i]; return r; }

// ── radial depth: moment-based visibility (webgiya's "Radial Depth" pass) ──────────────────────────────────
// The planar cutoff is a crude proxy for visibility: it rejects surfels whose offset along their own normal is
//    large, which stops floor-to-flank leaks but says nothing about whether anything is actually BETWEEN the
//    surfel and the point being shaded. A surfel on the ground still leaks under the car, because the shading
//    point is coplanar-ish with it and simply occluded.
//
//    So each surfel also stores depth MOMENTS in a few directional bins -- mean distance and mean squared
//    distance to whatever its rays hit. At gather time Chebyshev's inequality turns those two numbers into a
//    probability that the shading point is visible from the surfel:
//
//        d <= mu                      -> fully visible
//        otherwise  sigma2 = mu2 - mu*mu,  p = sigma2 / (sigma2 + (d - mu)^2)
//
//    Same machinery DDGI uses for probe visibility. It is what stops indirect light bleeding through geometry,
//    which is the difference between "soft" and "dirty".
constexpr int kDepthBins = 8;

struct Surfel{
    V pos,n,albedo; float radius; Sh3 E, Enew; uint32_t age=0;
    float dMean[kDepthBins]{}, dMean2[kDepthBins]{}; uint32_t dCount[kDepthBins]{};
};

// Bin a direction into the hemisphere around N: 4 azimuth sectors x 2 elevation rings.
inline int DepthBin(V N, V d)
{
    V t = norm(std::fabs(N.x)>0.9f ? cross(N,{0,1,0}) : cross(N,{1,0,0}));
    V b = cross(N,t);
    const float e = dot(d,N);                       // 0..1 over the hemisphere
    const int ring = e > 0.70710678f ? 1 : 0;       // 45 degrees splits the two rings
    const float az = std::atan2(dot(d,b), dot(d,t)) + PI;
    const int sector = std::min(3, (int)(az / (PI*0.5f)));
    return ring*4 + sector;
}

// Chebyshev visibility of a point at distance `dist` in direction `dir` from this surfel.
inline float RadialVisibility(const Surfel& s, V dir, float dist)
{
    const int k = DepthBin(s.n, dir);
    if (s.dCount[k] == 0u) return 1.0f;             // nothing measured that way yet: do not invent occlusion
    const float mu = s.dMean[k], mu2 = s.dMean2[k];
    if (dist <= mu + 1e-3f) return 1.0f;
    const float sigma2 = std::max(1e-6f, mu2 - mu*mu);
    const float dd = dist - mu;
    return sigma2 / (sigma2 + dd*dd);
}

// Project a radiance sample arriving from direction d into L1 SH.
inline void ShAccumulate(Sh3& sh, V d, V L, float w)
{
    sh.c[0] = sh.c[0] + L*(0.282095f*w);
    sh.c[1] = sh.c[1] + L*(0.488603f*d.y*w);
    sh.c[2] = sh.c[2] + L*(0.488603f*d.z*w);
    sh.c[3] = sh.c[3] + L*(0.488603f*d.x*w);
}

// Evaluate irradiance along N, with the Lambert convolution (A0 = pi, A1 = 2pi/3).
inline V ShIrradiance(const Sh3& sh, V N)
{
    const float A0=3.14159265f*0.282095f, A1=2.0943951f*0.488603f;
    return sh.c[0]*A0 + sh.c[1]*(A1*N.y) + sh.c[2]*(A1*N.z) + sh.c[3]*(A1*N.x);
}
// Cell size is RMAX + eps so the 3x3x3 cell walk in gatherEcov cannot miss a contributing surfel.
struct Grid{ float cell=0.701f; std::vector<std::vector<int>> cells; int nx,ny,nz; V lo;
    void build(V mn,V mx){ lo=mn; nx=std::max(1,(int)((mx.x-mn.x)/cell)+1); ny=std::max(1,(int)((mx.y-mn.y)/cell)+1); nz=std::max(1,(int)((mx.z-mn.z)/cell)+1);
        cells.assign((size_t)nx*ny*nz,{}); }
    int index(V p)const{ int ix=std::clamp((int)((p.x-lo.x)/cell),0,nx-1),iy=std::clamp((int)((p.y-lo.y)/cell),0,ny-1),iz=std::clamp((int)((p.z-lo.z)/cell),0,nz-1);
        return (iz*ny+iy)*nx+ix; }
    void add(int i,V p){ cells[index(p)].push_back(i); }
};
template<class F> static void par(int n,F f){ unsigned T=std::max(1u,std::thread::hardware_concurrency()); std::vector<std::thread> th;
    std::atomic<int> next{0}; auto work=[&]{ int i; while((i=next.fetch_add(1))<n) f(i); };
    for(unsigned t=0;t<T;++t) th.emplace_back(work);
    for(auto& x:th) x.join();
}

struct Cam{ V eye,fwd,right,up; float tanHalf,aspect; int W,H; };
static V rayDir(const Cam& c,float px,float py){ float u=(2*(px+0.5f)/c.W-1)*c.tanHalf*c.aspect, v=(1-2*(py+0.5f)/c.H)*c.tanHalf;
    return norm(c.fwd+c.right*u+c.up*v); }
static inline float aces(float x){x*=0.9f;return std::min(1.0f,std::max(0.0f,(x*(2.51f*x+0.03f))/(x*(2.43f*x+0.59f)+0.14f)));}
static inline uint8_t enc(float v){return (uint8_t)std::lround(std::pow(aces(v),1.0f/2.2f)*255.0f);}

int main(int argc,char**argv){
    int W=800,H=450,FRAMES=28,RAYS=8; std::string outDir="Projects/Project-Drive/Diagnostics";
    std::string outName="drive_gi.ppm";
    float sunHour=13.0f;   // --sun <h> : the same hour the other render modes are given
    int REFLECT=1;   // --reflect off|sky|raytraced  (ReflectionModeCategory; default Sky, as RT-off degrades to)
    float eyeX=-6.40f,eyeY=-5.40f,eyeZ=2.20f, aimX=0.0f,aimY=0.0f,aimZ=0.70f, fovDeg=46.0f;
    for(int i=1;i<argc;++i){ std::string a=argv[i]; auto nx=[&](int d){return i+1<argc?std::atoi(argv[++i]):d;};
        auto nf=[&](float d){return i+1<argc?(float)std::atof(argv[++i]):d;};
        if(a=="--w")W=nx(W); else if(a=="--h")H=nx(H); else if(a=="--frames")FRAMES=nx(FRAMES); else if(a=="--rays")RAYS=nx(RAYS);
        else if(a=="--eye"){eyeX=nf(eyeX);eyeY=nf(eyeY);eyeZ=nf(eyeZ);}
        else if(a=="--aim"){aimX=nf(aimX);aimY=nf(aimY);aimZ=nf(aimZ);}
        else if(a=="--fov")fovDeg=nf(fovDeg);
        else if(a=="--name")outName=(i+1<argc?argv[++i]:outName);
        else if(a=="--sun")sunHour=nf(sunHour);
        else if(a=="--reflect"){ std::string v=(i+1<argc?argv[++i]:"sky"); REFLECT = v=="off"?0:(v=="raytraced"?2:1); }
        else if(a=="--out")outDir=(i+1<argc?argv[++i]:outDir); }
    using Clock=std::chrono::steady_clock; auto t0=Clock::now();

    // The engine's own celestial solver supplies the sun, exactly as DriveSceneMirror gives it to the raster,
    //    so the three render modes cannot be lit by three different suns.
    {
        Frontier::HostRuntime::CelestialSequence Sky;
        Sky.Prepare();
        Sky.Observation.LocalHours = sunHour;
        Sky.Prepare();
        const Frontier::SkyConstantRecord R = Sky.PackSkyRecord();
        const V Dir{R.SunDirection[0], R.SunDirection[1], R.SunDirection[2]};
        if (len(Dir) > 1e-4f) SUN_DIR = norm(Dir);
        // SunDirect, NOT SunRadiance. SkyConstantRecord.h labels SunDirect "panel direct-sun factor" -- it is
        //    the quantity a surface shades with, already carrying the gain, the colour and the atmospheric
        //    transmittance. SunRadiance (22.0 flat white here) is the disc's own radiance for the sky march,
        //    and using it to shade blew every upward-facing surface to white. SunDirect at this hour is
        //    (5.22, 4.56, 3.53): warm, because a 32.5-degree sun has come a long way through the air.
        const V Rad{R.SunDirect[0], R.SunDirect[1], R.SunDirect[2]};
        if (len(Rad) > 1e-6f) SUN_COL = Rad;

        // The same atmosphere the raster is handed, read back through the public accessor and baked.
        Frontier::VisibilityRaster Probe;
        Frontier::CelestialBudget Budget;
        Budget.AtmosphereSamples = 16u; Budget.AtmosphereLightSamples = 6u;
        Sky.ApplyTo(Probe, Budget);
        g_Sky = Probe.QueryCelestial();
        if (g_Sky.Enabled) BakeDome();
        std::printf("SurfelReference: sky %s (%dx%d dome, %u/%u atmosphere samples)\n",
                    g_SkyBaked ? "from AtmosphereModel" : "FALLBACK gradient",
                    kDomeU, kDomeV, g_Sky.SampleCount, g_Sky.LightSampleCount);
        std::printf("SurfelReference: sun hour %.2f -> dir (%.3f %.3f %.3f), radiance (%.2f %.2f %.2f)\n",
                    sunHour, SUN_DIR.x, SUN_DIR.y, SUN_DIR.z, SUN_COL.x, SUN_COL.y, SUN_COL.z);
    }

    g_Materials.Build();   // the authored slabs, flattened by the engine's own MaterialIndex
    std::vector<Tri> tris=BuildScene();
    V mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(auto&t:tris){ mn=vmin(mn,vmin(vmin(t.a,t.b),t.c)); mx=vmax(mx,vmax(vmax(t.a,t.b),t.c)); }
    BVH bvh; bvh.build(tris);
    auto tBVH=Clock::now();

    // Shared render-mode framing, so this sheet is comparable with the raster and ReSTIR ones pixel for pixel.
    // Overridable from the command line for turntable sequences.
    Cam cam;
    cam.eye={eyeX,eyeY,eyeZ};
    cam.fwd=norm(V{aimX,aimY,aimZ}-cam.eye);
    cam.right=norm(cross(cam.fwd,{0,0,1}));
    cam.up=cross(cam.right,cam.fwd);
    cam.tanHalf=std::tan(fovDeg*PI/180.0f/2); cam.aspect=(float)W/H; cam.W=W; cam.H=H;

    // G-buffer
    std::vector<V> gP(W*H),gN(W*H),gA(W*H); std::vector<uint8_t> gV(W*H,0); std::vector<uint32_t> gM(W*H,0u);
    par(W*H,[&](int i){ int x=i%W,y=i/W; V rd=rayDir(cam,(float)x,(float)y); int ti; float t;
        if(bvh.closest(cam.eye,rd,1e5f,ti,t)){ V P=cam.eye+rd*t; V N=tris[ti].n; if(dot(N,rd)>0)N=N*-1.0f;
            gP[i]=P; gN[i]=N; gA[i]=MaterialAlbedo(tris[ti].mat); gM[i]=tris[ti].mat; gV[i]=1; } });

    // spawn surfels on visible surface (coverage-driven: 1 per cell footprint, filled over frames)
    std::vector<Surfel> sf; sf.reserve(200000); Grid grid; grid.build(mn-V{2,2,2},mx+V{2,2,2});
    // Surfel radius scales with distance from the eye, exactly as RaytraceToggle/CpuMirror/ModeMatrix.cpp
    //    and the SurfelCommit/SurfelGIResolve pair do. The previous `return 0.9f` was a stub: one fixed
    //    0.9 m footprint over a 4.4 m car means a handful of surfels blanket the whole vehicle, which is
    //    the blotchy result that was reported. Constants are ModeMatrix's big-scene set.
    const float RMIN=0.22f, RMAX=0.70f, RADFAC=0.055f, COVERAGE_TARGET=2.4f;
    const int   SPAWN_BUDGET=4000;
    auto radiusAt=[&](V P){ float d=len(P-cam.eye); return std::min(RMAX,std::max(RMIN,RADFAC*d)); };
    auto gatherEcov=[&](V P,V N,float& covOut){ Sh3 e; float wsum=0;
        // search 3x3x3 neighbourhood
        for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            V q=P+V{(float)dx,(float)dy,(float)dz}*grid.cell; int c=grid.index(q); if(c<0||c>=(int)grid.cells.size())continue;
            for(int s:grid.cells[c]){ const Surfel& S=sf[s]; V d=P-S.pos; float dist=len(d); if(dist>=S.radius)continue;
                float wn=dot(N,S.n); if(wn<=0) continue;                     // must face the same way
                // PLANAR CUTOFF. Without it a surfel sitting on the ground lights a point on the car's
                //    flank simply because the two are within one radius of each other. That is the single
                //    worst divergence from the shipped shader and it is exactly why the GI looked wrong on
                //    the vehicle while looking fine on a large flat floor -- a floor has no neighbouring
                //    surface at a different orientation to leak from.
                float planar=std::fabs(dot(d,S.n)); if(planar>S.radius*0.5f) continue;
                float wd=1.0f-dist/S.radius; wd*=wd;                          // SQUARED falloff, as the shader
                float vis=1.0f;
                if(dist>1e-4f) vis=RadialVisibility(S, d*(-1.0f/dist), dist);   // surfel -> shading point
                float w=wn*wd*vis; if(w<=0) continue; e=e+S.E*w; wsum+=w; } }
        covOut=wsum; return wsum>1e-4f? ShIrradiance(e*(1.0f/wsum), N) : V{0,0,0}; };
    auto gatherE=[&](V P,V N){ float c; return gatherEcov(P,N,c); };
    auto gatherCoverage=[&](V P,V N){ float c; gatherEcov(P,N,c); return c; };

    auto spawnPass=[&](int frame){ RNG rr(hashi(frame*2654435761u+7u));
        // COVERAGE-DRIVEN spawning, as the proven mirror does it: seed where the field is thin, not at a
        //    flat 4% of pixels with a fixed 0.75 m dedupe. The old rule left the car under-covered because
        //    one surfel within 0.75 m suppressed every later candidate across the whole bodywork.
        int budget=SPAWN_BUDGET, stride=3, off=frame%(stride*stride);
        int ox=off%stride, oy=off/stride;
        for(int y=oy;y<H&&budget>0;y+=stride) for(int x=ox;x<W&&budget>0;x+=stride){
            int i=y*W+x; if(!gV[i]) continue;
            V P=gP[i];
            float cov=gatherCoverage(P,gN[i]);
            if(cov>=COVERAGE_TARGET) continue;
            if(rr.f() > (1.0f-cov/COVERAGE_TARGET)*0.9f+0.1f) continue;
            Surfel s; s.pos=P; s.n=gN[i]; s.albedo=gA[i]; s.radius=radiusAt(P);
            // WARM START, isotropically: the gather hands back irradiance and s.E now STORES irradiance, so
            //    inverting the L0 term seeds a flat SH of the right magnitude rather than letting a new surfel
            //    pop black. (While E was albedo-premultiplied these two were different quantities.)
            { const V E0=gatherE(P,gN[i]); s.E.c[0]=E0*(1.0f/(3.14159265f*0.282095f)); }
            s.age=4u;
            --budget;
            sf.push_back(s); grid.add((int)sf.size()-1,P); } };

    // one temporal step: each surfel casts hemisphere rays, gathers direct at the hit, running-mean into E
    auto temporal=[&](int frame){ int n=(int)sf.size();
        par(n,[&](int si){ Surfel& s=sf[si]; RNG r(hashi((uint32_t)si*2246822519u ^ (uint32_t)frame*3266489917u));
            Sh3 sh; const float w=6.28318531f/(float)RAYS;   // uniform hemisphere: pdf = 1/2pi
            for(int k=0;k<RAYS;++k){ V d=hemiUniform(s.n,r.f(),r.f()); int ti; float t; V L;
                float hitDist=1e4f;
                if(bvh.closest(s.pos+s.n*0.003f,d,1e4f,ti,t)){ V P=s.pos+d*t; V N=tris[ti].n; if(dot(N,d)>0)N=N*-1.0f;
                    L=DirectLight(bvh,P,N,tris[ti].mat,d*-1.0f); hitDist=t; }
                else L=SkyColour(d);
                {   // running mean of depth and depth^2 in this direction's bin
                    const int k=DepthBin(s.n,d);
                    const float dClamp=std::min(hitDist, s.radius*4.0f);
                    const uint32_t c=++s.dCount[k]; const float a=1.0f/(float)std::min(c,256u);
                    s.dMean[k]  += (dClamp        - s.dMean[k])  * a;
                    s.dMean2[k] += (dClamp*dClamp - s.dMean2[k]) * a;
                }
                // Firefly clamp. One ray landing on the emissive tail lamp otherwise injects a bright blob that
                //    the running mean then takes dozens of frames to forget -- visible as a lingering hot patch.
                const float m=std::max(L.x,std::max(L.y,L.z)); const float kClamp=8.0f;
                if(m>kClamp) L=L*(kClamp/m);
                // IRRADIANCE, not radiosity. `L` is already the radiance LEAVING the hit surface — DirectLight
                //    evaluated that surface's own BSDF, so the colour bleed is in it. Multiplying by the SURFEL's
                //    albedo here and again by the receiver's albedo at gather time applied the albedo twice: on
                //    this scene's rubber (0.037) the indirect came out 27x too dark, and because the squaring is
                //    per channel it also shifted hue — the cone's (0.95, 0.35, 0.05) bounced as (1.00, 0.12, 0.00).
                //    ModeMatrix.cpp and SurfelIrradianceUpdate.slang both accumulate the bare radiance; so does this.
                ShAccumulate(sh,d,L,w);
            }
            s.Enew=sh; });
        for(auto& s:sf){ s.age++; float a=1.0f/std::min(s.age,64u); s.E=s.E*(1.0f-a)+s.Enew*a; } };

    for(int f=0;f<FRAMES;++f){ spawnPass(f); temporal(f); }
    auto tGI=Clock::now();


    std::vector<V> img(W*H);
    par(W*H,[&](int i){ int x=i%W,y=i/W; V rd=rayDir(cam,(float)x,(float)y);
        if(!gV[i]){ img[i]=SkyColour(rd); return; }
        V P=gP[i],N=gN[i],A=gA[i];
        V direct=DirectLight(bvh,P,N,gM[i],rd*-1.0f);

        // ── reflections: Off / Sky / Raytraced ──────────────────────────────────────────────────────────────
        // ControlCentreHost's ReflectionModeCategory, mirrored. There is deliberately NO screen-space option:
        //    a reflection is either traced against the scene or it is the sky dome sampled along the mirror
        //    vector. In surfel mode the raytracing budget is by definition absent, so GameExecution degrades
        //    Raytraced -> Sky; the mode is still accepted here so the CPU proof can show both.
        V reflection{0,0,0};
        if (REFLECT != 0)
        {
            const V R = rd - N*(2.0f*dot(rd,N));
            if (REFLECT == 2)   // raytraced: one mirror ray, shaded, with the sky as the miss
            {
                int ti2; float t2;
                if (bvh.closest(P + N*0.002f, R, 1e4f, ti2, t2))
                {
                    const V HP = P + R*t2; V HN = tris[ti2].n; if (dot(HN,R) > 0) HN = HN*-1.0f;
                    // The indirect term at the REFLECTED hit goes through that hit's material too, matching
                    //    SurfelGIResolve.slang's ShadeHit. Bare `gatherE*INV_PI` would make every reflected
                    //    surface bounce light like a white Lambertian regardless of what it is.
                    const vec3 hresp = Frontier::UnifiedMaterial::AmbientResponse(MaterialFor(tris[ti2].mat),
                                                                                  vec3(HN.x,HN.y,HN.z),
                                                                                  vec3(-R.x,-R.y,-R.z));
                    const V hbounce{hresp.x,hresp.y,hresp.z};
                    reflection = DirectLight(bvh, HP, HN, tris[ti2].mat, R*-1.0f) + hbounce*(gatherE(HP,HN)*INV_PI);
                }
                else reflection = SkyColour(R);
            }
            else reflection = SkyColour(R);   // sky dome along the mirror vector, no rays
            // Weighted by SCHLICK FRESNEL, not by the raw specular weight. The first version used
            //    SpecularWeight * (1 - SpecularRoughness), which is ~0.9 for almost every material here, so
            //    nine tenths of the sky dome was added to every surface in the scene and the frame washed out
            //    to near-white. Reflectance at normal incidence is ~0.04 for a dielectric and only approaches
            //    1 at grazing angles -- that angular falloff is the whole character of a reflection.
            const ShadingRecord& m = MaterialFor(gM[i]);
            const float CosTheta = std::min(1.0f, std::fabs(dot(N, rd*-1.0f)));
            const float Fc = (1.0f-CosTheta)*(1.0f-CosTheta)*(1.0f-CosTheta)*(1.0f-CosTheta)*(1.0f-CosTheta);
            V F0{0.04f,0.04f,0.04f};
            F0 = F0*(1.0f-m.Metalness) + V{m.BaseColor.x,m.BaseColor.y,m.BaseColor.z}*m.Metalness;
            const V Fr = F0 + (V{1,1,1}-F0)*Fc;
            const float Gloss = (1.0f-m.SpecularRoughness)*(1.0f-m.SpecularRoughness);
            reflection = V{reflection.x*Fr.x, reflection.y*Fr.y, reflection.z*Fr.z} * Gloss;
        }
        // Indirect is modulated by the material it LANDS on, through the ENGINE's material model rather than a
        //    hand-rolled Lambert. AmbientResponse is the integral of f.cos over the hemisphere for this slab, so
        //    outgoing = E/pi * response, and a clearcoated or metallic surface responds to bounce light the way
        //    it responds to any other light. `BaseColor*(1-Metalness)/pi` could not: it has no coat, no sheen,
        //    no transmission, no F82 tint, and it is the one thing UnifiedMaterialEvaluation.h exists to forbid.
        const ShadingRecord& mr = MaterialFor(gM[i]);
        const vec3 resp = Frontier::UnifiedMaterial::AmbientResponse(mr, vec3(N.x,N.y,N.z),
                                                                     vec3(-rd.x,-rd.y,-rd.z));
        V indirect = V{resp.x,resp.y,resp.z}*(gatherE(P,N)*INV_PI);
        img[i]=direct+indirect+reflection;
        (void)x;(void)y; });

    // write PPM
    std::string ppm=outDir+"/"+outName; FILE* fp=std::fopen(ppm.c_str(),"wb");
    if(fp){ std::fprintf(fp,"P6\n%d %d\n255\n",W,H); for(int i=0;i<W*H;++i){ unsigned char px[3]={enc(img[i].x),enc(img[i].y),enc(img[i].z)}; std::fwrite(px,1,3,fp);} std::fclose(fp); }

    auto tEnd=Clock::now();
    auto ms=[&](Clock::time_point a,Clock::time_point b){ return std::chrono::duration<double,std::milli>(b-a).count(); };
    std::string tim=outDir+"/surfel_timing.log";
    if(FILE* tf=std::fopen(tim.c_str(),"w")){
        std::fprintf(tf,"# Project-Drive SurfelReference timing\n");
        std::fprintf(tf,"resolution        %dx%d\n",W,H);
        std::fprintf(tf,"triangles         %zu\n",tris.size());
        std::fprintf(tf,"bvh_nodes         %zu\n",bvh.nodes.size());
        std::fprintf(tf,"surfels           %zu\n",sf.size());
        std::fprintf(tf,"gi_frames         %d (rays/surfel %d)\n",FRAMES,RAYS);
        std::fprintf(tf,"build_bvh_ms      %.1f\n",ms(t0,tBVH));
        std::fprintf(tf,"surfel_gi_ms      %.1f\n",ms(tBVH,tGI));
        std::fprintf(tf,"final_shade_ms    %.1f\n",ms(tGI,tEnd));
        std::fprintf(tf,"total_ms          %.1f\n",ms(t0,tEnd));
        std::fclose(tf);
    }
    std::printf("SurfelReference: %zu tris, %zu surfels, %dx%d, %d GI frames -> %s (%.1f ms total)\n",
                tris.size(), sf.size(), W, H, FRAMES, ppm.c_str(), ms(t0,tEnd));
    return 0;
}
