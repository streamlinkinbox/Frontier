//============================================================================================================================================
//                                                      VISIBILITYRASTER.CPP
//============================================================================================================================================
// 🧩 The visibility buffer, rasterized on the CPU. Pass one keeps the nearest triangle per pixel; the shadow
//    pass keeps the nearest depth per texel from each light tap; the shade pass spends the two buffers with no
//    ray query anywhere. Taps are fixed and stratified (deterministic across runs).
//
//    Which filter runs over the map is the quality tier's call, carried in by AssignShadowCriteria: a single hard
//    comparison, a fixed-radius PCF box, or PCSS, whose blocker search sizes the kernel from the real occluder
//    distance so contact stays sharp and the penumbra widens with distance. See VisibilityRaster.h for the ladder.

#include "VisibilityRaster.h"

#include "SceneStructure.h"
#include "DisplayPresentation/AtmosphereModel.h"
#include "DisplayPresentation/ColourTransfer.h"
#include "DisplayPresentation/CelestialSolver.h"
#include "DisplayPresentation/SkyConstantRecord.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265359f;
// The flat blue that used to stand in for a sky. Kept only as the fallback for when the celestial model is
//    switched off, so the raster still produces a sensible background rather than black.
constexpr float kSkyFallback[3] = { 0.30f, 0.42f, 0.63f };

// [-] Sky luminance above which no star can be seen. Sirius reaches about 0.077 in these units through the
//    catalogue's own gain, and a twilight sky passes that around the point the first stars appear, so the
//    ceiling sits just above it rather than at an arbitrary time of day.
constexpr float kStarVisibilityCeiling = 0.09f;

// The smoothstep and lerp the sun's body needs (Twilight keeps its own private pair, and the raster must not
//    reach through the moon records for sun math). File-local: no other translation unit should see these.
inline float Smooth01(float Edge0, float Edge1, float V) noexcept
{
    float T = (V - Edge0) / (Edge1 - Edge0);
    T = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
    return T * T * (3.0f - 2.0f * T);
}
inline float Mix01(float A, float B, float T) noexcept { return A + (B - A) * T; }

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
    Shadow_.assign(static_cast<size_t>(Shadows_.MapSide) * static_cast<size_t>(Shadows_.MapSide), 1e30f);
    Acc_.assign(Cells * 3u, 0.0f);

    RasterizePrimary(Level, Eye, Forward, Right, Up, FovYRadians, Width, Height);
    Shade(Level, Eye, Forward, Right, Up, FovYRadians, Width, Height, Rgba, MeanLum);
    return true;
}

bool VisibilityRaster::RenderOrthographic(const SceneStructure& Level,
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3], float HalfHeightWorld,
                              uint32_t Width, uint32_t Height,
                              unsigned char* Rgba, double& MeanLum) noexcept
{
    const auto& Flat = Level.QueryFlatTriangles();
    if (Flat.empty() || Width == 0u || Height == 0u || Rgba == nullptr)
        return false;
    if (!(HalfHeightWorld > 0.0f))
        return false;

    CollectLumi(Level);
    PlaceTaps();

    const size_t Cells = static_cast<size_t>(Width) * static_cast<size_t>(Height);
    Depth_.assign(Cells, 1e30f);
    TriId_.assign(Cells, kMiss);
    Bary_.assign(Cells * 2u, 0.0f);
    TriN_.assign(Flat.size() * 3u, 0.0f);
    Shadow_.assign(static_cast<size_t>(Shadows_.MapSide) * static_cast<size_t>(Shadows_.MapSide), 1e30f);
    Acc_.assign(Cells * 3u, 0.0f);

    RasterizePrimaryOrthographic(Level, Eye, Forward, Right, Up, HalfHeightWorld, Width, Height);
    // The shade pass ignores the projection arguments (it rebuilds hit points off the visibility buffer),
    //    so the perspective field of view below is a formality, never consulted.
    Shade(Level, Eye, Forward, Right, Up, 1.0471976f, Width, Height, Rgba, MeanLum);
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
        // PCSS's LightSize: the luminaire's own extent. Taking √Area of the whole emitter (not of one tap's share)
        //    is the physically meaningful figure — the penumbra is cast by the light's real width, and the four
        //    taps are just how that one emitter is integrated.
        Tap.Size = std::sqrt(L.Area * static_cast<float>(Lumi_.size()));
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

void VisibilityRaster::RasterizePrimaryOrthographic(const SceneStructure& Level, const float Eye[3],
                                        const float Forward[3], const float Right[3], const float Up[3],
                                        float HalfHeightWorld, uint32_t Width, uint32_t Height) noexcept
{
    const auto& Flat = Level.QueryFlatTriangles();
    const float HalfH = HalfHeightWorld;
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
        float Sx[3], Sy[3], Zv[3];
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
            Zv[K] = Zc;
            Sx[K] = (Xc / HalfW * 0.5f + 0.5f) * static_cast<float>(Width);
            Sy[K] = (1.0f - (Yc / HalfH * 0.5f + 0.5f)) * static_cast<float>(Height);
        }
        if (Near)
            continue;

        float Area = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Sx[2], Sy[2]);
        if (Area == 0.0f)
            continue;
        bool Swapped = false;
        if (Area < 0.0f)
        {
            const float Tx = Sx[1]; Sx[1] = Sx[2]; Sx[2] = Tx;
            const float Ty = Sy[1]; Sy[1] = Sy[2]; Sy[2] = Ty;
            const float Tz = Zv[1]; Zv[1] = Zv[2]; Zv[2] = Tz;
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
                const float Z = W0 * Zv[0] + W1 * Zv[1] + W2 * Zv[2];
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
            Sx[K] = (Xc * InvZ[K] / ShadowTan_ * 0.5f + 0.5f) * static_cast<float>(Shadows_.MapSide);
            Sy[K] = (1.0f - (Yc * InvZ[K] / ShadowTan_ * 0.5f + 0.5f)) * static_cast<float>(Shadows_.MapSide);
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
        if (LoX < 0) LoX = 0; if (HiX > static_cast<int>(Shadows_.MapSide)) HiX = static_cast<int>(Shadows_.MapSide);
        if (LoY < 0) LoY = 0; if (HiY > static_cast<int>(Shadows_.MapSide)) HiY = static_cast<int>(Shadows_.MapSide);
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
                const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(Shadows_.MapSide) + static_cast<size_t>(X);
                if (Z < Shadow_[Idx])
                    Shadow_[Idx] = Z;
            }
        }
    }
}

