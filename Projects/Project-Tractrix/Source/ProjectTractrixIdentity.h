//============================================================================================================================================
// 📦 Project-Tractrix/ProjectTractrixIdentity.h — project identity constants for the vehicle showcase
//============================================================================================================================================
//
//    Project-Tractrix is an independent vehicle project opened by Frontier.exe. The name refers to the tractrix curve:
//    the path a towed point traces behind a moving one, the classic model of a trailing wheel's caster geometry.
//
//    These constants identify project-owned behaviour. FrontierHost supplies the window title and all shared facilities.

#pragma once

namespace Frontier::Tractrix {

inline constexpr const char* kProjectName    = "Project-Tractrix";
inline constexpr const char* kWindowTitle    = "Project-Tractrix  |  Vehicle Physics  |  Frontier Engine";
inline constexpr const char* kShowcaseLabel  = "Project-Tractrix  |  Vehicle Physics";

// Phase-0 physics-thread defaults (see Engine/PhysicalDynamics/VehiclePhysicsThread.h). The Jolt world steps at
//    kBaseStepHz; the XPBD tyre sub-steps kTyreSubsteps times inside each of those steps.
inline constexpr double   kBaseStepHz    = 60.0;
inline constexpr unsigned kTyreSubsteps  = 12u;    // ⇒ ~720 Hz effective tyre rate

} // namespace Frontier::Tractrix
