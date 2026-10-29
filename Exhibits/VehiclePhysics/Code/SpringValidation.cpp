/*==========================================================================================================================================
                                                    SPRING VALIDATION
                                     ------------------------------------------------------------------
                    Compare the native pressure-fed tyre/strut against the current VehicleHMI demo.
==========================================================================================================================================*/

#include "Engine/Physics/VehicleSolver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

int main(int Count, char** Arguments)
{
    using namespace Frontier;
    if (Count != 2) return 2;
    std::ifstream Reference(Arguments[1]);
    if (!Reference) return 2;
    std::unique_ptr<VehicleSolver> Vehicle;
    double PositionError = 0, VelocityError = 0, AngularError = 0, OrientationError = 0;
    double ForceError = 0, DeflectionError = 0, LoadError = 0, LengthError = 0;
    int Samples = 0, MismatchedContact = 0, MismatchedPinch = 0, MismatchedTopOut = 0;
    int PinchedZeroPressure = 0, PinchedDrop = 0, AirborneDrop = 0;
    bool Finite = true, Reported = false;
    std::string Tag;
    while (Reference >> Tag)
    {
        int Scenario = 0, Step = 0;
        double Pressure[4], Throttle = 0, Brake = 0, Steer = 0;
        Reference >> Scenario >> Step;
        for (double& P : Pressure) Reference >> P;
        Reference >> Throttle >> Brake >> Steer;
        double Position[3], Velocity[3], Orientation[4], Angular[3];
        for (double& V : Position) Reference >> V;
        for (double& V : Velocity) Reference >> V;
        for (double& V : Orientation) Reference >> V;
        for (double& V : Angular) Reference >> V;
        double Wheel[4][7]; for (auto& W : Wheel) for (double& V : W) Reference >> V;
        if (!Reference || Tag != "S") { std::fprintf(stderr, "Invalid sample\n"); return 2; }
        if (Step == 0)
        {
            Vehicle = std::make_unique<VehicleSolver>();
            Vehicle->AssignCarcassModel(CarcassSpringModel);
            const double Spawn[3] = {-100.0, -34.0, Scenario == 4 ? 1.2 : 0.53}; Vehicle->Respawn(Spawn, 0.0);
            if (Scenario == 4) Vehicle->LinearVelocity[2] = -8.0;
        }
        Vehicle->AssignTyrePressures(Pressure);
        const double Grip[4] = {1,1,1,1}; Vehicle->AssignGrip(Grip);
        VehicleCommand Command; Command.Throttle = Throttle; Command.Brake = Brake; Command.Steer = Steer;
        Vehicle->Advance(Command, 1.0 / 480.0);
        if (!Reported && (std::abs(Vehicle->Position[2]-Position[2]) > 1e-7 ||
                          std::abs(Vehicle->Wheels[0].TyreForce-Wheel[0][0]) > 1e-3))
        {
            std::printf("first mismatch: scenario %d step %d, z error %.9g, force error %.9g N\n",
                        Scenario, Step, Vehicle->Position[2]-Position[2], Vehicle->Wheels[0].TyreForce-Wheel[0][0]);
            Reported = true;
        }
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            PositionError = std::max(PositionError, std::abs(Vehicle->Position[Axis] - Position[Axis]));
            VelocityError = std::max(VelocityError, std::abs(Vehicle->LinearVelocity[Axis] - Velocity[Axis]));
            AngularError = std::max(AngularError, std::abs(Vehicle->AngularVelocity[Axis] - Angular[Axis]));
            Finite &= std::isfinite(Vehicle->Position[Axis]) && std::isfinite(Vehicle->LinearVelocity[Axis]);
        }
        for (int Axis = 0; Axis < 4; ++Axis)
            OrientationError = std::max(OrientationError, std::abs(Vehicle->Orientation[Axis] - Orientation[Axis]));
        for (int I = 0; I < 4; ++I)
        {
            const auto& W = Vehicle->Wheels[I];
            ForceError = std::max(ForceError, std::abs(W.TyreForce - Wheel[I][0]));
            DeflectionError = std::max(DeflectionError, std::abs(W.TyreDeflection - Wheel[I][1]));
            LoadError = std::max(LoadError, std::abs(W.Load - Wheel[I][2]));
            LengthError = std::max(LengthError, std::abs(W.Length - Wheel[I][3]));
            MismatchedContact += W.Contact != (Wheel[I][4] != 0.0);
            MismatchedPinch += W.Pinched != (Wheel[I][5] != 0.0);
            MismatchedTopOut += W.ToppedOut != (Wheel[I][6] != 0.0);
            if (Scenario == 3) PinchedZeroPressure += W.Pinched;
            if (Scenario == 4) { PinchedDrop += W.Pinched; AirborneDrop += !W.Contact; }
        }
        ++Samples;
    }
    const bool Success = (Samples == 3480 || Samples == 3600) && Finite && MismatchedContact == 0 && MismatchedPinch == 0 &&
                         MismatchedTopOut == 0 && PinchedZeroPressure > 0 && PinchedDrop > 0 && AirborneDrop > 0 && PositionError < 1e-6 &&
                         VelocityError < 1e-6 && AngularError < 1e-6 && OrientationError < 1e-6 &&
                         ForceError < 1e-2 && DeflectionError < 1e-7 && LoadError < 1e-2 && LengthError < 1e-7;
    std::printf("%s spring against current demo, four 720-step cases + %d-step drop: %d samples\n", Success ? "PASS" : "FAIL", Samples - 2880, Samples);
    std::printf("max errors: position %.9g m, velocity %.9g m/s, angular %.9g rad/s, quaternion %.9g\n",
                PositionError, VelocityError, AngularError, OrientationError);
    std::printf("max errors: tyre force %.9g N, deflection %.9g m, load %.9g N, strut length %.9g m, contact mismatches %d\n",
                ForceError, DeflectionError, LoadError, LengthError, MismatchedContact);
    std::printf("pinch state mismatches %d, top-out mismatches %d; pinched wheel samples: zero-pressure %d, drop %d; airborne drop %d\n",
                MismatchedPinch, MismatchedTopOut, PinchedZeroPressure, PinchedDrop, AirborneDrop);
    std::printf("scope: prescribed pressures/grips, flat course, no debris; host thermal evolution excluded\n");
    return Success ? 0 : 1;
}
