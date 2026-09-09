//============================================================================================================================================
//                                                  TRAVERSALREFITBENCHMARK.CPP
//============================================================================================================================================
// 🧩 Where the per-frame acceleration-structure cost actually goes, so the budget is a measurement rather than a
//    guess. Splits the refit chain into its three stages on a showroom-sized scene.
//
//    Result on a pre-AVX host (the i3-2120 class this targets):
//        BVH::Refit       0.22 ms    O(n) bottom-up bounds — cheap, as expected
//        MBVH8 collapse   0.74 ms
//        CWBVH compress   6.31 ms    dominates; O(total nodes), re-emits STATIC geometry too
//
//    That last line is the finding that shapes the design: the cost scales with the WHOLE scene, not with the
//    moving part, because CWBVH is a packed GPU format with no partial update. A full CWBVH rebuild is 28.1 ms
//    (169% of a 16.7 ms frame), so refit is the right call — but the ceiling is real, and the way past it is a
//    true two-level split where the static BLAS is emitted once and only the dynamic BLAS is re-emitted.
//
//    Build: g++ -std=c++20 -O2 -msse2 -mno-avx -I ExternalPackages/tinybvh \
//               Scratchpad/TraversalRefitBenchmark.cpp -o /tmp/rb && /tmp/rb

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <random>
using namespace tinybvh;
using Clock=std::chrono::high_resolution_clock;
int main(){
    const uint32_t N=16806;
    std::vector<bvhvec4> v(N*3);
    std::mt19937 r(7); std::uniform_real_distribution<float> d(-2.f,2.f);
    for(uint32_t i=0;i<N;++i){float cx=d(r),cy=d(r),cz=d(r);
        for(int k=0;k<3;++k)v[i*3+k]=bvhvec4(cx+d(r)*0.02f,cy+d(r)*0.02f,cz+d(r)*0.02f,0);}
    BVH8_CWBVH cw; cw.Build(v.data(),N);
    const int reps=20; double a=0,b=0,c=0;
    for(int f=0;f<reps;++f){
        for(uint32_t i=0;i<6144*3;++i) v[i].z += 0.0001f;
        auto t0=Clock::now(); cw.bvh8.bvh.Refit();               auto t1=Clock::now();
        cw.bvh8.ConvertFrom(cw.bvh8.bvh,true);                   auto t2=Clock::now();
        cw.ConvertFrom(cw.bvh8,true);                            auto t3=Clock::now();
        a+=std::chrono::duration<double,std::milli>(t1-t0).count();
        b+=std::chrono::duration<double,std::milli>(t2-t1).count();
        c+=std::chrono::duration<double,std::milli>(t3-t2).count();
    }
    printf("  BVH::Refit          %.3f ms\n", a/reps);
    printf("  MBVH8 collapse      %.3f ms\n", b/reps);
    printf("  CWBVH compress      %.3f ms\n", c/reps);
    return 0;
}
