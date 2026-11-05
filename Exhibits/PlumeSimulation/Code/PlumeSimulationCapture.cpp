/*==========================================================================================================================================
                                                    PLUMESIMULATIONCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The C++ smoke-and-fire plume on its own Vulkan device: the CPU modules drive the burnout script, PlumeSimulationSequence runs the transport
//    and the march with the programs of the PlumeSimulation archive set.
//
//     PlumeSimulationCapture capture <repository root> <shader folder> <output folder> [grade low|medium|high] [trace scale]
//     PlumeSimulationCapture parity  <repository root> <shader folder> <reference folder> <output folder>
//
// 📝 capture writes pictures of the burnout at eight moments. parity replays the run the demo's WGSL made on Dawn (Experimental/VehicleHmiPrototype/
//    Tools/PlumeParityGpu.mjs writes the reference folder: the run's size, the frame record it marched with, and the lattices, regions and march it left)
//    and compares what the Slang programs leave, element for element, after the same steps.
//
//    What stands in. The illumination port's scene distance field, shadow cascades and sky are not part of this exhibit: the plume's programs bind
//    them, so the smallest resources of the right kind are bound — a distance field of zero cascades (the analytic ground plane at z = 0 answers every
//    sample and no occupant is ever indexed), no opaque surface, a shadow with no cascade (the sun is always seen) and a one-texel sky. The pictures
//    composite the march's radiance over a flat backdrop on the CPU; the scene's own picture belongs to the illumination host.

#include "PlumeScenario.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/RefreshEncoding.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"
#include "Frontier/Engine/Rendering/PlumeSimulation/PlumeSimulationSequence.h"

#include <stb_image_write.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;
using namespace Frontier::PlumeSimulation;
using namespace PlumeScenario;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   SMALL TOOLS
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> ReadBytes(const std::filesystem::path& Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}

// 📐 FNV-1a, 64 bits, over bytes — written as 16 hex digits (the exporters' hash).
std::string HashBytes(const uint8_t* Bytes, size_t Count)
{
    uint64_t Hash = 0xcbf29ce484222325ull;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= 0x100000001b3ull;
    }
    char Text[17];
    std::snprintf(Text, sizeof(Text), "%016llx", static_cast<unsigned long long>(Hash));
    return Text;
}

float HalfToFloat(uint16_t Half)
{
    const uint32_t Sign = (Half >> 15) & 1u, Exponent = (Half >> 10) & 31u, Mantissa = Half & 1023u;
    float          Value;
    if (Exponent == 0) { Value = std::ldexp(static_cast<float>(Mantissa), -24); }
    else if (Exponent == 31) { Value = Mantissa ? std::nanf("") : INFINITY; }
    else { Value = std::ldexp(static_cast<float>(Mantissa | 1024u), static_cast<int>(Exponent) - 25); }
    return Sign ? -Value : Value;
}

float ProjectSrgb(float Linear)
{
    const float Clamped = std::min(1.0f, std::max(0.0f, Linear));
    return Clamped <= 0.0031308f ? 12.92f * Clamped : 1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE STAND-INS
//------------------------------------------------------------------------------------------------------------------------

struct PlumeStage
{
    std::shared_ptr<DescriptorLayout>  DistanceLayout{};
    std::shared_ptr<DescriptorGroup>   DistanceGroup{};
    std::shared_ptr<DeviceRegion>      Occupants{};
    std::shared_ptr<DeviceRegion>      Refresh{};
    std::shared_ptr<DeviceTexture>     DepthTexture{};
    std::shared_ptr<DeviceTextureView> DepthView{};
    PlumeMarchInputs                   March{};
};

std::shared_ptr<DeviceTextureView> ConstructViewOf(DeviceExchange& Exchange, const std::shared_ptr<DeviceTexture>& Texture, ViewDimension Dimension)
{
    TextureViewSpecification Specification;
    Specification.Dimension    = Dimension;
    Specification.DimensionSet = true;
    return Exchange.ConstructView(Texture, Specification);
}

// 📝 PlumeParityGpu.mjs builds the same resources in the same order, sizes and contents.
bool ConstructStage(DeviceExchange& Exchange, std::array<uint32_t, 2> Screen, PlumeStage& Stage, std::string& Diagnostic)
{
    using D = DescriptorCode;
    // ① the scene distance field's group: zero cascades, the ground plane at z = 0
    auto DistanceRecords = Exchange.ConstructRegion(160, "Distance record");
    std::array<std::shared_ptr<DeviceTextureView>, 4> ClipViews{};
    for (uint32_t Level = 0; Level < 4; ++Level)
    {
        TextureSpecification Clip;
        Clip.Label         = "Clip " + std::to_string(Level);
        Clip.Dimension     = TextureDimension::Volumetric;
        Clip.Format        = TextureFormat::Rgba16Float;
        Clip.Usage         = TextureUsage::Sampled;
        ClipViews[Level]   = ConstructViewOf(Exchange, Exchange.ConstructTexture(Clip), ViewDimension::Volumetric);
    }
    SamplerSpecification Cascade;
    Cascade.MagFilter = Cascade.MinFilter = FilterMode::Linear;
    Cascade.AddressU  = Cascade.AddressV = Cascade.AddressW = AddressMode::Repeat;
    auto CascadeSampler = Exchange.ConstructSampler(Cascade);
    auto EpochRegion    = Exchange.ConstructRegion(16, "Brick stamps");
    Stage.DistanceLayout = Exchange.ConstructDescriptorLayout(
        std::vector<DescriptorSlot>{{D::Uniform}, {D::Texture3D}, {D::Texture3D}, {D::Texture3D}, {D::Texture3D}, {D::Sampler}, {D::StorageRead}}, "Distance traversal");
    Stage.DistanceGroup = Exchange.ConstructDescriptorGroup(Stage.DistanceLayout, {DistanceRecords, ClipViews[0], ClipViews[1], ClipViews[2], ClipViews[3], CascadeSampler, EpochRegion});
    Stage.Occupants     = Exchange.ConstructRegion(336 * 4, "Occupants");

    // ② what the march reads beside the plume: the refresh constants, a surface with no depth, a shadow with no cascade, a one-texel sky
    Stage.Refresh = Exchange.ConstructRegion(1072, "Refresh constants");
    TextureSpecification Depth;
    Depth.Label  = "Surface depth";
    Depth.Width  = Screen[0];
    Depth.Height = Screen[1];
    Depth.Format = TextureFormat::Depth32Float;
    Depth.Usage  = TextureUsage::Sampled | TextureUsage::RenderAttachment;
    Stage.DepthTexture = Exchange.ConstructTexture(Depth);
    Stage.DepthView    = Exchange.ConstructView(Stage.DepthTexture);
    // 📝 Depth images are not zero-filled by the exchange: an empty raster scope clears it to the reversed far plane — nothing stored, nothing occludes.
    {
        CommandSequence       Sequence = Exchange.BeginSequence();
        const DepthAttachment Cleared{Stage.DepthView, LoadOperation::Clear, 0.0f};
        Sequence.BeginRaster({}, &Cleared, "Surface depth");
        Sequence.EndRaster();
        if (!Exchange.Submit(Sequence, &Diagnostic)) return false;
    }
    TextureSpecification Shadow;
    Shadow.Label  = "Shadow depth";
    Shadow.Format = TextureFormat::Depth32Float;
    Shadow.Usage  = TextureUsage::Sampled;
    auto ShadowTexture = Exchange.ConstructTexture(Shadow);
    SamplerSpecification Comparison;
    Comparison.MagFilter = Comparison.MinFilter = FilterMode::Linear;
    Comparison.Compare   = CompareFunction::Less;
    TextureSpecification Sky;
    Sky.Label  = "Sky view";
    Sky.Format = TextureFormat::Rgba8Unorm;
    Sky.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget;
    auto SkyTexture = Exchange.ConstructTexture(Sky);
    const std::array<uint8_t, 4> SkyTexel = {115, 158, 230, 255};
    Exchange.WriteTexture(SkyTexture, 0, SkyTexel.data(), SkyTexel.size(), 1, 1);
    SamplerSpecification SkySampling;
    SkySampling.MagFilter = SkySampling.MinFilter = FilterMode::Linear;
    Stage.March.Refresh          = Stage.Refresh;
    Stage.March.SurfaceDepth     = Stage.DepthView;
    Stage.March.ShadowLayered    = ConstructViewOf(Exchange, ShadowTexture, ViewDimension::PlanarLayered);
    Stage.March.ShadowComparison = Exchange.ConstructSampler(Comparison);
    Stage.March.ShadowReceiver   = Exchange.ConstructRegion(224, "Cascade receiver");
    Stage.March.SkyView          = Exchange.ConstructView(SkyTexture);
    Stage.March.SkySampler       = Exchange.ConstructSampler(SkySampling);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE BURNOUT'S REFRESH
//------------------------------------------------------------------------------------------------------------------------

// 📝 One refresh of the burnout script, through the CPU modules and onto the device: the schedule decides, the sequence uploads and records.
struct BurnoutRun
{
    SmokeSequence           Smoke;
    PlumeSimulationSequence Plume;
    Mulberry32              Noise{0x51ED270Bu};
    std::vector<std::string> RecordHashes{}, SourceHashes{};
    std::vector<uint32_t>    StepSources{}, StepRefreshes{};
    double                   StepMilliseconds = 0.0;

    BurnoutRun(DeviceExchange& Exchange, ShaderInterchange& Shaders, std::string_view Grade) : Smoke(Grade), Plume(Exchange, Shaders, Smoke.AccessSchedule()) {}

    bool Advance(DeviceExchange& Exchange, PlumeStage& Stage, int Step, std::string& Diagnostic)
    {
        std::array<double, 8> Random{};
        for (double& Value : Random) { Value = Noise(); }
        const BurnoutRefresh Refresh = ConstructBurnout(Step, Random);
        SmokeAdvanceInput    Input;
        Input.Enabled        = Refresh.Enabled;
        Input.Vehicle        = &Refresh.Vehicle;
        Input.LightDirection = LightDirection;
        Input.Gas            = &Refresh.Gas;
        Input.Readings       = &Refresh.Readings;
        Input.Thermal        = &Refresh.Thermal;
        Input.Emissions      = &Refresh.Emissions;
        Smoke.Advance(Refresh.Seconds, Input);
        Plume.UploadFireLights();
        if (!Smoke.QuerySchedule().QueryStepPending())
        {
            return true;
        }
        const PlumeSchedule& Schedule = Smoke.QuerySchedule();
        RecordHashes.push_back(HashBytes(Schedule.QueryRecordBytes().data(), PlumeRecordBytes));
        SourceHashes.push_back(HashBytes(reinterpret_cast<const uint8_t*>(Smoke.QuerySources().QueryRecords().data()),
                                         static_cast<size_t>(Smoke.QuerySources().QueryCount()) * FluidDynamics::PlumeSourceStride * sizeof(float)));
        StepSources.push_back(Smoke.QuerySources().QueryCount());
        StepRefreshes.push_back(static_cast<uint32_t>(Step));
        const auto Begun = std::chrono::steady_clock::now();
        Plume.UploadStep(Smoke.QuerySources());
        CommandSequence Sequence = Exchange.BeginSequence();
        Plume.RecordTransport(Sequence, Stage.DistanceGroup);
        const bool Done = Exchange.Submit(Sequence, &Diagnostic);
        StepMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count();
        return Done;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARITY
//------------------------------------------------------------------------------------------------------------------------

struct ParityTally
{
    int Compared = 0, Exact = 0, Failed = 0;
};

// 📝 Element for element: how many differ, by how much at most. Halves and floats are compared as numbers (a NaN equals a NaN), integers exactly.
void CompareCapture(const char* Name, const std::string& Kind, const std::vector<uint8_t>& Device, const std::vector<uint8_t>& Reference, const std::string& ReferenceHash,
                    ParityTally& Tally, double Tolerance)
{
    ++Tally.Compared;
    const std::string DeviceHash = HashBytes(Device.data(), Device.size());
    if (Device.size() != Reference.size())
    {
        ++Tally.Failed;
        std::printf("  🔴 %-11s %zu bytes on the device, %zu in the reference\n", Name, Device.size(), Reference.size());
        return;
    }
    size_t Elements = 0, Differing = 0, Worst = 0;
    double MaximumDifference = 0.0, MaximumMagnitude = 0.0;
    const auto Note = [&](double Left, double Right, size_t Index) {
        ++Elements;
        MaximumMagnitude = std::max(MaximumMagnitude, std::fabs(Right));
        const bool Same = (std::isnan(Left) && std::isnan(Right)) || Left == Right;
        if (!Same)
        {
            ++Differing;
            const double Difference = std::fabs(Left - Right);
            if (Difference > MaximumDifference) { MaximumDifference = Difference; Worst = Index; }
        }
    };
    if (Kind == "rgba16f")
    {
        for (size_t Index = 0; Index + 1 < Device.size(); Index += 2)
        {
            uint16_t A, B;
            std::memcpy(&A, &Device[Index], 2);
            std::memcpy(&B, &Reference[Index], 2);
            Note(HalfToFloat(A), HalfToFloat(B), Index / 2);
        }
    }
    else if (Kind == "f32")
    {
        for (size_t Index = 0; Index + 3 < Device.size(); Index += 4)
        {
            float A, B;
            std::memcpy(&A, &Device[Index], 4);
            std::memcpy(&B, &Reference[Index], 4);
            Note(A, B, Index / 4);
        }
    }
    else
    {
        for (size_t Index = 0; Index + 3 < Device.size(); Index += 4)
        {
            int32_t A, B;
            std::memcpy(&A, &Device[Index], 4);
            std::memcpy(&B, &Reference[Index], 4);
            Note(A, B, Index / 4);
        }
    }
    const bool Identical = DeviceHash == ReferenceHash && Differing == 0;
    if (Identical) { ++Tally.Exact; }
    const bool Acceptable = Identical || MaximumDifference <= Tolerance * std::max(1.0, MaximumMagnitude);
    if (!Acceptable) { ++Tally.Failed; }
    std::printf("  %s %-11s %9zu elements  %s  differing %zu  max |Δ| %.3g (of magnitude up to %.3g)%s\n", Acceptable ? (Identical ? "🟢" : "🟡") : "🔴", Name, Elements,
                Identical ? "bit for bit" : "not bit for bit", Differing, MaximumDifference, MaximumMagnitude,
                Differing > 0 ? (std::string("  worst at element ") + std::to_string(Worst)).c_str() : "");
}

int RunParity(const std::filesystem::path& Root, const std::filesystem::path& ShaderFolder, const std::filesystem::path& ReferenceFolder, const std::filesystem::path& Output)
{
    std::string Diagnostic;
    toml::table Reference;
    try { Reference = toml::parse_file((ReferenceFolder / "Reference.toml").string()); }
    catch (const toml::parse_error& Failure) { std::fprintf(stderr, "🔴 Reference.toml: %s\n", Failure.what()); return 1; }
    const toml::table& Run = *Reference["Run"].as_table();
    const std::string  Grade     = Run["Grade"].value<std::string>().value_or("low");
    const int          Refreshes = static_cast<int>(Run["Refreshes"].value<int64_t>().value_or(0));
    const std::array<uint32_t, 2> Trace  = {static_cast<uint32_t>(Run["TraceExtent"][0].value<int64_t>().value_or(0)), static_cast<uint32_t>(Run["TraceExtent"][1].value<int64_t>().value_or(0))};
    const std::array<uint32_t, 2> Screen = {static_cast<uint32_t>(Run["ScreenExtent"][0].value<int64_t>().value_or(0)), static_cast<uint32_t>(Run["ScreenExtent"][1].value<int64_t>().value_or(0))};

    DeviceExchange Exchange;
    if (!Exchange.Initialize("", &Diagnostic)) { std::fprintf(stderr, "🔴 device: %s\n", Diagnostic.c_str()); return 1; }
    std::printf("🧩 PlumeSimulationCapture parity on %s\n", Exchange.AdapterDescription().c_str());
    ShaderInterchange Shaders(Exchange, ShaderFolder);
    Shaders.ConfigureConstants(ConstructSpecializationConstants({96u, 0u, 0u, 0u}));
    PlumeStage Stage;
    if (!ConstructStage(Exchange, Screen, Stage, Diagnostic)) { std::fprintf(stderr, "🔴 stage: %s\n", Diagnostic.c_str()); return 1; }

    BurnoutRun Burn(Exchange, Shaders, Grade);
    if (!Burn.Plume.ConstructResources(Stage.DistanceLayout, Stage.Occupants)) { std::fprintf(stderr, "🔴 programs: %s\n", Burn.Plume.QueryDiagnostic().c_str()); return 1; }
    Burn.Plume.ConstructTargets(Trace, Stage.March);

    const auto Begun = std::chrono::steady_clock::now();
    for (int Step = 0; Step < Refreshes; ++Step)
    {
        if (!Burn.Advance(Exchange, Stage, Step, Diagnostic)) { std::fprintf(stderr, "🔴 refresh %d: %s\n", Step, Diagnostic.c_str()); return 1; }
    }
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Begun).count();

    // the frame record the reference marched with, then the march of the last refresh
    const std::vector<uint8_t> Frame = ReadBytes(ReferenceFolder / "Refresh.bin");
    if (Frame.size() != 1072) { std::fprintf(stderr, "🔴 Refresh.bin is %zu bytes, not 1072\n", Frame.size()); return 1; }
    Exchange.WriteRegion(Stage.Refresh, 0, Frame.data(), Frame.size());
    {
        CommandSequence Sequence = Exchange.BeginSequence();
        Burn.Plume.RecordMarch(Sequence);
        if (!Exchange.Submit(Sequence, &Diagnostic)) { std::fprintf(stderr, "🔴 march: %s\n", Diagnostic.c_str()); return 1; }
    }

    // ① the run itself: the same steps from the same records
    ParityTally Tally;
    int         Failures = 0;
    const auto  Expect = [&](bool Condition, const std::string& Claim) {
        std::printf("  %s %s\n", Condition ? "🟢" : "🔴", Claim.c_str());
        if (!Condition) ++Failures;
    };
    const PlumeSchedule& Schedule = Burn.Smoke.QuerySchedule();
    std::printf("🧩 %d refreshes on %s: %u transport steps in %.1f s\n", Refreshes, Grade.c_str(), Schedule.QueryStepsTaken(), Seconds);
    const auto Hashes = [&](const char* Key) {
        std::vector<std::string> Values;
        if (const toml::array* Array = Run[Key].as_array()) { for (const toml::node& Entry : *Array) { Values.push_back(Entry.value<std::string>().value_or("")); } }
        return Values;
    };
    const auto Integers = [&](const char* Key) {
        std::vector<uint32_t> Values;
        if (const toml::array* Array = Run[Key].as_array()) { for (const toml::node& Entry : *Array) { Values.push_back(static_cast<uint32_t>(Entry.value<int64_t>().value_or(-1))); } }
        return Values;
    };
    Expect(Schedule.QueryStepsTaken() == static_cast<uint32_t>(Run["StepsTaken"].value<int64_t>().value_or(-1)), "steps taken: " + std::to_string(Schedule.QueryStepsTaken()));
    Expect(Schedule.QueryState() == static_cast<uint32_t>(Run["State"].value<int64_t>().value_or(-1)), "state parity: " + std::to_string(Schedule.QueryState()));
    Expect(Schedule.QueryResident() == Run["Resident"].value<bool>().value_or(false), "residency");
    Expect(Burn.StepRefreshes == Integers("StepRefreshes"), "the refreshes that took a step");
    Expect(Burn.StepSources == Integers("StepSources"), "the source count of every step");
    Expect(Burn.RecordHashes == Hashes("StepRecordHashes"), "the 256 byte record of every step (" + std::to_string(Burn.RecordHashes.size()) + " hashes)");
    Expect(Burn.SourceHashes == Hashes("StepSourceHashes"), "the source records of every step");
    Expect(HashBytes(Schedule.QueryFireLightBytes().data(), FireLightBytes) == Run["FireLightHash"].value<std::string>().value_or(""), "the fire-light record");
    Expect(HashBytes(Frame.data(), Frame.size()) == Run["RefreshHash"].value<std::string>().value_or(""), "the frame record read from the reference");

    // ② what the device left
    struct Capture { const char* Name; };
    const auto Reference_ = [&](const char* Name) { return ReadBytes(ReferenceFolder / (std::string(Name) + ".bin")); };
    const auto Kind       = [&](const char* Name) { return Reference[Name]["Kind"].value<std::string>().value_or(""); };
    const auto Hash       = [&](const char* Name) { return Reference[Name]["Hash"].value<std::string>().value_or(""); };
    const double Tolerance = 1.0e-3;   // [-] of the larger of 1 and the capture's magnitude — the demo's half-float lattices round at 1e-3
    const uint32_t State = Schedule.QueryState();
    std::vector<uint8_t> Bytes;
    const auto Texture = [&](const char* Name, const std::shared_ptr<DeviceTexture>& Source) {
        Exchange.ReadTexture(Source, 0, 0, Bytes);
        CompareCapture(Name, Kind(Name), Bytes, Reference_(Name), Hash(Name), Tally, Tolerance);
    };
    const auto Region = [&](const char* Name, const std::shared_ptr<DeviceRegion>& Source, size_t ByteCount) {
        Exchange.ReadRegion(Source, 0, ByteCount, Bytes);
        CompareCapture(Name, Kind(Name), Bytes, Reference_(Name), Hash(Name), Tally, Tolerance);
    };
    const uint64_t CoarseCells = static_cast<uint64_t>(Schedule.QueryVelocityExtent()[0]) * Schedule.QueryVelocityExtent()[1] * Schedule.QueryVelocityExtent()[2];
    Texture("Velocity", Burn.Plume.QueryVelocityTexture(State));
    Texture("Thermo", Burn.Plume.QueryThermoTexture(State));
    Texture("Light", Burn.Plume.QueryLightTexture());
    Region("Curl", Burn.Plume.QueryCurlRegion(), CoarseCells * 16);
    Region("Divergence", Burn.Plume.QueryDivergenceRegion(), CoarseCells * 4);
    Region("PressureA", Burn.Plume.QueryPressureRegion(0), CoarseCells * 4);
    Region("PressureB", Burn.Plume.QueryPressureRegion(1), CoarseCells * 4);
    Region("Coverage", Burn.Plume.QueryCoverageRegion(), 32);
    Texture("March", Burn.Plume.QueryMarchTexture());
    std::printf("%d of %d captures bit for bit, %d beyond the tolerance of %.0e; %d claims failed\n", Tally.Exact, Tally.Compared, Tally.Failed, Tolerance, Failures);

    std::filesystem::create_directories(Output);
    std::ofstream Ledger(Output / "PlumeParity.tsv");
    Ledger << "grade\trefreshes\tsteps\tcaptures\tbit_for_bit\tbeyond_tolerance\tclaims_failed\n"
           << Grade << '\t' << Refreshes << '\t' << Schedule.QueryStepsTaken() << '\t' << Tally.Compared << '\t' << Tally.Exact << '\t' << Tally.Failed << '\t' << Failures << '\n';
    return Tally.Failed == 0 && Failures == 0 ? 0 : 1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAPTURE
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t Width  = 640;   // [px]
constexpr uint32_t Height = 400;   // [px]

struct Shot
{
    const char* Label;
    int         Refresh;   // [-] the burnout script's refresh the picture is taken after
};

// 📝 The flat backdrop the march's radiance is composited over: a sky that pales toward the horizon, a two-metre checker for the ground.
std::array<float, 3> ResolveBackdrop(const Vector3& Origin, const Vector3& Direction)
{
    if (Direction[2] >= -1.0e-4)
    {
        const float T = static_cast<float>(std::min(1.0, std::max(0.0, Direction[2] * 2.2)));
        return {0.62f + (0.16f - 0.62f) * T, 0.74f + (0.30f - 0.74f) * T, 0.88f + (0.55f - 0.88f) * T};
    }
    const double Travel = -Origin[2] / Direction[2];
    const double X = Origin[0] + Direction[0] * Travel, Y = Origin[1] + Direction[1] * Travel;
    const bool   Dark = (static_cast<long>(std::floor(X / 2.0)) + static_cast<long>(std::floor(Y / 2.0))) % 2 == 0;
    const float  Fade = static_cast<float>(std::min(1.0, Travel / 90.0));
    const float  Base = Dark ? 0.16f : 0.21f;
    return {Base + (0.62f - Base) * Fade, Base + (0.74f - Base) * Fade, Base + (0.88f - Base) * Fade};
}

int RunCapture(const std::filesystem::path& Root, const std::filesystem::path& ShaderFolder, const std::filesystem::path& Output, std::string_view Grade, uint32_t TraceScale)
{
    std::string Diagnostic;
    std::filesystem::create_directories(Output);
    const std::array<uint32_t, 2> Screen = {Width, Height};
    const std::array<uint32_t, 2> Trace  = {Width / TraceScale, Height / TraceScale};
    DeviceExchange Exchange;
    if (!Exchange.Initialize("", &Diagnostic)) { std::fprintf(stderr, "🔴 device: %s\n", Diagnostic.c_str()); return 1; }
    std::printf("🧩 PlumeSimulationCapture on %s, grade %s\n", Exchange.AdapterDescription().c_str(), std::string(Grade).c_str());
    ShaderInterchange Shaders(Exchange, ShaderFolder);
    Shaders.ConfigureConstants(ConstructSpecializationConstants({96u, 0u, 0u, 0u}));
    PlumeStage Stage;
    if (!ConstructStage(Exchange, Screen, Stage, Diagnostic)) { std::fprintf(stderr, "🔴 stage: %s\n", Diagnostic.c_str()); return 1; }
    BurnoutRun Burn(Exchange, Shaders, Grade);
    if (!Burn.Plume.ConstructResources(Stage.DistanceLayout, Stage.Occupants)) { std::fprintf(stderr, "🔴 programs: %s\n", Burn.Plume.QueryDiagnostic().c_str()); return 1; }
    Burn.Plume.ConstructTargets(Trace, Stage.March);
    const PlumeDescription Description = Burn.Smoke.QuerySchedule().QueryDescription();
    std::printf("🧩 lattices %ux%ux%u fine at %.0f cm, %ux%ux%u coarse, %u pressure sweeps, %u march samples, %.1f MiB; march %ux%u for %ux%u\n", Description.Thermo[0], Description.Thermo[1],
                Description.Thermo[2], Description.EdgeThermo * 100.0, Description.Velocity[0], Description.Velocity[1], Description.Velocity[2], Description.Sweeps, Description.Samples,
                static_cast<double>(Description.Bytes) / (1024.0 * 1024.0), Trace[0], Trace[1], Width, Height);

    const std::vector<Shot> Shots = {{"Tyre smoke 1.0 s", 60}, {"Thick burnout 2.0 s", 120}, {"Fire 3.0 s", 180}, {"Inferno and burst 3.8 s", 230},
                                     {"Crosswind 5.0 s", 300}, {"Shredded 5.7 s", 345}, {"Tyres burning out 6.5 s", 390}, {"Switched off 7.0 s", 419}};
    std::ofstream Ledger(Output / "Captures.tsv");
    Ledger << "capture\trefresh\tsteps\tsources\tresident\tpixels_with_smoke\tdarkest_transmittance\tmarch_ms\n";
    std::vector<std::vector<uint8_t>> Pictures;
    RefreshEncoder Encoder;
    const LightSolution  Light{};
    RefreshToggles Toggles;
    RefreshExternalInputs External;
    int Failures = 0;
    size_t Next = 0;
    for (int Step = 0; Step <= Shots.back().Refresh && Next < Shots.size(); ++Step)
    {
        if (!Burn.Advance(Exchange, Stage, Step, Diagnostic)) { std::fprintf(stderr, "🔴 refresh %d: %s\n", Step, Diagnostic.c_str()); return 1; }
        if (Step != Shots[Next].Refresh) { continue; }
        const Shot& Current = Shots[Next++];

        // the camera: from the side, a little behind the car, looking at the plume's foot
        const BurnoutRefresh Placement = ConstructBurnout(Step, {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5});
        const Vector3 Car = Placement.Vehicle.Position;
        CameraPose Camera;
        Camera.Position            = {Car[0] - 3.0, -12.0, 3.2};
        Camera.Target              = {Car[0] - 4.0, 0.0, 1.4};
        Camera.Forward             = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
        Camera.VerticalFieldOfView = 0.62;
        Camera.Aspect              = static_cast<double>(Width) / Height;
        Camera.Near                = 0.1;
        LightSolution Sun = Light;
        Sun.Direction     = NormalizeVector(LightDirection);
        Sun.SunDirection  = Sun.Direction;
        Sun.Illuminance   = {3.0, 2.9, 2.7};
        std::vector<uint8_t> Frame = Encoder.Encode(Camera, Sun, Toggles, External, Screen, Trace, TraceScale, static_cast<uint32_t>(Step), Camera.Target, Step > 0);
        Exchange.WriteRegion(Stage.Refresh, 0, Frame.data(), Frame.size());
        const auto Begun = std::chrono::steady_clock::now();
        CommandSequence Sequence = Exchange.BeginSequence();
        Burn.Plume.RecordMarch(Sequence);
        std::vector<uint8_t> Radiance;
        if (!Exchange.Submit(Sequence, &Diagnostic) || !Exchange.ReadTexture(Burn.Plume.QueryMarchTexture(), 0, 0, Radiance)) { std::printf("  🔴 %s: %s\n", Current.Label, Diagnostic.c_str()); ++Failures; continue; }
        const double MarchMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count();
        // 📝 A plume that is not resident records no march at all (the burnout script switches it off for ten refreshes at 400): the picture then holds the
        //    backdrop alone, as the host's composite would without a plume.
        const bool Idle = !Burn.Smoke.QuerySchedule().QueryResident();
        if (Idle)
        {
            for (size_t Texel = 0; Texel < Radiance.size() / 8; ++Texel)
            {
                const uint16_t One = 0x3C00;   // 1.0 as a half
                std::memset(&Radiance[Texel * 8], 0, 6);
                std::memcpy(&Radiance[Texel * 8 + 6], &One, 2);
            }
        }

        // composite over the backdrop: premultiplied radiance plus transmittance times the backdrop, the march's own pixel nearest each picture pixel
        const Transform ViewTransform = ConstructViewTransform(Camera.Position, Camera.Target);
        const Vector3   Right   = NormalizeVector(CrossVector(Camera.Forward, {0.0, 0.0, 1.0}));
        const Vector3   Up      = CrossVector(Right, Camera.Forward);
        const double    Half    = std::tan(Camera.VerticalFieldOfView * 0.5);
        std::vector<uint8_t> Picture(static_cast<size_t>(Width) * Height * 3u);
        uint64_t Smoky = 0;
        float    Darkest = 1.0f;
        (void)ViewTransform;
        for (uint32_t Y = 0; Y < Height; ++Y)
        {
            for (uint32_t X = 0; X < Width; ++X)
            {
                const double U = ((X + 0.5) / Width * 2.0 - 1.0) * Half * Camera.Aspect, V = (1.0 - (Y + 0.5) / Height * 2.0) * Half;
                const Vector3 Direction = NormalizeVector(AddVector(Camera.Forward, AddVector(ScaleVector(Right, U), ScaleVector(Up, V))));
                const std::array<float, 3> Backdrop = ResolveBackdrop(Camera.Position, Direction);
                const size_t Texel = (static_cast<size_t>(Y / TraceScale) * Trace[0] + X / TraceScale) * 4u;
                float Channels[4];
                for (int Channel = 0; Channel < 4; ++Channel)
                {
                    uint16_t Half16;
                    std::memcpy(&Half16, &Radiance[(Texel + Channel) * 2], 2);
                    Channels[Channel] = HalfToFloat(Half16);
                }
                if (Channels[3] < 0.99f) ++Smoky;
                Darkest = std::min(Darkest, Channels[3]);
                for (int Channel = 0; Channel < 3; ++Channel)
                {
                    Picture[(static_cast<size_t>(Y) * Width + X) * 3u + Channel] = static_cast<uint8_t>(ProjectSrgb(Channels[Channel] * 0.42f + Channels[3] * Backdrop[Channel]) * 255.0f + 0.5f);
                }
            }
        }
        std::string Name = Current.Label;
        std::replace(Name.begin(), Name.end(), ' ', '-');
        if (!stbi_write_png((Output / ("Plume-" + Name + ".png")).string().c_str(), static_cast<int>(Width), static_cast<int>(Height), 3, Picture.data(), static_cast<int>(Width * 3u))) ++Failures;
        const PlumeSchedule& Schedule = Burn.Smoke.QuerySchedule();
        std::printf("  %-26s refresh %3d  steps %3u  sources %2u  %s  smoke over %6llu px  darkest transmittance %.4f  march %.0f ms\n", Current.Label, Step, Schedule.QueryStepsTaken(),
                    Burn.Smoke.QuerySources().QueryCount(), Idle ? "idle    " : "resident", static_cast<unsigned long long>(Smoky), Darkest, Idle ? 0.0 : MarchMilliseconds);
        Ledger << Current.Label << '\t' << Step << '\t' << Schedule.QueryStepsTaken() << '\t' << Burn.Smoke.QuerySources().QueryCount() << '\t' << (Schedule.QueryResident() ? 1 : 0) << '\t'
               << Smoky << '\t' << Darkest << '\t' << MarchMilliseconds << '\n';
        Pictures.push_back(std::move(Picture));
    }

    // the contact sheet: two columns of the shots at half size
    if (Pictures.size() == Shots.size())
    {
        const uint32_t Columns = 2, Rows = static_cast<uint32_t>((Pictures.size() + Columns - 1) / Columns), CellWidth = Width / 2, CellHeight = Height / 2;
        std::vector<uint8_t> Sheet(static_cast<size_t>(Columns) * CellWidth * Rows * CellHeight * 3u, 255u);
        for (size_t Index = 0; Index < Pictures.size(); ++Index)
        {
            const uint32_t Column = static_cast<uint32_t>(Index % Columns), Row = static_cast<uint32_t>(Index / Columns);
            for (uint32_t Y = 0; Y < CellHeight; ++Y)
            {
                for (uint32_t X = 0; X < CellWidth; ++X)
                {
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        unsigned Total = 0;
                        for (uint32_t Sample = 0; Sample < 4; ++Sample) Total += Pictures[Index][((Y * 2 + Sample / 2) * Width + X * 2 + Sample % 2) * 3 + Channel];
                        Sheet[(((Row * CellHeight + Y) * Columns * CellWidth) + Column * CellWidth + X) * 3 + Channel] = static_cast<uint8_t>(Total / 4);
                    }
                }
            }
        }
        if (!stbi_write_png((Output / "Plume-Burnout.png").string().c_str(), static_cast<int>(Columns * CellWidth), static_cast<int>(Rows * CellHeight), 3, Sheet.data(),
                            static_cast<int>(Columns * CellWidth * 3u))) ++Failures;
    }
    std::printf("%s\n", Failures == 0 ? "PlumeSimulationCapture: every shot written" : "PlumeSimulationCapture: failures above");
    return Failures == 0 ? 0 : 1;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::string Mode = ArgumentCount > 1 ? Arguments[1] : "";
    if (Mode == "capture" && ArgumentCount >= 5)
    {
        return RunCapture(Arguments[2], Arguments[3], Arguments[4], ArgumentCount > 5 ? Arguments[5] : "low", ArgumentCount > 6 ? static_cast<uint32_t>(std::atoi(Arguments[6])) : 2u);
    }
    if (Mode == "parity" && ArgumentCount >= 6)
    {
        return RunParity(Arguments[2], Arguments[3], Arguments[4], Arguments[5]);
    }
    std::fprintf(stderr, "usage: PlumeSimulationCapture capture <repository root> <shader folder> <output folder> [grade] [trace scale]\n"
                         "       PlumeSimulationCapture parity  <repository root> <shader folder> <reference folder> <output folder>\n");
    return 2;
}
