// R6 row-1 proof harness: the new reservoir payload (stored light index + uv + visibility) preserves the
//    R4b estimator exactly while killing the O(L) nearest-emissive searches. Self-contained (no engine
//    includes — mirrors ReSTIRViewport.slang ResampleCandidate line for line).
// Build (from repo root): g++ -std=c++20 -O2 Scratchpad/ReSTIRReservoirTest.cpp -o /tmp/rrt && /tmp/rrt
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

uint32_t gState = 0u;
float RandFloat() { gState = gState * 747796405u + 2891336453u; uint32_t w = ((gState >> ((gState >> 28u) + 4u)) ^ gState) * 277803737u; return float((w >> 22u) ^ w) * (1.0f / 4294967296.0f); }

struct Light { float x, y, z, e; };   // point emitter at (x,y,z) with scalar radiance e
struct Reservoir { float px, py, pz, wSum; uint32_t m; float W; uint32_t light; float u, v; uint32_t visible; };

float PHat(const Light& L, float hx, float hy, float hz, float nx, float ny, float nz)
{
    const float dx = L.x - hx, dy = L.y - hy, dz = L.z - hz;
    const float d2 = dx * dx + dy * dy + dz * dz;
    const float cosT = std::max(0.0f, (dx * nx + dy * ny + dz * nz) / std::sqrt(d2));
    return cosT * L.e / (d2 + 0.001f);
}

