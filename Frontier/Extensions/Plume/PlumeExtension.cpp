/*==========================================================================================================================================
                                                        PLUMEEXTENSION.CPP
==========================================================================================================================================*/
// 🧩 One refresh of the Smoke & fire editor's viewport: bring-up on first use, the emitter and the gas handed to the plume's sources, one step of
//    the transport when the schedule asks for it, the march from the editor's camera, and the march's radiance composited over a backdrop into the
//    picture the editor shows.

#include "Frontier/Extensions/Plume/PlumeExtension.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier
{

using namespace GlobalIllumination;
using namespace PlumeSimulation;

namespace
{

constexpr FluidDynamics::Vector3 StageLight  = { 0.3, 0.2, 0.9327379053088815 };   // [-]  the sun's direction: the plume exhibit's
constexpr double                 CarHeight   = 0.55;                                // [m]  the parked car's reference point above the ground
constexpr double                 RaysPerRefresh = 250000.0;                         // [-]  march rays a refresh may spend

// 📝 ceil(sqrt(pixels / rays)) picture pixels to a trace texel, two at least.
uint32_t ResolveTraceScale(std::array<uint32_t, 2> Extent)
{
    const double Scale = std::ceil(std::sqrt(static_cast<double>(Extent[0]) * static_cast<double>(Extent[1]) / RaysPerRefresh));
    return std::max(2u, static_cast<uint32_t>(Scale));
}

} // namespace

PlumeExtension::PlumeExtension(std::filesystem::path ShaderRoot, const VulkanDeviceAccess* HostDevice) : ShaderRoot_(std::move(ShaderRoot))
{
    if (HostDevice != nullptr && HostDevice->Device != VK_NULL_HANDLE)
    {
        Device_             = *HostDevice;
        HostDeviceAttached_ = true;
    }
}

PlumeExtension::~PlumeExtension() = default;

std::string PlumeExtension::ResolveGradeName(const WorldSpace& World)
{
    const auto* Entry = dynamic_cast<const RenderPipelineRecord*>(const_cast<WorldSpace&>(World).FindFirstByType("RenderPipeline"));
    const int   Tier  = Entry != nullptr ? Entry->QueryConfiguration().QualityTier : 2;
    return Tier <= 0 ? "low" : (Tier == 1 ? "medium" : "high");
}

PlumeEmission PlumeExtension::ResolveTestEmission(bool Burning, const FluidDynamics::Vector3& CarPosition, const FluidDynamics::Vector3& Forward)
{
    // 📝 The prototype's emission also names a hub, a strength, an expansion, a second (soot) plume and the combustion power; the plume's sources read
    //    only the position, the radius, the velocity and the four rates (SmokeSequence.js Advance), so those are what this carries.
    const FluidDynamics::Vector3 Point = { CarPosition[0] + 3.0 * Forward[0], CarPosition[1] + 3.0 * Forward[1], 0.05 };
    PlumeEmission                Emission;
    Emission.Active    = true;
    Emission.Position  = Burning ? FluidDynamics::Vector3{ Point[0], Point[1], 0.25 } : Point;
    Emission.Radius    = Burning ? 0.45 : 0.35;
    Emission.Velocity  = { 0.0, 0.0, Burning ? 1.2 : 0.35 };
    Emission.SmokeRate = Burning ? 0.0 : 26.0;
    Emission.SootRate  = 0.0;
    Emission.FlameRate = Burning ? 12.0 : 0.0;
    Emission.HeatRate  = Burning ? 39.0 : 1.2;
    return Emission;
}

bool PlumeExtension::ConstructDevice(std::array<uint32_t, 2> Picture, std::string& Diagnostic)
{
    Exchange_ = std::make_unique<DeviceExchange>();
    if (HostDeviceAttached_ ? !Exchange_->AttachDevice(Device_, &Diagnostic) : !Exchange_->Initialize("", &Diagnostic)) return false;
    Shaders_ = std::make_unique<ShaderInterchange>(*Exchange_, ShaderRoot_ / "PlumeSimulation");
    Shaders_->ConfigureConstants(ConstructSpecializationConstants({ 96u, 0u, 0u, 0u }));
    StandInExtent_ = Picture;
    return ConstructPlumeStandIns(*Exchange_, Picture, StandIns_, Diagnostic);
}

bool PlumeExtension::ConstructPlume(const std::string& Grade, std::string& Diagnostic)
{
    Plume_.reset();   // 📝 the lattices reference the schedule: they go first
    Smoke_ = std::make_unique<SmokeSequence>(Grade);
    Plume_ = std::make_unique<PlumeSimulationSequence>(*Exchange_, *Shaders_, Smoke_->AccessSchedule());
    if (!Plume_->ConstructResources(StandIns_.DistanceLayout, StandIns_.Occupants))
    {
        Diagnostic = Plume_->QueryDiagnostic();
        return false;
    }
    Grade_ = Grade;
    return true;
}

bool PlumeExtension::ConstructPicture(std::array<uint32_t, 2> Picture, std::array<uint32_t, 2> Trace, std::string& Diagnostic)
{
    if (StandInExtent_ != Picture)
    {
        if (!ConstructStandInDepth(*Exchange_, Picture, StandIns_, Diagnostic)) return false;
        StandInExtent_ = Picture;
    }
    TextureSpecification Specification;
    Specification.Label  = "Plume picture";
    Specification.Width  = Picture[0];
    Specification.Height = Picture[1];
    Specification.Format = TextureFormat::Rgba8Unorm;
    Specification.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget | TextureUsage::CopySource;
    Presented_           = Exchange_->ConstructTexture(Specification);
    PresentedView_       = Exchange_->ConstructView(Presented_);
    Plume_->ConstructTargets(Trace, StandIns_.March);
    Picture_ = Picture;
    Trace_   = Trace;
    return true;
}

bool PlumeExtension::ReadPresentedImage(std::vector<uint8_t>& Rgba, std::string* Diagnostic)
{
    if (!Presented_ || !Exchange_->ReadTexture(Presented_, 0, 0, Rgba))
    {
        if (Diagnostic) *Diagnostic = "the plume picture could not be read back";
        return false;
    }
    return true;
}

PlumeViewportOutcome PlumeExtension::Render(const PlumeViewportRequest& Request, const WorldSpace& World)
{
    PlumeViewportOutcome Outcome;
    if (Request.Gas == nullptr || Request.Extent[0] < 32 || Request.Extent[1] < 32)
    {
        Outcome.Image.Failure = "There is no stage to draw.";
        return Outcome;
    }
    const auto                   Begun   = std::chrono::steady_clock::now();
    const std::string            Grade   = ResolveGradeName(World);
    const uint32_t               Scale   = ResolveTraceScale(Request.Extent);
    const std::array<uint32_t, 2> Trace   = { (Request.Extent[0] + Scale - 1u) / Scale, (Request.Extent[1] + Scale - 1u) / Scale };
    const std::array<uint32_t, 2> Picture = { Trace[0] * Scale, Trace[1] * Scale };

    // 📝 On the editor's queue the previous interface refresh may still sample the presented picture; this refresh replaces what it needs to.
    if (HostDeviceAttached_ && Exchange_) Exchange_->AwaitIdle();
    std::string Diagnostic;
    if (Failure_.empty() && Exchange_ == nullptr && !ConstructDevice(Picture, Diagnostic)) Failure_ = "Plume bring-up failed: " + Diagnostic;
    if (Failure_.empty() && (Plume_ == nullptr || Grade != Grade_))
    {
        Picture_ = { 0, 0 };   // the march's targets belong to the plume that is replaced
        if (!ConstructPlume(Grade, Diagnostic)) Failure_ = "Plume programs failed: " + Diagnostic;
    }
    if (Failure_.empty() && (Picture != Picture_ || Presented_ == nullptr))
    {
        TraceScale_ = Scale;
        if (!ConstructPicture(Picture, Trace, Diagnostic)) Failure_ = "Plume picture failed: " + Diagnostic;
    }
    if (!Failure_.empty())
    {
        Outcome.Image.Failure = Failure_;
        return Outcome;
    }
    Outcome.Image.Adapter = Exchange_->AdapterDescription();

    // ① the sources: a parked car (no tyre slides, nothing burning) and the emitter the editor asks for, beside the four entries the tyres own
    if (Request.Clear) Smoke_->Clear();
    FluidDynamics::PlumeVehicleBody Body;
    Body.Position = { 0.0, 0.0, CarHeight };
    std::vector<FluidDynamics::PlumeWheelReading> Readings(4);
    std::vector<FluidDynamics::PlumeWheelThermal> Thermal(4);
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        const bool           Front   = Wheel < 2;
        const Vector3        Contact = { Body.Position[0] + (Front ? 1.3 : -1.3), Body.Position[1] + ((Wheel & 1) ? -0.8 : 0.8), 0.0 };
        Body.Wheels.push_back({ 0.0, 0.34, 0.0 });
        Readings[Wheel].ContactFraction = 1.0;
        Readings[Wheel].ContactPoint    = Contact;
        Readings[Wheel].HubPosition     = { Contact[0], Contact[1], 0.34 };
        Thermal[Wheel].Surface          = 293.15;
    }
    std::vector<PlumeEmission> Emissions(4);
    if (Request.TestEmitter > 0) Emissions.push_back(ResolveTestEmission(Request.TestEmitter == 2, Body.Position, { 1.0, 0.0, 0.0 }));

    SmokeAdvanceInput Input;
    Input.Enabled        = Request.Enabled;
    Input.Vehicle        = &Body;
    Input.LightDirection = StageLight;
    Input.Gas            = Request.Gas;
    Input.Readings       = &Readings;
    Input.Thermal        = &Thermal;
    Input.Emissions      = &Emissions;
    Smoke_->Advance(Request.Seconds, Input);
    Plume_->UploadFireLights();

    // ② one step of the transport, when the schedule says one is due
    if (Smoke_->QuerySchedule().QueryStepPending())
    {
        Plume_->UploadStep(Smoke_->QuerySources());
        CommandSequence Sequence = Exchange_->BeginSequence();
        Plume_->RecordTransport(Sequence, StandIns_.DistanceGroup);
        if (!Exchange_->Submit(Sequence, &Diagnostic))
        {
            Outcome.Image.Failure = "Plume step failed: " + Diagnostic;
            return Outcome;
        }
    }

    // ③ the camera: an orbit about the emitter, z up
    const Vector3 Target = { 3.0, 0.0, 1.8 };
    const double  Level  = std::cos(Request.Pitch);
    CameraPose    Camera;
    Camera.Position            = { Target[0] + Request.Distance * std::sin(Request.Yaw) * Level, Target[1] - Request.Distance * std::cos(Request.Yaw) * Level,
                                   Target[2] + Request.Distance * std::sin(Request.Pitch) };
    Camera.Target              = Target;
    Camera.Forward             = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
    Camera.VerticalFieldOfView = 0.78;
    Camera.Aspect              = static_cast<double>(Picture_[0]) / static_cast<double>(Picture_[1]);
    Camera.Near                = 0.1;
    LightSolution Sun;
    Sun.Direction    = NormalizeVector(StageLight);
    Sun.SunDirection = Sun.Direction;
    Sun.Illuminance  = { 3.0, 2.9, 2.7 };
    const RefreshToggles        Toggles{};
    const RefreshExternalInputs External{};
    const std::vector<uint8_t> Record = Encoder_.Encode(Camera, Sun, Toggles, External, Picture_, Trace_, TraceScale_, Refreshes_, Camera.Target, Refreshes_ > 0);
    Exchange_->WriteRegion(StandIns_.Refresh, 0, Record.data(), Record.size());
    ++Refreshes_;

    // ④ the march, read back, and composited over the backdrop
    const bool Idle = !Smoke_->QuerySchedule().QueryResident();
    if (!Idle)
    {
        CommandSequence Sequence = Exchange_->BeginSequence();
        Plume_->RecordMarch(Sequence);
        if (!Exchange_->Submit(Sequence, &Diagnostic) || !Exchange_->ReadTexture(Plume_->QueryMarchTexture(), 0, 0, Radiance_))
        {
            Outcome.Image.Failure = "Plume march failed: " + Diagnostic;
            return Outcome;
        }
    }
    ResolvePlumePicture(Radiance_, Idle, Camera, Picture_, Trace_, TraceScale_, Backdrop_, Pixels_);
    Exchange_->WriteTexture(Presented_, 0, Pixels_.data(), Pixels_.size(), Picture_[0], Picture_[1]);
    CommandSequence Landing = Exchange_->BeginSequence();   // 📝 an empty sequence: the queued write lands before the next submission, and the submission waits
    if (!Exchange_->Submit(Landing, &Diagnostic))
    {
        Outcome.Image.Failure = "Plume picture could not be written: " + Diagnostic;
        return Outcome;
    }

    // ⑤ what the editor reads
    const SmokeMetrics& Metrics = Smoke_->QueryMetrics();
    Outcome.View                = PresentedView_->Handle;
    Outcome.Image.Extent        = Picture_;
    Outcome.Image.Solving       = true;
    Outcome.Image.Readout       = { Grade == "low" ? 0u : (Grade == "medium" ? 1u : 2u), !Idle, Metrics.Sources, Metrics.Ticks, Metrics.Relocations };
    Outcome.Image.RefreshMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count();
    return Outcome;
}

} // namespace Frontier
