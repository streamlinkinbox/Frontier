/*==========================================================================================================================================
                                              REPLAYSEEDRESERVOIRVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify seed-bank RIS normalization, spatial replay, fresh-seed retention and conservative resets.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                  DIFFUSE SUBMISSION
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::array<float,4>> ExecuteReplaySeedReservoir(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Reset, bool Spatial = false,
    uint32_t Bounces = 1, bool Facets = false, float Floor = .01f,
    const std::array<float,3>& Environment = {0,0,0})
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, false, Spatial, Reset, Environment, Floor, true,
        false, false, Facets, false, false, 64, false, false, false, false, false, false, false,
        false, 1, false, false, true, true);
}

float ReplaySeedUnit(uint32_t& Bits)
{
    return float(NextReplayTailRandom(Bits) >> 8) * (1.f / 16777216.f);
}

float ReplaySeedTarget(float Red, float Floor)
{
    return Red * .2126f + Red * .7152f + Red * .0722f + Floor;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         INDEPENDENT RECTANGLE / RIS ORACLE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySeedReservoirOracle(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    uint32_t LocalCompared = 0, SpatialCompared = 0, EdgeExcluded = 0, ChangedVisibility = 0;
    double MaximumLocalError = 0, MaximumSpatialError = 0;
    for (float Floor : {.0001f, .01f, 1.f})
    {
        LightComputeSubmission Local(Driver), Spatial(Driver);
        Local.Prepare(Previous, Shaders, false, false, false, false, true);
        Spatial.Prepare(Previous, Shaders, false, false, false, false, true);
        for (uint32_t Index = 0; Index < 3; ++Index)
        {
            const uint32_t Seed = 150001u + 7919u * Index;
            (void)ExecuteReplaySeedReservoir(Local, Previous, Seed, Index == 0, false, 1, false, Floor);
            (void)ExecuteReplaySeedReservoir(Spatial, Previous, Seed, Index == 0, true, 1, false, Floor);
        }
        const auto PriorGuides = Local.PrimaryReceivers();
        constexpr uint32_t Seed = 191111;
        const auto LocalImage = ExecuteReplaySeedReservoir(Local, Current, Seed, false, false, 1, false, Floor);
        const auto SpatialImage = ExecuteReplaySeedReservoir(Spatial, Current, Seed, false, true, 1, false, Floor);
        const auto Guides = Local.PrimaryReceivers();
        Require(Local.ExtendedStatistics()[23] == 970 && Spatial.ExtendedStatistics()[23] == 970 &&
                    Spatial.ExtendedStatistics()[22] > 0, "Seed RIS did not gather banks and spatially reuse reservoirs");
        for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
        {
            const auto First = ReplaySampleSlot(Local, 13, Pixel), Second = ReplaySampleSlot(Local, 14, Pixel);
            const auto Packed = ReplaySampleSlot(Local, 2, Pixel), Selected = ReplaySampleSlot(Local, 3, Pixel);
            const auto NewFirst = ReplaySampleSlot(Local, 11, Pixel);
            const auto NewSecond = ReplaySampleSlot(Local, 12, Pixel);
            Require(ReplaySampleSeed(NewFirst[0], NewFirst[1]) == ReplayFreshSeed(Seed, Pixel),
                    "Seed RIS persisted the selected winner instead of the fresh unselected draw");
            Require(NewFirst == ReplaySampleSlot(Spatial, 11, Pixel) &&
                        NewSecond == ReplaySampleSlot(Spatial, 12, Pixel), "Spatial policy changed the fresh seed bank");
            const uint32_t Count = uint32_t(Second[2]);
            Require(Packed[3] == float(Count + 1), "Seed RIS represented count omitted a retraced candidate");
            Require(Count == 0 || (NewFirst[2] == First[0] && NewFirst[3] == First[1]),
                    "Seed RIS did not shift the gathered bank");
            const std::array<uint32_t,4> Seeds{ReplayFreshSeed(Seed, Pixel),
                Count > 0 ? ReplaySampleSeed(First[0], First[1]) : 0,
                Count > 1 ? ReplaySampleSeed(First[2], First[3]) : 0,
                Count > 2 ? ReplaySampleSeed(Second[0], Second[1]) : 0};
            uint32_t SelectionBits = (Seed * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
            uint32_t Winner = Seeds[0];
            auto Oracle = ReplaySamplePlaneOracle(Guides[Pixel], Winner);
            bool Boundary = Oracle.Boundary;
            float Red = Oracle.Radiance, Target = ReplaySeedTarget(Red, Floor), Sum = Target;
            for (uint32_t Index = 1; Index <= Count; ++Index)
            {
                Oracle = ReplaySamplePlaneOracle(Guides[Pixel], Seeds[Index]);
                Boundary = Boundary || Oracle.Boundary;
                const float CandidateTarget = ReplaySeedTarget(Oracle.Radiance, Floor);
                Sum += CandidateTarget;
                if (ReplaySeedUnit(SelectionBits) * Sum < CandidateTarget)
                {
                    Winner = Seeds[Index]; Red = Oracle.Radiance; Target = CandidateTarget;
                }
                if (Guides[Pixel].MotionPixel[3] != 0)
                {
                    const int32_t X = int32_t(std::floor(Guides[Pixel].MotionPixel[0] + .5f));
                    const int32_t Y = int32_t(std::floor(Guides[Pixel].MotionPixel[1] + .5f));
                    if (X >= 0 && Y >= 0 && X < 32 && Y < 32)
                    {
                        const auto Old = ReplaySamplePlaneOracle(PriorGuides[uint32_t(Y) * 32 + uint32_t(X)], Seeds[Index]);
                        if (!Old.Boundary && !Oracle.Boundary && Old.Radiance != Oracle.Radiance) ++ChangedVisibility;
                    }
                }
            }
            // 📝 Rectangle oracle is diffuse-plane only; blocker response is intentionally a different albedo.
            if (Guides[Pixel].MotionPositionToken[3] != 1) continue;
            if (Boundary) { ++EdgeExcluded; continue; }
            const float Weight = Sum / (float(Count + 1) * Target);
            MaximumLocalError = std::max(MaximumLocalError, std::abs(double(Packed[2]) - Weight));
            MaximumLocalError = std::max(MaximumLocalError, std::abs(double(LocalImage[Pixel][0]) - Red * Weight));
            Require(ReplaySampleSeed(Packed[0], Packed[1]) == Winner && std::abs(Selected[0] - Red) < 1e-6,
                    "Local seed RIS selected an unexpected current-surface path");
            ++LocalCompared;

            // 📝 Rebuild spatial selection at the recipient from donor W/M and independent rectangle intersections.
            uint32_t SpatialBits = (Seed * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
            float SpatialSum = 0, Represented = 0, Chosen = 0, ChosenTarget = Floor;
            bool SpatialBoundary = false;
            const int32_t CentreX = int32_t(Pixel % 32), CentreY = int32_t(Pixel / 32);
            constexpr std::array<std::array<int32_t,2>,5> Offsets{{{0,0},{-1,0},{1,0},{0,-1},{0,1}}};
            for (const auto& Offset : Offsets)
            {
                const int32_t X = CentreX + Offset[0], Y = CentreY + Offset[1];
                if (X < 0 || Y < 0 || X >= 32 || Y >= 32) continue;
                const auto Source = ReplaySampleSlot(Spatial, 2, uint32_t(Y) * 32 + uint32_t(X));
                if (Source[3] <= 0 || Source[2] <= 0) continue;
                Oracle = ReplaySamplePlaneOracle(Guides[Pixel], ReplaySampleSeed(Source[0], Source[1]));
                SpatialBoundary = SpatialBoundary || Oracle.Boundary;
                const float RecipientTarget = ReplaySeedTarget(Oracle.Radiance, Floor);
                const float Contribution = RecipientTarget * Source[2] * Source[3];
                SpatialSum += Contribution; Represented += Source[3];
                if (ReplaySeedUnit(SpatialBits) * SpatialSum < Contribution)
                {
                    Chosen = Oracle.Radiance; ChosenTarget = RecipientTarget;
                }
            }
            if (SpatialBoundary) { ++EdgeExcluded; continue; }
            const double Expected = Chosen * (SpatialSum / (Represented * ChosenTarget));
            MaximumSpatialError = std::max(MaximumSpatialError, std::abs(double(SpatialImage[Pixel][0]) - Expected));
            ++SpatialCompared;
        }
    }
    Require(LocalCompared > 2000 && SpatialCompared > 2000 && ChangedVisibility > 0 &&
                MaximumLocalError < 1e-5 && MaximumSpatialError < 1e-5,
            "Seed RIS normalization or spatial current-surface oracle differs");
    std::printf("SEED_RIS_ORACLE floors=3 local_pixels=%u spatial_pixels=%u edge_excluded=%u "
                "changed_visibility_paths=%u local_max_error=%.9g spatial_max_error=%.9g\n",
                LocalCompared, SpatialCompared, EdgeExcluded, ChangedVisibility, MaximumLocalError, MaximumSpatialError);
}

//------------------------------------------------------------------------------------------------------------------------
//                                         INDEPENDENT AREA / MULTIBOUNCE MEANS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySeedReservoirConvergence(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Batches = 16, CpuSamples = 1024;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    for (uint32_t Bounces : {1u, 4u})
    {
        // 📝 The independent multibounce tracer fixes its environment to 0.025 RGB.
        const float Ambient = Bounces == 4 ? .025f : 0.f;
        const std::array<float,3> Environment{Ambient, Ambient, Ambient};
        LightComputeSubmission Average(Driver), Local(Driver), Spatial(Driver);
        Average.Prepare(Previous, Shaders, false, false, false, false, true);
        Local.Prepare(Previous, Shaders, false, false, false, false, true);
        Spatial.Prepare(Previous, Shaders, false, false, false, false, true);
        std::array<DirectGpuMoments,3> Means;
        std::array<double,3> Squared{};
        std::vector<double> Reference(Current.Receivers.size(), -1);
        double ReferenceSum = 0, ReferenceVariance = 0;
        uint32_t Points = 0;
        std::mt19937_64 Seeds(0xb51d4a79ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Index = 0; Index < 3; ++Index)
            {
                const uint32_t Seed = uint32_t(Seeds());
                (void)ExecuteReplaySampleHistory(Average, Previous, Seed, Index == 0, true, Bounces, Environment);
                (void)ExecuteReplaySeedReservoir(Local, Previous, Seed, Index == 0, false, Bounces, false, .01f, Environment);
                (void)ExecuteReplaySeedReservoir(Spatial, Previous, Seed, Index == 0, true, Bounces, false, .01f, Environment);
            }
            const uint32_t Seed = uint32_t(Seeds());
            const std::array<std::vector<std::array<float,4>>,3> Images{
                ExecuteReplaySampleHistory(Average, Current, Seed, false, true, Bounces, Environment),
                ExecuteReplaySeedReservoir(Local, Current, Seed, false, false, Bounces, false, .01f, Environment),
                ExecuteReplaySeedReservoir(Spatial, Current, Seed, false, true, Bounces, false, .01f, Environment)};
            if (Batch == 0)
            {
                const auto Guides = Local.PrimaryReceivers();
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    if (Guides[Pixel].MotionPositionToken[3] != 1 || ReplaySampleSlot(Local, 14, Pixel)[2] != 3) continue;
                    if (Bounces == 1) Reference[Pixel] = ReplayCameraPlaneArea(Guides[Pixel], 128);
                    else
                    {
                        // 📝 Independent brute-force C++ triangles and a different cosine frame/PRNG.
                        std::mt19937_64 Random(0xe51b1c39ull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                        DirectGpuMoments Cpu;
                        for (uint32_t Index = 0; Index < CpuSamples; ++Index)
                            Cpu.Accumulate(ReplayReferencePath(Current, Guides[Pixel], Bounces, Random)[0]);
                        Reference[Pixel] = Cpu.Mean();
                        const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                        ReferenceVariance += Error * Error;
                    }
                    ReferenceSum += Reference[Pixel];
                    ++Points;
                }
                Require(Points == 794, "Seed RIS reference coverage changed");
            }
            std::array<double,3> BatchSum{};
            for (uint32_t Pixel = 0; Pixel < Reference.size(); ++Pixel)
            {
                if (Reference[Pixel] < 0) continue;
                Require(ReplaySampleSlot(Local, 14, Pixel)[2] == 3 && ReplaySampleSlot(Spatial, 14, Pixel)[2] == 3,
                        "Seed RIS reference pixel lost its full seed bank");
                for (uint32_t Mode = 0; Mode < 3; ++Mode)
                {
                    const double Red = Images[Mode][Pixel][0];
                    Require(std::isfinite(Red) && Red >= 0, "Invalid RIS convergence output");
                    BatchSum[Mode] += Red;
                    Squared[Mode] += (Red - Reference[Pixel]) * (Red - Reference[Pixel]);
                }
            }
            for (uint32_t Mode = 0; Mode < 3; ++Mode) Means[Mode].Accumulate(BatchSum[Mode] / Points);
            if ((Batch + 1) % 4 == 0)
            {
                std::printf("SEED_RIS_BATCH bounces=%u completed=%u/%u\n", Bounces, Batch + 1, Batches);
                std::fflush(stdout);
            }
        }
        const double ReferenceMean = ReferenceSum / Points, ReferenceSe = std::sqrt(ReferenceVariance) / Points;
        constexpr std::array<const char*,3> Labels{"seed_average", "seed_ris_local", "seed_ris_spatial"};
        for (uint32_t Mode = 0; Mode < 3; ++Mode)
        {
            const double Error = Means[Mode].StandardError() * std::sqrt(double(Batches) / (Batches - 1));
            std::printf("SEED_RIS_REFERENCE bounces=%u mode=%s batches=%u points=%u reference=%.9g "
                        "reference_se=%.9g mean=%.9g nominal_batch_se=%.9g rmse=%.9g\n",
                        Bounces, Labels[Mode], Batches, Points, ReferenceMean, ReferenceSe, Means[Mode].Mean(), Error,
                        std::sqrt(Squared[Mode] / (double(Batches) * Points)));
            Require(std::abs(Means[Mode].Mean() - ReferenceMean) < 4 * std::hypot(Error, ReferenceSe) + .001,
                    "Seed RIS mean disagrees with the independent reference");
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             HISTORY GUARDS AND CAPTURE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySeedReservoirGuards(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayCameraPlane(0, 0);
    LightComputeSubmission Compute(Driver), Fresh(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    (void)ExecuteReplaySeedReservoir(Compute, Scene, 1111, true);
    (void)ExecuteReplaySeedReservoir(Compute, Scene, 2222, false);
    Require(Compute.ExtendedStatistics()[23] == 1024, "Seed RIS did not warm up");
    for (uint32_t Pixel = 0; Pixel < 7; ++Pixel)
    {
        auto Bank = ReplaySampleSlot(Compute, 11, Pixel);
        Bank[0] = std::numeric_limits<float>::quiet_NaN();
        SetReplaySampleSlot(Compute, 11, Pixel, Bank);
    }
    const auto Image = ExecuteReplaySeedReservoir(Compute, Scene, 3333, false);
    const auto Raw = ExecuteReplaySampleHistory(Fresh, Scene, 3333, true, false);
    for (uint32_t Pixel = 0; Pixel < 7; ++Pixel)
        Require(ReplaySampleSlot(Compute, 14, Pixel)[2] == 0 && Image[Pixel] == Raw[Pixel],
                "Corrupt RIS candidate banks did not fall back to the current fresh path");
    Require(Compute.ExtendedStatistics()[23] == 1017, "RIS corruption leaked into unaffected banks");
    (void)ExecuteReplaySeedReservoir(Compute, Scene, 4444, false, true);
    Require(Compute.ReplayLastSampleHistoryReset && Compute.ExtendedStatistics()[23] == 0,
            "Spatial policy transition did not reset the RIS bank");
    (void)ExecuteReplaySampleHistory(Compute, Scene, 5555, false, true);
    Require(Compute.ReplayLastSampleHistoryReset && Compute.ExtendedStatistics()[23] == 0,
            "Leaving seed RIS did not reset history");
    (void)ExecuteReplaySeedReservoir(Compute, Scene, 6666, false);
    Require(Compute.ReplayLastSampleHistoryReset && Compute.ExtendedStatistics()[23] == 0,
            "Entering seed RIS did not reset history");
    const auto Changed = ReplayCameraPlane(.03125f, 0, .1f);
    const auto ResetImage = ExecuteReplaySeedReservoir(Compute, Changed, 7777, false);
    const auto ResetRaw = ExecuteReplaySampleHistory(Fresh, Changed, 7777, true, false);
    Require(Compute.ReplayLastSampleHistoryReset && Compute.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(ResetImage, ResetRaw), "Scene revision kept stale RIS seed history");
    const uint32_t Sequence = Compute.HistorySequence;
    uint32_t Rejected = 0;
    try { (void)Compute.ExecuteReplay(Scene, 1, 1, true, true, false, {0,0,0}, .01f, true,
        false, false, false, false, false, 64, false, false, false, false, false, false, false,
        false, 1, false, false, true, true); }
    catch (const std::exception&) { ++Rejected; }
    Require(Rejected == 1 && Sequence == Compute.HistorySequence, "Selected-reservoir temporal reuse was accepted");
    std::printf("SEED_RIS_GUARDS corrupt_banks=7 unaffected=1017 mode_and_spatial_reset=1 "
                "scene_reset_fresh_exact=1 temporal_rejected=1 sequence_unchanged=1\n");
}

void CaptureReplaySeedReservoir(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    LightComputeSubmission Average(Driver), Local(Driver), Spatial(Driver);
    Average.Prepare(Previous, Shaders, false, false, false, false, true);
    Local.Prepare(Previous, Shaders, false, false, false, false, true);
    Spatial.Prepare(Previous, Shaders, false, false, false, false, true);
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        const uint32_t Seed = 340001u + Index * 7919u;
        (void)ExecuteReplaySampleFacets(Average, Previous, Seed, Index == 0, true, 4, {0,0,0});
        (void)ExecuteReplaySeedReservoir(Local, Previous, Seed, Index == 0, false, 4, true);
        (void)ExecuteReplaySeedReservoir(Spatial, Previous, Seed, Index == 0, true, 4, true);
    }
    const auto AverageImage = ExecuteReplaySampleFacets(Average, Current, 380011, false, true, 4, {0,0,0});
    const auto LocalImage = ExecuteReplaySeedReservoir(Local, Current, 380011, false, false, 4, true);
    const auto SpatialImage = ExecuteReplaySeedReservoir(Spatial, Current, 380011, false, true, 4, true);
    Require(Local.ExtendedStatistics()[23] == 3990 && Spatial.ExtendedStatistics()[23] == 3990 &&
                Spatial.ExtendedStatistics()[22] > 0, "Four-bounce RIS capture did not retain or spatially replay candidates");
    for (const auto* Image : {&AverageImage, &LocalImage, &SpatialImage})
        for (const auto& Pixel : *Image)
            for (float Channel : Pixel) Require(std::isfinite(Channel) && Channel >= 0, "RIS capture is invalid");
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/SeedAverage.ppm", AverageImage, Size, Size, 3);
    WriteImage(Directory + "/SeedReservoirLocal.ppm", LocalImage, Size, Size, 3);
    WriteImage(Directory + "/SeedReservoirSpatial.ppm", SpatialImage, Size, Size, 3);
    std::printf("SEED_RIS_CAPTURE size=64 bounces=4 warm_frames=3 accepted=3990 spatial_replays=%u\n",
                Spatial.ExtendedStatistics()[22]);
}

void ValidateReplaySeedReservoir(const char* Driver, const std::string& Shaders)
{
    ValidateReplaySeedReservoirOracle(Driver, Shaders);
    ValidateReplaySeedReservoirGuards(Driver, Shaders);
    ValidateReplaySeedReservoirConvergence(Driver, Shaders);
    std::printf("PASS bounded seed-bank RIS and spatial replay; not general ReSTIR GI\n");
}
} // namespace