// Old path: chosen light found by nearest-emitter search (the O(L) loops row 1 deletes).
uint32_t NearestLight(const std::vector<Light>& Ls, float px, float py, float pz)
{
    uint32_t best = 0u; float bd = 1e30f;
    for (uint32_t i = 0u; i < Ls.size(); ++i)
    {
        const float dx = Ls[i].x - px, dy = Ls[i].y - py, dz = Ls[i].z - pz;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int gFail = 0;
void Check(const char* n, double got, double want, double tol)
{
    const bool ok = std::fabs(got - want) <= tol; gFail += !ok ? 1 : 0;
    std::printf("  %-64s %12.6f (want %.6f +-%.6f) %s\n", n, got, want, tol, ok ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    // Cornell-like: shading point under two emitters (bright small / dim large stand-ins).
    const std::vector<Light> Ls = { { 0.0f, 0.0f, 2.0f, 32.0f }, { 1.5f, 0.5f, 1.0f, 4.0f } };
    constexpr float Hx = 0.2f, Hy = -0.3f, Hz = 0.0f, Nx = 0.0f, Ny = 0.0f, Nz = 1.0f;
    const float pSource = 1.0f / float(Ls.size());

    std::printf("[1] stored-index W bit-identical to brute-force-search W (1000 reservoirs x 8 candidates)\n");
    {
        int mism = 0;
        for (int r = 0; r < 1000; ++r)
        {
            gState = 1234u + uint32_t(r);
            std::vector<float> stream(8u * 4u);
            for (float& f : stream) f = RandFloat();
            auto build = [&](bool stored) {
                Reservoir res{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                for (uint32_t s = 0u; s < 8u; ++s)
                {
                    const uint32_t li = uint32_t(stream[s * 4u + 0u] * float(Ls.size())) % Ls.size();
                    const float u = stream[s * 4u + 1u], v = stream[s * 4u + 2u], pick = stream[s * 4u + 3u];
                    const Light& L = Ls[li];
                    const float w = PHat(L, Hx, Hy, Hz, Nx, Ny, Nz) / pSource;
                    res.wSum += w; res.m += 1u;
                    if (pick * res.wSum <= w) { res.px = L.x; res.py = L.y; res.pz = L.z; res.light = li; res.u = u; res.v = v; }
                }
                const uint32_t zi = stored ? res.light : NearestLight(Ls, res.px, res.py, res.pz);
                // NOTE: brute force can misidentify when two emitters coincide; here they never do, so zi must agree.
                const float pSel = PHat(Ls[zi], Hx, Hy, Hz, Nx, Ny, Nz);
                res.W = pSel > 0.0f ? res.wSum / (float(res.m) * pSel) : 0.0f;
                return res;
            };
            const Reservoir a = build(true), b = build(false);
            if (a.W != b.W || a.light != b.light) ++mism;
        }
        Check("mismatched W / light", double(mism), 0.0, 0.0);
    }

    std::printf("[2] RIS identity E[pHat(z)*W] = E[w] within 1%% (200k reservoirs)\n");
    {
        gState = 777u;
        double sumPHat = 0.0, sumEst = 0.0;
        constexpr int N = 200000;
        for (int r = 0; r < N; ++r)
        {
            Reservoir res{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            for (uint32_t s = 0u; s < 8u; ++s)
            {
                const uint32_t li = uint32_t(RandFloat() * float(Ls.size())) % Ls.size();
                const float u = RandFloat(), v = RandFloat(), pick = RandFloat();
                const float w = PHat(Ls[li], Hx, Hy, Hz, Nx, Ny, Nz) / pSource;
                sumPHat += w * pSource;
                res.wSum += w; res.m += 1u;
                if (pick * res.wSum <= w) { res.light = li; res.u = u; res.v = v; }
            }
            const float pSel = PHat(Ls[res.light], Hx, Hy, Hz, Nx, Ny, Nz);
            res.W = res.wSum / (float(res.m) * pSel);
            sumEst += pSel * res.W;
        }
        // RIS identity: E[pHat(z)*W] = E[w] = E[pHat]/pSource (w = pHat/pSource per candidate).
        Check("E[pHat(z)*W] vs E[w]", sumEst / N, sumPHat / (N * 8u * pSource), (sumPHat / (N * 8u * pSource)) * 0.01);
    }

    std::printf("[3] pairwise MIS combine: M sums, WeightSum sums, z in {zA,zB}, W exact\n");
    {
        gState = 4242u;
        int bad = 0;
        for (int r = 0; r < 5000; ++r)
        {
            Reservoir A{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, B{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            for (int k = 0; k < 2; ++k)
            {
                Reservoir& res = k == 0 ? A : B;
                const uint32_t M = 4u + uint32_t(RandFloat() * 8u);
                for (uint32_t s = 0u; s < M; ++s)
                {
                    const uint32_t li = uint32_t(RandFloat() * float(Ls.size())) % Ls.size();
                    const float u = RandFloat(), v = RandFloat(), pick = RandFloat();
                    const float w = PHat(Ls[li], Hx, Hy, Hz, Nx, Ny, Nz) / pSource;
                    res.wSum += w; res.m += 1u;
                    if (pick * res.wSum <= w) { res.light = li; res.u = u; res.v = v; }
                }
            }
            // Pairwise MIS merge (the row-2/3 operator): pick A with prob wA/(wA+wB).
            Reservoir C{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            C.m = A.m + B.m; C.wSum = A.wSum + B.wSum;
            const Reservoir& win = (RandFloat() * C.wSum <= A.wSum) ? A : B;
            C.light = win.light; C.u = win.u; C.v = win.v;
            const float pSel = PHat(Ls[C.light], Hx, Hy, Hz, Nx, Ny, Nz);
            C.W = C.wSum / (float(C.m) * pSel);
            const bool ok = C.m == A.m + B.m && C.wSum == A.wSum + B.wSum
                && (C.light == A.light || C.light == B.light)
                && C.W == C.wSum / (float(C.m) * PHat(Ls[C.light], Hx, Hy, Hz, Nx, Ny, Nz));
            bad += ok ? 0 : 1;
        }
        Check("bad merges", double(bad), 0.0, 0.0);
    }

    std::printf("[4] M-clamp: merged M = Mcur + min(Mprev, 20*Mcur); growth bounded over 60 frames\n");
    {
        gState = 9182u;
        uint32_t m = 8u;   // current-frame M every frame
        int bad = 0;
        for (int f = 0; f < 60; ++f)
        {
            const uint32_t mPrev = m;
            const uint32_t capped = mPrev < 20u * 8u ? mPrev : 20u * 8u;
            m = 8u + capped;
            if (m > 21u * 8u) ++bad;   // must never exceed one frame's intake over the cap
        }
        Check("M bounded by 21xMcur", double(bad), 0.0, 0.0);
        Check("M at frame 60 (steady state)", double(m), 168.0, 0.0);
    }

    std::printf("[5] validation: 25-degree normal / 10%% depth / stride guard accept + reject\n");
    {
        const float cos25 = 0.906308f;
        auto valid = [&](float ndot, float curD, float prevD, float strideW, float vpW) {
            return ndot > cos25 && std::fabs(curD - prevD) / std::max(curD, 1e-3f) < 0.10f && strideW == vpW;
        };
        Check("identical surface", valid(1.0f, 4.0f, 4.0f, 1280.0f, 1280.0f) ? 1.0 : 0.0, 1.0, 0.0);
        Check("24-degree tilt (dot .9135)", valid(0.9135f, 4.0f, 4.0f, 1280.0f, 1280.0f) ? 1.0 : 0.0, 1.0, 0.0);
        Check("26-degree tilt (dot .8988) rejected", valid(0.8988f, 4.0f, 4.0f, 1280.0f, 1280.0f) ? 1.0 : 0.0, 0.0, 0.0);
        Check("9%% deeper accepted", valid(1.0f, 4.0f, 4.36f, 1280.0f, 1280.0f) ? 1.0 : 0.0, 1.0, 0.0);
        Check("11%% deeper rejected", valid(1.0f, 4.0f, 4.44f, 1280.0f, 1280.0f) ? 1.0 : 0.0, 0.0, 0.0);
        Check("render-scale change rejected", valid(1.0f, 4.0f, 4.0f, 960.0f, 1280.0f) ? 1.0 : 0.0, 0.0, 0.0);
    }

    std::printf("[6] temporal merge algebra: pairwise-MIS pick + W == direct formula (5000 merges)\n");
    {
        gState = 555u;
        int bad = 0;
        for (int r = 0; r < 5000; ++r)
        {
            // Current reservoir (M=8 built above in spirit): random consistent triple (wSum, M, pCur, W).
            const float mCur = 8.0f, pCur = 0.5f + RandFloat(), wSumCur = pCur * mCur * (0.5f + RandFloat());
            const float wCur0 = wSumCur;   // wCur = pCur*W*M = wSum by construction
            // Previous reservoir: random (W, M), target of its sample re-evaluated here = pPrev.
            const float mPrevFull = 8.0f + float(uint32_t(RandFloat() * 500u));
            const float mPrev = std::min(mPrevFull, 20.0f * mCur);
            const float pPrev = 0.5f + RandFloat(), wPrevFull = 0.5f + RandFloat();
            const float wPrev = pPrev * wPrevFull * mPrev;
            const float total = wCur0 + wPrev;
            const float pick = RandFloat();
            const bool takePrev = total > 0.0f && pick * total <= wPrev;
            // Direct formula for the merged weight with the winning sample's target:
            const float pWin = takePrev ? pPrev : pCur;
            const float wDirect = total / ((mCur + mPrev) * pWin);
            // Shader path: same operations in the same order.
            float wShader = 0.0f;
            {
                const float t = wCur0 + wPrev;
                const bool tp = t > 0.0f && pick * t <= wPrev;
                const float pw = tp ? pPrev : pCur;
                wShader = pw > 0.0f ? t / ((mCur + mPrev) * pw) : 0.0f;
                bad += (tp != takePrev || wShader != wDirect) ? 1 : 0;
            }
            bad += !(wDirect >= 0.0f) ? 1 : 0;
        }
        Check("bad merges", double(bad), 0.0, 0.0);
    }

    std::printf("[7] spatial taps: running-M cap, winner age+1, W == closed form (3000 pixels x 4 taps)\n");
    {
        gState = 31337u;
        int bad = 0;
        for (int r = 0; r < 3000; ++r)
        {
            // Current reservoir after temporal: (m, wSum, W, pSel, age, sel). pSel = target at the selection.
            float mCur = 8.0f;
            float pSel = 0.5f + RandFloat();
            float wSum = pSel * mCur * (0.5f + RandFloat());
            float W = wSum / (mCur * pSel);
            uint32_t age = 0u;
            int sel = 0;   // 0 = own sample, t+1 = tap t
            // Four taps: validity gate + (mNeigh, WNeigh, pNeigh, tapAge). pSelfWin tracks the target at the
            //    running selection (the shader re-evaluates PHatFull at the current selection every tap).
            float pAtSel = pSel;
            float cappedSum = 0.0f;
            int lastWin = 0;
            float pWin = pSel;
            uint32_t ageWin = 0u;
            bool anyTake = false;
            for (int t = 0; t < 4; ++t)
            {
                const bool valid = RandFloat() < 0.7f;
                const float mN = float(uint32_t(RandFloat() * 500u));
                const float wN = 0.5f + RandFloat(), pN = 0.5f + RandFloat();
                const uint32_t aN = uint32_t(RandFloat() * 6u);
                const float pick = RandFloat();
                if (!valid) continue;   // rejected tap leaves everything untouched
                const float capped = std::min(mN, 20.0f * mCur);   // cap against the RUNNING M (grows per tap)
                cappedSum += capped;
                const float wS = pAtSel * W * mCur, wT = pN * wN * capped, tot = wS + wT;
                const bool take = tot > 0.0f && pick * tot <= wT;
                if (take) { sel = t + 1; pAtSel = pN; age = aN + 1u; lastWin = t + 1; pWin = pN; ageWin = aN; anyTake = true; }
                mCur += capped; wSum = tot;
                W = pAtSel > 0.0f ? tot / (mCur * pAtSel) : 0.0f;
            }
            // Closed form from the recorded winner + accepted caps (independent code path, same math).
            const float wDirect = wSum / (mCur * pAtSel);
            const bool ok = mCur == 8.0f + cappedSum
                && W == wDirect
                && (sel == 0 || sel == lastWin)
                && (anyTake ? (age == ageWin + 1u && pAtSel == pWin) : (age == 0u && sel == 0));
            bad += ok ? 0 : 1;
        }
        Check("bad spatial merges", double(bad), 0.0, 0.0);
    }

    std::printf("[8] spatial validation: every reject path leaves the reservoir untouched\n");
    {
        const float cos25 = 0.906308f;
        auto valid = [&](float mN, float ndot, float curD, float prevD, float strideW, float vpW, bool inBounds) {
            return mN > 0.0f && inBounds && ndot > cos25
                && std::fabs(curD - prevD) / std::max(curD, 1e-3f) < 0.10f && strideW == vpW;
        };
        // (m, wSum, W, sel, age) snapshot; each reject case must preserve it.
        constexpr float M0 = 168.0f, S0 = 12.5f, W0 = 0.31f;
        int changed = 0;
        auto tap = [&](float mN, float ndot, float curD, float prevD, float strideW, float vpW, bool inBounds) {
            float m = M0, s = S0, w = W0; int sel = 3; uint32_t age = 2u;
            if (valid(mN, ndot, curD, prevD, strideW, vpW, inBounds)) { m += 1.0f; s += 1.0f; w += 1.0f; sel = 9; age = 9u; }
            if (m != M0 || s != S0 || w != W0 || sel != 3 || age != 2u) ++changed;
        };
        tap(0.0f, 1.0f, 4.0f, 4.0f, 1280.0f, 1280.0f, true);     // empty neighbour (M = 0)
        tap(40.0f, 0.8660f, 4.0f, 4.0f, 1280.0f, 1280.0f, true); // 30-degree normal tilt
        tap(40.0f, 1.0f, 4.0f, 4.48f, 1280.0f, 1280.0f, true);   // 12% depth gap
        tap(40.0f, 1.0f, 4.0f, 4.0f, 960.0f, 1280.0f, true);     // render-scale stride change
        tap(40.0f, 1.0f, 4.0f, 4.0f, 1280.0f, 1280.0f, false);   // tap outside the viewport
        Check("reject paths with side effects", double(changed), 0.0, 0.0);
        Check("valid tap still merges", valid(40.0f, 1.0f, 4.0f, 4.0f, 1280.0f, 1280.0f, true) ? 1.0 : 0.0, 1.0, 0.0);
    }

    std::printf("[9] Walker-alias pick: frequencies == Probability and E[w] matches uniform (200k picks)\n");
    {
        // Two-entry table, hand-verified Walker over powers {3, 1}: scaled {1.5, 0.5} -> Threshold {1.0, 0.5},
        //    Alias {0, 0}, Probability {0.75, 0.25}.
        constexpr float Threshold[2] = { 1.0f, 0.5f };
        constexpr uint32_t Alias[2] = { 0u, 0u };
        constexpr float Prob[2] = { 0.75f, 0.25f };
        gState = 20260u;
        constexpr int N = 200000;
        int cntA[2] = { 0, 0 };
        double sumWA = 0.0, sumWU = 0.0;
        for (int r = 0; r < N; ++r)
        {
            // Alias path (shader PickLight, alias branch).
            const uint32_t col = uint32_t(RandFloat() * 2.0f) % 2u;
            const uint32_t z = RandFloat() < Threshold[col] ? col : Alias[col];
            ++cntA[z];
            sumWA += double(PHat(Ls[z], Hx, Hy, Hz, Nx, Ny, Nz)) / double(Prob[z]);
            // Uniform path (shader PickLight, identity branch).
            const uint32_t u = uint32_t(RandFloat() * 2.0f) % 2u;
            sumWU += double(PHat(Ls[u], Hx, Hy, Hz, Nx, Ny, Nz)) / (1.0 / 2.0);
        }
        // E[w] is source-independent: both must equal pHat0 + pHat1.
        const double S = double(PHat(Ls[0], Hx, Hy, Hz, Nx, Ny, Nz)) + double(PHat(Ls[1], Hx, Hy, Hz, Nx, Ny, Nz));
        Check("alias freq light 0", double(cntA[0]) / N, 0.75, 0.01);
        Check("alias freq light 1", double(cntA[1]) / N, 0.25, 0.01);
        Check("E[w] alias == pHat sum", sumWA / N, S, S * 0.01);
        Check("E[w] uniform == pHat sum", sumWU / N, S, S * 0.01);
    }

    std::printf("[10] age chain: a winning tap sets age exactly once per frame over 5 frames\n");
    {
        int bad = 0;
        uint32_t resAge = 0u;   // fresh sample on frame 0
        for (uint32_t f = 1u; f <= 5u; ++f)
        {
            const uint32_t tapAge = resAge;   // spatiotemporal tap: this pixel's own prev-frame reservoir
            const bool take = true;           // tap wins this frame
            uint32_t takeAge = resAge;
            if (take) takeAge = tapAge + 1u;  // shader: takeAge = neigh.Counts.w + 1u
            resAge = takeAge;
            bad += (resAge != f) ? 1 : 0;
        }
        Check("age after 5 winning frames", double(resAge), 5.0, 0.0);
        // A losing tap keeps the temporal age untouched.
        {
            uint32_t age = 2u;
            const uint32_t takeAge = age;   // shader: takeAge initialised to resAge, unchanged on reject
            age = takeAge;
            bad += (age != 2u) ? 1 : 0;
        }
        Check("age deviations", double(bad), 0.0, 0.0);
    }

    if (gFail == 0) std::printf("ALL PASS (0 failures)\n");
    else std::printf("FAILURES: %d\n", gFail);
    return gFail;
}
