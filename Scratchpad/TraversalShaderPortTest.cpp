// Runs TraversalCWBVH.slang as C++ (GLSL builtins emulated) against tinybvh's own CWBVH traversal.
#include "GeometricRaster/SceneCodec.h"
#include "GeometricRaster/SceneStructure.h"
#include "GeometricRaster/TraversalIndex.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
using namespace Frontier;
// ---- minimal GLSL shim ----
struct vec3{float x,y,z;}; struct vec4{float x,y,z,w; vec3 xyz()const{return{x,y,z};}}; struct uvec2{unsigned x,y;};
static vec3 operator-(vec3 a,vec3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static unsigned floatBitsToUint(float f){unsigned u;memcpy(&u,&f,4);return u;}
static float uintBitsToFloat(unsigned u){float f;memcpy(&f,&u,4);return f;}
static int findMSB(unsigned v){return v?31-__builtin_clz(v):-1;}
static unsigned bitCount(unsigned v){return __builtin_popcount(v);}
static std::vector<vec4> CwbvhNodes, CwbvhTris;
struct TraversalHit{float t,u,v;unsigned primitive;};
static unsigned SignExtendS8x4(unsigned i){unsigned b0=(i&0x80000000u)?0xFF000000u:0,b1=(i&0x00800000u)?0x00FF0000u:0,b2=(i&0x00008000u)?0x0000FF00u:0,b3=(i&0x00000080u)?0x000000FFu:0;return b0+b1+b2+b3;}
static void UnpackU8x4(unsigned v,float o[4]){o[0]=float(v&255u);o[1]=float((v>>8)&255u);o[2]=float((v>>16)&255u);o[3]=float(v>>24);}
static void TraverseChildren(vec4 n0,vec4 n1,vec4 n2,vec4 n3,vec4 n4,vec3 O,vec3 rD,float tmax,unsigned octinv4,unsigned& hitmask){
    unsigned ew=floatBitsToUint(n0.w); int ex=int(ew&255u),ey=int((ew>>8)&255u),ez=int((ew>>16)&255u);
    ex=ex>127?ex-256:ex; ey=ey>127?ey-256:ey; ez=ez>127?ez-256:ez;
    float idirx=uintBitsToFloat(unsigned(ex+127)<<23)*rD.x, idiry=uintBitsToFloat(unsigned(ey+127)<<23)*rD.y, idirz=uintBitsToFloat(unsigned(ez+127)<<23)*rD.z;
    float origx=(n0.x-O.x)*rD.x, origy=(n0.y-O.y)*rD.y, origz=(n0.z-O.z)*rD.z; hitmask=0;
    for(unsigned half=0;half<2;++half){
        unsigned meta4=floatBitsToUint(half==0?n1.z:n1.w), isInner4=(meta4&(meta4<<1))&0x10101010u, innerMask4=SignExtendS8x4(isInner4<<3);
        unsigned bitIndex4=(meta4^(octinv4&innerMask4))&0x1F1F1F1Fu, childBits4=(meta4>>5)&0x07070707u;
        unsigned qxLo=floatBitsToUint(half==0?n2.x:n2.y),qxHi=floatBitsToUint(half==0?n3.z:n3.w),qyLo=floatBitsToUint(half==0?n2.z:n2.w),qyHi=floatBitsToUint(half==0?n4.x:n4.y),qzLo=floatBitsToUint(half==0?n3.x:n3.y),qzHi=floatBitsToUint(half==0?n4.z:n4.w);
        float lox[4],hix[4],loy[4],hiy[4],loz[4],hiz[4];
        UnpackU8x4(rD.x<0?qxHi:qxLo,lox);UnpackU8x4(rD.x<0?qxLo:qxHi,hix);UnpackU8x4(rD.y<0?qyHi:qyLo,loy);UnpackU8x4(rD.y<0?qyLo:qyHi,hiy);UnpackU8x4(rD.z<0?qzHi:qzLo,loz);UnpackU8x4(rD.z<0?qzLo:qzHi,hiz);
        for(int k=0;k<4;++k){ float cmin=std::max(std::max(std::max(lox[k]*idirx+origx,loy[k]*idiry+origy),loz[k]*idirz+origz),0.f);
            float cmax=std::min(std::min(std::min(hix[k]*idirx+origx,hiy[k]*idiry+origy),hiz[k]*idirz+origz),tmax);
            if(cmin<=cmax) hitmask|=((childBits4>>(8*k))&(k==3?0xFFFFFFFFu:255u))<<((bitIndex4>>(8*k))&(k==3?0xFFFFFFFFu:31u)); }
    }
}
static TraversalHit TraverseClosest(vec3 O,vec3 D,vec3 rD,float tmax){
    TraversalHit hit{tmax,0,0,0xFFFFFFFFu}; uvec2 stack[32]; unsigned sp=0;
    unsigned octinv4=(7u-((D.x<0?4u:0u)|(D.y<0?2u:0u)|(D.z<0?1u:0u)))*0x01010101u; uvec2 ngroup{0,0x80000000u},tgroup{0,0};
    for(unsigned guard=0;guard<100000u;++guard){
        if(ngroup.y>0x00FFFFFFu){ unsigned hits=ngroup.y,imask=ngroup.y,cbi=unsigned(findMSB(hits)),base=ngroup.x; ngroup.y&=~(1u<<cbi); if(ngroup.y>0x00FFFFFFu) stack[sp++]=ngroup;
            unsigned slot=(cbi-24u)^(octinv4&255u), rel=bitCount(imask&~(0xFFFFFFFFu<<slot)), ci=(base+rel)*5u;
            vec4 n0=CwbvhNodes[ci],n1=CwbvhNodes[ci+1],n2=CwbvhNodes[ci+2],n3=CwbvhNodes[ci+3],n4=CwbvhNodes[ci+4];
            ngroup.x=floatBitsToUint(n1.x); tgroup={floatBitsToUint(n1.y),0}; unsigned hm; TraverseChildren(n0,n1,n2,n3,n4,O,rD,hit.t,octinv4,hm);
            ngroup.y=(hm&0xFF000000u)|(floatBitsToUint(n0.w)>>24); tgroup.y=hm&0x00FFFFFFu; }
        else { tgroup=ngroup; ngroup={0,0}; }
        while(tgroup.y!=0){ unsigned ti=unsigned(findMSB(tgroup.y)), ta=tgroup.x+ti*3u; vec3 e1=CwbvhTris[ta].xyz(),e2=CwbvhTris[ta+1].xyz(); vec4 v0=CwbvhTris[ta+2]; tgroup.y-=1u<<ti;
            vec3 r=cross(D,e1); float a=dot(e2,r),f=1.f/a; vec3 s=O-v0.xyz(); float u=f*dot(s,r); vec3 q=cross(s,e2); float v=f*dot(D,q); if(u<0||v<0||u+v>1) continue; float d=f*dot(e1,q); if(d<=0||d>=hit.t) continue; hit={d,u,v,floatBitsToUint(v0.w)}; }
        if(ngroup.y<=0x00FFFFFFu){ if(sp>0) ngroup=stack[--sp]; else break; }
    }
    return hit;
}
int main(int argc,char**argv){
    for(int a=1;a<argc;++a){ SceneStructure S; std::string E; if(!SceneCodec::Decode(argv[a],S,{},&E)){printf("%s\n",E.c_str());return 1;}
        TraversalIndex X; X.Build(S.QueryFlatTriangles(),false);
        CwbvhNodes.resize(X.QueryNodeBlob().size()/4); memcpy(CwbvhNodes.data(),X.QueryNodeBlob().data(),X.QueryNodeBlob().size()*4);
        CwbvhTris.resize(X.QueryLeafBlob().size()/4); memcpy(CwbvhTris.data(),X.QueryLeafBlob().data(),X.QueryLeafBlob().size()*4);
        Vector3 lo=S.QueryBoundsMinimum(),hi=S.QueryBoundsMaximum(); std::mt19937 g(11); std::uniform_real_distribution<float> U(0,1); int N=20000,same=0,hits=0;
        for(int i=0;i<N;++i){ float O[3]={lo.x+U(g)*(hi.x-lo.x),lo.y+U(g)*(hi.y-lo.y),lo.z+U(g)*(hi.z-lo.z)}; float z=1-2*U(g),ph=6.2831853f*U(g),r=std::sqrt(std::max(0.f,1-z*z)); float D[3]={r*std::cos(ph),r*std::sin(ph),z};
            float tr; unsigned pr; bool hr=X.TraceClosest(O,D,tr,pr); vec3 o{O[0],O[1],O[2]},d{D[0],D[1],D[2]},rd{1/d.x,1/d.y,1/d.z}; TraversalHit h=TraverseClosest(o,d,rd,1e30f);
            bool hs=h.primitive!=0xFFFFFFFFu; if(hr) hits++; if(hr==hs&&(!hr||std::fabs(tr-h.t)<=1e-5f*std::max(1.f,tr))) same++; else printf("  diff: ref %d %g #%u  port %d %g #%u\n",hr,tr,pr,hs,h.t,h.primitive); }
        printf("%-10s GLSL-port vs tinybvh AVX: %d/%d identical hit/miss and distance within 1e-5 rel (%d hits)\n",S.QueryName().c_str(),same,N,hits); }
}
