//============================================================================================================================================
//                                                    VEHICLEINSPECTORSEQUENCE.H
//============================================================================================================================================
// 📦 Project-Drive's vehicle inspector — the project-owned exchange around the native Inspector draw, the counterpart
//    to Project-Zero's EditorInspectorSequence / CameraInspectorBinding. The vehicle is one outliner node that
//    expands into a row per subsystem (Chassis, Engine, Turbo, Gearbox, Tyre-long, Tyre-lat, Aero, plus a live
//    read-out), each keyed into the family-6 inspector-key space so it survives moves and renames. Selecting a row
//    builds that subsystem's EditorSheet; committing writes the edits straight back into the live
//    VehicleSolverConfiguration, and the host then calls VehicleInstanceSequence::Reconfigure to rebind the tyres.
//
//    ALL CURVES ARE EDITABLE. The native inspector's widget vocabulary is scalar (Slider/Switch/Select/Readout) —
//    it has no spline canvas — so every curve is exposed as its control set, which is exactly what defines it:
//        • Engine torque curve      → one slider per (rpm → N·m) knot          [Engine]
//        • Turbo boost curve        → one slider per (turbo-rpm → Bar) knot     [Turbo]
//        • Gearbox ratios           → one slider per gear (index → ratio)       [Gearbox]
//        • Pacejka MF slip curves   → the B/C/D/E shape coefficients            [Tyre-long, Tyre-lat]
//        • Aero coefficient curves  → the lift/drag coefficients + wing angles  [Aero]
//    Editing a knot's Y (or a coefficient) reshapes the curve; knot X positions are held. That is the curve editor.

#pragma once

