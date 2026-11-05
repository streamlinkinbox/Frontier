/*==========================================================================================================================================
                                                       PLUMESOURCEPROJECTION.H
==========================================================================================================================================*/
// 🧩 Vehicle telemetry projected into smoke, soot and fire sources, fire lights and ember emitters: onset hysteresis, burnout, burning rubber,
//    pops. A port of the HMI prototype's PlumeSourceProjection.js (itself the gas vehicle intake of Frontier's smoke: SmokeOnsetClassifier and
//    ResolveWheelSmoke), statement for statement.

#pragma once

#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier::FluidDynamics
{

using GlobalIllumination::Vector3;

// 📝 SmokeSpecification's constants.
struct SmokeSpecification
{
    static constexpr double SourcePowerOnset     = 200.0;      // [W]
    static constexpr double SourceReleaseRatio   = 0.60;       // [-]
    static constexpr double SourceOnsetSeconds   = 0.05;       // [s]
    static constexpr double SourceReleaseSeconds = 0.08;       // [s]
    static constexpr double SourcePowerFull      = 18000.0;    // [W]
    static constexpr double MinimumSlipSpeed     = 0.35;       // [m/s]
    static constexpr double DensityRate          = 18.0;       // [1/s]
    static constexpr double CarrierVelocityShare = 0.15;       // [-]
    static constexpr double SmokeHeatRate        = 0.04;       // [K/s]
    static constexpr double FireHeatPowerScale   = 0.001;      // [K/J]
    static constexpr double PatchRadius          = 0.16;       // [m]   GasRoadTyrePatchRadius
};

inline constexpr uint32_t PlumeSourceStride   = 12;   // [f32] PlumeSource: position·radius, velocity·spare, rates (smoke, heat, flame, soot)
inline constexpr uint32_t PlumeSourceCapacity = 24;   // [-]   four patches, four arches, eight flame seats, two exhaust tips, four bursts, two spare

// 📝 One tyre as the projection reads it: the wheel's contact reading, its tread's heat and its geometry (SmokeSequence.js ProjectPlumeTelemetry).
struct PlumeWheelHeat
{
    bool   Ignited            = false;
    double CombustionPower    = 0.0;   // [W]
    double SurfaceTemperature = 0.0;   // [K]
};

struct PlumeWheel
{
    bool                   InContact     = false;
    double                 SlidingPower  = 0.0;   // [W]
    double                 SlipSpeed     = 0.0;   // [m/s]
    Vector3                ContactPoint{};        // [m]
    Vector3                ContactVelocity{};     // [m/s]
    Vector3                HubPosition{};         // [m]
    PlumeWheelHeat         Thermal{};
    bool                   Burst         = false;
    double                 Radius        = 0.0;   // [m]
    std::optional<Vector3> Longitudinal;          // [-]   the wheel's heading; absent reads as +X
    double                 SurfaceSpeed  = 0.0;   // [m/s]
};

struct PlumeExhaust
{
    Vector3 Position{};    // [m]
    Vector3 Direction{};   // [-]
};

struct PlumeTelemetry
{
    Vector3                   Position{};
    Vector3                   Velocity{};
    std::vector<PlumeExhaust> Exhausts;
    double                    AfterfireSeconds = 0.0;   // [s]
    std::vector<PlumeWheel>   Wheels;
};

// 📝 Options: { Afterfire, EmissionRate, SmokeYield, HeatYield, SootYield, FireLight, Embers }; an absent option reads as its default here.
struct PlumeProjectionOptions
{
    bool   Afterfire    = false;
    double EmissionRate = 1.0;
    double SmokeYield   = 1.0;
    double HeatYield    = 1.0;
    double SootYield    = 1.0;
    double FireLight    = 1.0;
    double Embers       = 1.0;
};

// 📝 A transient source: queued with a duration and emitted until done. Remaining is set by QueueBurst.
struct PlumeBurst
{
    Vector3 Position{};
    double  Radius  = 0.0;
    Vector3 Velocity{};
    double  Smoke   = 0.0;
    double  Heat    = 0.0;
    double  Flame   = 0.0;
    double  Soot    = 0.0;
    double  Seconds = 0.0;
    double  Remaining = 0.0;
};

struct PlumeFireLight
{
    Vector3 Position{};   // [m]
    double  Radius = 0.0; // [m]
    Vector3 Radiance{};   // [-] linear
};

struct PlumeEmber
{
    Vector3 Position{};
    Vector3 Velocity{};
    double  Rate        = 0.0;
    double  Temperature = 0.0;   // [K]
    double  Spread      = 0.0;
    double  Lift        = 0.0;
    double  Gravity     = 0.0;
    double  Life        = 0.0;   // [s]
};

struct SmokeOnset
{
    double RequestSeconds = 0.0;
    double QuietSeconds   = 0.0;
    bool   Active         = false;
};

struct PlumeBounds
{
    Vector3 Minimum{};
    Vector3 Maximum{};
};

class PlumeSourceProjection
{
public:
    PlumeSourceProjection();

    void Reset();

    // 📝 xorshift32: Seed ^= Seed << 13; Seed ^= Seed >>> 17; Seed ^= Seed << 5 (the same bits as the JavaScript's int32 operators).
    [[nodiscard]] double ProjectUniform();

    // 📝 Queues a transient source: the newest four are kept.
    void QueueBurst(const PlumeBurst& Burst);

    // 📝 AdvanceSmokeOnset(): entry needs power and slip speed sustained, release has grace, contact loss cuts at once.
    bool AdvanceOnset(SmokeOnset& Onset, double Power, double SlipSpeed, bool Contact, double Seconds);

    // 📝 Fills Records with this advance's sources, Lights with the fire lights and Embers with the ember emitters; returns the source count.
    uint32_t Project(const PlumeTelemetry& Telemetry, double Seconds, const PlumeProjectionOptions& Options);

    // 📝 Bounds of every source grown by 3.5 of its radii (the Gaussian's reach), for the lattice's cheap reject.
    [[nodiscard]] PlumeBounds ResolveBounds() const;

    // 📝 Appends one source by hand (the HMI's editor, fracture and burst emitters), as SmokeSequence.Advance does after the tyres.
    void AppendRecord(const std::array<float, PlumeSourceStride>& Record);

    [[nodiscard]] uint32_t                                                          QueryCount() const noexcept { return Count_; }
    [[nodiscard]] uint32_t                                                          QueryActiveTyres() const noexcept { return ActiveTyres_; }
    [[nodiscard]] bool                                                              QueryPopping() const noexcept { return Popping_; }
    [[nodiscard]] double                                                            QueryElapsed() const noexcept { return Elapsed_; }
    [[nodiscard]] uint32_t                                                          QuerySeed() const noexcept { return Seed_; }
    [[nodiscard]] const std::array<float, PlumeSourceCapacity * PlumeSourceStride>& QueryRecords() const noexcept { return Records_; }
    [[nodiscard]] const std::vector<PlumeFireLight>&                                QueryLights() const noexcept { return Lights_; }
    [[nodiscard]] const std::vector<PlumeEmber>&                                    QueryEmbers() const noexcept { return Embers_; }
    [[nodiscard]] const std::vector<PlumeBurst>&                                    QueryBursts() const noexcept { return Bursts_; }
    [[nodiscard]] const std::array<SmokeOnset, 4>&                                  QueryOnsets() const noexcept { return Onsets_; }

private:
    std::array<SmokeOnset, 4>                                    Onsets_{};
    std::array<double, 2>                                        PopSeconds_{};
    std::array<double, 2>                                        PopStrength_{};
    uint32_t                                                     Seed_ = 0x2545F491u;
    std::array<float, PlumeSourceCapacity * PlumeSourceStride>   Records_{};
    uint32_t                                                     Count_       = 0;
    uint32_t                                                     ActiveTyres_ = 0;
    bool                                                         Popping_     = false;
    std::vector<PlumeBurst>                                      Bursts_;
    std::vector<PlumeFireLight>                                  Lights_;
    std::vector<PlumeEmber>                                      Embers_;
    double                                                       Elapsed_ = 0.0;
};

} // namespace Frontier::FluidDynamics
