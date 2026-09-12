//============================================================================================================================================
// 📦 Scratchpad/CelestialSkyProof.cpp — the project's own sky, rendered at four times of day
//============================================================================================================================================
// Celestial port, step 1. Renders the proof ground through the real VisibilityRaster, driven by a live
//    CelestialSequence (prepare → tick → ApplyTo) exactly as GameExecution drives it, with the shipping star
//    catalogue and the shipping moon atlas, and writes one sheet per time of day.
//
// FIDELITY CONTRACT (CLAUDE.md: proof images show what the project does). Every pixel here is produced by
//    SHIPPING translation units fed through PROJECT wiring: the sequence is prepared, not hand-built, so the
//    22.0 sun intensity, the tier budgets (via CelestialTier::BudgetFor), the star catalogue, the moon roster
//    and the twilight settings are the project's own — there is no settings struct in this file that bypasses
//    ApplyTo. The one deliberate deviation is declared, not hidden: the project DISPLAYS the ReSTIR kernel's
//    image, which needs a GPU, and this proof renders the GI-off raster, the project's own fallback path, which
//    runs on a CPU. The kernel is held to the same models by the transcription parity proofs and the structural
//    gates instead (SkyKernelParityProof, MoonRenderProof §1, CheckPostKernel.sh).
//
// The gate reads the numbers below, not the pictures — the sheets are for the eye, the assertions are the proof:
//    • noon must be brighter than dusk, and dusk brighter than night (a sky that does not track the sun is the
//      failure mode a screenshot hides),
//    • the noon zenith must be blue-dominant (the Rayleigh signature, asserted on linear radiance),
//    • night must be essentially black rather than the old flat blue constant,
//    • the night sheet must carry the star field (on/off census), while the noon sheet must not (the daylight
//      gate, proved end to end rather than trusted).

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

constexpr uint32_t kWidth  = 480;
constexpr uint32_t kHeight = 320;

int Failures = 0;

void Require(const char* Label, bool Ok, const char* Detail)
{
    if (!Ok) ++Failures;
    std::printf("  %-56s %s   %s\n", Label, Ok ? "PASS" : "FAIL", Detail);
}

// A ground plane and a couple of blocks — enough that the sky has a silhouette to sit behind, which is what makes
//    a wrong horizon obvious. The Cornell box is a closed room and would show no sky at all.
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
    //    it SKIP any triangle with a vertex nearer than that, and it cannot split one. A single -40..40 plane has
    //    corners behind the camera, so the whole ground vanished and rendered as sky-black — which is exactly
    //    what the first run of this proof showed. Tiles keep every triangle wholly in front of the eye.
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

struct Statistics
{
    double Mean[3]{};
    double ZenithRgb[3]{};   // top-centre pixel band
    double HorizonRgb[3]{};  // just above the horizon line
};

Statistics Measure(const std::vector<unsigned char>& Sheet)
{
    Statistics Out{};
    double Sum[3] = {};
    for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
        for (int C = 0; C < 3; ++C) Sum[C] += Sheet[I * 4u + static_cast<size_t>(C)];
    for (int C = 0; C < 3; ++C) Out.Mean[C] = Sum[C] / (static_cast<double>(kWidth) * kHeight);

    // Zenith band: the top 6% of rows, centre half.
    double Z[3] = {}; uint32_t Zn = 0;
    for (uint32_t Y = 0; Y < kHeight * 6u / 100u; ++Y)
        for (uint32_t X = kWidth / 4u; X < kWidth * 3u / 4u; ++X, ++Zn)
            for (int C = 0; C < 3; ++C) Z[C] += Sheet[(static_cast<size_t>(Y) * kWidth + X) * 4u + static_cast<size_t>(C)];
    for (int C = 0; C < 3; ++C) Out.ZenithRgb[C] = Zn ? Z[C] / Zn : 0.0;

    // Horizon band: rows just ABOVE the horizon line, so this is sky against sky. An earlier version sampled a
    //    band that included ground, which meant a direction-blind sky (every pixel evaluated at the zenith) still
    //    passed the gradient check — the difference it measured was sky-vs-ground, not zenith-vs-horizon.
    double H[3] = {}; uint32_t Hn = 0;
    for (uint32_t Y = kHeight * 38u / 100u; Y < kHeight * 43u / 100u; ++Y)
        for (uint32_t X = 0; X < kWidth; ++X, ++Hn)
            for (int C = 0; C < 3; ++C) H[C] += Sheet[(static_cast<size_t>(Y) * kWidth + X) * 4u + static_cast<size_t>(C)];
    for (int C = 0; C < 3; ++C) Out.HorizonRgb[C] = Hn ? H[C] / Hn : 0.0;
    return Out;
}

} // namespace

