/*==========================================================================================================================================
                                                 CONTROLVEHICLEPHYSICS.CPP
==========================================================================================================================================*/
// 🧩 Implementation of ControlVehicleSpecification, SuspensionStrutSpecification, Pacejka MF6.1 TyreSlipSolver,
//    SolidContinuumWheel (3D Hexahedral Neo-Hookean FEM), and 6-DOF ControlVehicleSolver.

#include "Engine/Physics/ControlVehiclePhysics.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double PiScalar    = 3.14159265358979323846;
constexpr double SmallScalar = 1.0e-8;

struct MeridionalBand
{
    float    RadiusM;
    float    LateralYM;
    float    ThicknessM;
    uint32_t Mobile;
};

constexpr std::array<MeridionalBand, 5> MeridionalBands = {{
    { 0.34480f, -0.115f, 0.018f, 0u },   // 0: Inner rim bead (clamped)
    { 0.48650f, -0.118f, 0.016f, 1u },   // 1: Inner sidewall / shoulder
    { 0.50855f,  0.000f, 0.024f, 1u },   // 2: Tread crown center
    { 0.48650f,  0.118f, 0.016f, 1u },   // 3: Outer sidewall / shoulder
    { 0.34480f,  0.115f, 0.018f, 0u }    // 4: Outer rim bead (clamped)
}};

constexpr std::array<std::array<float, 3>, 8> HexParentSigns = {{
    { -1.0f, -1.0f, -1.0f }, {  1.0f, -1.0f, -1.0f }, {  1.0f,  1.0f, -1.0f }, { -1.0f,  1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f }, {  1.0f, -1.0f,  1.0f }, {  1.0f,  1.0f,  1.0f }, { -1.0f,  1.0f,  1.0f }
}};

double SignScalar(double Value) noexcept
{
    return Value > 0.0 ? 1.0 : (Value < 0.0 ? -1.0 : 0.0);
}

float Invert3x3Transpose(float J00, float J01, float J02,
                         float J10, float J11, float J12,
                         float J20, float J21, float J22,
                         std::array<float, 9>& Out) noexcept
{
    const float C00 = J11 * J22 - J12 * J21;
    const float C01 = J12 * J20 - J10 * J22;
    const float C02 = J10 * J21 - J11 * J20;
    const float C10 = J02 * J21 - J01 * J22;
    const float C11 = J00 * J22 - J02 * J20;
    const float C12 = J01 * J20 - J00 * J21;
    const float C20 = J01 * J12 - J02 * J11;
    const float C21 = J02 * J10 - J00 * J12;
    const float C22 = J00 * J11 - J01 * J10;
    const float Det = J00 * C00 + J01 * C01 + J02 * C02;
    const float Inv = std::fabs(Det) > 1.0e-12f ? (1.0f / Det) : 0.0f;
    Out = {
        C00 * Inv, C01 * Inv, C02 * Inv,
        C10 * Inv, C11 * Inv, C12 * Inv,
        C20 * Inv, C21 * Inv, C22 * Inv
    };
    return std::fabs(Det);
}

std::array<float, 3> RotateByQuaternion(std::array<float, 4> Q, std::array<float, 3> V) noexcept
{
    const float Tx = 2.0f * (Q[1] * V[2] - Q[2] * V[1]);
    const float Ty = 2.0f * (Q[2] * V[0] - Q[0] * V[2]);
    const float Tz = 2.0f * (Q[0] * V[1] - Q[1] * V[0]);
    return {
        V[0] + Q[3] * Tx + (Q[1] * Tz - Q[2] * Ty),
        V[1] + Q[3] * Ty + (Q[2] * Tx - Q[0] * Tz),
        V[2] + Q[3] * Tz + (Q[0] * Ty - Q[1] * Tx)
    };
}

std::array<float, 4> AxisAngleQuaternion(std::array<float, 3> Axis, float AngleRad) noexcept
{
    const float Half = 0.5f * AngleRad;
    const float S    = std::sin(Half);
    return { Axis[0] * S, Axis[1] * S, Axis[2] * S, std::cos(Half) };
}

std::array<float, 4> MultiplyQuaternion(std::array<float, 4> A, std::array<float, 4> B) noexcept
{
    return {
        A[3] * B[0] + A[0] * B[3] + A[1] * B[2] - A[2] * B[1],
        A[3] * B[1] - A[0] * B[2] + A[1] * B[3] + A[2] * B[0],
        A[3] * B[2] + A[0] * B[1] - A[1] * B[0] + A[2] * B[3],
        A[3] * B[3] - A[0] * B[0] - A[1] * B[1] - A[2] * B[2]
    };
}

} // namespace

//========================================================================================================================
//                                        POWERTRAIN INTERPOLATION TABLES
//========================================================================================================================

double ControlVehicleSpecification::SampleEngineTorqueNm(double Rpm) noexcept
{
    const auto& Curve = EngineTorqueCurve;
    if (Rpm <= Curve.front()[0])
    {
        return Curve.front()[1];
    }
    if (Rpm >= Curve.back()[0])
    {
        return Curve.back()[1];
    }
    for (size_t I = 0u; I + 1u < Curve.size(); ++I)
    {
        if (Rpm >= Curve[I][0] && Rpm <= Curve[I + 1u][0])
        {
            const double Alpha = (Rpm - Curve[I][0]) / std::max(1.0, Curve[I + 1u][0] - Curve[I][0]);
            return Curve[I][1] + Alpha * (Curve[I + 1u][1] - Curve[I][1]);
        }
    }
    return 520.0;
}

double ControlVehicleSpecification::SampleTurboMultiplier(double BoostBar) noexcept
{
    static constexpr std::array<std::array<double, 2>, 4> Curve = {{
        { 0.0, 1.00 }, { 0.5, 1.30 }, { 1.0, 1.70 }, { 1.2, 1.85 }
    }};
    if (BoostBar <= 0.0)
    {
        return 1.0;
    }
    if (BoostBar >= Curve.back()[0])
    {
        return Curve.back()[1];
    }
    for (size_t I = 0u; I + 1u < Curve.size(); ++I)
    {
        if (BoostBar >= Curve[I][0] && BoostBar <= Curve[I + 1u][0])
        {
            const double Alpha = (BoostBar - Curve[I][0]) / (Curve[I + 1u][0] - Curve[I][0]);
            return Curve[I][1] + Alpha * (Curve[I + 1u][1] - Curve[I][1]);
        }
    }
    return 1.0;
}

//========================================================================================================================
//                                           DIGRESSIVE SUSPENSION SPECIFICATION
//========================================================================================================================

void SuspensionStrutSpecification::Calibrate(double CornerSprungMassKg) noexcept
{
    const double Omega = 2.0 * PiScalar * NaturalFrequencyHz;
    SpringRateNm       = CornerSprungMassKg * Omega * Omega;
    DampingRateNsm     = 2.0 * DampingRatio * CornerSprungMassKg * Omega;
    PreloadNewtons     = SpringRateNm * 0.02;
    const double Range = BumpTravelM + DroopTravelM;
    Coefficients       = {
        SpringRateNm * HardeningFactor,
        -(SpringRateNm * (HardeningFactor - 1.0)) / std::max(1.0e-4, Range),
        0.0
    };
}

double SuspensionStrutSpecification::RateFloorCompression() const noexcept
{
    const double A = 3.0 * Coefficients[2];
    const double B = 2.0 * Coefficients[1];
    const double C = Coefficients[0] - SpringRateNm * 0.10;
    if (std::fabs(A) < 1.0e-6)
    {
        if (std::fabs(B) < 1.0e-6)
        {
            return 0.0;
        }
        const double Root = -C / B;
        return Root > 0.0 ? Root : 0.0;
    }
    const double Disc = B * B - 4.0 * A * C;
    if (Disc < 0.0)
    {
        return 0.0;
    }
    const double Sq   = std::sqrt(Disc);
    const double R1   = (-B - Sq) / (2.0 * A);
    const double R2   = (-B + Sq) / (2.0 * A);
    const double Low  = std::min(R1, R2);
    const double High = std::max(R1, R2);
    return Low > 0.0 ? Low : (High > 0.0 ? High : 0.0);
}

