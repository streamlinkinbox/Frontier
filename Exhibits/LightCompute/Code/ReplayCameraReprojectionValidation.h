/*==========================================================================================================================================
                                        REPLAYCAMERAREPROJECTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate strict diffuse camera reprojection, disocclusion rejection and independent light-area integrals.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                STATIC CAMERA SCENE
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ReplayCameraPlane(float Shift, float PreviousShift, float BlockShift = 0, float SourcePower = 2,
                                     uint32_t Size = 32)
{
    LightComputeArchive Scene;
    Scene.Width = Scene.Height = Size;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    std::vector<ReplayPatch> Patches;
    const auto Quad = [&](SpatialVector A, SpatialVector B, SpatialVector C, SpatialVector D,
                          std::array<float,3> Normal, std::array<float,3> Albedo,
                          std::array<float,3> Radiance, float Token)
    {
        Patches.push_back({{A,B,C}, Normal, Albedo, Radiance, Token});
        Patches.push_back({{A,C,D}, Normal, Albedo, Radiance, Token});
    };
    Quad({-4,-4,0}, {4,-4,0}, {4,4,0}, {-4,4,0}, {0,0,1}, {.5f,.5f,.5f}, {0,0,0}, 1);
    Quad({-.2+BlockShift,-.5,.5}, {.2+BlockShift,-.5,.5}, {.2+BlockShift,.5,.5}, {-.2+BlockShift,.5,.5},
         {0,0,1}, {.2f,.2f,.2f}, {0,0,0}, 2);
    Quad({-1,-1,3}, {1,-1,3}, {1,1,3}, {-1,1,3}, {0,0,-1}, {0,0,0},
         {SourcePower,SourcePower,SourcePower}, 3);
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
    Scene.Camera[0] = {Shift,0,2,.01f};
    Scene.Camera[1] = {0,0,-1,10};
    Scene.Camera[2] = {1,0,0,.5f};
    Scene.Camera[3] = {0,1,0,.5f};
    Scene.Camera[4] = {PreviousShift,0,2,.01f};
    Scene.Camera[5] = Scene.Camera[1];
    Scene.Camera[6] = Scene.Camera[2];
    Scene.Camera[7] = Scene.Camera[3];
    Scene.Camera[8] = {0,0,1000,0};
    Scene.Camera[9] = Scene.Camera[8];
    Scene.Camera[10] = {0,0,0,1};
    Scene.Camera[11] = {1,0,1,0};
    return Scene;
}

std::vector<std::array<float,4>> ExecuteReplayCameraPlane(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Temporal, bool Spatial, bool Reset,
    bool Reconstruct, bool CameraReprojection, uint32_t Candidates = 1, uint32_t Bounces = 1,
    std::array<float,3> Environment = {0,0,0}, bool SampleFacets = true)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, Temporal, Spatial, Reset, Environment, .01f, true,
                                 false, false, SampleFacets, false, false, 64, false, false, false,
                                 false, false, false, false, Reconstruct, Candidates, false, CameraReprojection);
}

// 📝 Independent rectangle integration: the blocker is intersected analytically rather than through the BVH.
double ReplayCameraPlaneArea(const LightComputeReceiver& Receiver, uint32_t Steps)
{
    const double X = Receiver.PositionHit[0], Y = Receiver.PositionHit[1];
    const double OriginZ = double(Receiver.PositionHit[2]) + .001;
    const double Height = 3 - OriginZ, BlockFraction = (.5 - OriginZ) / Height, Step = 2.0 / Steps;
    double Sum = 0;
    for (uint32_t Row = 0; Row < Steps; ++Row)
        for (uint32_t Column = 0; Column < Steps; ++Column)
        {
            const double SourceX = (Column + .5) * Step - 1, SourceY = (Row + .5) * Step - 1;
            const double BlockX = X + (SourceX - X) * BlockFraction;
            const double BlockY = Y + (SourceY - Y) * BlockFraction;
            if (std::abs(BlockX) <= .2 && std::abs(BlockY) <= .5) continue;
            const double Dx = SourceX - X, Dy = SourceY - Y, DistanceSquared = Dx*Dx + Dy*Dy + Height*Height;
            Sum += Height*Height / (DistanceSquared*DistanceSquared);
        }
    return Sum * Step * Step / 3.14159265358979323846;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         EXACT REPROJECTION AND OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCameraReprojection(const char* Driver, const std::string& Shaders,
                                      bool Temporal, bool Spatial, uint32_t Candidates)
{
    const auto Previous = ReplayCameraPlane(0, 0);
    const auto Current = ReplayCameraPlane(.0625f, 0);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    std::vector<std::array<float,4>> PriorColour;
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
    {
        const uint32_t Seed = 510001 + 7919u * Frame;
        (void)ExecuteReplayCameraPlane(Fresh, Previous, Seed, Temporal, Spatial, Frame == 0, false, false, Candidates);
        PriorColour = ExecuteReplayCameraPlane(Filtered, Previous, Seed, Temporal, Spatial, Frame == 0,
                                               true, true, Candidates);
    }
    const auto PreviousGuides = Filtered.PrimaryReceivers();
    std::vector<std::array<float,4>> PriorHistory;
    for (uint32_t Pixel = 0; Pixel < PriorColour.size(); ++Pixel)
        PriorHistory.push_back(ReplayTailSlot(Filtered, 5, Pixel));
    const auto Raw = ExecuteReplayCameraPlane(Fresh, Current, 590001, Temporal, Spatial, false, false, false, Candidates);
    const auto Result = ExecuteReplayCameraPlane(Filtered, Current, 590001, Temporal, Spatial, false, true, true, Candidates);
    const auto Counts = Filtered.ExtendedStatistics();
    const auto Guides = Filtered.PrimaryReceivers();
    uint32_t Accepted = 0, Rejected = 0, Disoccluded = 0, FloorAccepted = 0, ChangedView = 0;
    double MaximumRecurrenceError = 0, RawSquaredError = 0, FilteredSquaredError = 0, QuadratureSquaredDifference = 0;
    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        const auto& Receiver = Guides[Pixel];
        const auto Retained = ReplayTailSlot(Filtered, 6, Pixel);
        const bool AcceptedHistory = Retained[3] >= 1;
        if (!AcceptedHistory)
        {
            ++Rejected;
            Require(std::memcmp(Raw[Pixel].data(), Result[Pixel].data(), sizeof(Result[Pixel])) == 0,
                    "Rejected camera history did not preserve fresh colour bits");
        }
        if (Receiver.PositionHit[3] == 0 || Receiver.MotionPixel[3] == 0) continue;
        const int32_t X = int32_t(std::floor(Receiver.MotionPixel[0] + .5f));
        const int32_t Y = int32_t(std::floor(Receiver.MotionPixel[1] + .5f));
        if (X < 0 || Y < 0 || X >= int32_t(Current.Width) || Y >= int32_t(Current.Height))
        {
            Require(!AcceptedHistory, "Out-of-bounds camera history was accepted");
            continue;
        }
        const uint32_t PreviousPixel = uint32_t(Y) * Current.Width + uint32_t(X);
        const auto& Old = PreviousGuides[PreviousPixel];
        if (Old.MotionPositionToken[3] != Receiver.MotionPositionToken[3])
        {
            ++Disoccluded;
            Require(!AcceptedHistory, "Disoccluded surface retained incompatible colour");
            continue;
        }
        if (!AcceptedHistory) continue;
        ++Accepted;
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Require(Old.PositionHit[Axis] == Receiver.PositionHit[Axis] &&
                        Old.NormalRoughness[Axis] == Receiver.NormalRoughness[Axis] &&
                        Old.PositionHit[Axis] == Receiver.MotionPositionToken[Axis] &&
                        Old.NormalRoughness[Axis] == Receiver.MotionNormal[Axis],
                    "Accepted camera history did not identify the exact static point and normal");
            ChangedView += Old.ViewMetallic[Axis] != Receiver.ViewMetallic[Axis] ? 1u : 0u;
            const float Length = std::min(PriorHistory[PreviousPixel][3] + 1, 16.f);
            const float Expected = PriorColour[PreviousPixel][Axis] +
                                   (Raw[Pixel][Axis] - PriorColour[PreviousPixel][Axis]) / Length;
            MaximumRecurrenceError = std::max(MaximumRecurrenceError, std::abs(double(Result[Pixel][Axis]) - Expected));
        }
        if (Receiver.MotionPositionToken[3] == 1 && !Temporal && !Spatial)
        {
            const double Reference = ReplayCameraPlaneArea(Receiver, 128);
            const double Coarse = ReplayCameraPlaneArea(Receiver, 64);
            RawSquaredError += std::pow(double(Raw[Pixel][0]) - Reference, 2);
            FilteredSquaredError += std::pow(double(Result[Pixel][0]) - Reference, 2);
            QuadratureSquaredDifference += (Reference - Coarse) * (Reference - Coarse);
            ++FloorAccepted;
        }
    }
    Require(!Filtered.ReplayLastReset && !Filtered.ReplayLastReconstructionReset && Filtered.ReplayLastHistoryCap == 1 &&
                Accepted == Counts[23] && Accepted > 100 && Rejected > 100 && Disoccluded > 5 && ChangedView > 100 &&
                MaximumRecurrenceError < 2e-6,
            "Strict camera reprojection did not retain only matching points and reject disocclusions");
    if (Temporal) Require(Counts[20] > 0, "Camera reprojection did not exercise reservoir history");
    if (Spatial) Require(Counts[22] > 0, "Camera reprojection did not exercise spatial donors");
    if (FloorAccepted > 0)
    {
        const double RawRmse = std::sqrt(RawSquaredError / FloorAccepted);
        const double FilteredRmse = std::sqrt(FilteredSquaredError / FloorAccepted);
        const double QuadratureRmse = std::sqrt(QuadratureSquaredDifference / FloorAccepted);
        std::printf("CAMERA_AREA points=%u raw_rmse=%.9g filtered_rmse=%.9g ratio=%.9g quadrature_rmse=%.9g\n",
                    FloorAccepted, RawRmse, FilteredRmse, FilteredRmse / RawRmse, QuadratureRmse);
        Require(FloorAccepted > 100 && FilteredRmse < .8 * RawRmse && QuadratureRmse < .002,
                "Camera history did not improve the checked diffuse area fixture");
    }
    std::printf("CAMERA_REPROJECTION candidates=%u temporal=%u spatial=%u accepted=%u rejected=%u "
                "disoccluded=%u changed_view_components=%u recurrence_error=%.9g reservoir=%u spatial_links=%u\n",
                Candidates, Temporal ? 1u : 0u, Spatial ? 1u : 0u, Accepted, Rejected, Disoccluded, ChangedView,
                MaximumRecurrenceError, Counts[20], Counts[22]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      INDEPENDENT MULTIBOUNCE CAMERA REFERENCE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCameraMultibounce(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t SamplesPerPoint = 4096, Frames = 8, Bounces = 4;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.0625f, 0);
    const std::array<float,3> Environment{.025f,.025f,.025f};
    LightComputeSubmission Fresh(Driver), Filtered(Driver), OneBounce(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    OneBounce.Prepare(Current, Shaders, false, false, false, false, true);
    const auto Render = [&](LightComputeSubmission& Compute, const LightComputeArchive& Scene, uint32_t Seed,
                            bool Reset, bool Reconstruct, bool Camera, uint32_t Count)
    {
        return ExecuteReplayCameraPlane(Compute, Scene, Seed, false, false, Reset, Reconstruct, Camera,
                                        1, Count, Environment, false);
    };
    std::vector<std::array<float,4>> PriorColour, PriorHistory;
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        const uint32_t Seed = 910001 + 7919u * Frame;
        (void)Render(Fresh, Previous, Seed, Frame == 0, false, false, Bounces);
        PriorColour = Render(Filtered, Previous, Seed, Frame == 0, true, true, Bounces);
    }
    const auto PreviousGuides = Filtered.PrimaryReceivers();
    for (uint32_t Pixel = 0; Pixel < PriorColour.size(); ++Pixel)
        PriorHistory.push_back(ReplayTailSlot(Filtered, 5, Pixel));
    const auto Raw = Render(Fresh, Current, 990001, false, false, false, Bounces);
    const auto Result = Render(Filtered, Current, 990001, false, true, true, Bounces);
    const auto RawOneBounce = Render(OneBounce, Current, 990001, true, false, false, 1);
    const auto Guides = Filtered.PrimaryReceivers();
    uint32_t Accepted = 0, Rejected = 0, FloorAccepted = 0, AdditionalBouncePixels = 0;
    double RawSquared = 0, FilteredSquared = 0, ReferenceVariance = 0, RecurrenceError = 0;
    double ReferenceSum = 0, RawSum = 0, FilteredSum = 0, MaximumAdditional = 0;
    std::vector<double> ReferencePoint(Guides.size(), -1);
    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        const double Additional = double(Raw[Pixel][0]) - RawOneBounce[Pixel][0];
        Require(Additional >= -1e-6, "Additional diffuse bounces decreased the paired raw sample");
        if (Additional > 1e-6) ++AdditionalBouncePixels;
        MaximumAdditional = std::max(MaximumAdditional, Additional);
        const auto& Receiver = Guides[Pixel];
        if (ReplayTailSlot(Filtered, 6, Pixel)[3] < 1)
        {
            ++Rejected;
            Require(Raw[Pixel] == Result[Pixel], "Rejected multibounce history differed from raw");
            continue;
        }
        ++Accepted;
        const int32_t X = int32_t(std::floor(Receiver.MotionPixel[0] + .5f));
        const int32_t Y = int32_t(std::floor(Receiver.MotionPixel[1] + .5f));
        Require(X >= 0 && Y >= 0 && X < int32_t(Current.Width) && Y < int32_t(Current.Height),
                "Accepted multibounce history was outside the previous frame");
        const uint32_t Donor = uint32_t(Y) * Current.Width + uint32_t(X);
        const auto& Old = PreviousGuides[Donor];
        Require(Old.MotionPositionToken[3] == Receiver.MotionPositionToken[3], "Multibounce history crossed a token");
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Require(Old.PositionHit[Axis] == Receiver.PositionHit[Axis] &&
                        Old.NormalRoughness[Axis] == Receiver.NormalRoughness[Axis],
                    "Multibounce history crossed a different position or normal");
            const float Length = std::min(PriorHistory[Donor][3] + 1, 16.f);
            const float Expected = PriorColour[Donor][Axis] + (Raw[Pixel][Axis] - PriorColour[Donor][Axis]) / Length;
            RecurrenceError = std::max(RecurrenceError, std::abs(double(Result[Pixel][Axis]) - Expected));
        }
        if (Receiver.MotionPositionToken[3] != 1) continue;
        std::mt19937_64 Random(0x91f42a7bull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
        DirectGpuMoments Reference;
        for (uint32_t Sample = 0; Sample < SamplesPerPoint; ++Sample)
            Reference.Accumulate(ReplayReferencePath(Current, Receiver, Bounces, Random)[0]);
        const double Mean = Reference.Mean();
        const double CorrectedSe = Reference.StandardError() * std::sqrt(double(SamplesPerPoint) / (SamplesPerPoint - 1));
        ReferencePoint[Pixel] = Mean;
        RawSquared += std::pow(double(Raw[Pixel][0]) - Mean, 2);
        FilteredSquared += std::pow(double(Result[Pixel][0]) - Mean, 2);
        ReferenceVariance += CorrectedSe * CorrectedSe;
        ReferenceSum += Mean; RawSum += Raw[Pixel][0]; FilteredSum += Result[Pixel][0];
        ++FloorAccepted;
    }
    Require(FloorAccepted > 100 && Accepted == Filtered.ExtendedStatistics()[23] && Rejected > 100 &&
                !Filtered.ReplayLastReconstructionReset && RecurrenceError < 2e-6 && AdditionalBouncePixels > 5,
            "Four-bounce camera fixture did not exercise retained, rejected and indirect samples");
    const double RawRmse = std::sqrt(RawSquared / FloorAccepted);
    const double FilteredRmse = std::sqrt(FilteredSquared / FloorAccepted);
    const double ReferenceRmsSe = std::sqrt(ReferenceVariance / FloorAccepted);
    std::printf("CAMERA_MULTIBOUNCE bounces=%u cpu_paths=%u points=%u accepted=%u rejected=%u extra_pixels=%u "
                "max_extra=%.9g reference_mean=%.9g raw_mean=%.9g filtered_mean=%.9g "
                "raw_rmse=%.9g filtered_rmse=%.9g ratio=%.9g cpu_rms_se=%.9g recurrence_error=%.9g\n",
                Bounces, FloorAccepted * SamplesPerPoint, FloorAccepted, Accepted, Rejected, AdditionalBouncePixels,
                MaximumAdditional, ReferenceSum / FloorAccepted, RawSum / FloorAccepted, FilteredSum / FloorAccepted,
                RawRmse, FilteredRmse, FilteredRmse / RawRmse, ReferenceRmsSe, RecurrenceError);
    Require(FilteredRmse < .8 * RawRmse && ReferenceRmsSe < .02,
            "Four-bounce camera reference did not show bounded reconstruction agreement");
    constexpr uint32_t Batches = 16;
    std::mt19937_64 BatchSeeds(0x7f2b8a51ull);
    DirectGpuMoments BatchRaw, BatchFiltered;
    double BatchRawSquared = 0, BatchFilteredSquared = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        for (uint32_t Frame = 0; Frame < Frames; ++Frame)
        {
            const uint32_t Seed = uint32_t(BatchSeeds());
            (void)Render(Fresh, Previous, Seed, Frame == 0, false, false, Bounces);
            (void)Render(Filtered, Previous, Seed, Frame == 0, true, true, Bounces);
        }
        const uint32_t Seed = uint32_t(BatchSeeds());
        const auto BatchRawImage = Render(Fresh, Current, Seed, false, false, false, Bounces);
        const auto BatchFilteredImage = Render(Filtered, Current, Seed, false, true, true, Bounces);
        Require(Filtered.ExtendedStatistics()[23] == Accepted, "Multibounce camera acceptance changed across batches");
        double RawMean = 0, FilteredMean = 0;
        uint32_t Points = 0;
        for (uint32_t Pixel = 0; Pixel < ReferencePoint.size(); ++Pixel)
        {
            if (ReferencePoint[Pixel] < 0) continue;
            Require(ReplayTailSlot(Filtered, 6, Pixel)[3] >= 1, "Multibounce batch lost an exact donor");
            RawMean += BatchRawImage[Pixel][0];
            FilteredMean += BatchFilteredImage[Pixel][0];
            BatchRawSquared += std::pow(double(BatchRawImage[Pixel][0]) - ReferencePoint[Pixel], 2);
            BatchFilteredSquared += std::pow(double(BatchFilteredImage[Pixel][0]) - ReferencePoint[Pixel], 2);
            ++Points;
        }
        Require(Points == FloorAccepted, "Multibounce reference point count changed");
        BatchRaw.Accumulate(RawMean / Points);
        BatchFiltered.Accumulate(FilteredMean / Points);
    }
    const double FiniteBatchCorrection = std::sqrt(double(Batches) / (Batches - 1));
    const double RawBatchSe = BatchRaw.StandardError() * FiniteBatchCorrection;
    const double FilteredBatchSe = BatchFiltered.StandardError() * FiniteBatchCorrection;
    const double ReferenceMeanSe = std::sqrt(ReferenceVariance) / FloorAccepted;
    const double BatchRawRmse = std::sqrt(BatchRawSquared / (FloorAccepted * Batches));
    const double BatchFilteredRmse = std::sqrt(BatchFilteredSquared / (FloorAccepted * Batches));
    std::printf("CAMERA_MULTIBOUNCE_REPEAT batches=%u samples=%u reference=%.9g cpu_mean_se=%.9g "
                "raw=%.9g batch_se=%.9g filtered=%.9g batch_se=%.9g raw_rmse=%.9g filtered_rmse=%.9g ratio=%.9g\n",
                Batches, FloorAccepted * Batches, ReferenceSum / FloorAccepted, ReferenceMeanSe,
                BatchRaw.Mean(), RawBatchSe, BatchFiltered.Mean(), FilteredBatchSe,
                BatchRawRmse, BatchFilteredRmse, BatchFilteredRmse / BatchRawRmse);
    Require(std::abs(BatchRaw.Mean() - ReferenceSum / FloorAccepted) < 6 * std::hypot(RawBatchSe, ReferenceMeanSe) + .002 &&
                std::abs(BatchFiltered.Mean() - ReferenceSum / FloorAccepted) <
                    6 * std::hypot(FilteredBatchSe, ReferenceMeanSe) + .002 &&
                BatchFilteredRmse < .8 * BatchRawRmse,
            "Repeated four-bounce camera batches disagreed with the independent reference");
    for (uint32_t Frame = 0; Frame < 3; ++Frame)
    {
        (void)Render(Fresh, Previous, 995000 + Frame, Frame == 0, false, false, Bounces);
        (void)Render(Filtered, Previous, 995000 + Frame, Frame == 0, true, true, Bounces);
    }
    const auto Blocked = ReplayCameraPlane(.0625f, 0, .1f);
    const auto BlockedRaw = Render(Fresh, Blocked, 995100, false, false, false, Bounces);
    const auto BlockedResult = Render(Filtered, Blocked, 995100, false, true, true, Bounces);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(BlockedRaw, BlockedResult),
            "Moving camera and blocker retained multibounce colour");
    const auto Reopened = ReplayCameraPlane(.0625f, .0625f);
    const auto ReopenedRaw = Render(Fresh, Reopened, 995101, false, false, false, Bounces);
    const auto ReopenedResult = Render(Filtered, Reopened, 995101, false, true, true, Bounces);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(ReopenedRaw, ReopenedResult),
            "Reopening multibounce visibility retained colour from the blocked scene");
    std::printf("CAMERA_MULTIBOUNCE_MOTION blocker_and_camera_reset=1 reopen_reset=1 both_raw_exact=1\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CHANGE AND POLICY REJECTIONS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCameraExactGuides(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.0625f, 0);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    for (uint32_t Frame = 0; Frame < 3; ++Frame)
    {
        (void)ExecuteReplayCameraPlane(Fresh, Previous, 605000 + Frame, false, false, Frame == 0, false, false);
        (void)ExecuteReplayCameraPlane(Filtered, Previous, 605000 + Frame, false, false, Frame == 0, true, true);
    }
    const auto PreviousGuides = Filtered.PrimaryReceivers();
    const auto Raw = ExecuteReplayCameraPlane(Fresh, Current, 605100, false, false, false, false, false);
    const auto CurrentGuides = Fresh.PrimaryReceivers();
    std::vector<std::pair<uint32_t,uint32_t>> Matches;
    for (uint32_t Pixel = 0; Pixel < CurrentGuides.size() && Matches.size() < 5; ++Pixel)
    {
        const auto& R = CurrentGuides[Pixel];
        if (R.PositionHit[3] == 0 || R.MotionPixel[3] == 0) continue;
        const int32_t X = int32_t(std::floor(R.MotionPixel[0] + .5f));
        const int32_t Y = int32_t(std::floor(R.MotionPixel[1] + .5f));
        if (X < 0 || Y < 0 || X >= int32_t(Current.Width) || Y >= int32_t(Current.Height)) continue;
        const uint32_t Donor = uint32_t(Y) * Current.Width + uint32_t(X);
        const auto& Old = PreviousGuides[Donor];
        bool Exact = Old.MotionPositionToken[3] == R.MotionPositionToken[3];
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
            Exact = Exact && Old.PositionHit[Axis] == R.PositionHit[Axis] &&
                    Old.PositionHit[Axis] == R.MotionPositionToken[Axis] &&
                    Old.NormalRoughness[Axis] == R.NormalRoughness[Axis] &&
                    Old.NormalRoughness[Axis] == R.MotionNormal[Axis];
        for (const auto& Match : Matches) Exact = Exact && Match.second != Donor;
        if (Exact && ReplayTailSlot(Filtered, 5, Donor)[3] >= 1) Matches.emplace_back(Pixel, Donor);
    }
    Require(Matches.size() == 5, "Camera guide perturbation fixture lacked exact donors");
    auto* Records = static_cast<std::array<float,4>*>(Filtered.Buffers[6].Address);
    const size_t N = Current.Receivers.size(), Start = 25 * N + 13889 + SolarSourceCount;
    const std::array<size_t,5> Slots{1, 4, 7, 8, 10};
    for (size_t I = 0; I < Matches.size(); ++I) Records[Start + Slots[I] * N + Matches[I].second][0] += 1e-6f;
    const auto Result = ExecuteReplayCameraPlane(Filtered, Current, 605100, false, false, false, true, true);
    for (const auto& Match : Matches)
        Require(ReplayTailSlot(Filtered, 6, Match.first)[3] == 0 && Raw[Match.first] == Result[Match.first],
                "Near-but-not-exact camera guide retained colour");
    Require(!Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] > 100,
            "Exact-guide perturbation unexpectedly reset all colour");
    std::printf("CAMERA_EXACT_GUIDES near_mismatches=5 position_normal_material_optics_emission=1 "
                "rejected_raw_exact=1 unaffected_retained=%u\n", Filtered.ExtendedStatistics()[23]);
}

void ValidateReplayCameraReprojectionChanges(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    const auto Warm = [&]()
    {
        for (uint32_t Frame = 0; Frame < 3; ++Frame)
        {
            (void)ExecuteReplayCameraPlane(Fresh, Previous, 610000 + Frame, true, true, Frame == 0, false, false);
            (void)ExecuteReplayCameraPlane(Filtered, Previous, 610000 + Frame, true, true, Frame == 0, true, true);
        }
    };
    uint32_t ResetCases = 0;
    const auto VerifyReset = [&](const LightComputeArchive& Scene, const char* Label)
    {
        Warm();
        const auto Raw = ExecuteReplayCameraPlane(Fresh, Scene, 610100, true, true, false, false, false);
        const auto Result = ExecuteReplayCameraPlane(Filtered, Scene, 610100, true, true, false, true, true);
        const auto Counts = Filtered.ExtendedStatistics();
        Require(Filtered.ReplayLastReconstructionReset && Counts[23] == 0 && ReplayCandidateImagesEqual(Raw, Result),
                "Changed scene or invalid camera projection retained old colour");
        std::printf("CAMERA_RESET %s colour=%u raw_bit_exact=1\n", Label, Counts[23]);
        ++ResetCases;
    };
    VerifyReset(ReplayCameraPlane(.0625f, 0, .1f), "blocker_and_camera");
    VerifyReset(ReplayCameraPlane(.0625f, 0, 0, 4), "emission_and_camera");
    auto ChangedMaterial = ReplayCameraPlane(.0625f, 0);
    for (auto& Facet : ChangedMaterial.Surfaces)
        if (Facet.OpticalToken[3] == 1) Facet.AlbedoRoughness[0] = .2f;
    VerifyReset(ChangedMaterial, "material_and_camera");
    auto WrongPrevious = ReplayCameraPlane(.0625f, 0);
    WrongPrevious.Camera[4][0] = .2f;
    VerifyReset(WrongPrevious, "wrong_previous_camera");
    auto ChangedMetadata = ReplayCameraPlane(.0625f, 0);
    ChangedMetadata.Camera[8][0] = .5f;
    VerifyReset(ChangedMetadata, "changed_motion_metadata");
    auto ChangedBounds = ReplayCameraPlane(.0625f, 0);
    ChangedBounds.Visibility.Enclosures[0].Minimum[0] -= 1;
    VerifyReset(ChangedBounds, "changed_bvh_bounds");
    Warm();
    const auto Subpixel = ReplayCameraPlane(.03125f, 0);
    const auto Raw = ExecuteReplayCameraPlane(Fresh, Subpixel, 610101, true, true, false, false, false);
    const auto Result = ExecuteReplayCameraPlane(Filtered, Subpixel, 610101, true, true, false, true, true);
    Require(!Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(Raw, Result), "Subpixel camera motion retained a nearby but different point");
    uint32_t Rejected = 0;
    const auto Invalid = [&](bool Rough, bool Textured, bool Primary, bool Reconstruct)
    {
        try
        {
            (void)Filtered.ExecuteReplay(Previous, 1, 1, false, false, true, {0,0,0}, .01f, Primary,
                false, Rough, true, false, false, 64, false, Textured, false, false, false,
                false, false, Reconstruct, 1, false, true);
        }
        catch (const std::exception&) { ++Rejected; }
    };
    Invalid(true, false, true, true);
    Invalid(false, true, true, true);
    Invalid(false, false, false, true);
    Invalid(false, false, true, false);
    auto InvalidCamera = Previous;
    InvalidCamera.Camera[6][3] = 0;
    const auto PriorSequence = Filtered.HistorySequence;
    try { (void)ExecuteReplayCameraPlane(Filtered, InvalidCamera, 610102, true, true, false, true, true); }
    catch (const std::exception&) { ++Rejected; }
    Require(Filtered.HistorySequence == PriorSequence, "Invalid camera advanced submitted history");
    for (uint32_t Frame = 0; Frame < 3; ++Frame)
    {
        (void)ExecuteReplayCameraPlane(Fresh, Previous, 620000 + Frame, true, true, Frame == 0, false, false);
        (void)ExecuteReplayCameraPlane(Filtered, Previous, 620000 + Frame, true, true, Frame == 0, true, false);
    }
    const auto DefaultCamera = ReplayCameraPlane(.0625f, 0);
    const auto DefaultRaw = ExecuteReplayCameraPlane(Fresh, DefaultCamera, 620100, true, true, false, false, false);
    const auto DefaultResult = ExecuteReplayCameraPlane(Filtered, DefaultCamera, 620100, true, true, false, true, false);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(DefaultRaw, DefaultResult), "Default camera policy no longer resets colour");
    for (uint32_t Frame = 0; Frame < 2; ++Frame)
    {
        (void)ExecuteReplayCameraPlane(Fresh, Previous, 620200 + Frame, true, true, Frame == 0, false, false);
        (void)ExecuteReplayCameraPlane(Filtered, Previous, 620200 + Frame, true, true, Frame == 0, true, false);
    }
    const auto ToggleRaw = ExecuteReplayCameraPlane(Fresh, Previous, 620210, true, true, false, false, false);
    const auto ToggleResult = ExecuteReplayCameraPlane(Filtered, Previous, 620210, true, true, false, true, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 &&
                ReplayCandidateImagesEqual(ToggleRaw, ToggleResult), "Enabling camera reprojection retained old policy colour");
    Require(ResetCases == 6 && Rejected == 5, "Diffuse camera reprojection guard coverage is incomplete");
    std::printf("CAMERA_GUARDS reset_cases=%u subpixel_colour=0 subpixel_raw_bit_exact=1 invalid_requests=%u "
                "invalid_camera_sequence_unchanged=1 default_camera_reset=1 option_toggle_reset=1\n", ResetCases, Rejected);
}

void ValidateReplayCameraReprojectionAll(const char* Driver, const std::string& Shaders)
{
    ValidateReplayCameraReprojection(Driver, Shaders, false, false, 1);
    ValidateReplayCameraReprojection(Driver, Shaders, true, true, 4);
    ValidateReplayCameraMultibounce(Driver, Shaders);
    ValidateReplayCameraExactGuides(Driver, Shaders);
    ValidateReplayCameraReprojectionChanges(Driver, Shaders);
    std::printf("PASS strict diffuse camera reprojection, independent area integral and change guards\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            CAMERA DIAGNOSTIC CAPTURE
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplayCameraReprojection(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64, Frames = 12;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(2.f / Size, 0, 0, 2, Size);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        const uint32_t Seed = 720001 + 7919u * Frame;
        (void)ExecuteReplayCameraPlane(Fresh, Previous, Seed, false, false, Frame == 0, false, false);
        (void)ExecuteReplayCameraPlane(Filtered, Previous, Seed, false, false, Frame == 0, true, true);
    }
    const auto Raw = ExecuteReplayCameraPlane(Fresh, Current, 830001, false, false, false, false, false);
    const auto Result = ExecuteReplayCameraPlane(Filtered, Current, 830001, false, false, false, true, true);
    std::vector<std::array<float,4>> Mask(Result.size());
    uint32_t Accepted = 0, Rejected = 0;
    for (uint32_t Pixel = 0; Pixel < Result.size(); ++Pixel)
    {
        if (ReplayTailSlot(Filtered, 6, Pixel)[3] >= 1)
        {
            Mask[Pixel] = {.1f, 1, .1f, 1};
            ++Accepted;
        }
        else
        {
            Require(std::memcmp(Raw[Pixel].data(), Result[Pixel].data(), sizeof(Result[Pixel])) == 0,
                    "Camera capture rejected pixel differed from raw output");
            Mask[Pixel] = {.5f, .02f, .02f, 1};
            ++Rejected;
        }
    }
    Require(Accepted == Filtered.ExtendedStatistics()[23] && Accepted > 100 && Rejected > 100,
            "Camera capture did not exercise both acceptance and rejection");
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/ReplayCameraFresh.ppm", Raw, Size, Size, 3);
    WriteImage(Directory + "/ReplayCameraReprojected.ppm", Result, Size, Size, 3);
    WriteImage(Directory + "/ReplayCameraAcceptance.ppm", Mask, Size, Size, 3);
    std::printf("CAMERA_CAPTURE %ux%u prior_frames=%u accepted=%u rejected=%u rejected_raw_exact=1\n",
                Size, Size, Frames, Accepted, Rejected);
    std::printf("PASS diffuse camera readback and acceptance mask\n");
}
} // namespace