#include "../../../Engine/Editor/EditorInstance.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Frontier {
namespace Drive {

enum class VehicleSection : uint32_t
{
    Live = 1u,   // live telemetry (read-only)
    Chassis,     // mass, grip, steering, forces
    Engine,      // torque curve + limits
    Turbo,       // boost curve + wastegate
    Gearbox,     // gear ratios + final drive
    TyreLong,    // Pacejka longitudinal shape
    TyreLat,     // Pacejka lateral shape
    Aero,        // wing / splitter / canard coefficients
    Count
};

// Family-6 key space (celestial=2, camera=1, wind=4 are taken in Project-Zero; 6 is ours).
[[nodiscard]] inline uint64_t VehicleInspectorKey(VehicleSection S) noexcept
{ return (uint64_t(6) << 32) | uint32_t(S); }

[[nodiscard]] inline const char* VehicleSectionLabel(VehicleSection S) noexcept
{
    switch (S)
    {
    case VehicleSection::Live:     return "Telemetry";
    case VehicleSection::Chassis:  return "Chassis";
    case VehicleSection::Engine:   return "Engine";
    case VehicleSection::Turbo:    return "Turbo";
    case VehicleSection::Gearbox:  return "Gearbox";
    case VehicleSection::TyreLong: return "Tyre — longitudinal";
    case VehicleSection::TyreLat:  return "Tyre — lateral";
    case VehicleSection::Aero:     return "Aero";
    default:                       return "Vehicle";
    }
}

//------------------------------------------------------------------------------------------------------------------------ small sheet helpers (same shape as CameraInspectorBinding)
namespace detail {

inline EditorPropertyGroup& NewGroup(EditorSheet& Sheet, const char* Title) noexcept
{
    auto& G = Sheet.Groups[Sheet.GroupCount++];
    G = EditorPropertyGroup{};
    std::snprintf(G.Title, sizeof(G.Title), "%s", Title);
    return G;
}
inline void Slider(EditorPropertyGroup& G, const char* Label, float V, float Lo, float Hi,
                   const char* Unit, uint32_t Decimals = 2u) noexcept
{
    if (G.PropertyCount >= kMaxEditorGroupProps) return;
    auto& P = G.Properties[G.PropertyCount++];
    P = EditorProperty{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Slider;
    P.Figure = V; P.Minimum = Lo; P.Maximum = Hi; P.Decimals = Decimals;
    std::snprintf(P.Unit, sizeof(P.Unit), "%s", Unit);
}
inline void Switch(EditorPropertyGroup& G, const char* Label, bool On) noexcept
{
    if (G.PropertyCount >= kMaxEditorGroupProps) return;
    auto& P = G.Properties[G.PropertyCount++];
    P = EditorProperty{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Switch; P.On = On;
}
inline void Readout(EditorPropertyGroup& G, const char* Label, const char* Text) noexcept
{
    if (G.PropertyCount >= kMaxEditorGroupProps) return;
    auto& P = G.Properties[G.PropertyCount++];
    P = EditorProperty{};
    std::snprintf(P.Label, sizeof(P.Label), "%s", Label);
    P.Category = EditorPropertyCategory::Readout;
    std::snprintf(P.Text, sizeof(P.Text), "%s", Text);
}
// Find a slider Figure (or switch On) by label; returns Old if absent or non-finite.
inline float ReadSlider(const EditorSheet& Sheet, const char* Label, float Old, float Lo, float Hi) noexcept
{
    for (uint32_t g = 0; g < Sheet.GroupCount; ++g)
        for (uint32_t p = 0; p < Sheet.Groups[g].PropertyCount; ++p)
        {
            const auto& P = Sheet.Groups[g].Properties[p];
            if (!std::strcmp(P.Label, Label))
                return std::isfinite(P.Figure) ? (P.Figure < Lo ? Lo : (P.Figure > Hi ? Hi : P.Figure)) : Old;
        }
    return Old;
}
inline bool ReadSwitch(const EditorSheet& Sheet, const char* Label, bool Old) noexcept
{
    for (uint32_t g = 0; g < Sheet.GroupCount; ++g)
        for (uint32_t p = 0; p < Sheet.Groups[g].PropertyCount; ++p)
            if (!std::strcmp(Sheet.Groups[g].Properties[p].Label, Label))
                return Sheet.Groups[g].Properties[p].On;
    return Old;
}

} // namespace detail

//------------------------------------------------------------------------------------------------------------------------ BUILD one subsystem's sheet
inline void BuildVehicleSheet(VehicleSection S,
                              const Frontier::Vehicle::VehicleSolverConfiguration& C,
                              const Frontier::Vehicle::VehicleTelemetry& Tel,
                              EditorSheet& Sheet) noexcept
{
    using namespace detail;
    Sheet.Appearance = EditorSheetAppearance::Generic;
    Sheet.GroupCount = 0;
    Sheet.InspectorKey = VehicleInspectorKey(S);
    char buf[48];

    switch (S)
    {
    case VehicleSection::Live:
    {
        auto& G = NewGroup(Sheet, "Live telemetry");
        std::snprintf(buf, sizeof(buf), "%.1f m/s  (%.0f km/h)", Tel.SpeedMetresPerSecond, Tel.SpeedMetresPerSecond*3.6f);
        Readout(G, "Speed", buf);
        std::snprintf(buf, sizeof(buf), "%d", Tel.GearIndex); Readout(G, "Gear index", buf);
        std::snprintf(buf, sizeof(buf), "%.0f rpm", Tel.EngineRPM); Readout(G, "Engine", buf);
        std::snprintf(buf, sizeof(buf), "%.2f Bar", Tel.BoostBar);  Readout(G, "Boost", buf);
        std::snprintf(buf, sizeof(buf), "%.0f N", Tel.TotalVerticalLoad); Readout(G, "Total load", buf);
        auto& W = NewGroup(Sheet, "Wheels (load / slip)");
        for (uint32_t i = 0; i < 4 && i < Tel.Wheels.size(); ++i)
        {
            char lbl[28]; std::snprintf(lbl, sizeof(lbl), "Wheel %u", i);
            std::snprintf(buf, sizeof(buf), "%.0f N  k=%.2f  a=%.1f deg",
                          Tel.Wheels[i].VerticalLoad, Tel.Wheels[i].SlipRatio, Tel.Wheels[i].SlipAngleRad*57.2958f);
            Readout(W, lbl, buf);
        }
        break;
    }
    case VehicleSection::Chassis:
    {
        auto& G = NewGroup(Sheet, "Chassis & grip");
        Slider(G, "Mass",                C.ChassisMass,        600, 2500, "kg", 0);
        Slider(G, "Grip coefficient",    C.GripCoefficient,    0.5f, 2.0f, "");
        Slider(G, "Cornering stiffness", C.CorneringStiffness, 5000, 80000, "N", 0);
        Slider(G, "Suspension damping",  C.SuspensionDamping,  0, 20000, "Ns/m", 0);
        Slider(G, "Rolling resistance",  C.RollingResistance,  0, 0.05f, "", 3);
        auto& D = NewGroup(Sheet, "Driving forces");
        Slider(D, "Drive force / wheel", C.DriveForcePerWheel, 0, 15000, "N", 0);
        Slider(D, "Brake force / wheel", C.BrakeForcePerWheel, 0, 20000, "N", 0);
        Slider(D, "Handbrake force",     C.HandbrakeForce,     0, 30000, "N", 0);
        Slider(D, "Max steer angle",     C.MaxSteerAngleRad*57.2958f, 5, 45, "deg", 0);
        Slider(D, "Steer rate",          C.SteerRatePerSecond, 1, 20, "1/s");
        break;
    }
    case VehicleSection::Engine:
    {
        auto& T = NewGroup(Sheet, "Torque curve (rpm -> Nm)");
        const auto& tc = C.Engine.TorqueCurve;
        for (size_t k = 0; k < tc.X.size() && T.PropertyCount < kMaxEditorGroupProps; ++k)
        {
            char lbl[28]; std::snprintf(lbl, sizeof(lbl), "%.0f rpm", tc.X[k]);
            Slider(T, lbl, tc.Y[k], 0, 1500, "Nm", 0);
        }
        auto& L = NewGroup(Sheet, "Engine limits");
        Slider(L, "Idle",           C.Engine.IdleRPM,    500, 1500, "rpm", 0);
        Slider(L, "Redline",        C.Engine.RedlineRPM, 4000, 9500, "rpm", 0);
        Slider(L, "Inertia",        C.Engine.EngineInertia, 0.1f, 1.0f, "kgm2");
        Slider(L, "Braking coeff",  C.Engine.EngineBrakingCoeff, 0, 0.3f, "", 3);
        Slider(L, "Upshift",        C.UpshiftRPM,   4000, 9000, "rpm", 0);
        Slider(L, "Downshift",      C.DownshiftRPM, 1000, 5000, "rpm", 0);
        break;
    }
    case VehicleSection::Turbo:
    {
        auto& B = NewGroup(Sheet, "Boost curve (rpm -> Bar)");
        const auto& bc = C.Turbo.BoostPressureCurve;
        for (size_t k = 0; k < bc.X.size() && B.PropertyCount < kMaxEditorGroupProps; ++k)
        {
            char lbl[28]; std::snprintf(lbl, sizeof(lbl), "%.0f rpm", bc.X[k]);
            Slider(B, lbl, bc.Y[k], 0, 3.0f, "Bar");
        }
        auto& W = NewGroup(Sheet, "Turbo limits");
        Slider(W, "Wastegate max",  C.Turbo.WastegateMaxBoost, 0.2f, 2.5f, "Bar");
        Slider(W, "Turbine eff",    C.Turbo.TurbineEfficiency, 0.3f, 0.95f, "");
        Slider(W, "Compressor eff", C.Turbo.CompressorEfficiency, 0.3f, 0.95f, "");
        break;
    }
    case VehicleSection::Gearbox:
    {
        auto& G = NewGroup(Sheet, "Gear ratios");
        const auto& gr = C.Transmission.GearRatios;
        for (size_t k = 0; k < gr.size() && G.PropertyCount < kMaxEditorGroupProps; ++k)
        {
            char lbl[28];
            if      (gr[k] < 0)  std::snprintf(lbl, sizeof(lbl), "Reverse %u", (unsigned)(k+1));
            else if (gr[k]==0.0f)std::snprintf(lbl, sizeof(lbl), "Neutral");
            else                 std::snprintf(lbl, sizeof(lbl), "Gear %u", (unsigned)(k-1));   // idx 3 = 1st
            Slider(G, lbl, gr[k], -5.0f, 6.0f, ":1");
        }
        auto& F = NewGroup(Sheet, "Final drive");
        Slider(F, "Final drive", C.Transmission.FinalDriveRatio, 2.0f, 6.0f, ":1");
        Slider(F, "Shift dwell", C.ShiftCooldownSeconds, 0.05f, 1.0f, "s");
        break;
    }
    case VehicleSection::TyreLong:
    {
        auto& P = NewGroup(Sheet, "Pacejka longitudinal (B/C/D/E)");
        Slider(P, "B stiffness pBx1", C.TyrePacejka.pBx1, 4, 30, "");
        Slider(P, "B load    pBx2",   C.TyrePacejka.pBx2, 0, 40, "");
        Slider(P, "C shape   pCx1",   C.TyrePacejka.pCx1, 1.0f, 2.2f, "");
        Slider(P, "D peak    pDx1",   C.TyrePacejka.pDx1, 0.5f, 2.5f, "");
        Slider(P, "D load    pDx2",   C.TyrePacejka.pDx2, -0.2f, 0.2f, "", 3);
        Slider(P, "E curve   pEx1",   C.TyrePacejka.pEx1, -1.0f, 1.0f, "", 3);
        auto& S2 = NewGroup(Sheet, "Longitudinal scaling");
        Slider(S2, "Force scale Lx", C.TyrePacejka.Lx, 0.5f, 2.0f, "");
        Slider(S2, "Ref load Fz0",   C.TyrePacejka.Fz0, 2.0f, 8.0f, "kN");
        break;
    }
    case VehicleSection::TyreLat:
    {
        auto& P = NewGroup(Sheet, "Pacejka lateral (B/C/D/E)");
        Slider(P, "B stiffness pBy1", C.TyrePacejka.pBy1, 4, 30, "");
        Slider(P, "B load    pBy2",   C.TyrePacejka.pBy2, 0, 30, "");
        Slider(P, "C shape   pCy1",   C.TyrePacejka.pCy1, 1.0f, 2.2f, "");
        Slider(P, "D peak    pDy1",   C.TyrePacejka.pDy1, 0.5f, 2.5f, "");
        Slider(P, "D load    pDy2",   C.TyrePacejka.pDy2, -0.2f, 0.2f, "", 3);
        Slider(P, "E curve   pEy1",   C.TyrePacejka.pEy1, -2.0f, 1.0f, "", 3);
        auto& S2 = NewGroup(Sheet, "Lateral scaling + camber");
        Slider(S2, "Force scale Ly", C.TyrePacejka.Ly, 0.5f, 2.0f, "");
        Slider(S2, "Camber pCamber1", C.TyrePacejka.pCamber1, -0.5f, 0.5f, "", 3);
        break;
    }
    case VehicleSection::Aero:
    {
        auto& W = NewGroup(Sheet, "Rear wing");
        Slider(W, "Wing area",     C.Aero.RearWing.Area_m2,        0.2f, 3.0f, "m2");
        Slider(W, "Wing Cl",       C.Aero.RearWing.BaseCoeffLift,  -6.0f, 0.0f, "");
        Slider(W, "Wing Cd",       C.Aero.RearWing.BaseCoeffDrag,   0.1f, 1.5f, "");
        Slider(W, "Wing angle",    C.Aero.RearWing.CurrentAngle_deg, C.Aero.RearWing.MinAngle_deg, C.Aero.RearWing.MaxAngle_deg, "deg", 0);
        auto& Sp = NewGroup(Sheet, "Front splitter");
        Slider(Sp, "Splitter area", C.Aero.FrontSplitter.Area_m2,     0.2f, 2.0f, "m2");
        Slider(Sp, "Splitter Cp",   C.Aero.FrontSplitter.CoeffPressure, 0.1f, 2.0f, "");
        auto& Cn = NewGroup(Sheet, "Canards");
        if (!C.Aero.Canards.empty())
        {
            Slider(Cn, "Canard area",   C.Aero.Canards[0].Area_m2,       0.05f, 1.0f, "m2");
            Slider(Cn, "Canard Cl",     C.Aero.Canards[0].BaseCoeffLift, -4.0f, 0.0f, "");
        }
        Switch(Cn, "Aero enabled", C.Aero.Enabled);
        break;
    }
    default: break;
    }
}

//------------------------------------------------------------------------------------------------------------------------ APPLY the committed sheet back into the live config
inline void ApplyVehicleSheet(VehicleSection S, const EditorSheet& Sheet,
                              Frontier::Vehicle::VehicleSolverConfiguration& C) noexcept
{
    using namespace detail;
    char lbl[28];
    switch (S)
    {
    case VehicleSection::Chassis:
        C.ChassisMass        = ReadSlider(Sheet, "Mass",                C.ChassisMass, 600, 2500);
        C.GripCoefficient    = ReadSlider(Sheet, "Grip coefficient",    C.GripCoefficient, 0.5f, 2.0f);
        C.CorneringStiffness = ReadSlider(Sheet, "Cornering stiffness", C.CorneringStiffness, 5000, 80000);
        C.SuspensionDamping  = ReadSlider(Sheet, "Suspension damping",  C.SuspensionDamping, 0, 20000);
        C.RollingResistance  = ReadSlider(Sheet, "Rolling resistance",  C.RollingResistance, 0, 0.05f);
        C.DriveForcePerWheel = ReadSlider(Sheet, "Drive force / wheel", C.DriveForcePerWheel, 0, 15000);
        C.BrakeForcePerWheel = ReadSlider(Sheet, "Brake force / wheel", C.BrakeForcePerWheel, 0, 20000);
        C.HandbrakeForce     = ReadSlider(Sheet, "Handbrake force",     C.HandbrakeForce, 0, 30000);
        C.MaxSteerAngleRad   = ReadSlider(Sheet, "Max steer angle",     C.MaxSteerAngleRad*57.2958f, 5, 45) / 57.2958f;
        C.SteerRatePerSecond = ReadSlider(Sheet, "Steer rate",          C.SteerRatePerSecond, 1, 20);
        break;
    case VehicleSection::Engine:
    {
        auto& tc = C.Engine.TorqueCurve;
        for (size_t k = 0; k < tc.X.size(); ++k)
        { std::snprintf(lbl, sizeof(lbl), "%.0f rpm", tc.X[k]); tc.Y[k] = ReadSlider(Sheet, lbl, tc.Y[k], 0, 1500); }
        C.Engine.IdleRPM            = ReadSlider(Sheet, "Idle",    C.Engine.IdleRPM, 500, 1500);
        C.Engine.RedlineRPM         = ReadSlider(Sheet, "Redline", C.Engine.RedlineRPM, 4000, 9500);
        C.Engine.EngineInertia      = ReadSlider(Sheet, "Inertia", C.Engine.EngineInertia, 0.1f, 1.0f);
        C.Engine.EngineBrakingCoeff = ReadSlider(Sheet, "Braking coeff", C.Engine.EngineBrakingCoeff, 0, 0.3f);
        C.UpshiftRPM                = ReadSlider(Sheet, "Upshift",   C.UpshiftRPM, 4000, 9000);
        C.DownshiftRPM              = ReadSlider(Sheet, "Downshift", C.DownshiftRPM, 1000, 5000);
        break;
    }
    case VehicleSection::Turbo:
    {
        auto& bc = C.Turbo.BoostPressureCurve;
        for (size_t k = 0; k < bc.X.size(); ++k)
        { std::snprintf(lbl, sizeof(lbl), "%.0f rpm", bc.X[k]); bc.Y[k] = ReadSlider(Sheet, lbl, bc.Y[k], 0, 3.0f); }
        C.Turbo.WastegateMaxBoost    = ReadSlider(Sheet, "Wastegate max",  C.Turbo.WastegateMaxBoost, 0.2f, 2.5f);
        C.Turbo.TurbineEfficiency    = ReadSlider(Sheet, "Turbine eff",    C.Turbo.TurbineEfficiency, 0.3f, 0.95f);
        C.Turbo.CompressorEfficiency = ReadSlider(Sheet, "Compressor eff", C.Turbo.CompressorEfficiency, 0.3f, 0.95f);
        break;
    }
    case VehicleSection::Gearbox:
    {
        auto& gr = C.Transmission.GearRatios;
        for (size_t k = 0; k < gr.size(); ++k)
        {
            if      (gr[k] < 0)   std::snprintf(lbl, sizeof(lbl), "Reverse %u", (unsigned)(k+1));
            else if (gr[k]==0.0f) std::snprintf(lbl, sizeof(lbl), "Neutral");
            else                  std::snprintf(lbl, sizeof(lbl), "Gear %u", (unsigned)(k-1));
            gr[k] = ReadSlider(Sheet, lbl, gr[k], -5.0f, 6.0f);
        }
        C.Transmission.FinalDriveRatio = ReadSlider(Sheet, "Final drive", C.Transmission.FinalDriveRatio, 2.0f, 6.0f);
        C.ShiftCooldownSeconds         = ReadSlider(Sheet, "Shift dwell", C.ShiftCooldownSeconds, 0.05f, 1.0f);
        break;
    }
    case VehicleSection::TyreLong:
        C.TyrePacejka.pBx1 = ReadSlider(Sheet, "B stiffness pBx1", C.TyrePacejka.pBx1, 4, 30);
        C.TyrePacejka.pBx2 = ReadSlider(Sheet, "B load    pBx2",   C.TyrePacejka.pBx2, 0, 40);
        C.TyrePacejka.pCx1 = ReadSlider(Sheet, "C shape   pCx1",   C.TyrePacejka.pCx1, 1.0f, 2.2f);
        C.TyrePacejka.pDx1 = ReadSlider(Sheet, "D peak    pDx1",   C.TyrePacejka.pDx1, 0.5f, 2.5f);
        C.TyrePacejka.pDx2 = ReadSlider(Sheet, "D load    pDx2",   C.TyrePacejka.pDx2, -0.2f, 0.2f);
        C.TyrePacejka.pEx1 = ReadSlider(Sheet, "E curve   pEx1",   C.TyrePacejka.pEx1, -1.0f, 1.0f);
        C.TyrePacejka.Lx   = ReadSlider(Sheet, "Force scale Lx",   C.TyrePacejka.Lx, 0.5f, 2.0f);
        C.TyrePacejka.Fz0  = ReadSlider(Sheet, "Ref load Fz0",     C.TyrePacejka.Fz0, 2.0f, 8.0f);
        break;
    case VehicleSection::TyreLat:
        C.TyrePacejka.pBy1     = ReadSlider(Sheet, "B stiffness pBy1", C.TyrePacejka.pBy1, 4, 30);
        C.TyrePacejka.pBy2     = ReadSlider(Sheet, "B load    pBy2",   C.TyrePacejka.pBy2, 0, 30);
        C.TyrePacejka.pCy1     = ReadSlider(Sheet, "C shape   pCy1",   C.TyrePacejka.pCy1, 1.0f, 2.2f);
        C.TyrePacejka.pDy1     = ReadSlider(Sheet, "D peak    pDy1",   C.TyrePacejka.pDy1, 0.5f, 2.5f);
        C.TyrePacejka.pDy2     = ReadSlider(Sheet, "D load    pDy2",   C.TyrePacejka.pDy2, -0.2f, 0.2f);
        C.TyrePacejka.pEy1     = ReadSlider(Sheet, "E curve   pEy1",   C.TyrePacejka.pEy1, -2.0f, 1.0f);
        C.TyrePacejka.Ly       = ReadSlider(Sheet, "Force scale Ly",   C.TyrePacejka.Ly, 0.5f, 2.0f);
        C.TyrePacejka.pCamber1 = ReadSlider(Sheet, "Camber pCamber1",  C.TyrePacejka.pCamber1, -0.5f, 0.5f);
        break;
    case VehicleSection::Aero:
        C.Aero.RearWing.Area_m2          = ReadSlider(Sheet, "Wing area",     C.Aero.RearWing.Area_m2, 0.2f, 3.0f);
        C.Aero.RearWing.BaseCoeffLift    = ReadSlider(Sheet, "Wing Cl",       C.Aero.RearWing.BaseCoeffLift, -6.0f, 0.0f);
        C.Aero.RearWing.BaseCoeffDrag    = ReadSlider(Sheet, "Wing Cd",       C.Aero.RearWing.BaseCoeffDrag, 0.1f, 1.5f);
        C.Aero.RearWing.CurrentAngle_deg = ReadSlider(Sheet, "Wing angle",    C.Aero.RearWing.CurrentAngle_deg, C.Aero.RearWing.MinAngle_deg, C.Aero.RearWing.MaxAngle_deg);
        C.Aero.FrontSplitter.Area_m2       = ReadSlider(Sheet, "Splitter area", C.Aero.FrontSplitter.Area_m2, 0.2f, 2.0f);
        C.Aero.FrontSplitter.CoeffPressure = ReadSlider(Sheet, "Splitter Cp",   C.Aero.FrontSplitter.CoeffPressure, 0.1f, 2.0f);
        if (!C.Aero.Canards.empty())
        {
            C.Aero.Canards[0].Area_m2       = ReadSlider(Sheet, "Canard area", C.Aero.Canards[0].Area_m2, 0.05f, 1.0f);
            C.Aero.Canards[0].BaseCoeffLift = ReadSlider(Sheet, "Canard Cl",   C.Aero.Canards[0].BaseCoeffLift, -4.0f, 0.0f);
        }
        C.Aero.Enabled              = ReadSwitch(Sheet, "Aero enabled",  C.Aero.Enabled);
        break;
    default: break;
    }
}

} // namespace Drive
} // namespace Frontier
