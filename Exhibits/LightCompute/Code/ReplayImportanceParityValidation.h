/*==========================================================================================================================================
                                         REPLAYIMPORTANCEPARITYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare default-off GPU outputs with the replay shader from the Engine-promotion checkpoint.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFAULT SHADER READBACKS
//------------------------------------------------------------------------------------------------------------------------

struct ReplayParityReadbacks
{
    std::vector<std::vector<std::array<float,4>>> Images;
    std::vector<std::array<uint32_t,3>> History;
};

ReplayParityReadbacks ReadReplayParityCases(const char* Driver, const std::string& Shaders, bool Importance = false,
                                            bool PowerWeighted = false)
{
    ReplayParityReadbacks Result;
    const auto Before = RoughTexturedRoom(0, 7, .65f, .35f);
    const auto Changed = RoughTexturedRoom(1, 7, .48f, .7f);
    const auto Covered = RoughTexturedRoom(1, 0, .48f, .7f);
    {
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Before, Shaders, false, false, false, false, true);
        Result.Images.push_back(TraceRoughTextureReplay(Compute, Before, 1, 740001, false, false, true,
                                                        false, true, true, Importance, PowerWeighted));
        Result.Images.push_back(TraceRoughTextureReplay(Compute, Before, 4, 740002, false, false, true,
                                                        false, true, true, Importance, PowerWeighted));
        for (uint32_t Iteration = 0; Iteration < 4; ++Iteration)
            (void)TraceRoughTextureReplay(Compute, Before, 4, 740010 + Iteration, true, false, Iteration == 0,
                                           false, true, true, Importance, PowerWeighted);
        for (const auto* Scene : {&Changed, &Covered})
        {
            Result.Images.push_back(TraceRoughTextureReplay(Compute, *Scene, 4, 740030 + uint32_t(Result.Images.size()),
                                                            true, true, false, false, true, true, Importance, PowerWeighted));
            const auto Counts = Compute.ExtendedStatistics();
            Result.History.push_back({Counts[20], Counts[22], Compute.ReplayLastHistoryCap});
            Require(!Compute.ReplayLastReset && Counts[20] > 0 && Counts[22] > 0 && Compute.ReplayLastHistoryCap == 1,
                    "Default parity fixture lost temporal/spatial history");
        }
    }
    {
        auto Scene = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, 64, 64);
        SetRoughTextureReplayMaterials(Scene, .48f, .7f);
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Scene, Shaders, false, false, false, false, true);
        Result.Images.push_back(TraceRoughTextureReplay(Compute, Scene, 4, 740101, false, true, true, true,
                                                        true, true, Importance, PowerWeighted));
    }
    return Result;
}

void ValidateReplayImportanceDefaultParity(const char* Driver, const std::string& CurrentShaders,
                                            const std::string& PreviousShaders)
{
    std::printf("IMPORTANCE_PARITY comparing previous checkpoint and current default-off shader\n");
    std::fflush(stdout);
    const auto Previous = ReadReplayParityCases(Driver, PreviousShaders);
    const auto Current = ReadReplayParityCases(Driver, CurrentShaders);
    Require(Previous.Images.size() == Current.Images.size() && Previous.History == Current.History,
            "Default replay history changed after adding the optional proposal");
    uint64_t DifferentComponents = 0, NonzeroPixels = 0, Components = 0;
    double MaximumDifference = 0;
    for (size_t I = 0; I < Previous.Images.size(); ++I)
    {
        Require(Previous.Images[I].size() == Current.Images[I].size(), "Default parity image extent changed");
        for (size_t P = 0; P < Previous.Images[I].size(); ++P)
        {
            const auto& A = Previous.Images[I][P]; const auto& B = Current.Images[I][P];
            NonzeroPixels += A[0] > 0 ? 1u : 0u;
            for (uint32_t C = 0; C < 4; ++C)
            {
                DifferentComponents += std::memcmp(&A[C], &B[C], sizeof(float)) != 0 ? 1u : 0u;
                MaximumDifference = std::max(MaximumDifference, std::abs(double(A[C]) - double(B[C])));
                ++Components;
            }
        }
    }
    std::printf("IMPORTANCE_PARITY images=%zu components=%llu differing_float_bits=%llu max_abs=%.9g "
                "nonzero_pixels=%llu history=%zu\n", Previous.Images.size(), (unsigned long long)Components,
                (unsigned long long)DifferentComponents, MaximumDifference, (unsigned long long)NonzeroPixels,
                Previous.History.size());
    Require(DifferentComponents == 0 && NonzeroPixels > 1000, "Default-off replay readback changed");
    std::printf("PASS bit-identical default-off replay readbacks and history against Engine checkpoint\n");
}
} // namespace