double SuspensionStrutSpecification::SpringForce(double CompressionM) const noexcept
{
    const auto Cubic = [&](double X) noexcept {
        return Coefficients[0] * X + Coefficients[1] * X * X + Coefficients[2] * X * X * X;
    };
    const double Floor = SpringRateNm * 0.10;
    const double Turn  = RateFloorCompression();
    if (Turn <= 0.0 || CompressionM <= Turn)
    {
        return Cubic(CompressionM);
    }
    return Cubic(Turn) + Floor * (CompressionM - Turn);
}

double SuspensionStrutSpecification::StaticCompressionFor(double SupportedLoadN) const noexcept
{
    if (SupportedLoadN <= PreloadNewtons)
    {
        return 0.0;
    }
    double Low  = 0.0;
    double High = MaxTravelM;
    for (int Iter = 0; Iter < 48; ++Iter)
    {
        const double Mid = 0.5 * (Low + High);
        if (PreloadNewtons + SpringForce(Mid) < SupportedLoadN)
        {
            Low = Mid;
        }
        else
        {
            High = Mid;
        }
    }
    return 0.5 * (Low + High);
}

void SuspensionStrutSpecification::ResolveFromSockets(double MountHeightM, double AxleHeightM, double SupportedLoadN) noexcept
{
    StaticCompressionM = StaticCompressionFor(SupportedLoadN);
    FreeLengthM        = (MountHeightM - AxleHeightM) + StaticCompressionM;
}

//========================================================================================================================
//                                          PACEJKA MAGIC FORMULA MF6.1 SOLVER
//========================================================================================================================

TyreSlipSolver::TyreSlipSolver(const PacejkaSlipParameters& Parameters) noexcept
    : Params(Parameters)
{
    Fz0        = std::max(Params.Fz0, 0.001);
    InverseFz0 = 1.0 / Fz0;
    Ly         = Params.Ly;
    LoadLong   = Params.MuLoadSensitivityLong;
    LoadLat    = Params.MuLoadSensitivityLat;
    LoadFloor  = Params.MuLoadMinFactor;

    Cx  = Params.pCx1 * Params.Lx;
    Dx1 = Params.pDx1 * Params.Lx;
    Dx2 = Params.pDx2 * Params.Lx;
    Dx3 = Params.pDx3;
    Bx1 = Params.pBx1 * Params.Lx;
    Bx2 = Params.pBx2 * Params.Lx;
    Bx3 = Params.pBx3;
    Ex1 = Params.pEx1 * Params.Lx;
    Ex2 = Params.pEx2 * Params.Lx;
    Ex3 = Params.pEx3 * Params.Lx;
    Ex4 = Params.pEx4;
    Hx1 = Params.pHx1 * Params.Lx;
    Hx2 = Params.pHx2 * Params.Lx;
    Vx1 = Params.pVx1 * Params.Lx;
    Vx2 = Params.pVx2 * Params.Lx;

    Cy      = Params.pCy1 * Params.Ly;
    Dy1     = Params.pDy1 * Params.Ly;
    Dy2     = Params.pDy2 * Params.Ly;
    Dy3     = Params.pDy3;
    By1     = Params.pBy1 * Params.Ly;
    By2     = Params.pBy2 * Params.Ly;
    By3     = Params.pBy3;
    Ey1     = Params.pEy1 * Params.Ly;
    Ey2     = Params.pEy2 * Params.Ly;
    Ey3     = Params.pEy3;
    Hy1     = Params.pHy1 * Params.Ly;
    Hy2     = Params.pHy2 * Params.Ly;
    Vy1     = Params.pVy1 * Params.Ly;
    Vy2     = Params.pVy2 * Params.Ly;
    Vy3     = Params.pVy3;
    Vy4     = Params.pVy4;
    Camber1 = Params.pCamber1 * Params.Ly;
    Camber2 = Params.pCamber2 * Params.Ly;
}

double TyreSlipSolver::LongitudinalForce(double SlipRatio, double CamberRad, double LoadNewtons) const noexcept
{
    if (LoadNewtons < SmallScalar)
    {
        return 0.0;
    }
    const double Kappa   = std::clamp(SlipRatio, -0.92, 1.50);
    const double Fz      = LoadNewtons * 0.001;
    const double Dfz     = (Fz - Fz0) * InverseFz0;
    const double Shifted = Kappa + Hx1 + Hx2 * Dfz;
    const double Nominal = (Dx1 + Dx2 * Dfz) * Fz * (1.0 - Dx3 * CamberRad * CamberRad);
    const double Dx      = Nominal * std::max(1.0 - LoadLong * std::fabs(Dfz), LoadFloor);
    const double Bcd     = (Bx1 + Bx2 * Dfz) * (1.0 - Bx3 * std::fabs(CamberRad)) * Fz;
    const double Bx      = Bcd / std::max(Cx * Dx, SmallScalar);
    const double Ex      = std::clamp((Ex1 + Ex2 * Dfz + Ex3 * Dfz * Dfz) * (1.0 - Ex4 * SignScalar(Shifted)), -1.0, 1.0);
    const double Svx     = (Vx1 + Vx2 * Dfz) * Fz;
    const double Prod    = Bx * Shifted;
    return (Dx * std::sin(Cx * std::atan(Prod - Ex * (Prod - std::atan(Prod)))) + Svx) * 1000.0;
}

double TyreSlipSolver::LateralForce(double SlipAngleRad, double CamberRad, double LoadNewtons) const noexcept
{
    if (LoadNewtons < SmallScalar)
    {
        return 0.0;
    }
    const double Alpha    = std::clamp(SlipAngleRad, -1.30, 1.30);
    const double Fz       = LoadNewtons * 0.001;
    const double Dfz      = (Fz - Fz0) * InverseFz0;
    const double Svy0     = (Vy1 + Vy2 * Dfz) * Fz;
    const double SvyGamma = Fz * (Vy3 + Vy4 * Dfz) * CamberRad * Ly;
    const double KyGamma  = (Camber1 + Camber2 * Dfz) * Fz;
    const double Nominal  = (Dy1 + Dy2 * Dfz) * Fz * (1.0 - Dy3 * CamberRad * CamberRad);
    const double Dy       = Nominal * std::max(1.0 - LoadLat * std::fabs(Dfz), LoadFloor);
    const double Bcd      = (By1 + By2 * Dfz) * (1.0 - By3 * std::fabs(CamberRad)) * Fz;
    const double By       = Bcd / std::max(Cy * Dy, SmallScalar);
    const double Shy      = Hy1 + Hy2 * Dfz + (KyGamma * CamberRad - SvyGamma) / std::max(Bcd, SmallScalar);
    const double Eff      = Alpha + Shy;
    const double Ey       = std::clamp((Ey1 + Ey2 * Dfz) * (1.0 - Ey3 * SignScalar(Eff)), -1.0, 1.0);
    const double Prod     = By * Eff;
    return (Dy * std::sin(Cy * std::atan(Prod - Ey * (Prod - std::atan(Prod)))) + Svy0 + SvyGamma) * 1000.0;
}

