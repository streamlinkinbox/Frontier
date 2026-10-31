/*==========================================================================================================================================
                                                 CONTROLVEHICLEPHYSICS.H
==========================================================================================================================================*/
// 🧩 1:1 ControlVehicle specification, Pacejka MF6.1 tyre slip solver, 3D Neo-Hookean hexahedral continuum FEM wheel,
//    digressive four-corner suspension, 18-point turbocharged powertrain, and 10,448-byte GPU visual state record.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                        AUTHORED SOCKETS & GEOMETRY SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct ControlVehicleSpecification
{
    static constexpr double MassKilograms           = 1300.0;     // [kg] - Total vehicle mass incl. driver & fuel
    static constexpr double CentreOfMassHeightM     = 0.53;       // [m]  - Rest CoM height above flat ground
    static constexpr double TyreOuterRadiusM        = 0.50855;    // [m]  - RubberFL Z/X span / 2
    static constexpr double TyreRimRadiusM          = 0.34460;    // [m]  - RimFL span / 2
    static constexpr double TyreWidthM              = 0.39580;    // [m]  - RubberFL Y span
    static constexpr double AuthoredRimRadiusM      = 0.344818;   // [m]  - ControlWheelStructure rim radius
    static constexpr double UnsprungMassPerWheelKg  = 20.0;       // [kg] - Carcass, hub, upright, brake, half strut
    static constexpr double FrontWeightFraction     = 0.4914;     // [-]  - Static front mass distribution
    static constexpr double RollRadiusGyrationM     = 0.60;       // [m]
    static constexpr double PitchRadiusGyrationM    = 1.55;       // [m]
    static constexpr double YawRadiusGyrationM      = 1.60;       // [m]

    static constexpr std::array<std::array<double, 3>, 4> AxleMountModel = {{
        { +1.7274, +1.0475, +0.0914 },
        { +1.7274, -1.0475, +0.0914 },
        { -1.6686, +1.0475, +0.0914 },
        { -1.6686, -1.0475, +0.0914 }
    }};

    static constexpr std::array<std::array<double, 3>, 4> SuspensionMountModel = {{
        { +1.7274, +1.0475, +0.6436 },
        { +1.7274, -1.0475, +0.6436 },
        { -1.6686, +1.0475, +0.6436 },
        { -1.6686, -1.0475, +0.6436 }
    }};

    static constexpr double AuthoredGroundOffsetM() noexcept
    {
        return TyreOuterRadiusM - AxleMountModel[0][2];
    }

    static constexpr double CentreOfMassAuthoredZM() noexcept
    {
        return CentreOfMassHeightM - AuthoredGroundOffsetM();
    }

    static constexpr double SprungMassKilograms() noexcept
    {
        return MassKilograms - 4.0 * UnsprungMassPerWheelKg;
    }

    // 18-point GT-R wide-open-throttle curve [RPM, Torque N·m] (VehicleSpecification.js EngineSheet)
    static constexpr std::array<std::array<double, 2>, 18> EngineTorqueCurve = {{
        {  800.0, 234.0 }, { 1200.0, 300.0 }, { 1600.0, 352.0 }, { 2000.0, 392.0 },
        { 2400.0, 428.0 }, { 2800.0, 460.0 }, { 3200.0, 486.0 }, { 3600.0, 504.0 },
        { 4000.0, 514.0 }, { 4400.0, 518.0 }, { 4800.0, 520.0 }, { 5200.0, 520.0 },
        { 5800.0, 520.0 }, { 6200.0, 506.0 }, { 6600.0, 486.0 }, { 7000.0, 462.0 },
        { 7600.0, 448.0 }, { 8500.0, 442.0 }
    }};

    static constexpr double IdleRpm               = 700.0;
    static constexpr double RedlineRpm            = 7200.0;
    static constexpr double WastegateMaxBoostBar  = 1.20;
    static constexpr double FinalDriveRatio       = 3.55;
    static constexpr double DriveEfficiency       = 0.92;
    static constexpr std::array<double, 9> GearRatios = {{
        -3.8, -2.0, 0.0, 4.2, 3.1, 2.5, 1.9, 1.5, 1.2
    }};

    [[nodiscard]] static double SampleEngineTorqueNm(double Rpm) noexcept;
    [[nodiscard]] static double SampleTurboMultiplier(double BoostBar) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           DIGRESSIVE SUSPENSION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct SuspensionStrutSpecification
{
    double                NaturalFrequencyHz = 2.0;    // [Hz]
    double                DampingRatio       = 0.60;   // [-]
    double                SpringRateNm       = 0.0;    // [N/m]
    double                DampingRateNsm     = 0.0;    // [N·s/m]
    double                MaxTravelM         = 0.35;   // [m]
    double                BumpTravelM        = 0.02;   // [m]
    double                DroopTravelM       = 0.08;   // [m]
    double                PreloadNewtons     = 0.0;    // [N]
    double                StaticCompressionM = 0.0;    // [m]
    double                FreeLengthM        = 0.0;    // [m]
    double                HardeningFactor    = 1.5;    // [-]
    std::array<double, 3> Coefficients       = { 0.0, 0.0, 0.0 };

    void                  Calibrate(double CornerSprungMassKg) noexcept;
    [[nodiscard]] double  RateFloorCompression() const noexcept;
    [[nodiscard]] double  SpringForce(double CompressionM) const noexcept;
    [[nodiscard]] double  StaticCompressionFor(double SupportedLoadN) const noexcept;
    void                  ResolveFromSockets(double MountHeightM, double AxleHeightM, double SupportedLoadN) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                          PACEJKA MAGIC FORMULA MF6.1 SOLVER
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaSlipParameters
{
    double Fz0                        = 5.0;
    double Lx                         = 1.2;
    double Ly                         = 1.3;
    double MuLoadSensitivityLong      = 0.08;
    double MuLoadSensitivityLat       = 0.12;
    double MuLoadMinFactor            = 0.60;
    double pBx1 = 18.0, pBx2 = 20.0, pBx3 = 0.50, pCx1 = 1.55, pDx1 = 1.60, pDx2 = -0.03, pDx3 = 0.015;
    double pEx1 = -0.10, pEx2 = -0.15, pEx3 = 0.005, pEx4 = 0.030, pHx1 = 0.0, pHx2 = 0.0, pVx1 = 0.0, pVx2 = 0.0;
    double pBy1 = 18.0, pBy2 = 10.0, pBy3 = 0.10, pCy1 = 1.45, pDy1 = 1.85, pDy2 = -0.06, pDy3 = 0.10;
    double pEy1 = -1.20, pEy2 = -0.50, pEy3 = 0.20, pHy1 = 0.0, pHy2 = 0.0, pVy1 = 0.0, pVy2 = 0.0, pVy3 = 0.0, pVy4 = 0.0;
    double pCamber1 = 0.12, pCamber2 = -0.05;
    double rBx1 = 12.0, rCx1 = 1.0, rBy1 = 5.0, rBy2 = 4.0, rBy4 = 0.0, rCy1 = 0.75;
    double rVy1 = 0.0, rVy2 = 0.0, rVy3 = 0.0, rVy4 = 0.0, rVy5 = 1.9;
    double RelaxationLengthLong       = 0.052;
    double RelaxationLengthLat        = 0.048;
    double RelaxationLoadExponentLong = 0.40;
    double RelaxationLoadExponentLat  = 0.45;
};

struct CombinedTyreForce
{
    double LongitudinalFx = 0.0;   // [N]
    double LateralFy      = 0.0;   // [N]
};

class TyreSlipSolver
{
  public:
    explicit TyreSlipSolver(const PacejkaSlipParameters& Parameters = {}) noexcept;

    [[nodiscard]] double                LongitudinalForce(double SlipRatio, double CamberRad, double LoadNewtons) const noexcept;
    [[nodiscard]] double                LateralForce(double SlipAngleRad, double CamberRad, double LoadNewtons) const noexcept;
    [[nodiscard]] CombinedTyreForce     Combine(double SlipRatio, double SlipAngleRad, double CamberRad, double LoadNewtons) const noexcept;
    [[nodiscard]] std::array<double, 2> ResolveRelaxationLengths(double LoadNewtons) const noexcept;

  private:
    PacejkaSlipParameters Params{};
    double Fz0 = 5.0, InverseFz0 = 0.2, Ly = 1.3;
    double LoadLong = 0.08, LoadLat = 0.12, LoadFloor = 0.60;
    double Cx = 0.0, Dx1 = 0.0, Dx2 = 0.0, Dx3 = 0.0, Bx1 = 0.0, Bx2 = 0.0, Bx3 = 0.0;
    double Ex1 = 0.0, Ex2 = 0.0, Ex3 = 0.0, Ex4 = 0.0, Hx1 = 0.0, Hx2 = 0.0, Vx1 = 0.0, Vx2 = 0.0;
    double Cy = 0.0, Dy1 = 0.0, Dy2 = 0.0, Dy3 = 0.0, By1 = 0.0, By2 = 0.0, By3 = 0.0;
    double Ey1 = 0.0, Ey2 = 0.0, Ey3 = 0.0, Hy1 = 0.0, Hy2 = 0.0, Vy1 = 0.0, Vy2 = 0.0, Vy3 = 0.0, Vy4 = 0.0;
    double Camber1 = 0.0, Camber2 = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                          3D HEXAHEDRAL NEO-HOOKEAN CONTINUUM FEM WHEEL ("solid" / 32x5 NODES)
//------------------------------------------------------------------------------------------------------------------------

enum class TyreContinuumModel : uint8_t
{
    Solid3DFem          = 0,   // [-] - 128 Hex8 compressible Neo-Hookean continuum elements ("solid")
    OrthotropicMembrane = 1,   // [-] - 2D orthotropic curved carcass membrane ("membrane")
    PositionBasedXpbd   = 2,   // [-] - Extended position-based carcass compliance ("xpbd")
    LinearSpring        = 3    // [-] - Analytic rigid carcass + radial spring ("spring")
};

struct WheelStepInputs
{
    std::array<float, 3> Center       = { 0.0f, 0.0f, 0.49f };
    std::array<float, 4> Orientation  = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> GroundNormal = { 0.0f, 0.0f, 1.0f };
    float                Deflection   = 0.0f;        // [m]
    float                Pressure     = 220000.0f;   // [Pa]
    float                LateralForce = 0.0f;        // [N]
};

class SolidContinuumWheel
{
  public:
    static constexpr uint32_t CircumferentialCount = 32u;
    static constexpr uint32_t MeridionalCount      = 5u;
    static constexpr uint32_t NodesPerWheel        = CircumferentialCount * MeridionalCount;          // 160
    static constexpr uint32_t ElementCount         = CircumferentialCount * (MeridionalCount - 1u);   // 128
    static constexpr float    ReferencePressurePa  = 220000.0f;

    SolidContinuumWheel() noexcept;

    void  Reset(std::array<float, 3> WheelCenter = { 0.0f, 0.0f, 0.0f },
                std::array<float, 4> WheelQuat   = { 0.0f, 0.0f, 0.0f, 1.0f }) noexcept;
    float Step(const WheelStepInputs& State, float DeltaSeconds = 1.0f / 120.0f, int Substeps = 2) noexcept;

    [[nodiscard]] std::array<float, 3> RestNodePosition(uint32_t NodeIndex) const noexcept
    {
        const uint32_t Base = (NodeIndex % NodesPerWheel) * 3u;
        return { RestLocal[Base], RestLocal[Base + 1u], RestLocal[Base + 2u] };
    }

    [[nodiscard]] std::array<float, 3> QueryNodeDisplacement(uint32_t NodeIndex) const noexcept
    {
        const uint32_t Base = (NodeIndex % NodesPerWheel) * 3u;
        return {
            LocalPositions[Base + 0u] - RestLocal[Base + 0u],
            LocalPositions[Base + 1u] - RestLocal[Base + 1u],
            LocalPositions[Base + 2u] - RestLocal[Base + 2u]
        };
    }

    [[nodiscard]] float QueryMaxDisplacementMetres() const noexcept;

    std::array<float, NodesPerWheel * 3u> RestLocal{};
    std::array<float, NodesPerWheel * 3u> InnerRestLocal{};
    std::array<float, NodesPerWheel * 3u> LocalPositions{};
    std::array<float, NodesPerWheel * 3u> LocalVelocities{};
    std::array<float, NodesPerWheel * 3u> Forces{};
    std::array<float, NodesPerWheel * 3u> WorldPositions{};
    std::array<float, NodesPerWheel>      InvMass{};

    std::array<uint16_t, ElementCount * 4u> ElemNodes{};
    std::array<float, ElementCount * 12u>   ElemGrad{};
    std::array<float, ElementCount>         ElemVol{};

    float    MeanJacobian  = 1.0f;
    float    MeanStressPa  = 0.0f;
    uint32_t ContactNodes  = 0u;
    float    VerticalForce = 0.0f;

  private:
    void BuildReferenceMesh() noexcept;
    void PrecomputeHexElements() noexcept;
    void SyncWorldPositions(std::array<float, 3> WheelCenter, std::array<float, 4> WheelQuat) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                           10,448-BYTE GPU VEHICLE VISUAL RECORD (VehicleVisualState.js)
//------------------------------------------------------------------------------------------------------------------------

struct VehicleVisualRecord
{
    static constexpr size_t HeaderFloats  = 52u;
    static constexpr size_t CarcassFloats = 4u * SolidContinuumWheel::NodesPerWheel * 4u; // 2560
    static constexpr size_t TotalFloats   = HeaderFloats + CarcassFloats;                 // 2612
    static constexpr size_t TotalBytes    = TotalFloats * sizeof(float);                  // 10448

    std::array<float, TotalFloats> Data{};
};

static_assert(VehicleVisualRecord::TotalBytes == 10448u,
              "VehicleVisualRecord must match VehicleHMI's exact 10,448-byte GPU storage layout.");

//------------------------------------------------------------------------------------------------------------------------
//                                        COMPLETE 6-DOF CONTROLVEHICLE SOLVER
//------------------------------------------------------------------------------------------------------------------------

struct VehicleDriverInputs
{
    double Throttle  = 0.0;   // [0..1]
    double Brake     = 0.0;   // [0..1]
    double Steer     = 0.0;   // [-1..1] (+1 left)
    bool   Handbrake = false;
};

struct WheelCornerTelemetry
{
    bool   Grounded          = true;
    bool   Shredded          = false;
    bool   Ignited           = false;
    double StrutLengthM      = 0.5522;     // [m]
    double CompressionM      = 0.0185;     // [m]
    double NormalLoadN       = 3150.0;     // [N]
    double TyreDeflectionM   = 0.0145;     // [m]
    double GaugePressurePa   = 220000.0;   // [Pa]
    double SurfaceTempKelvin = 338.15;     // [K]  (65 C)
    double TreadFraction     = 1.0;        // [0..1]
    double SlipRatio         = 0.0;        // [-]
    double SlipAngleRad      = 0.0;        // [rad]
    double LongitudinalFx    = 0.0;        // [N]
    double LateralFy         = 0.0;        // [N]
    double SlidingEnergyJ    = 0.0;        // [J]
    double SpinAngularVel    = 0.0;        // [rad/s]
    double SpinAngleRad      = 0.0;        // [rad]
    double SteerAngleRad     = 0.0;        // [rad]
};

class ControlVehicleSolver
{
  public:
    ControlVehicleSolver() noexcept;

    void Respawn(std::array<double, 3> WorldPosition = { -40.0, 0.0, 0.53 }, double YawRadians = 0.0) noexcept;
    void AssignTyreModel(TyreContinuumModel Model) noexcept;
    void ShredTyre(size_t WheelIndex) noexcept;
    void RestoreTyre(size_t WheelIndex) noexcept;
    void Advance(const VehicleDriverInputs& Inputs, double DeltaSeconds, double GroundElevationM = 0.0) noexcept;
    void EncodeVisualRecord(VehicleVisualRecord& OutRecord, bool SeeThroughGlass = true) const noexcept;

    [[nodiscard]] TyreContinuumModel                          QueryTyreModel() const noexcept { return ActiveTyreModel; }
    [[nodiscard]] const char*                                 QueryTyreModelLabel() const noexcept;
    [[nodiscard]] int32_t                                     QueryGearNumber() const noexcept { return GearSlotIndex - 2; }
    [[nodiscard]] double                                      QueryFlywheelTorqueNm() const noexcept;
    [[nodiscard]] const std::array<SuspensionStrutSpecification, 4>& QueryStruts() const noexcept { return Struts; }
    [[nodiscard]] const std::array<WheelCornerTelemetry, 4>&  QueryWheels() const noexcept { return Wheels; }
    [[nodiscard]] const std::array<SolidContinuumWheel, 4>&   QueryContinuumWheels() const noexcept { return ContinuumWheels; }

    std::array<double, 3> Position            = { -40.0, 0.0, 0.53 };
    std::array<double, 3> LinearVelocity      = { 0.0, 0.0, 0.0 };
    std::array<double, 4> Orientation         = { 0.0, 0.0, 0.0, 1.0 };
    double                YawRadians          = 0.0;
    double                PitchRadians        = 0.0;
    double                RollRadians         = 0.0;
    double                YawRateRadPerSec    = 0.0;
    double                PitchRateRadPerSec  = 0.0;
    double                RollRateRadPerSec   = 0.0;

    double                SteeringAngleRad    = 0.0;
    double                ThrottleInput       = 0.0;
    double                BrakeInput          = 0.0;
    double                EngineRpm           = 700.0;
    double                TurboBoostBar       = 0.0;
    int32_t               GearSlotIndex       = 3;       // 3 = 1st gear in TransmissionSheet
    double                ShiftCooldownSec    = 0.0;
    double                ForwardSpeedMps     = 0.0;
    double                SpeedKilometresHour = 0.0;
    uint32_t              HullSampleCount     = 96u;
    uint32_t              HullContactCount    = 0u;
    double                HullPointRadiusM    = 0.065;

    [[nodiscard]] const char* QueryGearLabel() const noexcept;

  private:
    TyreContinuumModel                          ActiveTyreModel = TyreContinuumModel::Solid3DFem;
    TyreSlipSolver                              SlipModel{};
    std::array<SuspensionStrutSpecification, 4> Struts{};
    std::array<WheelCornerTelemetry, 4>         Wheels{};
    std::array<SolidContinuumWheel, 4>          ContinuumWheels{};
};

} // namespace Frontier
