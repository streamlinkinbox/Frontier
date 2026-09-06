//============================================================================================================================================
//                                                 TLASSPLITTRAVERSALBENCHMARK.CPP
//============================================================================================================================================
// 🧩 Measures the COST of the obvious optimisation, not just its benefit. Splitting static and dynamic objects
//    into two TLASes makes the per-frame rebuild ~14x cheaper -- but every ray must then traverse two trees.
//    This measures that second half. The result refuted the split: see
//    References/DynamicGeometryPlan-TwoLevelTraversal.md §2b.
//
//    Build: g++ -std=c++20 -O2 -msse2 -mno-avx -I ExternalPackages/tinybvh Scratchpad/TlasSplitTraversalBenchmark.cpp -o /tmp/t && /tmp/t

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <random>
using namespace tinybvh;
using Clock=std::chrono::high_resolution_clock;
static std::vector<bvhvec4> MakeMesh(uint32_t t,unsigned s){
    std::mt19937 r(s); std::uniform_real_distribution<float> d(-1.f,1.f);
    std::vector<bvhvec4> v(t*3);
    for(uint32_t i=0;i<t;++i){float cx=d(r),cy=d(r),cz=d(r);
        for(int k=0;k<3;++k)v[i*3+k]=bvhvec4(cx+d(r)*0.05f,cy+d(r)*0.05f,cz+d(r)*0.05f,0);}
    return v;}
int main(){
    auto pt=MakeMesh(500,1); BVH prop; prop.Build(pt.data(),500);
    auto ct=MakeMesh(2000,2); BVH car; car.Build(ct.data(),2000);
    BVHBase* blas[]={&prop,&car};
    const uint32_t S=1000,D=100;
    std::mt19937 rng(7); std::uniform_real_distribution<float> p(-100.f,100.f);
    std::vector<BLASInstance> all(S+D),sta(S),dyn(D);
    for(uint32_t i=0;i<S+D;++i){all[i]=BLASInstance(i<S?0u:1u);
        all[i].transform[3]=p(rng);all[i].transform[7]=p(rng);all[i].transform[11]=p(rng);}
    for(uint32_t i=0;i<S;++i)sta[i]=all[i];
    for(uint32_t i=0;i<D;++i)dyn[i]=all[S+i];
    BVH one,ts,td;
    one.Build(all.data(),S+D,blas,2);
    ts.Build(sta.data(),S,blas,2);
    td.Build(dyn.data(),D,blas,2);

    const int R=200000;
    std::mt19937 r2(11); std::uniform_real_distribution<float> o(-120.f,120.f),dd(-1.f,1.f);
    std::vector<Ray> rays; rays.reserve(R);
    for(int i=0;i<R;++i){
        bvhvec3 O(o(r2),o(r2),o(r2)); bvhvec3 Dv(dd(r2),dd(r2),dd(r2));
        float L=sqrtf(Dv.x*Dv.x+Dv.y*Dv.y+Dv.z*Dv.z); Dv=bvhvec3(Dv.x/L,Dv.y/L,Dv.z/L);
        rays.push_back(Ray(O,Dv));
    }
    auto s=Clock::now(); for(auto rr:rays) one.Intersect(rr); auto e=Clock::now();
    double a=std::chrono::duration<double,std::milli>(e-s).count();
    s=Clock::now(); for(auto rr:rays){ Ray r1=rr; ts.Intersect(r1); Ray r2b=rr; r2b.hit.t=r1.hit.t; td.Intersect(r2b);} e=Clock::now();
    double b=std::chrono::duration<double,std::milli>(e-s).count();
    printf("Traversal of %d rays, 1100 instances:\n",R);
    printf("   ONE merged TLAS      : %8.2f ms  (%.3f us/ray)\n",a,a*1000/R);
    printf("   TWO TLAS (static+dyn): %8.2f ms  (%.3f us/ray)   overhead %+.1f%%\n",b,b*1000/R,100.0*(b-a)/a);
    return 0;
}
