/*==========================================================================================================================================
                                                     REPLAYTEMPORALCONTRACTVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify selected-history retracing, current weighting, geometric rejection and corrupt-source fallback on Vulkan.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                ANALYTIC CURRENT RECTANGLES
//------------------------------------------------------------------------------------------------------------------------

struct TemporalCameraSequence
{
    const char* Label;
    float       Shift;
    float       BlockShift;
    float       PreviousPower;
    float       CurrentPower;
};

ReplaySampleOracleResult InspectTemporalRectangle(
    const LightComputeReceiver& Receiver,
    uint32_t                    Seed,
    float                       BlockShift,
    float                       Power)
{
    if (Power == 0) return {0,false};
    const float Radius = std::sqrt(float(NextReplayTailRandom(Seed) >> 8) / 16777216.f);
    const float Azimuth = 6.283185307179586f * (float(NextReplayTailRandom(Seed) >> 8) / 16777216.f);
    const double Dx = Radius * std::cos(Azimuth), Dy = Radius * std::sin(Azimuth);
    const double Dz = std::sqrt(std::max(0.f,1.f - Radius * Radius));
    if (Dz <= 1e-8) return {0,true};
    const double X = Receiver.PositionHit[0], Y = Receiver.PositionHit[1], Z = Receiver.PositionHit[2] + .001;
    const double BlockX = X + Dx * (.5 - Z) / Dz - BlockShift, BlockY = Y + Dy * (.5 - Z) / Dz;
    const double SourceX = X + Dx * (3 - Z) / Dz, SourceY = Y + Dy * (3 - Z) / Dz;
    const bool Blocked = std::abs(BlockX) <= .2 && std::abs(BlockY) <= .5;
    const bool Hit = std::abs(SourceX) <= 1 && std::abs(SourceY) <= 1;
    const double BlockEdge = std::min(std::abs(std::abs(BlockX)-.2),std::abs(std::abs(BlockY)-.5));
    const double SourceEdge = std::min(std::abs(std::abs(SourceX)-1),std::abs(std::abs(SourceY)-1));
    const bool Edge = (std::abs(BlockX) < .202 && std::abs(BlockY) < .502 && BlockEdge < .001) ||
                      (std::abs(SourceX) < 1.002 && std::abs(SourceY) < 1.002 && SourceEdge < .001);
    return {Hit && !Blocked ? .5f * Power : 0.f,Edge};
}

std::vector<std::array<float,4>> ExecuteTemporalContract(
    LightComputeSubmission&    Compute,
    const LightComputeArchive& Scene,
    uint32_t                   Seed,
    bool                       Reset,
    float                      Floor)
{
    return Compute.ExecuteReplay(Scene,1,Seed,true,false,Reset,{0,0,0},Floor,true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               CHECKED / UNCHECKED PARITY
//------------------------------------------------------------------------------------------------------------------------

void ValidateTemporalContract(const char* Driver, const std::string& Shaders)
{
    const std::array<TemporalCameraSequence,6> Sequences{{
        {"stationary",0,0,2,2}, {"half_pixel",.03125f,0,2,2}, {"camera_cut",4,0,2,2},
        {"blocker_move",0,.125f,2,2}, {"source_off",0,0,2,0}, {"source_on",0,0,0,2}
    }};
    const auto Initial = ReplayCameraPlane(0,0);
    LightComputeSubmission Checked(Driver), Unchecked(Driver);
    Checked.Prepare(Initial,Shaders,false,false,false,false,true);
    Unchecked.Prepare(Initial,Shaders+"/unchecked",false,false,false,false,true);
    for (float Floor : {.0001f,.01f,1.f})
        for (const auto& Sequence : Sequences)
        {
            const auto Previous = ReplayCameraPlane(0,0,0,Sequence.PreviousPower);
            const auto Current = ReplayCameraPlane(Sequence.Shift,0,Sequence.BlockShift,Sequence.CurrentPower);
            for (uint32_t Step = 0; Step < 3; ++Step)
            {
                const auto Fresh = ExecuteTemporalContract(Checked,Previous,150001u+Step*7919u,Step==0,Floor);
                const auto Audit = ExecuteTemporalContract(Unchecked,Previous,150001u+Step*7919u,Step==0,Floor);
                Require(ReplayCandidateImagesEqual(Fresh,Audit),"History guard changed a valid warm-up image");
            }
            std::array<std::vector<std::array<float,4>>,3> Old;
            for (uint32_t Pixel = 0; Pixel < Checked.ReceiverCount; ++Pixel)
                for (uint32_t Index = 0; Index < 3; ++Index)
                    Old[Index].push_back(ReplaySampleSlot(Checked,Index==2 ? 4 : Index,Pixel));
            constexpr uint32_t Seed = 191111;
            const auto Image = ExecuteTemporalContract(Checked,Current,Seed,false,Floor);
            const auto Audit = ExecuteTemporalContract(Unchecked,Current,Seed,false,Floor);
            Require(ReplayCandidateImagesEqual(Image,Audit) && Checked.ExtendedStatistics()==Unchecked.ExtendedStatistics(),
                    "History guard changed valid output or counters");
            const auto Guides = Checked.PrimaryReceivers();
            uint32_t Compared = 0, Boundary = 0, Accepted = 0, Rejected = 0;
            double Maximum = 0;
            for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
            {
                for (uint32_t Slot : {0u,1u,2u,3u,4u})
                    Require(ReplaySampleSlot(Checked,Slot,Pixel)==ReplaySampleSlot(Unchecked,Slot,Pixel),
                            "History guard changed valid reservoir slabs");
                for (float Channel : Image[Pixel]) Require(std::isfinite(Channel),"Nonfinite temporal image");
                const auto& Receiver = Guides[Pixel];
                bool Compatible = false;
                std::array<float,4> Source{};
                if (Receiver.MotionPixel[3] != 0)
                {
                    const int32_t X = int32_t(std::floor(Receiver.MotionPixel[0]+.5f));
                    const int32_t Y = int32_t(std::floor(Receiver.MotionPixel[1]+.5f));
                    if (X>=0 && Y>=0 && X<int32_t(Current.Width) && Y<int32_t(Current.Height))
                    {
                        const uint32_t Donor = uint32_t(Y)*Current.Width+uint32_t(X);
                        Source = Old[0][Donor];
                        double Distance = 0, Dot = 0, PriorLength = 0, MotionLength = 0;
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            const double Delta = Old[1][Donor][Axis]-Receiver.MotionPositionToken[Axis];
                            Distance += Delta*Delta;
                            Dot += double(Old[2][Donor][Axis])*Receiver.MotionNormal[Axis];
                            PriorLength += double(Old[2][Donor][Axis])*Old[2][Donor][Axis];
                            MotionLength += double(Receiver.MotionNormal[Axis])*Receiver.MotionNormal[Axis];
                        }
                        const double Radius = std::clamp(double(Receiver.MotionPixel[2]),0.,.25);
                        Compatible = Source[3]>0 && Source[2]>0 && Old[1][Donor][3]==Receiver.MotionPositionToken[3] &&
                                     Distance<=Radius*Radius && Dot>=.9*std::sqrt(PriorLength*MotionLength);
                        if (Compatible) ++Accepted; else ++Rejected;
                    }
                }
                if (Receiver.MotionPositionToken[3] != 1) continue;
                uint32_t Selected = ReplayFreshSeed(Seed,Pixel);
                auto Fresh = InspectTemporalRectangle(Receiver,Selected,Sequence.BlockShift,Sequence.CurrentPower);
                bool Edge = Fresh.Boundary;
                float Red = Fresh.Radiance, Target = ReplaySeedTarget(Red,Floor), Sum = Target, Count = 1;
                if (Compatible)
                {
                    const uint32_t Candidate = ReplaySampleSeed(Source[0],Source[1]);
                    const auto Replayed = InspectTemporalRectangle(Receiver,Candidate,Sequence.BlockShift,Sequence.CurrentPower);
                    Edge = Edge || Replayed.Boundary;
                    const float Retained = std::min(Source[3],float(Checked.ReplayLastHistoryCap));
                    const float ReplayedTarget = ReplaySeedTarget(Replayed.Radiance,Floor);
                    const float Weight = ReplayedTarget*Source[2]*Retained;
                    Sum += Weight;
                    Count += Retained;
                    uint32_t Selection = (Seed*277803737u+1597334677u)^(Pixel*2891336453u+1u);
                    if (ReplaySeedUnit(Selection)*Sum < Weight)
                    {
                        Selected = Candidate;
                        Red = Replayed.Radiance;
                        Target = ReplayedTarget;
                    }
                }
                if (Edge) { ++Boundary; continue; }
                const float Normalization = Sum/(Count*Target);
                const auto Packed = ReplaySampleSlot(Checked,2,Pixel);
                Require(ReplaySampleSeed(Packed[0],Packed[1])==Selected && Packed[3]==Count,
                        "Temporal winner or represented count differs from CPU selection");
                Require(std::abs(double(Packed[2])-Normalization)<1e-5*(1+Normalization),
                        "Temporal normalization differs from independent selection arithmetic");
                const double Expected = Red*Normalization;
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    Maximum = std::max(Maximum,std::abs(double(Image[Pixel][Channel])-Expected)/(1+std::abs(Expected)));
                ++Compared;
            }
            Require(Compared>500 && Maximum<1e-5 && Checked.ExtendedStatistics()[20]==Accepted &&
                    Checked.ExtendedStatistics()[21]==Rejected,"Temporal rectangles or geometric rejection differ");
            if (Sequence.CurrentPower==0)
                for (const auto& Pixel : Image) Require(Pixel[0]==0 && Pixel[1]==0 && Pixel[2]==0,"Old RGB leaked after source off");
            if (Sequence.Shift==4) Require(Accepted==0,"Camera cut accepted out-of-bounds history");
            std::printf("TEMPORAL_GPU sequence=%s floor=%.4g compared=%u edge_excluded=%u accepted=%u rejected=%u "
                        "history_cap=%u max_scaled_rgb_error=%.9g valid_audit_exact=1\n",Sequence.Label,Floor,
                        Compared,Boundary,Accepted,Rejected,Checked.ReplayLastHistoryCap,Maximum);
            std::fflush(stdout);
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             MALFORMED SELECTED-HISTORY SOURCES
//------------------------------------------------------------------------------------------------------------------------

void ValidateTemporalCorruption(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayCameraPlane(0,0,0,2,17);
    LightComputeSubmission Checked(Driver), Unchecked(Driver);
    Checked.Prepare(Scene,Shaders,false,false,false,false,true);
    Unchecked.Prepare(Scene,Shaders+"/unchecked",false,false,false,false,true);
    uint32_t OldFailures = 0, OldNonfinite = 0;
    const float Nan = std::numeric_limits<float>::quiet_NaN(), Infinity = std::numeric_limits<float>::infinity();
    for (uint32_t Mutation = 0; Mutation < 10; ++Mutation)
    {
        for (uint32_t Step = 0; Step < 3; ++Step)
        {
            (void)ExecuteTemporalContract(Checked,Scene,150001u+Step*7919u,Step==0,.01f);
            (void)ExecuteTemporalContract(Unchecked,Scene,150001u+Step*7919u,Step==0,.01f);
        }
        const uint32_t Pixel = 0;
        auto Source = ReplaySampleSlot(Checked,0,Pixel);
        if (Mutation==0) Source[2]=Infinity;
        if (Mutation==1) Source[2]=Nan;
        if (Mutation==2) Source[3]=Infinity;
        if (Mutation==3) Source[3]=3.5f;
        if (Mutation==4) Source[0]=Nan;
        if (Mutation==5) Source[0]=.5f;
        if (Mutation==6) Source[1]=65536;
        if (Mutation==7) Source[2]=-1;
        if (Mutation==8) Source[3]=69;
        if (Mutation==9)
        {
            const auto Receiver = Checked.PrimaryReceivers()[Pixel];
            uint32_t Seed = 1;
            while (InspectTemporalRectangle(Receiver,Seed,0,2).Radiance!=1) ++Seed;
            Source={float(Seed&65535u),float(Seed>>16u),std::numeric_limits<float>::max(),68};
        }
        SetReplaySampleSlot(Checked,0,Pixel,Source);
        SetReplaySampleSlot(Unchecked,0,Pixel,Source);
        constexpr uint32_t Seed = 191111;
        const auto Image = ExecuteTemporalContract(Checked,Scene,Seed,false,.01f);
        const auto Audit = ExecuteTemporalContract(Unchecked,Scene,Seed,false,.01f);
        const auto Receiver = Checked.PrimaryReceivers()[Pixel];
        const auto Expected = InspectTemporalRectangle(Receiver,ReplayFreshSeed(Seed,Pixel),0,2);
        std::printf("TEMPORAL_CORRUPTION_OBSERVED mutation=%u count=%.9g red=%.9g accepted=%u rejected=%u\n",
                    Mutation,ReplaySampleSlot(Checked,2,Pixel)[3],Image[Pixel][0],
                    Checked.ExtendedStatistics()[20],Checked.ExtendedStatistics()[21]);
        std::fflush(stdout);
        Require(!Expected.Boundary && ReplaySampleSlot(Checked,2,Pixel)[3]==1 &&
                    Checked.ExtendedStatistics()[20]==Scene.Receivers.size()-1 &&
                    Checked.ExtendedStatistics()[21]==1,"Malformed history did not use a fresh-only fallback");
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Require(Image[Pixel][Channel]==Expected.Radiance,"Corrupt history contaminated fresh-only output");
        bool Finite = true;
        for (const auto& Colour : Audit) for (float Channel : Colour) Finite = Finite && std::isfinite(Channel);
        OldNonfinite += Finite ? 0u : 1u;
        const bool Failed = !Finite || ReplaySampleSlot(Unchecked,2,Pixel)[3]!=1;
        OldFailures += Failed ? 1u : 0u;
        std::printf("TEMPORAL_CORRUPTION mutation=%u fresh_fallback=1 audit_contract_failure=%u audit_nonfinite=%u\n",
                    Mutation,unsigned(Failed),unsigned(!Finite));
    }
    Require(OldFailures>=6 && OldNonfinite>=1,"Unchecked negative control did not reproduce the malformed-history defect");
    std::printf("TEMPORAL_GUARDS cases=10 unchecked_failures=%u unchecked_nonfinite=%u\n",OldFailures,OldNonfinite);
    std::fflush(stdout);
    static_assert(PathReplayMaximumInitialCandidates==4,"Temporal source bound requires four fresh candidates");
    for (uint32_t Step=0; Step<18; ++Step)
    {
        const auto Image = ExecuteReplayCameraPlane(Checked,Scene,230001u+Step*7919u,true,false,Step==0,
                                                    false,false,4,1,{0,0,0},false);
        const auto Audit = ExecuteReplayCameraPlane(Unchecked,Scene,230001u+Step*7919u,true,false,Step==0,
                                                    false,false,4,1,{0,0,0},false);
        Require(ReplayCandidateImagesEqual(Image,Audit),"Source guard changed a valid four-candidate sequence");
    }
    for (uint32_t Pixel=0; Pixel<Scene.Receivers.size(); ++Pixel)
        Require(ReplaySampleSlot(Checked,0,Pixel)[3]==68 && ReplaySampleSlot(Unchecked,0,Pixel)[3]==68,
                "Maximum legitimate represented count was rejected");
    Require(Checked.ExtendedStatistics()[20]==Scene.Receivers.size() && Checked.ExtendedStatistics()[21]==0,
            "Maximum-count source failed temporal eligibility");
    std::printf("TEMPORAL_MAX_COUNT fresh_candidates=4 steps=18 represented=68 accepted=%u valid_audit_exact=1\n",
                Checked.ExtendedStatistics()[20]);
}

void CaptureTemporalContract(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    const auto Previous = ReplayCameraPlane(0,0,0,2,64), Current = ReplayCameraPlane(1.f/64,0,0,2,64);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous,Shaders,false,false,false,false,true);
    for (uint32_t Step=0; Step<3; ++Step)
        (void)ExecuteTemporalContract(Compute,Previous,150001u+Step*7919u,Step==0,.01f);
    std::filesystem::create_directories(Directory);
    const auto Image = ExecuteTemporalContract(Compute,Current,191111,false,.01f);
    WriteImage(Directory+"/TemporalCurrent.ppm",Image,64,64,3);
    const uint32_t CurrentAccepted = Compute.ExtendedStatistics()[20];
    const auto Off = ReplayCameraPlane(1.f/64,1.f/64,0,0,64);
    const auto Dark = ExecuteTemporalContract(Compute,Off,200111,false,.01f);
    for (const auto& Pixel : Dark) Require(Pixel[0]==0 && Pixel[1]==0 && Pixel[2]==0,"Captured temporal source-off ghost");
    WriteImage(Directory+"/TemporalSourceOff.ppm",Dark,64,64,3);
    std::printf("TEMPORAL_CAPTURE size=64 current_accepted=%u source_off_black_pixels=4096\n",CurrentAccepted);
}
} // namespace
