//============================================================================================================================================
// 📦 Scratchpad/MoonRenderProof.cpp — the moons through the real GI-off raster, and the shader against the model
//============================================================================================================================================
// Celestial moons, both halves of the dual-path proof:
//
//  ① PARITY — MoonRecords.slang re-transcribed back into C++ and compared against EvaluateMoons over a synthetic
//     flat albedo, the SkyKernelParityProof shape. A swapped axis or a dropped term shows as a number, and the
//     zenith guard and the zero-size backstop are pinned here rather than trusted.
//  ② RENDER — Luna plus Ember over the proof ground through the SHIPPING VisibilityRaster, driven by a live
//     CelestialSequence (register → decode → assign → ApplyTo), at night. The gate reads the numbers — a bright
//     disc where the moon sits, glow and limb inside sane bands, moonlit ground above the moonless baseline —
//     and Diagnostics/MoonProof_Night.png is for the eye.
//
// FIDELITY CONTRACT (CLAUDE.md: proof images show what the project does). The sheet is rendered by shipping
//    translation units through project wiring — the sequence is prepared, the atlas decodes through the real
//    index, nothing is hand-built — with placed moons and a night hour as the only declared test choices.
//
//  ③ CONTENT — the atlas files are not just decodable, they are MOONS: the real Luna, sampled through the
//     real view construction, must show maria darker than highlands and real variance across a disc. Dimensions
//     alone would pass a white rectangle; this is the check that answers "the moon has no textures".
#include "GeometricRaster/VisibilityRaster.h"
#include "GeometricRaster/SceneStructure.h"
#include "GeometricRaster/GeometryStructure.h"
#include "DisplayPresentation/CelestialSolver.h"
#include "DisplayPresentation/MoonConstantRecord.h"
#include "Projects/Project-Zero/Source/CelestialSequence.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

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

bool Nearly(float A, float B)
{
    const float Scale = 1.0f + std::fabs(A) + std::fabs(B);
    return std::fabs(A - B) <= 1e-5f * Scale;
}

