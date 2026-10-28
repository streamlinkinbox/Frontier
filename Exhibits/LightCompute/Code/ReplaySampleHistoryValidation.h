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
    Require(Slot <= 14 && Pixel < N, "Sample-history slot is outside the receiver domain");
    const size_t Index = Compute.CompactReplayStorage ? size_t(PathReplayCompactSlot(Slot, uint32_t(N), Pixel)) :
        (25 + size_t(Slot)) * N + 13889 + SolarSourceCount + Pixel;
    Require(Index < Compute.ReplayOutputDescriptorBytes / 16, "Sample-history slot exceeds output storage");
    std::array<float,4> Value{};
    std::memcpy(Value.data(), static_cast<const char*>(Compute.Buffers[6].Address) + Index * 16, 16);
    return Value;
}
void SetReplaySampleSlot(LightComputeSubmission& Compute, uint32_t Slot, uint32_t Pixel,
                         const std::array<float,4>& Value)
{
    const size_t N = Compute.ReceiverCount;
    Require(Slot <= 14 && Pixel < N, "Sample-history write is outside the receiver domain");
    const size_t Index = Compute.CompactReplayStorage ? size_t(PathReplayCompactSlot(Slot, uint32_t(N), Pixel)) :
        (25 + size_t(Slot)) * N + 13889 + SolarSourceCount + Pixel;
    Require(Index < Compute.ReplayOutputDescriptorBytes / 16, "Sample-history write exceeds output storage");
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
std::vector<std::array<float,4>> ExecuteReplaySampleHistory(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Reset, bool History, uint32_t Bounces = 1,
    const std::array<float,3>& Environment = {0,0,0}, uint32_t Candidates = 1,
    bool Temporal = false, bool Spatial = false, bool Reconstruct = false, bool Rough = false,
    bool Textured = false, bool Primary = true, bool SampleFacets = false)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, Temporal, Spatial, Reset, Environment, .01f, Primary,
                                 false, Rough, SampleFacets, false, false, 64, false, Textured, false,
                                 false, false, false, false, Reconstruct, Candidates, false, false, History);
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
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, true); });
    ExpectRejected([&] { (void)ExecuteReplaySampleHistory(Retraced, Scene, 1, false, true, 1, {0,0,0}, 1, false, true); });
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

void ValidateReplaySampleHistory(const char* Driver, const std::string& Shaders)
{
    ValidateReplaySampleSubpixel(Driver, Shaders);
    ValidateReplaySampleFacetConvergence(Driver, Shaders);
    ValidateReplaySampleMultibounce(Driver, Shaders);
    ValidateReplaySampleGuards(Driver, Shaders);
    std::printf("PASS bounded subpixel seed retracing, area and multibounce references, and history guards\n");
}
} // namespace
