//============================================================================================================================================
//                                                      VISIBILITYRASTER.CPP
//============================================================================================================================================
// 🧩 The visibility buffer, rasterized on the CPU. Pass one keeps the nearest triangle per pixel; the shadow
//    pass keeps the nearest depth per texel from each light tap; the shade pass spends the two buffers with no
//    ray query anywhere. Taps are fixed and stratified (deterministic across runs); a 2x2 percentage-closer
//    gather per tap keeps the penumbra edges the four taps would otherwise band smooth instead of dithered.

#include "VisibilityRaster.h"

#include "SceneStructure.h"

#include <cmath>
#include <cstring>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kSky[3] = { 0.30f, 0.42f, 0.63f };

} // namespace

bool VisibilityRaster::Render(const SceneStructure& Level,
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3], float FovYRadians,
                              uint32_t Width, uint32_t Height,
                              unsigned char* Rgba, double& MeanLum) noexcept
{
    const auto& Flat = Level.QueryFlatTriangles();
    if (Flat.empty() || Width == 0u || Height == 0u || Rgba == nullptr)
        return false;
    if (!(FovYRadians > 0.0f) || !(FovYRadians < kPi))
        return false;

    CollectLumi(Level);
    PlaceTaps();

    const size_t Cells = static_cast<size_t>(Width) * static_cast<size_t>(Height);
    Depth_.assign(Cells, 1e30f);
    TriId_.assign(Cells, kMiss);
    Bary_.assign(Cells * 2u, 0.0f);
    TriN_.assign(Flat.size() * 3u, 0.0f);
    Shadow_.assign(static_cast<size_t>(kShadowSize) * static_cast<size_t>(kShadowSize), 1e30f);
    Acc_.assign(Cells * 3u, 0.0f);

    RasterizePrimary(Level, Eye, Forward, Right, Up, FovYRadians, Width, Height);
    Shade(Level, Eye, Forward, Right, Up, FovYRadians, Width, Height, Rgba, MeanLum);
    return true;
}

void VisibilityRaster::CollectLumi(const SceneStructure& Level) noexcept
{
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();
    Lumi_.clear();

    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[T].MaterialSlot, sizeof(Slot));
        if (Slot >= Records.size())
            continue;
        const MaterialRecord& R = Records[Slot];
        if (R.EmissiveR + R.EmissiveG + R.EmissiveB <= 0.0f)
            continue;
        LumiTri L{};
        L.A[0] = Flat[T].VertexAlphaX; L.A[1] = Flat[T].VertexAlphaY; L.A[2] = Flat[T].VertexAlphaZ;
        L.B[0] = Flat[T].VertexBetaX;  L.B[1] = Flat[T].VertexBetaY;  L.B[2] = Flat[T].VertexBetaZ;
        L.C[0] = Flat[T].VertexGammaX; L.C[1] = Flat[T].VertexGammaY; L.C[2] = Flat[T].VertexGammaZ;
        const float Ux = L.B[0] - L.A[0], Uy = L.B[1] - L.A[1], Uz = L.B[2] - L.A[2];
        const float Vx = L.C[0] - L.A[0], Vy = L.C[1] - L.A[1], Vz = L.C[2] - L.A[2];
        float Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
        const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (Len <= 0.0f)
            continue;
        L.N[0] = Nx / Len; L.N[1] = Ny / Len; L.N[2] = Nz / Len;
        L.Area  = Len * 0.5f;
        L.Le[0] = R.EmissiveR; L.Le[1] = R.EmissiveG; L.Le[2] = R.EmissiveB;
        Lumi_.push_back(L);
    }
}

