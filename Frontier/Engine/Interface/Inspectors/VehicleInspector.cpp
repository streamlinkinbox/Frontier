/*============================================================================================================================================
                                                          VEHICLEINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 VehicleConfiguration page: three panes over the stock control kit — every powertrain/induction variable (incl.
//    the supercharger family with bypass valve and parasitic tax), chassis and per-corner soft-body tyre setup, and
//    the live telemetry published by the physics thread (speed, boost, charger speed, tyre pressure/heat/wear).

#include "Engine/Interface/Inspectors/VehicleInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

namespace
{
    const char* const kCornerNames[4] = { "FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT" };
    const char* const kCarcassNames[4] = { "Spring", "XPBD Lattice", "XPBD Projection", "FEM Solid" };
    const char* const kInductionNames[5] = { "Naturally Aspirated", "Turbo", "Roots SC", "TwinScrew SC", "Centrifugal SC" };
    const char* const kPaneNames[3] = { "Powertrain", "Chassis & Tyres", "Physics & Live" };
    const char* const kGearNames[9] = { "R2", "R1", "N", "1st", "2nd", "3rd", "4th", "5th", "6th" };
}

void VehicleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({ "Physics Active", "Proxy + solver", &Parameters.PhysicsActive });
    OutSwitches.push_back({ "Stock Turbo Law", "Demo bit-exact", &Parameters.StockTurboLaw });
    OutSwitches.push_back({ "Race Tune", "SC boost ceiling", &Parameters.RaceTune });
    OutSwitches.push_back({ "Line Lock", "Burnout assist", &Parameters.LineLockEnabled });
}

void VehicleInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    if (!TyreChoicesReady)
    {
        for (int Corner = 0; Corner < 4; ++Corner)
            TyreCornerChoice[Corner] = static_cast<int>(Parameters.Tyres[Corner].Carcass);
        TyreChoicesReady = true;
    }

    InspectorControls::ConstructSegmentedRow(Palette, Faces, "##VehiclePane", "Section", ActivePane, kPaneNames, 3);

    switch (ActivePane)
    {
    case 0:  ConstructPowertrainPane();  break;
    case 1:  ConstructChassisTyresPane(); break;
    default: ConstructPhysicsLivePane(); break;
    }
}

void VehicleInspector::ConstructPowertrainPane()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;

    // 📝 Induction family: the demo's stock first-order turbo stays bit-exact while StockTurboLaw is on; every other
    //    arrangement — and Turbo with the stock law off — runs the unified InductionCharger module.
    InspectorControls::ConstructSectionDivider(Palette, Faces, "ENGINE & INDUCTION", "GT-R 18-pt curve");
    Changed |= InspectorControls::ConstructSegmentedRow(Palette, Faces, "##Induction", "Induction", Parameters.Induction,
                                                        kInductionNames, 5);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##IdleRpm", "Idle", Parameters.EngineIdleRpm, 300.0f, 2500.0f, "%.0f rpm");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Redline", "Redline", Parameters.EngineRedlineRpm, 4000.0f, 11000.0f, "%.0f rpm");

    InspectorControls::ConstructSectionDivider(Palette, Faces, "TURBOCHARGER", "tau = spool");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Wastegate", "Wastegate Boost", Parameters.TurboWastegateBar, 0.1f, 2.5f, "%.2f bar");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Spool", "Spool Lag", Parameters.TurboSpoolSeconds, 0.05f, 1.5f, "%.2f s");

    // 📝 Supercharger block: belt drive ratio, tune ceilings, bypass valve slew and the bypassed parasitic floor.
    //    The parasitic tax is subtracted from crank torque at throttle; the boost builds rpm-linearly (Roots/TwinScrew)
    //    or on the steeper centrifugal slope.
    InspectorControls::ConstructSectionDivider(Palette, Faces, "SUPERCHARGER", "GRIT preset-1");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DriveRatio", "Drive Ratio", Parameters.SuperchargerDriveRatio, 1.5f, 5.0f, "%.2f x crank");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ScBaseBoost", "Base Boost Ceiling", Parameters.SuperchargerMaxBoostBar, 0.1f, 1.5f, "%.2f bar");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ScRaceBoost", "Race Boost Ceiling", Parameters.RaceTuneBoostBar, 0.1f, 2.0f, "%.2f bar");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##BypassRate", "Bypass Valve Rate", Parameters.BypassRateBarPerSecond, 0.5f, 20.0f, "%.1f bar/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Parasitic", "Parasitic Loss (bypassed)", Parameters.ParasiticLossPercent, 0.0f, 30.0f, "%.1f %%");

    InspectorControls::ConstructSectionDivider(Palette, Faces, "TRANSMISSION", "6-spd AMT");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FinalDrive", "Final Drive", Parameters.FinalDriveRatio, 2.0f, 6.0f, "%.2f : 1");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Eff", "Drive Efficiency", Parameters.DriveEfficiency, 0.5f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##UpShift", "Upshift", Parameters.UpshiftRpm, 3000.0f, 11000.0f, "%.0f rpm");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DownShift", "Downshift", Parameters.DownshiftRpm, 800.0f, 6000.0f, "%.0f rpm");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Launch", "Launch Slip Target", Parameters.LaunchRpm, 800.0f, 6000.0f, "%.0f rpm");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Clutch", "Clutch Capacity", Parameters.ClutchCapacityNewtonMetre, 200.0f, 3000.0f, "%.0f N*m");

    if (Parameters.UpshiftRpm >= Parameters.EngineRedlineRpm)
    {
        InspectorControls::ConstructMutedNote(Palette, Faces, "Upshift sits at the redline: the torque limiter trims before the shift.");
    }

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void VehicleInspector::ConstructChassisTyresPane()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;

    InspectorControls::ConstructSectionDivider(Palette, Faces, "SPAWN", "z up");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpawnX", "Position X", Parameters.SpawnMetres[0], -500.0f, 500.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpawnY", "Position Y", Parameters.SpawnMetres[1], -500.0f, 500.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpawnZ", "Position Z", Parameters.SpawnMetres[2], 0.0f, 50.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpawnYaw", "Yaw", Parameters.SpawnYawDegrees, -180.0f, 180.0f, "%.1f deg");

    InspectorControls::ConstructSectionDivider(Palette, Faces, "CHASSIS & AERO", "1300 kg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Mass", "Mass", Parameters.MassKilograms, 400.0f, 4000.0f, "%.0f kg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CoM", "CoM Height", Parameters.CentreOfMassHeightMetres, 0.15f, 1.2f, "%.3f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FrontWt", "Front Weight", Parameters.FrontWeightPercent, 25.0f, 75.0f, "%.1f %%");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Lock", "Steering Lock", Parameters.SteeringLockDegrees, 5.0f, 45.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SteerRate", "Steering Rate", Parameters.SteeringRatePerSecond, 0.5f, 15.0f, "%.1f /s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Brake", "Brake Torque / Wheel", Parameters.BrakeTorquePerWheelNewtonMetre, 200.0f, 6000.0f, "%.0f N*m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Handbrake", "Handbrake Torque", Parameters.HandbrakeTorqueNewtonMetre, 200.0f, 8000.0f, "%.0f N*m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ArbF", "Anti-Roll Front", Parameters.AntiRollStiffnessNewtonPerMetre[0], 0.0f, 250000.0f, "%.0f N/m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ArbR", "Anti-Roll Rear", Parameters.AntiRollStiffnessNewtonPerMetre[1], 0.0f, 250000.0f, "%.0f N/m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Drag", "Drag Area Cd*A", Parameters.DragAreaMetreSquared, 0.1f, 2.0f, "%.2f m2");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Lift", "Downforce Area Cl*A", Parameters.LiftAreaMetreSquared, 0.0f, 4.0f, "%.2f m2");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LiftF", "Front Downforce", Parameters.FrontLiftPercent, 0.0f, 100.0f, "%.0f %%");

    // 📝 Per-corner soft-body tyres: each corner picks its carcass category (Spring / XPBD lattice / XPBD projection /
    //    FEM solid), its cold inflation and its starting tread — the full per-tyre set from the prototype inspector.
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        VehicleTyreCornerConfiguration& Tyre = Parameters.Tyres[Corner];
        char Divider[40];
        std::snprintf(Divider, sizeof(Divider), "TYRE · %s", kCornerNames[Corner]);
        InspectorControls::ConstructSectionDivider(Palette, Faces, Divider, kCarcassNames[TyreCornerChoice[Corner]]);

        char Id[40];
        std::snprintf(Id, sizeof(Id), "##Carcass%d", Corner);
        if (InspectorControls::ConstructSegmentedRow(Palette, Faces, Id, "Carcass", TyreCornerChoice[Corner], kCarcassNames, 4))
        {
            Tyre.Carcass = static_cast<TyreCarcassCategory>(std::clamp(TyreCornerChoice[Corner], 0, 3));
            Changed = true;
        }
        std::snprintf(Id, sizeof(Id), "##Pressure%d", Corner);
        Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, Id, "Cold Pressure", Tyre.PressureKpa, 0.0f, 450.0f, "%.0f kPa");
        std::snprintf(Id, sizeof(Id), "##Tread%d", Corner);
        Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, Id, "Tread Remaining", Tyre.TreadRemaining, 0.0f, 1.0f, "%.2f");
    }

    if (InspectorControls::ConstructActionButton(Palette, Faces, "##PitFreshSet", "Pit: Fit Fresh Set"))
    {
        for (int Corner = 0; Corner < 4; ++Corner)
        {
            Parameters.Tyres[Corner].TreadRemaining = 1.0f;
            if (Parameters.Tyres[Corner].PressureKpa < 50.0f) Parameters.Tyres[Corner].PressureKpa = 220.0f;
        }
        Changed = true;
    }

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void VehicleInspector::ConstructPhysicsLivePane()
{
    const InspectorPalette                  Palette = InspectorPalette::Resolve();
    const EditorTypefaces                   Faces   = EditorTypefaces::Resolve();
    const VehicleConfiguration::Measurements& Live  = Parameters.Measured;

    InspectorControls::ConstructSectionDivider(Palette, Faces, "PHYSICS WORLD", "480 Hz substeps");
    InspectorControls::ConstructMutedNote(Palette, Faces,
        "The vehicle substeps internally at 480 Hz inside fixed world slices on the physics thread; its kinematic "
        "proxy (bound hull) exchanges collision Delta-v with adapter bodies. Fractures stay static bound colliders.");

    InspectorControls::ConstructSectionDivider(Palette, Faces, Live.Live ? "TELEMETRY · LIVE" : "TELEMETRY · STANDBY");

    char SpeedBuf[48]{}, RpmBuf[48]{}, GearBuf[16]{}, BoostBuf[48]{};
    std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%.1f km/h", Live.SpeedMetrePerSecond * 3.6f);
    std::snprintf(RpmBuf, sizeof(RpmBuf), "%.0f rpm", Live.EngineRpm);
    std::snprintf(GearBuf, sizeof(GearBuf), "%s", kGearNames[std::clamp(Live.GearIndex, 0, 8)]);
    std::snprintf(BoostBuf, sizeof(BoostBuf), "%.2f bar", Live.BoostBar);
    const std::array<InspectorReadoutCell, 4> Drivetrain = {{
        { "SPEED", SpeedBuf }, { "ENGINE", RpmBuf }, { "GEAR", GearBuf }, { "BOOST", BoostBuf },
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Drivetrain.data(), Drivetrain.size());

    char ChargerBuf[48]{}, ParasiticBuf[48]{}, BurnoutBuf[16]{};
    std::snprintf(ChargerBuf, sizeof(ChargerBuf), "%.0f rpm", Live.ChargerRpm);
    std::snprintf(ParasiticBuf, sizeof(ParasiticBuf), "%.1f N·m", Live.ParasiticDragNewtonMetre);
    std::snprintf(BurnoutBuf, sizeof(BurnoutBuf), "%s", Live.Burnout ? "ON" : "off");
    const std::array<InspectorReadoutCell, 3> Induction = {{
        { "CHARGER", ChargerBuf }, { "PARASITIC", ParasiticBuf }, { "LINE LOCK", BurnoutBuf },
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Induction.data(), Induction.size());

    // 📝 Hot tyre state per corner from the thermal solver: gauge pressure (gas law), tread/carcass/gas temperature,
    //    remaining tread and the grip factor — the readings the physics thread publishes in PhysicsWheelSnapshot.
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        const auto& Wheel = Live.Wheels[Corner];
        char Divider[48];
        std::snprintf(Divider, sizeof(Divider), "TYRE %s · HOT", kCornerNames[Corner]);
        InspectorControls::ConstructSectionDivider(Palette, Faces, Divider);

        char PressureBuf[48]{}, TreadBuf[48]{}, CarcassBuf[48]{}, GasBuf[48]{};
        std::snprintf(PressureBuf, sizeof(PressureBuf), "%.0f kPa", Wheel.PressureKpa);
        std::snprintf(TreadBuf, sizeof(TreadBuf), "%.0f C", Wheel.TreadCelsius);
        std::snprintf(CarcassBuf, sizeof(CarcassBuf), "%.0f C", Wheel.CarcassCelsius);
        std::snprintf(GasBuf, sizeof(GasBuf), "%.0f C", Wheel.GasCelsius);
        const std::array<InspectorReadoutCell, 4> Thermal = {{
            { "PRESSURE", PressureBuf }, { "TREAD", TreadBuf }, { "CARCASS", CarcassBuf }, { "GAS", GasBuf },
        }};
        InspectorControls::ConstructReadoutStrip(Palette, Faces, Thermal.data(), Thermal.size());

        char TreadLeftBuf[48]{}, GripBuf[48]{}, LoadBuf[48]{}, DeflBuf[48]{};
        std::snprintf(TreadLeftBuf, sizeof(TreadLeftBuf), "%.0f %%", Wheel.TreadRemaining * 100.0f);
        std::snprintf(GripBuf, sizeof(GripBuf), "%.2f", Wheel.Grip);
        std::snprintf(LoadBuf, sizeof(LoadBuf), "%.0f N", Wheel.LoadNewton);
        std::snprintf(DeflBuf, sizeof(DeflBuf), "%.1f mm", Wheel.DeflectionMillimetres);
        const std::array<InspectorReadoutCell, 4> Contact = {{
            { "TREAD LEFT", TreadLeftBuf }, { "GRIP", GripBuf }, { "LOAD", LoadBuf }, { "DEFLECTION", DeflBuf },
        }};
        InspectorControls::ConstructReadoutStrip(Palette, Faces, Contact.data(), Contact.size());
    }
}

#elif FRONTIER_EDITOR

void VehicleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void VehicleInspector::ConstructInspector() {}

#endif

} // namespace Frontier