CombinedTyreForce TyreSlipSolver::Combine(double SlipRatio, double SlipAngleRad, double CamberRad, double LoadNewtons) const noexcept
{
    double Fx = LongitudinalForce(SlipRatio, CamberRad, LoadNewtons);
    double Fy = LateralForce(SlipAngleRad, CamberRad, LoadNewtons);
    if (LoadNewtons < SmallScalar)
    {
        return { 0.0, 0.0 };
    }
    const double Kappa    = std::clamp(SlipRatio, -0.92, 1.50);
    const double Alpha    = std::clamp(SlipAngleRad, -1.30, 1.30);
    const double Fz       = LoadNewtons * 0.001;
    const double Dfz      = (Fz - Fz0) * InverseFz0;
    const double StiffLon = (Bx1 + Bx2 * Dfz) * (1.0 - Bx3 * std::fabs(CamberRad)) * Fz;
    const double StiffLat = (By1 + By2 * Dfz) * (1.0 - By3 * std::fabs(CamberRad)) * Fz;
    const double KappaS   = Kappa / std::max(StiffLon, SmallScalar);
    const double AlphaS   = std::tan(Alpha) / std::max(StiffLat, SmallScalar);
    const double Comb     = std::hypot(KappaS, AlphaS);
    double       Gx       = 1.0;
    double       Gy       = 1.0;
    if (Comb > SmallScalar)
    {
        Gx = std::cos(Params.rCx1 * std::atan(Params.rBx1 * AlphaS));
        const double Rby = Params.rBy1 * std::cos(std::atan(Params.rBy2 * (Alpha - Params.rBy4)));
        Gy = std::cos(Params.rCy1 * std::atan(Rby * KappaS * Comb));
    }
    const double DyKappa  = (Params.rVy1 + Params.rVy2 * Dfz + Params.rVy3 * CamberRad) * std::cos(std::atan(Params.rVy4 * Alpha));
    const double SvyKappa = DyKappa * std::sin(std::atan(Params.rVy5 * Kappa)) * Fz;
    return { Fx * Gx, Fy * Gy + SvyKappa * 1000.0 };
}

std::array<double, 2> TyreSlipSolver::ResolveRelaxationLengths(double LoadNewtons) const noexcept
{
    const double Ratio = std::max(LoadNewtons * 0.001 / Fz0, 0.05);
    return {
        Params.RelaxationLengthLong * std::pow(Ratio, Params.RelaxationLoadExponentLong),
        Params.RelaxationLengthLat  * std::pow(Ratio, Params.RelaxationLoadExponentLat)
    };
}

//========================================================================================================================
//                                   3D HEXAHEDRAL NEO-HOOKEAN CONTINUUM FEM WHEEL
//========================================================================================================================

SolidContinuumWheel::SolidContinuumWheel() noexcept
{
    BuildReferenceMesh();
    PrecomputeHexElements();
}

void SolidContinuumWheel::BuildReferenceMesh() noexcept
{
    for (uint32_t I = 0u; I < CircumferentialCount; ++I)
    {
        const float Theta = static_cast<float>(2.0 * PiScalar * static_cast<double>(I) / static_cast<double>(CircumferentialCount));
        const float CosT  = std::cos(Theta);
        const float SinT  = std::sin(Theta);
        for (uint32_t M = 0u; M < MeridionalCount; ++M)
        {
            const MeridionalBand& Band = MeridionalBands[M];
            const float           ROut = Band.RadiusM;
            const float           RIn  = ROut - Band.ThicknessM;
            const uint32_t        Node = I * MeridionalCount + M;
            const uint32_t        Idx3 = Node * 3u;

            RestLocal[Idx3 + 0u]      = ROut * CosT;
            RestLocal[Idx3 + 1u]      = Band.LateralYM;
            RestLocal[Idx3 + 2u]      = ROut * SinT;

            InnerRestLocal[Idx3 + 0u] = RIn * CosT;
            InnerRestLocal[Idx3 + 1u] = Band.LateralYM * 0.94f;
            InnerRestLocal[Idx3 + 2u] = RIn * SinT;

            LocalPositions[Idx3 + 0u] = RestLocal[Idx3 + 0u];
            LocalPositions[Idx3 + 1u] = RestLocal[Idx3 + 1u];
            LocalPositions[Idx3 + 2u] = RestLocal[Idx3 + 2u];

            WorldPositions[Idx3 + 0u] = RestLocal[Idx3 + 0u];
            WorldPositions[Idx3 + 1u] = RestLocal[Idx3 + 1u];
            WorldPositions[Idx3 + 2u] = RestLocal[Idx3 + 2u];

            InvMass[Node] = Band.Mobile ? (M == 2u ? 0.85f : 1.10f) : 0.0f;
        }
    }
}

void SolidContinuumWheel::PrecomputeHexElements() noexcept
{
    std::array<float, 9>  InvJt{};
    std::array<float, 24> CornerX{};
    uint32_t              Elem = 0u;

    for (uint32_t I = 0u; I < CircumferentialCount; ++I)
    {
        const uint32_t INext = (I + 1u) % CircumferentialCount;
        for (uint32_t M = 0u; M < MeridionalCount - 1u; ++M)
        {
            const uint16_t N0 = static_cast<uint16_t>(I * MeridionalCount + M);
            const uint16_t N1 = static_cast<uint16_t>(INext * MeridionalCount + M);
            const uint16_t N2 = static_cast<uint16_t>(INext * MeridionalCount + (M + 1u));
            const uint16_t N3 = static_cast<uint16_t>(I * MeridionalCount + (M + 1u));

            ElemNodes[Elem * 4u + 0u] = N0;
            ElemNodes[Elem * 4u + 1u] = N1;
            ElemNodes[Elem * 4u + 2u] = N2;
            ElemNodes[Elem * 4u + 3u] = N3;

            const std::array<uint16_t, 4> Nodes4 = { N0, N1, N2, N3 };
            for (uint32_t C = 0u; C < 4u; ++C)
            {
                const uint32_t Base = static_cast<uint32_t>(Nodes4[C]) * 3u;
                CornerX[C * 3u + 0u]        = InnerRestLocal[Base + 0u];
                CornerX[C * 3u + 1u]        = InnerRestLocal[Base + 1u];
                CornerX[C * 3u + 2u]        = InnerRestLocal[Base + 2u];
                CornerX[(C + 4u) * 3u + 0u] = RestLocal[Base + 0u];
                CornerX[(C + 4u) * 3u + 1u] = RestLocal[Base + 1u];
                CornerX[(C + 4u) * 3u + 2u] = RestLocal[Base + 2u];
            }

            float J00 = 0.0f, J01 = 0.0f, J02 = 0.0f;
            float J10 = 0.0f, J11 = 0.0f, J12 = 0.0f;
            float J20 = 0.0f, J21 = 0.0f, J22 = 0.0f;
            for (uint32_t A = 0u; A < 8u; ++A)
            {
                const auto& S  = HexParentSigns[A];
                const float D0 = 0.125f * S[0], D1 = 0.125f * S[1], D2 = 0.125f * S[2];
                const float Xx = CornerX[A * 3u + 0u], Xy = CornerX[A * 3u + 1u], Xz = CornerX[A * 3u + 2u];
                J00 += Xx * D0; J01 += Xx * D1; J02 += Xx * D2;
                J10 += Xy * D0; J11 += Xy * D1; J12 += Xy * D2;
                J20 += Xz * D0; J21 += Xz * D1; J22 += Xz * D2;
            }

            const float DetJ = Invert3x3Transpose(J00, J01, J02, J10, J11, J12, J20, J21, J22, InvJt);
            ElemVol[Elem]    = std::max(1.0e-6f, 8.0f * DetJ);

            const uint32_t GradBase = Elem * 12u;
            for (uint32_t C = 0u; C < 4u; ++C)
            {
                const auto& S  = HexParentSigns[C + 4u];
                const float D0 = 0.125f * S[0], D1 = 0.125f * S[1], D2 = 0.125f * S[2];
                ElemGrad[GradBase + C * 3u + 0u] = InvJt[0] * D0 + InvJt[1] * D1 + InvJt[2] * D2;
                ElemGrad[GradBase + C * 3u + 1u] = InvJt[3] * D0 + InvJt[4] * D1 + InvJt[5] * D2;
                ElemGrad[GradBase + C * 3u + 2u] = InvJt[6] * D0 + InvJt[7] * D1 + InvJt[8] * D2;
            }
            ++Elem;
        }
    }
}