void VisibilityRaster::PlaceTaps() noexcept
{
    TapCount_ = 0u;
    if (Lumi_.empty())
        return;
    // Fixed stratified warp points: the same four taps every render, spread over the luminaire area.
    constexpr float kWarp[4][2] = { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
    for (uint32_t K = 0u; K < kLightTaps; ++K)
    {
        const LumiTri& L = Lumi_[K % Lumi_.size()];
        const float Sq = std::sqrt(kWarp[K][0]);
        LightTap& Tap = Taps_[K];
        Tap.P[0] = L.A[0] + (L.B[0] - L.A[0]) * (1.0f - Sq) + (L.C[0] - L.A[0]) * (Sq * kWarp[K][1]);
        Tap.P[1] = L.A[1] + (L.B[1] - L.A[1]) * (1.0f - Sq) + (L.C[1] - L.A[1]) * (Sq * kWarp[K][1]);
        Tap.P[2] = L.A[2] + (L.B[2] - L.A[2]) * (1.0f - Sq) + (L.C[2] - L.A[2]) * (Sq * kWarp[K][1]);
        Tap.N[0] = L.N[0]; Tap.N[1] = L.N[1]; Tap.N[2] = L.N[2];
        Tap.Le[0] = L.Le[0]; Tap.Le[1] = L.Le[1]; Tap.Le[2] = L.Le[2];
        Tap.Weight = L.Area * static_cast<float>(Lumi_.size()) / static_cast<float>(kLightTaps);
    }
    TapCount_ = kLightTaps;
}

void VisibilityRaster::RasterizePrimary(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                                        const float Right[3], const float Up[3], float FovYRadians,
                                        uint32_t Width, uint32_t Height) noexcept
{
    const auto& Flat = Level.QueryFlatTriangles();
    const float HalfH = std::tan(FovYRadians * 0.5f);
    const float HalfW = HalfH * static_cast<float>(Width) / static_cast<float>(Height);

    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        const float Ax = Flat[T].VertexAlphaX, Ay = Flat[T].VertexAlphaY, Az = Flat[T].VertexAlphaZ;
        const float Bx = Flat[T].VertexBetaX,  By = Flat[T].VertexBetaY,  Bz = Flat[T].VertexBetaZ;
        const float Cx = Flat[T].VertexGammaX, Cy = Flat[T].VertexGammaY, Cz = Flat[T].VertexGammaZ;
        float Nx = (By - Ay) * (Cz - Az) - (Bz - Az) * (Cy - Ay);
        float Ny = (Bz - Az) * (Cx - Ax) - (Bx - Ax) * (Cz - Az);
        float Nz = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
        const float Nl = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (Nl <= 0.0f)
            continue;
        TriN_[T * 3u] = Nx / Nl; TriN_[T * 3u + 1u] = Ny / Nl; TriN_[T * 3u + 2u] = Nz / Nl;

        const float Vx[3] = { Ax, Bx, Cx }, Vy[3] = { Ay, By, Cy }, Vz[3] = { Az, Bz, Cz };
        float Sx[3], Sy[3], InvZ[3];
        bool Near = false;
        for (int K = 0; K < 3; ++K)
        {
            const float Rx = Vx[K] - Eye[0], Ry = Vy[K] - Eye[1], Rz = Vz[K] - Eye[2];
            const float Xc = Rx * Right[0] + Ry * Right[1] + Rz * Right[2];
            const float Yc = Rx * Up[0]    + Ry * Up[1]    + Rz * Up[2];
            const float Zc = Rx * Forward[0] + Ry * Forward[1] + Rz * Forward[2];
            if (Zc < kNear)
            {
                Near = true;
                break;
            }
            InvZ[K] = 1.0f / Zc;
            Sx[K] = (Xc * InvZ[K] / HalfW * 0.5f + 0.5f) * static_cast<float>(Width);
            Sy[K] = (1.0f - (Yc * InvZ[K] / HalfH * 0.5f + 0.5f)) * static_cast<float>(Height);
        }
        if (Near)
            continue;

        float Area = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Sx[2], Sy[2]);
        if (Area == 0.0f)
            continue;
        // A swapped triangle keeps vertex zero in slot zero while slots one and two trade places, so the
        // second stored weight must be read back from the swapped slot or the shade point lands mirrored.
        bool Swapped = false;
        if (Area < 0.0f)
        {
            const float Tx = Sx[1]; Sx[1] = Sx[2]; Sx[2] = Tx;
            const float Ty = Sy[1]; Sy[1] = Sy[2]; Sy[2] = Ty;
            const float Tz = InvZ[1]; InvZ[1] = InvZ[2]; InvZ[2] = Tz;
            Area = -Area;
            Swapped = true;
        }
        int LoX = static_cast<int>(std::floor(Sx[0] < Sx[1] ? (Sx[0] < Sx[2] ? Sx[0] : Sx[2]) : (Sx[1] < Sx[2] ? Sx[1] : Sx[2])));
        int HiX = static_cast<int>(std::ceil(Sx[0] > Sx[1] ? (Sx[0] > Sx[2] ? Sx[0] : Sx[2]) : (Sx[1] > Sx[2] ? Sx[1] : Sx[2])));
        int LoY = static_cast<int>(std::floor(Sy[0] < Sy[1] ? (Sy[0] < Sy[2] ? Sy[0] : Sy[2]) : (Sy[1] < Sy[2] ? Sy[1] : Sy[2])));
        int HiY = static_cast<int>(std::ceil(Sy[0] > Sy[1] ? (Sy[0] > Sy[2] ? Sy[0] : Sy[2]) : (Sy[1] > Sy[2] ? Sy[1] : Sy[2])));
        if (LoX < 0) LoX = 0; if (HiX > static_cast<int>(Width)) HiX = static_cast<int>(Width);
        if (LoY < 0) LoY = 0; if (HiY > static_cast<int>(Height)) HiY = static_cast<int>(Height);
        const float InverseArea = 1.0f / Area;

        for (int Y = LoY; Y < HiY; ++Y)
        {
            for (int X = LoX; X < HiX; ++X)
            {
                const float Px = static_cast<float>(X) + 0.5f;
                const float Py = static_cast<float>(Y) + 0.5f;
                const float W0 = EdgeWeight(Sx[1], Sy[1], Sx[2], Sy[2], Px, Py) * InverseArea;
                const float W1 = EdgeWeight(Sx[2], Sy[2], Sx[0], Sy[0], Px, Py) * InverseArea;
                const float W2 = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Px, Py) * InverseArea;
                if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                    continue;
                const float Z = 1.0f / (W0 * InvZ[0] + W1 * InvZ[1] + W2 * InvZ[2]);
                const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X);
                if (Z >= Depth_[Idx])
                    continue;
                Depth_[Idx] = Z;
                TriId_[Idx] = static_cast<uint32_t>(T);
                Bary_[Idx * 2u] = W0;
                Bary_[Idx * 2u + 1u] = Swapped ? W2 : W1;
            }
        }
    }
}

