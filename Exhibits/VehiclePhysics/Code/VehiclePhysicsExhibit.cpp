//============================================================================================================================
// 🧩 VehiclePhysics exhibit — proves the Phase-7 physics ports against the HTML demonstration's own code and numbers.
//============================================================================================================================
// 📝 A  MF6.1 + relaxation + thermal trajectory + PB-FEM vertices vs TyreOracle.mjs (the demo's JS, run under node).
//    B  Static settle of the ported VehicleSolver vs the demo's VolumetricTyrePhysics.test.mjs expectations.
//    C  XPBD lattice load/deflection rig vs the demo's carcass-spring K = 25 kN/m + 0.9·p curve (calibration proof).
//    D  The whole car standing and driving on the XPBD lattice carcass.
//    E  Scripted drive on the 1:1 spring model: launch, gearshifts, slalom, brake, line-lock burnout (thermal), launch.
//    F  Jolt loose-body scene (JoltScene.cpp): cone row drive-through + wall impact through the chassis proxy.
//    Writes CSV evidence next to Results.txt for VehiclePhysicsImages.py. Usage: VehiclePhysicsExhibit <oracle> <outdir>
//============================================================================================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Engine/Physics/TyreCarcassLattice.h"
#include "Engine/Physics/TyreDeformation.h"
#include "Engine/Physics/TyreSlipSolver.h"
#include "Engine/Physics/TyreThermalSolver.h"
#include "Engine/Physics/VehicleSolver.h"
#include "JoltScene.h"

using namespace Frontier;

static int Passed = 0, Failed = 0;
static void Check(const char* Name, bool Condition, const char* Detail)
{
    std::printf("[%s] %s %s\n", Condition ? "PASS" : "FAIL", Name, Detail);
    if (Condition) ++Passed; else ++Failed;
}
static bool Finite3(const double V[3]) { return std::isfinite(V[0]) && std::isfinite(V[1]) && std::isfinite(V[2]); }

//------------------------------------------------------------------------------------------------------------------------
//                                                       A. ORACLE CHECKS
//------------------------------------------------------------------------------------------------------------------------

