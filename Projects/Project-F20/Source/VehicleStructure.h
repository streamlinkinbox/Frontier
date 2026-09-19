//============================================================================================================================================
// 📦 Project-F20/Source/VehicleStructure.h — Racing Vehicle Physical Parameters and Telemetry Topology
//============================================================================================================================================

#pragma once

#include "../../../DeviceExchange/OrientationClassifier.h"
#include <cstdint>

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                 VEHICLE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct VehicleConfiguration
{
    float                   ChassisMass;                        // [kg] total vehicle chassis mass
    float                   EnginePeakTorque;                   // [N*m] maximum engine torque output
    float                   BrakeTorque;                        // [N*m] braking torque capacity
    float                   SteeringLockAngle;                  // [rad] maximum wheel turn angle
    float                   AerodynamicDownforceCoefficient;    // [N/(m/s)²] aero downforce scalar
    float                   SuspensionRestLength;               // [m] uncompressed spring length
    float                   SuspensionStiffness;                // [N/m] suspension spring constant
    float                   SuspensionDamping;                  // [N*s/m] damper rate
    float                   TireFrictionCoefficient;            // [0..1] peak lateral grip
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VEHICLE TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct VehicleTelemetry
{
    Frontier::Vector3       SpatialLocation;                    // [m] chassis world position
    Frontier::Quaternion    AngularOrientation;                 // [rad] chassis rotation
    Frontier::Vector3       LinearVelocity;                     // [m/s] vehicle linear velocity
    float                   SpeedKilometersPerHour;             // [km/h] forward ground speed
    float                   EngineRevolutionsPerMinute;         // [RPM] engine crankshaft speed
    int32_t                 CurrentGear;                        // [gear] current transmission gear (-1=R, 0=N, 1..6)
    float                   SteeringInput;                      // [-1..1] steering request
    float                   ThrottleInput;                      // [0..1] throttle pedal pressure
    float                   BrakeInput;                         // [0..1] brake pedal pressure
    float                   ChassisDamageRatio;                 // [0..1] XPBD softbody deformation ratio
};

} // namespace Frontier::ProjectF20
