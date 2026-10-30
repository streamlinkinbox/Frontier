/*==========================================================================================================================================
                                                 ILLUMINATION CHAIN VALIDATION (EXHIBIT)
                                     ------------------------------------------------------------------
                 Headless statistical proof for the Phase 5 portable core: deterministic streams,
                      discrepancy sequences, reservoir estimator, SVGF filter, chain declaration.
==========================================================================================================================================*/

// Oracles are analytic, not golden images (the demo's GPU proofs join at the SDK phase):
//   - RIS with a perfect target has *exactly* zero estimator variance (algebraic identity);
//   - RIS/reuse with imperfect targets is unbiased within 5-sigma Monte-Carlo bounds at fixed seeds;
//   - temporal (M-clamped) and spatial combines keep unbiasedness while cutting variance;
//   - a 256-receiver analytic test scene runs the whole §8.2 estimator chain against brute-force sums;
//   - SVGF temporal+wavelet filtering cuts RMSE against a known ground truth while holding a guided edge;
//   - the declared chain resolves in dependency order, keeps its ping-pong imports off the atlas,
//     and derives barriers without refusal.
// Usage: IlluminationChainValidation [ppm-prefix]   (writes <prefix>svgf-strip.ppm evidence image)

#include "Engine/Foundation/DeterministicRandom.h"
#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/DiscrepancySequence.h"
#include "Engine/Rendering/IlluminationUnits.h"
#include "Engine/Rendering/ReservoirSampling.h"
#include "Engine/Rendering/TransientTargetAtlas.h"
#include "Engine/Rendering/VarianceFiltering.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int Failures = 0;
int Total    = 0;

void Check(bool Condition, const char* Label)
{
    ++Total;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL  %s\n", Label);
    }
}

void Print(const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    std::vprintf(Format, Args);
    va_end(Args);
}

double MeanOf(const std::vector<double>& Values)
{
    double Sum = 0.0;
    for (double V : Values) Sum += V;
    return Values.empty() ? 0.0 : Sum / static_cast<double>(Values.size());
}

double StdOf(const std::vector<double>& Values, double Mean)
{
    double Sum = 0.0;
    for (double V : Values) Sum += (V - Mean) * (V - Mean);
    return Values.size() > 1 ? std::sqrt(Sum / static_cast<double>(Values.size() - 1)) : 0.0;
}

// Unbiasedness oracle: |mean - truth| within 5 standard errors (fixed seeds make this deterministic).
bool Unbiased(const std::vector<double>& Estimates, double Truth, const char* Label)
{
    const double Mean   = MeanOf(Estimates);
    const double Std    = StdOf(Estimates, Mean);
    const double Bound  = 5.0 * Std / std::sqrt(static_cast<double>(Estimates.size())) + 1e-9;
    const bool   Passed = std::abs(Mean - Truth) <= Bound;
    if (!Passed)
        std::printf("      [%s] mean=%.6f truth=%.6f bound=%.6f std=%.4f\n", Label, Mean, Truth, Bound, Std);
    return Passed;
}

// Inverse-CDF pick from a discrete pdf using U in [0,1).
uint32_t PickFromPdf(const std::vector<double>& Pdf, double U) noexcept
{
    double Acc = 0.0;
    for (uint32_t I = 0; I < Pdf.size(); ++I)
    {
        Acc += Pdf[I];
        if (U < Acc) return I;
    }
    return static_cast<uint32_t>(Pdf.size()) - 1;
}

