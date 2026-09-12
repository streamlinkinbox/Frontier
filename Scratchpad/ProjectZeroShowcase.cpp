//============================================================================================================================================
//                                                 PROJECTZEROSHOWCASE.CPP
//============================================================================================================================================
// 🧩 Project Zero's sky, sun, moon and stars as the game itself renders them — five aimed frames.
//
//    WHAT THIS IS: the project's own CelestialSequence (prepare → tick → ApplyTo) driven exactly as GameExecution
//    drives it — same tier budget via CelestialTier::BudgetFor, same shipping star catalogue, same six moon
//    albedos decoded through the real index, every entity shown (the project default) — rendered through the
//    CPU raster the game's no-ray path reads through ApplyTo. The clock is the only input; the sun, the
//    twilight, the moon and the wheeling stars all come out of the tick. Nothing is hand-set, nothing is mocked,
//    except where this header says so.
//
//    WHAT THIS IS NOT: the Vulkan window. This box has no GPU, no Vulkan driver and no display server, so
//    Project-Zero.exe cannot execute here; these frames are the headless twin of its sky path, at 960x540.
//
//    The day frames face the solved sun's azimuth the way a photographer would. The night frames need two dates,
//    and the reason is honest astronomy, verified with the shipping solver: on the gates' date (10 Sep 2026) the
//    real moon is new (illumination 0.00) and below the horizon all night, so the project's linked Luna —
//    Prepare() links roster slot 0 to the solved lunar frame — has nothing to show. The linked-moon frame is
//    therefore dated 26 Sep 2026, when the moon is full (illumination 1.00) and 48 deg up at 22h, and the camera
//    aims at the SOLVED moon direction (a linked slot ignores its Azimuth/Elevation, so aiming at those would
//    frame empty sky — the mistake this showcase made in its first draft). The placed-moon frame stays on
//    10 Sep and drives the roster the way the reference panel does: slot 0 unlinked and put at az 0 / el 25 at
//    2 deg, slot 1 (Ember) at az 14 / el 15 at 3 deg — the MoonRenderProof arrangement, with the stars left on so
//    the frame carries moons and stars together. The fifth frame turns to the parked local volume at 11h, when
//    its patch runs densest (probed 0.76 mean across the day): the enable is flipped — the volume parks off
//    until the scene wants weather somewhere specific — and the camera aims at the box's live centre.

#include "GeometricRaster/VisibilityRaster.h"
#include "GeometricRaster/SceneStructure.h"
#include "GeometricRaster/GeometryStructure.h"
#include "DisplayPresentation/CelestialSolver.h"
#include "DisplayPresentation/CelestialTier.h"
#include "DisplayPresentation/FidelityClassifier.h"
#include "DisplayPresentation/MoonConstantRecord.h"
#include "Projects/Project-Zero/Source/CelestialSequence.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>

