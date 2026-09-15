//============================================================================================================================================
//                                                      TLASSCALEBENCHMARK.CPP
//============================================================================================================================================
// 🧩 How a merged-TLAS full rebuild scales with TOTAL instance count, on a pre-AVX host. Answers "at what scene
//    size does the per-frame TLAS rebuild stop being free?" -- the honest budget line for a 1000+ object world.
//
//    Build: g++ -std=c++20 -O2 -msse2 -mno-avx -I ExternalPackages/tinybvh Scratchpad/TlasScaleBenchmark.cpp -o /tmp/c && /tmp/c

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <random>
using namespace tinybvh;
using Clock=std::chrono::high_resolution_clock;
static std::vector<bvhvec4> M(uint32_t t,unsigned s){
    std::mt19937 r(s);std::uniform_real_distribution<float> d(-1.f,1.f);
    std::vector<bvhvec4> v(t*3);
    for(uint32_t i=0;i<t;++i){float cx=d(r),cy=d(r),cz=d(r);
        for(int k=0;k<3;++k)v[i*3+k]=bvhvec4(cx+d(r)*0.05f,cy+d(r)*0.05f,cz+d(r)*0.05f,0);}return v;}
int main(){
    auto pt=M(500,1); BVH prop; prop.Build(pt.data(),500);
    BVHBase* blas[]={&prop};
    printf("Merged-TLAS full rebuild cost vs TOTAL instance count (SSE2, ~i3-2120 class)\n");
    printf("%8s %12s %10s\n","instances","rebuild ms","%% of 16.7ms");
    for(uint32_t N : {500u,1000u,1150u,1500u,2000u,3000u,5000u,8000u}){
        std::vector<BLASInstance> a(N); std::mt19937 rng(7);
        std::uniform_real_distribution<float> p(-100.f,100.f);
        for(uint32_t i=0;i<N;++i){a[i]=BLASInstance(0u);
            a[i].transform[3]=p(rng);a[i].transform[7]=p(rng);a[i].transform[11]=p(rng);}
        BVH t; t.Build(a.data(),N,blas,1);
        int reps=N<=2000?60:25;
        auto s=Clock::now();
        for(int r=0;r<reps;++r){ for(uint32_t i=0;i<N/10;++i)a[i].transform[11]+=0.002f;
            t.Build(a.data(),N,blas,1);} auto e=Clock::now();
        double ms=std::chrono::duration<double,std::milli>(e-s).count()/reps;
        printf("%8u %12.3f %9.1f%%%s\n",N,ms,100*ms/16.7, ms>1.67?"   <-- over 10% of frame":"");
    }
    return 0;
}