void VisibilityRaster::RasterizeShadow(const SceneStructure& Level, const float Tap[3], const float Centre[3]) noexcept
{
    float Dx = Centre[0] - Tap[0], Dy = Centre[1] - Tap[1], Dz = Centre[2] - Tap[2];
    const float Dl = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    if (Dl <= 0.0f)
    {
        Dx = 0.0f; Dy = 0.0f; Dz = -1.0f;
    }
    else
    {
        Dx /= Dl; Dy /= Dl; Dz /= Dl;
    }
    ShadowD_[0] = Dx; ShadowD_[1] = Dy; ShadowD_[2] = Dz;
    float Ux = 0.0f, Uy = 0.0f, Uz = 1.0f;
    if (Dx * Dx + Dy * Dy < 0.01f)
    {
        Ux = 0.0f; Uy = 1.0f; Uz = 0.0f;
    }
    float Rx = Dy * Uz - Dz * Uy, Ry = Dz * Ux - Dx * Uz, Rz = Dx * Uy - Dy * Ux;
    const float Rl = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
    Rx /= Rl; Ry /= Rl; Rz /= Rl;
    ShadowR_[0] = Rx; ShadowR_[1] = Ry; ShadowR_[2] = Rz;
    ShadowU_[0] = Ry * Dz - Rz * Dy; ShadowU_[1] = Rz * Dx - Rx * Dz; ShadowU_[2] = Rx * Dy - Ry * Dx;
    ShadowTan_ = std::tan(kShadowHalf * kPi / 180.0f);

    std::fill(Shadow_.begin(), Shadow_.end(), 1e30f);
    const auto& Flat = Level.QueryFlatTriangles();
    constexpr float kLightNear = 0.02f;

    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        const float Vx[3] = { Flat[T].VertexAlphaX, Flat[T].VertexBetaX, Flat[T].VertexGammaX };
        const float Vy[3] = { Flat[T].VertexAlphaY, Flat[T].VertexBetaY, Flat[T].VertexGammaY };
        const float Vz[3] = { Flat[T].VertexAlphaZ, Flat[T].VertexBetaZ, Flat[T].VertexGammaZ };
        float Sx[3], Sy[3], InvZ[3];
        bool Behind = false;
        for (int K = 0; K < 3; ++K)
        {
            const float Ox = Vx[K] - Tap[0], Oy = Vy[K] - Tap[1], Oz = Vz[K] - Tap[2];
            const float Xc = Ox * ShadowR_[0] + Oy * ShadowR_[1] + Oz * ShadowR_[2];
            const float Yc = Ox * ShadowU_[0] + Oy * ShadowU_[1] + Oz * ShadowU_[2];
            const float Zc = Ox * ShadowD_[0] + Oy * ShadowD_[1] + Oz * ShadowD_[2];
            if (Zc < kLightNear)
            {
                Behind = true;
                break;
            }
            InvZ[K] = 1.0f / Zc;
            Sx[K] = (Xc * InvZ[K] / ShadowTan_ * 0.5f + 0.5f) * static_cast<float>(kShadowSize);
            Sy[K] = (1.0f - (Yc * InvZ[K] / ShadowTan_ * 0.5f + 0.5f)) * static_cast<float>(kShadowSize);
        }
        if (Behind)
            continue;

        float Area = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Sx[2], Sy[2]);
        if (Area == 0.0f)
            continue;
        if (Area < 0.0f)
        {
            const float Tx = Sx[1]; Sx[1] = Sx[2]; Sx[2] = Tx;
            const float Ty = Sy[1]; Sy[1] = Sy[2]; Sy[2] = Ty;
            const float Tz = InvZ[1]; InvZ[1] = InvZ[2]; InvZ[2] = Tz;
            Area = -Area;
        }
        int LoX = static_cast<int>(std::floor(Sx[0] < Sx[1] ? (Sx[0] < Sx[2] ? Sx[0] : Sx[2]) : (Sx[1] < Sx[2] ? Sx[1] : Sx[2])));
        int HiX = static_cast<int>(std::ceil(Sx[0] > Sx[1] ? (Sx[0] > Sx[2] ? Sx[0] : Sx[2]) : (Sx[1] > Sx[2] ? Sx[1] : Sx[2])));
        int LoY = static_cast<int>(std::floor(Sy[0] < Sy[1] ? (Sy[0] < Sy[2] ? Sy[0] : Sy[2]) : (Sy[1] < Sy[2] ? Sy[1] : Sy[2])));
        int HiY = static_cast<int>(std::ceil(Sy[0] > Sy[1] ? (Sy[0] > Sy[2] ? Sy[0] : Sy[2]) : (Sy[1] > Sy[2] ? Sy[1] : Sy[2])));
        if (LoX < 0) LoX = 0; if (HiX > static_cast<int>(kShadowSize)) HiX = static_cast<int>(kShadowSize);
        if (LoY < 0) LoY = 0; if (HiY > static_cast<int>(kShadowSize)) HiY = static_cast<int>(kShadowSize);
        const float InverseArea = 1.0f / Area;

        for (int Y = LoY; Y < HiY; ++Y)
        {
            for (int X = LoX; X < HiX; ++X)
            {
                const float Px = static_cast<float>(X) + 0.5f;
                const float Py = static_cast<float>(Y) + 0.5f;
                const float W0 = EdgeWeight(Sx[1], Sy[1], Sx[2], Sy[2], Px, Py) * InverseArea;
                const float W1 = EdgeWeight(Sx[2], Sy[2], Sx[0], Sy[0], Px, Py) * InverseArea;
                const float W2 = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Px, Py) * InverseArea;
                if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                    continue;
                const float Z = 1.0f / (W0 * InvZ[0] + W1 * InvZ[1] + W2 * InvZ[2]);
                const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(kShadowSize) + static_cast<size_t>(X);
                if (Z < Shadow_[Idx])
                    Shadow_[Idx] = Z;
            }
        }
    }
}