using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {

constexpr uint32_t kWidth  = 960;
constexpr uint32_t kHeight = 540;

// A ground plane and a couple of blocks — enough that the sky has a silhouette to sit behind, which is what makes
//    a wrong horizon obvious. Copied from CelestialSkyProof's scene so the showcase shares the gate's ground truth.
// std::deque, not std::vector: GeometryStructure is not movable, so a vector cannot reallocate it.
void BuildScene(SceneStructure& Level, std::deque<GeometryStructure>& Owned)
{
    auto Quad = [&](float ax, float ay, float az, float bx, float by, float bz,
                    float cx, float cy, float cz, float dx, float dy, float dz)
    {
        Owned.emplace_back();
        GeometryStructure& G = Owned.back();
        VertexRecord V[4]{};
        const float P[4][3] = { {ax,ay,az}, {bx,by,bz}, {cx,cy,cz}, {dx,dy,dz} };
        float ux = bx-ax, uy = by-ay, uz = bz-az, vx = dx-ax, vy = dy-ay, vz = dz-az;
        float nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
        const float nl = std::sqrt(nx*nx+ny*ny+nz*nz);
        if (nl > 0.0f) { nx/=nl; ny/=nl; nz/=nl; }
        for (int i = 0; i < 4; ++i)
        {
            V[i].SpatialLocation  = Vector3{ P[i][0], P[i][1], P[i][2] };
            V[i].NormalDirection  = Vector3{ nx, ny, nz };
            V[i].TangentDirection = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };
            V[i].TextureCoordinateU = static_cast<float>(i == 1 || i == 2);
            V[i].TextureCoordinateV = static_cast<float>(i >= 2);
        }
        G.AppendVertices(V, 4u);
        const uint32_t Idx[6] = { 0u,1u,2u, 0u,2u,3u };
        G.AppendIndices(Idx, 6u);
        PolyhedralCluster C{};
        C.BoundingRadius = 200.0f;
        C.ConeCutoff     = 1.0f;
        C.TriangleCount  = static_cast<uint32_t>(G.QueryIndices().size() / 3u);
        G.RegisterCluster(C);
    };

    // Ground, tiled rather than one huge quad. VisibilityRaster is an unclipped rasteriser: kNear (0.05 m) makes
    //    it SKIP any triangle with a vertex nearer than that, and it cannot split one. Tiles keep every triangle
    //    wholly in front of the eye.
    for (int Ty = 0; Ty < 8; ++Ty)
    {
        const float Y0 = -4.0f + static_cast<float>(Ty) * 6.0f;
        const float Y1 = Y0 + 6.0f;
        for (int Tx = -3; Tx < 3; ++Tx)
        {
            const float X0 = static_cast<float>(Tx) * 8.0f;
            const float X1 = X0 + 8.0f;
            Quad(X0, Y0, 0.0f,  X1, Y0, 0.0f,  X1, Y1, 0.0f,  X0, Y1, 0.0f);
        }
    }
    // Two standing slabs, so there is a silhouette against the sky.
    Quad(-3.0f, 6.0f, 0.0f,  -1.0f, 6.0f, 0.0f,  -1.0f, 6.0f, 3.5f,  -3.0f, 6.0f, 3.5f);
    Quad( 1.5f, 9.0f, 0.0f,   4.0f, 9.0f, 0.0f,   4.0f, 9.0f, 2.2f,   1.5f, 9.0f, 2.2f);

    MaterialDescriptor M;
    M.Name = "Ground";
    MaterialSlabDescriptor S{};
    S.BaseColor[0] = 0.42f; S.BaseColor[1] = 0.40f; S.BaseColor[2] = 0.36f;
    S.SpecularRoughness = 0.8f;
    M.Slabs.push_back(S);
    const uint32_t Slot = Level.RegisterMaterial(M);

    const Matrix4x4 I = Matrix4x4::Identity();
    for (GeometryStructure& G : Owned) Level.RegisterInstance(G, I, Slot, 0u);
    Level.Finalise();
}

// When does the sun stand at the requested elevation inside [FromHour, ToHour]? Solved with the shipping solver,
//    coarse pass plus fine pass — the same approach as the dawn stages in CelestialSkyProof.
float SolveHourForElevation(float Elevation, float FromHour, float ToHour)
{
    CelestialObservation Probe{};
    Probe.Year = 2026; Probe.Month = 9; Probe.Day = 10;
    Probe.UtcOffset = 2.0f; Probe.Latitude = -26.19f; Probe.Longitude = 28.32f;
    float Hour = FromHour, Best = 1e9f;
    for (float H = FromHour; H <= ToHour; H += 0.01f)
    {
        Probe.LocalHours = H;
        const float Residual = std::fabs(CelestialSolver::Solve(Probe).Sun.Elevation - Elevation);
        if (Residual < Best) { Best = Residual; Hour = H; }
    }
    for (float H = Hour - 0.02f; H <= Hour + 0.02f; H += 0.001f)
    {
        Probe.LocalHours = H;
        const float Residual = std::fabs(CelestialSolver::Solve(Probe).Sun.Elevation - Elevation);
        if (Residual < Best) { Best = Residual; Hour = H; }
    }
    return Hour;
}

void WriteFrame(const char* Path, const std::vector<unsigned char>& Rgba)
{
    std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
    for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
    {
        Rgb[I * 3u + 0u] = Rgba[I * 4u + 0u];
        Rgb[I * 3u + 1u] = Rgba[I * 4u + 1u];
        Rgb[I * 3u + 2u] = Rgba[I * 4u + 2u];
    }
    PngWriteShim::WritePng(Path, static_cast<int>(kWidth), static_cast<int>(kHeight), 3, Rgb.data(),
                           static_cast<int>(kWidth) * 3);
    double Sum = 0.0;
    for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
        Sum += (Rgb[I * 3u + 0u] + Rgb[I * 3u + 1u] + Rgb[I * 3u + 2u]) / 3.0;
    std::printf("  wrote %s (mean lum %.1f)\n", Path, Sum / (static_cast<double>(kWidth) * kHeight));
}

// Aim the camera along a sky direction in full 3D: Right = Forward x world-up (the project's camera convention),
//    Up = Right x Forward. The caller passes a unit direction.
void AimAt(const float* Direction, float Forward[3], float Right[3], float Up[3])
{
    Forward[0] = Direction[0]; Forward[1] = Direction[1]; Forward[2] = Direction[2];
    float Rx = Forward[1], Ry = -Forward[0], Rz = 0.0f;
    const float Rl = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
    Right[0] = Rx / Rl; Right[1] = Ry / Rl; Right[2] = Rz / Rl;
    Up[0] = Right[1] * Forward[2] - Right[2] * Forward[1];
    Up[1] = Right[2] * Forward[0] - Right[0] * Forward[2];
    Up[2] = Right[0] * Forward[1] - Right[1] * Forward[0];
}

} // namespace

