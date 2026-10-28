/*==========================================================================================================================================
                                                   REPLAYBOUNDEDTEMPORALVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate a bounded temporal proposal plus three spatial donors at eight current path calls.
#pragma once
#include <cstdlib>

namespace
{
uint32_t BoundedWarmupSteps()
{
    const char* Text=std::getenv("BOUNDED_WARMUP");
    const unsigned long Count=Text ? std::stoul(Text) : 8;
    Require(Count==3 || Count==8,"Bounded audit supports three or eight warm-up submissions");
    return uint32_t(Count);
}

uint64_t ValidateBoundedPathCount(const LightComputeSubmission& Compute, bool Fresh)
{
    uint64_t Expected = 0;
    for (uint32_t Pixel=0; Pixel<Compute.ReceiverCount; ++Pixel)
    {
        const float Count = ReplaySampleSlot(Compute,2,Pixel)[3];
        if (Count<=0) continue;
        const float Local = ReplaySampleSlot(Compute,14,Pixel)[2]+1;
        Require(Count>=Local && Count<=2*Local && Local>=1 && Local<=4,
                "Bounded history exceeded the current local represented count");
        Expected += Fresh ? 8u : uint32_t(Local);
    }
    if (!Fresh) Expected += Compute.ExtendedStatistics()[20]+Compute.ExtendedStatistics()[22]-Compute.ExtendedStatistics()[15];
    Require(Expected==Compute.ExtendedStatistics()[16] && Expected<=uint64_t(Compute.ReceiverCount)*8,
            "Bounded actual path calls differ from the declared eight-call ceiling");
    return Expected;
}

bool ValidateBoundedPixelEightCalls(const LightComputeSubmission& Compute, uint32_t Pixel)
{
    const uint32_t Width=Compute.ReceiverWidth, Height=Compute.ReceiverHeight;
    const uint32_t X=Pixel%Width, Y=Pixel/Width;
    if (X==0 || Y==0 || X+1>=Width || Y+1>=Height) return false;
    if (ReplaySampleSlot(Compute,14,Pixel)[2]!=3 || ReplaySampleSlot(Compute,2,Pixel)[3]<=4) return false;
    const std::array<uint32_t,4> Donors{Pixel-1,Pixel+1,Pixel-Width,Pixel+Width};
    for (uint32_t Index=0; Index<4; ++Index)
        if (Index!=(Pixel&3u) && ReplaySampleSlot(Compute,2,Donors[Index])[3]<=0) return false;
    return true;
}

void ValidateBoundedTemporalGuards(const char* Driver, const std::string& Shaders)
{
    const auto Scene=ReplayCameraPlane(0,0,0,2,17);
    LightComputeSubmission Corrupt(Driver), Absent(Driver);
    Corrupt.Prepare(Scene,Shaders,false,false,false,false,true,false,true);
    Absent.Prepare(Scene,Shaders,false,false,false,false,true,false,true);
    for (uint32_t Mutation=0; Mutation<4; ++Mutation)
    {
        for (uint32_t Step=0; Step<3; ++Step)
        {
            (void)ExecuteReplaySeedReservoir(Corrupt,Scene,150001u+7919u*Step,Step==0,true);
            (void)ExecuteReplaySeedReservoir(Absent,Scene,150001u+7919u*Step,Step==0,true);
        }
        auto Source=ReplaySampleSlot(Corrupt,0,0);
        auto Empty=Source;
        Empty[3]=0;
        if (Mutation==0) Source[2]=std::numeric_limits<float>::infinity();
        if (Mutation==1) Source[0]=.5f;
        if (Mutation==2) Source[3]=9;
        if (Mutation==3)
        {
            const auto Receiver=Corrupt.PrimaryReceivers()[0];
            uint32_t Seed=1;
            while (InspectTemporalRectangle(Receiver,Seed,0,2).Radiance!=1) ++Seed;
            Source={float(Seed&65535u),float(Seed>>16u),std::numeric_limits<float>::max(),8};
        }
        SetReplaySampleSlot(Corrupt,0,0,Source);
        SetReplaySampleSlot(Absent,0,0,Empty);
        const auto Image=ExecuteReplaySeedReservoir(Corrupt,Scene,191111,false,true);
        const auto Control=ExecuteReplaySeedReservoir(Absent,Scene,191111,false,true);
        Require(ReplayCandidateImagesEqual(Image,Control) && ReplaySampleSlot(Corrupt,2,0)[3]==4 &&
                Corrupt.ExtendedStatistics()[20]==288 && Corrupt.ExtendedStatistics()[21]==1,
                "Bounded temporal corruption did not equal absent-history output");
        for (const auto& Colour:Image) for (float Channel:Colour)
            Require(std::isfinite(Channel),"Bounded corrupt-source fallback was nonfinite");
    }
    std::puts("BOUNDED_GUARDS corrupt_sources=4 absent_history_output_exact=1 finite=1");
}

void ValidateBoundedEvaluationCache(const char* Driver, const std::string& Shaders)
{
    const auto Previous=ReplayCameraPlane(0,0), Current=ReplayCameraPlane(.03125f,0);
    LightComputeSubmission Cached(Driver), Original(Driver);
    Cached.Prepare(Previous,Shaders,false,false,false,false,true,false,true);
    Original.Prepare(Previous,Shaders+"/uncached",false,false,false,false,true,false,true);
    uint32_t Frames=0;
    uint64_t Saved=0;
    for (bool Facets:{false,true})
        for (uint32_t Step=0; Step<9; ++Step)
        {
            const auto& Scene=Step==8 ? Current : Previous;
            const uint32_t Seed=340001u+7919u*Step;
            const auto Image=ExecuteReplaySeedReservoir(Cached,Scene,Seed,Step==0,true,4,Facets,.01f,{.025f,.025f,.025f});
            const auto Control=ExecuteReplaySeedReservoir(Original,Scene,Seed,Step==0,true,4,Facets,.01f,{.025f,.025f,.025f});
            Require(ReplayCandidateImagesEqual(Image,Control),"Caching changed bounded temporal radiance");
            for (uint32_t Pixel=0; Pixel<Image.size(); ++Pixel)
                for (uint32_t Slot:{0u,1u,2u,3u,4u,11u,12u,13u,14u})
                    Require(ReplaySampleSlot(Cached,Slot,Pixel)==ReplaySampleSlot(Original,Slot,Pixel),
                            "Caching changed a reservoir or pure-fresh seed bank");
            const auto A=Cached.ExtendedStatistics(), B=Original.ExtendedStatistics();
            for (uint32_t Counter:{20u,21u,22u,23u})
                Require(A[Counter]==B[Counter],"Caching changed proposal admission or geometry rejection");
            Require(A[16]+A[15]==B[16] && B[15]==0,"Evaluation cache failed exact path-work accounting");
            Saved+=A[15];
            ++Frames;
        }
    Require(Saved>0,"Current-surface cache was never used");
    std::printf("BOUNDED_CACHE_PARITY frames=%u output_and_nine_slabs_exact=1 saved_calls=%llu\n",Frames,
                static_cast<unsigned long long>(Saved));
}

void ValidateReplayBoundedTemporal(const char* Driver, const std::string& Shaders)
{
    ValidateFreshBudgetRectangle(Driver, Shaders);
    ValidateBoundedTemporalGuards(Driver, Shaders);
    ValidateBoundedEvaluationCache(Driver, Shaders);
    constexpr uint32_t Batches = 32, CpuSamples = 4096;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Fresh(Driver), Reused(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    Reused.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline FreshAccess(Fresh, Shaders);
    std::vector<uint32_t> Pixels;
    std::array<std::vector<double>,2> References;
    std::array<double,2> ReferenceMean{}, ReferenceSe{};
    std::printf("BOUNDED_DEVICE name=%s type=%u api=%u compact_output_bytes=%llu\n",
        Reused.Properties.deviceName, uint32_t(Reused.Properties.deviceType), Reused.Properties.apiVersion,
        static_cast<unsigned long long>(Reused.ReplayOutputDescriptorBytes));
    std::fflush(stdout);
    for (uint32_t BounceIndex = 0; BounceIndex < 2; ++BounceIndex)
        for (bool Facets : {false, true})
        {
            const uint32_t Bounces = BounceIndex == 0 ? 1 : 4;
            const float Ambient = Bounces == 1 ? 0.f : .025f;
            const std::array<float,3> Environment{Ambient, Ambient, Ambient};
            std::mt19937_64 Seeds(0xb51d4a79ull);
            std::array<DirectGpuMoments,2> Means;
            DirectGpuMoments PairedMean, PairedMse;
            std::array<double,2> Squared{};
            uint64_t FreshCalls = 0, ReusedCalls = 0, WarmCalls = 0;
            uint64_t FreshQueries = 0, ReusedQueries = 0, WarmQueries = 0, Duplicates = 0, CachedCalls = 0;
            for (uint32_t Batch = 0; Batch < Batches; ++Batch)
            {
                for (uint32_t Index = 0; Index < BoundedWarmupSteps(); ++Index)
                {
                    (void)ExecuteReplaySeedReservoir(Reused, Previous, uint32_t(Seeds()), Index == 0,
                                                     true, Bounces, Facets, .01f, Environment);
                    WarmCalls += ValidateBoundedPathCount(Reused, false);
                    WarmQueries += Reused.ExtendedStatistics()[17] + Reused.ExtendedStatistics()[2];
                }
                std::vector<std::array<float,4>> Prior;
                for (uint32_t Pixel=0; Pixel<Reused.ReceiverCount; ++Pixel)
                    Prior.push_back(ReplaySampleSlot(Reused,0,Pixel));
                const uint32_t Seed = uint32_t(Seeds());
                const auto FreshImage = ExecuteFreshBudget(FreshAccess, Current, Seed, Bounces, Environment, Facets);
                const auto ReusedImage = ExecuteReplaySeedReservoir(Reused, Current, Seed, false, true,
                                                         Bounces, Facets, .01f, Environment);
                FreshCalls += ValidateBoundedPathCount(Fresh, true);
                ReusedCalls += ValidateBoundedPathCount(Reused, false);
                CachedCalls += Reused.ExtendedStatistics()[15];
                FreshQueries += Fresh.ExtendedStatistics()[17] + Fresh.ExtendedStatistics()[2];
                ReusedQueries += Reused.ExtendedStatistics()[17] + Reused.ExtendedStatistics()[2];
                const auto Guides = Reused.PrimaryReceivers();
                if (Pixels.empty())
                    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                        if (Guides[Pixel].MotionPositionToken[3] == 1 && ValidateBoundedPixelEightCalls(Reused, Pixel))
                            Pixels.push_back(Pixel);
                Require(Pixels.size() > 500, "Equal-budget geometric measurement mask is too small");
                if (References[BounceIndex].empty())
                {
                    double Sum = 0, Variance = 0, QuadratureSquared = 0;
                    for (uint32_t Pixel : Pixels)
                    {
                        double Reference = 0;
                        if (Bounces == 1)
                        {
                            Reference = ReplayCameraPlaneArea(Guides[Pixel], 128);
                            const double Coarse = ReplayCameraPlaneArea(Guides[Pixel], 64);
                            QuadratureSquared += (Reference - Coarse) * (Reference - Coarse);
                        }
                        else
                        {
                            std::mt19937_64 Random(0xe51b1c39ull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                            DirectGpuMoments Cpu;
                            for (uint32_t Index = 0; Index < CpuSamples; ++Index)
                                Cpu.Accumulate(ReplayReferencePath(Current, Guides[Pixel], Bounces, Random)[0]);
                            Reference = Cpu.Mean();
                            const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                            Variance += Error * Error;
                        }
                        References[BounceIndex].push_back(Reference);
                        Sum += Reference;
                    }
                    ReferenceMean[BounceIndex] = Sum / Pixels.size();
                    ReferenceSe[BounceIndex] = std::sqrt(Variance) / Pixels.size();
                    Require(Bounces != 1 || std::sqrt(QuadratureSquared / Pixels.size()) < .002,
                            "Area-integral finite quadrature disagrees");
                    std::printf("BOUNDED_REFERENCE bounces=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "cpu_paths=%zu quadrature_rmse=%.9g\n", Bounces, Pixels.size(), ReferenceMean[BounceIndex],
                        ReferenceSe[BounceIndex], Bounces == 4 ? Pixels.size() * CpuSamples : 0,
                        std::sqrt(QuadratureSquared / Pixels.size()));
                    std::fflush(stdout);
                }
                std::array<double,2> BatchSum{}, BatchSquared{};
                for (size_t Index = 0; Index < Pixels.size(); ++Index)
                {
                    const uint32_t Pixel = Pixels[Index];
                    Require(ValidateBoundedPixelEightCalls(Reused, Pixel) && ReplaySampleSlot(Reused, 14, Pixel)[2] == 3,
                            "Measurement pixel lost its eight-current-path contract");
                    const auto& Receiver=Guides[Pixel];
                    const uint32_t Donor=uint32_t(std::floor(Receiver.MotionPixel[1]+.5f))*Current.Width+
                                         uint32_t(std::floor(Receiver.MotionPixel[0]+.5f));
                    const uint32_t Historical=ReplaySampleSeed(Prior[Donor][0],Prior[Donor][1]);
                    const auto First=ReplaySampleSlot(Reused,13,Pixel), Second=ReplaySampleSlot(Reused,14,Pixel);
                    const std::array<uint32_t,4> LocalSeeds{ReplayFreshSeed(Seed,Pixel),
                        ReplaySampleSeed(First[0],First[1]),ReplaySampleSeed(First[2],First[3]),
                        ReplaySampleSeed(Second[0],Second[1])};
                    Duplicates += std::find(LocalSeeds.begin(),LocalSeeds.end(),Historical)!=LocalSeeds.end() ? 1u : 0u;
                    for (uint32_t Mode = 0; Mode < 2; ++Mode)
                    {
                        const double Red = Mode == 0 ? FreshImage[Pixel][0] : ReusedImage[Pixel][0];
                        Require(std::isfinite(Red) && Red >= 0, "Invalid equal-budget radiance");
                        const double Error = Red - References[BounceIndex][Index];
                        BatchSum[Mode] += Red;
                        BatchSquared[Mode] += Error * Error;
                    }
                }
                for (uint32_t Mode = 0; Mode < 2; ++Mode)
                {
                    Means[Mode].Accumulate(BatchSum[Mode] / Pixels.size());
                    Squared[Mode] += BatchSquared[Mode];
                }
                PairedMean.Accumulate((BatchSum[1] - BatchSum[0]) / Pixels.size());
                PairedMse.Accumulate((BatchSquared[1] - BatchSquared[0]) / Pixels.size());
                if ((Batch + 1) % 4 == 0)
                {
                    std::printf("BOUNDED_PROGRESS bounces=%u facets=%u completed=%u/%u\n",
                                Bounces, unsigned(Facets), Batch + 1, Batches);
                    std::fflush(stdout);
                }
            }
            const double Correction = std::sqrt(double(Batches) / (Batches - 1));
            for (uint32_t Mode = 0; Mode < 2; ++Mode)
            {
                const double Error = Means[Mode].StandardError() * Correction;
                std::printf("BOUNDED_RESULT bounces=%u facets=%u mode=%s batches=%u points=%zu "
                    "paths_at_measured_pixel=8 mean=%.9g nominal_batch_se=%.9g rmse=%.9g reference=%.9g\n",
                    Bounces, unsigned(Facets), Mode == 0 ? "fresh_eight" : "bounded_temporal", Batches, Pixels.size(),
                    Means[Mode].Mean(), Error, std::sqrt(Squared[Mode] / (Batches * Pixels.size())),
                    ReferenceMean[BounceIndex]);
                Require(std::abs(Means[Mode].Mean() - ReferenceMean[BounceIndex]) <
                    4 * std::hypot(Error, ReferenceSe[BounceIndex]) + .001,
                    "Equal-budget mean disagrees with the independent reference");
            }
            std::printf("BOUNDED_OVERLAP bounces=%u facets=%u warm_frames=%u duplicate_temporal_seeds=%llu "
                        "measured_temporal_seeds=%zu\n",Bounces,unsigned(Facets),BoundedWarmupSteps(),
                        static_cast<unsigned long long>(Duplicates),Pixels.size()*Batches);
            std::printf("BOUNDED_CACHE bounces=%u facets=%u saved_current_calls=%llu\n",Bounces,unsigned(Facets),
                        static_cast<unsigned long long>(CachedCalls));
            const double DifferenceError = PairedMean.StandardError() * Correction;
            std::printf("BOUNDED_PAIRED bounces=%u facets=%u mean_difference=%.9g nominal_mean_se=%.9g "
                "mse_difference=%.9g nominal_mse_se=%.9g\n", Bounces, unsigned(Facets), PairedMean.Mean(),
                DifferenceError, PairedMse.Mean(), PairedMse.StandardError() * Correction);
            Require(std::abs(PairedMean.Mean()) < 4 * DifferenceError + .001,
                    "Equal-budget paired means disagree beyond the declared gate");
            std::printf("BOUNDED_WORK bounces=%u facets=%u current_submissions_each=%u warm_submissions_reuse=%u "
                "fresh_current_calls=%llu reuse_current_calls=%llu reuse_warm_calls=%llu "
                "fresh_indirect_queries=%llu reuse_indirect_queries=%llu reuse_warm_indirect_queries=%llu\n",
                Bounces, unsigned(Facets), Batches, Batches * BoundedWarmupSteps(),
                static_cast<unsigned long long>(FreshCalls), static_cast<unsigned long long>(ReusedCalls),
                static_cast<unsigned long long>(WarmCalls), static_cast<unsigned long long>(FreshQueries),
                static_cast<unsigned long long>(ReusedQueries), static_cast<unsigned long long>(WarmQueries));
            std::fflush(stdout);
        }
    std::printf("PASS bounded eight-current-path comparison and reference gates; quality advantage is not required to pass\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ACTUAL READBACKS
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplayBoundedTemporal(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    LightComputeSubmission Fresh(Driver), Reused(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    Reused.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline ControlPipeline(Fresh, Shaders);
    std::filesystem::create_directories(Directory);
    for (bool Facets : {false, true})
    {
        uint64_t Warm = 0;
        for (uint32_t Index = 0; Index < BoundedWarmupSteps(); ++Index)
        {
            (void)ExecuteReplaySeedReservoir(Reused, Previous, 340001u + Index * 7919u, Index == 0,
                                             true, 4, Facets, .01f, {.025f,.025f,.025f});
            Warm += ValidateBoundedPathCount(Reused, false);
        }
        const auto FreshImage = ExecuteFreshBudget(ControlPipeline, Current, 380011, 4, {.025f,.025f,.025f}, Facets);
        const auto ReusedImage = ExecuteReplaySeedReservoir(Reused, Current, 380011, false, true, 4, Facets,
                                                  .01f, {.025f,.025f,.025f});
        std::vector<std::array<float,4>> Mask(ReusedImage.size());
        const auto Guides = Reused.PrimaryReceivers();
        uint32_t Measured = 0;
        for (uint32_t Pixel = 0; Pixel < ReusedImage.size(); ++Pixel)
        {
            const bool Accepted = Guides[Pixel].MotionPositionToken[3] == 1 && ValidateBoundedPixelEightCalls(Reused, Pixel);
            Mask[Pixel] = Accepted ? std::array<float,4>{.05f,1,.05f,1} : std::array<float,4>{.5f,.02f,.02f,1};
            Measured += Accepted ? 1u : 0u;
        }
        const std::string Suffix = Facets ? "Facet" : "Cosine";
        WriteImage(Directory + "/BoundedFresh" + Suffix + ".ppm", FreshImage, Size, Size, 3);
        WriteImage(Directory + "/BoundedReuse" + Suffix + ".ppm", ReusedImage, Size, Size, 3);
        WriteImage(Directory + "/BoundedMask" + Suffix + ".ppm", Mask, Size, Size, 3);
        std::printf("BOUNDED_CAPTURE size=64 bounces=4 facets=%u measured_points=%u fresh_calls=%llu "
            "reuse_calls=%llu warm_reuse_calls=%llu\n", unsigned(Facets), Measured,
            static_cast<unsigned long long>(ValidateBoundedPathCount(Fresh, true)),
            static_cast<unsigned long long>(ValidateBoundedPathCount(Reused, false)),
            static_cast<unsigned long long>(Warm));
        const auto Off=ReplayCameraPlane(1.f/Size,1.f/Size,0,0,Size);
        const auto Dark=ExecuteReplaySeedReservoir(Reused,Off,390013,false,true,4,Facets,.01f,{0,0,0});
        Require(Reused.ReplayLastSampleHistoryReset && Reused.ExtendedStatistics()[20]==0 &&
                Reused.ExtendedStatistics()[23]==0,"Changed scene reused bounded history");
        for (const auto& Colour:Dark)
            Require(Colour[0]==0 && Colour[1]==0 && Colour[2]==0,"Bounded temporal source-off ghost");
        WriteImage(Directory+"/BoundedOff"+Suffix+".ppm",Dark,Size,Size,3);
        std::printf("BOUNDED_RESET facets=%u selected_history=0 bank_history=0 black_pixels=4096\n",unsigned(Facets));
    }
}
} // namespace