// One RIS estimate of sum(Contribution) with M candidates from Proposal (pdf) and target pHat.
double RisEstimate(const std::vector<double>& Contribution, const std::vector<double>& Target,
                   const std::vector<double>& Proposal, uint32_t M, DeterministicRandom& Random)
{
    Reservoir R;
    for (uint32_t I = 0; I < M; ++I)
    {
        const uint32_t Candidate = PickFromPdf(Proposal, Random.NextUnit());
        const double   Pdf       = Proposal[Candidate];
        R.Update({Candidate, 0.0, 0.0}, Pdf > 0.0 ? Target[Candidate] / Pdf : 0.0, Random.NextUnit());
    }
    R.Finalize(Target[R.Sample().Candidate]);
    return R.Estimate(Contribution[R.Sample().Candidate]);
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::string PpmPrefix = Argc > 1 ? Argv[1] : "";

    // ------------------------------------------------------------------ deterministic streams
    {
        DeterministicRandom A(12345, 6789), B(12345, 6789), C(12345, 999);
        bool Same = true, Differs = false;
        for (int I = 0; I < 1000; ++I)
        {
            const uint32_t X = A.NextUInt32();
            Same &= (X == B.NextUInt32());
            Differs |= (X != C.NextUInt32());
        }
        Check(Same, "PCG: same seed replays bit-exactly");
        Check(Differs, "PCG: different stream diverges");

        DeterministicRandom R(42, 7);
        double Sum = 0.0; bool InRange = true;
        const int N = 100000;
        for (int I = 0; I < N; ++I) { const double U = R.NextUnit(); Sum += U; InRange &= (U >= 0.0 && U < 1.0); }
        const double MeanBound = 5.0 / std::sqrt(12.0 * N);
        Check(InRange, "PCG: NextUnit stays in [0,1)");
        Check(std::abs(Sum / N - 0.5) < MeanBound, "PCG: NextUnit mean within 5 sigma of 0.5");
        Check(R.NextBelow(0) == 0, "PCG: NextBelow(0) defined");
        bool BelowOk = true;
        uint32_t Counts[4] = {};
        for (int I = 0; I < 40000; ++I) { const uint32_t V = R.NextBelow(4); BelowOk &= V < 4; Counts[V]++; }
        for (uint32_t Count : Counts) BelowOk &= Count > 9000 && Count < 11000;
        Check(BelowOk, "PCG: NextBelow(4) roughly uniform");
    }

    // ------------------------------------------------------------------ discrepancy sequences
    {
        Check(std::abs(RadicalInverse(2, 1) - 0.5) < 1e-15, "RadicalInverse(2,1) = 1/2");
        Check(std::abs(RadicalInverse(2, 3) - 0.75) < 1e-15, "RadicalInverse(2,3) = 3/4");
        Check(std::abs(RadicalInverse(3, 3) - 1.0 / 9.0) < 1e-15, "RadicalInverse(3,3) = 1/9");
        Check(RadicalInverse(1, 5) == 0.0, "RadicalInverse base<2 refused to 0");

        // Hammersley X is perfectly stratified: exactly one point per 1/N band.
        const uint64_t N = 1024;
        std::vector<uint32_t> Bands(32, 0);
        for (uint64_t I = 0; I < N; ++I)
            Bands[static_cast<uint32_t>(HammersleyPoint(I, N).X * 32.0)]++;
        bool Stratified = true;
        for (uint32_t B : Bands) Stratified &= (B == 32);
        Check(Stratified, "Hammersley: exact 1D stratification");

        // Chi-square uniformity on a 16x16 grid: Halton must beat pseudo-random at equal count.
        const auto ChiSquare = [](const std::vector<SequencePoint>& Points) {
            double Cells[256] = {};
            for (const SequencePoint& P : Points)
            {
                const uint32_t CX = std::min(static_cast<uint32_t>(P.X * 16.0), 15u);
                const uint32_t CY = std::min(static_cast<uint32_t>(P.Y * 16.0), 15u);
                Cells[CY * 16 + CX] += 1.0;
            }
            const double Expected = static_cast<double>(Points.size()) / 256.0;
            double Chi = 0.0;
            for (double Cell : Cells) Chi += (Cell - Expected) * (Cell - Expected) / Expected;
            return Chi;
        };
        std::vector<SequencePoint> Halton, Pseudo;
        DeterministicRandom R(2024, 11);
        for (uint64_t I = 1; I <= 4096; ++I) Halton.push_back(HaltonPoint(I));
        for (int I = 0; I < 4096; ++I) Pseudo.push_back({R.NextUnit(), R.NextUnit()});
        const double ChiHalton = ChiSquare(Halton), ChiPseudo = ChiSquare(Pseudo);
        Print("discrepancy chi2: Halton %.1f vs PCG %.1f (lower is more uniform)\n", ChiHalton, ChiPseudo);
        Check(ChiHalton < ChiPseudo, "Halton chi-square beats pseudo-random at N=4096");

        // Cranley-Patterson wraps into [0,1) and inverts exactly.
        const SequencePoint P = HaltonPoint(17);
        const SequencePoint Q = CranleyPatterson(P, 0.37, 0.91);
        const SequencePoint S = CranleyPatterson(Q, -0.37 + 1.0, -0.91 + 1.0);
        Check(Q.X >= 0.0 && Q.X < 1.0 && Q.Y >= 0.0 && Q.Y < 1.0, "CranleyPatterson wraps to [0,1)");
        Check(std::abs(S.X - P.X) < 1e-12 && std::abs(S.Y - P.Y) < 1e-12, "CranleyPatterson inverts");
    }

    // ------------------------------------------------------------------ reservoir mechanics (exact)
    {
        Reservoir R;
        for (int I = 0; I < 5; ++I) R.Update({static_cast<uint32_t>(I), 0, 0}, 0.0, 0.0);
        Check(R.SampleCount() == 5 && !R.HasSample(), "reservoir: zero weights count M, never win");
        R.Finalize(1.0);
        Check(R.Weight() == 0.0 && R.Estimate(37.0) == 0.0, "reservoir: empty finalises to W=0");

        R.Reset();
        R.Update({0, 0, 0}, std::numeric_limits<double>::quiet_NaN(), 0.0);
        R.Update({1, 0, 0}, std::numeric_limits<double>::infinity(), 0.0);
        R.Update({2, 0, 0}, -3.0, 0.0);
        Check(R.SampleCount() == 3 && !R.HasSample(), "reservoir: NaN/inf/negative weights are inert");

        R.Reset();
        R.Update({0, 0, 0}, 0.0, 0.999);
        const bool Won = R.Update({7, 0, 0}, 2.0, 0.999);
        Check(Won && R.Sample().Candidate == 7, "reservoir: sole positive weight always wins");

        R.Reset();
        for (uint32_t I = 0; I < 4; ++I) R.Update({I, 0, 0}, 1.0, 0.0);   // xi=0 replaces every time
        Check(R.Sample().Candidate == 3, "reservoir: xi=0 keeps replacing (last positive wins)");

        R.ClampSampleCount(2);
        Check(R.SampleCount() == 2, "reservoir: M-clamp exact");

        // Weighted choice frequency matches w1/(w1+w2).
        DeterministicRandom Random(99, 5);
        uint32_t Wins = 0; const int Trials = 20000;
        for (int T = 0; T < Trials; ++T)
        {
            Reservoir Two;
            Two.Update({0, 0, 0}, 1.0, Random.NextUnit());
            Two.Update({1, 0, 0}, 3.0, Random.NextUnit());
            if (Two.Sample().Candidate == 1) ++Wins;
        }
        const double Frac = static_cast<double>(Wins) / Trials;
        const double Sigma = std::sqrt(0.75 * 0.25 / Trials);
        Check(std::abs(Frac - 0.75) < 5.0 * Sigma, "reservoir: win frequency matches weights");

        // Combine: M arithmetic + zero-Jacobian never wins + xi=0 forces win.
        Reservoir Source; Source.Update({9, 0, 0}, 4.0, 0.5); Source.Finalize(2.0);  // W = 4/(1*2) = 2
        Reservoir Sink;   Sink.Update({3, 0, 0}, 1.0, 0.5);
        const uint32_t Before = Sink.SampleCount();
        const bool WonZero = Sink.Combine(Source, 1.0, 0.0, 0.0);
        Check(!WonZero && Sink.Sample().Candidate == 3 && Sink.SampleCount() == Before + 1,
              "reservoir: Jacobian 0 counts M but cannot win");
        const bool WonForced = Sink.Combine(Source, 1.0, 1.0, 0.0);
        Check(WonForced && Sink.Sample().Candidate == 9, "reservoir: combine with xi=0 takes the sample");
    }

    // ------------------------------------------------------------------ RIS estimator (statistical)
    {
        // 64 discrete lights with pseudo-random positive contributions.
        const uint32_t K = 64;
        std::vector<double> Contribution(K), TargetPerfect(K), TargetRough(K), PdfPerfect(K), PdfProposal(K), PdfUniform(K);
        DeterministicRandom Setup(777, 3);
        double SumF = 0.0, SumProposal = 0.0;
        for (uint32_t I = 0; I < K; ++I)
        {
            Contribution[I] = 0.05 + 3.0 * std::pow(Setup.NextUnit(), 2.0);
            SumF += Contribution[I];
            TargetPerfect[I] = Contribution[I];
            TargetRough[I]   = std::pow(Contribution[I], 0.7);
            SumProposal += std::pow(Contribution[I], 0.3);
        }
        // Proposal (what candidates are drawn from: a cheap proxy, e.g. light power) must differ from
        // the target (what we wish we sampled: closer to the true contribution). With proposal ==
        // target every RIS weight is the same constant and resampling degenerates — no M-scaling.
        for (uint32_t I = 0; I < K; ++I)
        {
            PdfPerfect[I]  = Contribution[I] / SumF;
            PdfProposal[I] = std::pow(Contribution[I], 0.3) / SumProposal;
            PdfUniform[I]  = 1.0 / K;
        }

        // Perfect target: every w_i = SumF exactly -> estimator is exactly SumF every trial.
        {
            DeterministicRandom R(1, 1);
            bool Exact = true;
            for (int T = 0; T < 200; ++T)
                Exact &= std::abs(RisEstimate(Contribution, TargetPerfect, PdfPerfect, 8, R) - SumF) < 1e-9;
            Check(Exact, "RIS: perfect target gives exactly zero-variance estimates");
        }

        // Rough target: unbiased, and far lower variance than single-sample uniform IS.
        const int Trials = 20000;
        std::vector<double> Ris, Naive;
        DeterministicRandom R1(2, 2), R2(3, 3);
        for (int T = 0; T < Trials; ++T) Ris.push_back(RisEstimate(Contribution, TargetRough, PdfProposal, 8, R1));
        for (int T = 0; T < Trials; ++T)
        {
            const uint32_t I = R2.NextBelow(K);
            Naive.push_back(Contribution[I] * K);
        }
        Check(Unbiased(Ris, SumF, "RIS rough"), "RIS: rough target remains unbiased");
        const double VarRis = std::pow(StdOf(Ris, MeanOf(Ris)), 2.0);
        const double VarNaive = std::pow(StdOf(Naive, MeanOf(Naive)), 2.0);
        Print("RIS variance %.4f vs naive %.4f (%.1fx lower)\n", VarRis, VarNaive, VarNaive / std::max(VarRis, 1e-12));
        Check(VarRis < 0.25 * VarNaive, "RIS: variance well below naive importance sampling");

        // Temporal reuse: frame-to-frame combine stays unbiased and tightens variance.
        std::vector<double> Single, Temporal;
        DeterministicRandom R3(4, 4), R4(5, 5);
        for (int T = 0; T < Trials; ++T) Single.push_back(RisEstimate(Contribution, TargetRough, PdfProposal, 4, R3));
        for (int T = 0; T < Trials; ++T)
        {
            Reservoir Previous;
            for (int I = 0; I < 4; ++I)
            {
                const uint32_t C = PickFromPdf(PdfProposal, R4.NextUnit());
                Previous.Update({C, 0, 0}, TargetRough[C] / PdfProposal[C], R4.NextUnit());
            }
            Previous.Finalize(TargetRough[Previous.Sample().Candidate]);
            Previous.ClampSampleCount(20 * 4);
            Reservoir Current;
            for (int I = 0; I < 4; ++I)
            {
                const uint32_t C = PickFromPdf(PdfProposal, R4.NextUnit());
                Current.Update({C, 0, 0}, TargetRough[C] / PdfProposal[C], R4.NextUnit());
            }
            Current.Combine(Previous, TargetRough[Previous.Sample().Candidate], 1.0, R4.NextUnit());
            Current.Finalize(TargetRough[Current.Sample().Candidate]);
            Temporal.push_back(Current.Estimate(Contribution[Current.Sample().Candidate]));
        }
        Check(Unbiased(Temporal, SumF, "temporal"), "temporal reuse: unbiased under static distribution");
        const double VarSingle = std::pow(StdOf(Single, MeanOf(Single)), 2.0);
        const double VarTemporal = std::pow(StdOf(Temporal, MeanOf(Temporal)), 2.0);
        Print("temporal variance %.4f vs single-frame %.4f\n", VarTemporal, VarSingle);
        Check(VarTemporal < 0.85 * VarSingle, "temporal reuse: variance reduced");

        // Distribution switch: a stale sample whose new target is zero contributes weight zero.
        {
            Reservoir Stale; Stale.Update({5, 0, 0}, 10.0, 0.5); Stale.Finalize(1.0);
            Reservoir Fresh; Fresh.Update({6, 0, 0}, 1.0, 0.5);
            const double SumBefore = Fresh.WeightSum();
            const bool Won = Fresh.Combine(Stale, 0.0, 1.0, 0.0);   // target under new frame = 0
            Check(!Won && Fresh.WeightSum() == SumBefore, "temporal: zero-target history sample is inert");
        }

        // Spatial reuse between two receivers (same discrete domain/measure -> Jacobian factor 1,
        // per-receiver targets). Receiver B must stay unbiased for its own truth.
        std::vector<double> ContributionB(K), TargetB(K), PdfB(K);
        double SumB = 0.0, SumPB = 0.0;
        for (uint32_t I = 0; I < K; ++I)
        {
            const double Geometry = 0.3 + (static_cast<double>((I * 7) % 13) / 13.0);
            ContributionB[I] = Contribution[I] * Geometry;
            SumB += ContributionB[I];
            TargetB[I] = std::pow(ContributionB[I], 0.7);
            SumPB += std::pow(ContributionB[I], 0.3);
        }
        for (uint32_t I = 0; I < K; ++I) PdfB[I] = std::pow(ContributionB[I], 0.3) / SumPB;
        std::vector<double> Spatial;
        DeterministicRandom R5(6, 6);
        for (int T = 0; T < Trials; ++T)
        {
            Reservoir NeighbourA;   // built under receiver A's target/proposal
            for (int I = 0; I < 6; ++I)
            {
                const uint32_t C = PickFromPdf(PdfProposal, R5.NextUnit());
                NeighbourA.Update({C, 0, 0}, TargetRough[C] / PdfProposal[C], R5.NextUnit());
            }
            NeighbourA.Finalize(TargetRough[NeighbourA.Sample().Candidate]);
            Reservoir ReceiverB;
            for (int I = 0; I < 3; ++I)
            {
                const uint32_t C = PickFromPdf(PdfB, R5.NextUnit());
                ReceiverB.Update({C, 0, 0}, TargetB[C] / PdfB[C], R5.NextUnit());
            }
            ReceiverB.Combine(NeighbourA, TargetB[NeighbourA.Sample().Candidate], 1.0, R5.NextUnit());
            ReceiverB.Finalize(TargetB[ReceiverB.Sample().Candidate]);
            Spatial.push_back(ReceiverB.Estimate(ContributionB[ReceiverB.Sample().Candidate]));
        }
        Check(Unbiased(Spatial, SumB, "spatial"), "spatial reuse: unbiased for the borrowing receiver");

        // Jacobian factor scales the combined weight (mechanical path of the spatial correction).
        {
            DeterministicRandom R6(8, 8);
            uint32_t Wins = 0; const int JT = 20000;
            for (int T = 0; T < JT; ++T)
            {
                Reservoir Other; Other.Update({1, 0, 0}, 2.0, 0.5); Other.Finalize(1.0); // W=2, M=1
                Reservoir Mine;  Mine.Update({0, 0, 0}, 2.0, 0.5);                        // wSum=2
                // combined w = Target(1) * J(0.5) * W(2) * M(1) = 1 -> P(win) = 1/(2+1) = 1/3
                if (Mine.Combine(Other, 1.0, 0.5, R6.NextUnit())) ++Wins;
            }
            const double Frac = static_cast<double>(Wins) / JT;
            const double Sigma = std::sqrt((1.0 / 3.0) * (2.0 / 3.0) / JT);
            Check(std::abs(Frac - 1.0 / 3.0) < 5.0 * Sigma, "combine: Jacobian factor scales win probability");
        }
    }

    // ------------------------------------------------------------------ analytic test scene (chain)
    {
        // 16x16 receivers on a floor, 32 emissive patches above. f = L * cosR * cosL / d^2 (analytic).
        struct Patch { double X, Y, Z, Radiance; };
        std::vector<Patch> Patches;
        DeterministicRandom SceneRandom(31337, 17);
        for (int I = 0; I < 32; ++I)
            Patches.push_back({SceneRandom.NextUnit() * 10.0, SceneRandom.NextUnit() * 10.0,
                               2.0 + SceneRandom.NextUnit() * 2.0, 0.5 + 9.0 * std::pow(SceneRandom.NextUnit(), 2.0)});
        double PowerSum = 0.0;
        for (const Patch& P : Patches) PowerSum += P.Radiance;
        std::vector<double> PowerPdf(Patches.size());
        for (size_t I = 0; I < Patches.size(); ++I) PowerPdf[I] = Patches[I].Radiance / PowerSum;

        const auto ContributionAt = [&](double RX, double RY, uint32_t Light) {
            const Patch& P = Patches[Light];
            const double DX = P.X - RX, DY = P.Y - RY, DZ = P.Z;
            const double D2 = DX * DX + DY * DY + DZ * DZ;
            const double D = std::sqrt(D2);
            const double Cos = DZ / D;                    // both normals face each other along z here
            return P.Radiance * Cos * Cos / D2;
        };

        double SquaredErrorChain = 0.0, SquaredErrorNaive = 0.0, TruthScale = 0.0;
        uint32_t Receivers = 0;
        std::vector<Reservoir> PreviousFrame(256);
        for (uint32_t Frame = 0; Frame < 4; ++Frame)
        {
            std::vector<Reservoir> Current(256);
            for (uint32_t RY = 0; RY < 16; ++RY)
            {
                for (uint32_t RX = 0; RX < 16; ++RX)
                {
                    const uint32_t Idx = RY * 16 + RX;
                    const double PX = RX * (10.0 / 15.0), PY = RY * (10.0 / 15.0);
                    DeterministicRandom Local(0xC0FFEEu + Idx, 1000u + Frame * 256u + Idx);
                    const double ShiftX = Local.NextUnit(), ShiftY = Local.NextUnit();
                    Reservoir& R = Current[Idx];
                    for (uint64_t C = 0; C < 6; ++C)   // initial candidates from the discrepancy sequence
                    {
                        const SequencePoint Q = CranleyPatterson(HammersleyPoint(C, 6), ShiftX, ShiftY);
                        const uint32_t Light = PickFromPdf(PowerPdf, Q.X);
                        const double PHat = ContributionAt(PX, PY, Light);
                        R.Update({Light, Q.X, Q.Y}, PowerPdf[Light] > 0.0 ? PHat / PowerPdf[Light] : 0.0, Q.Y);
                    }
                    if (Frame > 0)
                    {
                        Reservoir History = PreviousFrame[Idx];
                        History.ClampSampleCount(20 * 6);
                        R.Combine(History, ContributionAt(PX, PY, History.Sample().Candidate), 1.0, Local.NextUnit());
                    }
                    R.Finalize(ContributionAt(PX, PY, R.Sample().Candidate));
                }
            }
            // Spatial pass: borrow right and below neighbours (discrete domain, Jacobian factor 1).
            std::vector<Reservoir> Spatial = Current;
            for (uint32_t RY = 0; RY < 16; ++RY)
            {
                for (uint32_t RX = 0; RX < 16; ++RX)
                {
                    const uint32_t Idx = RY * 16 + RX;
                    const double PX = RX * (10.0 / 15.0), PY = RY * (10.0 / 15.0);
                    DeterministicRandom Local(0xBEEFu + Idx, 5000u + Frame * 256u + Idx);
                    const uint32_t Neighbours[2] = {RY * 16 + std::min(RX + 1, 15u), std::min(RY + 1, 15u) * 16 + RX};
                    for (uint32_t N : Neighbours)
                        Spatial[Idx].Combine(Current[N], ContributionAt(PX, PY, Current[N].Sample().Candidate), 1.0, Local.NextUnit());
                    Spatial[Idx].Finalize(ContributionAt(PX, PY, Spatial[Idx].Sample().Candidate));
                }
            }
            PreviousFrame = Spatial;

            if (Frame == 3)
            {
                for (uint32_t RY = 0; RY < 16; ++RY)
                {
                    for (uint32_t RX = 0; RX < 16; ++RX)
                    {
                        const uint32_t Idx = RY * 16 + RX;
                        const double PX = RX * (10.0 / 15.0), PY = RY * (10.0 / 15.0);
                        double Truth = 0.0;
                        for (uint32_t L = 0; L < Patches.size(); ++L) Truth += ContributionAt(PX, PY, L);
                        const double Estimate = Spatial[Idx].Estimate(ContributionAt(PX, PY, Spatial[Idx].Sample().Candidate));
                        DeterministicRandom NaiveRandom(0xF00Du + Idx, 9000u + Idx);
                        const uint32_t Pick = NaiveRandom.NextBelow(static_cast<uint32_t>(Patches.size()));
                        const double Naive = ContributionAt(PX, PY, Pick) * static_cast<double>(Patches.size()); // 1-sample uniform IS
                        SquaredErrorChain += (Estimate - Truth) * (Estimate - Truth);
                        SquaredErrorNaive += (Naive - Truth) * (Naive - Truth);
                        TruthScale += Truth;
                        ++Receivers;
                    }
                }
            }
        }
        const double RmseChain = std::sqrt(SquaredErrorChain / Receivers);
        const double RmseNaive = std::sqrt(SquaredErrorNaive / Receivers);
        Print("test scene: chain RMSE %.4f vs naive 1-spp %.4f (mean truth %.3f)\n",
              RmseChain, RmseNaive, TruthScale / Receivers);
        Check(RmseChain < 0.55 * RmseNaive, "test scene: full chain beats naive 1-spp RMSE decisively");
        Check(RmseChain < 0.30 * (TruthScale / Receivers), "test scene: chain RMSE under 30% of mean truth");
    }

    // ------------------------------------------------------------------ SVGF filtering
    {
        const uint32_t W = 64, H = 64, Count = W * H;
        std::vector<double> Truth(Count), Depth(Count);
        std::vector<Vec3> Normals(Count);
        for (uint32_t Y = 0; Y < H; ++Y)
        {
            for (uint32_t X = 0; X < W; ++X)
            {
                const size_t P = static_cast<size_t>(Y) * W + X;
                const double Radial = std::exp(-((X - 20.0) * (X - 20.0) + (Y - 32.0) * (Y - 32.0)) / 300.0);
                Truth[P] = 0.15 + Radial + (X >= 40 ? 0.9 : 0.0);          // guided step at x=40
                Depth[P] = X >= 40 ? 8.0 : 3.0;
                Normals[P] = X >= 40 ? Vec3{0.6f, 0.0f, 0.8f} : Vec3{0.0f, 0.0f, 1.0f};
            }
        }
        const auto Gaussian = [](DeterministicRandom& R) {
            const double U1 = std::max(R.NextUnit(), 1e-12), U2 = R.NextUnit();
            return std::sqrt(-2.0 * std::log(U1)) * std::cos(2.0 * 3.141592653589793 * U2);
        };
        DeterministicRandom Noise(2718, 28);
        std::vector<double> Noisy1(Count);
        for (size_t P = 0; P < Count; ++P) Noisy1[P] = Truth[P] * (1.0 + 0.35 * Gaussian(Noise));

        // Temporal moments over 12 independent frames.
        VarianceHistory History;
        History.Resize(W, H);
        std::vector<double> Frame(Count);
        bool Integrated = true;
        for (int F = 0; F < 12; ++F)
        {
            for (size_t P = 0; P < Count; ++P) Frame[P] = Truth[P] * (1.0 + 0.35 * Gaussian(Noise));
            Integrated &= History.Integrate(Frame.data(), W, H, 0.25);
        }
        Check(Integrated && History.FramesIntegrated() == 12, "SVGF temporal: 12 frames integrate");
        Check(!History.Integrate(Frame.data(), W / 2, H, 0.25), "SVGF temporal: size mismatch refused");

        double MeanVariance = 0.0;
        for (double V : History.Variance()) MeanVariance += V;
        MeanVariance /= Count;
        Check(MeanVariance > 0.001, "SVGF temporal: variance estimate is live under noise");

        // History exactness: constant input, reset mask behaviour.
        {
            VarianceHistory Small; Small.Resize(2, 1);
            const double C1[2] = {3.0, 5.0};
            Small.Integrate(C1, 2, 1, 0.5);
            Small.Integrate(C1, 2, 1, 0.5);
            Check(Small.Mean()[0] == 3.0 && Small.Variance()[0] == 0.0, "SVGF temporal: constant is exact");
            const double C2[2] = {9.0, 5.0};
            const uint8_t Reset[2] = {1, 0};
            Small.Integrate(C2, 2, 1, 0.5, Reset);
            Check(Small.Mean()[0] == 9.0, "SVGF temporal: reset mask restarts the pixel");
        }

        // Wavelet cascade 1,2,4,8,16 with variance propagation.
        std::vector<double> Ping = History.Mean(), Pong(Count), VarPing = History.Variance(), VarPong(Count);
        const VarianceFilterParameters Params{};
        bool StepsOk = true;
        for (uint32_t Level = 0; Level < 5; ++Level)
        {
            const bool Last = Level == 4;
            StepsOk &= VarianceWaveletStep(Ping.data(), VarPing.data(), Depth.data(), Normals.data(), W, H,
                                           1u << Level, Params, Pong.data(), Last ? nullptr : VarPong.data());
            Ping.swap(Pong);
            VarPing.swap(VarPong);
        }
        Check(StepsOk, "SVGF wavelet: five tap steps run");
        Check(!VarianceWaveletStep(nullptr, VarPing.data(), Depth.data(), Normals.data(), W, H, 1, Params,
                                   Pong.data(), nullptr), "SVGF wavelet: null input refused");

        const auto Rmse = [&](const std::vector<double>& A) {
            double S = 0.0;
            for (size_t P = 0; P < Count; ++P) S += (A[P] - Truth[P]) * (A[P] - Truth[P]);
            return std::sqrt(S / Count);
        };
        const double RmseNoisy = Rmse(Noisy1), RmseFiltered = Rmse(Ping);
        Print("SVGF: RMSE noisy %.4f -> filtered %.4f (%.1fx lower)\n", RmseNoisy, RmseFiltered, RmseNoisy / std::max(RmseFiltered, 1e-12));
        Check(RmseFiltered < 0.40 * RmseNoisy, "SVGF: filtered RMSE under 40% of single-frame noise");

        // Guided edge retention: plateau means just left/right of x=40 keep >=85% of the step the
        // truth buffer itself shows over the same windows (the radial bump makes it less than 0.9).
        double Left = 0.0, Right = 0.0, TruthLeft = 0.0, TruthRight = 0.0; int EdgeN = 0;
        for (uint32_t Y = 8; Y < 56; ++Y)
        {
            for (uint32_t X = 34; X < 38; ++X) { Left += Ping[static_cast<size_t>(Y) * W + X]; TruthLeft += Truth[static_cast<size_t>(Y) * W + X]; }
            for (uint32_t X = 42; X < 46; ++X) { Right += Ping[static_cast<size_t>(Y) * W + X]; TruthRight += Truth[static_cast<size_t>(Y) * W + X]; }
            EdgeN += 4;
        }
        const double Step = (Right - Left) / EdgeN;
        const double TrueStep = (TruthRight - TruthLeft) / EdgeN;
        Print("SVGF: guided step retained %.3f of true %.3f (%.0f%%)\n", Step, TrueStep, 100.0 * Step / TrueStep);
        Check(Step > 0.85 * TrueStep, "SVGF: depth/normal-guided edge survives the cascade");

        // Constant image passes through the wavelet untouched.
        {
            std::vector<double> Flat(Count, 2.5), FlatVar(Count, 0.01), FlatOut(Count);
            const bool Ok = VarianceWaveletStep(Flat.data(), FlatVar.data(), Depth.data(), Normals.data(),
                                                W, H, 4, Params, FlatOut.data(), nullptr);
            double MaxDelta = 0.0;
            for (double V : FlatOut) MaxDelta = std::max(MaxDelta, std::abs(V - 2.5));
            Check(Ok && (MaxDelta < 1e-9), "SVGF wavelet: constant field is preserved");
        }

        // Joint bilateral upsample respects the same guides.
        {
            const uint32_t LW = 32, LH = 32;
            std::vector<double> Low(LW * LH), LowDepth(LW * LH), HighOut(Count);
            std::vector<Vec3> LowNormals(LW * LH);
            for (uint32_t Y = 0; Y < LH; ++Y)
                for (uint32_t X = 0; X < LW; ++X)
                {
                    const size_t P = static_cast<size_t>(Y) * LW + X;
                    Low[P] = X >= 20 ? 1.8 : 0.4;
                    LowDepth[P] = X >= 20 ? 8.0 : 3.0;
                    LowNormals[P] = X >= 20 ? Vec3{0.6f, 0.0f, 0.8f} : Vec3{0.0f, 0.0f, 1.0f};
                }
            const bool Ok = JointBilateralUpsample2X(Low.data(), LowDepth.data(), LowNormals.data(), LW, LH,
                                                     Depth.data(), Normals.data(), Params, HighOut.data());
            Check(Ok, "upsample: 2x joint bilateral runs");
            const double DeepLeft = HighOut[32 * W + 20], DeepRight = HighOut[32 * W + 52];
            Check(std::abs(DeepLeft - 0.4) < 0.05 && std::abs(DeepRight - 1.8) < 0.05,
                  "upsample: plateaus land on their own side of the guide edge");
            Check(!JointBilateralUpsample2X(nullptr, LowDepth.data(), LowNormals.data(), LW, LH,
                                            Depth.data(), Normals.data(), Params, HighOut.data()),
                  "upsample: null refused");
        }

        // Evidence strip: [truth | noisy frame | filtered], each 64x64 scaled x3.
        if (!PpmPrefix.empty())
        {
            const std::string Path = PpmPrefix + "svgf-strip.ppm";
            FILE* File = std::fopen(Path.c_str(), "wb");
            if (File != nullptr)
            {
                const uint32_t Scale = 3, SW = W * 3 * Scale, SH = H * Scale;
                std::fprintf(File, "P6\n%u %u\n255\n", SW, SH);
                const auto ToByte = [](double V) {
                    const double Mapped = std::pow(std::clamp(V / 2.2, 0.0, 1.0), 1.0 / 2.2);
                    return static_cast<unsigned char>(Mapped * 255.0);
                };
                for (uint32_t Y = 0; Y < SH; ++Y)
                {
                    for (uint32_t X = 0; X < SW; ++X)
                    {
                        const uint32_t Panel = X / (W * Scale);
                        const uint32_t PX = (X % (W * Scale)) / Scale, PY = Y / Scale;
                        const double V = Panel == 0 ? Truth[PY * W + PX] : Panel == 1 ? Noisy1[PY * W + PX] : Ping[PY * W + PX];
                        const unsigned char B = ToByte(V);
                        const unsigned char RGB[3] = {B, B, B};
                        std::fwrite(RGB, 1, 3, File);
                    }
                }
                std::fclose(File);
                Print("evidence image -> %s\n", Path.c_str());
            }
        }
    }

    // ------------------------------------------------------------------ chain declaration (§8.2)
    {
        const IlluminationFrameDeclaration Frame = DeclareIlluminationFrame(1920, 1080);
        ResolvedSubmissionOrder Order;
        std::string Diagnostic;
        Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "chain: submission order resolves");
        if (!Diagnostic.empty()) Print("      diagnostic: %s\n", Diagnostic.c_str());

        const auto PositionOf = [&](const char* Name) {
            for (uint32_t Pos = 0; Pos < Order.Order.size(); ++Pos)
                if (Frame.Units[Order.Order[Pos]].Name == Name) return static_cast<int>(Pos);
            return -1;
        };
        const bool Ordered =
            PositionOf("DiscrepancySequenceSubmission") < PositionOf("DirectRadianceSubmission") &&
            PositionOf("ClusteredLightsSubmission") < PositionOf("DirectRadianceSubmission") &&
            PositionOf("DirectRadianceSubmission") < PositionOf("ReservoirTemporalSubmission") &&
            PositionOf("ReservoirTemporalSubmission") < PositionOf("ReservoirSpatialSubmission") &&
            PositionOf("ReservoirSpatialSubmission") < PositionOf("SurfaceReflectanceSubmission") &&
            PositionOf("MaterialResolveSubmission") < PositionOf("SurfaceReflectanceSubmission") &&
            PositionOf("SurfaceReflectanceSubmission") < PositionOf("VarianceTemporalSubmission") &&
            PositionOf("VarianceTemporalSubmission") < PositionOf("VarianceWaveletSubmission0") &&
            PositionOf("VarianceWaveletSubmission0") < PositionOf("VarianceWaveletSubmission4") &&
            PositionOf("VarianceWaveletSubmission4") < PositionOf("VarianceUpsampleSubmission");
        Check(Ordered, "chain: resolved order honours the §8.2 sequence");

        const auto IsImported = [&](const char* Name) {
            for (const std::string& T : Order.ImportedTargets) if (T == Name) return true;
            return false;
        };
        Check(IsImported("ReservoirsPrevious") && IsImported("SurfaceAttributes") && IsImported("LightingHistory"),
              "chain: history/surface imports detected (read before written)");

        ResolvedTransientAtlas Atlas;
        Check(ResolveTransientTargetAtlas(Frame.Units, Order, Frame.Targets, Atlas, Diagnostic),
              "chain: transient atlas resolves");
        bool PersistentOffAtlas = true;
        for (const AtlasPlacement& Placement : Atlas.Placements)
        {
            for (const RenderTargetDeclaration& Target : Frame.Targets)
                if (Target.Name == Placement.Name && Target.Persistent)
                    PersistentOffAtlas &= (Placement.Bin == UINT32_MAX);
        }
        Check(PersistentOffAtlas, "chain: ping-pong/history targets never atlas-aliased");
        Print("chain atlas: %llu bytes aliased vs %llu unaliased (%.0f%% saved)\n",
              static_cast<unsigned long long>(Atlas.TotalBytes),
              static_cast<unsigned long long>(Atlas.UnaliasedBytes),
              Atlas.UnaliasedBytes ? 100.0 * (1.0 - static_cast<double>(Atlas.TotalBytes) / static_cast<double>(Atlas.UnaliasedBytes)) : 0.0);
        Check(Atlas.TotalBytes < Atlas.UnaliasedBytes, "chain: atlas aliasing actually saves memory");

        DerivedBarriers Barriers;
        Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic),
              "chain: barriers derive without refusal");
        if (!Diagnostic.empty()) Print("      diagnostic: %s\n", Diagnostic.c_str());
        Print("chain barriers: %zu derived\n", Barriers.Barriers.size());
        Check(!Barriers.Barriers.empty(), "chain: barrier set non-empty");

        // Resolution determinism.
        ResolvedSubmissionOrder Again;
        (void)ResolveSubmissionOrder(Frame.Units, Again, Diagnostic);
        Check(Again.Order == Order.Order, "chain: resolution is deterministic");
    }

    Print("Illumination chain validation: %d/%d checks passed\n", Total - Failures, Total);
    return Failures == 0 ? 0 : 1;
}
