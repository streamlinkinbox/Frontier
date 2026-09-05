//============================================================================================================================================
//                                                   TLASCONTENTIONBENCHMARK.CPP
//============================================================================================================================================
// 🧩 Checks the assumption behind "just build the TLAS on a worker thread": that a spare core exists to build it
//    on. Re-runs the per-frame TLAS rebuild while N busy threads compete for the CPU.
//
//    This matters because the target host is an i3-2120 -- 2 physical cores, 4 hardware threads -- which is
//    already carrying a render thread, Jolt's job pool (hardware_concurrency-1) and miniaudio's realtime
//    callback. Adding a TLAS worker to a machine with no free lane makes the rebuild SLOWER, not faster, and
//    risks preempting the audio thread, which is the one thread that must never miss its deadline.
//
//    Build: g++ -std=c++20 -O2 -msse2 -mno-avx -pthread -I ExternalPackages/tinybvh \
//               Scratchpad/TlasContentionBenchmark.cpp -o /tmp/tc && /tmp/tc

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <atomic>
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
    const uint32_t N=1150;
    std::vector<BLASInstance> a(N); std::mt19937 rng(7);
    std::uniform_real_distribution<float> p(-100.f,100.f);
    for(uint32_t i=0;i<N;++i){a[i]=BLASInstance(0u);
        a[i].transform[3]=p(rng);a[i].transform[7]=p(rng);a[i].transform[11]=p(rng);}
    BVH t; t.Build(a.data(),N,blas,1);

    auto measure=[&](int busy)->double{
        std::atomic<bool> stop{false};
        std::vector<std::thread> load;
        for(int i=0;i<busy;++i) load.emplace_back([&]{ volatile double x=0; while(!stop) x+=0.5; });
        const int reps=40;
        auto s=Clock::now();
        for(int r=0;r<reps;++r){ for(uint32_t i=0;i<N/10;++i)a[i].transform[11]+=0.002f;
            t.Build(a.data(),N,blas,1); }
        auto e=Clock::now();
        stop=true; for(auto&th:load) th.join();
        return std::chrono::duration<double,std::milli>(e-s).count()/reps;
    };
    printf("TLAS rebuild, %u instances, under CPU contention (host has %u hw threads):\n",
           N, std::thread::hardware_concurrency());
    for(int busy : {0,1,2,3}){
        double ms=measure(busy);
        printf("  %d competing busy thread(s): %7.3f ms  (%5.1f%% of 16.7ms)\n",busy,ms,100*ms/16.7);
    }
    return 0;
}