void SolidContinuumWheel::Reset(std::array<float, 3> WheelCenter, std::array<float, 4> WheelQuat) noexcept
{
    LocalPositions = RestLocal;
    LocalVelocities.fill(0.0f);
    MeanJacobian  = 1.0f;
    MeanStressPa  = 0.0f;
    ContactNodes  = 0u;
    VerticalForce = 0.0f;
    SyncWorldPositions(WheelCenter, WheelQuat);
}

float SolidContinuumWheel::QueryMaxDisplacementMetres() const noexcept
{
    float MaxDispSq = 0.0f;
    for (uint32_t N = 0u; N < NodesPerWheel; ++N)
    {
        const uint32_t B  = N * 3u;
        const float    Dx = LocalPositions[B + 0u] - RestLocal[B + 0u];
        const float    Dy = LocalPositions[B + 1u] - RestLocal[B + 1u];
        const float    Dz = LocalPositions[B + 2u] - RestLocal[B + 2u];
        MaxDispSq         = std::max(MaxDispSq, Dx * Dx + Dy * Dy + Dz * Dz);
    }
    return std::sqrt(MaxDispSq);
}

void SolidContinuumWheel::SyncWorldPositions(std::array<float, 3> WheelCenter, std::array<float, 4> WheelQuat) noexcept
{
    for (uint32_t N = 0u; N < NodesPerWheel; ++N)
    {
        const uint32_t Base = N * 3u;
        const auto     Rot  = RotateByQuaternion(WheelQuat, { LocalPositions[Base], LocalPositions[Base + 1u], LocalPositions[Base + 2u] });
        WorldPositions[Base + 0u] = Rot[0] + WheelCenter[0];
        WorldPositions[Base + 1u] = Rot[1] + WheelCenter[1];
        WorldPositions[Base + 2u] = Rot[2] + WheelCenter[2];
    }
}