float VisibilityRaster::Shadow(const float P[3], const float N[3], float NdotL, const float Tap[3]) const noexcept
{
    // The sample leaves from the surface point nudged one normal offset outward; without that nudge a
    // grazing wall lands inside its own map texels and peppers itself with acne. The 2x2 gather returns a
    // lit fraction, so the penumbra arrives as a smooth ramp instead of binary dither.
    constexpr float kNormalOffset = 0.02f; // [m] along the geometric normal, ahead of the map test
    const float Qx = P[0] + N[0] * kNormalOffset;
    const float Qy = P[1] + N[1] * kNormalOffset;
    const float Qz = P[2] + N[2] * kNormalOffset;
    const float Ox = Qx - Tap[0], Oy = Qy - Tap[1], Oz = Qz - Tap[2];
    const float Xc = Ox * ShadowR_[0] + Oy * ShadowR_[1] + Oz * ShadowR_[2];
    const float Yc = Ox * ShadowU_[0] + Oy * ShadowU_[1] + Oz * ShadowU_[2];
    const float Zc = Ox * ShadowD_[0] + Oy * ShadowD_[1] + Oz * ShadowD_[2];
    if (Zc < 0.02f)
        return 1.0f;
    // Outside the frustum there is no occluder information: lit, by the usual convention.
    const float U = Xc / (Zc * ShadowTan_) * 0.5f + 0.5f;
    const float V = 1.0f - (Yc / (Zc * ShadowTan_) * 0.5f + 0.5f);
    if (U < 0.0f || U >= 1.0f || V < 0.0f || V >= 1.0f)
        return 1.0f;
    const float TexU = U * static_cast<float>(kShadowSize) - 0.5f;
    const float TexV = V * static_cast<float>(kShadowSize) - 0.5f;
    const int32_t X0 = static_cast<int32_t>(TexU);
    const int32_t Y0 = static_cast<int32_t>(TexV);
    const float Fx = TexU - static_cast<float>(X0);
    const float Fy = TexV - static_cast<float>(Y0);
    const float Bias = kShadowBias * (1.0f + (1.0f - NdotL));
    float LitSum = 0.0f, LitWeight = 0.0f;
    for (int Dy = 0; Dy < 2; ++Dy)
    {
        for (int Dx = 0; Dx < 2; ++Dx)
        {
            const int32_t Xi = X0 + Dx, Yi = Y0 + Dy;
            if (Xi < 0 || Yi < 0 || Xi >= static_cast<int32_t>(kShadowSize) ||
                Yi >= static_cast<int32_t>(kShadowSize))
                continue;
            const size_t Idx = static_cast<size_t>(Yi) * static_cast<size_t>(kShadowSize)
                + static_cast<size_t>(Xi);
            const float Wx = Dx == 0 ? 1.0f - Fx : Fx;
            const float Wy = Dy == 0 ? 1.0f - Fy : Fy;
            LitWeight += Wx * Wy;
            if (!(Shadow_[Idx] + Bias < Zc))
                LitSum += Wx * Wy;
        }
    }
    if (LitWeight <= 0.0f)
        return 1.0f;
    return LitSum / LitWeight;
}

