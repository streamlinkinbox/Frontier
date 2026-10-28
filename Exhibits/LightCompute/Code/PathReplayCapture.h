/*==========================================================================================================================================
                                                   PATHREPLAYCAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture multiple synthetic diffuse scenes from Vulkan primary rays and replayed path reservoirs.
#pragma once

namespace
{
struct ReplayPatch
{
    std::array<SpatialVector,3> Corners{};
    std::array<float,3> Normal{}, Albedo{}, Radiance{};
    float Token = 0;
};
LightComputeArchive ReplayRoom(float BlockShift)
{
    LightComputeArchive Scene;
    Scene.Width = 256; Scene.Height = 160;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    std::vector<ReplayPatch> Patches;
    auto Quad = [&](SpatialVector A, SpatialVector B, SpatialVector C, SpatialVector D, std::array<float,3> Normal,
                    std::array<float,3> Albedo, std::array<float,3> Radiance, float Token)
    {
        Patches.push_back({{A,B,C},Normal,Albedo,Radiance,Token});
        Patches.push_back({{A,C,D},Normal,Albedo,Radiance,Token});
    };
    const std::array<float,3> White{.70f,.70f,.70f}, Red{.68f,.11f,.07f}, Blue{.09f,.18f,.65f},
                              Dark{.09f,.09f,.09f}, None{0,0,0};
    Quad({-3,0,0},{3,0,0},{3,6,0},{-3,6,0},{0,0,1},White,None,1);
    Quad({-3,6,0},{3,6,0},{3,6,4},{-3,6,4},{0,-1,0},White,None,2);
    Quad({-3,0,0},{-3,6,0},{-3,6,4},{-3,0,4},{1,0,0},Red,None,3);
    Quad({3,6,0},{3,0,0},{3,0,4},{3,6,4},{-1,0,0},Blue,None,4);
    Quad({-3,6,4},{3,6,4},{3,0,4},{-3,0,4},{0,0,-1},White,None,5);
    Quad({-1.0,1.4,3.98},{1.0,1.4,3.98},{1.0,4.6,3.98},{-1.0,4.6,3.98},
         {0,0,-1},None,{12,10,8},6);
    // The same two triangles translate from outside the room to below the ceiling source.
    const double Left = -1.35 + BlockShift, Right = 1.35 + BlockShift;
    Quad({Left,1.2,3.48},{Right,1.2,3.48},{Right,4.8,3.48},{Left,4.8,3.48},
         {0,0,-1},Dark,None,7);
    // A matte cuboid supplies non-coplanar indirect surfaces and visible contact regions.
    const double X0 = -.85, X1 = .30, Y0 = 2.0, Y1 = 3.45, Z = 1.22;
    Quad({X0,Y0,0},{X1,Y0,0},{X1,Y0,Z},{X0,Y0,Z},{0,-1,0},White,None,8);
    Quad({X1,Y1,0},{X0,Y1,0},{X0,Y1,Z},{X1,Y1,Z},{0,1,0},White,None,8);
    Quad({X0,Y1,0},{X0,Y0,0},{X0,Y0,Z},{X0,Y1,Z},{-1,0,0},White,None,8);
    Quad({X1,Y0,0},{X1,Y1,0},{X1,Y1,Z},{X1,Y0,Z},{1,0,0},White,None,8);
    Quad({X0,Y0,Z},{X1,Y0,Z},{X1,Y1,Z},{X0,Y1,Z},{0,0,1},White,None,8);
    std::vector<std::array<SpatialVector,3>> Geometry;
    for (const auto& P : Patches) Geometry.push_back(P.Corners);
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (size_t I = 0; I < Scene.Visibility.Corners.size(); ++I)
    {
        const auto& T = Scene.Visibility.Corners[I];
        const auto& P = Patches[Scene.Visibility.OriginalFacets[I]];
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {P.Normal[0],P.Normal[1],P.Normal[2],0};
        Facet.AlbedoRoughness = {P.Albedo[0],P.Albedo[1],P.Albedo[2],1};
        Facet.OpticalToken = {0,1.5f,0,P.Token};
        Facet.Emissive = {P.Radiance[0],P.Radiance[1],P.Radiance[2],0};
        Scene.Surfaces.push_back(Facet);
    }
    const float Pitch = -.0656f, Cosine = std::cos(Pitch), Sine = std::sin(Pitch), Vertical = .39f;
    Scene.Camera[0] = {0,-5,2.1f,.01f};
    Scene.Camera[1] = {0,Cosine,Sine,100};
    Scene.Camera[2] = {1,0,0,Vertical*float(Scene.Width)/float(Scene.Height)};
    Scene.Camera[3] = {0,-Sine,Cosine,Vertical};
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4+I] = Scene.Camera[I];
    Scene.Camera[8] = {0,0,1000,0}; // No rigid-motion mapping is claimed for these synthetic triangles.
    Scene.Camera[9] = Scene.Camera[8];
    Scene.Camera[10] = {0,0,0,1};
    Scene.Camera[11] = {1,0,1,0};
    return Scene;
}
struct ReplayCaptureResult
{
    std::vector<std::array<float,4>> Pixels, FirstPixels;
    std::vector<LightComputeReceiver> Guides, FirstGuides;
    double WholeMean = 0, FloorMean = 0;
    uint32_t Accepted = 0, SpatialLinks = 0, HistoryCap = 0;
    bool Reset = false;
};
ReplayCaptureResult CaptureReplaySequence(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                           uint32_t Hits, uint32_t FirstSeed, bool Reset, uint32_t Frames,
                                           bool SampleFacets = false, const LightComputeArchive* Following = nullptr,
                                           bool BalanceSampling = false, bool TexturedDiffuse = false,
                                           bool CaptureFirst = false, bool RoughOpaque = false,
                                           bool UniformFacetMixture = false, bool ImportanceRough = false)
{
    Require(Frames >= 2 && (!Following || (Following->Width == Scene.Width && Following->Height == Scene.Height &&
                Following->Receivers.size() == Scene.Receivers.size())), "Invalid replay capture sequence extent");
    std::vector<std::array<double,3>> Sum(Scene.Receivers.size());
    uint32_t Accumulated = 0;
    ReplayCaptureResult Result;
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        const auto& FrameScene = Frame > 0 && Following ? *Following : Scene;
        const auto Pixels = Compute.ExecuteReplay(FrameScene, Hits, FirstSeed + Frame * 7919u, true, true,
                                                   Reset && Frame == 0, {.025f,.025f,.025f}, .01f, true,
                                                   false, RoughOpaque, SampleFacets, false, false, 64, BalanceSampling,
                                                   TexturedDiffuse, ImportanceRough, UniformFacetMixture,
                                                   UniformFacetMixture);
        const auto Counters = Compute.ExtendedStatistics();
        if (Frame == 0)
        {
            Result.Accepted = Counters[20]; Result.SpatialLinks = Counters[22];
            Result.Reset = Compute.ReplayLastReset; Result.HistoryCap = Compute.ReplayLastHistoryCap;
            if (CaptureFirst)
            {
                Result.FirstPixels = Pixels;
                Result.FirstGuides = Compute.PrimaryReceivers();
            }
        }
        if (Frame >= Frames / 2)
        {
            ++Accumulated;
            for (size_t I = 0; I < Pixels.size(); ++I)
                for (uint32_t C = 0; C < 3; ++C)
                {
                    Require(std::isfinite(Pixels[I][C]) && Pixels[I][C] >= 0, "Invalid replay capture pixel");
                    Sum[I][C] += Pixels[I][C];
                }
        }
    }
    Result.Pixels.resize(Sum.size());
    uint64_t FloorCount = 0;
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
        {
            const size_t I = size_t(Y) * Scene.Width + X;
            for (uint32_t C = 0; C < 3; ++C) Result.Pixels[I][C] = float(Sum[I][C] / Accumulated);
            Result.Pixels[I][3] = 1;
            const double L = .2126 * Result.Pixels[I][0] + .7152 * Result.Pixels[I][1] + .0722 * Result.Pixels[I][2];
            Result.WholeMean += L;
            if (Y >= 118 && Y < 150 && X >= 75 && X < 190) { Result.FloorMean += L; ++FloorCount; }
        }
    Result.WholeMean /= Result.Pixels.size();
    Result.FloorMean /= FloorCount;
    Result.Guides.resize(Scene.Receivers.size());
    std::memcpy(Result.Guides.data(), Compute.Buffers[1].Address, Result.Guides.size() * sizeof(LightComputeReceiver));
    return Result;
}
// Small geometry-guided linear-RGB blur for display only. It is not used by any numerical validation.
std::vector<std::array<float,4>> FilterReplayPreview(const ReplayCaptureResult& Image, uint32_t Width, uint32_t Height,
                                                     bool RespectAlbedo = false)
{
    std::vector<std::array<float,4>> Filtered = Image.Pixels;
    constexpr int Radius = 4;
    constexpr double SigmaSquared = 4.0;
    for (uint32_t Y = 0; Y < Height; ++Y)
        for (uint32_t X = 0; X < Width; ++X)
        {
            const size_t Index = size_t(Y) * Width + X;
            const auto& Centre = Image.Guides[Index];
            if (Centre.PositionHit[3] == 0 || Centre.MotionPositionToken[3] <= 0) continue;
            std::array<double,3> Total{};
            double Sum = 0;
            for (int DY = -Radius; DY <= Radius; ++DY)
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    const int PX = int(X) + DX, PY = int(Y) + DY;
                    if (PX < 0 || PY < 0 || PX >= int(Width) || PY >= int(Height)) continue;
                    const size_t Near = size_t(PY) * Width + size_t(PX);
                    const auto& Other = Image.Guides[Near];
                    const double Dot = Centre.NormalRoughness[0]*Other.NormalRoughness[0] +
                                       Centre.NormalRoughness[1]*Other.NormalRoughness[1] +
                                       Centre.NormalRoughness[2]*Other.NormalRoughness[2];
                    if (Other.PositionHit[3] == 0 || Centre.MotionPositionToken[3] != Other.MotionPositionToken[3] ||
                        Dot < .99) continue;
                    if (RespectAlbedo && (std::abs(Centre.AlbedoIor[0]-Other.AlbedoIor[0]) > .12f ||
                                          std::abs(Centre.AlbedoIor[1]-Other.AlbedoIor[1]) > .12f ||
                                          std::abs(Centre.AlbedoIor[2]-Other.AlbedoIor[2]) > .12f)) continue;
                    const double Weight = std::exp(-double(DX*DX + DY*DY) / (2*SigmaSquared));
                    for (uint32_t C = 0; C < 3; ++C) Total[C] += Weight * Image.Pixels[Near][C];
                    Sum += Weight;
                }
            if (Sum > 0)
                for (uint32_t C = 0; C < 3; ++C) Filtered[Index][C] = float(Total[C] / Sum);
        }
    return Filtered;
}
void CaptureReplayRooms(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    const auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    LightComputeSubmission Compute(Driver);
    std::printf("ROOM preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Open.Width, Open.Height, Open.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    const uint32_t Frames = 48;
    const auto One = CaptureReplaySequence(Compute, Open, 1, 1000, true, Frames);
    std::printf("ROOM completed one-hit open sequence\n"); std::fflush(stdout);
    const auto Four = CaptureReplaySequence(Compute, Open, 4, 2000, true, Frames);
    std::printf("ROOM completed four-hit open sequence\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 3000, false, Frames);
    std::printf("ROOM completed four-hit blocked sequence\n"); std::fflush(stdout);
    const auto Reopened = CaptureReplaySequence(Compute, Open, 4, 4000, false, Frames);
    std::printf("ROOM completed four-hit reopened sequence\n"); std::fflush(stdout);
    Require(!Occluded.Reset && !Reopened.Reset && Occluded.Accepted > 0 && Reopened.Accepted > 0 &&
                Occluded.SpatialLinks > 0 && Reopened.SpatialLinks > 0 && Occluded.HistoryCap == 1 &&
                Reopened.HistoryCap == 1, "Moving-room capture did not retain and cap histories");
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/ReplayRoomOne.ppm", One.Pixels, Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomOpen.ppm", Four.Pixels, Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomBlocked.ppm", Occluded.Pixels, Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomReopened.ppm", Reopened.Pixels, Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomOnePreview.ppm", FilterReplayPreview(One, Open.Width, Open.Height), Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomOpenPreview.ppm", FilterReplayPreview(Four, Open.Width, Open.Height), Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomBlockedPreview.ppm", FilterReplayPreview(Occluded, Open.Width, Open.Height), Open.Width, Open.Height);
    WriteImage(Directory + "/ReplayRoomReopenedPreview.ppm", FilterReplayPreview(Reopened, Open.Width, Open.Height), Open.Width, Open.Height);
    std::printf("ROOM_CAPTURE frames_each=%u averaged_last=%u one_floor=%.9g open_floor=%.9g "
                "blocked_floor=%.9g reopened_floor=%.9g\n", Frames, Frames/2, One.FloorMean, Four.FloorMean,
                Occluded.FloorMean, Reopened.FloorMean);
    std::printf("ROOM_MOTION blocked_accepted=%u reopened_accepted=%u spatial_links=%u/%u reset=%u/%u cap=%u/%u\n",
                Occluded.Accepted, Reopened.Accepted, Occluded.SpatialLinks, Reopened.SpatialLinks,
                Occluded.Reset ? 1u : 0u, Reopened.Reset ? 1u : 0u, Occluded.HistoryCap, Reopened.HistoryCap);
    Require(Four.FloorMean > One.FloorMean && Four.FloorMean > Occluded.FloorMean &&
                Reopened.FloorMean > Occluded.FloorMean, "Rendered floor did not respond to path depth or moving blocker");
    std::printf("PASS four disclosed synthetic Vulkan readbacks; display exposure=.45 gamma=2.2\n");
}
} // namespace