void VisibilityRaster::AssignShadowCriteria(const ShadowCriteria& Criteria) noexcept
{
    // The map side is clamped to something a CPU proof can actually rasterize; the tiers stay well inside it.
    Shadows_.Filter   = Criteria.Filter;
    Shadows_.MapSide  = Criteria.MapSide  < 64u ? 64u : (Criteria.MapSide  > 4096u ? 4096u : Criteria.MapSide);
    Shadows_.TapCount = Criteria.TapCount <  1u ?  1u : (Criteria.TapCount >   15u ?   15u : Criteria.TapCount);
}

float VisibilityRaster::ShadowTexel(int32_t X, int32_t Y) const noexcept
{
    if (X < 0 || Y < 0 || X >= static_cast<int32_t>(Shadows_.MapSide) || Y >= static_cast<int32_t>(Shadows_.MapSide))
        return 1e30f;   // off the map: nothing occludes there
    return Shadow_[static_cast<size_t>(Y) * static_cast<size_t>(Shadows_.MapSide) + static_cast<size_t>(X)];
}

bool VisibilityRaster::BlockerDepth(float TexU, float TexV, float Zc, float Bias, float Radius,
                                    float& OutDepth) const noexcept
{
    // PCSS step one. Everything nearer to the light than this receiver is an occluder; their MEAN depth is what
    //    the penumbra relation needs (the nearest alone would over-soften wherever two occluders overlap).
    const float Step = Radius <= 0.0f ? 1.0f : Radius * 2.0f / static_cast<float>(kBlockerTaps - 1u);
    const float Half = Radius <= 0.0f ? 0.0f : Radius;
    float Sum = 0.0f;
    uint32_t Count = 0u;
    for (uint32_t Y = 0u; Y < kBlockerTaps; ++Y)
    {
        for (uint32_t X = 0u; X < kBlockerTaps; ++X)
        {
            const float Sx = TexU - Half + Step * static_cast<float>(X);
            const float Sy = TexV - Half + Step * static_cast<float>(Y);
            const float D  = ShadowTexel(static_cast<int32_t>(Sx + 0.5f), static_cast<int32_t>(Sy + 0.5f));
            if (D + Bias < Zc)
            {
                Sum += D;
                ++Count;
            }
        }
    }
    if (Count == 0u)
        return false;
    OutDepth = Sum / static_cast<float>(Count);
    return true;
}

float VisibilityRaster::FilterLit(float TexU, float TexV, float Zc, float Bias,
                                  float RadiusTexels, uint32_t Taps) const noexcept
{
    // A single tap is the hard test; anything wider is a box of comparisons whose mean is the lit fraction,
    //    which is what turns a binary edge into a ramp.
    if (Taps <= 1u || RadiusTexels <= 0.0f)
        return ShadowTexel(static_cast<int32_t>(TexU + 0.5f), static_cast<int32_t>(TexV + 0.5f)) + Bias < Zc ? 0.0f : 1.0f;

    const float Step = RadiusTexels * 2.0f / static_cast<float>(Taps - 1u);
    float Lit = 0.0f;
    for (uint32_t Y = 0u; Y < Taps; ++Y)
    {
        for (uint32_t X = 0u; X < Taps; ++X)
        {
            const float Sx = TexU - RadiusTexels + Step * static_cast<float>(X);
            const float Sy = TexV - RadiusTexels + Step * static_cast<float>(Y);
            if (!(ShadowTexel(static_cast<int32_t>(Sx + 0.5f), static_cast<int32_t>(Sy + 0.5f)) + Bias < Zc))
                Lit += 1.0f;
        }
    }
    return Lit / static_cast<float>(Taps * Taps);
}

