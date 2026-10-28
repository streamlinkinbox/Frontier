/*==========================================================================================================================================
                                             REPLAYTEXTUREMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Independent primary-ray and multibounce references for textured rigid-motion replay.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                  TEXTURED MOTION FIXTURE
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ReplayTexturedMotionRigid(float Shift, float Angle, uint32_t Palette, float Blocker,
                                               uint32_t Width = 32, uint32_t Height = 32)
{
    auto Scene = ReplayMotionRigid(Shift, Angle, Width, Height, Blocker);
    const auto Original = ReplayCheckerPixels(Palette);
    std::vector<ReplayTextureColour> Pixels(64);
    for (uint32_t Y = 0; Y < 8; ++Y)
        for (uint32_t X = 0; X < 8; ++X)
            Pixels[size_t(Y) * 8 + X] = Original[((X / 4 + Y / 4) & 1) ? 2 : 0];
    ReplayInstallTexture(Scene, Pixels, 8);
    Scene.Angles[4] = Scene.Angles[5] = 1.2f;
    Scene.Angles[6] = .21f;
    Scene.Angles[7] = -.37f;
    Scene.Angles[8] = Scene.Angles[9] = Scene.Angles[10] = .8f;
    const float Cosine = std::cos(Angle), Sine = std::sin(Angle);
    for (auto& F : Scene.Surfaces)
    {
        if (F.OpticalToken[3] != 1 && F.OpticalToken[3] != 8) continue;
        F.Emissive[3] = 1;
        auto Coordinate = [&](std::array<float,4>& P, std::array<float,4>& N)
        {
            float X = P[0], Y = P[1];
            if (F.OpticalToken[3] == 8)
            {
                const float Translated = X - .8f * Angle;
                X = Cosine * Translated + Sine * Y;
                Y = -Sine * Translated + Cosine * Y;
            }
            P[3] = .36f * X + .10f * Y - .25f;
            N[3] = .30f * Y + .33f * P[2] - .4f;
        };
        Coordinate(F.A, F.NormalA);
        Coordinate(F.B, F.NormalB);
        Coordinate(F.C, F.NormalC);
    }
    return Scene;
}

void ValidateReplayRigidTextureCoordinates(const LightComputeArchive& Previous, const LightComputeArchive& Current,
                                            float Angle)
{
    const double Cosine = std::cos(double(Angle)), Sine = std::sin(double(Angle));
    double Maximum = 0;
    uint32_t Matched = 0;
    for (const auto& F : Current.Surfaces)
    {
        if (F.OpticalToken[3] != 8) continue;
        const std::array<const std::array<float,4>*,3> Positions{&F.A, &F.B, &F.C};
        const std::array<const std::array<float,4>*,3> Normals{&F.NormalA, &F.NormalB, &F.NormalC};
        for (size_t I = 0; I < Positions.size(); ++I)
        {
            const auto& P = *Positions[I];
            const double X = Cosine * (P[0] - .8 * Angle) + Sine * P[1];
            const double Y = -Sine * (P[0] - .8 * Angle) + Cosine * P[1];
            bool Found = false;
            for (const auto& Old : Previous.Surfaces)
            {
                if (Old.OpticalToken[3] != 8) continue;
                const std::array<const std::array<float,4>*,3> OldPositions{&Old.A, &Old.B, &Old.C};
                const std::array<const std::array<float,4>*,3> OldNormals{&Old.NormalA, &Old.NormalB, &Old.NormalC};
                for (size_t J = 0; J < OldPositions.size(); ++J)
                {
                    const auto& Q = *OldPositions[J];
                    if (std::abs(X - Q[0]) > 2e-6 || std::abs(Y - Q[1]) > 2e-6 ||
                        std::abs(double(P[2]) - Q[2]) > 2e-6) continue;
                    Maximum = std::max(Maximum, std::abs(double(P[3]) - Q[3]));
                    Maximum = std::max(Maximum, std::abs(double((*Normals[I])[3]) - (*OldNormals[J])[3]));
                    Found = true;
                    break;
                }
                if (Found) break;
            }
            Require(Found, "Moved textured vertex did not map to the previous rigid object");
            ++Matched;
        }
    }
    std::printf("TEXTURE_MOTION_UV vertices=%u maximum_error=%.9g\n", Matched, Maximum);
    Require(Matched == 30 && Maximum < 2e-6, "Texture coordinates did not follow the rotated object");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INDEPENDENT PRIMARY RAYS
//------------------------------------------------------------------------------------------------------------------------

struct ReplayPrimaryReference
{
    LightComputeReceiver Receiver{};
    size_t Facet = 0;
    double Distance = 0, BaryU = 0, BaryV = 0;
    uint32_t Mip = UINT32_MAX;
    bool Hit = false, NearMipBoundary = false;
};

ReplayPrimaryReference TraceReplayPrimaryReference(const LightComputeArchive& Scene, uint32_t X, uint32_t Y)
{
    const auto Eye = ReplayReferenceXyz(Scene.Camera[0]);
    const auto Forward = ReplayReferenceXyz(Scene.Camera[1]);
    const auto Right = ReplayReferenceXyz(Scene.Camera[2]);
    const auto Up = ReplayReferenceXyz(Scene.Camera[3]);
    const double ScreenX = (double(X) + .5) / Scene.Width * 2 - 1;
    const double ScreenY = 1 - (double(Y) + .5) / Scene.Height * 2;
    const auto Direction = ReplayReferenceUnit(ReplayReferenceAdd(Forward,
        ReplayReferenceAdd(ReplayReferenceScale(Right, ScreenX * Scene.Camera[2][3]),
                           ReplayReferenceScale(Up, ScreenY * Scene.Camera[3][3]))));
    const double Projection = ReplayReferenceDot(Direction, Forward);
    const double Near = Scene.Camera[0][3] / Projection, Far = Scene.Camera[1][3] / Projection;
    ReplayPrimaryReference Result;
    Result.Distance = Far;
    for (size_t I = 0; I < Scene.Surfaces.size(); ++I)
    {
        const auto& F = Scene.Surfaces[I];
        const auto A = ReplayReferenceXyz(F.A);
        const auto E1 = ReplayReferenceSub(ReplayReferenceXyz(F.B), A);
        const auto E2 = ReplayReferenceSub(ReplayReferenceXyz(F.C), A);
        const auto P = ReplayReferenceCross(Direction, E2);
        const double Determinant = ReplayReferenceDot(E1, P);
        if (std::abs(Determinant) < 1e-12) continue;
        const auto S = ReplayReferenceSub(Eye, A);
        const double Inverse = 1 / Determinant, U = ReplayReferenceDot(S, P) * Inverse;
        if (U < 0 || U > 1) continue;
        const auto Q = ReplayReferenceCross(S, E1);
        const double V = ReplayReferenceDot(Direction, Q) * Inverse;
        if (V < 0 || U + V > 1) continue;
        const double Distance = ReplayReferenceDot(E2, Q) * Inverse;
        if (Distance > Near && Distance < Result.Distance)
        {
            Result.Facet = I;
            Result.Distance = Distance;
            Result.BaryU = U;
            Result.BaryV = V;
            Result.Hit = true;
        }
    }
    if (!Result.Hit) return Result;
    const auto& F = Scene.Surfaces[Result.Facet];
    auto Normal = ReplayReferenceUnit(ReplayReferenceAdd(
        ReplayReferenceAdd(ReplayReferenceScale(ReplayReferenceXyz(F.NormalA), 1 - Result.BaryU - Result.BaryV),
                           ReplayReferenceScale(ReplayReferenceXyz(F.NormalB), Result.BaryU)),
        ReplayReferenceScale(ReplayReferenceXyz(F.NormalC), Result.BaryV)));
    if (ReplayReferenceDot(Normal, Direction) > 0) Normal = ReplayReferenceScale(Normal, -1);
    const auto Position = ReplayReferenceAdd(Eye, ReplayReferenceScale(Direction, Result.Distance));
    auto& R = Result.Receiver;
    R.PositionHit = {float(Position[0]), float(Position[1]), float(Position[2]), 1};
    R.NormalRoughness = {float(Normal[0]), float(Normal[1]), float(Normal[2]), F.AlbedoRoughness[3]};
    R.ViewMetallic = {float(-Direction[0]), float(-Direction[1]), float(-Direction[2]), F.OpticalToken[0]};
    R.AlbedoIor = {F.AlbedoRoughness[0], F.AlbedoRoughness[1], F.AlbedoRoughness[2], F.OpticalToken[1]};
    R.ResidualSpecular = {F.Emissive[0], F.Emissive[1], F.Emissive[2], F.OpticalToken[2]};
    R.MotionPositionToken[3] = F.OpticalToken[3];
    if (F.Emissive[3] > 0)
    {
        const auto Sample = ReplayReferenceTexture(Scene, F, Result.BaryU, Result.BaryV, Position, Normal,
                                                    ReplayReferenceScale(Direction, -1));
        const size_t Offset = size_t(F.Emissive[3]) - 1;
        for (uint32_t C = 0; C < 3; ++C) R.AlbedoIor[C] = float(Sample.Albedo[C]);
        R.NormalRoughness[3] = Scene.Angles[Offset + 11];
        R.ViewMetallic[3] = Scene.Angles[Offset + 12];
        R.AlbedoIor[3] = Scene.Angles[Offset + 13];
        R.ExpectedDirect[3] = F.Emissive[3];
        Result.Mip = Sample.Mip;
        Result.NearMipBoundary = Sample.NearMipBoundary;
    }
    return Result;
}

std::vector<ReplayPrimaryReference> ValidateReplayPrimaryReference(
    const LightComputeArchive& Scene, const std::vector<LightComputeReceiver>& Actual, const char* Name)
{
    std::vector<ReplayPrimaryReference> Result(Actual.size());
    uint32_t Hits = 0, Textured = 0, MipBoundaries = 0;
    std::array<uint32_t,4> Mips{};
    double PositionError = 0, NormalError = 0, AlbedoError = 0;
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
        {
            const size_t I = size_t(Y) * Scene.Width + X;
            const auto Reference = TraceReplayPrimaryReference(Scene, X, Y);
            Result[I] = Reference;
            const auto& R = Actual[I];
            Require(Reference.Hit == (R.PositionHit[3] != 0), "Primary hit coverage differs from independent rays");
            if (!Reference.Hit) continue;
            ++Hits;
            Require(Reference.Receiver.MotionPositionToken[3] == R.MotionPositionToken[3],
                    "Primary surface identity differs from independent rays");
            if (Scene.Surfaces[Reference.Facet].Emissive[3] > 0)
            {
                ++Textured;
                Require(Reference.Mip < Mips.size(), "Unexpected texture mip in motion reference");
                ++Mips[Reference.Mip];
            }
            if (Reference.NearMipBoundary) ++MipBoundaries;
            for (uint32_t C = 0; C < 3; ++C)
            {
                PositionError = std::max(PositionError, std::abs(double(Reference.Receiver.PositionHit[C]) - R.PositionHit[C]));
                NormalError = std::max(NormalError, std::abs(double(Reference.Receiver.NormalRoughness[C]) - R.NormalRoughness[C]));
                if (!Reference.NearMipBoundary)
                    AlbedoError = std::max(AlbedoError, std::abs(double(Reference.Receiver.AlbedoIor[C]) - R.AlbedoIor[C]));
            }
        }
    std::printf("TEXTURE_MOTION_PRIMARY %s hits=%u textured=%u mips=%u/%u/%u/%u mip_boundaries=%u "
                "position_error=%.9g normal_error=%.9g albedo_error=%.9g\n", Name, Hits, Textured,
                Mips[0], Mips[1], Mips[2], Mips[3], MipBoundaries, PositionError, NormalError, AlbedoError);
    Require(Hits > 2000 && Textured > 400 && Mips[0] + Mips[1] + Mips[2] > 100 &&
                PositionError < 5e-4 && NormalError < 1e-5 && AlbedoError < 2e-4,
            "Independent primary-ray or texture comparison failed");
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  MOTION AND PATH COMPARISON
//------------------------------------------------------------------------------------------------------------------------

uint32_t SelectReplayMotionProbe(const LightComputeArchive& Scene, const std::vector<LightComputeReceiver>& Previous,
                                  const std::vector<LightComputeReceiver>& Current,
                                  const std::vector<ReplayPrimaryReference>& Reference, float Token,
                                  uint32_t TargetX, uint32_t TargetY)
{
    uint32_t Best = UINT32_MAX, Distance = UINT32_MAX;
    const auto Palette = ReplayCheckerPixels(1);
    for (uint32_t Y = 1; Y + 1 < Scene.Height; ++Y)
        for (uint32_t X = 1; X + 1 < Scene.Width; ++X)
        {
            const uint32_t I = Y * Scene.Width + X;
            const auto& R = Current[I];
            if (R.MotionPositionToken[3] != Token || R.MotionPixel[3] == 0 || Reference[I].NearMipBoundary ||
                Reference[I].Mip > 2 ||
                std::min({Reference[I].BaryU, Reference[I].BaryV, 1 - Reference[I].BaryU - Reference[I].BaryV}) < .01)
                continue;
            double Contrast = 0;
            for (uint32_t C = 0; C < 3; ++C)
                Contrast = std::max(Contrast, std::abs(double(Reference[I].Receiver.AlbedoIor[C]) -
                                    .4 * (Palette[0][C] + Palette[2][C])));
            if (Contrast < .015) continue;
            const int Px = int(std::floor(R.MotionPixel[0] + .5f));
            const int Py = int(std::floor(R.MotionPixel[1] + .5f));
            if (Px < 0 || Py < 0 || Px >= int(Scene.Width) || Py >= int(Scene.Height)) continue;
            const auto& P = Previous[size_t(Py) * Scene.Width + size_t(Px)];
            if (P.MotionPositionToken[3] != Token) continue;
            double Separation = 0, Dot = 0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                Separation += std::pow(double(P.PositionHit[C]) - R.MotionPositionToken[C], 2);
                Dot += double(P.NormalRoughness[C]) * R.MotionNormal[C];
            }
            const double Radius = std::min(.25, double(R.MotionPixel[2]));
            if (Separation > Radius * Radius || Dot < .9) continue;
            const uint32_t Gap = uint32_t(std::abs(int(X) - int(TargetX)) + std::abs(int(Y) - int(TargetY)));
            if (Gap < Distance) { Best = I; Distance = Gap; }
        }
    Require(Best != UINT32_MAX, "No compatible textured rigid-motion probe was found");
    std::printf("TEXTURE_MOTION_PROBE token=%.0f pixel=%u,%u mip=%u albedo=%.6g,%.6g,%.6g "
                "previous_pixel=%.4f,%.4f\n", Token, Best % Scene.Width, Best / Scene.Width, Reference[Best].Mip,
                Reference[Best].Receiver.AlbedoIor[0], Reference[Best].Receiver.AlbedoIor[1],
                Reference[Best].Receiver.AlbedoIor[2], Current[Best].MotionPixel[0], Current[Best].MotionPixel[1]);
    return Best;
}

ReplayRgbMoments TraceReplayMotionReference(const LightComputeArchive& Scene, const ReplayPrimaryReference& Primary,
                                             uint32_t Hits, uint64_t Seed, uint32_t Count)
{
    ReplayRgbMoments Result;
    std::mt19937_64 Random(Seed);
    for (uint32_t I = 0; I < Count; ++I)
    {
        const auto Radiance = ReplayReferencePath(Scene, Primary.Receiver, Hits, Random, false, true);
        for (uint32_t C = 0; C < 3; ++C)
            Result[C].Accumulate(Radiance[C] + Primary.Receiver.ResidualSpecular[C]);
    }
    return Result;
}

double ReplaySampleStandardError(const DirectGpuMoments& Moments)
{
    Require(Moments.Count > 1, "At least two samples are required for sample variance");
    return Moments.StandardError() * std::sqrt(double(Moments.Count) / (Moments.Count - 1));
}

void CompareReplayMotionProbe(const char* Name, const ReplayRgbMoments& Reference, const ReplayRgbMoments& Actual)
{
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double CpuSe = ReplaySampleStandardError(Reference[C]);
        const double GpuSe = ReplaySampleStandardError(Actual[C]);
        const double Tolerance = 6 * std::hypot(CpuSe, GpuSe) + .01 * std::abs(Reference[C].Mean()) + 1e-4;
        std::printf("TEXTURE_MOTION_REFERENCE %s channel=%u cpu=%.9g cpu_se=%.9g vulkan=%.9g batch_se=%.9g "
                    "cpu_paths=%u gpu_batches=%u\n", Name, C, Reference[C].Mean(), CpuSe, Actual[C].Mean(), GpuSe,
                    Reference[C].Count, Actual[C].Count);
        Require(std::abs(Reference[C].Mean() - Actual[C].Mean()) < Tolerance,
                "Moving textured pixel disagrees with independent brute-force paths");
    }
}

void ValidateReplayTextureMotion(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 64, Height = 64, CpuPaths = 240000, Batches = 128;
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    auto Covered = ReplayTexturedMotionRigid(.07f, .12f, 1, 0, Width, Height);
    SettleReplayMotion(Covered);
    ValidateReplayRigidTextureCoordinates(Previous, Current, .12f);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    const std::array<float,3> Environment{.025f,.025f,.025f};
    (void)TraceTexturedReplay(Compute, Previous, 1, 170001, false, false, true, true, false, false, Environment);
    const auto PreviousGuides = Compute.PrimaryReceivers();
    (void)ValidateReplayPrimaryReference(Previous, PreviousGuides, "previous");
    (void)TraceTexturedReplay(Compute, Current, 1, 170002, false, false, true, true, false, false, Environment);
    const auto CurrentGuides = Compute.PrimaryReceivers();
    const auto CurrentReference = ValidateReplayPrimaryReference(Current, CurrentGuides, "moved");
    (void)TraceTexturedReplay(Compute, Covered, 1, 170003, false, false, true, true, false, false, Environment);
    const auto CoveredReference = ValidateReplayPrimaryReference(Covered, Compute.PrimaryReceivers(), "blocked");
    const std::array<uint32_t,2> Pixels{
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 8, 28, 38),
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 1, 46, 50)};
    std::array<ReplayRgbMoments,2> CpuOpen, CpuBlocked, GpuFresh, GpuMoved, GpuBlocked;
    std::printf("TEXTURE_MOTION_REFERENCE tracing %u CPU paths for each of four primary pixels\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        CpuOpen[I] = TraceReplayMotionReference(Current, CurrentReference[Pixels[I]], 4, 171001 + I, CpuPaths);
        CpuBlocked[I] = TraceReplayMotionReference(Covered, CoveredReference[Pixels[I]], 4, 172001 + I, CpuPaths);
    }
    uint64_t MovedLinks = 0, BlockedLinks = 0, SpatialLinks = 0;
    uint32_t MovedProbeAccepts = 0, BlockedProbeAccepts = 0;
    auto RepresentedCount = [&](uint32_t Pixel)
    {
        // 📝 Slot two is the pre-spatial current reservoir; its fourth component counts represented candidates.
        const size_t Start = size_t(Width) * Height * 27 + 13889 + SolarSourceCount;
        std::array<float,4> Packed{};
        std::memcpy(Packed.data(), static_cast<const char*>(Compute.Buffers[6].Address) + (Start + Pixel) * 16, 16);
        return Packed[3];
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 173000 + 101 * Batch;
        const auto Fresh = TraceTexturedReplay(Compute, Current, 4, Seed, false, true, true,
                                                true, true, true, Environment);
        for (uint32_t I = 0; I < Pixels.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C) GpuFresh[I][C].Accumulate(Fresh[Pixels[I]][C]);
        for (uint32_t Frame = 0; Frame < 3; ++Frame)
            (void)TraceTexturedReplay(Compute, Previous, 4, Seed + 10 + Frame, true, true, Frame == 0,
                                      true, true, true, Environment);
        const auto Moved = TraceTexturedReplay(Compute, Current, 4, Seed + 20, true, true, false,
                                               true, true, true, Environment);
        auto Counts = Compute.ExtendedStatistics();
        MovedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Textured rigid transition did not retain capped history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            MovedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) GpuMoved[I][C].Accumulate(Moved[Pixels[I]][C]);
        }
        const auto Blocked = TraceTexturedReplay(Compute, Covered, 4, Seed + 21, true, true, false,
                                                 true, true, true, Environment);
        Counts = Compute.ExtendedStatistics();
        BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Textured blocker transition did not retain capped history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            BlockedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) GpuBlocked[I][C].Accumulate(Blocked[Pixels[I]][C]);
        }
    }
    bool DetectBlocked = false;
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        const char* Name = I == 0 ? "object" : "floor";
        const std::string FreshName = std::string(Name) + "_fresh";
        const std::string MovedName = std::string(Name) + "_moved_reused";
        const std::string BlockedName = std::string(Name) + "_blocked_reused";
        CompareReplayMotionProbe(FreshName.c_str(), CpuOpen[I], GpuFresh[I]);
        CompareReplayMotionProbe(MovedName.c_str(), CpuOpen[I], GpuMoved[I]);
        CompareReplayMotionProbe(BlockedName.c_str(), CpuBlocked[I], GpuBlocked[I]);
        for (uint32_t C = 0; C < 3; ++C)
            DetectBlocked |= CpuOpen[I][C].Mean() - CpuBlocked[I][C].Mean() >
                6 * std::hypot(ReplaySampleStandardError(CpuOpen[I][C]), ReplaySampleStandardError(CpuBlocked[I][C]));
    }
    std::printf("TEXTURE_MOTION_LINKS moved=%llu blocked=%llu spatial=%llu moved_probe_accepts=%u "
                "blocked_probe_accepts=%u batches=%u\n", (unsigned long long)MovedLinks,
                (unsigned long long)BlockedLinks, (unsigned long long)SpatialLinks, MovedProbeAccepts,
                BlockedProbeAccepts, Batches);
    Require(DetectBlocked && MovedProbeAccepts == 2 * Batches && BlockedProbeAccepts == 2 * Batches &&
                MovedLinks > Batches * 500 && BlockedLinks > Batches * 500 && SpatialLinks > 0,
            "Motion probes did not retain history or the independent blocker control was insensitive");
    std::printf("PASS independent primary and four-hit textured paths during camera, rigid, atlas and blocker changes\n");
}
} // namespace