float SolidContinuumWheel::Step(const WheelStepInputs& State, float DeltaSeconds, int Substeps) noexcept
{
    const float Deflection = std::max(0.0f, State.Deflection);
    const float Pressure   = std::max(0.0f, State.Pressure);
    const auto  DownLocal  = RotateByQuaternion(
        { -State.Orientation[0], -State.Orientation[1], -State.Orientation[2], State.Orientation[3] },
        { -State.GroundNormal[0], -State.GroundNormal[1], -State.GroundNormal[2] });
    const float ContactPhi = std::atan2(DownLocal[2], DownLocal[0]);

    const float PNorm        = std::max(0.08f, Pressure / ReferencePressurePa);
    const float YoungE       = 1.2e4f * (0.32f + 0.68f * PNorm);
    const float Nu           = 0.44f;
    const float Mu           = YoungE / (2.0f * (1.0f + Nu));
    const float Lambda       = (YoungE * Nu) / ((1.0f + Nu) * (1.0f - 2.0f * Nu));
    const int   Steps        = std::max(1, Substeps);
    const float SubDt        = std::min(DeltaSeconds / static_cast<float>(Steps), 1.0f / 240.0f);
    const float Damping      = std::exp(-38.0f * SubDt);
    const float LateralShift = std::clamp(State.LateralForce / 145000.0f, -0.042f, 0.042f);
    const float PatchSpan    = std::clamp(0.44f / std::sqrt(std::max(0.15f, PNorm)), 0.22f, 1.10f);

    float JSum      = 0.0f;
    float StressSum = 0.0f;

    for (int StepIdx = 0; StepIdx < Steps; ++StepIdx)
    {
        Forces.fill(0.0f);
        JSum      = 0.0f;
        StressSum = 0.0f;

        for (uint32_t E = 0u; E < ElementCount; ++E)
        {
            const uint32_t NBase = E * 4u;
            const uint32_t GBase = E * 12u;
            const float    V0    = ElemVol[E];

            float F00 = 1.0f, F01 = 0.0f, F02 = 0.0f;
            float F10 = 0.0f, F11 = 1.0f, F12 = 0.0f;
            float F20 = 0.0f, F21 = 0.0f, F22 = 1.0f;

            for (uint32_t C = 0u; C < 4u; ++C)
            {
                const uint32_t Node3 = static_cast<uint32_t>(ElemNodes[NBase + C]) * 3u;
                const float    Ux    = LocalPositions[Node3 + 0u] - RestLocal[Node3 + 0u];
                const float    Uy    = LocalPositions[Node3 + 1u] - RestLocal[Node3 + 1u];
                const float    Uz    = LocalPositions[Node3 + 2u] - RestLocal[Node3 + 2u];
                const float    Gx    = ElemGrad[GBase + C * 3u + 0u];
                const float    Gy    = ElemGrad[GBase + C * 3u + 1u];
                const float    Gz    = ElemGrad[GBase + C * 3u + 2u];

                F00 += Ux * Gx; F01 += Ux * Gy; F02 += Ux * Gz;
                F10 += Uy * Gx; F11 += Uy * Gy; F12 += Uy * Gz;
                F20 += Uz * Gx; F21 += Uz * Gy; F22 += Uz * Gz;
            }

            const float C00  = F11 * F22 - F12 * F21;
            const float C01  = F12 * F20 - F10 * F22;
            const float C02  = F10 * F21 - F11 * F20;
            const float C10  = F02 * F21 - F01 * F22;
            const float C11  = F00 * F22 - F02 * F20;
            const float C12  = F01 * F20 - F00 * F21;
            const float C20  = F01 * F12 - F02 * F11;
            const float C21  = F02 * F10 - F00 * F12;
            const float C22  = F00 * F11 - F01 * F10;
            const float RawJ = F00 * C00 + F01 * C01 + F02 * C02;
            const float J    = std::clamp(RawJ, 0.30f, 2.20f);
            const float InvJ = 1.0f / J;
            JSum += J;

            const float Coeff = (Lambda * J * (J - 1.0f) - Mu) * InvJ;
            const float P00 = Mu * F00 + Coeff * C00, P01 = Mu * F01 + Coeff * C01, P02 = Mu * F02 + Coeff * C02;
            const float P10 = Mu * F10 + Coeff * C10, P11 = Mu * F11 + Coeff * C11, P12 = Mu * F12 + Coeff * C12;
            const float P20 = Mu * F20 + Coeff * C20, P21 = Mu * F21 + Coeff * C21, P22 = Mu * F22 + Coeff * C22;

            StressSum += std::sqrt(P00 * P00 + P11 * P11 + P22 * P22);

            for (uint32_t C = 0u; C < 4u; ++C)
            {
                const uint32_t Node = ElemNodes[NBase + C];
                if (InvMass[Node] <= 0.0f)
                {
                    continue;
                }
                const uint32_t Node3 = Node * 3u;
                const float    Gx    = ElemGrad[GBase + C * 3u + 0u];
                const float    Gy    = ElemGrad[GBase + C * 3u + 1u];
                const float    Gz    = ElemGrad[GBase + C * 3u + 2u];
                Forces[Node3 + 0u] -= V0 * (P00 * Gx + P01 * Gy + P02 * Gz);
                Forces[Node3 + 1u] -= V0 * (P10 * Gx + P11 * Gy + P12 * Gz);
                Forces[Node3 + 2u] -= V0 * (P20 * Gx + P21 * Gy + P22 * Gz);
            }
        }

        uint32_t ContactCount = 0u;
        for (uint32_t I = 0u; I < CircumferentialCount; ++I)
        {
            const float Theta   = static_cast<float>(2.0 * PiScalar * static_cast<double>(I) / static_cast<double>(CircumferentialCount));
            float       DPhi    = Theta - ContactPhi;
            DPhi               -= static_cast<float>(2.0 * PiScalar) * std::round(DPhi / static_cast<float>(2.0 * PiScalar));
            const float AbsDPhi = std::fabs(DPhi);

            for (uint32_t M = 1u; M <= 3u; ++M)
            {
                const uint32_t Node    = I * MeridionalCount + M;
                const uint32_t Node3   = Node * 3u;
                const float    InvM    = InvMass[Node];
                const float    Rx      = RestLocal[Node3 + 0u];
                const float    Ry      = RestLocal[Node3 + 1u];
                const float    Rz      = RestLocal[Node3 + 2u];
                const float    Ux      = LocalPositions[Node3 + 0u] - Rx;
                const float    Uy      = LocalPositions[Node3 + 1u] - Ry;
                const float    Uz      = LocalPositions[Node3 + 2u] - Rz;
                const float    TetherK = 450.0f * (0.4f + 0.6f * PNorm);

                const float Vx = (LocalVelocities[Node3 + 0u] + SubDt * InvM * (Forces[Node3 + 0u] - TetherK * Ux)) * Damping;
                const float Vy = (LocalVelocities[Node3 + 1u] + SubDt * InvM * (Forces[Node3 + 1u] - TetherK * Uy)) * Damping;
                const float Vz = (LocalVelocities[Node3 + 2u] + SubDt * InvM * (Forces[Node3 + 2u] - TetherK * Uz)) * Damping;

                float Nx = LocalPositions[Node3 + 0u] + SubDt * Vx;
                float Ny = LocalPositions[Node3 + 1u] + SubDt * Vy;
                float Nz = LocalPositions[Node3 + 2u] + SubDt * Vz;

                if (Deflection > 1.0e-5f)
                {
                    const float RadialRest = MeridionalBands[M].RadiusM;
                    const float RadialFrac = (RadialRest - 0.34480f) / (0.50855f - 0.34480f);
                    float       TargetDr   = 0.0f;
                    float       TargetDy   = 0.0f;

                    if (AbsDPhi < PatchSpan)
                    {
                        ++ContactCount;
                        const float U   = AbsDPhi / PatchSpan;
                        const float Env = 0.5f * (1.0f + std::cos(U * static_cast<float>(PiScalar)));
                        TargetDr        = -Deflection * Env * RadialFrac;
                        if (M != 2u)
                        {
                            const float BulgeSign = (M == 1u) ? -1.0f : 1.0f;
                            const float BulgeAmp  = 0.52f / (0.4f + 0.6f * std::sqrt(PNorm));
                            TargetDy             += BulgeSign * Deflection * BulgeAmp * Env;
                        }
                    }
                    else if (AbsDPhi < PatchSpan * 1.5f)
                    {
                        const float V = (AbsDPhi - PatchSpan) / (0.5f * PatchSpan);
                        TargetDr      = std::sin(V * static_cast<float>(PiScalar)) * (0.12f * Deflection * RadialFrac);
                    }

                    if (AbsDPhi < PatchSpan * 1.2f)
                    {
                        const float UShear   = AbsDPhi / (PatchSpan * 1.2f);
                        const float ShearEnv = 0.5f * (1.0f + std::cos(UShear * static_cast<float>(PiScalar)));
                        TargetDy            += LateralShift * ShearEnv * RadialFrac;
                    }

                    const float CosT  = std::cos(Theta);
                    const float SinT  = std::sin(Theta);
                    const float GoalR = std::max(0.34480f + 0.008f, RadialRest + TargetDr);
                    const float GoalX = GoalR * CosT;
                    const float GoalY = Ry + TargetDy;
                    const float GoalZ = GoalR * SinT;
                    const float Blend = (AbsDPhi < PatchSpan * 1.5f) ? 0.78f : 0.45f;

                    Nx += (GoalX - Nx) * Blend;
                    Ny += (GoalY - Ny) * Blend;
                    Nz += (GoalZ - Nz) * Blend;
                }

                LocalVelocities[Node3 + 0u] = (Nx - LocalPositions[Node3 + 0u]) / SubDt;
                LocalVelocities[Node3 + 1u] = (Ny - LocalPositions[Node3 + 1u]) / SubDt;
                LocalVelocities[Node3 + 2u] = (Nz - LocalPositions[Node3 + 2u]) / SubDt;
                LocalPositions[Node3 + 0u]  = Nx;
                LocalPositions[Node3 + 1u]  = Ny;
                LocalPositions[Node3 + 2u]  = Nz;
            }
        }
        ContactNodes = ContactCount;
    }

    MeanJacobian  = JSum / static_cast<float>(ElementCount);
    MeanStressPa  = StressSum / static_cast<float>(ElementCount);
    VerticalForce = Deflection * (25000.0f + 0.90f * Pressure);
    SyncWorldPositions(State.Center, State.Orientation);
    return VerticalForce;
}

//========================================================================================================================
//                                        COMPLETE 6-DOF CONTROLVEHICLE SOLVER
//========================================================================================================================

ControlVehicleSolver::ControlVehicleSolver() noexcept
{
    const double Sprung      = ControlVehicleSpecification::SprungMassKilograms();
    const double FrontCorner = 0.5 * Sprung * ControlVehicleSpecification::FrontWeightFraction;
    const double RearCorner  = 0.5 * Sprung * (1.0 - ControlVehicleSpecification::FrontWeightFraction);
    const double MountZ      = ControlVehicleSpecification::SuspensionMountModel[0][2];
    const double AxleZ       = ControlVehicleSpecification::AxleMountModel[0][2];

    for (size_t I = 0u; I < 4u; ++I)
    {
        const double CornerMass = (I < 2u) ? FrontCorner : RearCorner;
        Struts[I].Calibrate(CornerMass);
        Struts[I].ResolveFromSockets(MountZ, AxleZ, CornerMass * 9.81);
    }

    Respawn({ -40.0, 0.0, ControlVehicleSpecification::CentreOfMassHeightM }, 0.0);
}

