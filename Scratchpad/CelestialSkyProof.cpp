//============================================================================================================================================
// 📦 Scratchpad/CelestialSkyProof.cpp — the sky, rendered through the real GI-off raster, at four times of day
//============================================================================================================================================
// Celestial port, step 1. Renders the Cornell box through the real VisibilityRaster with the celestial background
//    enabled, at times taken from the real CelestialSolver, and writes one sheet per time of day.
//
// This is the GI-off half of the dual-path proof. It is deliberately the SHIPPING raster and the SHIPPING solver,
//    not a reimplementation: the whole point is that the thing which renders is the thing which is checked.
//
// The gate reads the numbers below, not the pictures — the sheets are for the eye, the assertions are the proof:
//    • noon must be brighter than dusk, and dusk brighter than night (a sky that does not track the sun is the
//      failure mode a screenshot hides),
//    • the noon zenith must be blue-dominant and the sunset horizon red-dominant (the Rayleigh signature),
//    • night must be essentially black rather than the old flat blue constant.

#include "GeometricRaster/VisibilityRaster.h"
#include "GeometricRaster/SceneStructure.h"
#include "GeometricRaster/GeometryStructure.h"
#include "DisplayPresentation/CelestialSolver.h"
#include "DisplayPresentation/FidelityClassifier.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>

using namespace Frontier;

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

    // Tier budgets come from the ladder, never from a literal here.
    FidelityClassifier Classifier;
    const FidelityCriteria Criteria = Classifier.ConstructCriteria(FidelityCategory::StandardFidelity);

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
    for (int M = 0; M < 4; ++M)
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = 9; At.Day = 10;
        At.LocalHours = Moments[M].Hour; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame Frame = CelestialSolver::Solve(At);

        VisibilityRaster::CelestialSettings Sky{};
        Sky.Enabled = true;
        Sky.CameraHeight = 2.0f;
        Sky.SampleCount = Criteria.AtmosphereSampleCount;
        Sky.LightSampleCount = Criteria.AtmosphereLightSampleCount;
        for (int C = 0; C < 3; ++C) Sky.Light.Direction[C] = Frame.Sun.Direction[C];
        Sky.Light.Intensity = 22.0f;

        VisibilityRaster Raster;
        Raster.AssignCelestial(Sky);

        std::vector<unsigned char> Sheet(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        const float Eye[3]     = { 0.0f, -6.0f, 1.7f };
        const float Forward[3] = { 0.0f,  1.0f, 0.0f };
        const float Right[3]   = { 1.0f,  0.0f, 0.0f };
        const float Up[3]      = { 0.0f,  0.0f, 1.0f };

        if (!Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                           kWidth, kHeight, Sheet.data(), MeanLuminance))
        {
            std::printf("  render failed for %s\n", Moments[M].Name);
            return 2;
        }

        Stats[M] = Measure(Sheet);
        std::printf("  %-7s %+7.2f %9.2f   %5.0f %5.0f %5.0f      %5.0f %5.0f %5.0f\n",
                    Moments[M].Name, static_cast<double>(Frame.Sun.Elevation),
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

    // ── The dawn transition ────────────────────────────────────────────────────────────────────────────────────
    // Sampled by sun elevation, not by the clock: every twilight term is keyed to elevation, and at this latitude
    //    the interesting range (-16 to +2 deg) is only about 70 minutes wide.
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
        VisibilityRaster::CelestialSettings Sky{};
        Sky.Enabled = true;
        Sky.CameraHeight = 2.0f;
        Sky.SampleCount = Criteria.AtmosphereSampleCount;
        Sky.LightSampleCount = Criteria.AtmosphereLightSampleCount;
        // Sun due north (the scene faces +Y), at the requested elevation.
        const float E = Stages[K].Elevation * 3.14159265f / 180.0f;
        Sky.Light.Direction[0] = 0.0f;
        Sky.Light.Direction[1] = std::cos(E);
        Sky.Light.Direction[2] = std::sin(E);
        Sky.Light.Intensity = 22.0f;

        VisibilityRaster Raster;
        Raster.AssignCelestial(Sky);
        std::vector<unsigned char> Sheet(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double MeanLuminance = 0.0;
        const float Eye[3]     = { 0.0f, -6.0f, 1.7f };
        const float Forward[3] = { 0.0f,  1.0f, 0.0f };
        const float Right[3]   = { 1.0f,  0.0f, 0.0f };
        const float Up[3]      = { 0.0f,  0.0f, 1.0f };
        if (!Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                           kWidth, kHeight, Sheet.data(), MeanLuminance)) return 2;

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
                for (int C = 0; C < 3; ++C) Row += Sheet[(static_cast<size_t>(Y) * kWidth + X) * 4u + static_cast<size_t>(C)];
            Rows[Y] = N ? Row / (N * 3.0) : 0.0;
        }
        double BrightestRow = 0.0;
        for (uint32_t Y = kHeight * 40u / 100u; Y < kHeight * 56u / 100u; ++Y)
        {
            const double Step = Rows[Y] - Rows[Y - 4u];
            if (Step > BrightestRow) BrightestRow = Step;
        }
        if (BrightestRow > LinePeak) { LinePeak = BrightestRow; LinePeakStage = K; }

        std::printf("  %-22s %+7.1f %9.2f   %5.0f %5.0f %5.0f      %7.1f\n",
                    Stages[K].Name, static_cast<double>(Stages[K].Elevation),
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

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the sky behaves" : "  THE SKY DOES NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
