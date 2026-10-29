/*==========================================================================================================================================
                                             REPLAYSAMPLEHISTORYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate current-surface seed retracing, subpixel motion, shadow-edge changes and history guards.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           SAMPLE HISTORY FIXTURE
//------------------------------------------------------------------------------------------------------------------------

std::array<float,4> ReplaySampleSlot(const LightComputeSubmission& Compute, uint32_t Slot, uint32_t Pixel)
{
    const size_t N = Compute.ReceiverCount;
    Require(Slot <= 20 && Pixel < N, "Replay history slot is outside the receiver domain");
    const size_t Index = (25 + size_t(Slot)) * N + 13889 + SolarSourceCount + Pixel;
    Require((Index + 1) * 16 <= Compute.Buffers[6].Size, "Replay history slot exceeds output storage");
    std::array<float,4> Value{};
    std::memcpy(Value.data(), static_cast<const char*>(Compute.Buffers[6].Address) + Index * 16, 16);
    return Value;
}
void SetReplaySampleSlot(LightComputeSubmission& Compute, uint32_t Slot, uint32_t Pixel,
                         const std::array<float,4>& Value)
{
    const size_t N = Compute.ReceiverCount;
    Require(Slot <= 20 && Pixel < N, "Replay history write is outside the receiver domain");
    const size_t Index = (25 + size_t(Slot)) * N + 13889 + SolarSourceCount + Pixel;
    Require((Index + 1) * 16 <= Compute.Buffers[6].Size, "Replay history write exceeds output storage");
    std::memcpy(static_cast<char*>(Compute.Buffers[6].Address) + Index * 16, Value.data(), 16);
}
uint32_t ReplaySampleSeed(float Low, float High)
{
    Require(std::isfinite(Low) && std::isfinite(High) && Low >= 0 && Low <= 65535 &&
                High >= 0 && High <= 65535 && std::floor(Low) == Low && std::floor(High) == High,
            "Invalid retained seed encoding");
    return uint32_t(Low) | (uint32_t(High) << 16u);
}
uint32_t ReplayFreshSeed(uint32_t FrameSeed, uint32_t Pixel)
{
    uint32_t Bits = FrameSeed ^ (Pixel * 747796405u + 2891336453u);
    return NextReplayTailRandom(Bits);
}
// 📝 Combined mode banks the generator stream's next unused draw (after the fresh candidates), so a banked
//    seed never competed in the reservoir whose temporal weight it may later accompany.
uint32_t ReplayBankSeed(uint32_t FrameSeed, uint32_t Pixel, uint32_t Candidates)
{
    uint32_t Bits = FrameSeed ^ (Pixel * 747796405u + 2891336453u);
    for (uint32_t Draw = 0; Draw < (Candidates > 0 ? Candidates : 1); ++Draw) NextReplayTailRandom(Bits);
    return NextReplayTailRandom(Bits);
}
std::vector<std::array<float,4>> ExecuteReplaySampleHistory(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Reset, bool History, uint32_t Bounces = 1,
    const std::array<float,3>& Environment = {0,0,0}, uint32_t Candidates = 1,
    bool Temporal = false, bool Spatial = false, bool Reconstruct = false, bool Rough = false,
    bool Textured = false, bool Primary = true, bool SampleFacets = false, bool SpatialDisk = false,
    bool BalanceSampling = false)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, Temporal, Spatial, Reset, Environment, .01f, Primary,
                                 false, Rough, SampleFacets, false, false, 64, BalanceSampling, Textured, false,
                                 false, false, false, false, Reconstruct, Candidates, false, false, History,
                                 SpatialDisk);
}
std::vector<std::array<float,4>> ExecuteReplaySampleFacets(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Reset, bool History, uint32_t Bounces,
    const std::array<float,3>& Environment)
{
    return ExecuteReplaySampleHistory(Compute, Scene, Seed, Reset, History, Bounces, Environment,
                                      1, false, false, false, false, false, true, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                   INDEPENDENT ONE-BOUNCE SHADOW ORACLE
//------------------------------------------------------------------------------------------------------------------------

struct ReplaySampleOracleResult { float Radiance = 0; bool Boundary = false; };
// 📝 PCG is mirrored only to inspect individual draws. Rectangle intersections do not use the GPU BVH.
ReplaySampleOracleResult ReplaySamplePlaneOracle(const LightComputeReceiver& R, uint32_t Seed)
{
    const float Radius = std::sqrt(float(NextReplayTailRandom(Seed) >> 8) / 16777216.f);
    const float Azimuth = 6.283185307179586f * (float(NextReplayTailRandom(Seed) >> 8) / 16777216.f);
    const double Dx = Radius * std::cos(Azimuth), Dy = Radius * std::sin(Azimuth);
    const double Dz = std::sqrt(std::max(0.f, 1.f - Radius * Radius));
    if (Dz <= 1e-8) return {0, true};
    const double X = R.PositionHit[0], Y = R.PositionHit[1], Z = double(R.PositionHit[2]) + .001;
    const double BlockX = X + Dx * (.5 - Z) / Dz, BlockY = Y + Dy * (.5 - Z) / Dz;
    const double SourceX = X + Dx * (3 - Z) / Dz, SourceY = Y + Dy * (3 - Z) / Dz;
    const bool Blocked = std::abs(BlockX) <= .2 && std::abs(BlockY) <= .5;
    const bool HitsSource = std::abs(SourceX) <= 1 && std::abs(SourceY) <= 1;
    const double BlockEdge = std::min(std::abs(std::abs(BlockX) - .2), std::abs(std::abs(BlockY) - .5));
    const double SourceEdge = std::min(std::abs(std::abs(SourceX) - 1), std::abs(std::abs(SourceY) - 1));
    // 📝 SwiftShader's direction approximations moved one sampled source intersection by 0.00028 m.
    const bool NearBlock = std::abs(BlockX) < .202 && std::abs(BlockY) < .502 && BlockEdge < .001;
    const bool NearSource = std::abs(SourceX) < 1.002 && std::abs(SourceY) < 1.002 && SourceEdge < .001;
    return {HitsSource && !Blocked ? 1.f : 0.f, NearBlock || NearSource};
}

void ValidateReplaySampleSubpixel(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Fresh(Driver), Retraced(Driver);
    std::printf("SAMPLE_HISTORY_DEVICE name=%s type=%u api=%u\n", Retraced.Properties.deviceName,
                uint32_t(Retraced.Properties.deviceType), Retraced.Properties.apiVersion);
    std::fflush(stdout);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Retraced.Prepare(Previous, Shaders, false, false, false, false, true);
    for (uint32_t Frame = 0; Frame < 3; ++Frame)
    {
        const uint32_t Seed = 150001u + 7919u * Frame;
        (void)ExecuteReplaySampleHistory(Fresh, Previous, Seed, Frame == 0, false);
        (void)ExecuteReplaySampleHistory(Retraced, Previous, Seed, Frame == 0, true);
    }
    const auto OldGuides = Retraced.PrimaryReceivers();
    std::vector<std::array<float,4>> OldFirst, OldSecond;
    for (uint32_t Pixel = 0; Pixel < OldGuides.size(); ++Pixel)
    {
        OldFirst.push_back(ReplaySampleSlot(Retraced, 11, Pixel));
        OldSecond.push_back(ReplaySampleSlot(Retraced, 12, Pixel));
    }
    constexpr uint32_t FrameSeed = 191111;
    const auto Raw = ExecuteReplaySampleHistory(Fresh, Current, FrameSeed, false, false);
    const auto Result = ExecuteReplaySampleHistory(Retraced, Current, FrameSeed, false, true);
    const auto Guides = Retraced.PrimaryReceivers();
    const auto Counts = Retraced.ExtendedStatistics();
    uint32_t Accepted = 0, Rejected = 0, Disoccluded = 0, Compared = 0, Boundary = 0;
    uint32_t VisibilityChanges = 0, FullHistory = 0, FloorPoints = 0;
    double MaxError = 0, RawSquared = 0, RetracedSquared = 0, ReferenceDifference = 0;
    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        const auto& R = Guides[Pixel];
        const auto GatheredFirst = ReplaySampleSlot(Retraced, 13, Pixel);
        const auto GatheredSecond = ReplaySampleSlot(Retraced, 14, Pixel);
        const uint32_t Count = uint32_t(GatheredSecond[2]);
        Require(Count <= 3, "Sample-history count exceeded the bounded seed bank");
        if (Count == 0)
        {
            ++Rejected;
            Require(std::memcmp(Raw[Pixel].data(), Result[Pixel].data(), sizeof(Result[Pixel])) == 0,
                    "Rejected sample history changed the fresh output");
        }
        else
        {
            ++Accepted;
            FullHistory += Count == 3 ? 1u : 0u;
            Require(R.PositionHit[3] != 0 && R.MotionPositionToken[3] > 0, "Seed history was accepted on a miss");
            const int32_t X = int32_t(std::floor(R.MotionPixel[0] + .5f));
            const int32_t Y = int32_t(std::floor(R.MotionPixel[1] + .5f));
            Require(X >= 0 && Y >= 0 && X < int32_t(Current.Width) && Y < int32_t(Current.Height),
                    "Accepted seed history was outside the previous image");
            const uint32_t Donor = uint32_t(Y) * Current.Width + uint32_t(X);
            const auto& Old = OldGuides[Donor];
            Require(Old.MotionPositionToken[3] == R.MotionPositionToken[3] &&
                        GatheredFirst == OldFirst[Donor] && GatheredSecond == OldSecond[Donor],
                    "Seed gather did not copy the previous compatible bank");
            const auto NewFirst = ReplaySampleSlot(Retraced, 11, Pixel);
            const auto NewSecond = ReplaySampleSlot(Retraced, 12, Pixel);
            Require(ReplaySampleSeed(NewFirst[0], NewFirst[1]) == ReplayFreshSeed(FrameSeed, Pixel) &&
                        NewFirst[2] == GatheredFirst[0] && NewFirst[3] == GatheredFirst[1] &&
                        NewSecond[0] == GatheredFirst[2] && NewSecond[1] == GatheredFirst[3] &&
                        NewSecond[2] == float(std::min(Count + 1, 3u)),
                    "Seed bank retained selected paths instead of the latest unselected fresh draws");
            if (R.MotionPositionToken[3] == 1)
            {
                std::array<uint32_t,4> Seeds{ReplayFreshSeed(FrameSeed, Pixel),
                    ReplaySampleSeed(GatheredFirst[0], GatheredFirst[1]),
                    Count > 1 ? ReplaySampleSeed(GatheredFirst[2], GatheredFirst[3]) : 0u,
                    Count > 2 ? ReplaySampleSeed(GatheredSecond[0], GatheredSecond[1]) : 0u};
                float Expected = 0;
                bool Ambiguous = false;
                for (uint32_t I = 0; I <= Count; ++I)
                {
                    const auto AtCurrent = ReplaySamplePlaneOracle(R, Seeds[I]);
                    Expected += AtCurrent.Radiance;
                    Ambiguous = Ambiguous || AtCurrent.Boundary;
                    if (I > 0)
                    {
                        const auto AtPrevious = ReplaySamplePlaneOracle(Old, Seeds[I]);
                        if (!AtCurrent.Boundary && !AtPrevious.Boundary && AtCurrent.Radiance != AtPrevious.Radiance)
                            ++VisibilityChanges;
                    }
                }
                if (Ambiguous) ++Boundary;
                else
                {
                    Expected /= float(Count + 1);
                    if (std::abs(Result[Pixel][0] - Expected) > 1e-6 && MaxError == 0)
                        std::fprintf(stderr, "SAMPLE_ORACLE_DIAGNOSTIC pixel=%u pos=%.9g,%.9g,%.9g raw=%.9g result=%.9g expected=%.9g fresh=%u old=%u,%u,%u\n",
                                     Pixel, R.PositionHit[0], R.PositionHit[1], R.PositionHit[2], Raw[Pixel][0],
                                     Result[Pixel][0], Expected, Seeds[0], Seeds[1], Seeds[2], Seeds[3]);
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                        MaxError = std::max(MaxError, std::abs(double(Result[Pixel][Channel]) - Expected));
                    ++Compared;
                }
                const double Reference = ReplayCameraPlaneArea(R, 128);
                const double Coarse = ReplayCameraPlaneArea(R, 64);
                RawSquared += std::pow(double(Raw[Pixel][0]) - Reference, 2);
                RetracedSquared += std::pow(double(Result[Pixel][0]) - Reference, 2);
                ReferenceDifference += std::pow(Reference - Coarse, 2);
                ++FloorPoints;
            }
        }
        if (R.PositionHit[3] == 0 || R.MotionPixel[3] == 0) continue;
        const int32_t X = int32_t(std::floor(R.MotionPixel[0] + .5f));
        const int32_t Y = int32_t(std::floor(R.MotionPixel[1] + .5f));
        if (X < 0 || Y < 0 || X >= int32_t(Current.Width) || Y >= int32_t(Current.Height)) continue;
        const uint32_t Donor = uint32_t(Y) * Current.Width + uint32_t(X);
        if (OldGuides[Donor].MotionPositionToken[3] != R.MotionPositionToken[3])
        {
            ++Disoccluded;
            Require(Count == 0, "Seed history crossed a surface token");
        }
    }
    std::printf("SAMPLE_SUBPIXEL shift_pixels=0.5 accepted=%u rejected=%u disoccluded=%u full_history=%u "
                "oracle_pixels=%u edge_excluded=%u max_rgb_error=%.9g changed_visibility_paths=%u "
                "counter=%u history_reset=%u\n",
                Accepted, Rejected, Disoccluded, FullHistory, Compared, Boundary, MaxError, VisibilityChanges,
                Counts[23], Retraced.ReplayLastSampleHistoryReset ? 1u : 0u);
    if (FloorPoints > 0)
        std::printf("SAMPLE_AREA points=%u raw_rmse=%.9g retraced_rmse=%.9g quadrature_rmse=%.9g "
                    "fresh_paths=1 retraced_max_paths=4\n",
                    FloorPoints, std::sqrt(RawSquared / FloorPoints), std::sqrt(RetracedSquared / FloorPoints),
                    std::sqrt(ReferenceDifference / FloorPoints));
    std::fflush(stdout);
    Require(!Retraced.ReplayLastSampleHistoryReset && Accepted == Counts[23] && Accepted > 100 && FullHistory > 100 &&
                Compared > 100 && Boundary < 30 && MaxError < 1e-6 && VisibilityChanges > 0 && FloorPoints > 100,
            "Subpixel seed retracing failed its independent shadow and bank checks");

    const auto Moved = ReplayCameraPlane(.0625f, .03125f, .1f);
    const auto Reopened = ReplayCameraPlane(.0625f, .0625f);
    const auto CheckReset = [&](const LightComputeArchive& Scene, uint32_t Seed)
    {
        const auto Baseline = ExecuteReplaySampleHistory(Fresh, Scene, Seed, false, false);
        const auto Image = ExecuteReplaySampleHistory(Retraced, Scene, Seed, false, true);
        Require(Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] == 0 &&
                    ReplayCandidateImagesEqual(Baseline, Image),
                "A scene revision failed to reset the seed bank and preserve fresh output");
    };
    CheckReset(Moved, 200011);
    CheckReset(Reopened, 200033);
    const auto WrongPrevious = ReplayCameraPlane(.075f, .2f);
    CheckReset(WrongPrevious, 200055);
    std::printf("SAMPLE_SCENE_RESET blocker_and_camera=1 reopen=1 wrong_previous_camera=1 raw_bit_exact=1\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                             EXPLICIT FACET SAMPLING AND EQUAL-PATH VALIDATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySampleFacetConvergence(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t WarmFrames = 3, Batches = 16;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    const std::array<float,3> Environment{0,0,0};
    LightComputeSubmission Fresh(Driver), Retraced(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Retraced.Prepare(Previous, Shaders, false, false, false, false, true);
    std::vector<double> ReferencePoint(Previous.Receivers.size(), -1);
    std::mt19937_64 BatchSeeds(0x4d793b21ull);
    DirectGpuMoments FourBatches, RetracedBatches, DifferenceBatches;
    double ReferenceSum = 0, QuadratureSquared = 0, FourSquared = 0, RetracedSquared = 0;
    uint32_t ReferencePoints = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        for (uint32_t Frame = 0; Frame < WarmFrames; ++Frame)
            (void)ExecuteReplaySampleFacets(Retraced, Previous, uint32_t(BatchSeeds()), Frame == 0,
                                            true, 1, Environment);
        const uint32_t Seed = uint32_t(BatchSeeds());
        const auto One = ExecuteReplaySampleFacets(Fresh, Current, Seed, true, false, 1, Environment);
        const auto Replayed = ExecuteReplaySampleFacets(Retraced, Current, Seed, false, true, 1, Environment);
        const auto Guides = Retraced.PrimaryReceivers();
        std::vector<double> Four(One.size());
        for (uint32_t Pixel = 0; Pixel < One.size(); ++Pixel) Four[Pixel] = One[Pixel][0];
        for (uint32_t Path = 1; Path < 4; ++Path)
        {
            const auto Extra = ExecuteReplaySampleFacets(Fresh, Current, uint32_t(BatchSeeds()), true, false,
                                                         1, Environment);
            for (uint32_t Pixel = 0; Pixel < One.size(); ++Pixel) Four[Pixel] += Extra[Pixel][0];
        }
        Require(!Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] > 100,
                "Facet sampling did not retain compatible seed histories");
        if (Batch == 0)
        {
            for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
            {
                const auto& R = Guides[Pixel];
                if (R.MotionPositionToken[3] != 1 || ReplaySampleSlot(Retraced, 14, Pixel)[2] != 3) continue;
                const double Reference = ReplayCameraPlaneArea(R, 128), Coarse = ReplayCameraPlaneArea(R, 64);
                ReferencePoint[Pixel] = Reference;
                ReferenceSum += Reference;
                QuadratureSquared += (Reference - Coarse) * (Reference - Coarse);
                ++ReferencePoints;
            }
            Require(ReferencePoints > 500, "Facet-area reference lacks fully retained plane points");
        }
        double FourMean = 0, ReplayedMean = 0;
        uint32_t Points = 0;
        for (uint32_t Pixel = 0; Pixel < ReferencePoint.size(); ++Pixel)
        {
            if (ReferencePoint[Pixel] < 0) continue;
            Require(ReplaySampleSlot(Retraced, 14, Pixel)[2] == 3, "Facet-area seed bank changed between batches");
            const double A = Four[Pixel] / 4, B = Replayed[Pixel][0];
            Require(std::isfinite(A) && std::isfinite(B) && A >= 0 && B >= 0, "Invalid facet-area output");
            FourMean += A; ReplayedMean += B;
            FourSquared += (A - ReferencePoint[Pixel]) * (A - ReferencePoint[Pixel]);
            RetracedSquared += (B - ReferencePoint[Pixel]) * (B - ReferencePoint[Pixel]);
            ++Points;
        }
        Require(Points == ReferencePoints, "Repeated facet-area reference point count changed");
        FourBatches.Accumulate(FourMean / Points);
        RetracedBatches.Accumulate(ReplayedMean / Points);
        DifferenceBatches.Accumulate((ReplayedMean - FourMean) / Points);
    }
    const double Correction = std::sqrt(double(Batches) / (Batches - 1));
    const double FourSe = FourBatches.StandardError() * Correction, RetracedSe = RetracedBatches.StandardError() * Correction;
    const double DifferenceSe = DifferenceBatches.StandardError() * Correction;
    const double ReferenceMean = ReferenceSum / ReferencePoints, Denominator = double(Batches) * ReferencePoints;
    std::printf("SAMPLE_FACETS batches=%u points=%u reference=%.9g quadrature_rmse=%.9g "
                "four_fresh=%.9g four_se=%.9g retraced=%.9g retraced_se=%.9g "
                "four_rmse=%.9g retraced_rmse=%.9g paired_difference=%.9g paired_se=%.9g\n",
                Batches, ReferencePoints, ReferenceMean, std::sqrt(QuadratureSquared / ReferencePoints),
                FourBatches.Mean(), FourSe, RetracedBatches.Mean(), RetracedSe,
                std::sqrt(FourSquared / Denominator), std::sqrt(RetracedSquared / Denominator),
                DifferenceBatches.Mean(), DifferenceSe);
    std::fflush(stdout);
    Require(std::sqrt(QuadratureSquared / ReferencePoints) < .002 &&
                std::abs(FourBatches.Mean() - ReferenceMean) < 6 * FourSe + .003 &&
                std::abs(RetracedBatches.Mean() - ReferenceMean) < 6 * RetracedSe + .003 &&
                std::abs(DifferenceBatches.Mean()) < 6 * DifferenceSe + .003,
            "Subpixel facet samples disagree with the independent area integral or four-fresh control");
    const auto Dark = ReplayCameraPlane(.03125f, .03125f, 0, 0);
    const auto Relit = ReplayCameraPlane(.03125f, .03125f);
    const auto DarkFresh = ExecuteReplaySampleFacets(Fresh, Dark, 399001, true, false, 1, Environment);
    const auto DarkHistory = ExecuteReplaySampleFacets(Retraced, Dark, 399001, false, true, 1, Environment);
    Require(Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(DarkFresh, DarkHistory) &&
                std::all_of(DarkHistory.begin(), DarkHistory.end(), [](const auto& P)
                    { return P[0] == 0 && P[1] == 0 && P[2] == 0; }),
            "Turning the source off retained sample history or nonzero radiance");
    const auto RelitFresh = ExecuteReplaySampleFacets(Fresh, Relit, 399003, true, false, 1, Environment);
    const auto RelitHistory = ExecuteReplaySampleFacets(Retraced, Relit, 399003, false, true, 1, Environment);
    Require(Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(RelitFresh, RelitHistory), "Relighting retained old sample history");
    std::printf("SAMPLE_FACETS_RESET source_off_black=1 relit_raw_exact=1 both_reset=1\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                FOUR-BOUNCE AND EQUAL-PATH VALIDATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySampleMultibounce(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Bounces = 4, WarmFrames = 3, Batches = 16, CpuSamples = 1024;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    const std::array<float,3> Environment{.025f,.025f,.025f};
    LightComputeSubmission Fresh(Driver), Retraced(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Retraced.Prepare(Previous, Shaders, false, false, false, false, true);
    std::vector<double> ReferencePoint(Previous.Receivers.size(), -1);
    std::mt19937_64 BatchSeeds(0xac7413d9ull);
    DirectGpuMoments OneBatches, FourBatches, RetracedBatches, PairedDifference;
    double ReferenceSum = 0, ReferenceVariance = 0, OneSquared = 0, FourSquared = 0, RetracedSquared = 0;
    uint32_t ReferencePoints = 0, FirstAccepted = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        for (uint32_t Frame = 0; Frame < WarmFrames; ++Frame)
            (void)ExecuteReplaySampleHistory(Retraced, Previous, uint32_t(BatchSeeds()), Frame == 0, true,
                                             Bounces, Environment);
        const uint32_t Seed = uint32_t(BatchSeeds());
        const auto One = ExecuteReplaySampleHistory(Fresh, Current, Seed, true, false, Bounces, Environment);
        const auto Replayed = ExecuteReplaySampleHistory(Retraced, Current, Seed, false, true, Bounces, Environment);
        const auto Guides = Retraced.PrimaryReceivers();
        std::vector<std::array<double,3>> Four(One.size());
        for (uint32_t Pixel = 0; Pixel < One.size(); ++Pixel)
            for (uint32_t Channel = 0; Channel < 3; ++Channel) Four[Pixel][Channel] = One[Pixel][Channel];
        for (uint32_t Path = 1; Path < 4; ++Path)
        {
            const auto Extra = ExecuteReplaySampleHistory(Fresh, Current, uint32_t(BatchSeeds()), true, false,
                                                          Bounces, Environment);
            for (uint32_t Pixel = 0; Pixel < Extra.size(); ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel) Four[Pixel][Channel] += Extra[Pixel][Channel];
        }
        Require(!Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] > 100,
                "Subpixel multibounce frame did not retain compatible seed histories");
        if (Batch == 0)
        {
            FirstAccepted = Retraced.ExtendedStatistics()[23];
            for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
            {
                const auto& R = Guides[Pixel];
                if (R.MotionPositionToken[3] != 1 || ReplaySampleSlot(Retraced, 14, Pixel)[2] != 3) continue;
                std::mt19937_64 Random(0xe51b1c39ull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                DirectGpuMoments Reference;
                for (uint32_t Sample = 0; Sample < CpuSamples; ++Sample)
                    Reference.Accumulate(ReplayReferencePath(Current, R, Bounces, Random)[0]);
                const double Se = Reference.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                ReferencePoint[Pixel] = Reference.Mean();
                ReferenceSum += ReferencePoint[Pixel];
                ReferenceVariance += Se * Se;
                ++ReferencePoints;
            }
            Require(ReferencePoints > 500, "Subpixel multibounce reference lacks fully retained plane points");
            std::printf("SAMPLE_MULTIBOUNCE_REFERENCE cpu_paths=%u points=%u reference=%.9g cpu_mean_se=%.9g "
                        "cpu_point_rms_se=%.9g accepted=%u\n",
                        ReferencePoints * CpuSamples, ReferencePoints, ReferenceSum / ReferencePoints,
                        std::sqrt(ReferenceVariance) / ReferencePoints,
                        std::sqrt(ReferenceVariance / ReferencePoints), FirstAccepted);
            std::fflush(stdout);
        }
        double OneMean = 0, FourMean = 0, ReplayedMean = 0;
        uint32_t Points = 0;
        for (uint32_t Pixel = 0; Pixel < ReferencePoint.size(); ++Pixel)
        {
            if (ReferencePoint[Pixel] < 0) continue;
            Require(ReplaySampleSlot(Retraced, 14, Pixel)[2] == 3, "Repeated camera frame lost its full seed bank");
            const double A = One[Pixel][0], B = Four[Pixel][0] / 4, C = Replayed[Pixel][0];
            Require(std::isfinite(A) && std::isfinite(B) && std::isfinite(C) && A >= 0 && B >= 0 && C >= 0,
                    "Nonfinite or negative multibounce sample output");
            OneMean += A; FourMean += B; ReplayedMean += C;
            OneSquared += (A - ReferencePoint[Pixel]) * (A - ReferencePoint[Pixel]);
            FourSquared += (B - ReferencePoint[Pixel]) * (B - ReferencePoint[Pixel]);
            RetracedSquared += (C - ReferencePoint[Pixel]) * (C - ReferencePoint[Pixel]);
            ++Points;
        }
        Require(Points == ReferencePoints, "Repeated camera reference point count changed");
        OneBatches.Accumulate(OneMean / Points);
        FourBatches.Accumulate(FourMean / Points);
        RetracedBatches.Accumulate(ReplayedMean / Points);
        PairedDifference.Accumulate((ReplayedMean - FourMean) / Points);
        if ((Batch + 1) % 4 == 0)
        {
            std::printf("SAMPLE_MULTIBOUNCE_BATCHES completed=%u/%u\n", Batch + 1, Batches);
            std::fflush(stdout);
        }
    }
    const double Correction = std::sqrt(double(Batches) / (Batches - 1));
    const double OneSe = OneBatches.StandardError() * Correction, FourSe = FourBatches.StandardError() * Correction;
    const double RetracedSe = RetracedBatches.StandardError() * Correction;
    const double DifferenceSe = PairedDifference.StandardError() * Correction;
    const double ReferenceMean = ReferenceSum / ReferencePoints, ReferenceSe = std::sqrt(ReferenceVariance) / ReferencePoints;
    const double Denominator = double(Batches) * ReferencePoints;
    std::printf("SAMPLE_MULTIBOUNCE bounces=%u batches=%u points=%u reference=%.9g cpu_mean_se=%.9g "
                "one=%.9g one_se=%.9g four_fresh=%.9g four_se=%.9g retraced=%.9g retraced_se=%.9g "
                "one_rmse=%.9g four_rmse=%.9g retraced_rmse=%.9g\n",
                Bounces, Batches, ReferencePoints, ReferenceMean, ReferenceSe, OneBatches.Mean(), OneSe,
                FourBatches.Mean(), FourSe, RetracedBatches.Mean(), RetracedSe,
                std::sqrt(OneSquared / Denominator), std::sqrt(FourSquared / Denominator),
                std::sqrt(RetracedSquared / Denominator));
    std::printf("SAMPLE_MULTIBOUNCE_PAIRED retraced_minus_four=%.9g batch_se=%.9g "
                "current_paths_each=4 warm_frames=%u\n", PairedDifference.Mean(), DifferenceSe, WarmFrames);
    Require(std::abs(OneBatches.Mean() - ReferenceMean) < 6 * std::hypot(OneSe, ReferenceSe) + .003 &&
                std::abs(FourBatches.Mean() - ReferenceMean) < 6 * std::hypot(FourSe, ReferenceSe) + .003 &&
                std::abs(RetracedBatches.Mean() - ReferenceMean) < 6 * std::hypot(RetracedSe, ReferenceSe) + .003 &&
                std::abs(PairedDifference.Mean()) < 6 * DifferenceSe + .003,
            "Subpixel multibounce averages disagree with the independent CPU model or four-fresh control");
}

//------------------------------------------------------------------------------------------------------------------------
//                             SAMPLE HISTORY COMBINED WITH RESERVOIR REUSE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySampleCombined(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    constexpr uint32_t SequenceSeed = 191111;
    const auto ReadAllSlots = [&](uint32_t Slot)
    {
        std::vector<std::array<float,4>> Slots(Previous.Receivers.size());
        for (uint32_t Pixel = 0; Pixel < Slots.size(); ++Pixel) Slots[Pixel] = ReplaySampleSlot(Compute, Slot, Pixel);
        return Slots;
    };
    const auto RunSequence = [&](bool History, bool Spatial, bool SpatialDisk)
    {
        for (uint32_t Step = 0; Step < 3; ++Step)
        {
            const uint32_t Seed = 150001u + 7919u * Step;
            (void)ExecuteReplaySampleHistory(Compute, Previous, Seed, Step == 0, History, 1, {0,0,0}, 1,
                                             true, Spatial, false, false, false, true, false, SpatialDisk);
        }
        return ExecuteReplaySampleHistory(Compute, Current, SequenceSeed, false, History, 1, {0,0,0}, 1,
                                          true, Spatial, false, false, false, true, false, SpatialDisk);
    };
    const auto TemporalImage = RunSequence(false, false, false);
    const auto CombinedTemporalImage = RunSequence(true, false, false);
    const auto TemporalBankFirst = ReadAllSlots(11), TemporalBankSecond = ReadAllSlots(12);
    const auto ReservoirImage = RunSequence(false, true, false);
    const auto ReservoirLocals = ReadAllSlots(2);
    const auto CombinedImage = RunSequence(true, true, false);
    const auto CombinedLocals = ReadAllSlots(2);
    const auto GatheredFirstAll = ReadAllSlots(13), GatheredSecondAll = ReadAllSlots(14);
    const auto SpatialBankFirst = ReadAllSlots(11), SpatialBankSecond = ReadAllSlots(12);
    const auto SpatialCounts = Compute.ExtendedStatistics();
    const auto DiskImage = RunSequence(true, true, true);
    const auto DiskLocals = ReadAllSlots(2);
    const auto DiskBankFirst = ReadAllSlots(11), DiskBankSecond = ReadAllSlots(12);
    const auto DiskCounts = Compute.ExtendedStatistics();
    uint32_t Accepted = 0, Rejected = 0, Changed = 0, DiskChanged = 0;
    for (uint32_t Pixel = 0; Pixel < CombinedImage.size(); ++Pixel)
    {
        const auto& GatheredFirst = GatheredFirstAll[Pixel];
        const auto& GatheredSecond = GatheredSecondAll[Pixel];
        const uint32_t Count = uint32_t(GatheredSecond[2]);
        Require(Count <= 3 && std::isfinite(CombinedImage[Pixel][0]) && CombinedImage[Pixel][0] >= 0 &&
                    std::isfinite(DiskImage[Pixel][0]) && DiskImage[Pixel][0] >= 0,
                "Combined sample-history output is invalid");
        if (Count == 0)
        {
            ++Rejected;
            Require(std::memcmp(TemporalImage[Pixel].data(), CombinedTemporalImage[Pixel].data(),
                                sizeof(CombinedTemporalImage[Pixel])) == 0 &&
                        std::memcmp(ReservoirLocals[Pixel].data(), CombinedLocals[Pixel].data(),
                                    sizeof(CombinedLocals[Pixel])) == 0 &&
                        std::memcmp(ReservoirLocals[Pixel].data(), DiskLocals[Pixel].data(),
                                    sizeof(DiskLocals[Pixel])) == 0,
                    "Combined mode altered pre-spatial local reservoir or non-spatial disoccluded output");
        }
        else
        {
            ++Accepted;
            Changed += std::memcmp(ReservoirImage[Pixel].data(), CombinedImage[Pixel].data(),
                                   sizeof(CombinedImage[Pixel])) != 0 ? 1u : 0u;
            DiskChanged += std::memcmp(ReservoirImage[Pixel].data(), DiskImage[Pixel].data(),
                                       sizeof(DiskImage[Pixel])) != 0 ? 1u : 0u;
            const auto CheckBank = [&](const std::array<float,4>& NewFirst, const std::array<float,4>& NewSecond)
            {
                Require(ReplaySampleSeed(NewFirst[0], NewFirst[1]) == ReplayBankSeed(SequenceSeed, Pixel, 1) &&
                            NewFirst[2] == GatheredFirst[0] && NewFirst[3] == GatheredFirst[1] &&
                            NewSecond[2] == float(std::min(Count + 1, 3u)),
                        "Combined mode did not shift the unused-draw seed bank");
            };
            CheckBank(TemporalBankFirst[Pixel], TemporalBankSecond[Pixel]);
            CheckBank(SpatialBankFirst[Pixel], SpatialBankSecond[Pixel]);
            CheckBank(DiskBankFirst[Pixel], DiskBankSecond[Pixel]);
        }
    }
    Require(Accepted == SpatialCounts[23] && Accepted == DiskCounts[23] &&
                Accepted > 100 && Rejected > 0 && Changed > 50 && DiskChanged > 50 &&
                DiskCounts[20] > 500 && DiskCounts[22] > 1000 && DiskCounts[9] > 0,
            "Combined mode did not exercise accepted, rejected, spatial-disk and culled pixels");
    std::printf("SAMPLE_COMBINED temporal=1 spatial=1 disk=1 accepted=%u rejected=%u "
                "pre_spatial_exact=1 non_spatial_disoccluded_exact=1 changed=%u disk_changed=%u "
                "disk_links=%u disk_culled=%u bank_shift_unused_draw=1\n",
                Accepted, Rejected, Changed, DiskChanged, DiskCounts[22], DiskCounts[9]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CORRUPTION AND HOST GUARDS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySampleGuards(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayCameraPlane(0, 0);
    LightComputeSubmission Fresh(Driver), Retraced(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Retraced.Prepare(Scene, Shaders, false, false, false, false, true);
    (void)ExecuteReplaySampleHistory(Fresh, Scene, 210001, true, false);
    (void)ExecuteReplaySampleHistory(Retraced, Scene, 210001, true, true);
    const auto Guides = Retraced.PrimaryReceivers();
    std::vector<uint32_t> Pixels;
    for (uint32_t Pixel = 0; Pixel < Guides.size() && Pixels.size() < 7; ++Pixel)
        if (Guides[Pixel].MotionPositionToken[3] == 1) Pixels.push_back(Pixel);
    Require(Pixels.size() == 7, "Corrupt-bank checks need seven diffuse receiver points");
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        const uint32_t Slot = I < 4 ? 11 : 12;
        auto Bank = ReplaySampleSlot(Retraced, Slot, Pixels[I]);
        if (I == 0) Bank[0] = std::numeric_limits<float>::quiet_NaN();
        if (I == 1) Bank[0] = -1;
        if (I == 2) Bank[0] = 65536;
        if (I == 3) Bank[0] = .5f;
        if (I == 4) Bank[2] = std::numeric_limits<float>::quiet_NaN();
        if (I == 5) Bank[2] = 4;
        if (I == 6) Bank[2] = 1.5f;
        SetReplaySampleSlot(Retraced, Slot, Pixels[I], Bank);
    }
    const auto Raw = ExecuteReplaySampleHistory(Fresh, Scene, 210003, false, false);
    const auto Image = ExecuteReplaySampleHistory(Retraced, Scene, 210003, false, true);
    for (uint32_t Pixel : Pixels)
        Require(ReplaySampleSlot(Retraced, 14, Pixel)[2] == 0 && Raw[Pixel] == Image[Pixel],
                "Corrupt history was not rejected to the fresh output");
    const uint32_t Unaffected = Retraced.ExtendedStatistics()[23];
    Require(Unaffected > 100, "Uncorrupted seed histories did not remain available");
    const uint32_t Sequence = Retraced.HistorySequence;
    uint32_t Rejected = 0;
    const auto ExpectRejected = [&](auto Operation)
    {
        try { Operation(); }
        catch (const std::exception&) { ++Rejected; }
    };
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 2); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, true, false, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, true, false, false, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, false, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, false, false, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, false, false, false, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, false, false, false, false, false); });
    Require(Rejected == 7 && Retraced.HistorySequence == Sequence, "Invalid sample-history requests advanced GPU history");
    (void)ExecuteReplaySampleHistory(Retraced, Scene, 210005, false, false);
    const auto ToggleFresh = ExecuteReplaySampleHistory(Fresh, Scene, 210007, false, false);
    const auto Toggle = ExecuteReplaySampleHistory(Retraced, Scene, 210007, false, true);
    Require(Retraced.ReplayLastSampleHistoryReset && Retraced.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(ToggleFresh, Toggle), "Re-enabling sample history did not reset its bank");
    std::printf("SAMPLE_GUARDS corrupt_banks=7 rejected_raw_exact=1 unaffected=%u invalid_requests=%u "
                "sequence_unchanged=1 toggle_reset=1\n", Unaffected, Rejected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              DIAGNOSTIC READBACKS
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplaySampleHistory(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64, WarmFrames = 3, Bounces = 4;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    const std::array<float,3> Environment{.025f,.025f,.025f};
    LightComputeSubmission Fresh(Driver), Retraced(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Retraced.Prepare(Previous, Shaders, false, false, false, false, true);
    for (uint32_t Frame = 0; Frame < WarmFrames; ++Frame)
        (void)ExecuteReplaySampleFacets(Retraced, Previous, 340001u + Frame * 7919u, Frame == 0,
                                        true, Bounces, Environment);
    const auto Raw = ExecuteReplaySampleFacets(Fresh, Current, 380011, true, false, Bounces, Environment);
    const auto Result = ExecuteReplaySampleFacets(Retraced, Current, 380011, false, true, Bounces, Environment);
    std::vector<std::array<double,3>> FourSum(Raw.size());
    for (uint32_t Pixel = 0; Pixel < Raw.size(); ++Pixel)
        for (uint32_t Channel = 0; Channel < 3; ++Channel) FourSum[Pixel][Channel] = Raw[Pixel][Channel];
    for (uint32_t Path = 1; Path < 4; ++Path)
    {
        const auto Extra = ExecuteReplaySampleFacets(Fresh, Current, 380011u + Path * 7919u, true, false,
                                                     Bounces, Environment);
        for (uint32_t Pixel = 0; Pixel < Raw.size(); ++Pixel)
            for (uint32_t Channel = 0; Channel < 3; ++Channel) FourSum[Pixel][Channel] += Extra[Pixel][Channel];
    }
    std::vector<std::array<float,4>> Four(Raw.size()), Mask(Raw.size());
    uint32_t Accepted = 0, Full = 0, Rejected = 0;
    for (uint32_t Pixel = 0; Pixel < Raw.size(); ++Pixel)
    {
        for (uint32_t Channel = 0; Channel < 3; ++Channel) Four[Pixel][Channel] = float(FourSum[Pixel][Channel] / 4);
        Four[Pixel][3] = Raw[Pixel][3];
        const float Count = ReplaySampleSlot(Retraced, 14, Pixel)[2];
        if (Count >= 1)
        {
            ++Accepted;
            if (Count == 3) { ++Full; Mask[Pixel] = {.1f, 1, .1f, 1}; }
            else Mask[Pixel] = {.05f, .1f, 1, 1};
        }
        else
        {
            ++Rejected;
            Mask[Pixel] = {.5f, .02f, .02f, 1};
            Require(std::memcmp(Raw[Pixel].data(), Result[Pixel].data(), sizeof(Result[Pixel])) == 0,
                    "Rejected sample capture pixel differed from the fresh image");
        }
    }
    Require(Accepted == Retraced.ExtendedStatistics()[23] && Full > 100 && Rejected > 0,
            "Sample capture did not exercise both full and rejected seed banks");
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/ReplaySampleFresh.ppm", Raw, Size, Size, 3);
    WriteImage(Directory + "/ReplaySampleFourFresh.ppm", Four, Size, Size, 3);
    WriteImage(Directory + "/ReplaySampleRetraced.ppm", Result, Size, Size, 3);
    WriteImage(Directory + "/ReplaySampleAcceptance.ppm", Mask, Size, Size, 3);
    std::printf("SAMPLE_CAPTURE %ux%u bounces=%u warm_frames=%u accepted=%u full=%u rejected=%u "
                "rejected_raw_exact=1\n", Size, Size, Bounces, WarmFrames, Accepted, Full, Rejected);
    std::printf("PASS subpixel seed and four-fresh path readbacks\n");
}

void ValidateReplayReservoirIntakeGuards(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayCameraPlane(0, 0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    const auto Raw = ExecuteReplaySampleHistory(Compute, Scene, 220003, true, false, 1, {0,0,0}, 1, false, false);
    (void)ExecuteReplaySampleHistory(Compute, Scene, 220001, true, false, 1, {0,0,0}, 1, true, false);
    const auto Guides = Compute.PrimaryReceivers();
    std::vector<uint32_t> Pixels;
    for (uint32_t Pixel = 0; Pixel < Guides.size() && Pixels.size() < 11; ++Pixel)
        if (Guides[Pixel].MotionPositionToken[3] == 1 && ReplaySampleSlot(Compute, 3, Pixel)[3] > 1.f)
            Pixels.push_back(Pixel);
    Require(Pixels.size() == 11, "Reservoir-intake corruption checks need eleven lit diffuse pixels");
    const auto CorruptSlotZero = [&]()
    {
        for (uint32_t Index = 0; Index < Pixels.size(); ++Index)
        {
            auto ReservoirSlot = ReplaySampleSlot(Compute, 0, Pixels[Index]);
            if (Index == 0) ReservoirSlot[0] = std::numeric_limits<float>::quiet_NaN();
            if (Index == 1) ReservoirSlot[0] = -1.f;
            if (Index == 2) ReservoirSlot[1] = 65536.f;
            if (Index == 3) ReservoirSlot[1] = .5f;
            if (Index == 4) ReservoirSlot[2] = std::numeric_limits<float>::quiet_NaN();
            if (Index == 5) ReservoirSlot[2] = std::numeric_limits<float>::infinity();
            if (Index == 6) ReservoirSlot[2] = 0.f;
            if (Index == 7) ReservoirSlot[3] = std::numeric_limits<float>::quiet_NaN();
            if (Index == 8) ReservoirSlot[3] = 1.5f;
            if (Index == 9) ReservoirSlot[3] = 33554432.f;
            if (Index == 10) { ReservoirSlot[2] = 3.4e38f; ReservoirSlot[3] = 8.f; }
            SetReplaySampleSlot(Compute, 0, Pixels[Index], ReservoirSlot);
        }
    };
    CorruptSlotZero();
    const auto TemporalChecked = ExecuteReplaySampleHistory(Compute, Scene, 220003, false, false, 1, {0,0,0}, 1, true, false);
    const auto TemporalCounts = Compute.ExtendedStatistics();
    for (uint32_t Pixel : Pixels)
        Require(ReplaySampleSlot(Compute, 2, Pixel)[3] == 1.f && Raw[Pixel] == TemporalChecked[Pixel],
                "Corrupt temporal reservoir was not rejected to the fresh 1-spp sample");
    (void)ExecuteReplaySampleHistory(Compute, Scene, 220001, true, false, 1, {0,0,0}, 1,
                                     true, true, false, false, false, true, false, true);
    CorruptSlotZero();
    const auto DiskChecked = ExecuteReplaySampleHistory(Compute, Scene, 220003, false, false, 1, {0,0,0}, 1,
                                                        true, true, false, false, false, true, false, true);
    for (uint32_t Pixel = 0; Pixel < DiskChecked.size(); ++Pixel)
    {
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Require(std::isfinite(DiskChecked[Pixel][Channel]) && DiskChecked[Pixel][Channel] >= 0.f,
                    "Corrupt reservoir history leaked non-finite radiance into spatial-disk output");
        const auto Persisted = ReplaySampleSlot(Compute, 0, Pixel);
        if (Guides[Pixel].PositionHit[3] != 0 && Guides[Pixel].MotionPositionToken[3] > 0)
            Require(std::isfinite(Persisted[2]) && Persisted[2] > 0.f && std::isfinite(Persisted[3]) &&
                        Persisted[3] >= 1.f && std::floor(Persisted[3]) == Persisted[3],
                    "Persisted reservoir violated finite weight or integer count invariants");
    }
    Require(TemporalCounts[21] >= 11 && TemporalCounts[20] > 500,
            "Reservoir intake guards did not reject corrupt histories while retaining valid pixels");
    const uint32_t Sequence = Compute.HistorySequence;
    bool SpatialDiskRejected = false;
    try
    {
        (void)ExecuteReplaySampleHistory(Compute, Scene, 220005, false, false, 1, {0,0,0}, 1,
                                         true, false, false, false, false, true, false, true);
    }
    catch (const std::exception&)
    {
        SpatialDiskRejected = true;
    }
    Require(SpatialDiskRejected && Compute.HistorySequence == Sequence,
            "Spatial-disk request without spatial reuse did not fail cleanly");
    std::printf("RESERVOIR_INTAKE_GUARDS corrupt_reservoirs=11 overflow_guarded=1 temporal_rejected=%u "
                "temporal_retained=%u disk_finite=1 host_guard=1\n",
                TemporalCounts[21], TemporalCounts[20]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  DRIVING CORRIDOR SPATIAL-DISK PROVING GROUNDS
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ReplayDrivingCorridorScene(float LateralShift, float PreviousShift, uint32_t Size = 64)
{
    LightComputeArchive Scene;
    Scene.Width = Scene.Height = Size;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    std::vector<ReplayPatch> Patches;
    const auto Quad = [&](SpatialVector A, SpatialVector B, SpatialVector C, SpatialVector D,
                          std::array<float,3> Normal, std::array<float,3> Albedo,
                          std::array<float,3> RadiantExitance, float Token)
    {
        Patches.push_back({{A,B,C}, Normal, Albedo, RadiantExitance, Token});
        Patches.push_back({{A,C,D}, Normal, Albedo, RadiantExitance, Token});
    };
    // Grazing asphalt road (z = 0 m), raised concrete kerb deck (z = 0.24 m) and vertical kerb step (x = 1.15 m).
    Quad({-5.0,-2.0,0.0}, {5.0,-2.0,0.0}, {5.0,12.0,0.0}, {-5.0,12.0,0.0},
         {0,0,1}, {.30f,.31f,.34f}, {0,0,0}, 1);
    Quad({1.15,0.2,0.24}, {2.30,0.2,0.24}, {2.30,10.5,0.24}, {1.15,10.5,0.24},
         {0,0,1}, {.66f,.64f,.58f}, {0,0,0}, 2);
    Quad({1.15,0.2,0.0}, {1.15,10.5,0.0}, {1.15,10.5,0.24}, {1.15,0.2,0.24},
         {-1,0,0}, {.54f,.52f,.48f}, {0,0,0}, 3);
    // Warm terracotta left wall (x = -2.10 m) and cool cyan right barrier wall (x = 2.30 m) for multi-bounce bleed.
    Quad({-2.10,0.2,0.0}, {-2.10,10.5,0.0}, {-2.10,10.5,2.50}, {-2.10,0.2,2.50},
         {1,0,0}, {.82f,.28f,.14f}, {0,0,0}, 4);
    Quad({2.30,10.5,0.0}, {2.30,0.2,0.0}, {2.30,0.2,2.10}, {2.30,10.5,2.10},
         {-1,0,0}, {.18f,.46f,.78f}, {0,0,0}, 5);
    // Overhead luminaire strip (z = 2.95 m) and wide underpass gantry occluder (z = 1.70 m) creating indirect GI penumbrae.
    Quad({-1.10,2.0,2.95}, {1.10,2.0,2.95}, {1.10,6.4,2.95}, {-1.10,6.4,2.95},
         {0,0,-1}, {0,0,0}, {7.8f,7.3f,6.4f}, 6);
    Quad({-0.85,2.2,1.70}, {0.85,2.2,1.70}, {0.85,5.2,1.70}, {-0.85,5.2,1.70},
         {0,0,-1}, {.24f,.25f,.28f}, {0,0,0}, 7);
    std::vector<std::array<SpatialVector,3>> Geometry;
    for (const auto& Patch : Patches) Geometry.push_back(Patch.Corners);
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (size_t Index = 0; Index < Scene.Visibility.Corners.size(); ++Index)
    {
        const auto& Triangle = Scene.Visibility.Corners[Index];
        const auto& Patch = Patches[Scene.Visibility.OriginalFacets[Index]];
        LightComputeFacet Facet;
        Facet.A = Triangle.A; Facet.B = Triangle.B; Facet.C = Triangle.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {Patch.Normal[0],Patch.Normal[1],Patch.Normal[2],0};
        Facet.AlbedoRoughness = {Patch.Albedo[0],Patch.Albedo[1],Patch.Albedo[2],1};
        Facet.OpticalToken = {0,1.5f,.5f,Patch.Token};
        Facet.Emissive = {Patch.Radiance[0],Patch.Radiance[1],Patch.Radiance[2],0};
        Scene.Surfaces.push_back(Facet);
    }
    // Exact 7-24-25 orthonormal driver camera basis pitched down toward the grazing road.
    Scene.Camera[0] = {LateralShift, -1.6f, 1.25f, .02f};
    Scene.Camera[1] = {0.f, .96f, -.28f, 25.f};
    Scene.Camera[2] = {1.f, 0.f, 0.f, .65f};
    Scene.Camera[3] = {0.f, .28f, .96f, .65f};
    Scene.Camera[4] = {PreviousShift, -1.6f, 1.25f, .02f};
    Scene.Camera[5] = Scene.Camera[1];
    Scene.Camera[6] = Scene.Camera[2];
    Scene.Camera[7] = Scene.Camera[3];
    Scene.Camera[8] = {0,0,1000,0};
    Scene.Camera[9] = Scene.Camera[8];
    Scene.Camera[10] = {0,0,0,1};
    Scene.Camera[11] = {1,0,1,0};
    return Scene;
}

std::vector<std::array<double,3>> TraceDrivingCorridorReference(const LightComputeArchive& Scene,
                                                                const std::vector<LightComputeReceiver>& Guides,
                                                                uint32_t Bounces, uint32_t PathsPerPixel,
                                                                uint64_t BaseSeed)
{
    std::vector<std::array<double,3>> Reference(Guides.size(), {0.0, 0.0, 0.0});
    for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        const auto& R = Guides[Pixel];
        if (R.PositionHit[3] == 0) continue;
        if (R.MotionPositionToken[3] == 6)
        {
            Reference[Pixel] = {R.ResidualSpecular[0], R.ResidualSpecular[1], R.ResidualSpecular[2]};
            continue;
        }
        std::mt19937_64 Random(BaseSeed + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
        std::array<double,3> Sum{0.0, 0.0, 0.0};
        for (uint32_t Sample = 0; Sample < PathsPerPixel; ++Sample)
        {
            const auto Radiance = ReplayReferencePath(Scene, R, Bounces, Random, false, false);
            for (uint32_t Channel = 0; Channel < 3; ++Channel) Sum[Channel] += Radiance[Channel];
        }
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Reference[Pixel][Channel] = R.ResidualSpecular[Channel] + Sum[Channel] / double(PathsPerPixel);
    }
    return Reference;
}

struct CorridorModeMetrics
{
    double RmseAll = 0.0;
    double RmseRoad = 0.0;
    double RmseBoundary = 0.0;
    double MeanLuminance = 0.0;
    uint32_t BounceQueries = 0;
    uint32_t ShadowQueries = 0;
    uint32_t SpatialLinks = 0;
    uint32_t SpatialCulled = 0;
    uint32_t TemporalAccepted = 0;
    uint32_t SeedAccepted = 0;
};

CorridorModeMetrics EvaluateCorridorMetrics(const std::vector<std::array<float,4>>& Image,
                                            const std::vector<std::array<double,3>>& Reference,
                                            const std::vector<LightComputeReceiver>& Guides,
                                            uint32_t Width, uint32_t Height,
                                            const std::array<uint32_t,24>& Counts)
{
    CorridorModeMetrics Metrics{};
    double AllSquared = 0.0, RoadSquared = 0.0, BoundarySquared = 0.0, LuminanceSum = 0.0;
    uint32_t AllCount = 0, RoadCount = 0, BoundaryCount = 0;
    for (uint32_t Y = 0; Y < Height; ++Y)
        for (uint32_t X = 0; X < Width; ++X)
        {
            const size_t Pixel = size_t(Y) * Width + X;
            const auto& R = Guides[Pixel];
            if (R.PositionHit[3] == 0 || R.MotionPositionToken[3] == 6) continue;
            bool NearDiscontinuity = false;
            for (int Dy = -2; Dy <= 2 && !NearDiscontinuity; ++Dy)
                for (int Dx = -2; Dx <= 2; ++Dx)
                {
                    const int Nx = int(X) + Dx, Ny = int(Y) + Dy;
                    if (Nx < 0 || Ny < 0 || Nx >= int(Width) || Ny >= int(Height)) continue;
                    const auto& Neighbor = Guides[size_t(Ny) * Width + size_t(Nx)];
                    if (Neighbor.PositionHit[3] == 0 || Neighbor.MotionPositionToken[3] != R.MotionPositionToken[3])
                    {
                        NearDiscontinuity = true;
                        break;
                    }
                }
            double PixelSquared = 0.0;
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const double Diff = double(Image[Pixel][Channel]) - Reference[Pixel][Channel];
                PixelSquared += Diff * Diff;
            }
            PixelSquared /= 3.0;
            AllSquared += PixelSquared;
            ++AllCount;
            LuminanceSum += 0.2126 * Image[Pixel][0] + 0.7152 * Image[Pixel][1] + 0.0722 * Image[Pixel][2];
            if (R.MotionPositionToken[3] == 1 && !NearDiscontinuity)
            {
                RoadSquared += PixelSquared;
                ++RoadCount;
            }
            if (NearDiscontinuity)
            {
                BoundarySquared += PixelSquared;
                ++BoundaryCount;
            }
        }
    Metrics.RmseAll = std::sqrt(AllSquared / std::max(1u, AllCount));
    Metrics.RmseRoad = std::sqrt(RoadSquared / std::max(1u, RoadCount));
    Metrics.RmseBoundary = std::sqrt(BoundarySquared / std::max(1u, BoundaryCount));
    Metrics.MeanLuminance = LuminanceSum / std::max(1u, AllCount);
    Metrics.ShadowQueries = Counts[2];
    Metrics.BounceQueries = Counts[8];
    Metrics.SpatialCulled = Counts[9];
    Metrics.TemporalAccepted = Counts[20];
    Metrics.SpatialLinks = Counts[22];
    Metrics.SeedAccepted = Counts[23];
    return Metrics;
}

std::vector<std::array<double,3>> AccumulateConvergedCorridorReference(LightComputeSubmission& Compute,
                                                                       const LightComputeArchive& Scene,
                                                                       uint32_t Bounces,
                                                                       const std::array<float,3>& Environment,
                                                                       uint32_t FrameSamples,
                                                                       bool Rough = false,
                                                                       bool ImportanceRough = false)
{
    std::vector<std::array<double,3>> Sum(Scene.Receivers.size(), {0.0, 0.0, 0.0});
    for (uint32_t Index = 0; Index < FrameSamples; ++Index)
    {
        const uint32_t Seed = 900001u + Index * 104729u;
        const auto SampleImg = Compute.ExecuteReplay(Scene, Bounces, Seed, false, false, true, Environment, .01f,
                                                     true, false, Rough, true, false, false, 64, true, false,
                                                     ImportanceRough, false, false, false, false, false, 1,
                                                     false, false, false, false);
        for (size_t Pixel = 0; Pixel < SampleImg.size(); ++Pixel)
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Sum[Pixel][Channel] += double(SampleImg[Pixel][Channel]);
    }
    for (auto& Pixel : Sum)
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Pixel[Channel] /= double(FrameSamples);
    return Sum;
}

LightComputeArchive MakeWetDrivingCorridorScene(float LateralShift, float PreviousShift, uint32_t Size)
{
    auto Scene = ReplayDrivingCorridorScene(LateralShift, PreviousShift, Size);
    for (auto& Facet : Scene.Surfaces)
    {
        const float Token = Facet.OpticalToken[3];
        if (Token == 1.f) { Facet.AlbedoRoughness[3] = .28f; Facet.OpticalToken[0] = .05f; }      // Wet glossy asphalt
        else if (Token == 2.f || Token == 3.f) { Facet.AlbedoRoughness[3] = .85f; }               // Dry rough concrete kerb
        else if (Token == 4.f || Token == 5.f) { Facet.AlbedoRoughness[3] = .35f; }               // Painted corridor walls
        else { Facet.AlbedoRoughness[3] = .75f; }
    }
    return Scene;
}

void ValidateReplaySpatialDiskAll(const char* Driver, const std::string& Shaders)
{
    ValidateReplayReservoirIntakeGuards(Driver, Shaders);
    std::fflush(stdout);
    constexpr uint32_t Size = 32, Bounces = 3, WarmSteps = 3, Batches = 8;
    const auto Previous = ReplayDrivingCorridorScene(0.f, 0.f, Size);
    const auto Current = ReplayDrivingCorridorScene(.012f, 0.f, Size);
    const auto WetPrevious = MakeWetDrivingCorridorScene(0.f, 0.f, Size);
    const auto WetCurrent = MakeWetDrivingCorridorScene(.012f, 0.f, Size);
    const std::array<float,3> Environment{.025f, .025f, .025f};
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    const auto CleanReference = AccumulateConvergedCorridorReference(Compute, Current, Bounces, Environment, 96);
    const auto Guides = Compute.PrimaryReceivers();
    const auto CpuReference = TraceDrivingCorridorReference(Current, Guides, Bounces, 256, 0x71e550da11ull);
    double CpuLuminance = 0.0, CleanLuminance = 0.0;
    uint32_t ValidCount = 0;
    for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
        if (Guides[Pixel].PositionHit[3] != 0 && Guides[Pixel].MotionPositionToken[3] != 6)
        {
            CpuLuminance += 0.2126 * CpuReference[Pixel][0] + 0.7152 * CpuReference[Pixel][1] + 0.0722 * CpuReference[Pixel][2];
            CleanLuminance += 0.2126 * CleanReference[Pixel][0] + 0.7152 * CleanReference[Pixel][1] + 0.0722 * CleanReference[Pixel][2];
            ++ValidCount;
        }
    CpuLuminance /= std::max(1u, ValidCount);
    CleanLuminance /= std::max(1u, ValidCount);
    Require(std::abs(CleanLuminance - CpuLuminance) < 0.008,
            "Converged GPU corridor baseline diverged from independent CPU brute-force reference");
    DirectGpuMoments FreshMean, CrossMean, DiskMean, UnifiedMean;
    double FreshRmseSum = 0.0, CrossRmseSum = 0.0, DiskRmseSum = 0.0, UnifiedRmseSum = 0.0;
    double CrossEdgeSum = 0.0, DiskEdgeSum = 0.0, UnifiedEdgeSum = 0.0;
    uint64_t CrossRays = 0, DiskRays = 0, UnifiedRays = 0, DiskCulledTotal = 0;
    const auto RunMode = [&](uint32_t Base, uint32_t EvalSeed, bool History, bool SpatialDisk)
    {
        for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            (void)ExecuteReplaySampleHistory(Compute, Previous, Base + Step * 733u, Step == 0, History,
                                             Bounces, Environment, 1, true, true, false, false, false,
                                             true, true, SpatialDisk, true);
        return ExecuteReplaySampleHistory(Compute, Current, EvalSeed, false, History, Bounces, Environment,
                                          1, true, true, false, false, false, true, true, SpatialDisk, true);
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Base = 610000u + Batch * 4099u, EvalSeed = Base + 9973u;
        const auto FreshImg = ExecuteReplaySampleHistory(Compute, Current, EvalSeed, true, false, Bounces, Environment,
                                                         1, false, false, false, false, false, true, true, false, true);
        const auto M0 = EvaluateCorridorMetrics(FreshImg, CleanReference, Guides, Size, Size, Compute.ExtendedStatistics());
        const auto CrossImg = RunMode(Base, EvalSeed, false, false);
        const auto M1 = EvaluateCorridorMetrics(CrossImg, CleanReference, Guides, Size, Size, Compute.ExtendedStatistics());
        const auto DiskImg = RunMode(Base, EvalSeed, false, true);
        const auto M2 = EvaluateCorridorMetrics(DiskImg, CleanReference, Guides, Size, Size, Compute.ExtendedStatistics());
        const auto UnifiedImg = RunMode(Base, EvalSeed, true, true);
        const auto M3 = EvaluateCorridorMetrics(UnifiedImg, CleanReference, Guides, Size, Size, Compute.ExtendedStatistics());
        FreshMean.Accumulate(M0.MeanLuminance); CrossMean.Accumulate(M1.MeanLuminance);
        DiskMean.Accumulate(M2.MeanLuminance); UnifiedMean.Accumulate(M3.MeanLuminance);
        FreshRmseSum += M0.RmseAll; CrossRmseSum += M1.RmseAll;
        DiskRmseSum += M2.RmseAll; UnifiedRmseSum += M3.RmseAll;
        CrossEdgeSum += M1.RmseBoundary; DiskEdgeSum += M2.RmseBoundary; UnifiedEdgeSum += M3.RmseBoundary;
        CrossRays += M1.BounceQueries + M1.ShadowQueries;
        DiskRays += M2.BounceQueries + M2.ShadowQueries;
        UnifiedRays += M3.BounceQueries + M3.ShadowQueries;
        DiskCulledTotal += M2.SpatialCulled;
    }
    const double Correction = std::sqrt(double(Batches) / (Batches - 1));
    const double DiskSe = DiskMean.StandardError() * Correction, UnifiedSe = UnifiedMean.StandardError() * Correction;
    std::printf("SPATIAL_DISK_CORRIDOR %ux%u batches=%u cpu_lum=%.6f clean_lum=%.6f "
                "fresh_lum=%.6f cross_lum=%.6f disk_lum=%.6f unified_lum=%.6f "
                "fresh_rmse=%.6f cross_rmse=%.6f disk_rmse=%.6f unified_rmse=%.6f "
                "cross_edge_rmse=%.6f disk_edge_rmse=%.6f unified_edge_rmse=%.6f "
                "cross_rays_avg=%llu disk_rays_avg=%llu unified_rays_avg=%llu disk_culled_avg=%llu\n",
                Size, Size, Batches, CpuLuminance, CleanLuminance,
                FreshMean.Mean(), CrossMean.Mean(), DiskMean.Mean(), UnifiedMean.Mean(),
                FreshRmseSum / Batches, CrossRmseSum / Batches, DiskRmseSum / Batches, UnifiedRmseSum / Batches,
                CrossEdgeSum / Batches, DiskEdgeSum / Batches, UnifiedEdgeSum / Batches,
                (unsigned long long)(CrossRays / Batches), (unsigned long long)(DiskRays / Batches),
                (unsigned long long)(UnifiedRays / Batches), (unsigned long long)(DiskCulledTotal / Batches));
    std::fflush(stdout);
    // Wet glossy asphalt + rough concrete kerb GGX benchmark on the same prepared Vulkan submission.
    const auto WetClean = AccumulateConvergedCorridorReference(Compute, WetCurrent, Bounces, Environment, 96, true, true);
    const auto WetGuides = Compute.PrimaryReceivers();
    double WetFreshRmse = 0.0, WetCrossRmse = 0.0, WetDiskRmse = 0.0;
    uint64_t WetCrossRays = 0, WetDiskRays = 0, WetCulled = 0;
    const auto RunWet = [&](uint32_t Base, uint32_t EvalSeed, bool SpatialDisk)
    {
        for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            (void)Compute.ExecuteReplay(WetPrevious, Bounces, Base + Step * 733u, true, true, Step == 0,
                                        Environment, .01f, true, false, true, true, false, false, 64, true,
                                        false, true, false, false, false, false, false, 1, false, false,
                                        false, SpatialDisk);
        return Compute.ExecuteReplay(WetCurrent, Bounces, EvalSeed, true, true, false, Environment, .01f,
                                     true, false, true, true, false, false, 64, true, false, true, false,
                                     false, false, false, false, 1, false, false, false, SpatialDisk);
    };
    for (uint32_t Batch = 0; Batch < 6; ++Batch)
    {
        const uint32_t Base = 810000u + Batch * 4099u, EvalSeed = Base + 9973u;
        const auto WF = Compute.ExecuteReplay(WetCurrent, Bounces, EvalSeed, false, false, true, Environment, .01f,
                                              true, false, true, true, false, false, 64, true, false, true, false,
                                              false, false, false, false, 1, false, false, false, false);
        const auto WM0 = EvaluateCorridorMetrics(WF, WetClean, WetGuides, Size, Size, Compute.ExtendedStatistics());
        const auto WC = RunWet(Base, EvalSeed, false);
        const auto WM1 = EvaluateCorridorMetrics(WC, WetClean, WetGuides, Size, Size, Compute.ExtendedStatistics());
        const auto WD = RunWet(Base, EvalSeed, true);
        const auto WM2 = EvaluateCorridorMetrics(WD, WetClean, WetGuides, Size, Size, Compute.ExtendedStatistics());
        WetFreshRmse += WM0.RmseAll; WetCrossRmse += WM1.RmseAll; WetDiskRmse += WM2.RmseAll;
        WetCrossRays += WM1.BounceQueries + WM1.ShadowQueries;
        WetDiskRays += WM2.BounceQueries + WM2.ShadowQueries;
        WetCulled += WM2.SpatialCulled;
    }
    std::printf("SPATIAL_DISK_WET_GGX %ux%u batches=6 fresh_rmse=%.6f cross_rmse=%.6f disk_rmse=%.6f "
                "cross_rays_avg=%llu disk_rays_avg=%llu disk_culled_avg=%llu\n",
                Size, Size, WetFreshRmse / 6.0, WetCrossRmse / 6.0, WetDiskRmse / 6.0,
                (unsigned long long)(WetCrossRays / 6), (unsigned long long)(WetDiskRays / 6),
                (unsigned long long)(WetCulled / 6));
    std::fflush(stdout);
    // Also compare standalone SampleHistory vs Unified (SampleHistory + Temporal + SpatialDisk) on Current.
    double StandaloneHistoryRmseSum = 0.0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Base = 610000u + Batch * 4099u, EvalSeed = Base + 9973u;
        for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            (void)ExecuteReplaySampleHistory(Compute, Previous, Base + Step * 733u, Step == 0, true,
                                             Bounces, Environment, 1, false, false, false, false, false,
                                             true, true, false, true);
        const auto SHImg = ExecuteReplaySampleHistory(Compute, Current, EvalSeed, false, true, Bounces, Environment,
                                                      1, false, false, false, false, false, true, true, false, true);
        StandaloneHistoryRmseSum += EvaluateCorridorMetrics(SHImg, CleanReference, Guides, Size, Size,
                                                            Compute.ExtendedStatistics()).RmseAll;
    }
    std::printf("SPATIAL_DISK_UNIFIED_GAIN %ux%u standalone_seed_rmse=%.6f unified_seed_disk_rmse=%.6f\n",
                Size, Size, StandaloneHistoryRmseSum / Batches, UnifiedRmseSum / Batches);
    std::fflush(stdout);
    Require(std::abs(DiskMean.Mean() - CpuLuminance) < 6.0 * DiskSe + 0.008 &&
                std::abs(UnifiedMean.Mean() - CpuLuminance) < 6.0 * UnifiedSe + 0.008,
            "Spatial-disk or unified seed+disk corridor mean luminance diverged from CPU Monte Carlo ground truth");
    Require(DiskCulledTotal > 0 && DiskRays < CrossRays &&
                UnifiedRmseSum < StandaloneHistoryRmseSum &&
                UnifiedRmseSum < FreshRmseSum * 0.55,
            "Unified seed-bank + spatial-disk mode failed ray reduction or RMSE improvement over standalone seed history");
    Require(WetCulled > 0 && WetDiskRays < WetCrossRays && WetDiskRmse < WetCrossRmse &&
                WetDiskRmse < WetFreshRmse * 0.65,
            "Wet-corridor rough GGX spatial disk failed ray reduction or RMSE reduction over legacy cross");
    std::printf("PASS driving-corridor Vogel disk, tangent-plane/roughness culling and unified seed-bank + reservoir reuse\n");
}

void CaptureReplaySpatialDiskComparison(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64, Bounces = 4, WarmSteps = 4;
    constexpr float StepShift = .015f;
    std::vector<LightComputeArchive> CorridorSteps;
    for (uint32_t Step = 0; Step <= WarmSteps; ++Step)
        CorridorSteps.push_back(ReplayDrivingCorridorScene(float(Step) * StepShift,
                                                           float(Step > 0 ? Step - 1 : 0) * StepShift, Size));
    const auto& Current = CorridorSteps.back();
    const std::array<float,3> Environment{.025f, .025f, .025f};
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(CorridorSteps[0], Shaders, false, false, false, false, true);
    constexpr uint32_t CaptureSeed = 780017u;
    const auto FreshImg = ExecuteReplaySampleHistory(Compute, Current, CaptureSeed, true, false, Bounces, Environment,
                                                     1, false, false, false, false, false, true, true, false, true);
    const auto Guides = Compute.PrimaryReceivers();
    const auto FreshStats = Compute.ExtendedStatistics();
    const auto RunMode = [&](bool History, bool SpatialDisk)
    {
        for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            (void)ExecuteReplaySampleHistory(Compute, CorridorSteps[Step], 710001u + Step * 7919u, Step == 0, History,
                                             Bounces, Environment, 1, true, true, false, false, false,
                                             true, true, SpatialDisk, true);
        return ExecuteReplaySampleHistory(Compute, Current, CaptureSeed, false, History, Bounces, Environment,
                                          1, true, true, false, false, false, true, true, SpatialDisk, true);
    };
    const auto CrossImg = RunMode(false, false);
    const auto CrossStats = Compute.ExtendedStatistics();
    const auto DiskImg = RunMode(false, true);
    const auto DiskStats = Compute.ExtendedStatistics();
    const auto UnifiedImg = RunMode(true, true);
    const auto UnifiedStats = Compute.ExtendedStatistics();
    for (uint32_t Step = 0; Step < WarmSteps; ++Step)
        (void)ExecuteReplaySampleHistory(Compute, CorridorSteps[Step], 710001u + Step * 7919u, Step == 0, true,
                                         Bounces, Environment, 1, false, false, false, false, false,
                                         true, true, false, true);
    const auto SeedOnlyImg = ExecuteReplaySampleHistory(Compute, Current, CaptureSeed, false, true, Bounces, Environment,
                                                        1, false, false, false, false, false, true, true, false, true);
    const auto SeedOnlyStats = Compute.ExtendedStatistics();
    const auto Reference = AccumulateConvergedCorridorReference(Compute, Current, Bounces, Environment, 192);
    std::vector<std::array<float,4>> RefImg(Guides.size()), GuideMap(Guides.size());
    for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        RefImg[Pixel] = {float(Reference[Pixel][0]), float(Reference[Pixel][1]), float(Reference[Pixel][2]), 1.f};
        const float Token = Guides[Pixel].PositionHit[3] == 0 ? 0.f : Guides[Pixel].MotionPositionToken[3];
        if (Token == 0.f) GuideMap[Pixel] = {.03f, .05f, .10f, 1.f};
        else if (Token == 1.f) GuideMap[Pixel] = {.22f, .24f, .28f, 1.f};
        else if (Token == 2.f) GuideMap[Pixel] = {.68f, .64f, .52f, 1.f};
        else if (Token == 3.f) GuideMap[Pixel] = {.95f, .75f, .15f, 1.f};
        else if (Token == 4.f) GuideMap[Pixel] = {.85f, .25f, .12f, 1.f};
        else if (Token == 5.f) GuideMap[Pixel] = {.15f, .50f, .85f, 1.f};
        else if (Token == 6.f) GuideMap[Pixel] = {1.f, .96f, .85f, 1.f};
        else GuideMap[Pixel] = {.45f, .22f, .68f, 1.f};
    }
    const auto M0 = EvaluateCorridorMetrics(FreshImg, Reference, Guides, Size, Size, FreshStats);
    const auto MS = EvaluateCorridorMetrics(SeedOnlyImg, Reference, Guides, Size, Size, SeedOnlyStats);
    const auto M1 = EvaluateCorridorMetrics(CrossImg, Reference, Guides, Size, Size, CrossStats);
    const auto M2 = EvaluateCorridorMetrics(DiskImg, Reference, Guides, Size, Size, DiskStats);
    const auto M3 = EvaluateCorridorMetrics(UnifiedImg, Reference, Guides, Size, Size, UnifiedStats);
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/CorridorReference512.ppm", RefImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorFresh1Spp.ppm", FreshImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorStandaloneSeed.ppm", SeedOnlyImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorLegacyCross.ppm", CrossImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorSpatialDisk.ppm", DiskImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorUnifiedSeedDisk.ppm", UnifiedImg, Size, Size, 1.8f);
    WriteImage(Directory + "/CorridorSurfaceDomains.ppm", GuideMap, Size, Size, 1.0f);
    const auto WriteErrorMap = [&](const std::string& Name, const std::vector<std::array<float,4>>& Candidate)
    {
        std::vector<std::array<float,4>> Heatmap(Guides.size(), {0.f, 0.f, 0.f, 1.f});
        for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
        {
            if (Guides[Pixel].PositionHit[3] == 0 || Guides[Pixel].MotionPositionToken[3] == 6) continue;
            double Err = 0.0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                const double Diff = double(Candidate[Pixel][C]) - Reference[Pixel][C];
                Err += Diff * Diff;
            }
            const float T = std::clamp(float(std::sqrt(Err / 3.0) * 4.0), 0.f, 1.f);
            // Perceptual thermal ramp: deep navy -> cyan -> amber -> white-hot.
            const float R = std::clamp(T * 2.2f - .35f, 0.f, 1.f);
            const float G = std::clamp(T < .5f ? T * 1.6f : 1.3f - (T - .5f) * .8f, 0.f, 1.f);
            const float B = std::clamp(T < .4f ? .2f + T * 2.0f : 1.0f - (T - .4f) * 1.6f, 0.f, 1.f);
            Heatmap[Pixel] = {R * R, G * G, B * B, 1.f};
        }
        WriteImage(Directory + "/" + Name + ".ppm", Heatmap, Size, Size, 1.0f);
    };
    WriteErrorMap("CorridorErrorFresh", FreshImg);
    WriteErrorMap("CorridorErrorSeed", SeedOnlyImg);
    WriteErrorMap("CorridorErrorCross", CrossImg);
    WriteErrorMap("CorridorErrorDisk", DiskImg);
    WriteErrorMap("CorridorErrorUnified", UnifiedImg);
    std::printf("SPATIAL_DISK_CAPTURE %ux%u bounces=%u ref_passes=192 "
                "fresh_rmse=%.6f seed_only_rmse=%.6f cross_rmse=%.6f disk_rmse=%.6f unified_rmse=%.6f "
                "fresh_road=%.6f seed_only_road=%.6f cross_road=%.6f disk_road=%.6f unified_road=%.6f "
                "fresh_edge=%.6f seed_only_edge=%.6f cross_edge=%.6f disk_edge=%.6f unified_edge=%.6f "
                "fresh_rays=%u seed_only_rays=%u cross_rays=%u disk_rays=%u unified_rays=%u "
                "cross_links=%u disk_links=%u unified_links=%u disk_culled=%u unified_seeds=%u\n",
                Size, Size, Bounces,
                M0.RmseAll, MS.RmseAll, M1.RmseAll, M2.RmseAll, M3.RmseAll,
                M0.RmseRoad, MS.RmseRoad, M1.RmseRoad, M2.RmseRoad, M3.RmseRoad,
                M0.RmseBoundary, MS.RmseBoundary, M1.RmseBoundary, M2.RmseBoundary, M3.RmseBoundary,
                M0.BounceQueries + M0.ShadowQueries, MS.BounceQueries + MS.ShadowQueries,
                M1.BounceQueries + M1.ShadowQueries, M2.BounceQueries + M2.ShadowQueries,
                M3.BounceQueries + M3.ShadowQueries,
                M1.SpatialLinks, M2.SpatialLinks, M3.SpatialLinks, M2.SpatialCulled, M3.SeedAccepted);
    std::printf("PASS driving-corridor spatial-disk and unified seed+reservoir comparison readbacks\n");
}

void ValidateReplaySampleHistory(const char* Driver, const std::string& Shaders)
{
    ValidateReplaySampleSubpixel(Driver, Shaders);
    ValidateReplaySampleCombined(Driver, Shaders);
    ValidateReplaySampleFacetConvergence(Driver, Shaders);
    ValidateReplaySampleMultibounce(Driver, Shaders);
    ValidateReplaySampleGuards(Driver, Shaders);
    ValidateReplayReservoirIntakeGuards(Driver, Shaders);
    std::printf("PASS bounded subpixel seed retracing, area and multibounce references, and history guards\n");
}
} // namespace
