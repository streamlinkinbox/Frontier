//============================================================================================================================================
//                                                    TLASREBUILDBENCHMARK.CPP
//============================================================================================================================================
// 🧩 Answers one question with a number instead of an opinion: is a per-frame TLAS rebuild cheap enough to do
//    on the CPU, and at what instance count does that stop being true?
//
//    Rebuilds a TLAS over N instances every iteration, moving every instance first, so the measurement is the
//    real per-frame cost and not a warm cache reading the same tree twice. The BLAS builds are timed separately
//    because they happen ONCE at load, never per frame -- that distinction is the whole point of the design.
//
//    Build (SSE2, matching a pre-AVX host such as the i3-2120):
//        g++ -std=c++20 -O2 -msse2 -mno-avx -I ExternalPackages/tinybvh Scratchpad/TlasRebuildBenchmark.cpp -o /tmp/tlas && /tmp/tlas

#define TINYBVH_IMPLEMENTATION
#include "tiny_bvh.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <random>

using namespace tinybvh;
using Clock = std::chrono::high_resolution_clock;

// Build a small object-space mesh (a ball ~ 320 tris) and a big one (the room ~ 5286 tris).
static std::vector<bvhvec4> MakeMesh(uint32_t tris, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::vector<bvhvec4> v(tris * 3);
    for (uint32_t i = 0; i < tris; ++i)
    {
        float cx = d(rng), cy = d(rng), cz = d(rng);
        for (int k = 0; k < 3; ++k)
            v[i*3+k] = bvhvec4(cx + d(rng)*0.05f, cy + d(rng)*0.05f, cz + d(rng)*0.05f, 0);
    }
    return v;
}

int main()
{
    // One BLAS for the room, one for a ball. Built ONCE.
    auto roomTris = MakeMesh(5286, 1);
    auto ballTris = MakeMesh(320, 2);

    BVH room, ball;
    auto t0 = Clock::now();
    room.Build(roomTris.data(), 5286);
    auto t1 = Clock::now();
    ball.Build(ballTris.data(), 320);
    auto t2 = Clock::now();

    printf("BLAS build (ONCE, not per frame):\n");
    printf("   room  5286 tris : %8.3f ms\n", std::chrono::duration<double,std::milli>(t1-t0).count());
    printf("   ball   320 tris : %8.3f ms\n", std::chrono::duration<double,std::milli>(t2-t1).count());

    BVHBase* blasList[] = { &room, &ball };

    for (uint32_t N : { 23u, 100u, 1000u, 10000u, 100000u })
    {
        std::vector<BLASInstance> inst(N);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> p(-20.0f, 20.0f);
        for (uint32_t i = 0; i < N; ++i)
        {
            inst[i] = BLASInstance(i == 0 ? 0u : 1u);       // instance 0 = room, rest = balls
            inst[i].transform[3]  = p(rng);
            inst[i].transform[7]  = p(rng);
            inst[i].transform[11] = p(rng);
        }

        BVH tlas;
        // warm
        tlas.Build(inst.data(), N, blasList, 2);

        const int reps = N <= 1000 ? 200 : 20;
        auto s = Clock::now();
        for (int r = 0; r < reps; ++r)
        {
            // this is the real per-frame cost: transforms changed, TLAS rebuilt
            for (uint32_t i = 1; i < N; ++i) inst[i].transform[11] += 0.001f;
            tlas.Build(inst.data(), N, blasList, 2);
        }
        auto e = Clock::now();
        double ms = std::chrono::duration<double,std::milli>(e-s).count() / reps;
        printf("TLAS rebuild %7u instances : %8.4f ms/frame   (%5.1f%% of a 16.7 ms frame)\n",
               N, ms, 100.0 * ms / 16.7);
    }
    return 0;
}