// ── Transcribed BACK from Engine/Shaders/MoonRecords.slang, not from MoonConstantRecord.h ────────────────────
// Same structure as GlslSkyRadiance in SkyKernelParityProof: the sampler is shared (a flat synthetic albedo, so
//    sampling cannot hide a math divergence), everything else is re-read from the shader source.
void GlslMoonAlong(const MoonDrawEntry* Entries, uint32_t Count, const float* Dir, const float* Trans,
                   float* Out)
{
    Out[0] = Out[1] = Out[2] = 0.0f;
    if (Entries == nullptr) return;
    if (Count > 4u) Count = 4u;
    const float kPi = 3.14159265358979323846f;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        const MoonDrawEntry& E = Entries[i];
        const float* md = E.Direction;
        const float ang = E.AngularRadius, bright = E.Brightness, phase = E.Phase, glowAmt = E.Glow;
        const float spin = E.Spin, tilt = E.Tilt, haze = E.Haze, gam = E.Gamma;
        if (!(ang > 1e-6f)) continue;
        float d = Dir[0]*md[0]+Dir[1]*md[1]+Dir[2]*md[2];
        d = d < -1.0f ? -1.0f : (d > 1.0f ? 1.0f : d);
        const float a = std::acos(d);
        const bool pole = std::fabs(md[2]) > 0.9998477f;
        const float ax = 0.0f, ay = pole ? 1.0f : 0.0f, az = pole ? 0.0f : 1.0f;
        float ux = md[1]*az-md[2]*ay, uy = md[2]*ax-md[0]*az, uz = md[0]*ay-md[1]*ax;
        const float ul = std::sqrt(ux*ux+uy*uy+uz*uz);
        if (ul > 0.0f) { ux/=ul; uy/=ul; uz/=ul; }
        const float vx = uy*md[2]-uz*md[1], vy = uz*md[0]-ux*md[2], vz = ux*md[1]-uy*md[0];
        if (a < ang*1.06f)
        {
            const float lx = (Dir[0]*ux+Dir[1]*uy+Dir[2]*uz)/std::sin(ang);
            const float ly = (Dir[0]*vx+Dir[1]*vy+Dir[2]*vz)/std::sin(ang);
            const float r2 = lx*lx+ly*ly;
            const float z = std::sqrt(r2 < 1.0f ? 1.0f-r2 : 0.0f);
            const float ct = std::cos(tilt), st = std::sin(tilt);
            const float ntx = lx, nty = ly*ct-z*st, ntz = ly*st+z*ct;
            const float lon = std::atan2(ntx,ntz)+spin;
            float nyc = nty < -1.0f ? -1.0f : (nty > 1.0f ? 1.0f : nty);
            const float lat = std::asin(nyc);
            float uu = lon/(2.0f*kPi)+0.5f; uu -= std::floor(uu);
            const float vv = 0.5f-lat/kPi;
            float alb[3]; SampleMoonAlbedo(E.Albedo, uu, vv, alb);
            alb[0] = std::pow(alb[0],gam)*E.Tint[0]; alb[1] = std::pow(alb[1],gam)*E.Tint[1]; alb[2] = std::pow(alb[2],gam)*E.Tint[2];
            const float ph = phase*2.0f*kPi;
            const float litx = std::sin(ph), litz = std::cos(ph);
            const float wrap = haze*0.3f;
            float ndl = (lx*litx+z*litz+wrap)/(1.0f+wrap);
            ndl = ndl > 0.0f ? ndl : 0.0f;
            const float ew = 0.975f+(0.88f-0.975f)*haze;
            const float e0 = ang*ew, e1 = ang*(1.0f+haze*0.06f);
            float te = (a-e0)/(e1-e0); te = te < 0.0f ? 0.0f : (te > 1.0f ? 1.0f : te);
            const float edge = 1.0f-te*te*(3.0f-2.0f*te);
            const float hm = haze > 0.35f ? haze : 0.35f;
            const float limb = 1.0f+(0.5f-1.0f)*(1.0f-z)*(1.0f-z)*hm;
            const float disc = bright*(std::pow(ndl,0.8f)*limb+0.012f)*edge;
            Out[0]+=alb[0]*disc; Out[1]+=alb[1]*disc; Out[2]+=alb[2]*disc;
            float t0=(a-ang*0.8f)/(ang*0.2f); t0=t0<0.0f?0.0f:(t0>1.0f?1.0f:t0);
            float t1=(a-ang)/(ang*0.06f); t1=t1<0.0f?0.0f:(t1>1.0f?1.0f:t1);
            const float rimA = (t0*t0*(3.0f-2.0f*t0))*(1.0f-t1*t1*(3.0f-2.0f*t1))*haze;
            const float rim = rimA*bright*0.35f*(0.3f+0.7f*ndl);
            Out[0]+=rim*E.Tint[0]; Out[1]+=rim*E.Tint[1]; Out[2]+=rim*E.Tint[2];
        }
        const float lum = 0.4f+0.6f*(0.5f+0.5f*std::cos(phase*2.0f*kPi));
        const float g = glowAmt*bright*(std::exp(-a/(ang*1.4f))*0.09f+std::exp(-a*3.5f)*0.005f)*lum;
        Out[0]+=g*E.Tint[0]; Out[1]+=g*E.Tint[1]; Out[2]+=g*E.Tint[2];
    }
    Out[0]*=Trans[0]; Out[1]*=Trans[1]; Out[2]*=Trans[2];
}

// A ground plane and a couple of blocks — the CelestialSkyProof recipe verbatim in shape, so a wrong horizon
//    reads the same way there as here. std::deque, not std::vector: GeometryStructure is not movable.
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

} // namespace

