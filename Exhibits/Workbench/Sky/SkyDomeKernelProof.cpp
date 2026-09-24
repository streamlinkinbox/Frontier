//============================================================================================================================================
// 📦 Exhibits/Workbench/Sky/SkyDomeKernelProof.cpp — the ENGINE's bake against the ENGINE's march, through the KERNEL's fetch
//============================================================================================================================================
// SkyProbeProof.cpp proved the SPLIT (what bakes, what stays analytic) on the panel's sky. This proof covers
//    the code that shipped into the engine build afterwards:
//
//    · the bake:  SkyDomeSheet.h's BakeSkyDomeSheet over AtmosphereModel::Integrate — RGBA16F halves, two
//      stacked octahedral squares (radiance over transmittance), sun cone inpainted;
//    · the fetch: the EXACT texel arithmetic SkyRecords.slang runs — the same octahedral fold, the same
//      half-texel clamps, the same row split — transcribed here over a CPU bilinear sampler, so the figure
//      this proof prints is the figure the kernel computes;
//    · the boolean's guardrails: SkyDomeCovers' three refusals (no bake, horizon band, solar cone) and
//      SkyDomeStagingMatches' staleness rule (a moved sun, a changed medium, a re-budgeted sample count all
//      drop the frame back to the march).
//
//    Gates: fetch-vs-march relative error under 2 % at three sun elevations (noon, low sun, below the
//    horizon), transmittance error under 1 %, the staleness compare answering exactly as specified, and the
//    RGBA16F round trip inside half precision's own bounds.
//============================================================================================================================================

#include "SkyDomeSheet.h"
#include "SkyConstantRecord.h"
#include "SpaceExport.h"   // #26c: the persisted bake rides an .environment container (ENVR row + PROB blob)
#include "SpaceCodec.h"

#include <algorithm>
#include <cstdio>
#include <cstdio>
#include <vector>

