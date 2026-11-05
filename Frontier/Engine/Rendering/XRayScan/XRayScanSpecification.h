/*==========================================================================================================================================
                                                       XRAYSCANSPECIFICATION.H
==========================================================================================================================================*/
// 🧩 The X-ray scan's constants and colour conversions, ported value for value from ScanSequence.js (Experimental/VehicleHmiPrototype),
//    which carries the values of interactive-vehicle-hmi-configurator.zip (Truck.tsx "scan animation state machine" and the scan-mode
//    extras). Coordinates are the car's own: x forward from the middle of the car, heights above the ground it stands on.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Frontier::XRayScan
{

struct ScanSpecificationValues
{
    double Cycle          = 6.6;     // [s]    one full pass: sweep, hold, fade
    double Sweep          = 4.6;     // [s]    the front's travel
    double Hold           = 1.0;     // [s]    everything stays ghost
    double FrontStart     = 3.25;    // [m]    ahead of the nose
    double FrontEnd       = -3.35;   // [m]    behind the tail
    double FrontIdle      = 3.3;     // [m]    uScanX before the first scan
    double RampRate       = 3.0;     // [1/s]  THREE.MathUtils.damp lambda of the toggle ramp
    double RampSettle     = 0.002;   // [-]    the ramp snaps to its target inside this
    double StepLimit      = 0.05;    // [s]    useFrame: dt = Math.min(dtRaw, 0.05)
    double SolidFloor     = 0.02;    // [-]    below this ramp the solid materials are opaque again (wantTransparent)
    double EdgeOpacity    = 0.55;    // [-]    the wireframe's opacity at full amount
    double EdgeMargin     = 0.05;    // [m]    the wireframe lives only past front + margin (clipPlane constant)
    std::array<double, 2> CurtainOpacity = {0.075, 0.30};   // [-]  the two curtain planes at full visibility
    double CurtainIdle    = 0.25;    // [-]    the curtain's share while the front rests (hold and fade)
    double CurtainHeight  = 1.18;    // [m]    the curtain group's height above the ground
    std::string_view GhostColour    = "#a9dcec";   // holoMat uColor
    std::string_view Accent         = "#38bdf8";   // holoMat uAccent, the curtain, the HUD: the drive mode's accent
    std::string_view EdgeColour     = "#8fd8f2";   // SCAN
    uint32_t         ParticleCount  = 130;
    std::array<double, 3> ParticleBox = {7.5, 3.2, 4.2};   // [m]  along the car, up, across
    double ParticleCentre  = 1.35;   // [m]    above the ground
    double ParticleSize    = 2.2;
    double ParticleSpeed   = 0.35;
    double ParticleOpacity = 0.55;
    std::string_view ParticleColour = "#8fd8f2";
};
inline constexpr ScanSpecificationValues ScanSpecification{};

// 📝 The material families the scan names (CourseMaterial, DriveCourse.h): the painted and clad body panels and the tyres carry a wireframe,
//    the lamps stay solid.
inline constexpr uint32_t BodyCoatFamily      = 6;
inline constexpr uint32_t VehiclePlasticFamily = 8;
inline constexpr uint32_t TyreFamily          = 9;
inline constexpr uint32_t HeadlightFamily     = 13;
inline constexpr uint32_t TaillightFamily     = 14;

// 📐 PaintRipple.js ConvertHexToLinear: #RRGGBB through the sRGB EOTF.
[[nodiscard]] std::array<double, 3> ConvertHexToLinear(std::string_view Hex);

// 📐 THREE.ColorManagement's linear → sRGB OETF (colorspace_fragment).
[[nodiscard]] double ConvertLinearToSrgb(double Value);

// 📐 THREE.ACESFilmicToneMapping at exposure 1 (tonemapping_fragment): Stephen Hill's RRT and ODT fit behind the 0.6 prescale. Every built-in
//    material of the reference (the wireframe, the curtain) and drei's Sparkles are mapped by it; holoMat is not.
[[nodiscard]] std::array<double, 3> ResolveAcesFilmic(const std::array<double, 3>& Linear);

// 📝 The colour a tone-mapped material of this hex lands on in the reference's sRGB canvas, as the encoded values it blends with.
[[nodiscard]] std::array<double, 3> ResolveDisplayColour(std::string_view Hex);

// 📐 Truck.tsx easeInOutCubic.
[[nodiscard]] double EaseInOutCubic(double X);

// 📐 THREE.MathUtils.damp.
[[nodiscard]] double DampValue(double Value, double Target, double Lambda, double Seconds);

} // namespace Frontier::XRayScan