void ControlVehicleSolver::Respawn(std::array<double, 3> WorldPosition, double YawRad) noexcept
{
    Position            = WorldPosition;
    LinearVelocity      = { 0.0, 0.0, 0.0 };
    YawRadians          = YawRad;
    PitchRadians        = 0.0;
    RollRadians         = 0.0;
    YawRateRadPerSec    = 0.0;
    PitchRateRadPerSec  = 0.0;
    RollRateRadPerSec   = 0.0;
    SteeringAngleRad    = 0.0;
    ThrottleInput       = 0.0;
    BrakeInput          = 0.0;
    EngineRpm           = ControlVehicleSpecification::IdleRpm;
    TurboBoostBar       = 0.0;
    GearSlotIndex       = 3;
    ShiftCooldownSec    = 0.0;
    ForwardSpeedMps     = 0.0;
    SpeedKilometresHour = 0.0;

    const auto YawQuat = AxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, static_cast<float>(YawRadians));
    Orientation        = { YawQuat[0], YawQuat[1], YawQuat[2], YawQuat[3] };

    const double Sprung      = ControlVehicleSpecification::SprungMassKilograms();
    const double FrontLoadN  = 0.5 * Sprung * ControlVehicleSpecification::FrontWeightFraction * 9.81;
    const double RearLoadN   = 0.5 * Sprung * (1.0 - ControlVehicleSpecification::FrontWeightFraction) * 9.81;
    const double EffStiff    = 25000.0 + 0.90 * 220000.0;

    for (size_t I = 0u; I < 4u; ++I)
    {
        auto&        W     = Wheels[I];
        const double LoadN = (I < 2u) ? FrontLoadN : RearLoadN;
        W                  = WheelCornerTelemetry{};
        W.Grounded         = true;
        W.NormalLoadN      = LoadN;
        W.CompressionM     = Struts[I].StaticCompressionM;
        W.StrutLengthM     = Struts[I].FreeLengthM - W.CompressionM;
        W.TyreDeflectionM  = LoadN / EffStiff;
        W.GaugePressurePa  = 220000.0;
        W.SurfaceTempKelvin = 341.15; // 68 C warm proving-grounds baseline
        ContinuumWheels[I].Reset();
        WheelStepInputs StepIn{};
        StepIn.Deflection  = static_cast<float>(W.TyreDeflectionM);
        StepIn.Pressure    = static_cast<float>(W.GaugePressurePa);
        StepIn.Orientation = YawQuat;
        for (int Warm = 0; Warm < 36; ++Warm)
        {
            (void)ContinuumWheels[I].Step(StepIn, 1.0f / 120.0f, 2);
        }
    }
}

void ControlVehicleSolver::AssignTyreModel(TyreContinuumModel Model) noexcept
{
    ActiveTyreModel = Model;
}

void ControlVehicleSolver::ShredTyre(size_t WheelIndex) noexcept
{
    if (WheelIndex < Wheels.size())
    {
        Wheels[WheelIndex].Shredded        = true;
        Wheels[WheelIndex].GaugePressurePa = 0.0;
        Wheels[WheelIndex].TreadFraction   = 0.0;
    }
}

void ControlVehicleSolver::RestoreTyre(size_t WheelIndex) noexcept
{
    if (WheelIndex < Wheels.size())
    {
        Wheels[WheelIndex].Shredded        = false;
        Wheels[WheelIndex].Ignited         = false;
        Wheels[WheelIndex].GaugePressurePa = 220000.0;
        Wheels[WheelIndex].TreadFraction   = 1.0;
    }
}

double ControlVehicleSolver::QueryFlywheelTorqueNm() const noexcept
{
    const double BaseNm = ControlVehicleSpecification::SampleEngineTorqueNm(EngineRpm);
    const double BoostM = ControlVehicleSpecification::SampleTurboMultiplier(TurboBoostBar);
    return BaseNm * (1.0 + (BoostM - 1.0) * std::clamp(ThrottleInput, 0.25, 1.0));
}

const char* ControlVehicleSolver::QueryTyreModelLabel() const noexcept
{
    switch (ActiveTyreModel)
    {
        case TyreContinuumModel::Solid3DFem:          return "3D Solid FEM";
        case TyreContinuumModel::OrthotropicMembrane: return "2D Membrane FEM";
        case TyreContinuumModel::PositionBasedXpbd:   return "XPBD Carcass";
        case TyreContinuumModel::LinearSpring:        return "Linear Spring";
    }
    return "3D Solid FEM";
}

const char* ControlVehicleSolver::QueryGearLabel() const noexcept
{
    switch (GearSlotIndex)
    {
        case 0: return "R2";
        case 1: return "R";
        case 2: return "N";
        case 3: return "1";
        case 4: return "2";
        case 5: return "3";
        case 6: return "4";
        case 7: return "5";
        case 8: return "6";
        default: return "1";
    }
}