int main()
{
    std::printf("\nMoonRecords.slang against MoonConstantRecord.h — the transcription is faithful\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    // ── ① PARITY ──────────────────────────────────────────────────────────────────────────────
    // A flat synthetic albedo: mid grey with a red lean, so every channel exercises the tint path.
    static const uint8_t Flat[4u * 4u * 4u] = {
        128,64,32,255, 128,64,32,255, 128,64,32,255, 128,64,32,255,
        128,64,32,255, 128,64,32,255, 128,64,32,255, 128,64,32,255,
        128,64,32,255, 128,64,32,255, 128,64,32,255, 128,64,32,255,
        128,64,32,255, 128,64,32,255, 128,64,32,255, 128,64,32,255,
    };
    {
        MoonDrawEntry E{};
        E.Albedo.Texels = Flat; E.Albedo.Width = 4u; E.Albedo.Height = 4u; E.Albedo.TexelBytes = 4u;
        struct Case { const char* Name; float Dir[3]; float Moon[3]; float AngDeg; float Phase; float Haze; float Gamma; float Tilt; };
        const float kD = 3.14159265358979323846f / 180.0f;
        const Case Cases[] = {
            { "disc centre, full",  {0,0.9063f,0.4226f}, {0,0.9063f,0.4226f}, 1.0f, 0.00f, 0.0f, 1.0f, 0.1f },
            { "disc limb, gibbous", {0,0.9063f,0.4226f}, {0.008f,0.9062f,0.4226f}, 1.0f, 0.12f, 0.0f, 1.0f, 0.1f },
            { "off disc, glow",     {0,0.9063f,0.4226f}, {0.25f,0.87f,0.42f}, 1.0f, 0.00f, 0.0f, 1.0f, 0.1f },
            { "new moon, glow only",{0,0.9063f,0.4226f}, {0,0.9063f,0.4226f}, 1.0f, 0.50f, 0.0f, 1.0f, 0.1f },
            { "hazy limb + rim",    {0,0.9063f,0.4226f}, {0.008f,0.9062f,0.4226f}, 2.0f, 0.12f, 1.0f, 1.0f, 0.5f },
            { "shard contrast",     {0,0.9063f,0.4226f}, {0,0.9063f,0.4226f}, 1.0f, 0.00f, 0.0f, 0.9f, 0.0f },
            { "zenith moon",        {0,0,1}, {0,0,1}, 1.0f, 0.00f, 0.0f, 1.0f, 0.1f },
            { "below horizon",      {0,0.9063f,0.4226f}, {0,-0.5f,-0.86f}, 1.0f, 0.00f, 0.0f, 1.0f, 0.1f },
        };
        std::printf("  %-20s %-30s %-30s %s\n","case","C++ model","GLSL transcription","worst rel");
        double Worst = 0.0;
        for (const Case& T : Cases)
        {
            for (int C = 0; C < 3; ++C) E.Direction[C] = T.Moon[C];
            { const float L = std::sqrt(E.Direction[0]*E.Direction[0]+E.Direction[1]*E.Direction[1]+E.Direction[2]*E.Direction[2]);
              for (int C = 0; C < 3; ++C) E.Direction[C] /= L; }
            E.AngularRadius = T.AngDeg * kD * 0.5f;
            E.Phase = T.Phase; E.Haze = T.Haze; E.Gamma = T.Gamma; E.Tilt = T.Tilt;
            E.Brightness = 1.6f; E.Glow = 0.8f;
            const float Trans[3] = { 0.9f, 0.85f, 0.8f };
            float Model[3], Glsl[3];
            EvaluateMoons(&E, 1u, T.Dir, Trans, Model);
            GlslMoonAlong(&E, 1u, T.Dir, Trans, Glsl);
            double Rel = 0.0;
            for (int C = 0; C < 3; ++C)
            {
                const double M = Model[C], S = Glsl[C];
                const double D = std::fabs(M-S)/std::fmax(std::fmax(std::fabs(M),std::fabs(S)),1e-9);
                if (std::fabs(M) > 1e-6 || std::fabs(S) > 1e-6) Rel = std::fmax(Rel, D);
            }
            Worst = std::fmax(Worst, Rel);
            std::printf("  %-20s %7.4f %7.4f %7.4f  %7.4f %7.4f %7.4f  %.2e\n", T.Name,
                        Model[0],Model[1],Model[2], Glsl[0],Glsl[1],Glsl[2], Rel);
        }
        std::printf("\n  worst relative difference across %zu directions: %.3e\n", sizeof(Cases)/sizeof(Cases[0]), Worst);
        Require("the shader computes the same moons as the model", Worst < 1e-5, "same bar as the sky parity proof");
    }

    // The backstops: a zero-size moon contributes nothing (no NaN), and the moon past the count is unread.
    {
        MoonDrawEntry E{};
        E.Albedo.Texels = Flat; E.Albedo.Width = 4u; E.Albedo.Height = 4u; E.Albedo.TexelBytes = 4u;
        E.Direction[0] = 0.0f; E.Direction[1] = 0.9063f; E.Direction[2] = 0.4226f;
        E.AngularRadius = 0.0f; E.Brightness = 1.6f; E.Glow = 0.8f;
        const float At[3] = { 0.0f, 0.9063f, 0.4226f };   // exactly at the would-be disc centre: 0/0 country
        const float Trans[3] = { 1.0f, 1.0f, 1.0f };
        float Out[3] = { 7.0f, 7.0f, 7.0f };
        EvaluateMoons(&E, 1u, At, Trans, Out);
        char Detail[96];
        std::snprintf(Detail, sizeof(Detail), "out=(%.2f %.2f %.2f)", Out[0], Out[1], Out[2]);
        Require("a zero-size moon is invisible, not a NaN", Out[0]==0.0f && Out[1]==0.0f && Out[2]==0.0f, Detail);
        float Amb[3];
        EvaluateMoonAmbient(&E, 1u, Amb);
        Require("moonlight is positive for a moon above the horizon", Amb[0] > 0.0f && Amb[1] > 0.0f && Amb[2] > 0.0f, "the direct fill the kernel adds");
        E.Direction[2] = -0.5f;
        EvaluateMoonAmbient(&E, 1u, Amb);
        Require("moonlight is zero for a moon below the horizon", Amb[0]==0.0f && Amb[1]==0.0f && Amb[2]==0.0f, "max(dir.z, 0)");
    }

    // ── ② RENDER ──────────────────────────────────────────────────────────────────────────────
    std::printf("\n  moons through the GI-off raster (VisibilityRaster)\n\n");

    SceneStructure Level;
    std::deque<GeometryStructure> Owned;
    BuildScene(Level, Owned);

    CelestialSequence Sky;
    Sky.Prepare();
    Sky.Observation.LocalHours = 23.0f;   // night: the sun 40+ deg down, so the discs read against black
    const float TickOrigin[3] = { 0.0f, 0.0f, 2.0f };
    Sky.Tick(0.0f, TickOrigin, 0.0f);
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;   // a moon-only sky keeps the counts honest
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = false;   // ... and no weather either
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalCloud)] = false;
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::LocalFog)] = false;

    // The atlas through the REAL index: register, decode at full resolution, assign — the GameExecution order.
    TextureIndex Textures;
    uint32_t Slots[kMoonAtlasCount];
    for (uint32_t M = 0u; M < kMoonAtlasCount; ++M)
    {
        char Path[128];
        std::snprintf(Path, sizeof(Path), "%s%s", kMoonTextureDirectory, kMoonAtlas[M].File);
        Slots[M] = Textures.RegisterPath(Path, /*Linear=*/false);
    }
    std::vector<std::string> Report;
    const uint32_t Failures2 = Textures.Decode(0u, &Report);
    char AssetDetail[128];
    std::snprintf(AssetDetail, sizeof(AssetDetail), "decoded %u textures, %u failures", Textures.QueryCount(), Failures2);
    Require("all six albedos decode", Failures2 == 0u && Textures.QueryCount() == 6u, AssetDetail);
    bool FullRes = true;
    for (const TextureDescriptor& T : Textures.QueryTextures())
        FullRes = FullRes && !T.Placeholder && T.Width == 2048u && T.Height == 1024u && T.TexelBytes() == 4u;
    Require("every albedo is a full 2048x1024 RGBA8", FullRes, "the asset contract, pinned");
    Sky.AssignMoonAtlas(Slots, Textures);

    // Two placed moons: Luna dead ahead and full, Ember off to the side and gibbous.
    Sky.MoonSlots[0].FollowSky = false;
    Sky.MoonSlots[0].Azimuth = 0.0f; Sky.MoonSlots[0].Elevation = 25.0f;
    Sky.MoonSlots[0].Size = 2.0f; Sky.MoonSlots[0].Phase = 0.5f;
    Sky.MoonSlots[1].Preset = 1u; Sky.MoonSlots[1].Visible = true;
    Sky.MoonSlots[1].FollowSky = false;
    Sky.MoonSlots[1].Azimuth = 14.0f; Sky.MoonSlots[1].Elevation = 15.0f;
    Sky.MoonSlots[1].Size = 3.0f; Sky.MoonSlots[1].Phase = 0.62f;

    CelestialBudget Budget{};   // 16x6 atmosphere samples by default — the Standard shape, no classifier needed

    const float Eye[3]     = { 0.0f, -6.0f, 1.7f };
    const float Forward[3] = { 0.0f,  0.9063f, 0.4226f };   // az 0, el 25: Luna sits dead centre
    const float Right[3]   = { 1.0f,  0.0f, 0.0f };
    const float Up[3]      = { 0.0f, -0.4226f, 0.9063f };

    auto RenderOnce = [&](bool MoonsOn, std::vector<unsigned char>& Sheet) -> bool
    {
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = MoonsOn;
        VisibilityRaster Raster;
        Sky.ApplyTo(Raster, Budget);
        double MeanLuminance = 0.0;
        return Raster.Render(Level, Eye, Forward, Right, Up, 55.0f * 3.14159265f / 180.0f,
                             kWidth, kHeight, Sheet.data(), MeanLuminance);
    };

    std::vector<unsigned char> WithMoons(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
    std::vector<unsigned char> Baseline(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
    const bool OkMoons = RenderOnce(true, WithMoons);
    const bool OkBase = RenderOnce(false, Baseline);
    Require("the moonlit frame renders", OkMoons, "VisibilityRaster.Render");
    Require("the moonless baseline renders", OkBase, "VisibilityRaster.Render");

    // Luna's disc centre: the frame centre pixel must jump from night-black to moon-bright.
    const size_t Centre = (static_cast<size_t>(kHeight / 2u) * kWidth + kWidth / 2u) * 4u;
    char CentreDetail[128];
    std::snprintf(CentreDetail, sizeof(CentreDetail), "R %u -> %u", Baseline[Centre], WithMoons[Centre]);
    Require("the disc centre lands moon-bright", OkMoons && OkBase && WithMoons[Centre] > Baseline[Centre] + 60u, CentreDetail);

    // Changed-pixel census: discs plus glow, present but bounded.
    uint32_t Changed = 0u;
    for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
    {
        int D = 0;
        for (int C = 0; C < 3; ++C)
        {
            const int A = WithMoons[I * 4u + static_cast<size_t>(C)];
            const int B = Baseline[I * 4u + static_cast<size_t>(C)];
            if (A > B + D) D = A - B;
        }
        if (D > 12) ++Changed;
    }
    char CensusDetail[128];
    std::snprintf(CensusDetail, sizeof(CensusDetail), "%u pixels of %u", Changed, kWidth * kHeight);
    Require("the moons cover a sane band of pixels", Changed > 50u && Changed < 30000u, CensusDetail);

    // Moonlit ground: the bottom band must lift above the moonless baseline.
    double GroundLit = 0.0, GroundDark = 0.0;
    uint32_t GroundN = 0u;
    for (uint32_t Y = kHeight * 90u / 100u; Y < kHeight; ++Y)
        for (uint32_t X = kWidth / 4u; X < kWidth * 3u / 4u; ++X, ++GroundN)
        {
            const size_t I = (static_cast<size_t>(Y) * kWidth + X) * 4u;
            GroundLit += (WithMoons[I] + WithMoons[I + 1u] + WithMoons[I + 2u]) / 3.0;
            GroundDark += (Baseline[I] + Baseline[I + 1u] + Baseline[I + 2u]) / 3.0;
        }
    GroundLit /= GroundN; GroundDark /= GroundN;
    char GroundDetail[128];
    std::snprintf(GroundDetail, sizeof(GroundDetail), "mean %.2f -> %.2f LSB", GroundDark, GroundLit);
    Require("moonlight lifts the ground above the baseline", GroundLit > GroundDark + 1.0, GroundDetail);

    // The sheet for the eye.
    {
        std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
        for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
            for (int C = 0; C < 3; ++C) Rgb[I * 3u + static_cast<size_t>(C)] = WithMoons[I * 4u + static_cast<size_t>(C)];
        PngWriteShim::WritePng("Diagnostics/MoonProof_Night.png", static_cast<int>(kWidth),
                               static_cast<int>(kHeight), 3, Rgb.data(), static_cast<int>(kWidth) * 3);
        std::printf("\n  sheet: Diagnostics/MoonProof_Night.png\n");
    }

    // ── ③ CONTENT ──────────────────────────────────────────────────────────────────────────────
    // The decoded Luna, through the same view construction AssignMoonAtlas uses, must BE the moon: dark maria
    //    against bright highlands, and real variance across a big disc. A white rectangle decodes to 2048x1024
    //    just as happily — this pins the CONTENT the dimensions cannot.
    {
        const TextureDescriptor& Luna = Textures.QueryTextures()[Slots[0]];
        MoonAlbedoView View{};
        const size_t Level0 = Luna.LevelOffsets.empty() ? 0u : Luna.LevelOffsets[0];
        if (!Luna.Texels.empty() && Level0 < Luna.Texels.size())
        {
            View.Texels = Luna.Texels.data() + Level0;
            View.Width = Luna.Width; View.Height = Luna.Height; View.TexelBytes = Luna.TexelBytes();
        }
        float Maria[3], Highland[3];
        SampleMoonAlbedo(View, 0.45f, 0.45f, Maria);      // maria country: dark basalt plains
        SampleMoonAlbedo(View, 0.05f, 0.10f, Highland);   // far-side highlands: bright crust
        const double MariaLum = (Maria[0] + Maria[1] + Maria[2]) / 3.0;
        const double HighlandLum = (Highland[0] + Highland[1] + Highland[2]) / 3.0;
        char ContentDetail[128];
        std::snprintf(ContentDetail, sizeof(ContentDetail), "maria %.2f vs highlands %.2f (linear)",
                      MariaLum, HighlandLum);
        std::printf("\n  atlas content: %s\n", ContentDetail);
        Require("Luna's maria read darker than its highlands",
                MariaLum < 0.50 && HighlandLum > 0.55 && HighlandLum - MariaLum > 0.15, ContentDetail);

        MoonDrawEntry Big{};
        Big.Direction[0] = 0.0f; Big.Direction[1] = 0.9063f; Big.Direction[2] = 0.4226f;
        Big.AngularRadius = 12.0f * 3.14159265f / 180.0f;   // a giant moon: texture fills the disc
        Big.Brightness = 1.6f; Big.Glow = 0.0f; Big.Phase = 0.0f;
        Big.Tilt = 6.7f * 3.14159265f / 180.0f; Big.Gamma = 1.0f;
        Big.Tint[0] = Big.Tint[1] = Big.Tint[2] = 1.0f;
        Big.Albedo = View;
        const float Trans[3] = { 1.0f, 1.0f, 1.0f };
        double Sum = 0.0, Sum2 = 0.0; int N = 0;
        for (int Gy = -8; Gy <= 8; ++Gy)
            for (int Gx = -8; Gx <= 8; ++Gx)
            {
                float Dx = Big.Direction[0] + Gx * 0.025f, Dz = Big.Direction[2] + Gy * 0.025f;
                const float L = std::sqrt(Dx * Dx + Big.Direction[1] * Big.Direction[1] + Dz * Dz);
                const float Dir[3] = { Dx / L, Big.Direction[1] / L, Dz / L };
                float Out[3];
                EvaluateMoons(&Big, 1u, Dir, Trans, Out);
                const double Lum = (Out[0] + Out[1] + Out[2]) / 3.0;
                Sum += Lum; Sum2 += Lum * Lum; ++N;
            }
        const double Mean = Sum / N, Std = std::sqrt(std::fmax(Sum2 / N - Mean * Mean, 0.0));
        char DiscDetail[128];
        std::snprintf(DiscDetail, sizeof(DiscDetail), "disc mean %.2f std %.2f", Mean, Std);
        Require("a giant Luna carries real texture variance, not flat white", Std > 0.10, DiscDetail);
    }

    // ── ④ LINKED ───────────────────────────────────────────────────────────────────────────────
    // Slot 0 as Prepare() leaves it: Luna FOLLOWING the solved lunar frame, not placed. §1 pins the link at
    //    the resolver and §2 proves placed moons draw, but nothing renders the link end to end — the engine's
    //    headline ("ours follows the solved lunar frame rather than sitting at a fixed chart position, because
    //    this engine HAS an ephemeris"). This renders it: a fresh sequence, project defaults, aimed at the
    //    SOLVED direction on a full-moon night. 26 Sep 2026 at 22h, when the solver puts the moon 48.6 deg up
    //    at illumination 1.00 — NOT the
    //    gates' Sep 10, whose moon is new (0.00) and below the horizon all night (verified hour by hour), so
    //    the linked slot has nothing to show on that date. The stars stay off the way §2 keeps them off: with
    //    no starfield, any moon-bright pixel at the aimed centre is the linked Luna — and a broken link renders
    //    the slot's dead values (az 300 / el 28) a quarter of the sky away instead, which is the mutation that
    //    proves this pin.
    {
        CelestialSequence Linked;
        Linked.Prepare();
        Linked.AssignMoonAtlas(Slots, Textures);
        Require("slot 0 opens linked to the ephemeris", Linked.MoonSlots[0].FollowSky == true,
                "Prepare links Luna; the roster's az/el sit dead while it does");
        Linked.Observation.Year = 2026; Linked.Observation.Month = 9; Linked.Observation.Day = 26;
        Linked.Observation.LocalHours = 22.0f; Linked.Observation.UtcOffset = 2.0f;
        Linked.Observation.Latitude = -26.19f; Linked.Observation.Longitude = 28.32f;
        const float LinkOrigin[3] = { 0.0f, 0.0f, 2.0f };
        Linked.Tick(0.0f, LinkOrigin, 0.0f);
        std::printf("\n  linked Luna: moon el %+.2f az %.1f illum %.2f at 22h on Sep 26\n",
                    Linked.Frame().Moon.Elevation, Linked.Frame().Moon.Azimuth,
                    Linked.Frame().MoonIllumination);

        // Aim at the SOLVED direction in full 3D: Right = Forward x world-up, Up = Right x Forward.
        const float* Md = Linked.Frame().Moon.Direction;
        float LF[3] = { Md[0], Md[1], Md[2] };
        float LR[3] = { LF[1], -LF[0], 0.0f };
        {
            const float Ll = std::sqrt(LR[0] * LR[0] + LR[1] * LR[1] + LR[2] * LR[2]);
            LR[0] /= Ll; LR[1] /= Ll; LR[2] /= Ll;
        }
        const float LU[3] = { LR[1] * LF[2] - LR[2] * LF[1],
                              LR[2] * LF[0] - LR[0] * LF[2],
                              LR[0] * LF[1] - LR[1] * LF[0] };

        Linked.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = true;
        Linked.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
        VisibilityRaster LinkRaster;
        Linked.ApplyTo(LinkRaster, Budget);
        std::vector<unsigned char> WithLinked(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        double LinkLuminance = 0.0;
        const bool OkLinked = LinkRaster.Render(Level, Eye, LF, LR, LU, 55.0f * 3.14159265f / 180.0f,
                                                kWidth, kHeight, WithLinked.data(), LinkLuminance);
        Linked.Shown[static_cast<uint32_t>(CelestialEntity::Moons)] = false;
        VisibilityRaster LinkBaseRaster;
        Linked.ApplyTo(LinkBaseRaster, Budget);
        std::vector<unsigned char> LinkBaseline(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
        const bool OkLinkBase = LinkBaseRaster.Render(Level, Eye, LF, LR, LU, 55.0f * 3.14159265f / 180.0f,
                                                      kWidth, kHeight, LinkBaseline.data(), LinkLuminance);
        Require("the linked frame renders", OkLinked, "VisibilityRaster.Render");
        Require("the linked baseline renders", OkLinkBase, "VisibilityRaster.Render");

        // The aimed centre pixel must jump from night-black to moon-bright: the aim is the solved direction,
        //    so only the linked Luna lands there. A broken link puts the dead slot values a quarter-sky away
        //    and this stays black (mutation-proven).
        const size_t LCentre = (static_cast<size_t>(kHeight / 2u) * kWidth + kWidth / 2u) * 4u;
        char LinkDetail[128];
        std::snprintf(LinkDetail, sizeof(LinkDetail), "R %u -> %u", LinkBaseline[LCentre], WithLinked[LCentre]);
        Require("the linked disc lands on the solved direction",
                OkLinked && OkLinkBase && WithLinked[LCentre] > LinkBaseline[LCentre] + 60u, LinkDetail);

        // The sheet for the eye.
        {
            std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
            for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
                for (int C = 0; C < 3; ++C) Rgb[I * 3u + static_cast<size_t>(C)] = WithLinked[I * 4u + static_cast<size_t>(C)];
            PngWriteShim::WritePng("Diagnostics/MoonProof_Linked.png", static_cast<int>(kWidth),
                                   static_cast<int>(kHeight), 3, Rgb.data(), static_cast<int>(kWidth) * 3);
            std::printf("\n  sheet: Diagnostics/MoonProof_Linked.png\n");
        }
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures==0 ? "  the moons reach the raster and match the shader" : "  THE MOONS DIVERGE");
    return Failures==0?0:1;
}
