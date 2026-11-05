/*==========================================================================================================================================
                                                          PLUMESCHEDULE.H
==========================================================================================================================================*/
// 🧩 The smoke-and-fire lattices' bookkeeping, apart from the device: residency, the fixed-rate step, the moving window, the step record, the
//    fire-light record, and the tiling detail volume. A port of PlumeSpace.js (Schedule, RecentreWindow, EncodeRecord, AssignFireLights, Clear,
//    QueryDescription, ConstructDetail) statement for statement; PlumeSimulationSequence holds the device half.
//
// 📝 One step runs at most once per refresh at no more than 60 Hz (a 120 Hz display steps every other refresh), its duration the time accumulated
//    since the last step, clamped to 1/30 s — semi-Lagrangian transport is unconditionally stable, so a slow refresh is one longer step rather
//    than a spiral of short ones.

#pragma once

#include "Frontier/Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"
#include "Frontier/Engine/PhysicalDynamics/FluidDynamics/PlumeSourceProjection.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::PlumeSimulation
{

using FluidDynamics::PlumeFireLight;
using FluidDynamics::PlumeGrade;
using FluidDynamics::PlumeSettings;
using FluidDynamics::PlumeSourceProjection;
using FluidDynamics::Vector3;

inline constexpr uint32_t PlumeRecordBytes    = 256;          // [B]   PlumeRecord: sixteen vec4
inline constexpr uint32_t FireLightBytes      = 144;          // [B]   FireLightRecord
inline constexpr uint32_t FireLightLimit      = 4;            // [-]
inline constexpr double   StepSeconds         = 1.0 / 60.0;   // [s]   shortest step
inline constexpr double   StepCeiling         = 1.0 / 30.0;   // [s]   longest step
inline constexpr double   FadeCeiling         = 12.0;         // [s]   SmokeSpecification::FadeCeiling — the plume is gone this long after the last source
inline constexpr int32_t  RestartOrigin       = 1 << 24;      // [cell] a prior window nothing lies in: every read of the old state returns nothing
inline constexpr uint32_t DetailExtent        = 32;           // [texel] tiling detail-noise volume edge
inline constexpr double   FloorHeight         = -0.5;         // [m]   the window's floor, under the course
inline constexpr double   FlameScale          = 80.0;         // [-]   flame emission at PlumeFire 1
inline constexpr uint32_t BytesPerFineCell    = 16;           // [B]   two rgba16float thermo states
inline constexpr uint32_t BytesPerCoarseCell  = 16 + 8 + 16 + 4 + 8;   // [B] two velocity states, light, curl, divergence, two pressures

// 📝 The device-side readout (QueryDescription), as numbers; the editor formats them.
struct PlumeDescription
{
    std::array<uint32_t, 3> Thermo{};     // [cell]
    std::array<uint32_t, 3> Velocity{};   // [cell]
    double                  EdgeThermo = 0.0;    // [m]
    double                  EdgeVelocity = 0.0;  // [m]
    std::array<double, 3>   Span{};       // [m]
    uint64_t                Cells = 0;
    uint64_t                Bytes = 0;
    std::string             Grade;
    uint32_t                Sweeps  = 0;
    uint32_t                Samples = 0;
};

class PlumeSchedule
{
public:
    explicit PlumeSchedule(const PlumeGrade& Configuration);

    // 📝 Switches to another grade: new extents, a clean plume.
    void AssignConfiguration(const PlumeGrade& Configuration);

    // 📝 Called once per refresh with the sources of this refresh. Decides residency and whether a step runs, moves the window when the focus
    //    leaves its middle half, and encodes the step record. The caller uploads the record, the sources and the coverage reset when
    //    QueryStepPending() says so.
    void Schedule(double Seconds, double RefreshSeconds, const Vector3& Focus, const PlumeSourceProjection& Sources, const PlumeSettings& Settings,
                  const Vector3& SunDirection);

    // 📝 Lights: brightest first; written every refresh, resident or not, because the direct radiance pass lights the scene with them.
    void AssignFireLights(const std::vector<PlumeFireLight>& Lights);

    // 📝 Forgets the plume at once: the next source restarts a clean lattice.
    void Clear();

    // 📝 PlumeSimulationSequence calls this after recording a step: the state parity flips and the step is counted.
    void CompleteStep();

    // 📝 ConstructLattices(): fresh lattices are at parity 0 and hold no plume.
    void ResetState();

    // 📝 The pressure sweeps the editor asks for (SmokeSequence.Advance): at least 4, even.
    void AssignPressureSweeps(double Requested);

    [[nodiscard]] PlumeDescription QueryDescription() const;

    // 📐 The tiling detail volume: three octaves of periodic value noise and one of inverted Worley cells, so the march can fold them into
    //    billows. Periodic in every axis, so a repeat sampler never shows a seam. rgba8unorm, DetailExtent³ texels, x fastest.
    [[nodiscard]] static std::vector<uint8_t> ConstructDetail();

    [[nodiscard]] bool                                   QueryResident() const noexcept { return Resident_; }
    [[nodiscard]] bool                                   QueryStepPending() const noexcept { return StepPending_; }
    [[nodiscard]] uint32_t                               QueryState() const noexcept { return State_; }
    [[nodiscard]] double                                 QueryAccumulator() const noexcept { return Accumulator_; }
    [[nodiscard]] double                                 QueryQuietSeconds() const noexcept { return QuietSeconds_; }
    [[nodiscard]] uint32_t                               QueryStepOrdinal() const noexcept { return StepOrdinal_; }
    [[nodiscard]] double                                 QueryElapsedSeconds() const noexcept { return ElapsedSeconds_; }
    [[nodiscard]] uint32_t                               QueryStepsTaken() const noexcept { return StepsTaken_; }
    [[nodiscard]] const PlumeGrade&                      QueryConfiguration() const noexcept { return Configuration_; }
    [[nodiscard]] const std::array<uint32_t, 3>&         QueryThermoExtent() const noexcept { return ThermoExtent_; }
    [[nodiscard]] const std::array<uint32_t, 3>&         QueryVelocityExtent() const noexcept { return VelocityExtent_; }
    [[nodiscard]] double                                 QueryCoarseEdge() const noexcept { return CoarseEdge_; }
    [[nodiscard]] const std::array<int32_t, 2>&          QueryOrigin() const noexcept { return Origin_; }
    [[nodiscard]] const std::array<int32_t, 2>&          QueryPriorOrigin() const noexcept { return PriorOrigin_; }
    [[nodiscard]] const std::array<uint8_t, PlumeRecordBytes>& QueryRecordBytes() const noexcept { return RecordBytes_; }
    [[nodiscard]] const std::array<uint8_t, FireLightBytes>&   QueryFireLightBytes() const noexcept { return FireLightBytes_; }
    [[nodiscard]] uint32_t                               QueryFireLightCount() const noexcept { return FireLightCount_; }
    [[nodiscard]] uint32_t                               QuerySourceCount() const noexcept { return SourceCount_; }
    [[nodiscard]] const std::array<int32_t, 8>&          QueryCoverageReset() const noexcept { return CoverageReset_; }

private:
    void RecentreWindow(const Vector3& Focus, bool Force);
    void EncodeRecord(double StepDuration, double RefreshSeconds, const PlumeSourceProjection& Sources, const PlumeSettings& Settings);

    PlumeGrade                                Configuration_;
    std::array<uint32_t, 3>                   ThermoExtent_{};
    std::array<uint32_t, 3>                   VelocityExtent_{};
    double                                    CoarseEdge_ = 0.0;
    std::array<int32_t, 2>                    Origin_{};
    std::array<int32_t, 2>                    PriorOrigin_{};

    bool                                      Resident_       = false;
    bool                                      StepPending_    = false;
    uint32_t                                  State_          = 0;
    double                                    Accumulator_    = 0.0;
    double                                    QuietSeconds_   = 0.0;
    double                                    QuietPrior_     = 0.0;
    uint32_t                                  StepOrdinal_    = 0;
    double                                    ElapsedSeconds_ = 0.0;
    uint32_t                                  StepsTaken_     = 0;
    Vector3                                   SunDirection_{0.0, 0.0, 1.0};
    std::array<uint8_t, PlumeRecordBytes>     RecordBytes_{};
    std::array<uint8_t, FireLightBytes>       FireLightBytes_{};
    uint32_t                                  FireLightCount_ = 0;
    uint32_t                                  SourceCount_    = 0;
    std::array<int32_t, 8>                    CoverageReset_  = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0, -0x7FFFFFFF, -0x7FFFFFFF, -0x7FFFFFFF, 0};
};

} // namespace Frontier::PlumeSimulation