static void RunOracleChecks(const char* OraclePath)
{
    std::FILE* File = std::fopen(OraclePath, "r");
    char Detail[256];
    if (!File)
    {
        Check("A oracle file present", false, OraclePath);
        return;
    }
    TyreSlipSolver Tyres;
    double MaxRelFx = 0.0, MaxRelFy = 0.0, MaxRelCb = 0.0, MaxRelRl = 0.0;
    int FxCount = 0, FyCount = 0, CbCount = 0, RlCount = 0;
    struct ThermalRow { double T, Surface, Carcass, Gas, Pressure, Grip, Tread; };
    std::vector<ThermalRow> ThermalRows;
    TyreDeformationRecord FemRecord;
    bool HaveRecord = false;
    double MaxFemError = 0.0;
    int FemCount = 0;
    char Line[1024];
    while (std::fgets(Line, sizeof Line, File))
    {
        double A, B, C, D, E, F, G, H, I;
        if (std::sscanf(Line, "FX %lf %lf %lf %lf", &A, &B, &C, &D) == 4)
        {
            const double Mine = Tyres.LongitudinalForce(A, B, C);
            MaxRelFx = std::fmax(MaxRelFx, std::fabs(Mine - D) / std::fmax(1.0, std::fabs(D)));
            ++FxCount;
        }
        else if (std::sscanf(Line, "FY %lf %lf %lf %lf", &A, &B, &C, &D) == 4)
        {
            const double Mine = Tyres.LateralForce(A, B, C);
            MaxRelFy = std::fmax(MaxRelFy, std::fabs(Mine - D) / std::fmax(1.0, std::fabs(D)));
            ++FyCount;
        }
        else if (std::sscanf(Line, "CB %lf %lf %lf %lf %lf %lf", &A, &B, &C, &D, &E, &F) == 6)
        {
            double Mine[2];
            Tyres.Combine(A, B, C, D, Mine);
            MaxRelCb = std::fmax(MaxRelCb, std::fabs(Mine[0] - E) / std::fmax(1.0, std::fabs(E)));
            MaxRelCb = std::fmax(MaxRelCb, std::fabs(Mine[1] - F) / std::fmax(1.0, std::fabs(F)));
            ++CbCount;
        }
        else if (std::sscanf(Line, "RL %lf %lf %lf", &A, &B, &C) == 3)
        {
            double Mine[2];
            Tyres.ResolveRelaxationLengths(A, Mine);
            MaxRelRl = std::fmax(MaxRelRl, std::fabs(Mine[0] - B) / std::fmax(1.0e-9, std::fabs(B)));
            MaxRelRl = std::fmax(MaxRelRl, std::fabs(Mine[1] - C) / std::fmax(1.0e-9, std::fabs(C)));
            ++RlCount;
        }
        else if (std::sscanf(Line, "TH %lf %lf %lf %lf %lf %lf %lf", &A, &B, &C, &D, &E, &F, &G) == 7)
            ThermalRows.push_back({ A, B, C, D, E, F, G });
        else if (std::sscanf(Line, "FR %lf %lf %lf %lf", &A, &B, &C, &D) == 4)
        {
            FemRecord.ContactAngle = (float)A;
            FemRecord.Deflection = (float)B;
            FemRecord.GaugePressure = (float)C;
            FemRecord.LateralDistortion = (float)D;
            FemRecord.Shredded = false;
            HaveRecord = true;
        }
        else if (HaveRecord && std::sscanf(Line, "FV %lf %lf %lf %lf %lf %lf", &A, &B, &C, &D, &E, &F) == 6)
        {
            const float In[3] = { (float)A, (float)B, (float)C };
            const float Normal[3] = { 0.0f, 0.0f, 1.0f };
            float Out[3], OutN[3];
            ResolveDeformedWheelVertex(FemRecord, true, In, Normal, Out, OutN);
            MaxFemError = std::fmax(MaxFemError, std::fabs(Out[0] - D));
            MaxFemError = std::fmax(MaxFemError, std::fabs(Out[1] - E));
            MaxFemError = std::fmax(MaxFemError, std::fabs(Out[2] - F));
            ++FemCount;
        }
        (void)H; (void)I;
    }
    std::fclose(File);
    std::snprintf(Detail, sizeof Detail, "n=%d max rel err %.3e (tol 1e-9)", FxCount, MaxRelFx);
    Check("A1 MF6.1 Fx vs demo JS", FxCount > 100 && MaxRelFx < 1.0e-9, Detail);
    std::snprintf(Detail, sizeof Detail, "n=%d max rel err %.3e (tol 1e-9)", FyCount, MaxRelFy);
    Check("A2 MF6.1 Fy vs demo JS", FyCount > 100 && MaxRelFy < 1.0e-9, Detail);
    std::snprintf(Detail, sizeof Detail, "n=%d max rel err %.3e (tol 1e-9)", CbCount, MaxRelCb);
    Check("A3 MF6.1 combined slip vs demo JS", CbCount > 20 && MaxRelCb < 1.0e-9, Detail);
    std::snprintf(Detail, sizeof Detail, "n=%d max rel err %.3e (tol 1e-9)", RlCount, MaxRelRl);
    Check("A4 relaxation lengths vs demo JS", RlCount >= 6 && MaxRelRl < 1.0e-9, Detail);
    std::snprintf(Detail, sizeof Detail, "n=%d max abs err %.3e m (tol 2e-4)", FemCount, MaxFemError);
    Check("A5 PB-FEM vertices vs WGSL transliteration", FemCount > 1000 && MaxFemError < 2.0e-4, Detail);
    // Thermal trajectory replay: identical schedule to TyreOracle.mjs.
    double MaxThermalRel = 0.0;
    {
        TyreThermalSolver Thermal(4);
        const TyreIgnitionMode Authored;
        TyreThermalReading Readings[4];
        for (int W = 0; W < 4; ++W)
        {
            Readings[W].SlidingPower = 0.0;
            Readings[W].ContactFraction = 1.0;
            Readings[W].Airspeed = 8.0;
            Readings[W].Shredded = false;
        }
        size_t Row = 0;
        for (int Step = 0; Step <= 96 && Row < ThermalRows.size(); ++Step, ++Row)
        {
            const double T = Step / 12.0;
            const TyreThermalWheel& W = Thermal.Wheels[2];
            const ThermalRow& R = ThermalRows[Row];
            const auto Rel = [](double Mine, double Ref) { return std::fabs(Mine - Ref) / std::fmax(1.0, std::fabs(Ref)); };
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.Surface, R.Surface));
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.Carcass, R.Carcass));
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.Gas, R.Gas));
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.GaugePressure, R.Pressure));
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.Grip, R.Grip));
            MaxThermalRel = std::fmax(MaxThermalRel, Rel(W.TreadMass, R.Tread));
            (void)T;
            Readings[2].SlidingPower = T < 3.0 ? 25000.0 : 0.0;
            Thermal.Advance(Readings, 1.0 / 12.0, Authored);
        }
        std::snprintf(Detail, sizeof Detail, "rows=%zu max rel err %.3e (tol 1e-9)", ThermalRows.size(), MaxThermalRel);
        Check("A6 thermal trajectory vs demo JS", ThermalRows.size() == 97 && MaxThermalRel < 1.0e-9, Detail);
    }
    // Pacejka curve dump for the image.
    std::FILE* Csv = std::fopen("pacejka.csv", "w");
    if (Csv)
    {
        std::fprintf(Csv, "# kappa Fx2k Fx4k Fx8k | alpha Fy2k Fy4k Fy8k\n");
        for (int I = 0; I <= 120; ++I)
        {
            const double Kappa = -0.92 + (1.5 + 0.92) * I / 120.0;
            const double Alpha = -1.3 + 2.6 * I / 120.0;
            std::fprintf(Csv, "%.5f %.2f %.2f %.2f %.5f %.2f %.2f %.2f\n", Kappa,
                         Tyres.LongitudinalForce(Kappa, 0.0, 2000.0), Tyres.LongitudinalForce(Kappa, 0.0, 4000.0),
                         Tyres.LongitudinalForce(Kappa, 0.0, 8000.0), Alpha,
                         Tyres.LateralForce(Alpha, 0.0, 2000.0), Tyres.LateralForce(Alpha, 0.0, 4000.0),
                         Tyres.LateralForce(Alpha, 0.0, 8000.0));
        }
        std::fclose(Csv);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       B. STATIC SETTLE (demo test expectations)
//------------------------------------------------------------------------------------------------------------------------

static void RunSettleChecks()
{
    char Detail[256];
    VehicleSolver Car;
    const double Spawn[3] = { -100.0, -34.0, 0.53 };
    Car.Respawn(Spawn, 0.0);
    VehicleCommand Idle;
    for (int I = 0; I < 240; ++I)
        Car.Advance(Idle, 1.0 / 120.0);
    bool AllContact = true, DeflBand = true, AngleBand = true, PressureBand = true, LatZero = true;
    double LoadSum = 0.0, MinDefl = 1.0, MaxDefl = 0.0;
    for (int W = 0; W < 4; ++W)
    {
        const VehicleWheelState& Wheel = Car.Wheels[W];
        AllContact = AllContact && Wheel.Contact;
        DeflBand = DeflBand && Wheel.TyreDeflection > 0.010 && Wheel.TyreDeflection < 0.025;
        MinDefl = std::fmin(MinDefl, Wheel.TyreDeflection);
        MaxDefl = std::fmax(MaxDefl, Wheel.TyreDeflection);
        LoadSum += Wheel.Load;
        const TyreDeformationRecord Record = ResolveTyreDeformationRecord(Wheel, Car.Orientation);
        AngleBand = AngleBand && std::fabs(Record.ContactAngle - (-3.141592653589793 * 0.5)) < 0.05;
        PressureBand = PressureBand && Record.GaugePressure >= 200000.0f && Record.GaugePressure <= 250000.0f;
        LatZero = LatZero && std::fabs(Record.LateralDistortion) < 1.0e-4;
    }
    Check("B1 settle: all wheels in contact", AllContact, "");
    std::snprintf(Detail, sizeof Detail, "deflection %.4f..%.4f m (demo band 0.010-0.025)", MinDefl, MaxDefl);
    Check("B2 settle: static deflection band", DeflBand, Detail);
    Check("B3 settle: contact angle -pi/2 within 0.05", AngleBand, "");
    Check("B4 settle: pressure 200-250 kPa", PressureBand, "");
    Check("B5 settle: zero lateral distortion at rest", LatZero, "");
    const double Weight = 1300.0 * 9.81;
    std::snprintf(Detail, sizeof Detail, "sum %.0f N vs weight %.0f N (%.2f%%)", LoadSum, Weight, 100.0 * std::fabs(LoadSum - Weight) / Weight);
    Check("B6 settle: corner loads sum to weight within 3%", std::fabs(LoadSum - Weight) / Weight < 0.03, Detail);
    // Encoder record fields (demo test 2/3 equivalents on synthetic wheel states).
    VehicleWheelState Synthetic;
    Synthetic.TyreDeflection = 0.060;
    Synthetic.Pressure = 30000.0;
    const double Identity[4] = { 0.0, 0.0, 0.0, 1.0 };
    TyreDeformationRecord Record = ResolveTyreDeformationRecord(Synthetic, Identity);
    const bool Flat = std::fabs(Record.Deflection - 0.060f) < 1.0e-6f && Record.GaugePressure == 30000.0f;
    Synthetic.Shredded = true;
    Record = ResolveTyreDeformationRecord(Synthetic, Identity);
    const bool ShreddedEmpty = Record.Deflection == 0.0f && Record.Shredded;
    Synthetic = VehicleWheelState();
    Synthetic.TyreDeflection = 0.018;
    Synthetic.Pressure = 220000.0;
    Synthetic.LateralForce = 4500.0;
    Record = ResolveTyreDeformationRecord(Synthetic, Identity);
    std::snprintf(Detail, sizeof Detail, "lat distortion %.4f m (expect 0.03)", Record.LateralDistortion);
    Check("B7 encoder: flat/shredded/cornering records", Flat && ShreddedEmpty && std::fabs(Record.LateralDistortion - 0.03f) < 1.0e-6f, Detail);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       C. XPBD LATTICE RIG
//------------------------------------------------------------------------------------------------------------------------

static double SpringForce(double Pressure, double Deflection)
{
    const double K = 25000.0 + 0.9 * Pressure;
    const double Pinch = 0.50855 - 0.34460 - 0.022;
    return K * Deflection + 1.5e6 * std::fmax(0.0, Deflection - Pinch);
}

static void RunLatticeRig()
{
    char Detail[256];
    const double Pressures[4] = { 0.0, 110000.0, 220000.0, 330000.0 };
    const double Deflections[6] = { 0.005, 0.010, 0.015, 0.020, 0.030, 0.040 };
    std::FILE* Csv = std::fopen("lattice.csv", "w");
    if (Csv)
        std::fprintf(Csv, "# pressure deflection latticeN springN ratio volume restvolume ktan\n");
    double ForceAt20[4] = { 0.0, 0.0, 0.0, 0.0 };
    bool Band220 = true, Monotonic220 = true;
    double Previous220 = -1.0, VolumeChange220 = 0.0, Ktan220 = 0.0;
    for (int P = 0; P < 4; ++P)
    {
        for (int D = 0; D < 6; ++D)
        {
            TyreCarcassLattice Lattice;
            TyreLatticeInput Input;
            Input.HubPosition[2] = 0.50855 - Deflections[D];
            Input.GroundHeight = 0.0;
            Input.GaugePressure = Pressures[P];
            Input.Seconds = 1.0 / 480.0;
            TyreLatticeOutput Output;
            double Sum = 0.0;
            int Count = 0;
            for (int S = 0; S < 720; ++S)
            {
                Output = Lattice.Step(Input);
                if (S >= 600) { Sum += Output.NormalForce; ++Count; }
            }
            Output.NormalForce = Sum / Count;
            const double Spring = SpringForce(Pressures[P], Deflections[D]);
            const double Ratio = Spring > 0.0 ? Output.NormalForce / Spring : 0.0;
            if (Csv)
                std::fprintf(Csv, "%.0f %.4f %.1f %.1f %.4f %.5f %.5f %.0f\n", Pressures[P], Deflections[D],
                             Output.NormalForce, Spring, Ratio, Output.Volume, Output.RestVolume, Output.TangentStiffness);
            if (P == 2)
            {
                if (D >= 1 && (Ratio < 0.75 || Ratio > 1.30))
                    Band220 = false;
                if (Output.NormalForce <= Previous220)
                    Monotonic220 = false;
                Previous220 = Output.NormalForce;
                if (D == 3)
                {
                    VolumeChange220 = (Output.Volume - Output.RestVolume) / Output.RestVolume;
                    Ktan220 = Output.TangentStiffness;
                }
                // Particle snapshot at 25 mm for the side-view image.
                if (D == 4)
                {
                    std::FILE* Particles = std::fopen("lattice_particles.csv", "w");
                    if (Particles)
                    {
                        std::fprintf(Particles, "# x y z (wheel frame, hub origin)\n");
                        for (int Index = 0; Index < TyreCarcassLattice::ParticleCount; ++Index)
                        {
                            double Pos[3];
                            Lattice.QueryParticle(Index, Pos);
                            std::fprintf(Particles, "%.5f %.5f %.5f\n", Pos[0], Pos[1], Pos[2]);
                        }
                        std::fclose(Particles);
                    }
                }
            }
            if (D == 3)
                ForceAt20[P] = Output.NormalForce;
        }
    }
    if (Csv)
        std::fclose(Csv);
    Check("C1 lattice vs spring at 220 kPa within 0.75-1.30 (10-40 mm)", Band220, "see lattice.csv");
    Check("C2 lattice force monotonic in deflection at 220 kPa", Monotonic220, "");
    std::snprintf(Detail, sizeof Detail, "F(20mm) = %.0f / %.0f / %.0f / %.0f N at 0/110/220/330 kPa",
                  ForceAt20[0], ForceAt20[1], ForceAt20[2], ForceAt20[3]);
    Check("C3 lattice force ordering by pressure at 20 mm",
          ForceAt20[3] > ForceAt20[2] && ForceAt20[2] > ForceAt20[1] && ForceAt20[1] > ForceAt20[0], Detail);
    std::snprintf(Detail, sizeof Detail, "dV/V = %+.3f%% at 220 kPa, 20 mm (tol 3%%)", 100.0 * VolumeChange220);
    Check("C4 lattice enclosed volume conserved under load", std::fabs(VolumeChange220) < 0.03, Detail);
    std::snprintf(Detail, sizeof Detail, "tangent stiffness %.0f N/m", Ktan220);
    Check("C5 lattice tangent stiffness in vehicle band", Ktan220 > 80000.0 && Ktan220 < 450000.0, Detail);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       D. CAR ON LATTICE
//------------------------------------------------------------------------------------------------------------------------

static void RunLatticeCar()
{
    char Detail[256];
    VehicleSolver Car;
    Car.AssignCarcassModel(CarcassLatticeModel);
    const double Spawn[3] = { 0.0, 0.0, 0.55 };
    Car.Respawn(Spawn, 0.0);
    VehicleCommand Idle;
    bool Finite = true;
    for (int S = 0; S < 360; ++S)
    {
        Car.Advance(Idle, 1.0 / 120.0);
        Finite = Finite && Finite3(Car.Position) && Finite3(Car.LinearVelocity);
    }
    bool AllContact = true;
    double MeanDefl = 0.0;
    for (int W = 0; W < 4; ++W)
    {
        AllContact = AllContact && Car.Wheels[W].Contact;
        MeanDefl += Car.Wheels[W].TyreDeflection * 0.25;
    }
    std::snprintf(Detail, sizeof Detail, "mean deflection %.4f m, CoM z %.4f m", MeanDefl, Car.Position[2]);
    Check("D1 car settles on XPBD lattice", Finite && AllContact && MeanDefl > 0.006 && MeanDefl < 0.035
          && std::fabs(Car.Position[2] - 0.51) < 0.07, Detail);
    VehicleCommand Go;
    Go.Throttle = 1.0;
    const double StartX = Car.Position[0];
    for (int S = 0; S < 360; ++S)
    {
        Car.Advance(Go, 1.0 / 120.0);
        Finite = Finite && Finite3(Car.Position) && Finite3(Car.LinearVelocity);
    }
    const double Speed = std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]);
    std::snprintf(Detail, sizeof Detail, "travelled %.1f m, speed %.1f m/s after 3 s WOT", Car.Position[0] - StartX, Speed);
    Check("D2 car drives on XPBD lattice", Finite && Car.Position[0] - StartX > 8.0 && Speed > 6.0, Detail);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       E. SCRIPTED DRIVE (spring model, 1:1)
//------------------------------------------------------------------------------------------------------------------------

static void RunDrive()
{
    char Detail[256];
    VehicleSolver Car;
    const double Spawn[3] = { 0.0, 0.0, 0.55 };
    Car.Respawn(Spawn, 0.0);
    std::FILE* Csv = std::fopen("drive.csv", "w");
    if (Csv)
        std::fprintf(Csv, "# t x y speed rpm gear steer loadFL loadFR loadRL loadRR deflFL deflRR slipRL slipRR tempRL pressureRL skid\n");
    const auto Speed = [&]() { return std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]); };
    bool Finite = true;
    double Time = 0.0;
    const auto Advance = [&](const VehicleCommand& Command, int Steps)
    {
        for (int S = 0; S < Steps; ++S)
        {
            Car.Advance(Command, 1.0 / 120.0);
            Time += 1.0 / 120.0;
            Finite = Finite && Finite3(Car.Position) && Finite3(Car.LinearVelocity) && std::isfinite(Car.EngineRPM);
            if (Csv && S % 2 == 0)
                std::fprintf(Csv, "%.3f %.3f %.3f %.3f %.0f %d %.3f %.0f %.0f %.0f %.0f %.4f %.4f %.3f %.3f %.1f %.0f %.2f\n",
                             Time, Car.Position[0], Car.Position[1], Speed(), Car.EngineRPM, Car.GearIndex, Car.SteerAngle,
                             Car.Wheels[0].Load, Car.Wheels[1].Load, Car.Wheels[2].Load, Car.Wheels[3].Load,
                             Car.Wheels[0].TyreDeflection, Car.Wheels[3].TyreDeflection,
                             Car.Wheels[2].SlipRatio, Car.Wheels[3].SlipRatio,
                             Car.Thermal.Wheels[2].Surface, Car.Thermal.Wheels[2].GaugePressure,
                             std::fmax(Car.Wheels[2].Skid, Car.Wheels[3].Skid));
        }
    };
    VehicleCommand Idle;
    Advance(Idle, 120);                                   // settle 1 s
    VehicleCommand Launch;
    Launch.Throttle = 1.0;
    Advance(Launch, 120 * 7);                             // launch 7 s
    const double LaunchSpeed = Speed();
    const int TopGear = Car.GearIndex;
    std::snprintf(Detail, sizeof Detail, "%.1f m/s in 7 s, gear index %d, boost %.2f bar", LaunchSpeed, TopGear, Car.Boost);
    Check("E1 launch: >22 m/s with upshifts", LaunchSpeed > 22.0 && TopGear >= 5, Detail);
    // Brake down to slalom entry speed (the open-loop sine at launch speed would simply spin the car).
    {
        VehicleCommand SlowDown;
        SlowDown.Brake = 0.7;
        for (int S = 0; S < 120 * 4 && Speed() > 16.5; ++S)
            Advance(SlowDown, 1);
    }
    const auto Heading = [&]()
    {
        const double* Q = Car.Orientation;
        const double Fx = 1.0 - 2.0 * (Q[1] * Q[1] + Q[2] * Q[2]);
        const double Fy = 2.0 * (Q[0] * Q[1] + Q[2] * Q[3]);
        return std::atan2(Fy, Fx);
    };
    // Slalom: sinusoidal steer about a heading/position feedback that keeps the car on the straight; count yaw-rate
    // sign changes and slip-angle peaks.
    int SignChanges = 0;
    double LastYawSign = 0.0, PeakSlipAngle = 0.0;
    for (int S = 0; S < 120 * 6; ++S)
    {
        VehicleCommand Command;
        // Speed governed to ~15 m/s and steer amplitude sized to the tyre: lateral demand grows with v²·δ, so an
        // ungoverned car accelerates itself past the 1.35 friction cap mid-slalom and saturates. At 15 m/s, 0.35
        // of the speed-eased lock asks for ~0.9 g — a hard slalom with real slip angles, not a spin.
        Command.Throttle = Speed() < 15.0 ? 0.4 : 0.0;
        Command.Brake = Speed() > 17.0 ? 0.35 : 0.0;
        Command.Steer = 0.35 * std::sin(2.0 * 3.141592653589793 * 0.35 * (S / 120.0))
                      - 0.9 * Heading() - 0.015 * Car.Position[1];
        Command.Steer = std::fmax(-1.0, std::fmin(1.0, Command.Steer));
        Advance(Command, 1);
        const double YawSign = Car.AngularVelocity[2] > 0.02 ? 1.0 : (Car.AngularVelocity[2] < -0.02 ? -1.0 : LastYawSign);
        if (LastYawSign != 0.0 && YawSign != LastYawSign)
            ++SignChanges;
        LastYawSign = YawSign;
        PeakSlipAngle = std::fmax(PeakSlipAngle, std::fabs(Car.Wheels[0].SlipAngle));
        PeakSlipAngle = std::fmax(PeakSlipAngle, std::fabs(Car.Wheels[2].SlipAngle));
    }
    std::snprintf(Detail, sizeof Detail, "yaw sign changes %d, peak |slip angle| %.3f rad", SignChanges, PeakSlipAngle);
    Check("E2 slalom: alternating yaw with real slip angles", SignChanges >= 3 && PeakSlipAngle > 0.06, Detail);
    // Brake to a stop.
    VehicleCommand Brake;
    Brake.Brake = 1.0;
    const double BrakeStartSpeed = Speed();
    const double BrakeStartX = Car.Position[0];
    double BrakeTime = 0.0;
    for (int S = 0; S < 120 * 10; ++S)
    {
        Advance(Brake, 1);
        BrakeTime += 1.0 / 120.0;
        if (Speed() < 0.15)
            break;
    }
    std::snprintf(Detail, sizeof Detail, "%.1f m/s -> stop in %.2f s / %.1f m", BrakeStartSpeed, BrakeTime, Car.Position[0] - BrakeStartX);
    Check("E3 full brake stop", Speed() < 0.3 && BrakeTime < 8.0, Detail);
    Advance(Idle, 60);
    // Line-lock burnout: both pedals down at a standstill.
    const double TempBefore = Car.Thermal.Wheels[2].Surface;
    VehicleCommand BurnoutCommand;
    BurnoutCommand.Throttle = 1.0;
    BurnoutCommand.Brake = 1.0;
    bool SawBurnout = false;
    double PeakRearSlip = 0.0, BurnoutSpeedMax = 0.0;
    for (int S = 0; S < 120 * 3; ++S)
    {
        Advance(BurnoutCommand, 1);
        SawBurnout = SawBurnout || Car.Burnout;
        PeakRearSlip = std::fmax(PeakRearSlip, std::fabs(Car.Wheels[2].SlipRatio));
        BurnoutSpeedMax = std::fmax(BurnoutSpeedMax, Speed());
    }
    const double TempRise = Car.Thermal.Wheels[2].Surface - TempBefore;
    std::snprintf(Detail, sizeof Detail, "engaged %d, peak rear slip %.2f, car speed <= %.2f m/s, rear tread +%0.1f K",
                  SawBurnout ? 1 : 0, PeakRearSlip, BurnoutSpeedMax, TempRise);
    Check("E4 line-lock burnout: rear slip held by front brakes, tyres heat",
          SawBurnout && PeakRearSlip > 0.4 && BurnoutSpeedMax < 2.5 && TempRise > 10.0, Detail);
    // Release: throttle only — the car should launch off the lock.
    Advance(Launch, 120 * 2);
    std::snprintf(Detail, sizeof Detail, "speed %.1f m/s 2 s after release", Speed());
    Check("E5 burnout release launches the car", Speed() > 7.0, Detail);
    Check("E6 finite state throughout the drive", Finite, "");
    if (Csv)
        std::fclose(Csv);
    // FEM outline dump for the image: authored ring at crown/shoulders, nominal vs flat records.
    std::FILE* Outline = std::fopen("fem_outline.csv", "w");
    if (Outline)
    {
        std::fprintf(Outline, "# record x y z (wheel local)\n");
        const TyreDeformationRecord Records[3] = {
            { -1.5707963f, 0.0f, 220000.0f, 0.0f, false },
            { -1.5707963f, 0.014f, 220000.0f, 0.0f, false },
            { -1.5707963f, 0.060f, 30000.0f, 0.02f, false }
        };
        for (int R = 0; R < 3; ++R)
            for (int Sector = 0; Sector <= 72; ++Sector)
            {
                const double Angle = Sector / 72.0 * 2.0 * 3.141592653589793;
                const float In[3] = { (float)(0.50855 * std::cos(Angle)), 0.10f, (float)(0.50855 * std::sin(Angle)) };
                const float Normal[3] = { (float)std::cos(Angle), 0.0f, (float)std::sin(Angle) };
                float Out[3], OutN[3];
                ResolveDeformedWheelVertex(Records[R], true, In, Normal, Out, OutN);
                std::fprintf(Outline, "%d %.5f %.5f %.5f\n", R, Out[0], Out[1], Out[2]);
            }
        std::fclose(Outline);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Argc, char** Argv)
{
    const char* Oracle = Argc > 1 ? Argv[1] : "oracle.txt";
    std::printf("VehiclePhysics exhibit — Phase 7 physics proofs\n");
    RunOracleChecks(Oracle);
    RunSettleChecks();
    RunLatticeRig();
    RunLatticeCar();
    RunDrive();
    const JoltSceneResult Jolt = RunJoltScene("jolt_trace.csv");
    char Detail[256];
    std::snprintf(Detail, sizeof Detail, "%d of %d cones displaced/tipped, total |dv| exchanged %.2f m/s",
                  Jolt.ConesDisplaced, Jolt.ConeCount, Jolt.TotalVelocityExchange);
    Check("F1 Jolt cone row: car knocks cones and feels them", Jolt.ConesDisplaced >= 5 && Jolt.TotalVelocityExchange > 0.3, Detail);
    std::snprintf(Detail, sizeof Detail, "impact at %.1f m/s -> %.2f m/s after 0.5 s, nose penetration %.3f m",
                  Jolt.WallImpactSpeedBefore, Jolt.WallSpeedAfter, Jolt.WallPenetration);
    Check("F2 Jolt wall: proxy exchange stops the car", Jolt.WallImpactSpeedBefore > 4.0 && Jolt.WallSpeedAfter < 2.0
          && Jolt.WallPenetration < 0.45, Detail);
    Check("F3 Jolt scene finite throughout", Jolt.FiniteThroughout, "");
    std::printf("SUMMARY passed %d failed %d\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