float VisibilityRaster::Shadow(const float P[3], const float N[3], float NdotL, const LightTap& Tap) const noexcept
{
    // The sample leaves from the surface point nudged one normal offset outward; without that nudge a
    // grazing wall lands inside its own map texels and peppers itself with acne.
    constexpr float kNormalOffset = 0.02f; // [m] along the geometric normal, ahead of the map test
    const float Qx = P[0] + N[0] * kNormalOffset;
    const float Qy = P[1] + N[1] * kNormalOffset;
    const float Qz = P[2] + N[2] * kNormalOffset;
    const float Ox = Qx - Tap.P[0], Oy = Qy - Tap.P[1], Oz = Qz - Tap.P[2];
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

    const float Side = static_cast<float>(Shadows_.MapSide);
    const float TexU = U * Side - 0.5f;
    const float TexV = V * Side - 0.5f;
    const float Bias = kShadowBias * (1.0f + (1.0f - NdotL));

    // Texels per metre at the receiver's depth: the map covers 2·Zc·tan(half) metres across Side texels, so this
    //    is what converts a penumbra measured in metres into a kernel measured in texels.
    const float TexelsPerMetre = Side / (2.0f * Zc * ShadowTan_);

    switch (Shadows_.Filter)
    {
        case ShadowFilterKind::HardShadowMap:
            return FilterLit(TexU, TexV, Zc, Bias, 0.0f, 1u);

        case ShadowFilterKind::WidePercentageCloserFilter:
        {
            // A fixed kernel: the radius is the tap count, so the penumbra is a constant number of texels wide
            //    wherever it falls. Cheap and stable — and deliberately not physical, which is the tier's point.
            const float Radius = static_cast<float>(Shadows_.TapCount) * 0.5f;
            return FilterLit(TexU, TexV, Zc, Bias, Radius, Shadows_.TapCount);
        }

        case ShadowFilterKind::PercentageCloserSoftShadow:
        default:
        {
            // Step one: search for blockers over a window scaled by the light's own size, as seen from here.
            const float SearchTexels = std::max(Tap.Size * TexelsPerMetre * 0.5f, 1.0f);
            float Blocker = 0.0f;
            if (!BlockerDepth(TexU, TexV, Zc, Bias, SearchTexels, Blocker))
                return 1.0f;                       // no occluder in the window: fully lit, and no kernel to pay for
            if (Blocker <= 0.0f)
                return FilterLit(TexU, TexV, Zc, Bias, 1.0f, Shadows_.TapCount);

            // Step two: similar triangles. w = (Receiver − Blocker) / Blocker × LightSize. At contact the numerator
            //    goes to zero and the kernel collapses to a hard test; far from the occluder it opens up.
            const float Penumbra = (Zc - Blocker) / Blocker * Tap.Size;
            const float Radius   = std::max(Penumbra * TexelsPerMetre * 0.5f, 0.5f);
            return FilterLit(TexU, TexV, Zc, Bias, Radius, Shadows_.TapCount);
        }
    }
}