void ControlVehicleSolver::Advance(const VehicleDriverInputs& Inputs, double DeltaSeconds, double GroundElevationM) noexcept
{
    const double Dt = std::clamp(DeltaSeconds, 1.0e-4, 0.05);
    ThrottleInput   = std::clamp(Inputs.Throttle, 0.0, 1.0);
    BrakeInput      = std::clamp(Inputs.Brake,    0.0, 1.0);

    const double TargetSteer = std::clamp(Inputs.Steer, -1.0, 1.0) * 0.52;
    const double MaxDelta    = 6.0 * Dt;
    SteeringAngleRad        += std::clamp(TargetSteer - SteeringAngleRad, -MaxDelta, MaxDelta);

    const double CosYaw = std::cos(YawRadians);
    const double SinYaw = std::sin(YawRadians);
    ForwardSpeedMps     = LinearVelocity[0] * CosYaw + LinearVelocity[1] * SinYaw;
    const double LatVel = -LinearVelocity[0] * SinYaw + LinearVelocity[1] * CosYaw;

    // Powertrain: 18-point GT-R torque + first-order wastegate turbo + automatic AMT shift schedule
    ShiftCooldownSec = std::max(0.0, ShiftCooldownSec - Dt);
    const double GearRatio  = ControlVehicleSpecification::GearRatios[static_cast<size_t>(std::clamp(GearSlotIndex, 0, 8))];
    const double TotalRatio = GearRatio * ControlVehicleSpecification::FinalDriveRatio;
    const double WheelOmega = ForwardSpeedMps / ControlVehicleSpecification::TyreOuterRadiusM;
    const double CoupledRpm = std::fabs(WheelOmega * TotalRatio) * (60.0 / (2.0 * PiScalar));
    const double LaunchRpm  = ThrottleInput > 0.05 ? (700.0 + 1900.0 * ThrottleInput) : 700.0;
    EngineRpm               = std::clamp(std::max(CoupledRpm, LaunchRpm),
                                         ControlVehicleSpecification::IdleRpm,
                                         ControlVehicleSpecification::RedlineRpm);

    if (ShiftCooldownSec <= 0.0)
    {
        if (GearSlotIndex >= 3 && GearSlotIndex < 8 && EngineRpm > 6800.0 && ThrottleInput > 0.25)
        {
            ++GearSlotIndex;
            ShiftCooldownSec = 0.35;
        }
        else if (GearSlotIndex > 3 && EngineRpm < 2600.0 && ForwardSpeedMps > 2.0)
        {
            --GearSlotIndex;
            ShiftCooldownSec = 0.35;
        }
    }

    const double TargetBoost = ThrottleInput * ControlVehicleSpecification::WastegateMaxBoostBar *
        std::clamp((EngineRpm - 1400.0) / 3200.0, 0.0, 1.0);
    TurboBoostBar += (TargetBoost - TurboBoostBar) * (1.0 - std::exp(-Dt / 0.55));

    const double BaseTorqueNm  = ControlVehicleSpecification::SampleEngineTorqueNm(EngineRpm) *
        ControlVehicleSpecification::SampleTurboMultiplier(TurboBoostBar);
    const double EngineDragNm  = 0.08 * (EngineRpm * 2.0 * PiScalar / 60.0);
    const double NetEngineNm   = ThrottleInput * BaseTorqueNm - (1.0 - ThrottleInput) * EngineDragNm;
    const double RearAxleDrive = NetEngineNm * TotalRatio * ControlVehicleSpecification::DriveEfficiency;

    // Static + dynamic load transfer across the 4 digressive struts
    const double SprungMass    = ControlVehicleSpecification::SprungMassKilograms();
    const double WheelbaseM    = 1.7274 + 1.6686;
    const double TrackWidthM   = 2.0 * 1.0475;
    double       NetWorldFx    = 0.0;
    double       NetWorldFy    = 0.0;
    double       NetYawTorque  = 0.0;

    for (size_t I = 0u; I < 4u; ++I)
    {
        auto&        W        = Wheels[I];
        const bool   IsFront  = (I < 2u);
        const bool   IsLeft   = ((I & 1u) == 0u);
        const double SignX    = IsFront ? 1.0 : -1.0;
        const double SignY    = IsLeft ? 1.0 : -1.0;
        const double LocalX   = ControlVehicleSpecification::AxleMountModel[I][0];
        const double LocalY   = ControlVehicleSpecification::AxleMountModel[I][1];

        // Suspension compression from body heave relative to local terrain elevation, pitch, and roll
        const double RestComZ = GroundElevationM + ControlVehicleSpecification::CentreOfMassHeightM;
        const double BaseComp = Struts[I].StaticCompressionM - (Position[2] - RestComZ);
        const double Comp     = std::clamp(BaseComp - SignX * PitchRadians * (0.5 * WheelbaseM) + SignY * RollRadians * (0.5 * TrackWidthM),
                                           0.0, Struts[I].MaxTravelM);
        const double CompVel  = -LinearVelocity[2] - SignX * PitchRateRadPerSec * (0.5 * WheelbaseM) + SignY * RollRateRadPerSec * (0.5 * TrackWidthM);

        W.CompressionM = Comp;
        W.StrutLengthM = Struts[I].FreeLengthM - Comp;
        W.Grounded     = (Comp > 1.0e-4);
        const double SpringN = W.Grounded
            ? std::max(0.0, Struts[I].PreloadNewtons + Struts[I].SpringForce(Comp) + Struts[I].DampingRateNsm * CompVel)
            : 0.0;
        W.NormalLoadN      = SpringN;
        const double Stiff = 25000.0 + 0.90 * W.GaugePressurePa;
        W.TyreDeflectionM  = W.Grounded ? std::clamp(SpringN / Stiff, 0.0, 0.085) : 0.0;

        // Ackermann steer on front axle
        double WheelSteer = 0.0;
        if (IsFront && std::fabs(SteeringAngleRad) > 1.0e-5)
        {
            const double TurnR = WheelbaseM / std::tan(std::fabs(SteeringAngleRad));
            const double Inner = std::atan(WheelbaseM / std::max(0.8, TurnR - 0.5 * TrackWidthM));
            const double Outer = std::atan(WheelbaseM / (TurnR + 0.5 * TrackWidthM));
            const bool   InnerSide = (SteeringAngleRad > 0.0) ? IsLeft : !IsLeft;
            WheelSteer = std::copysign(InnerSide ? Inner : Outer, SteeringAngleRad);
        }
        W.SteerAngleRad = WheelSteer;

        // Wheel hub velocity in steered wheel frame
        const double HubVxBody = ForwardSpeedMps - YawRateRadPerSec * LocalY;
        const double HubVyBody = LatVel          + YawRateRadPerSec * LocalX;
        const double Cs        = std::cos(WheelSteer);
        const double Ss        = std::sin(WheelSteer);
        const double VLong     =  HubVxBody * Cs + HubVyBody * Ss;
        const double VLat      = -HubVxBody * Ss + HubVyBody * Cs;

        const double DriveTorque = IsFront ? (0.5 * 0.35 * RearAxleDrive) : (0.5 * 0.65 * RearAxleDrive);
        const double BrakeTorque = BrakeInput * 2600.0 + ((!IsFront && Inputs.Handbrake) ? 4200.0 : 0.0);

        const double RollingSpeed = VLong / ControlVehicleSpecification::TyreOuterRadiusM;
        const double RadiusM      = ControlVehicleSpecification::TyreOuterRadiusM;
        if (W.Grounded && W.NormalLoadN > 25.0)
        {
            // Unconditionally stable semi-implicit wheel-spin / Pacejka traction coupling
            const double PeakTireTorque = std::max(180.0, W.NormalLoadN * 1.52 * RadiusM);
            const double NetAxleTorque  = DriveTorque - std::copysign(
                std::min(BrakeTorque, std::fabs(DriveTorque) + PeakTireTorque),
                (std::fabs(W.SpinAngularVel) > 0.2) ? W.SpinAngularVel : 1.0);
            const double TorqueRatio    = NetAxleTorque / PeakTireTorque;
            const double TargetSlip     = std::clamp(TorqueRatio * 0.14 +
                                                     (std::fabs(TorqueRatio) > 1.0 ? std::copysign((std::fabs(TorqueRatio) - 1.0) * 0.65, TorqueRatio) : 0.0),
                                                     -0.85, 1.35);
            const double RefV           = std::max(2.2, std::fabs(VLong));
            const double TargetOmega    = RollingSpeed + (TargetSlip * RefV) / RadiusM;
            W.SpinAngularVel           += (TargetOmega - W.SpinAngularVel) * (1.0 - std::exp(-36.0 * Dt));
            if (ThrottleInput < 0.01 && BrakeTorque > 50.0 && std::fabs(VLong) < 0.25)
            {
                W.SpinAngularVel = 0.0;
            }
        }
        else
        {
            W.SpinAngularVel = (W.SpinAngularVel + Dt * (DriveTorque / 1.35)) * std::exp(-1.5 * Dt);
        }
        W.SpinAngleRad += W.SpinAngularVel * Dt;

        const double RefSpeed = std::max(1.2, std::fabs(VLong));
        W.SlipRatio           = W.Grounded
            ? std::clamp((W.SpinAngularVel * ControlVehicleSpecification::TyreOuterRadiusM - VLong) / RefSpeed, -0.92, 1.50)
            : 0.0;
        W.SlipAngleRad        = W.Grounded ? std::atan2(-VLat, RefSpeed) : 0.0;

        const CombinedTyreForce ForcesLocal = W.Grounded
            ? SlipModel.Combine(W.SlipRatio, W.SlipAngleRad, 0.0, W.NormalLoadN)
            : CombinedTyreForce{ 0.0, 0.0 };
        W.LongitudinalFx = ForcesLocal.LongitudinalFx;
        W.LateralFy      = ForcesLocal.LateralFy;

        // Thermal & pressure response (ideal gas law + sliding friction work)
        const double SlipSpeed = std::hypot(W.SpinAngularVel * ControlVehicleSpecification::TyreOuterRadiusM - VLong, VLat);
        const double PowerW    = std::fabs(W.LongitudinalFx * (W.SpinAngularVel * ControlVehicleSpecification::TyreOuterRadiusM - VLong)) +
                                 std::fabs(W.LateralFy * VLat);
        W.SlidingEnergyJ      += PowerW * Dt;
        W.SurfaceTempKelvin    = std::clamp(W.SurfaceTempKelvin + (PowerW * 1.8e-4 - (W.SurfaceTempKelvin - 298.15) * 0.045) * Dt,
                                            293.15, 620.0);
        W.GaugePressurePa      = 220000.0 * (W.SurfaceTempKelvin / 341.15);
        W.Ignited              = (W.SurfaceTempKelvin > 540.0);
        if (SlipSpeed > 4.0)
        {
            W.TreadFraction = std::max(0.05, W.TreadFraction - 1.2e-5 * SlipSpeed * Dt);
        }

        // Rotate wheel force back to body -> world
        const double FxBody = W.LongitudinalFx * Cs - W.LateralFy * Ss;
        const double FyBody = W.LongitudinalFx * Ss + W.LateralFy * Cs;
        NetYawTorque       += LocalX * FyBody - LocalY * FxBody;
        NetWorldFx         += FxBody * CosYaw - FyBody * SinYaw;
        NetWorldFy         += FxBody * SinYaw + FyBody * CosYaw;

        // Step 3D Neo-Hookean FEM continuum carcass
        if (ActiveTyreModel != TyreContinuumModel::LinearSpring)
        {
            const auto YawQ   = AxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, static_cast<float>(YawRadians + WheelSteer));
            const auto SpinQ  = AxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, static_cast<float>(W.SpinAngleRad));
            const auto WheelQ = MultiplyQuaternion(YawQ, SpinQ);
            WheelStepInputs StepIn{};
            StepIn.Center       = {
                static_cast<float>(Position[0] + LocalX * CosYaw - LocalY * SinYaw),
                static_cast<float>(Position[1] + LocalX * SinYaw + LocalY * CosYaw),
                static_cast<float>(ControlVehicleSpecification::TyreOuterRadiusM - W.TyreDeflectionM)
            };
            StepIn.Orientation  = WheelQ;
            StepIn.Deflection   = static_cast<float>(W.TyreDeflectionM);
            StepIn.Pressure     = static_cast<float>(W.GaugePressurePa);
            StepIn.LateralForce = static_cast<float>(W.LateralFy);
            (void)ContinuumWheels[I].Step(StepIn, static_cast<float>(Dt), 2);
        }
    }

    // Aerodynamic drag (0.5 * rho * CdA = 0.38) + tyre rolling resistance (~190 N) + 6-DOF integration
    const double SpeedNorm = std::hypot(LinearVelocity[0], LinearVelocity[1]);
    const double DragCoeff = 0.38;
    const double RollRes   = (SpeedNorm > 0.05) ? (190.0 / SpeedNorm) : 12.0;
    const double DragFx    = -DragCoeff * SpeedNorm * LinearVelocity[0] - RollRes * LinearVelocity[0];
    const double DragFy    = -DragCoeff * SpeedNorm * LinearVelocity[1] - RollRes * LinearVelocity[1];

    const double TotalMass = ControlVehicleSpecification::MassKilograms;
    const double AccelX    = (NetWorldFx + DragFx) / TotalMass;
    const double AccelY    = (NetWorldFy + DragFy) / TotalMass;

    LinearVelocity[0] += AccelX * Dt;
    LinearVelocity[1] += AccelY * Dt;
    if (ThrottleInput < 0.01 && SpeedNorm < 0.12 && BrakeInput > 0.05)
    {
        LinearVelocity[0] = 0.0;
        LinearVelocity[1] = 0.0;
    }

    const double YawInertia = SprungMass * ControlVehicleSpecification::YawRadiusGyrationM * ControlVehicleSpecification::YawRadiusGyrationM;
    YawRateRadPerSec        = (YawRateRadPerSec + (NetYawTorque / YawInertia) * Dt) * std::exp(-2.6 * Dt);
    YawRadians             += YawRateRadPerSec * Dt;

    // Longitudinal/lateral load-transfer pitch & roll dynamics
    const double LongAccelBody =  AccelX * CosYaw + AccelY * SinYaw;
    const double LatAccelBody  = -AccelX * SinYaw + AccelY * CosYaw;
    const double TargetPitch   = std::clamp( LongAccelBody * 0.0028, -0.055, 0.055);
    const double TargetRoll    = std::clamp(-LatAccelBody  * 0.0042, -0.075, 0.075);
    PitchRateRadPerSec         = (TargetPitch - PitchRadians) * 8.0;
    RollRateRadPerSec          = (TargetRoll  - RollRadians)  * 8.0;
    PitchRadians              += PitchRateRadPerSec * Dt;
    RollRadians               += RollRateRadPerSec  * Dt;

    Position[0] = std::clamp(Position[0] + LinearVelocity[0] * Dt, -101.0, 101.0);
    Position[1] = std::clamp(Position[1] + LinearVelocity[1] * Dt,  -55.0,  55.0);
    Position[2] = GroundElevationM + ControlVehicleSpecification::CentreOfMassHeightM;

    ForwardSpeedMps     = LinearVelocity[0] * std::cos(YawRadians) + LinearVelocity[1] * std::sin(YawRadians);
    SpeedKilometresHour = std::fabs(ForwardSpeedMps) * 3.6;

    const auto Qz = AxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, static_cast<float>(YawRadians));
    const auto Qy = AxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, static_cast<float>(PitchRadians));
    const auto Qx = AxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, static_cast<float>(RollRadians));
    const auto Q  = MultiplyQuaternion(MultiplyQuaternion(Qz, Qy), Qx);
    Orientation   = { Q[0], Q[1], Q[2], Q[3] };
}

