/*==========================================================================================================================================
                                                   REPLAYROOMREFERENCE.H
==========================================================================================================================================*/
// 🧩 Double-precision, brute-force, independent-RNG diffuse path reference for the authored room.
#pragma once

namespace
{
using ReplayReferenceVector = std::array<double,3>;
ReplayReferenceVector ReplayReferenceAdd(ReplayReferenceVector A, ReplayReferenceVector B)
{ return {A[0]+B[0], A[1]+B[1], A[2]+B[2]}; }
ReplayReferenceVector ReplayReferenceSub(ReplayReferenceVector A, ReplayReferenceVector B)
{ return {A[0]-B[0], A[1]-B[1], A[2]-B[2]}; }
ReplayReferenceVector ReplayReferenceScale(ReplayReferenceVector A, double S)
{ return {A[0]*S,A[1]*S,A[2]*S}; }
double ReplayReferenceDot(ReplayReferenceVector A, ReplayReferenceVector B)
{ return A[0]*B[0] + A[1]*B[1] + A[2]*B[2]; }
ReplayReferenceVector ReplayReferenceCross(ReplayReferenceVector A, ReplayReferenceVector B)
{ return {A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]}; }
ReplayReferenceVector ReplayReferenceUnit(ReplayReferenceVector A)
{ return ReplayReferenceScale(A, 1 / std::sqrt(ReplayReferenceDot(A,A))); }
ReplayReferenceVector ReplayReferenceXyz(const std::array<float,4>& P) { return {P[0],P[1],P[2]}; }
struct ReplayTextureReferenceSample
{
    ReplayReferenceVector Albedo{};
    uint32_t Mip = 0;
    bool Wrapped = false, NearMipBoundary = false;
};
// 📝 Independent double-precision UV, footprint, mip and periodic bilinear evaluation of a guarded atlas.
ReplayTextureReferenceSample ReplayReferenceTexture(const LightComputeArchive& Scene, const LightComputeFacet& F,
                                                    double BaryU, double BaryV, ReplayReferenceVector Position,
                                                    ReplayReferenceVector Normal, ReplayReferenceVector View)
{
    const auto& Atlas = Scene.Angles;
    const size_t MaterialOffset = size_t(F.Emissive[3]) - 1;
    const double Weight = 1 - BaryU - BaryV;
    const double U = (F.A[3]*Weight + F.B[3]*BaryU + F.C[3]*BaryV) * Atlas[MaterialOffset+4] + Atlas[MaterialOffset+6];
    const double V = (F.NormalA[3]*Weight + F.NormalB[3]*BaryU + F.NormalC[3]*BaryV) * Atlas[MaterialOffset+5] + Atlas[MaterialOffset+7];
    const auto Eye = ReplayReferenceXyz(Scene.Camera[0]);
    const auto Delta = ReplayReferenceSub(Position, Eye);
    const double Footprint = 2 * std::sqrt(ReplayReferenceDot(Delta,Delta)) * Scene.Camera[3][3] /
        (Scene.Height * std::max(.05, ReplayReferenceDot(Normal,View))) *
        std::max(std::abs(double(Atlas[MaterialOffset+4])), std::abs(double(Atlas[MaterialOffset+5])));
    size_t Cursor = size_t(Atlas[MaterialOffset+2]);
    uint32_t Size = uint32_t(Atlas[Cursor]);
    Cursor += 4;
    ReplayTextureReferenceSample Result;
    Result.Wrapped = U < 0 || U >= 1 || V < 0 || V >= 1;
    while (Size > 1)
    {
        if (std::abs(Footprint * Size - 1) < 1e-5) Result.NearMipBoundary = true;
        if (Footprint * Size <= 1) break;
        Cursor += size_t(Size) * Size * 3;
        Size /= 2;
        ++Result.Mip;
    }
    const double X = (U - std::floor(U)) * Size - .5, Y = (V - std::floor(V)) * Size - .5;
    const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
    const double Fx = X - X0, Fy = Y - Y0;
    auto Texel = [&](int Px, int Py, uint32_t C)
    {
        const auto Wrap = [Size](int P) { return uint32_t((P % int(Size) + int(Size)) % int(Size)); };
        return double(Atlas[Cursor + (size_t(Wrap(Py)) * Size + Wrap(Px)) * 3 + C]);
    };
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double Top = Texel(X0,Y0,C) * (1-Fx) + Texel(X0+1,Y0,C) * Fx;
        const double Bottom = Texel(X0,Y0+1,C) * (1-Fx) + Texel(X0+1,Y0+1,C) * Fx;
        Result.Albedo[C] = Atlas[MaterialOffset+8+C] * (Top * (1-Fy) + Bottom * Fy);
    }
    return Result;
}
std::array<double,3> RoughReplayBrdf(const LightComputeReceiver& R, ReplayReferenceVector Incoming)
{
    const auto N = ReplayReferenceUnit(ReplayReferenceXyz(R.NormalRoughness));
    const auto V = ReplayReferenceUnit(ReplayReferenceXyz(R.ViewMetallic));
    const double NV = std::max(0.0, ReplayReferenceDot(N,V)), NL = std::max(0.0, ReplayReferenceDot(N,Incoming));
    if (NV <= 0 || NL <= 0) return {};
    const auto H = ReplayReferenceUnit(ReplayReferenceAdd(V,Incoming));
    const double NH = std::clamp(ReplayReferenceDot(N,H), 0.0, 1.0);
    const double VH = std::clamp(ReplayReferenceDot(V,H), 0.0, 1.0);
    const double Rough = R.NormalRoughness[3], Alpha = Rough*Rough, Alpha2 = Alpha*Alpha;
    const double Denom = NH*NH*(Alpha2-1)+1, D = Alpha2/(3.14159265358979323846*Denom*Denom);
    const double Visibility = .5 / (NL*std::sqrt(NV*NV*(1-Alpha2)+Alpha2) +
                                     NV*std::sqrt(NL*NL*(1-Alpha2)+Alpha2));
    const double Ior = R.AlbedoIor[3], Ratio = (Ior-1)/(Ior+1), Metallic = R.ViewMetallic[3];
    const bool Authored = R.ExpectedDirect[3] > 0;
    // 📝 Authored strata use fixed specular level .5 and separate entrance/exit diffuse Fresnel factors.
    const double Dielectric = Ratio*Ratio*(Authored ? 1 : R.ResidualSpecular[3]*2);
    std::array<double,3> Value{};
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double Base = R.AlbedoIor[C], F0 = Dielectric*(1-Metallic) + Base*Metallic;
        const double Fresnel = F0 + (1-F0)*std::pow(1-VH,5);
        const double Entrance = F0 + (1-F0)*std::pow(1-NL,5);
        const double Exit = F0 + (1-F0)*std::pow(1-NV,5);
        const double Diffuse = Authored ? (1-Entrance)*(1-Exit) : 1-Fresnel;
        Value[C] = D*Visibility*Fresnel + Diffuse*Base*(1-Metallic)/3.14159265358979323846;
    }
    return Value;
}
ReplayReferenceVector ReplayReferencePath(const LightComputeArchive& Scene, const LightComputeReceiver& Receiver,
                                           uint32_t Hits, std::mt19937_64& Random, bool RoughOpaque = false,
                                           bool TexturedDiffuse = false)
{
    ReplayReferenceVector Normal = ReplayReferenceUnit(ReplayReferenceXyz(Receiver.NormalRoughness));
    ReplayReferenceVector Origin = ReplayReferenceAdd(ReplayReferenceXyz(Receiver.PositionHit),
                                                       ReplayReferenceScale(Normal, .001));
    ReplayReferenceVector Throughput{1,1,1}, Result{};
    LightComputeReceiver Current = Receiver;
    for (uint32_t Bounce = 0; Bounce < Hits; ++Bounce)
    {
        const double U = std::generate_canonical<double,53>(Random), V = std::generate_canonical<double,53>(Random);
        const double R = std::sqrt(U), Phi = 6.2831853071795864769 * V;
        // The orthonormal frame and random generator are independent of the Slang implementation.
        const ReplayReferenceVector Helper = std::abs(Normal[0]) < .75 ? ReplayReferenceVector{1,0,0}
                                                                        : ReplayReferenceVector{0,1,0};
        const auto Tangent = ReplayReferenceUnit(ReplayReferenceCross(Helper, Normal));
        const auto Bitangent = ReplayReferenceCross(Normal, Tangent);
        auto Direction = ReplayReferenceAdd(ReplayReferenceScale(Tangent, R*std::cos(Phi)),
                                            ReplayReferenceScale(Bitangent, R*std::sin(Phi)));
        Direction = ReplayReferenceUnit(ReplayReferenceAdd(Direction, ReplayReferenceScale(Normal, std::sqrt(1-U))));
        const auto Scatter = RoughOpaque ? RoughReplayBrdf(Current, Direction) : ReplayReferenceXyz(Current.AlbedoIor);
        for (uint32_t C = 0; C < 3; ++C)
            Throughput[C] *= Scatter[C] * (RoughOpaque ? 3.14159265358979323846 : 1);
        if (std::max({Throughput[0],Throughput[1],Throughput[2]}) <= 0) break;
        double Distance = 1e30, HitU = 0, HitV = 0;
        size_t Hit = Scene.Surfaces.size();
        // Brute-force Möller–Trumbore, not the GPU's packed BVH traversal.
        for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
        {
            const auto& F = Scene.Surfaces[Index];
            const auto A = ReplayReferenceXyz(F.A), E1 = ReplayReferenceSub(ReplayReferenceXyz(F.B), A),
                       E2 = ReplayReferenceSub(ReplayReferenceXyz(F.C), A);
            const auto P = ReplayReferenceCross(Direction, E2);
            const double Det = ReplayReferenceDot(E1, P);
            if (std::abs(Det) < 1e-12) continue;
            const auto S = ReplayReferenceSub(Origin, A);
            const double Inverse = 1 / Det, BaryU = ReplayReferenceDot(S, P) * Inverse;
            if (BaryU < 0 || BaryU > 1) continue;
            const auto Q = ReplayReferenceCross(S, E1);
            const double BaryV = ReplayReferenceDot(Direction, Q) * Inverse;
            if (BaryV < 0 || BaryU + BaryV > 1) continue;
            const double T = ReplayReferenceDot(E2, Q) * Inverse;
            if (T > .0001 && T < Distance) { Distance = T; Hit = Index; HitU = BaryU; HitV = BaryV; }
        }
        if (Hit == Scene.Surfaces.size())
        {
            for (uint32_t C = 0; C < 3; ++C) Result[C] += Throughput[C] * .025;
            break;
        }
        const auto& F = Scene.Surfaces[Hit];
        for (uint32_t C = 0; C < 3; ++C) Result[C] += Throughput[C] * F.Emissive[C];
        const auto N = ReplayReferenceAdd(ReplayReferenceAdd(ReplayReferenceScale(ReplayReferenceXyz(F.NormalA), 1-HitU-HitV),
                                                              ReplayReferenceScale(ReplayReferenceXyz(F.NormalB), HitU)),
                                           ReplayReferenceScale(ReplayReferenceXyz(F.NormalC), HitV));
        Normal = ReplayReferenceUnit(N);
        if (ReplayReferenceDot(Normal, Direction) > 0) Normal = ReplayReferenceScale(Normal, -1);
        const auto StrikePosition = ReplayReferenceAdd(Origin, ReplayReferenceScale(Direction, Distance));
        Origin = ReplayReferenceAdd(StrikePosition, ReplayReferenceScale(Normal, .001));
        Current = {};
        Current.NormalRoughness = {float(Normal[0]),float(Normal[1]),float(Normal[2]),F.AlbedoRoughness[3]};
        Current.ViewMetallic = {float(-Direction[0]),float(-Direction[1]),float(-Direction[2]),F.OpticalToken[0]};
        Current.AlbedoIor = {F.AlbedoRoughness[0],F.AlbedoRoughness[1],F.AlbedoRoughness[2],F.OpticalToken[1]};
        Current.ResidualSpecular[3] = F.OpticalToken[2];
        if (TexturedDiffuse && F.Emissive[3] > 0)
        {
            const auto Sample = ReplayReferenceTexture(Scene, F, HitU, HitV, StrikePosition, Normal,
                                                         ReplayReferenceScale(Direction, -1));
            const size_t Offset = size_t(F.Emissive[3]) - 1;
            for (uint32_t C = 0; C < 3; ++C) Current.AlbedoIor[C] = float(Sample.Albedo[C]);
            Current.NormalRoughness[3] = Scene.Angles[Offset + 11];
            Current.ViewMetallic[3] = Scene.Angles[Offset + 12];
            Current.AlbedoIor[3] = Scene.Angles[Offset + 13];
            Current.ExpectedDirect[3] = F.Emissive[3];
        }
    }
    return Result;
}
using ReplayRgbMoments = std::array<DirectGpuMoments,3>;
ReplayRgbMoments ReplayReferenceIntegrate(const LightComputeArchive& Scene, uint32_t Hits, uint64_t Seed,
                                            bool RoughOpaque = false, bool TexturedDiffuse = false)
{
    ReplayRgbMoments Result;
    std::mt19937_64 Random(Seed);
    const auto& Receiver = Scene.Receivers[size_t(Scene.Width) + 1];
    for (uint32_t Sample = 0; Sample < 400000; ++Sample)
    {
        const auto Radiance = ReplayReferencePath(Scene, Receiver, Hits, Random, RoughOpaque, TexturedDiffuse);
        for (uint32_t C = 0; C < 3; ++C) Result[C].Accumulate(Radiance[C]);
    }
    return Result;
}
void ReplayAccumulateRgb(ReplayRgbMoments& Moments, const std::vector<std::array<float,4>>& Image, uint32_t Width)
{
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
        {
            const auto& P = Image[size_t(3*Y+1)*Width + 3*X+1];
            for (uint32_t C = 0; C < 3; ++C) Moments[C].Accumulate(P[C]);
        }
}
struct ReplayGroupedMoments
{
    ReplayRgbMoments Pixels, Batches;
    void Accumulate(const std::vector<std::array<float,4>>& Image, uint32_t Width)
    {
        ReplayRgbMoments Local;
        ReplayAccumulateRgb(Local, Image, Width);
        for (uint32_t C = 0; C < 3; ++C)
        {
            Pixels[C].Sum += Local[C].Sum; Pixels[C].Squares += Local[C].Squares; Pixels[C].Count += Local[C].Count;
            Batches[C].Accumulate(Local[C].Mean());
        }
    }
};
void ReplayCompareRgb(const char* Name, const ReplayRgbMoments& Reference, const ReplayGroupedMoments& Actual)
{
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double PixelSe = Actual.Pixels[C].StandardError();
        // 📝 Use sample, not population, variance for the finite set of independent batch means.
        const double BatchSe = Actual.Batches[C].Count > 1 ? Actual.Batches[C].StandardError() *
            std::sqrt(double(Actual.Batches[C].Count) / (Actual.Batches[C].Count - 1)) : 0;
        const double CombinedSe = std::hypot(Reference[C].StandardError(), std::max(PixelSe, BatchSe));
        std::printf("ROOM_REFERENCE %s channel=%u cpu=%.9g cpu_se=%.9g vulkan=%.9g pixel_se=%.9g "
                    "batch_se=%.9g n=%u/%u batches=%u\n", Name, C, Reference[C].Mean(), Reference[C].StandardError(),
                    Actual.Pixels[C].Mean(), PixelSe, BatchSe, Reference[C].Count, Actual.Pixels[C].Count,
                    Actual.Batches[C].Count);
        Require(std::abs(Reference[C].Mean()-Actual.Pixels[C].Mean()) <
                    6*CombinedSe + .01*std::abs(Reference[C].Mean()) + 1e-4,
                "Room replay disagrees with independent brute-force Monte Carlo reference");
    }
}
void ValidateReplayRoomReference(const char* Driver, const std::string& Shaders)
{
    auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    auto Receivers = DirectGpuFixture(false);
    for (auto& R : Receivers.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.PositionHit = {1.4f,2.0f,0,1};
            R.MotionPositionToken = {1.4f,2.0f,0,1};
            R.AlbedoIor = {.7f,.7f,.7f,1.5f};
        }
    Open.Width = Blocked.Width = Receivers.Width;
    Open.Height = Blocked.Height = Receivers.Height;
    Open.Receivers = Blocked.Receivers = Receivers.Receivers;
    std::printf("ROOM_REFERENCE tracing three 400000-path CPU baselines against %zu facets\n", Open.Surfaces.size());
    std::fflush(stdout);
    const auto CpuOne = ReplayReferenceIntegrate(Open, 1, 314159);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 271828);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 161803);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    ReplayGroupedMoments One, Four, Stale, FreshBlocked;
    uint32_t Accepted = 0, Links = 0;
    const std::array<float,3> Environment{.025f,.025f,.025f};
    for (uint32_t Batch = 0; Batch < 16; ++Batch)
    {
        const uint32_t Seed = 7000 + Batch*100;
        One.Accumulate(Compute.ExecuteReplay(Open, 1, Seed, false, false, true, Environment), Open.Width);
        Four.Accumulate(Compute.ExecuteReplay(Open, 4, Seed+1, false, false, true, Environment), Open.Width);
        FreshBlocked.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+2, false, false, true, Environment), Open.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Open, 4, Seed+10+Frame, true, false, Frame == 0, Environment);
        Stale.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+19, true, true, false, Environment), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Room-reference transition unexpectedly discarded or retained long history");
    }
    ReplayCompareRgb("open_one", CpuOne, One);
    ReplayCompareRgb("open_four", CpuOpen, Four);
    ReplayCompareRgb("blocked_fresh_four", CpuBlocked, FreshBlocked);
    ReplayCompareRgb("blocked_reused_four", CpuBlocked, Stale);
    Require(Accepted == 16*5120 && Links > 0, "Room-reference reused paths were not exercised");
    std::printf("ROOM_REFERENCE accepted=%u spatial_links=%u independent_cpu_rng=mt19937_64 brute_force=1\n", Accepted, Links);
    std::printf("PASS bounded room transport against independent Monte Carlo; no production or material-general claim\n");
}
} // namespace
