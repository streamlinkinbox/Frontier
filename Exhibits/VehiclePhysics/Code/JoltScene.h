//============================================================================================================================
// 🧩 VehiclePhysics exhibit — JoltScene.h
//============================================================================================================================
// 📝 The Jolt side of the proving ground, mirroring the demonstration's JoltWorld.js architecture with Jolt Physics
//    C++ 5.2.0: Jolt owns the loose bodies (cones, crates) and statics (ground, wall); the car stays Frontier's own
//    VehicleSolver, represented in Jolt by a dynamic convex-hull proxy (the demo's fallback hull points, vehicle mass,
//    zero gravity factor and damping). Every tick the proxy is posed and velocated from the car, Jolt steps, and only
//    the collision-induced velocity change is applied back to the car — chassis travel and gravity are never
//    double-integrated. World frame matches the vehicle solver (X forward/east, Y left/north, Z up): Jolt is given
//    gravity (0, 0, −9.81) and used Z-up throughout, its Y-axis cylinder rotated into place for the cones.
//============================================================================================================================

#pragma once

#include <string>
#include <vector>

#include "Engine/Physics/VehicleSolver.h"

struct JoltSceneResult
{
    int ConeCount = 0;                 // [-]
    int ConesDisplaced = 0;            // [-]   moved > 0.4 m or tipped
    double TotalVelocityExchange = 0.0;  // [m/s] Σ|Δv| applied to the car by Jolt collisions (cone phase only)
    double WallImpactSpeedBefore = 0.0;  // [m/s]
    double WallSpeedAfter = 0.0;         // [m/s] 0.5 s after first wall contact exchange
    double WallPenetration = 0.0;        // [m]   deepest proxy-nose excursion past the wall face
    bool FiniteThroughout = true;        // [-]
};

// 📝 Runs the cone-row drive-through and the wall impact against a fresh VehicleSolver, writing the top-down scene
//    trace (car path, cone start/final poses, wall) to TraceCsvPath.
JoltSceneResult RunJoltScene(const std::string& TraceCsvPath);