int main()
{
    std::printf("\nProject Zero showcase: the sky the game renders, five aimed frames\n");
    for (int I = 0; I < 70; ++I) std::putchar('='); std::printf("\n\n");

    SceneStructure Level;
    std::deque<GeometryStructure> Owned;
    BuildScene(Level, Owned);

    FidelityClassifier Classifier;
    const FidelityCriteria Criteria = Classifier.ConstructCriteria(FidelityCategory::StandardFidelity);
    const CelestialBudget Budget = CelestialTier::BudgetFor(Criteria);

    CelestialSequence Sky;
    Sky.Prepare();
    std::printf("  %u stars catalogued\n", Sky.Stars().QuerySourceCount());
    TextureIndex Textures;
    uint32_t AtlasSlots[kMoonAtlasCount];
    for (uint32_t M = 0u; M < kMoonAtlasCount; ++M)
    {
        char Path[128];
        std::snprintf(Path, sizeof(Path), "%s%s", kMoonTextureDirectory, kMoonAtlas[M].File);
        AtlasSlots[M] = Textures.RegisterPath(Path, /*Linear=*/false);
    }
    std::vector<std::string> AtlasReport;
    if (Textures.Decode(0u, &AtlasReport) != 0u) { std::printf("  moon atlas failed to decode\n"); return 2; }
    Sky.AssignMoonAtlas(AtlasSlots, Textures);
    for (uint32_t E = 0u; E < kCelestialEntityCount; ++E) Sky.Shown[E] = true;

    const float TickOrigin[3] = { 0.0f, 0.0f, 2.0f };
    const float Eye[3]        = { 0.0f, -6.0f, 1.7f };
    constexpr float kHalfFov  = 55.0f * 3.14159265f / 180.0f;
    constexpr float kDeg      = 3.14159265f / 180.0f;

    auto TickTo = [&](float Hour, int Day = 10)
    {
        Sky.Observation.Year = 2026; Sky.Observation.Month = 9; Sky.Observation.Day = Day;
        Sky.Observation.LocalHours = Hour; Sky.Observation.UtcOffset = 2.0f;
        Sky.Observation.Latitude = -26.19f; Sky.Observation.Longitude = 28.32f;
        Sky.Tick(0.0f, TickOrigin, 0.0f);
    };

    // ── 1. Morning: the sun at +10 deg, faced ────────────────────────────────────────────────────
    {
        TickTo(SolveHourForElevation(10.0f, 6.0f, 10.0f));
        float F[3] = { 0.0f, 1.0f, 0.0f };
        {
            const float Hx = Sky.Frame().Sun.Direction[0], Hy = Sky.Frame().Sun.Direction[1];
            const float Hl = std::sqrt(Hx * Hx + Hy * Hy);
            if (Hl > 1e-6f) { F[0] = Hx / Hl; F[1] = Hy / Hl; F[2] = 0.0f; }
        }
        float R[3], U[3];
        AimAt(F, F, R, U);
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Frame(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, F, R, U, kHalfFov, kWidth, kHeight, Frame.data(), MeanLuminance)) return 2;
        std::printf("  morning: sun el %+.2f az %.1f at %.2fh\n",
                    static_cast<double>(Sky.Frame().Sun.Elevation), static_cast<double>(Sky.Frame().Sun.Azimuth),
                    static_cast<double>(Sky.Observation.LocalHours));
        WriteFrame("Diagnostics/ProjectZero_Showcase_Morning.png", Frame);
    }

    // ── 2. Sunset: the sun at +1.5 deg, faced ────────────────────────────────────────────────────
    {
        TickTo(SolveHourForElevation(1.5f, 15.0f, 19.0f));
        float F[3] = { 0.0f, 1.0f, 0.0f };
        {
            const float Hx = Sky.Frame().Sun.Direction[0], Hy = Sky.Frame().Sun.Direction[1];
            const float Hl = std::sqrt(Hx * Hx + Hy * Hy);
            if (Hl > 1e-6f) { F[0] = Hx / Hl; F[1] = Hy / Hl; F[2] = 0.0f; }
        }
        float R[3], U[3];
        AimAt(F, F, R, U);
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Frame(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, F, R, U, kHalfFov, kWidth, kHeight, Frame.data(), MeanLuminance)) return 2;
        std::printf("  sunset: sun el %+.2f az %.1f at %.2fh\n",
                    static_cast<double>(Sky.Frame().Sun.Elevation), static_cast<double>(Sky.Frame().Sun.Azimuth),
                    static_cast<double>(Sky.Observation.LocalHours));
        WriteFrame("Diagnostics/ProjectZero_Showcase_Sunset.png", Frame);
    }

    // ── 3. Night, linked: slot 0 as Prepare() leaves it — Luna following the solved lunar frame ──
    {
        // 26 Sep, full moon. The aim reads the SOLVED direction: a linked slot ignores Azimuth/Elevation.
        TickTo(22.0f, 26);
        float F[3], R[3], U[3];
        AimAt(Sky.Frame().Moon.Direction, F, R, U);
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Frame(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, F, R, U, kHalfFov, kWidth, kHeight, Frame.data(), MeanLuminance)) return 2;
        std::printf("  linked: moon el %+.2f az %.1f illum %.2f at 22h on Sep 26 (slot 0 as Prepare leaves it)\n",
                    static_cast<double>(Sky.Frame().Moon.Elevation),
                    static_cast<double>(Sky.Frame().Moon.Azimuth),
                    static_cast<double>(Sky.Frame().MoonIllumination));
        WriteFrame("Diagnostics/ProjectZero_Showcase_NightLinked.png", Frame);
    }

    // ── 4. Night, placed: the roster driven the way the reference panel drives it ────────────────
    {
        // The MoonRenderProof arrangement, stated plainly: slot 0 unlinked and put at az 0 / el 25 at 2 deg,
        //    slot 1 (Ember) at az 14 / el 15 at 3 deg, slots 2-3 as Prepare parks them (hidden). The stars stay
        //    on — the moon proof hides them to keep its counts honest, but this frame wants moons and stars
        //    together, and the two coexist (verified: the moon draws identically with stars on or off).
        TickTo(22.0f, 10);
        Sky.MoonSlots[0].FollowSky = false;
        Sky.MoonSlots[0].Azimuth = 0.0f; Sky.MoonSlots[0].Elevation = 25.0f;
        Sky.MoonSlots[0].Size = 2.0f; Sky.MoonSlots[0].Phase = 0.5f;
        Sky.MoonSlots[1].Preset = 1u; Sky.MoonSlots[1].Visible = true;
        Sky.MoonSlots[1].FollowSky = false;
        Sky.MoonSlots[1].Azimuth = 14.0f; Sky.MoonSlots[1].Elevation = 15.0f;
        Sky.MoonSlots[1].Size = 3.0f; Sky.MoonSlots[1].Phase = 0.62f;
        const float Az = Sky.MoonSlots[0].Azimuth * kDeg, El = Sky.MoonSlots[0].Elevation * kDeg;
        const float Aim[3] = { std::sin(Az) * std::cos(El), std::cos(Az) * std::cos(El), std::sin(El) };
        float F[3], R[3], U[3];
        AimAt(Aim, F, R, U);
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Frame(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, F, R, U, kHalfFov, kWidth, kHeight, Frame.data(), MeanLuminance)) return 2;
        std::printf("  placed: Luna 2 deg full + Ember 3 deg gibbous at 22h on Sep 10 (stars on)\n");
        WriteFrame("Diagnostics/ProjectZero_Showcase_NightPlaced.png", Frame);
    }

    // ── 5. Morning, local: the parked volume with its enable flipped ────────────────────────────
    {
        TickTo(11.0f, 10);
        Sky.LocalCloud.Enabled = true;
        float Aim[3] = { Sky.LocalCloud.Centre[0] - Eye[0],
                         Sky.LocalCloud.Centre[1] - Eye[1],
                         Sky.LocalCloud.Centre[2] - Eye[2] };
        {
            const float Al = std::sqrt(Aim[0] * Aim[0] + Aim[1] * Aim[1] + Aim[2] * Aim[2]);
            Aim[0] /= Al; Aim[1] /= Al; Aim[2] /= Al;
        }
        float F[3], R[3], U[3];
        AimAt(Aim, F, R, U);
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Frame(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, F, R, U, kHalfFov, kWidth, kHeight, Frame.data(), MeanLuminance)) return 2;
        std::printf("  local: parked volume enabled at 11h on Sep 10 (box centre %.0f %.0f %.0f)\n",
                    (double)Sky.LocalCloud.Centre[0], (double)Sky.LocalCloud.Centre[1],
                    (double)Sky.LocalCloud.Centre[2]);
        WriteFrame("Diagnostics/ProjectZero_Showcase_LocalCloud.png", Frame);
    }

    std::printf("\n  showcase rendered\n");
    return 0;
}
