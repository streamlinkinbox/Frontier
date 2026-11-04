/*==========================================================================================================================================
                                                SPRING ONE-STEP VALIDATION
==========================================================================================================================================*/
// 🧩 Compare native one-step outputs against synchronized states from the unchanged JavaScript hard-drop.

#include "Engine/Physics/VehicleSolver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace
{

struct SpringSnapshot
{
    double Position[3], Velocity[3], Orientation[4], Angular[3];
    double Steer, Gear, Boost, Rpm;
    double Wheels[4][16];

    bool Read(std::istream& Input)
    {
        for (double& Component : Position) Input >> Component;
        for (double& Component : Velocity) Input >> Component;
        for (double& Component : Orientation) Input >> Component;
        for (double& Component : Angular) Input >> Component;
        Input >> Steer >> Gear >> Boost >> Rpm;
        for (auto& Wheel : Wheels) for (double& Component : Wheel) Input >> Component;
        return static_cast<bool>(Input);
    }
};

} // namespace

int main(int Count, char** Arguments)
{
    using namespace Frontier;
    if (Count != 2) return 2;
    std::ifstream Input(Arguments[1]);
    if (!Input) return 2;
    VehicleSolver Vehicle;
    Vehicle.AssignCarcassModel(CarcassSpringModel);
    const double Spawn[3] = {-100.0, -34.0, 1.2};
    Vehicle.Respawn(Spawn, 0.0);
    double PositionError = 0, VelocityError = 0, AngularError = 0, OrientationError = 0;
    double ForceError = 0, LoadError = 0, LengthError = 0, TyreDeflectionError = 0, SlipError = 0;
    int ForceStep = 0, LoadStep = 0, ContactMismatch = 0, PinchMismatch = 0, TopMismatch = 0, ShredMismatch = 0;
    bool Finite = true;
    for (int Step = 0; Step < 720; ++Step)
    {
        int SampleIndex = -1; SpringSnapshot Before{}, After{};
        Input >> SampleIndex;
        if (SampleIndex != Step || !Before.Read(Input) || !After.Read(Input)) return 2;
        // Restore public dynamic inputs from the JS trajectory before every step; do not restore its outputs.
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Vehicle.Position[Axis] = Before.Position[Axis];
            Vehicle.LinearVelocity[Axis] = Before.Velocity[Axis];
            Vehicle.AngularVelocity[Axis] = Before.Angular[Axis];
        }
        for (int Axis = 0; Axis < 4; ++Axis) Vehicle.Orientation[Axis] = Before.Orientation[Axis];
        Vehicle.SteerAngle = Before.Steer; Vehicle.GearIndex = static_cast<int>(Before.Gear);
        Vehicle.Boost = Before.Boost; Vehicle.EngineRPM = Before.Rpm;
        for (int Index = 0; Index < 4; ++Index)
        {
            auto& Wheel = Vehicle.Wheels[Index]; const double* Fields = Before.Wheels[Index];
            Wheel.Length = Fields[0]; Wheel.Compression = Fields[1]; Wheel.Deflection = Fields[2];
            Wheel.Spin = Fields[3]; Wheel.SpinAngle = Fields[4]; Wheel.SlipRatio = Fields[5]; Wheel.SlipAngle = Fields[6];
            Wheel.TyreDeflection = Fields[7]; Wheel.DeflectionRate = Fields[8]; Wheel.Radius = Fields[9];
            Wheel.Contact = Fields[10] != 0; Wheel.Pinched = Fields[11] != 0; Wheel.ToppedOut = Fields[12] != 0;
            Wheel.Shredded = Fields[13] != 0; Wheel.TyreForce = Fields[14]; Wheel.Load = Fields[15];
        }
        const double Pressure[4] = {30000,30000,30000,30000}, Grip[4] = {1,1,1,1};
        Vehicle.AssignTyrePressures(Pressure); Vehicle.AssignGrip(Grip);
        Vehicle.Advance(VehicleCommand{}, 1.0/480.0);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            PositionError = std::max(PositionError, std::abs(Vehicle.Position[Axis]-After.Position[Axis]));
            VelocityError = std::max(VelocityError, std::abs(Vehicle.LinearVelocity[Axis]-After.Velocity[Axis]));
            AngularError = std::max(AngularError, std::abs(Vehicle.AngularVelocity[Axis]-After.Angular[Axis]));
            Finite &= std::isfinite(Vehicle.Position[Axis]) && std::isfinite(Vehicle.LinearVelocity[Axis]) &&
                      std::isfinite(Vehicle.AngularVelocity[Axis]);
        }
        for (int Axis = 0; Axis < 4; ++Axis)
        {
            OrientationError = std::max(OrientationError, std::abs(Vehicle.Orientation[Axis]-After.Orientation[Axis]));
            Finite &= std::isfinite(Vehicle.Orientation[Axis]);
        }
        for (int Index = 0; Index < 4; ++Index)
        {
            const auto& Wheel = Vehicle.Wheels[Index]; const double* Fields = After.Wheels[Index];
            const double ForceDifference = std::abs(Wheel.TyreForce-Fields[14]);
            const double LoadDifference = std::abs(Wheel.Load-Fields[15]);
            Finite &= std::isfinite(Wheel.TyreForce) && std::isfinite(Wheel.Load) && std::isfinite(Wheel.Length) &&
                      std::isfinite(Wheel.TyreDeflection) && std::isfinite(Wheel.Spin) && std::isfinite(Wheel.SlipRatio) &&
                      std::isfinite(Wheel.SlipAngle);
            if (ForceDifference > ForceError) { ForceError = ForceDifference; ForceStep = Step; }
            if (LoadDifference > LoadError) { LoadError = LoadDifference; LoadStep = Step; }
            LengthError = std::max(LengthError, std::abs(Wheel.Length-Fields[0]));
            TyreDeflectionError = std::max(TyreDeflectionError, std::abs(Wheel.TyreDeflection-Fields[7]));
            SlipError = std::max({SlipError, std::abs(Wheel.Spin-Fields[3]), std::abs(Wheel.SlipRatio-Fields[5]),
                                  std::abs(Wheel.SlipAngle-Fields[6])});
            ContactMismatch += Wheel.Contact != (Fields[10] != 0);
            PinchMismatch += Wheel.Pinched != (Fields[11] != 0);
            TopMismatch += Wheel.ToppedOut != (Fields[12] != 0);
            ShredMismatch += Wheel.Shredded != (Fields[13] != 0);
        }
    }
    const bool Success = Finite && PositionError < 1e-12 && VelocityError < 1e-12 && AngularError < 1e-12 &&
                         OrientationError < 1e-12 && ForceError < 1e-6 && LoadError < 1e-6 && LengthError < 1e-12 &&
                         TyreDeflectionError < 1e-12 && SlipError < 1e-10 && !ContactMismatch && !PinchMismatch &&
                         !TopMismatch && !ShredMismatch;
    std::printf("%s synchronized one-step comparison at 720 JS states, 4 wheels, hard drop\n", Success ? "PASS" : "FAIL");
    std::printf("max errors: position %.9g m, velocity %.9g m/s, angular %.9g rad/s, quaternion %.9g\n",
                PositionError, VelocityError, AngularError, OrientationError);
    std::printf("max errors: tyre force %.9g N at step %d, load %.9g N at step %d, length %.9g m, deflection %.9g m, spin/slip %.9g\n",
                ForceError, ForceStep, LoadError, LoadStep, LengthError, TyreDeflectionError, SlipError);
    std::printf("contact/pinch/top-out/shred mismatches %d/%d/%d/%d\n", ContactMismatch, PinchMismatch, TopMismatch, ShredMismatch);
    std::printf("scope: same public state and prescribed pressure/grip at each step; zero input, no debris; host thermal evolution excluded\n");
    return Success ? 0 : 1;
}