int main()
{
    std::printf("\nCelestial sky through the GI-off raster (VisibilityRaster)\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    SceneStructure Level;
    std::deque<GeometryStructure> Owned;
    BuildScene(Level, Owned);

    // Tier budgets come from the ladder, mapped through the project's own tier table — never from a literal here.
    FidelityClassifier Classifier;
    const FidelityCriteria Criteria = Classifier.ConstructCriteria(FidelityCategory::StandardFidelity);
    const CelestialBudget Budget = CelestialTier::BudgetFor(Criteria);

    // The project's own sky: prepared, not hand-built. Prepare loads the shipping catalogue, so the night sheet
    //    below carries the same stars the app shows; the atlas decodes through the real index in GameExecution
    //    order, so the moons ride with their shipping albedos; every entity is shown, which is the project's
    //    default (EditorInstance::Visible is true until the outliner hides it).
    CelestialSequence Sky;
    Sky.Prepare();
    {
        char StarDetail[96];
        std::snprintf(StarDetail, sizeof(StarDetail), "%u stars catalogued", Sky.Stars().QuerySourceCount());
        Require("Prepare loads the shipping star catalogue", Sky.Stars().QuerySourceCount() > 0u, StarDetail);
    }
    TextureIndex Textures;
    uint32_t AtlasSlots[kMoonAtlasCount];
    for (uint32_t M = 0u; M < kMoonAtlasCount; ++M)
    {
        char Path[128];
        std::snprintf(Path, sizeof(Path), "%s%s", kMoonTextureDirectory, kMoonAtlas[M].File);
        AtlasSlots[M] = Textures.RegisterPath(Path, /*Linear=*/false);
    }
    std::vector<std::string> AtlasReport;
    const uint32_t AtlasFailures = Textures.Decode(0u, &AtlasReport);
    {
        char AssetDetail[128];
        std::snprintf(AssetDetail, sizeof(AssetDetail), "decoded %u textures, %u failures",
                      Textures.QueryCount(), AtlasFailures);
        Require("all six moon albedos decode", AtlasFailures == 0u && Textures.QueryCount() == 6u, AssetDetail);
    }
    Sky.AssignMoonAtlas(AtlasSlots, Textures);
    for (uint32_t E = 0u; E < kCelestialEntityCount; ++E) Sky.Shown[E] = true;

    const float TickOrigin[3] = { 0.0f, 0.0f, 2.0f };

    struct Moment { const char* Name; float Hour; };
    const Moment Moments[] = {
        { "Dawn",  6.5f },
        { "Noon", 12.0f },
        { "Dusk", 17.8f },
        { "Night",22.0f },
    };

    std::printf("  tier Standard: atmosphere %u view samples x %u light samples\n\n",
                Criteria.AtmosphereSampleCount, Criteria.AtmosphereLightSampleCount);
    std::printf("  %-7s %8s %9s   %-22s %-22s\n", "moment", "sun el", "mean lum", "zenith RGB", "horizon RGB");

    Statistics Stats[4];
    const float Eye[3]     = { 0.0f, -6.0f, 1.7f };
    const float Forward[3] = { 0.0f,  1.0f, 0.0f };
    const float Right[3]   = { 1.0f,  0.0f, 0.0f };
    const float Up[3]      = { 0.0f,  0.0f, 1.0f };
    for (int M = 0; M < 4; ++M)
    {
        // Each moment starts from the full sky: the air-only re-render below hides bodies and weather on the
        //    shared sequence, and without this reset every sheet after Dawn would render bare.
        for (uint32_t E = 0u; E < kCelestialEntityCount; ++E) Sky.Shown[E] = true;
        // The clock is the only input; the sun, the stars' rotation, the twilight and the moons all come out of
        //    the tick, and the raster is fed by ApplyTo — the GameExecution order, with nothing hand-set.
        Sky.Observation.Year = 2026; Sky.Observation.Month = 9; Sky.Observation.Day = 10;
        Sky.Observation.LocalHours = Moments[M].Hour; Sky.Observation.UtcOffset = 2.0f;
        Sky.Observation.Latitude = -26.19f; Sky.Observation.Longitude = 28.32f;
        Sky.Tick(0.0f, TickOrigin, 0.0f);

        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);

        std::vector<unsigned char> Sheet(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;

        if (!Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                           kWidth, kHeight, Sheet.data(), MeanLuminance))
        {
            std::printf("  render failed for %s\n", Moments[M].Name);
            return 2;
        }

        // The sheet above is the project's sky — moons, stars and weather on. The moment pins are about the
        //    AIR (the Rayleigh signature, the tracking), so the stats are measured on an air-only re-render with
        //    the bodies and the weather hidden — the same Bare rule the terminator section uses below. The sheet
        //    shows the sky; the numbers prove the air; the weather section at the end proves the clouds.
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = false;
        VisibilityRaster AirRaster;
        Sky.ApplyTo(AirRaster, Budget);
        std::vector<unsigned char> Air(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanAir = 0.0;
        if (!AirRaster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                              kWidth, kHeight, Air.data(), MeanAir))
        {
            std::printf("  air render failed for %s\n", Moments[M].Name);
            return 2;
        }
        (void)MeanAir;

        Stats[M] = Measure(Air);
        std::printf("  %-7s %+7.2f %9.2f   %5.0f %5.0f %5.0f      %5.0f %5.0f %5.0f\n",
                    Moments[M].Name, static_cast<double>(Sky.Frame().Sun.Elevation),
                    (Stats[M].Mean[0] + Stats[M].Mean[1] + Stats[M].Mean[2]) / 3.0,
                    Stats[M].ZenithRgb[0], Stats[M].ZenithRgb[1], Stats[M].ZenithRgb[2],
                    Stats[M].HorizonRgb[0], Stats[M].HorizonRgb[1], Stats[M].HorizonRgb[2]);

        std::string Path = std::string("Diagnostics/Celestial_01_TimeOfDay_") + Moments[M].Name + ".png";
        std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
        for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
        {
            Rgb[I * 3u + 0u] = Sheet[I * 4u + 0u];
            Rgb[I * 3u + 1u] = Sheet[I * 4u + 1u];
            Rgb[I * 3u + 2u] = Sheet[I * 4u + 2u];
        }
        PngWriteShim::WritePng(Path.c_str(), static_cast<int>(kWidth), static_cast<int>(kHeight), 3, Rgb.data(),
                               static_cast<int>(kWidth) * 3);
        std::printf("           wrote %s\n", Path.c_str());
    }

    // ── The star field reaches the image (and only at night) ─────────────────────────────────────────────────
    // The Night sheet above rendered with stars shown; re-render it hidden and count what changes. Then the same
    //    pair at noon, where the daylight gate must hold every pixel equal. This is the end-to-end form of the
    //    question "are the stars showing as they should": present on a dark sky, absent on a bright one.
    {
        auto Census = [&](float Hour, bool& Rendered) -> uint32_t
        {
            Rendered = false;
            Sky.Observation.LocalHours = Hour;
            Sky.Tick(0.0f, TickOrigin, 0.0f);
            std::vector<unsigned char> On(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
            std::vector<unsigned char> Off(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
            double MeanLuminance = 0.0;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = true;
            {
                VisibilityRaster Raster;
                Sky.ApplyTo(Raster, Budget);
                if (!Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                                   kWidth, kHeight, On.data(), MeanLuminance)) return 0u;
            }
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
            {
                VisibilityRaster Raster;
                Sky.ApplyTo(Raster, Budget);
                if (!Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                                   kWidth, kHeight, Off.data(), MeanLuminance)) return 0u;
            }
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = true;
            Rendered = true;
            uint32_t Changed = 0u;
            for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
            {
                int D = 0;
                for (int C = 0; C < 3; ++C)
                {
                    const int A = On[I * 4u + static_cast<size_t>(C)];
                    const int B = Off[I * 4u + static_cast<size_t>(C)];
                    if (A > B + D) D = A - B;
                }
                if (D > 12) ++Changed;
            }
            return Changed;
        };
        bool NightRendered = false, NoonRendered = false;
        const uint32_t NightStars = Census(22.0f, NightRendered);
        const uint32_t NoonStars = Census(12.0f, NoonRendered);
        char CensusDetail[128];
        std::snprintf(CensusDetail, sizeof(CensusDetail), "%u pixels differ at night, %u at noon",
                      NightStars, NoonStars);
        std::printf("\n  star census: %s\n", CensusDetail);
        Require("both census pairs render", NightRendered && NoonRendered, "VisibilityRaster.Render");
        Require("stars reach the night image", NightStars > 20u && NightStars < 30000u, CensusDetail);
        Require("daylight gates every star out", NoonStars == 0u, CensusDetail);
    }

    // ── The dawn transition ────────────────────────────────────────────────────────────────────────────────────
    // Sampled by sun elevation, not by the clock: every twilight term is keyed to elevation, and at this latitude
    //    the interesting range (-16 to +2 deg) is only about 70 minutes wide. The hours below are SOLVED, not
    //    chosen — CelestialSolver is asked when each elevation occurs, and the sequence ticks to that hour — so
    //    the sun stands at its true dawn azimuth and the camera faces it, the way a photographer would.
    std::printf("\n  dawn transition (sampled by sun elevation)\n");
    std::printf("  %-22s %8s %9s   %-22s %s\n", "stage", "sun el", "mean lum", "horizon RGB", "line sharpness");

    struct Stage { const char* Name; float Elevation; };
    const Stage Stages[] = {
        { "astronomical -15", -15.0f },
        { "nautical -10",     -10.0f },
        { "civil -5.5",        -5.5f },   // the white line switches on here
        { "line rising -4",    -4.0f },
        { "line peak -2",      -2.0f },
        { "horizon -0.5",      -0.5f },   // handing over to the disc
        { "sunrise +1",         1.0f },
        { "risen +4",           4.0f },
    };

    double PreviousLine = -1.0;
    double LinePeak = 0.0; int LinePeakStage = -1;
    for (int K = 0; K < 8; ++K)
    {
        // When does the sun stand at the requested elevation? Scan the morning with the shipping solver; the
        //    ephemeris is smooth, so a coarse pass plus a fine pass lands within a few hundredths of a degree.
        float StageHour = 6.0f;
        {
            CelestialObservation Probe{};
            Probe.Year = 2026; Probe.Month = 9; Probe.Day = 10;
            Probe.UtcOffset = 2.0f; Probe.Latitude = -26.19f; Probe.Longitude = 28.32f;
            float Best = 1e9f;
            for (float H = 3.0f; H <= 9.0f; H += 0.01f)
            {
                Probe.LocalHours = H;
                const float Residual = std::fabs(CelestialSolver::Solve(Probe).Sun.Elevation - Stages[K].Elevation);
                if (Residual < Best) { Best = Residual; StageHour = H; }
            }
            for (float H = StageHour - 0.02f; H <= StageHour + 0.02f; H += 0.001f)
            {
                Probe.LocalHours = H;
                const float Residual = std::fabs(CelestialSolver::Solve(Probe).Sun.Elevation - Stages[K].Elevation);
                if (Residual < Best) { Best = Residual; StageHour = H; }
            }
            if (Best > 0.15f)
            {
                std::printf("  stage %d: no hour reaches %+.1f deg (best residual %.2f)\n",
                            K, static_cast<double>(Stages[K].Elevation), static_cast<double>(Best));
                return 2;
            }
        }

        Sky.Observation.Year = 2026; Sky.Observation.Month = 9; Sky.Observation.Day = 10;
        Sky.Observation.LocalHours = StageHour; Sky.Observation.UtcOffset = 2.0f;
        Sky.Observation.Latitude = -26.19f; Sky.Observation.Longitude = 28.32f;
        Sky.Tick(0.0f, TickOrigin, 0.0f);

        // Face the solved sun: its horizontal direction is the camera forward, and Right = Forward x WorldUp is
        //    the project's camera convention (see RayGeneration.slang). The dawn then sits frame-centre, which is
        //    what keeps the hairline measurement below pointed at the twilight instead of at empty sky.
        float FaceForward[3] = { 0.0f, 1.0f, 0.0f };
        {
            const float Hx = Sky.Frame().Sun.Direction[0], Hy = Sky.Frame().Sun.Direction[1];
            const float Hl = std::sqrt(Hx * Hx + Hy * Hy);
            if (Hl > 1e-6f) { FaceForward[0] = Hx / Hl; FaceForward[1] = Hy / Hl; FaceForward[2] = 0.0f; }
        }
        const float FaceRight[3] = { FaceForward[1], -FaceForward[0], 0.0f };
        const float FaceUp[3] = { 0.0f, 0.0f, 1.0f };
        constexpr float kHalfFov = 55.0f * 3.14159265f / 180.0f;

        // Two renders: the sheet keeps the project's moons, stars and weather (it is the project's dawn),
        //    while the line is measured on a bodies-and-weather-off render — the hairline is atmosphere, and a
        //    moon limb or a cloud band crossing the rows would otherwise be measured as twilight. Same sky, same
        //    hour; only the outliner differs.
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        std::vector<unsigned char> Sheet(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        if (!Raster.Render(Level, Eye, FaceForward, FaceRight, FaceUp, kHalfFov,
                           kWidth, kHeight, Sheet.data(), MeanLuminance)) return 2;

        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = false;
        VisibilityRaster BareRaster;
        Sky.ApplyTo(BareRaster, Budget);
        std::vector<unsigned char> Bare(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        if (!BareRaster.Render(Level, Eye, FaceForward, FaceRight, FaceUp, kHalfFov,
                               kWidth, kHeight, Bare.data(), MeanLuminance)) return 2;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = true;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = true;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = true;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = true;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = true;

        const Statistics St = Measure(Sheet);

        // ⚠️ The line is measured by SHARPNESS, not brightness. An earlier version took the brightest row in the
        //    sky band, which rose monotonically all the way past sunrise — it was measuring daylight, and would
        //    have "passed" with the hairline deleted. A hairline is a local step: a row markedly brighter than the
        //    row a few pixels above it. That signature peaks below the horizon and collapses once the disc is up.
        double Rows[kHeight];
        for (uint32_t Y = 0; Y < kHeight; ++Y)
        {
            double Row = 0.0; uint32_t N = 0;
            for (uint32_t X = kWidth * 3u / 8u; X < kWidth * 5u / 8u; ++X, ++N)
                for (int C = 0; C < 3; ++C) Row += Bare[(static_cast<size_t>(Y) * kWidth + X) * 4u + static_cast<size_t>(C)];
            Rows[Y] = N ? Row / (N * 3.0) : 0.0;
        }
        double BrightestRow = 0.0;
        for (uint32_t Y = kHeight * 40u / 100u; Y < kHeight * 56u / 100u; ++Y)
        {
            const double Step = Rows[Y] - Rows[Y - 4u];
            if (Step > BrightestRow) BrightestRow = Step;
        }
        if (BrightestRow > LinePeak) { LinePeak = BrightestRow; LinePeakStage = K; }

        // The sun's face: the brightest pixel in the sun's box (rows 30-50%, centre quarter) on the
        //    bodies-off render. Below the horizon the box is dark sky — the planet gates the disc, so a bright
        //    box there means the disc leaks through the earth; risen, the box holds the 12x Duke and clips.
        //    Both ends are asserted where they are measured: stage 0 below, stage 7 risen.
        double SunBox = 0.0;
        for (uint32_t Y = kHeight * 30u / 100u; Y < kHeight * 50u / 100u; ++Y)
            for (uint32_t X = kWidth * 3u / 8u; X < kWidth * 5u / 8u; ++X)
            {
                const size_t P = (static_cast<size_t>(Y) * kWidth + X) * 4u;
                const double Lum = (Bare[P + 0u] + Bare[P + 1u] + Bare[P + 2u]) / 3.0;
                if (Lum > SunBox) SunBox = Lum;
            }
        if (K == 0)
        {
            char SunDetail[128];
            std::snprintf(SunDetail, sizeof(SunDetail), "sun-box max %.0f at -15 deg (dark sky, gated disc)", SunBox);
            Require("below the horizon the sun's box is dark", SunBox < 60.0, SunDetail);
        }
        if (K == 7)
        {
            // The sun's face, proven by DIMMING the sun. At full brightness the wash itself reaches 245
            //    in the sun's box (measured on the pre-disc sheet), so a "bright box" assertion would pass
            //    with the disc deleted. Sky wash scales with the sunlight: at 5% brightness the wash
            //    collapses while the 12x Duke stays bright — a bright cluster on a dark field that cannot
            //    be the wash. The max pins the disc (no disc: ~12), the mean pins the dimming (unwired
            //    brightness: the wash stays at 245 and the mean fails).
            const float SavedBrightness = Sky.SkyBrightness;
            Sky.SkyBrightness = 0.05f;
            // The disc, not the weather: at 5% brightness a cloud crossing the sun's box would occult the disc
            //    and fail the seat pin, so the dimmed render hides the decks the way the Bare render does.
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = false;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = false;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = false;
            VisibilityRaster DimRaster;
            Sky.ApplyTo(DimRaster, Budget);
            std::vector<unsigned char> Dim(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
            if (!DimRaster.Render(Level, Eye, FaceForward, FaceRight, FaceUp, kHalfFov,
                                  kWidth, kHeight, Dim.data(), MeanLuminance)) return 2;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = true;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = true;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = true;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = true;
            Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = true;
            Sky.SkyBrightness = SavedBrightness;
            // The seat window holds the 3 px sun (frame centre, v~0.43 at +4 deg) with room for its
            //    soft edge; the field is the box minus the seat. Without the disc the two maxima agree (both
            //    are twilight); the disc lifts the seat alone — so the field maximum is the measured ceiling
            //    the disc must clear, printed every run so the margin is visible, not buried.
            double DimMax = 0.0, DimSeat = 0.0, DimField = 0.0;
            for (uint32_t Y = kHeight * 30u / 100u; Y < kHeight * 50u / 100u; ++Y)
                for (uint32_t X = kWidth * 3u / 8u; X < kWidth * 5u / 8u; ++X)
                {
                    const size_t P = (static_cast<size_t>(Y) * kWidth + X) * 4u;
                    const double Lum = (Dim[P + 0u] + Dim[P + 1u] + Dim[P + 2u]) / 3.0;
                    if (Lum > DimMax) DimMax = Lum;
                    const bool Seat = (Y >= 132u && Y <= 144u && X >= 234u && X <= 246u);
                    if (Seat) { if (Lum > DimSeat) DimSeat = Lum; }
                    else { if (Lum > DimField) DimField = Lum; }
                }
            char SunDetail[192];
            std::snprintf(SunDetail, sizeof(SunDetail),
                          "dimmed seat %.0f vs field ceiling %.0f (disc lifts the seat alone)",
                          DimSeat, DimField);
            Require("dimmed, the sun still shows its face", DimSeat > DimField + 40.0, SunDetail);

            // The aureole never blows out: the wash 1-2 deg out from the sun compresses below the knee —
            //    the reference's own rule ("normalised to its own peak, so it can never blow out"). The seat
            //    self-locates (the brightest pixel in the sun's box, disc or wash peak); the ring around it
            //    reads the wash. With the shoulder the ring renders 229.5, without it 244.3, and the gate
            //    fails (mutation-proven) — the ring alone is the assertion, since the seat's own maximum
            //    tracks the wash peak on either side and cannot tell the disc's presence.
            double SeatMax = 0.0; uint32_t SeatX = kWidth / 2, SeatY = kHeight * 43u / 100u;
            for (uint32_t Y = kHeight * 30u / 100u; Y < kHeight * 50u / 100u; ++Y)
                for (uint32_t X = kWidth * 3u / 8u; X < kWidth * 5u / 8u; ++X)
                {
                    const size_t P = (static_cast<size_t>(Y) * kWidth + X) * 4u;
                    const double Lum = (Bare[P + 0u] + Bare[P + 1u] + Bare[P + 2u]) / 3.0;
                    if (Lum > SeatMax) { SeatMax = Lum; SeatX = X; SeatY = Y; }
                }
            double RingSum = 0.0, RingN = 0.0;
            for (uint32_t Y = 0; Y < kHeight; ++Y)
                for (uint32_t X = 0; X < kWidth; ++X)
                {
                    const int Dx = static_cast<int>(X) - static_cast<int>(SeatX);
                    const int Dy = static_cast<int>(Y) - static_cast<int>(SeatY);
                    const int D2 = Dx * Dx + Dy * Dy;
                    if (D2 < 36 || D2 >= 144) continue;   // 6-12 px: 1-2 deg out, the disc excluded
                    const size_t P = (static_cast<size_t>(Y) * kWidth + X) * 4u;
                    RingSum += (Bare[P + 0u] + Bare[P + 1u] + Bare[P + 2u]) / 3.0; RingN += 1.0;
                }
            const double RingMean = RingN > 0.0 ? RingSum / RingN : 255.0;
            char AureoleDetail[160];
            std::snprintf(AureoleDetail, sizeof(AureoleDetail), "wash ring %.1f at the self-located seat (capped)",
                          RingMean);
            Require("risen, the aureole never blows out", RingMean < 238.0, AureoleDetail);
        }

        std::printf("  %-22s %+7.1f %9.2f   %5.0f %5.0f %5.0f      %7.1f\n",
                    Stages[K].Name, static_cast<double>(Sky.Frame().Sun.Elevation),
                    (St.Mean[0] + St.Mean[1] + St.Mean[2]) / 3.0,
                    St.HorizonRgb[0], St.HorizonRgb[1], St.HorizonRgb[2], BrightestRow);

        char File[160];
        std::snprintf(File, sizeof(File), "Diagnostics/Celestial_02_Dawn_%d_%s.png", K,
                      Stages[K].Elevation < 0.0f ? "below" : "above");
        std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
        for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
        {
            Rgb[I * 3u + 0u] = Sheet[I * 4u + 0u];
            Rgb[I * 3u + 1u] = Sheet[I * 4u + 1u];
            Rgb[I * 3u + 2u] = Sheet[I * 4u + 2u];
        }
        PngWriteShim::WritePng(File, static_cast<int>(kWidth), static_cast<int>(kHeight), 3, Rgb.data(),
                               static_cast<int>(kWidth) * 3);
        if (K == 7)
        {
            // The stage-7 sheet above now carries weather, so the disc's eye-proof moves to its own sheet: the
            //    bare air-only render (disc, aureole, nothing else), from which the disc closeup is cropped.
            std::vector<unsigned char> AirRgb(static_cast<size_t>(kWidth) * kHeight * 3u);
            for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
            {
                AirRgb[I * 3u + 0u] = Bare[I * 4u + 0u];
                AirRgb[I * 3u + 1u] = Bare[I * 4u + 1u];
                AirRgb[I * 3u + 2u] = Bare[I * 4u + 2u];
            }
            PngWriteShim::WritePng("Diagnostics/Celestial_02_Dawn_7_air.png", static_cast<int>(kWidth),
                                   static_cast<int>(kHeight), 3, AirRgb.data(), static_cast<int>(kWidth) * 3);
        }
        (void)PreviousLine;
    }
    std::printf("           wrote Diagnostics/Celestial_02_Dawn_*.png (8 stages)\n");

    // ── Leaving the atmosphere ─────────────────────────────────────────────────────────────────────────────────
    // Two failures this guards, both reported from orbit. Rays that pass below the horizon must meet a PLANET:
    //    the raster's miss path had no world of its own, so the only ground was whatever finite geometry the
    //    scene held and from altitude that read as a slab hanging in space with stars shining through the earth.
    //    And above the shell the sky must be black — a march with a distance limit but no planet gives neither.
    std::printf("\n  leaving the atmosphere\n");
    std::printf("  %-12s %10s %10s %10s   %s\n", "altitude", "up R", "up G", "up B", "looking down");
    struct Rung { const char* Name; float Height; bool ExpectBlackAbove; };
    const Rung Ladder[] = {
        { "ground 2 m",   2.0f,       false },
        { "top 60 km",    60000.0f,   true  },
        { "ISS 400 km",   400000.0f,  true  },
        { "3000 km",      3000000.0f, true  },
    };
    bool SpaceIsBlack = true, PlanetIsLit = true;
    for (const Rung& R : Ladder)
    {
        AtmosphereMedium Medium{};
        AtmosphereLight  Light{};
        Light.Direction[0] = 0.0f; Light.Direction[1] = 0.6f; Light.Direction[2] = 0.8f;

        const float Up[3]   = { 0.0f, 0.0f, 1.0f };
        const float Down[3] = { 0.0f, 0.3f, -0.954f };
        const AtmosphereSample Above = AtmosphereModel::Integrate(Medium, Light, R.Height, Up, 24u, 8u);
        const AtmosphereSample Below = AtmosphereModel::Integrate(Medium, Light, R.Height, Down, 24u, 8u);

        std::printf("  %-12s %10.5f %10.5f %10.5f   %s\n", R.Name,
                    Above.Radiance[0], Above.Radiance[1], Above.Radiance[2],
                    Below.HitGround ? "planet" : "space");

        const float Sum = Above.Radiance[0] + Above.Radiance[1] + Above.Radiance[2];
        if (R.ExpectBlackAbove && Sum > 1.0e-4f) SpaceIsBlack = false;
        if (R.Height > 100000.0f && !Below.HitGround) PlanetIsLit = false;
    }
    Require("above the atmosphere the sky is black", SpaceIsBlack, "no scattering outside the shell");
    Require("from orbit, looking down finds the planet", PlanetIsLit, "HitGround reported below the horizon");

    std::printf("\n  assertions\n");
    const double MeanNoon  = (Stats[1].Mean[0] + Stats[1].Mean[1] + Stats[1].Mean[2]) / 3.0;
    const double MeanDusk  = (Stats[2].Mean[0] + Stats[2].Mean[1] + Stats[2].Mean[2]) / 3.0;
    const double MeanNight = (Stats[3].Mean[0] + Stats[3].Mean[1] + Stats[3].Mean[2]) / 3.0;

    char Detail[160];
    std::snprintf(Detail, sizeof(Detail), "noon %.1f > dusk %.1f", MeanNoon, MeanDusk);
    Require("the sky tracks the sun: noon brighter than dusk", MeanNoon > MeanDusk, Detail);

    std::snprintf(Detail, sizeof(Detail), "dusk %.1f > night %.1f", MeanDusk, MeanNight);
    Require("dusk brighter than night", MeanDusk > MeanNight, Detail);

    std::snprintf(Detail, sizeof(Detail), "night mean %.2f", MeanNight);
    Require("night is dark, not the old flat blue", MeanNight < 12.0, Detail);

    // ⚠️ The blue-dominance of the sky is asserted on LINEAR radiance further down, not on sheet pixels.
    //    This used to compare 8-bit channels and demanded B > R * 1.15. That is unsound once a filmic curve is
    //    in play: ACES compresses highlights, so two bright channels converge on the way to 8-bit — the same
    //    zenith that reads B/R 2.47 in radiance reads 1.36 through the curve in isolation, and only 1.04 once
    //    the sampled band averages in brighter sky nearer the sun. The physics never changed; the instrument was
    //    measuring the tone curve. All that is checked on pixels here is that the zenith is not washed out.
    std::snprintf(Detail, sizeof(Detail), "zenith B %.0f, R %.0f (linear ratio asserted below)",
                  Stats[1].ZenithRgb[2], Stats[1].ZenithRgb[0]);
    Require("noon zenith has colour at all (not clipped white)",
            Stats[1].ZenithRgb[2] > Stats[1].ZenithRgb[0] && Stats[1].ZenithRgb[2] < 254.0, Detail);

    std::snprintf(Detail, sizeof(Detail), "noon zenith B %.0f vs horizon B %.0f",
                  Stats[1].ZenithRgb[2], Stats[1].HorizonRgb[2]);
    // ⚠️ The directional check is made on LINEAR radiance straight from the model, not on sheet pixels.
    //    An earlier version compared two pixel bands and was worthless: a deliberately direction-blind sky (every
    //    pixel evaluated at the zenith) produced a band difference of exactly 8, the same as the correct sky,
    //    because near the top of the Reinhard curve both bands sit close to saturation and the residual gap was
    //    ground and edge pixels bleeding in rather than Rayleigh. Two very different renderers, one identical
    //    number — a check that cannot separate them is not a check.
    {
        AtmosphereMedium Medium{};
        AtmosphereLight  Light{};
        CelestialObservation At{};
        At.Year = 2026; At.Month = 9; At.Day = 10;
        At.LocalHours = 12.0f; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame Frame = CelestialSolver::Solve(At);
        for (int C = 0; C < 3; ++C) Light.Direction[C] = Frame.Sun.Direction[C];

        const float Zenith[3]  = { 0.0f, 0.0f, 1.0f };
        const float Horizon[3] = { 0.0f, 0.9998f, 0.02f };
        const AtmosphereSample Z = AtmosphereModel::Integrate(Medium, Light, 2.0f, Zenith,
                                       Criteria.AtmosphereSampleCount, Criteria.AtmosphereLightSampleCount);
        const AtmosphereSample H = AtmosphereModel::Integrate(Medium, Light, 2.0f, Horizon,
                                       Criteria.AtmosphereSampleCount, Criteria.AtmosphereLightSampleCount);

        // Zenith is blue-dominant; the horizon has scattered its blue out over the longer path and is far less so.
        const double ZenithRatio  = Z.Radiance[2] / std::fmax(Z.Radiance[0], 1e-9f);
        const double HorizonRatio = H.Radiance[2] / std::fmax(H.Radiance[0], 1e-9f);
        std::snprintf(Detail, sizeof(Detail), "zenith B/R %.2f vs horizon B/R %.2f", ZenithRatio, HorizonRatio);
        Require("the sky is directional: zenith bluer than horizon", ZenithRatio > HorizonRatio * 1.5, Detail);

        // And the two directions must simply not be the same colour, which is what a direction-blind sky is.
        const double Delta = std::fabs(static_cast<double>(Z.Radiance[2]) - static_cast<double>(H.Radiance[2]))
                           / std::fmax(static_cast<double>(Z.Radiance[2]), 1e-9);
        std::snprintf(Detail, sizeof(Detail), "relative blue difference %.1f%%", Delta * 100.0);
        Require("zenith and horizon are not the same radiance", Delta > 0.10, Detail);
    }

    // The white line must peak while the sun is still BELOW the horizon — that is what makes it the pre-sunrise
    //    transition rather than just the sun coming up. Stages 2..5 are the -5.5 to -0.5 window.
    std::snprintf(Detail, sizeof(Detail), "sharpest at stage %d (%.1f deg)", LinePeakStage,
                  LinePeakStage >= 0 ? static_cast<double>(Stages[LinePeakStage].Elevation) : 0.0);
    Require("the horizon edge sharpens before the sun rises", LinePeakStage >= 2 && LinePeakStage <= 5, Detail);

    // ⚠️ Peak LOCATION alone does not prove the line exists: deleting the hairline entirely still leaves the
    //    twilight glow peaking in the same window (measured: 14.9 at -2 deg with the line removed, against 25.2
    //    with it). Only the MAGNITUDE separates a drawn hairline from a smooth gradient, because the line is
    //    ~0.11 deg wide — about a fifth of the solar disc — and a glow simply cannot produce that step.
    std::snprintf(Detail, sizeof(Detail), "peak sharpness %.1f (glow alone reaches only ~15)", LinePeak);
    Require("the white line is a hairline, not just the glow", LinePeak > 20.0, Detail);

    // ── The weather reaches the image ────────────────────────────────────────────────────────────────────────────
    // The layer and the parked volume, end to end: each renders on and off at a stated hour and the census counts
    //    what changes, the way the star census counts the night. The night pair asserts the SIGN, not just the
    //    size — clouds must darken a starry sky, because occlusion wins and nothing glows — and the noon pair is
    //    bounded both ways, so a march that whites out or blacks out fails. The evaluator at the end asserts the
    //    parked box holds real body at 11h, so a reverted ghost (0.6/1.0 averages 0.09) fails without rendering.
    {
        auto RenderPair = [&](float Hour, CelestialEntity Toggled, bool BoxEnable, const float F[3],
                              const float R[3], const float U[3], std::vector<unsigned char>& On,
                              std::vector<unsigned char>& Off) -> bool
        {
            Sky.Observation.LocalHours = Hour;
            Sky.Tick(0.0f, TickOrigin, 0.0f);
            if (BoxEnable) Sky.LocalCloud.Enabled = true;
            Sky.Shown[static_cast<uint32_t>(Toggled)] = true;
            double MeanLuminance = 0.0;
            {
                VisibilityRaster Raster;
                Sky.ApplyTo(Raster, Budget);
                if (!Raster.Render(Level, Eye, F, R, U, 55.0f * 3.14159265f / 180.0f,
                                   kWidth, kHeight, On.data(), MeanLuminance)) return false;
            }
            Sky.Shown[static_cast<uint32_t>(Toggled)] = false;
            {
                VisibilityRaster Raster;
                Sky.ApplyTo(Raster, Budget);
                if (!Raster.Render(Level, Eye, F, R, U, 55.0f * 3.14159265f / 180.0f,
                                   kWidth, kHeight, Off.data(), MeanLuminance)) return false;
            }
            Sky.Shown[static_cast<uint32_t>(Toggled)] = true;
            if (BoxEnable) Sky.LocalCloud.Enabled = false;
            return true;
        };
        auto CountChanged = [&](const std::vector<unsigned char>& On,
                                const std::vector<unsigned char>& Off) -> uint32_t
        {
            uint32_t Changed = 0u;
            for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
            {
                int D = 0;
                for (int C = 0; C < 3; ++C)
                {
                    const int A = On[I * 4u + static_cast<size_t>(C)];
                    const int B = Off[I * 4u + static_cast<size_t>(C)];
                    if (A > B + D) D = A - B;
                }
                if (D > 12) ++Changed;
            }
            return Changed;
        };
        auto MeanLum = [&](const std::vector<unsigned char>& Buf) -> double
        {
            double Sum = 0.0;
            for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
                Sum += (Buf[I * 4u + 0u] + Buf[I * 4u + 1u] + Buf[I * 4u + 2u]) / 3.0;
            return Sum / (static_cast<double>(kWidth) * kHeight);
        };
        auto WriteSheet = [&](const char* Path, const std::vector<unsigned char>& Buf)
        {
            std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
            for (size_t I = 0; I < static_cast<size_t>(kWidth) * kHeight; ++I)
            {
                Rgb[I * 3u + 0u] = Buf[I * 4u + 0u];
                Rgb[I * 3u + 1u] = Buf[I * 4u + 1u];
                Rgb[I * 3u + 2u] = Buf[I * 4u + 2u];
            }
            PngWriteShim::WritePng(Path, static_cast<int>(kWidth), static_cast<int>(kHeight), 3, Rgb.data(),
                                   static_cast<int>(kWidth) * 3);
        };

        std::vector<unsigned char> On(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        std::vector<unsigned char> Off(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        bool Rendered = RenderPair(12.0f, CelestialEntity::CloudLayer, false, Forward, Right, Up, On, Off);
        const uint32_t NoonChanged = Rendered ? CountChanged(On, Off) : 0u;
        const double NoonCloudy = Rendered ? MeanLum(On) : 0.0;
        const double NoonClear = Rendered ? MeanLum(Off) : 0.0;
        // No sheet for the noon pair: its on-render is pixel-identical to the Noon moment sheet above (same
        //    sky, same hour, same camera — verified by hash), so Celestial_01_TimeOfDay_Noon.png IS its eye-proof.

        // The box-aimed camera: Forward along eye-to-box-centre, Right = Forward x world-up — the showcase's
        //    AimAt convention, inlined, so this frame and the showcase's local frame agree by construction.
        float BoxF[3] = { Sky.LocalCloud.Centre[0] - Eye[0],
                          Sky.LocalCloud.Centre[1] - Eye[1],
                          Sky.LocalCloud.Centre[2] - Eye[2] };
        {
            const float Bl = std::sqrt(BoxF[0] * BoxF[0] + BoxF[1] * BoxF[1] + BoxF[2] * BoxF[2]);
            BoxF[0] /= Bl; BoxF[1] /= Bl; BoxF[2] /= Bl;
        }
        float BoxR[3] = { BoxF[1], -BoxF[0], 0.0f };
        {
            const float Rl = std::sqrt(BoxR[0] * BoxR[0] + BoxR[1] * BoxR[1]);
            BoxR[0] /= Rl; BoxR[1] /= Rl;
        }
        const float BoxU[3] = { BoxR[1] * BoxF[2] - BoxR[2] * BoxF[1],
                                BoxR[2] * BoxF[0] - BoxR[0] * BoxF[2],
                                BoxR[0] * BoxF[1] - BoxR[1] * BoxF[0] };
        const bool BoxRendered = RenderPair(11.0f, CelestialEntity::LocalCloud, true, BoxF, BoxR, BoxU, On, Off);
        const uint32_t BoxChanged = BoxRendered ? CountChanged(On, Off) : 0u;
        if (BoxRendered) WriteSheet("Diagnostics/Celestial_03_Cloud_Box.png", On);
        Rendered = Rendered && BoxRendered;

        const bool NightRendered = RenderPair(22.0f, CelestialEntity::CloudLayer, false, Forward, Right, Up,
                                              On, Off);
        const uint32_t NightChanged = NightRendered ? CountChanged(On, Off) : 0u;
        const double NightCloudy = NightRendered ? MeanLum(On) : 0.0;
        const double NightClear = NightRendered ? MeanLum(Off) : 0.0;
        Rendered = Rendered && NightRendered;

        // LocalDensity honours the parked flag, and the box pair above restored it — enable for the probe.
        Sky.LocalCloud.Enabled = true;
        double BodySum = 0.0;
        int BodyN = 0;
        for (int Z = -1; Z <= 1; ++Z)
            for (int Y = -1; Y <= 1; ++Y)
                for (int X = -1; X <= 1; ++X)
                {
                    const float P[3] = { Sky.LocalCloud.Centre[0] + X * 50.0f,
                                         Sky.LocalCloud.Centre[1] + Y * 50.0f,
                                         Sky.LocalCloud.Centre[2] + Z * 25.0f };
                    BodySum += VolumetricMedia::LocalDensity(Sky.LocalCloud, Sky.Wind, P, 11.0f * 3600.0f);
                    ++BodyN;
                }
        const double BodyMean = BodySum / BodyN;

        std::printf("\n  cloud census: noon %u changed (cloudy %.1f vs clear %.1f), box %u changed,\n"
                    "                night %u changed (cloudy %.2f vs clear %.2f), box body %.3f at 11h\n",
                    NoonChanged, NoonCloudy, NoonClear, BoxChanged, NightChanged, NightCloudy, NightClear,
                    BodyMean);
        Require("all three cloud pairs render", Rendered && NightRendered, "VisibilityRaster.Render");
        std::snprintf(Detail, sizeof(Detail), "%u pixels differ at noon", NoonChanged);
        Require("the layer reaches the noon image", NoonChanged > 5000u && NoonChanged < 150000u, Detail);
        std::snprintf(Detail, sizeof(Detail), "cloudy noon mean %.1f (clear %.1f)", NoonCloudy, NoonClear);
        Require("noon clouds neither white out nor black out", NoonCloudy > 60.0 && NoonCloudy < 220.0, Detail);
        std::snprintf(Detail, sizeof(Detail), "%u pixels differ with the box", BoxChanged);
        Require("the parked volume reaches the image", BoxChanged > 1000u && BoxChanged < 100000u, Detail);
        std::snprintf(Detail, sizeof(Detail), "cloudy %.2f vs clear %.2f at 22h", NightCloudy, NightClear);
        Require("night clouds darken the sky", NightRendered && NightCloudy < NightClear, Detail);
        // The 22h patch is a veil, not a lid: it halves the airglow (0.08 vs 0.16) without a single pixel
        //    crossing the D>12 census — no dense core sits over a star. So the pin asserts the veil (the ratio),
        //    floored against a march that goes black (which would also halve, to zero).
        std::snprintf(Detail, sizeof(Detail), "cloudy/clear ratio %.2f at %.2f LSB", NightCloudy / NightClear,
                      NightCloudy);
        Require("night clouds veil the airglow", NightCloudy < NightClear * 0.75 && NightCloudy > 0.01, Detail);
        std::snprintf(Detail, sizeof(Detail), "mean box density %.3f at 11h (a ghost reads ~0.09)", BodyMean);
        Require("the parked box holds real body at 11h", BodyMean > 0.30, Detail);
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the sky behaves" : "  THE SKY DOES NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