void VisibilityRaster::Shade(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                             const float Right[3], const float Up[3], float FovYRadians,
                             uint32_t Width, uint32_t Height, unsigned char* Rgba, double& MeanLum) noexcept
{
    (void)Forward; (void)Right; (void)Up; (void)FovYRadians;
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();
    const size_t Cells = static_cast<size_t>(Width) * static_cast<size_t>(Height);

    float MinB[3] = { 1e30f, 1e30f, 1e30f }, MaxB[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        const float Vx[3] = { Flat[T].VertexAlphaX, Flat[T].VertexBetaX, Flat[T].VertexGammaX };
        const float Vy[3] = { Flat[T].VertexAlphaY, Flat[T].VertexBetaY, Flat[T].VertexGammaY };
        const float Vz[3] = { Flat[T].VertexAlphaZ, Flat[T].VertexBetaZ, Flat[T].VertexGammaZ };
        for (int K = 0; K < 3; ++K)
        {
            if (Vx[K] < MinB[0]) MinB[0] = Vx[K]; if (Vx[K] > MaxB[0]) MaxB[0] = Vx[K];
            if (Vy[K] < MinB[1]) MinB[1] = Vy[K]; if (Vy[K] > MaxB[1]) MaxB[1] = Vy[K];
            if (Vz[K] < MinB[2]) MinB[2] = Vz[K]; if (Vz[K] > MaxB[2]) MaxB[2] = Vz[K];
        }
    }
    const float Centre[3] = { (MinB[0] + MaxB[0]) * 0.5f, (MinB[1] + MaxB[1]) * 0.5f, (MinB[2] + MaxB[2]) * 0.5f };

    // Seed pass: sky where nothing won, ambient where something did, emission where it glows.
    for (size_t Idx = 0u; Idx < Cells; ++Idx)
    {
        float* Out = &Acc_[Idx * 3u];
        if (TriId_[Idx] == kMiss)
        {
            Out[0] = kSky[0]; Out[1] = kSky[1]; Out[2] = kSky[2];
            continue;
        }
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[TriId_[Idx]].MaterialSlot, sizeof(Slot));
        if (Slot >= Records.size())
        {
            Out[0] = kSky[0]; Out[1] = kSky[1]; Out[2] = kSky[2];
            TriId_[Idx] = kMiss;
            continue;
        }
        const MaterialRecord& R = Records[Slot];
        if (R.EmissiveR + R.EmissiveG + R.EmissiveB > 0.0f)
        {
            Out[0] = R.EmissiveR; Out[1] = R.EmissiveG; Out[2] = R.EmissiveB;
            continue;
        }
        Out[0] = R.AlbedoR * (1.0f - R.Metalness) * kAmbient;
        Out[1] = R.AlbedoG * (1.0f - R.Metalness) * kAmbient;
        Out[2] = R.AlbedoB * (1.0f - R.Metalness) * kAmbient;
    }

    // One shadow map per tap, spent over every covered pixel before the next tap renders.
    for (uint32_t TapIdx = 0u; TapIdx < TapCount_; ++TapIdx)
    {
        const LightTap& Tap = Taps_[TapIdx];
        RasterizeShadow(Level, Tap.P, Centre);
        for (uint32_t Y = 0u; Y < Height; ++Y)
        {
            for (uint32_t X = 0u; X < Width; ++X)
            {
                const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X);
                const uint32_t Tri = TriId_[Idx];
                if (Tri == kMiss)
                    continue;
                uint32_t Slot = 0u;
                std::memcpy(&Slot, &Flat[Tri].MaterialSlot, sizeof(Slot));
                const MaterialRecord& R = Records[Slot];
                if (R.EmissiveR + R.EmissiveG + R.EmissiveB > 0.0f)
                    continue;

                const float W0 = Bary_[Idx * 2u], W1 = Bary_[Idx * 2u + 1u], W2 = 1.0f - W0 - W1;
                const float P[3] = { W0 * Flat[Tri].VertexAlphaX + W1 * Flat[Tri].VertexBetaX + W2 * Flat[Tri].VertexGammaX,
                                     W0 * Flat[Tri].VertexAlphaY + W1 * Flat[Tri].VertexBetaY + W2 * Flat[Tri].VertexGammaY,
                                     W0 * Flat[Tri].VertexAlphaZ + W1 * Flat[Tri].VertexBetaZ + W2 * Flat[Tri].VertexGammaZ };
                float Ns[3] = { TriN_[Tri * 3u], TriN_[Tri * 3u + 1u], TriN_[Tri * 3u + 2u] };
                float Ex = Eye[0] - P[0], Ey = Eye[1] - P[1], Ez = Eye[2] - P[2];
                if (Ns[0] * Ex + Ns[1] * Ey + Ns[2] * Ez < 0.0f) { Ns[0] = -Ns[0]; Ns[1] = -Ns[1]; Ns[2] = -Ns[2]; }
                const float El = std::sqrt(Ex * Ex + Ey * Ey + Ez * Ez);
                Ex /= El; Ey /= El; Ez /= El;
                const float NdotV = Ns[0] * Ex + Ns[1] * Ey + Ns[2] * Ez;

                float Sx = Tap.P[0] - P[0], Sy = Tap.P[1] - P[1], Sz = Tap.P[2] - P[2];
                const float Sl = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
                Sx /= Sl; Sy /= Sl; Sz /= Sl;
                const float NdotL = Ns[0] * Sx + Ns[1] * Sy + Ns[2] * Sz;
                const float LdotL = -(Tap.N[0] * Sx + Tap.N[1] * Sy + Tap.N[2] * Sz);
                if (NdotL <= 0.0f || LdotL <= 0.0f)
                    continue;
                const float LitFrac = Shadow(P, Ns, NdotL, Tap.P);
                if (LitFrac <= 0.0f)
                    continue;

                const float Geo = NdotL * LdotL * Tap.Weight * LitFrac / (Sl * Sl);
                float Hx = Sx + Ex, Hy = Sy + Ey, Hz = Sz + Ez;
                const float Hl = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
                Hx /= Hl; Hy /= Hl; Hz /= Hl;
                const float NdotH = Ns[0] * Hx + Ns[1] * Hy + Ns[2] * Hz;
                float Alpha = R.Roughness * R.Roughness;
                if (Alpha < 0.05f)
                    Alpha = 0.05f;
                const float A2 = Alpha * Alpha;
                const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
                const float Df = A2 / (kPi * Denom * Denom);
                const float Kk = (Alpha + 1.0f) * (Alpha + 1.0f) * 0.125f;
                const float G = (NdotV / (NdotV * (1.0f - Kk) + Kk)) * (NdotL / (NdotL * (1.0f - Kk) + Kk));
                const float Cos1 = 1.0f - NdotH;
                const float Cos5 = Cos1 * Cos1 * Cos1 * Cos1 * Cos1;
                float* Out = &Acc_[Idx * 3u];
                const float Alb[3] = { R.AlbedoR, R.AlbedoG, R.AlbedoB };
                for (int C = 0; C < 3; ++C)
                {
                    const float F0 = 0.04f + (Alb[C] - 0.04f) * R.Metalness;
                    const float F = F0 + (1.0f - F0) * Cos5;
                    const float Spec = Df * G * F / (4.0f * NdotV * NdotL);
                    const float Diff = Alb[C] * (1.0f - R.Metalness) / kPi;
                    Out[C] += (Diff + Spec) * Tap.Le[C] * Geo;
                }
            }
        }
    }

    double LumSum = 0.0;
    for (uint32_t Y = 0u; Y < Height; ++Y)
    {
        for (uint32_t X = 0u; X < Width; ++X)
        {
            const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X);
            unsigned char* Px = Rgba + Idx * 4u;
            for (int C = 0; C < 3; ++C)
            {
                const float Reinhard = Acc_[Idx * 3u + static_cast<size_t>(C)];
                const float Mapped    = Reinhard / (1.0f + Reinhard);
                const float Gamma     = std::pow(Mapped < 0.0f ? 0.0f : Mapped, 1.0f / 2.2f);
                Px[C] = static_cast<unsigned char>(Gamma * 255.0f + 0.5f);
            }
            Px[3] = 255u;
            LumSum += 0.2126 * Px[0] + 0.7152 * Px[1] + 0.0722 * Px[2];
        }
    }
    MeanLum = LumSum / (static_cast<double>(Cells) * 255.0);
}

} // namespace Frontier