void ControlVehicleSolver::EncodeVisualRecord(VehicleVisualRecord& OutRecord, bool SeeThroughGlass) const noexcept
{
    OutRecord.Data.fill(0.0f);
    OutRecord.Data[0]  = static_cast<float>(SteeringAngleRad);
    OutRecord.Data[1]  = SeeThroughGlass ? 1.0f : 0.0f;
    OutRecord.Data[8]  = 1.0f;
    OutRecord.Data[15] = 1.0f;
    OutRecord.Data[19] = 1.0f;

    const std::array<float, 4> BaseQ = {
        static_cast<float>(Orientation[0]),
        static_cast<float>(Orientation[1]),
        static_cast<float>(Orientation[2]),
        static_cast<float>(Orientation[3])
    };

    for (size_t Index = 0u; Index < 4u; ++Index)
    {
        const WheelCornerTelemetry& W           = Wheels[Index];
        const size_t                WheelOffset = 20u + Index * 4u;
        OutRecord.Data[WheelOffset + 0u]        = W.Shredded ? 1.0f : 0.0f;
        if (W.Shredded)
        {
            continue;
        }

        const auto Steered   = MultiplyQuaternion(BaseQ, AxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, static_cast<float>(W.SteerAngleRad)));
        const auto Spun      = MultiplyQuaternion(Steered, AxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, static_cast<float>(W.SpinAngleRad)));
        const auto LocalDown = RotateByQuaternion({ -Spun[0], -Spun[1], -Spun[2], Spun[3] }, { 0.0f, 0.0f, -1.0f });
        const float ContactAngle      = std::atan2(LocalDown[2], LocalDown[0]);
        const float LateralDistortion = std::clamp(static_cast<float>(W.LateralFy / 150000.0), -0.04f, 0.04f);

        const size_t DeformOffset         = 36u + Index * 4u;
        OutRecord.Data[DeformOffset + 0u] = ContactAngle;
        OutRecord.Data[DeformOffset + 1u] = static_cast<float>(std::max(0.0, W.TyreDeflectionM));
        OutRecord.Data[DeformOffset + 2u] = static_cast<float>(std::max(0.0, W.GaugePressurePa));
        OutRecord.Data[DeformOffset + 3u] = LateralDistortion;

        const bool CarcassActive = (ActiveTyreModel != TyreContinuumModel::LinearSpring);
        OutRecord.Data[WheelOffset + 1u] = CarcassActive ? 1.0f : 0.0f;
        OutRecord.Data[WheelOffset + 2u] = static_cast<float>(static_cast<uint8_t>(ActiveTyreModel));

        if (CarcassActive)
        {
            const SolidContinuumWheel& Carcass = ContinuumWheels[Index];
            const size_t               BaseOut = VehicleVisualRecord::HeaderFloats + Index * SolidContinuumWheel::NodesPerWheel * 4u;
            for (uint32_t N = 0u; N < SolidContinuumWheel::NodesPerWheel; ++N)
            {
                const uint32_t N3 = N * 3u;
                const size_t   O4 = BaseOut + static_cast<size_t>(N) * 4u;
                OutRecord.Data[O4 + 0u] = Carcass.LocalPositions[N3 + 0u] - Carcass.RestLocal[N3 + 0u];
                OutRecord.Data[O4 + 1u] = Carcass.LocalPositions[N3 + 1u] - Carcass.RestLocal[N3 + 1u];
                OutRecord.Data[O4 + 2u] = Carcass.LocalPositions[N3 + 2u] - Carcass.RestLocal[N3 + 2u];
                OutRecord.Data[O4 + 3u] = 0.0f;
            }
        }
    }
}

} // namespace Frontier