namespace {

using namespace Frontier;

uint32_t Failures = 0u;

void Expect(bool Condition, const char* Caption)
{
    std::fprintf(stderr, "  %s %s\n", Condition ? "PASS" : "FAIL", Caption);
    if (!Condition) ++Failures;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       THE KERNEL'S FETCH, TRANSCRIBED
//------------------------------------------------------------------------------------------------------------------------

float HalfToFloat(uint16_t Half) noexcept
{
    const uint32_t Sign = (static_cast<uint32_t>(Half) & 0x8000u) << 16u;
    const uint32_t Exponent = (Half >> 10u) & 0x1Fu;
    const uint32_t Mantissa = Half & 0x3FFu;
    if (Exponent == 0u)
    {
        if (Mantissa == 0u) { float F; uint32_t B = Sign; std::memcpy(&F, &B, 4u); return F; }
        // subnormal half — the bake flushes these, but the reader stays correct anyway
        float F = static_cast<float>(Mantissa) / 16777216.0f;
        return Sign ? -F : F;
    }
    const uint32_t Bits = Sign | ((Exponent - 15u + 127u) << 23u) | (Mantissa << 13u);
    float F; std::memcpy(&F, &Bits, 4u);
    return F;
}

// One bilinear tap of the half sheet at normalised (U, V) — GLSL's own convention (texel centres at
//    (i + 0.5) / size, clamp-at-edge behaviour guaranteed by the caller's half-texel clamps).
void SampleSheet(const std::vector<uint16_t>& Halves, uint32_t Width, uint32_t Height,
                 float U, float V, float OutRgb[3]) noexcept
{
    const float Tx = std::clamp(U * static_cast<float>(Width) - 0.5f, 0.0f, static_cast<float>(Width - 1u));
    const float Ty = std::clamp(V * static_cast<float>(Height) - 0.5f, 0.0f, static_cast<float>(Height - 1u));
    const uint32_t X0 = static_cast<uint32_t>(Tx), Y0 = static_cast<uint32_t>(Ty);
    const uint32_t X1 = std::min(X0 + 1u, Width - 1u), Y1 = std::min(Y0 + 1u, Height - 1u);
    const float Ax = Tx - static_cast<float>(X0), Ay = Ty - static_cast<float>(Y0);
    for (int C = 0; C < 3; ++C)
    {
        const float T00 = HalfToFloat(Halves[(static_cast<size_t>(Y0) * Width + X0) * 4u + static_cast<size_t>(C)]);
        const float T10 = HalfToFloat(Halves[(static_cast<size_t>(Y0) * Width + X1) * 4u + static_cast<size_t>(C)]);
        const float T01 = HalfToFloat(Halves[(static_cast<size_t>(Y1) * Width + X0) * 4u + static_cast<size_t>(C)]);
        const float T11 = HalfToFloat(Halves[(static_cast<size_t>(Y1) * Width + X1) * 4u + static_cast<size_t>(C)]);
        OutRgb[C] = (T00 * (1.0f - Ax) + T10 * Ax) * (1.0f - Ay) + (T01 * (1.0f - Ax) + T11 * Ax) * Ay;
    }
}

// SkyRecords.slang's fetch branch, line for line: the fold, the half-texel clamps, the row split.
void KernelFetch(const std::vector<uint16_t>& Halves, const float Direction[3],
                 float OutRadiance[3], float OutTransmittance[3]) noexcept
{
    float OctU = 0.0f, OctV = 0.0f;
    SkyDomeOctFromDirection(Direction, OctU, OctV);
    const float Side = static_cast<float>(kSkyDomeSide);
    const float FetchU   = std::clamp(OctU * Side, 0.5f, Side - 0.5f) / Side;
    const float FetchRow = std::clamp(OctV * Side, 0.5f, Side - 0.5f);
    SampleSheet(Halves, kSkyDomeSide, kSkyDomeSide * 2u, FetchU, FetchRow / (2.0f * Side), OutRadiance);
    SampleSheet(Halves, kSkyDomeSide, kSkyDomeSide * 2u, FetchU, (FetchRow + Side) / (2.0f * Side), OutTransmittance);
}

// SkyDomeCovers, transcribed: the three refusals the kernel applies before any fetch.
bool KernelCovers(bool SheetResident, const float Direction[3], const float Sun[3]) noexcept
{
    if (!SheetResident) return false;
    if (std::fabs(Direction[2]) < kSkyDomeHorizonSine) return false;
    if (Direction[0] * Sun[0] + Direction[1] * Sun[1] + Direction[2] * Sun[2] > kSkyDomeSunConeCos) return false;
    return true;
}

float LuminanceOf(const float Rgb[3]) noexcept { return 0.2126f * Rgb[0] + 0.7152f * Rgb[1] + 0.0722f * Rgb[2]; }

//------------------------------------------------------------------------------------------------------------------------
//                                                 ONE STAGING'S FIGURES
//------------------------------------------------------------------------------------------------------------------------

void ProveStaging(const char* Caption, const AtmosphereLight& Light)
{
    const AtmosphereMedium Medium{};   // the product's own constants (AtmosphereModel.h defaults)
    constexpr uint32_t kViewSamples = 16u, kLightSamples = 6u;   // the Standard tier's budget

    std::vector<uint16_t> Halves;
    BakeSkyDomeSheet(Medium, Light, kViewSamples, kLightSamples, Halves);
    Expect(Halves.size() == static_cast<size_t>(kSkyDomeSide) * kSkyDomeSide * 2u * 4u,
           "the sheet is 256 x 512 RGBA16F exactly");

    // The census: quasi-random directions the rule lets the fetch answer, fetch vs march.
    uint32_t Seed = 0x9E3779B9u;
    const auto NextUnit = [&Seed]() {
        Seed = Seed * 1664525u + 1013904223u;
        return static_cast<float>(Seed >> 8u) / 16777216.0f;
    };
    double RadianceSum = 0.0, TransmittanceWorst = 0.0, LumSum = 0.0;
    std::vector<double> Errors;
    Errors.reserve(8000u);
    struct Sample { float MarchLum, FetchLum, TransErr; };
    std::vector<Sample> Samples;
    Samples.reserve(8000u);
    while (Samples.size() < 8000u)
    {
        const float Zc = NextUnit() * 2.0f - 1.0f;
        const float Az = NextUnit() * 6.2831853f;
        const float R  = std::sqrt(std::max(0.0f, 1.0f - Zc * Zc));
        const float Direction[3] = { R * std::cos(Az), R * std::sin(Az), Zc };
        if (!KernelCovers(true, Direction, Light.Direction)) continue;

        const AtmosphereSample March = AtmosphereModel::Integrate(Medium, Light, kSkyDomeCameraHeight,
                                                                  Direction, kViewSamples, kLightSamples);
        float FetchRadiance[3], FetchTransmittance[3];
        KernelFetch(Halves, Direction, FetchRadiance, FetchTransmittance);

        Sample S;
        S.MarchLum = LuminanceOf(March.Radiance);
        S.FetchLum = LuminanceOf(FetchRadiance);
        S.TransErr = 0.0f;
        for (int C = 0; C < 3; ++C)
            S.TransErr = std::max(S.TransErr, std::fabs(FetchTransmittance[C] - March.Transmittance[C]));
        Samples.push_back(S);
        LumSum += static_cast<double>(S.MarchLum);
    }
    // The same floored ratio SkyProbeProof uses: a black-night texel of 2e-4 against 1e-4 is not an error
    //    anyone can see, and an unfloored ratio lets the invisible drown the visible.
    const double Floor = std::max(1e-6, LumSum / static_cast<double>(Samples.size()) * 0.01);
    for (const Sample& S : Samples)
    {
        const double Err = std::fabs(static_cast<double>(S.FetchLum - S.MarchLum))
                         / std::max(Floor, static_cast<double>(S.MarchLum));
        Errors.push_back(Err);
        RadianceSum += Err;
        TransmittanceWorst = std::max(TransmittanceWorst, static_cast<double>(S.TransErr));
    }
    std::sort(Errors.begin(), Errors.end());
    const double Mean = RadianceSum / static_cast<double>(Errors.size());
    const double P95  = Errors[static_cast<size_t>(static_cast<double>(Errors.size()) * 0.95)];

    std::fprintf(stderr, "  %s: mean %.3f%%  P95 %.3f%%  transmittance worst %.4f\n",
                 Caption, Mean * 100.0, P95 * 100.0, TransmittanceWorst);
    Expect(Mean < 0.02, "radiance: the kernel's fetch answers within 2% of the engine's march (mean)");
    Expect(TransmittanceWorst < 0.01, "transmittance: the fetched column matches the marched one within 0.01");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::fprintf(stderr, "[SkyDomeKernelProof] the engine bake through the kernel's own fetch arithmetic\n");

    // Three stagings: high sun, low sun (the gradient's steepest daylight), and a set sun (the twilight-black
    //    integral — the bake must reproduce even the near-zero dome without inventing light).
    {
        AtmosphereLight Noon{};
        Noon.Direction[0] = 0.0f; Noon.Direction[1] = 0.3714f; Noon.Direction[2] = 0.9285f;
        ProveStaging("sun high (68 deg)", Noon);
    }
    {
        AtmosphereLight Low{};
        Low.Direction[0] = 0.0f; Low.Direction[1] = 0.9848f; Low.Direction[2] = 0.1736f;
        ProveStaging("sun low (10 deg)", Low);
    }
    {
        AtmosphereLight Set{};
        Set.Direction[0] = 0.0f; Set.Direction[1] = 0.9962f; Set.Direction[2] = -0.0872f;
        ProveStaging("sun set (-5 deg)", Set);
    }

    // The refusals, each one exercised at the boundary it guards.
    std::fprintf(stderr, "[SkyDomeKernelProof] the boolean's guardrails\n");
    {
        const float Sun[3] = { 0.0f, 0.3714f, 0.9285f };
        const float Zenith[3]  = { 0.0f, 0.0f, 1.0f };
        const float Level[3]   = { 1.0f, 0.0f, 0.0f };
        const float NearSun[3] = { 0.0f, 0.3898f, 0.9209f };   // ~1.2 deg off the sun — inside the 4 deg cone
        Expect(!KernelCovers(false, Zenith, Sun), "no sheet resident: every direction marches");
        Expect(!KernelCovers(true, Level, Sun),   "the horizon band marches (the planet-edge discontinuity)");
        Expect(!KernelCovers(true, NearSun, Sun), "the solar cone marches (the disc's gradient)");
        Expect(KernelCovers(true, Zenith, Sun),   "the open dome fetches");
    }

    // The staleness rule: the exact refusals PackSkyRecord relies on.
    {
        SkyConstantRecord Baked{};
        Baked.SunDirection[0] = 0.0f; Baked.SunDirection[1] = 0.3714f; Baked.SunDirection[2] = 0.9285f;
        Baked.SunRadiance[0] = 22.0f; Baked.SunRadiance[1] = 22.0f; Baked.SunRadiance[2] = 22.0f; Baked.SunRadiance[3] = 1.0f;
        Baked.Rayleigh[0] = 5.8e-6f; Baked.Rayleigh[1] = 13.5e-6f; Baked.Rayleigh[2] = 33.1e-6f; Baked.Rayleigh[3] = 8000.0f;
        Baked.Mie[0] = 21.0e-6f; Baked.Mie[1] = 1200.0f; Baked.Mie[2] = 0.78f;
        Baked.Planet[0] = 6360000.0f; Baked.Planet[1] = 60000.0f; Baked.Planet[2] = 2.0f;
        Baked.Control[0] = 16u; Baked.Control[1] = 6u;

        SkyConstantRecord Current = Baked;
        Expect(SkyDomeStagingMatches(Baked, Current), "an unchanged staging keeps the fetch");
        Current = Baked; Current.SunDirection[1] += 0.01f;
        Expect(!SkyDomeStagingMatches(Baked, Current), "a moved sun falls back to the march");
        Current = Baked; Current.Mie[0] *= 1.5f;
        Expect(!SkyDomeStagingMatches(Baked, Current), "a hazier medium falls back to the march");
        Current = Baked; Current.Control[0] = 32u;
        Expect(!SkyDomeStagingMatches(Baked, Current), "a re-budgeted sample count falls back to the march");
        Current = Baked; Current.Twilight[0] = 3.0f; Current.Mie[3] = 40.0f;
        Expect(SkyDomeStagingMatches(Baked, Current), "twilight and shadow lanes never invalidate the dome (they are not in the integral)");
    }

    // The RGBA16F round trip: the bake's writer against the reader above, across the dome's dynamic range.
    {
        double Worst = 0.0;
        for (float Value : { 0.0f, 1e-4f, 3e-3f, 0.05f, 0.5f, 1.0f, 1.4913f })
        {
            const float Back = HalfToFloat(SkyDomeHalfFromFloat(Value));
            const double Rel = Value > 0.0f ? std::fabs(Back - Value) / Value : std::fabs(Back);
            Worst = std::max(Worst, Rel);
        }
        std::fprintf(stderr, "  half round trip worst relative error %.5f\n", Worst);
        Expect(Worst < 1.0 / 1024.0, "RGBA16F round trip stays inside half precision's own 2^-10");
    }

    // #26c the persisted bake: the exact container layout CelestialSequence writes and reads — ENVR row +
    //    PROB blob of (staging record, RGBA16F halves) — round-tripped through the REAL SpaceExport/SpaceCodec,
    //    with the staleness rule exercised at the file boundary. The layout constants here ARE the contract:
    //    a drift in either body fails this gate before it fails a launch.
    std::fprintf(stderr, "[SkyDomeKernelProof] the persisted bake (.environment ENVR + PROB)\n");
    {
        // A small real bake to carry (the low-sun staging — the steepest gradients).
        AtmosphereMedium Medium{};
        AtmosphereLight Light{};
        Light.Direction[0] = 0.0f; Light.Direction[1] = 0.9848f; Light.Direction[2] = 0.1736f;
        std::vector<uint16_t> Halves;
        BakeSkyDomeSheet(Medium, Light, 16u, 6u, Halves);

        SkyConstantRecord Staging{};
        for (int C = 0; C < 3; ++C)
        {
            Staging.SunDirection[C] = Light.Direction[C];
            Staging.SunRadiance[C]  = Light.Colour[C] * Light.Intensity;
            Staging.Rayleigh[C]     = Medium.RayleighScattering[C] * Medium.RayleighStrength;
            Staging.Ozone[C]        = Medium.OzoneAbsorption[C] * Medium.OzoneStrength;
        }
        Staging.SunRadiance[3] = 1.0f;
        Staging.Rayleigh[3] = Medium.RayleighScaleHeight;
        Staging.Mie[0] = Medium.MieScattering * Medium.MieStrength;
        Staging.Mie[1] = Medium.MieScaleHeight; Staging.Mie[2] = Medium.MieAnisotropy;
        Staging.Planet[0] = Medium.PlanetRadius; Staging.Planet[1] = Medium.AtmosphereHeight; Staging.Planet[2] = 2.0f;
        Staging.Control[0] = 16u; Staging.Control[1] = 6u;

        // Write: the PROB blob is record + halves; the ENVR row carries the browsing figures.
        std::vector<uint8_t> Probe(sizeof(SkyConstantRecord) + Halves.size() * sizeof(uint16_t));
        std::memcpy(Probe.data(), &Staging, sizeof(SkyConstantRecord));
        std::memcpy(Probe.data() + sizeof(SkyConstantRecord), Halves.data(), Halves.size() * sizeof(uint16_t));
        SpaceEnvironmentRow Row{};
        std::snprintf(Row.Name, sizeof(Row.Name), "Sky Dome");
        Row.SunHour = 10.0f;
        Row.TerrainRef = 0xFFFFFFFFu; Row.TerrainBlob = 0xFFFFFFFFu;
        SpaceExportContext Context;
        Context.Exporter = "SkyDomeKernelProof";
        std::vector<uint8_t> FileBytes;
        std::string Trouble;
        Expect(SpaceExportEnvironment(Context, Row, "Sky Dome", Probe, 1u, FileBytes, Trouble),
               "the environment container writes (ENVR row + PROB blob)");

        // Read: the same walk LoadSkyDome takes, byte for byte.
        SpaceReader Reader;
        Expect(Reader.Open(FileBytes, Trouble), "the container re-opens and its checksums stand");
        const std::vector<SpaceEnvironmentRow> Rows = Reader.Rows<SpaceEnvironmentRow>(kTagEnvr, Trouble);
        Expect(Rows.size() == 1u && Rows[0].SkyProbeBlob != 0xFFFFFFFFu, "the ENVR row names its PROB blob");
        std::vector<uint8_t> ReadProbe;
        Expect(Reader.ReadBlobs(Trouble) && Reader.BlobBytes(Rows[0].SkyProbeBlob, ReadProbe, Trouble),
               "the PROB blob reads back");
        Expect(ReadProbe.size() == Probe.size() && std::memcmp(ReadProbe.data(), Probe.data(), Probe.size()) == 0,
               "the round trip is byte-identical - staging record and every texel");

        // The staleness rule at the file boundary: the recorded staging accepts itself and refuses a moved sun.
        SkyConstantRecord FileRecord{};
        std::memcpy(&FileRecord, ReadProbe.data(), sizeof(SkyConstantRecord));
        Expect(SkyDomeStagingMatches(FileRecord, Staging), "the file's staging accepts the staging it was baked at");
        SkyConstantRecord Moved = Staging;
        Moved.SunDirection[2] += 0.02f;
        Expect(!SkyDomeStagingMatches(FileRecord, Moved), "a launch under a moved sun refuses the file and re-bakes");
    }

    std::fprintf(stderr, "[SkyDomeKernelProof] %s\n", Failures == 0u ? "every figure agrees" : "FAILURES above");
    return Failures == 0u ? 0 : 1;
}