void VisibilityRaster::Shade(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                             const float Right[3], const float Up[3], float FovYRadians,
                             uint32_t Width, uint32_t Height, unsigned char* Rgba, double& MeanLum) noexcept
{
    // The camera basis used to be discarded here: the background was one flat colour, so the ray a pixel looked
    //    along did not matter. With a real atmosphere it is the only thing that matters — the sky is a function of
    //    direction, and a constant would throw away the whole gradient from zenith to horizon.
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

    // The sun's own elevation and compass bearing, needed by the twilight term. Derived from the light direction
    //    the caller already supplied, so the raster never has to know about CelestialSolver.
    const float SunElevationDegrees = std::asin(std::fmax(-1.0f, std::fmin(1.0f, Celestial_.Light.Direction[2])))
                                    * 180.0f / kPi;
    // The clouds' sunlight, shared by the pixels and the ambient probe below: full sun above the horizon, none
    //    below -12 deg, a twilight ramp between. The march knows no planet shadow (see the note at the pixel
    //    composite), so both call sites take this rather than restating the ramp.
    const float CloudDayFactor = Smooth01(-12.0f, 0.0f, SunElevationDegrees);
    const float CloudSunRad[3] = { Celestial_.Light.Colour[0] * Celestial_.Light.Intensity * CloudDayFactor,
                                   Celestial_.Light.Colour[1] * Celestial_.Light.Intensity * CloudDayFactor,
                                   Celestial_.Light.Colour[2] * Celestial_.Light.Intensity * CloudDayFactor };
    const float SunBearing = std::atan2(Celestial_.Light.Direction[0], Celestial_.Light.Direction[1]);
    const auto AzimuthDeltaFor = [SunBearing](const float Dir[3]) -> float
    {
        // Angle between the view ray's horizontal bearing and the sun's, folded to [0, pi].
        const float Bearing = std::atan2(Dir[0], Dir[1]);
        float Delta = std::fabs(Bearing - SunBearing);
        if (Delta > kPi) Delta = 2.0f * kPi - Delta;
        return Delta;
    };

    // The sky, evaluated per pixel along that pixel's own view ray. Sample counts come from the tier ladder so
    //    a Minimal frame integrates 8 steps and a Reference frame 32 — the panel never restates these.
    const float TanHalf = std::tan(FovYRadians * 0.5f);
    const float Aspect  = static_cast<float>(Width) / static_cast<float>(Height);
    // One pixel's angular size. A star smaller than this would alias into a flicker as the camera turns.
    const float PixelAngle = FovYRadians / static_cast<float>(Height);
    const auto SkyAlong = [&](size_t Idx, float* Out)
    {
        if (!Celestial_.Enabled)
        {
            Out[0] = kSkyFallback[0]; Out[1] = kSkyFallback[1]; Out[2] = kSkyFallback[2];
            return;
        }
        const uint32_t Px = static_cast<uint32_t>(Idx % Width);
        const uint32_t Py = static_cast<uint32_t>(Idx / Width);
        const float Sx = (2.0f * ((static_cast<float>(Px) + 0.5f) / static_cast<float>(Width)) - 1.0f) * TanHalf * Aspect;
        const float Sy = (1.0f - 2.0f * ((static_cast<float>(Py) + 0.5f) / static_cast<float>(Height))) * TanHalf;
        float Dir[3] = { Forward[0] + Right[0] * Sx + Up[0] * Sy,
                         Forward[1] + Right[1] * Sx + Up[1] * Sy,
                         Forward[2] + Right[2] * Sx + Up[2] * Sy };
        const float Length = std::sqrt(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
        if (Length > 0.0f) { Dir[0] /= Length; Dir[1] /= Length; Dir[2] /= Length; }
        const AtmosphereSample S = AtmosphereModel::Integrate(Celestial_.Medium, Celestial_.Light,
                                                              Celestial_.CameraHeight, Dir,
                                                              Celestial_.SampleCount, Celestial_.LightSampleCount);
        Out[0] = S.Radiance[0]; Out[1] = S.Radiance[1]; Out[2] = S.Radiance[2];

        // Aureole compression, transcribed term for term from SkyAlong in SkyRecords.slang: the luminance-ratio
        //    soft shoulder inside its angular gate (see the note there for why the knee exists). Runs here, on
        //    the integral alone, so the twilight, the line, the moons and the disc all add afterward onto the
        //    same capped air both paths share. Guarded on rock like everything below: the planet is shaded as
        //    Lambertian terrain, not air, and must never see the shoulder.
        if (!S.HitGround)
        {
            const float AureoleDot = Dir[0] * Celestial_.Light.Direction[0]
                                   + Dir[1] * Celestial_.Light.Direction[1]
                                   + Dir[2] * Celestial_.Light.Direction[2];
            const float AureoleAng = std::acos(std::fmax(-1.0f, std::fmin(1.0f, AureoleDot)));
            const float AureoleLum = Out[0] * 0.2126f + Out[1] * 0.7152f + Out[2] * 0.0722f;
            const float AureoleGate = std::exp(-AureoleAng / kAureoleSigma);
            const float AureoleTarget = AureoleLum <= kAureoleKnee
                ? AureoleLum : kAureoleKnee + (AureoleLum - kAureoleKnee) * kAureoleSlope;
            const float AureoleMix = AureoleLum > 0.0f
                ? (AureoleLum + (AureoleTarget - AureoleLum) * AureoleGate) / AureoleLum : 1.0f;
            Out[0] *= AureoleMix; Out[1] *= AureoleMix; Out[2] *= AureoleMix;
        }

        // ⚠️ Rays that meet the planet must not go on to collect stars. AtmosphereModel already stops the
        //    integral at the surface and reports it, but the miss path had no notion of a world: the only ground
        //    was whatever finite geometry the scene happened to contain, so from altitude the scene read as a
        //    slab hanging in space with stars shining through the earth beneath it. HitGround is the horizon,
        //    and below it there is a planet.
        //
        //    The surface itself is shaded as a lit Lambertian sphere rather than left black, because a ray that
        //    reaches the ground from above is looking at daylit terrain — that is what makes the limb read as a
        //    planet instead of a hole.
        // The cloud march's far plane: open sky, unless the ray meets the planet — then the surface, so no
        //    cloud draws behind the limb it sits behind.
        float CloudMaxDist = 1e30f;
        if (S.HitGround)
        {
            const float Centre[3] = { 0.0f, 0.0f, Celestial_.Medium.PlanetRadius + Celestial_.CameraHeight };
            float Near = 0.0f, Far = 0.0f;
            if (AtmosphereModel::IntersectSphere(Centre, Dir, Celestial_.Medium.PlanetRadius, Near, Far) && Near > 0.0f)
            {
                CloudMaxDist = Near;
                const float Hit[3] = { Centre[0] + Dir[0] * Near, Centre[1] + Dir[1] * Near, Centre[2] + Dir[2] * Near };
                const float Length = std::sqrt(Hit[0] * Hit[0] + Hit[1] * Hit[1] + Hit[2] * Hit[2]);
                if (Length > 0.0f)
                {
                    const float Normal[3] = { Hit[0] / Length, Hit[1] / Length, Hit[2] / Length };
                    const float NdotL = Normal[0] * Celestial_.Light.Direction[0]
                                      + Normal[1] * Celestial_.Light.Direction[1]
                                      + Normal[2] * Celestial_.Light.Direction[2];
                    const float Lit = NdotL > 0.0f ? NdotL : 0.0f;
                    for (int C = 0; C < 3; ++C)
                        Out[C] += Celestial_.GroundAlbedo[C] * Lit * Celestial_.Light.Intensity * 0.05f
                                * S.Transmittance[C];
                }
            }
        }
        const bool SeesSpace = !S.HitGround;

        // Clouds, composited over the air as their own layer — the reference architecture (a separate cloud
        //    system over SkyAtmosphere), not a second atmosphere. The march runs from the eye to the planet
        //    when the ray meets it, so limb haze stays this side of the surface. The transmittance is kept:
        //    every celestial add below (stars, moons, disc) sits behind the weather and is scaled by it,
        //    while the twilight and the line are foreground airglow and are not.
        //
        //    The day factor, because the march knows no planet shadow: the air integral earns its night from
        //    geometry (every sample sunk in shadow), but the march would light midnight clouds with the full
        //    noon sun. Full sun above the horizon, none below -12 deg, a twilight ramp between — so night
        //    clouds are dark occluders until moonlight lands (with Moon NEE), and overcast eats the stars.
        //    Lit by the air just integrated: clouds shine with the sky around them, not in front of it.
        float CloudT = 1.0f;
        if (Celestial_.CloudLayer.Enabled || Celestial_.LocalCloud.Enabled || Celestial_.LocalFog.Enabled)
        {
            const VolumetricSample V = VolumetricMedia::March(Celestial_.CloudLayer, Celestial_.LocalCloud,
                                                              Celestial_.LocalFog, Celestial_.Wind,
                                                              Celestial_.CloudBudget, Eye, Dir, CloudMaxDist,
                                                              Celestial_.Light.Direction, CloudSunRad, Out,
                                                              Celestial_.CloudTime);
            for (int C = 0; C < 3; ++C) Out[C] = Out[C] * V.Transmittance + V.Scatter[C];
            CloudT = V.Transmittance;
        }

        // Stars, before twilight, because they are behind it: the glow washes them out near the horizon rather
        //    than the other way round. Only the cell the ray falls in is tested — 8 920 stars binned into 1 024
        //    cells means about 9 candidates instead of the whole catalogue (StarCatalogueIndex).
        // ⚠️ The star pass is gated on the SKY's own brightness, and the gate is a correctness matter before it
        //    is a performance one: without it, stars are drawn over a noon sky and show through daylight. The
        //    physical reason is that a star is only visible when it outshines the sky behind it — daylight does
        //    not remove the star, it removes the CONTRAST — so the test is against the sky at this very pixel
        //    rather than against a time of day.
        //
        //    It is also the measured win the reference branch kept (da4b0d7): skipping the cell lookup over the
        //    lit half of the sky costs nothing to add and removes most of the star work from every day frame.
        const float SkyLuminance = Out[0] * 0.2126f + Out[1] * 0.7152f + Out[2] * 0.0722f;
        const bool  StarsVisible = SkyLuminance < kStarVisibilityCeiling;
        if (SeesSpace && StarsVisible && Celestial_.Stars != nullptr && !Celestial_.Stars->Empty())
        {
            // The catalogue is equatorial J2000 and the ray is in the horizon frame, so the ray is rotated into
            //    the catalogue's frame rather than the catalogue into the ray's — one rotation per pixel instead
            //    of one per star.
            float Equatorial[3];
            HorizonToEquatorial(Dir, Celestial_.LocalSiderealTime, Celestial_.Latitude, Equatorial);
            const uint32_t Cell = StarCatalogueIndex::CellForDirection(Equatorial[0], Equatorial[1], Equatorial[2]);
            const std::vector<StarCellRecord>& Cells = Celestial_.Stars->QueryCells();
            if (Cell < Cells.size())
            {
                const std::vector<StarRecord>& All = Celestial_.Stars->QueryStars();
                const StarCellRecord& Bucket = Cells[Cell];
                for (uint32_t I = 0u; I < Bucket.Count; ++I)
                {
                    const StarRecord& Star = All[Bucket.First + I];
                    const float Dot = Equatorial[0] * Star.DirectionX
                                    + Equatorial[1] * Star.DirectionY
                                    + Equatorial[2] * Star.DirectionZ;
                    if (Dot <= 0.0f) continue;
                    const float Angle = std::acos(std::fmin(1.0f, Dot));
                    // ⚠️ A star is a POINT SOURCE and must read as one. The first version used a core radius of
                    //    0.9 pixels with a Gaussian skirt running to 3x that, so every star covered about 5.4
                    //    pixels and the field looked like smudges rather than points — visibly soft at any
                    //    resolution.
                    //
                    //    The constraint pulling the other way is aliasing: a star smaller than a pixel lands
                    //    between sample points and winks in and out as the camera turns, which is worse than
                    //    blur because it only appears in motion. The resolution is to keep the ENERGY inside
                    //    roughly one pixel while letting a small skirt handle the sub-pixel positioning: a half
                    //    pixel core, a skirt of a third of a pixel, cut off at 1.6 pixels.
                    const float Radius = std::fmax(Celestial_.StarSize * 0.0002f, PixelAngle * 0.5f);
                    if (Angle > Radius * 1.6f) continue;
                    // Flat-topped core so the star is at full intensity where it lands, then a tight Gaussian
                    //    edge. The narrow skirt is what keeps it a point rather than a smudge.
                    const float Falloff = (Angle - Radius) / (Radius * 0.35f);
                    const float Core = Angle <= Radius ? 1.0f : std::exp(-Falloff * Falloff);
                    // Gain 1.0, not a fraction. The catalogue's luminance is already relative to magnitude 0,
                    //    so scaling it down crushes the faint end: at 0.02 a magnitude-4 star reached 4/255 and
                    //    the sky rendered as a nearly empty black field with only Sirius faintly visible.
                    // Energy normalisation. A point source carries a FIXED total, so tightening the profile
                    //    must raise its peak or the star just gets dimmer — measured, the new footprint holds
                    //    5.73x less solid angle than the soft one it replaced, and the first sharpened render
                    //    came out visibly darker for exactly that reason.
                    const float Gain = Star.Luminance * Celestial_.StarBrightness * Core * 5.73f;
                    Out[0] += Star.ColourRed * Gain * CloudT;
                    Out[1] += Star.ColourGreen * Gain * CloudT;
                    Out[2] += Star.ColourBlue * Gain * CloudT;
                }
            }
        }

        // Moons, after the stars and before the twilight: they are behind both the glow and the air, so the
        //    disc is multiplied by the same transmittance the integral measured rather than drawn over it. Gated
        //    on SeesSpace like the stars — below the horizon there is a planet in the way, not a moonrise.
        //    Behind the weather too: overcast eats the moon as it eats the stars (CloudT is 1 when clear).
        if (SeesSpace && Celestial_.Moons != nullptr && Celestial_.Moons->Count > 0u)
        {
            float MoonRgb[3];
            EvaluateMoons(Celestial_.Moons->Entries, Celestial_.Moons->Count, Dir, S.Transmittance, MoonRgb);
            Out[0] += MoonRgb[0] * CloudT; Out[1] += MoonRgb[1] * CloudT; Out[2] += MoonRgb[2] * CloudT;
        }

        // Twilight rides on top of the physical integral. Single scattering cannot produce a lit sky once the sun
        //    is below the horizon (every sample is in the planet's shadow), so without this the pre-dawn sky is
        //    black. See the note above Twilight in AtmosphereModel.h.
        float Glow[3];
        Twilight::Evaluate(Dir, SunElevationDegrees, AzimuthDeltaFor(Dir), Celestial_.Twilight, Glow);
        Out[0] += Glow[0]; Out[1] += Glow[1]; Out[2] += Glow[2];

        // The sun's body. The integral above is skylight only, so without this the sun is a Mie glow with no
        //    source on this path while the kernel draws a disc — two skies from one engine. Transcribed term
        //    for term from the shader (SkyAlong in SkyRecords.slang): smoothstep-edged, limb-darkened, gated
        //    near the horizon, at the panel's own defaults (0.53 deg diameter, 0.25 softness, 12x disc
        //    radiance), reddened by the integral's own transmittance. A hidden sun needs no extra gate:
        //    a disabled sky takes the fallback branch above, and below the horizon SeesSpace is false.
        if (SeesSpace)
        {
            const float SunDot = Dir[0] * Celestial_.Light.Direction[0]
                               + Dir[1] * Celestial_.Light.Direction[1]
                               + Dir[2] * Celestial_.Light.Direction[2];
            const float SunAng = std::acos(std::fmax(-1.0f, std::fmin(1.0f, SunDot)));
            constexpr float kSunSoft = 0.25f;
            constexpr float kSunBoost = 12.0f;
            const float ViewElev = std::asin(std::fmax(-1.0f, std::fmin(1.0f, Dir[2]))) * 180.0f / kPi;
            const float SunSoftElev = Mix01(1.0f, 2.2f, 1.0f - Smooth01(0.0f, 4.0f, ViewElev));
            const float SunDisc = 1.0f - Smooth01(kSunAngularRadius * (1.0f - kSunSoft * 0.9f * SunSoftElev),
                                                  kSunAngularRadius, SunAng);
            const float SunLimb = Mix01(1.0f, 0.55f, Smooth01(0.0f, kSunAngularRadius, SunAng));
            const float SunGate = Mix01(0.35f, 1.0f, Smooth01(-1.0f, 8.0f, ViewElev));
            // Behind the weather: overcast dims the disc as it dims the stars (CloudT is 1 when clear).
            for (int C = 0; C < 3; ++C)
                Out[C] += SunDisc * SunLimb * Celestial_.Light.Colour[C] * Celestial_.Light.Intensity
                        * S.Transmittance[C] * kSunBoost * SunGate * CloudT;
        }
    };

    // The sky's contribution as an ambient term, evaluated ONCE for the frame (see the note at the fill below).
    float SkyAmbient[3] = { 0.0f, 0.0f, 0.0f };
    if (Celestial_.Enabled)
    {
        const float Zenith[3] = { 0.0f, 0.0f, 1.0f };
        const AtmosphereSample Probe = AtmosphereModel::Integrate(Celestial_.Medium, Celestial_.Light,
                                                                  Celestial_.CameraHeight, Zenith,
                                                                  Celestial_.SampleCount, Celestial_.LightSampleCount);
        // A hemisphere of sky at that radiance, times the Lambert 1/pi, is pi * L / pi = L. The 0.5 accounts for
        //    the ground taking the other half of the sphere.
        //
        //    Marched through the weather first: one march per frame, so overcast darkens the world the clouds
        //    shade. The probe looks straight up and never meets the planet, and it is lit by the air it just
        //    measured — the same composite the pixels run, at a single direction.
        float ProbeSky[3] = { Probe.Radiance[0], Probe.Radiance[1], Probe.Radiance[2] };
        if (Celestial_.CloudLayer.Enabled || Celestial_.LocalCloud.Enabled || Celestial_.LocalFog.Enabled)
        {
            const VolumetricSample Pv = VolumetricMedia::March(Celestial_.CloudLayer, Celestial_.LocalCloud,
                                                               Celestial_.LocalFog, Celestial_.Wind,
                                                               Celestial_.CloudBudget, Eye, Zenith, 1e30f,
                                                               Celestial_.Light.Direction, CloudSunRad, ProbeSky,
                                                               Celestial_.CloudTime);
            for (int C = 0; C < 3; ++C) ProbeSky[C] = ProbeSky[C] * Pv.Transmittance + Pv.Scatter[C];
        }
        for (int C = 0; C < 3; ++C) SkyAmbient[C] = ProbeSky[C] * 0.5f;
        // Moonlight joins the probe, not the pixels: the reference panel's ambient loop is a per-frame constant
        //    too, and the kernel adds the identical term — so a moonlit frame cannot be bright on one path and
        //    black on the other. At a moonless midnight this is zero and the night stays black, which is what
        //    the kAmbient note below defends.
        if (Celestial_.Moons != nullptr && Celestial_.Moons->Count > 0u)
        {
            float MoonAmb[3];
            EvaluateMoonAmbient(Celestial_.Moons->Entries, Celestial_.Moons->Count, MoonAmb);
            for (int C = 0; C < 3; ++C) SkyAmbient[C] += MoonAmb[C];
        }
    }

    // Seed pass: sky where nothing won, ambient where something did, emission where it glows.
    for (size_t Idx = 0u; Idx < Cells; ++Idx)
    {
        float* Out = &Acc_[Idx * 3u];
        if (TriId_[Idx] == kMiss)
        {
            SkyAlong(Idx, Out);
            continue;
        }
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[TriId_[Idx]].MaterialSlot, sizeof(Slot));
        if (Slot >= Records.size())
        {
            SkyAlong(Idx, Out);
            TriId_[Idx] = kMiss;
            continue;
        }
        const MaterialRecord& R = Records[Slot];
        if (R.EmissiveR + R.EmissiveG + R.EmissiveB > 0.0f)
        {
            Out[0] = R.EmissiveR; Out[1] = R.EmissiveG; Out[2] = R.EmissiveB;
            continue;
        }
        // Ambient fill. With the celestial model off this is the flat kAmbient the raster has always used; with
        //    it on, the sky IS the ambient — this is the GI-off path's environment light, and without it a scene
        //    lit only by the sky renders as black geometry against a correct sky, which is exactly what the first
        //    run of CelestialSkyProof showed.
        //
        //    The term is the sky's own radiance toward the zenith, scaled by the hemisphere the surface sees. A
        //    single zenith evaluation rather than a cosine-weighted integral is deliberate: it is one atmosphere
        //    evaluation per FRAME instead of one per pixel, which is the same trade the reference demo measured
        //    and kept (source commit da4b0d7, "sky-ambient computed once per frame via a 4x1 probe pass instead
        //    of 3 atmosphere evaluations per pixel").
        //    kAmbient is REPLACED rather than added to when the sky is on. Adding them looked reasonable until
        //    the night sheet: with the sun 53 deg below the horizon the sky contributes nothing, so the constant
        //    was the only term left and it lit a moonless midnight to a flat grey floor brighter than dusk. The
        //    flat fill exists precisely because there was no environment light; once there is one, it is the
        //    environment light.
        const float Fill[3] = { Celestial_.Enabled ? SkyAmbient[0] : kAmbient,
                                Celestial_.Enabled ? SkyAmbient[1] : kAmbient,
                                Celestial_.Enabled ? SkyAmbient[2] : kAmbient };
        Out[0] = R.AlbedoR * (1.0f - R.Metalness) * Fill[0];
        Out[1] = R.AlbedoG * (1.0f - R.Metalness) * Fill[1];
        Out[2] = R.AlbedoB * (1.0f - R.Metalness) * Fill[2];
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
                const float LitFrac = Shadow(P, Ns, NdotL, Tap);
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
            // One shared transfer, not a local copy of the curve. This path used to apply plain Reinhard with no
            //    exposure and no low-light desaturation while the ReSTIR kernel applied ACES with both, so the
            //    same radiance reached the screen up to 49/255 apart depending on which path drew it. That was
            //    survivable while the two drew different things; it is not, now that one sky feeds both.
            float Linear[3] = { Acc_[Idx * 3u + 0u], Acc_[Idx * 3u + 1u], Acc_[Idx * 3u + 2u] };
            // Weather in front of the world: a local volume between the eye and the surface occludes it. The
            //    march runs eye-to-hit — the slab is kilometres above any scene triangle, so it can only ever
            //    contribute nothing here, and the interval tests cost a rounding error. Lit by the environment
            //    (SkyAmbient), not by the wall behind: the cloud shines with the sky, even over night ground.
            if (Celestial_.Enabled && TriId_[Idx] != kMiss &&
                (Celestial_.CloudLayer.Enabled || Celestial_.LocalCloud.Enabled || Celestial_.LocalFog.Enabled))
            {
                const uint32_t Tri = TriId_[Idx];
                const float W0 = Bary_[Idx * 2u], W1 = Bary_[Idx * 2u + 1u], W2 = 1.0f - W0 - W1;
                const float P[3] = { W0 * Flat[Tri].VertexAlphaX + W1 * Flat[Tri].VertexBetaX + W2 * Flat[Tri].VertexGammaX,
                                     W0 * Flat[Tri].VertexAlphaY + W1 * Flat[Tri].VertexBetaY + W2 * Flat[Tri].VertexGammaY,
                                     W0 * Flat[Tri].VertexAlphaZ + W1 * Flat[Tri].VertexBetaZ + W2 * Flat[Tri].VertexGammaZ };
                float Hd[3] = { P[0] - Eye[0], P[1] - Eye[1], P[2] - Eye[2] };
                const float Hl = std::sqrt(Hd[0] * Hd[0] + Hd[1] * Hd[1] + Hd[2] * Hd[2]);
                if (Hl > 0.0f)
                {
                    Hd[0] /= Hl; Hd[1] /= Hl; Hd[2] /= Hl;
                    const VolumetricSample Hv = VolumetricMedia::March(Celestial_.CloudLayer, Celestial_.LocalCloud,
                                                                       Celestial_.LocalFog, Celestial_.Wind,
                                                                       Celestial_.CloudBudget, Eye, Hd, Hl,
                                                                       Celestial_.Light.Direction, CloudSunRad,
                                                                       SkyAmbient, Celestial_.CloudTime);
                    for (int C = 0; C < 3; ++C) Linear[C] = Linear[C] * Hv.Transmittance + Hv.Scatter[C];
                }
            }
            unsigned char Encoded[3];
            ColourPipeline::ApplyToByte(Colour_, Linear, Encoded);
            Px[0] = Encoded[0]; Px[1] = Encoded[1]; Px[2] = Encoded[2];
            Px[3] = 255u;
            LumSum += 0.2126 * Px[0] + 0.7152 * Px[1] + 0.0722 * Px[2];
        }
    }
    MeanLum = LumSum / (static_cast<double>(Cells) * 255.0);
}

} // namespace Frontier
