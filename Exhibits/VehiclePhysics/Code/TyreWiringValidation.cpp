/*==========================================================================================================================================
                                                 TYRE WIRING VALIDATION
                                     ------------------------------------------------------------------
                        Compare selected tyre shapes and spring dynamics with the current demo.
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
    std::string Tag, Scenario, Model;
    int Steps = 0, Samples = 0, ActiveChecks = 0;
    double WorstMotion = 0, WorstForce = 0, WorstLocal = 0, WorstWorld = 0, MaxDisplacement = 0;
    bool Finite = true;
    while (Reference >> Tag)
    {
        int Step = 0;
        Reference >> Scenario >> Step >> Model;
        if (!Reference || Tag != "V") return 2;
        double Position[3], Velocity[3], Orientation[4], Angular[3], Wheel[4][4];
        for (double& V : Position) Reference >> V;
        for (double& V : Velocity) Reference >> V;
        for (double& V : Orientation) Reference >> V;
        for (double& V : Angular) Reference >> V;
        for (auto& W : Wheel) for (double& V : W) Reference >> V;
        if (!Reference) return 2;
        const double Pressures[4] = {220000, 30000, 220000, 30000}, Grip[4] = {1,1,1,1};
        if (Step == 0)
        {
            Vehicle = std::make_unique<VehicleSolver>();
            Vehicle->AssignCarcassModel(Scenario == "xpbd" ? CarcassProjectionModel :
                                        (Scenario == "solid" ? CarcassSolidModel : CarcassSpringModel));
            const double Spawn[3] = {-100, -34, 0.53}; Vehicle->Respawn(Spawn, 0.0);
        }
        Vehicle->AssignTyrePressures(Pressures); Vehicle->AssignGrip(Grip);
        if (Scenario == "switch")
        {
            if (Step == 10 || Step == 40) Vehicle->AssignCarcassModel(CarcassProjectionModel);
            if (Step == 25) Vehicle->AssignCarcassModel(CarcassSolidModel);
            if (Step == 55) Vehicle->AssignCarcassModel(CarcassSpringModel);
        }
        const CarcassModel Actual = Vehicle->QueryCarcassModel();
        if ((Model == "xpbd" && Actual != CarcassProjectionModel) ||
            (Model == "solid" && Actual != CarcassSolidModel) ||
            (Model == "spring" && Actual != CarcassSpringModel)) return 2;
        ++ActiveChecks;
        VehicleCommand Command;
        Command.Throttle = Step < 20 ? 0.0 : 0.25;
        Command.Steer = Step < 35 ? 0.0 : 0.1;
        Vehicle->Advance(Command, 1.0 / 120.0);
        for (int A = 0; A < 3; ++A)
        {
            WorstMotion = std::max({WorstMotion, std::abs(Vehicle->Position[A] - Position[A]),
                                    std::abs(Vehicle->LinearVelocity[A] - Velocity[A]),
                                    std::abs(Vehicle->AngularVelocity[A] - Angular[A])});
        }
        for (int A = 0; A < 4; ++A)
            WorstMotion = std::max(WorstMotion, std::abs(Vehicle->Orientation[A] - Orientation[A]));
        for (int I = 0; I < 4; ++I)
            WorstForce = std::max({WorstForce, std::abs(Vehicle->Wheels[I].TyreForce-Wheel[I][0]),
                                   std::abs(Vehicle->Wheels[I].Load-Wheel[I][2])});
        if (Model != "spring")
        {
            for (int I = 0; I < 4; ++I)
            {
                const auto& Local = Model == "xpbd" ? Vehicle->QueryCarcassProjection(I).QueryLocalPositions()
                                                     : Vehicle->QueryCarcassSolid(I).QueryLocalPositions();
                const auto& World = Model == "xpbd" ? Vehicle->QueryCarcassProjection(I).QueryWorldPositions()
                                                     : Vehicle->QueryCarcassSolid(I).QueryWorldPositions();
                const auto& Rest = Model == "xpbd" ? Vehicle->QueryCarcassProjection(I).QueryRestPositions()
                                                    : Vehicle->QueryCarcassSolid(I).QueryRestPositions();
                for (int N = 0; N < TyreContinuumParticleCount; ++N)
                {
                    Reference >> Tag; if (Tag != "N") return 2;
                    double ExpectedLocal[3], ExpectedWorld[3];
                    for (double& V : ExpectedLocal) Reference >> V;
                    for (double& V : ExpectedWorld) Reference >> V;
                    if (!Reference) return 2;
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        const int B = N*3+Axis;
                        WorstLocal = std::max(WorstLocal, std::abs(Local[B]-ExpectedLocal[Axis]));
                        WorstWorld = std::max(WorstWorld, std::abs(World[B]-ExpectedWorld[Axis]));
                        MaxDisplacement = std::max(MaxDisplacement, std::abs(static_cast<double>(Local[B])-Rest[B]));
                        Finite &= std::isfinite(Local[B]) && std::isfinite(World[B]);
                    }
                    ++Samples;
                }
            }
        }
        ++Steps;
    }
    const bool Success = Steps == 180 && ActiveChecks == 180 && Finite && MaxDisplacement > 0.005 &&
                         WorstMotion < 1e-6 && WorstForce < 1e-3 && WorstLocal < 2e-6 && WorstWorld < 2e-6;
    std::printf("%s vehicle shape wiring, fixed input and mode changes: %d advances, %d particle observations\n",
                Success ? "PASS" : "FAIL", Steps, Samples);
    std::printf("max errors: chassis %.9g, force %.9g N, local %.9g m, world %.9g m; max deformation %.9g m\n",
                WorstMotion, WorstForce, WorstLocal, WorstWorld, MaxDisplacement);
    std::printf("scope: prescribed pressure/grip; shapes do not feed prescribed reactions into chassis\n");
    return Success ? 0 : 1;
}
