/*==========================================================================================================================================
                                               TYRE CONTINUUM VALIDATION
                                     ------------------------------------------------------------------
                     Compare native tyre deformation with unchanged current VehicleHMI JavaScript.
==========================================================================================================================================*/

#include "Engine/Physics/TyreContinuum.h"
#include "Engine/Physics/TyreSurfaceProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace
{
int Passed = 0, Failed = 0;
void Check(const char* Label, bool Condition, double Measurement)
{
    std::printf("%s %-58s %.9g\n", Condition ? "PASS" : "FAIL", Label, Measurement);
    Condition ? ++Passed : ++Failed;
}
}

int main(int Count, char** Arguments)
{
    using namespace Frontier;
    if (Count != 2) { std::fprintf(stderr, "Expected path to JavaScript observations\n"); return 2; }
    std::ifstream Source(Arguments[1]);
    if (!Source) { std::fprintf(stderr, "Cannot open JavaScript observations\n"); return 2; }
    std::unique_ptr<TyreCarcassProjection> Projection;
    std::unique_ptr<TyreCarcassSolid> Solid;
    double WorstLocal[2]{}, WorstWorld[2]{}, WorstForce[2]{}, WorstJacobian = 0, WorstStress = 0;
    int Tested[2]{}, WrongContacts = 0, UnexpectedRows = 0;
    bool Finite = true, BeadsPinned = true;
    std::string Tag, Type;
    while (Source >> Tag)
    {
        if (Tag != "C") { ++UnexpectedRows; break; }
        int Case = -1, StepIndex = -1;
        double Seconds = 0, ExpectedForce = 0, ExpectedJ = 0, ExpectedStress = 0, ExpectedContacts = 0;
        TyreContinuumInput Input;
        Source >> Type >> Case >> StepIndex >> Seconds >> Input.Deflection >> Input.Pressure >> Input.LateralForce;
        for (double& V : Input.Centre) Source >> V;
        for (double& V : Input.Orientation) Source >> V;
        for (double& V : Input.GroundNormal) Source >> V;
        Source >> ExpectedForce >> ExpectedJ >> ExpectedStress >> ExpectedContacts;
        const int Which = Type == "projection" ? 0 : (Type == "solid" ? 1 : -1);
        if (Which < 0) { ++UnexpectedRows; break; }
        if (StepIndex == 0)
        {
            if (Which == 0) { Projection = std::make_unique<TyreCarcassProjection>(); Projection->Reset(); }
            else { Solid = std::make_unique<TyreCarcassSolid>(); Solid->Reset(); }
        }
        const double Force = Which == 0 ? Projection->Advance(Input, Seconds, 2) : Solid->Advance(Input, Seconds, 2);
        const auto& Local = Which == 0 ? Projection->QueryLocalPositions() : Solid->QueryLocalPositions();
        const auto& World = Which == 0 ? Projection->QueryWorldPositions() : Solid->QueryWorldPositions();
        const auto& Rest = Which == 0 ? Projection->QueryRestPositions() : Solid->QueryRestPositions();
        WorstForce[Which] = std::max(WorstForce[Which], std::abs(Force - ExpectedForce));
        if (Which == 1)
        {
            WorstJacobian = std::max(WorstJacobian, std::abs(Solid->QueryMeanJacobian() - ExpectedJ));
            WorstStress = std::max(WorstStress, std::abs(Solid->QueryMeanStress() - ExpectedStress) /
                                               std::max(1.0, std::abs(ExpectedStress)));
            WrongContacts += Solid->QueryContactCount() != static_cast<int>(ExpectedContacts);
        }
        for (int N = 0; N < TyreContinuumParticleCount; ++N)
        {
            Source >> Tag;
            if (Tag != "N") { ++UnexpectedRows; break; }
            double ExpectedLocal[3], ExpectedWorld[3];
            for (double& V : ExpectedLocal) Source >> V;
            for (double& V : ExpectedWorld) Source >> V;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const int B = N*3+Axis;
                WorstLocal[Which] = std::max(WorstLocal[Which], std::abs(Local[B] - ExpectedLocal[Axis]));
                WorstWorld[Which] = std::max(WorstWorld[Which], std::abs(World[B] - ExpectedWorld[Axis]));
                Finite &= std::isfinite(Local[B]) && std::isfinite(World[B]);
                if (N % 5 == 0 || N % 5 == 4) BeadsPinned &= Local[B] == Rest[B];
            }
        }
        ++Tested[Which];
    }
    Check("complete 12 cases x 8 samples / model", Tested[0] == 96 && Tested[1] == 96 && UnexpectedRows == 0 && !Source.bad(),
          Tested[0] + Tested[1]);
    Check("projection local positions vs demo (max m)", WorstLocal[0] < 2e-6, WorstLocal[0]);
    Check("projection world positions vs demo (max m)", WorstWorld[0] < 2e-6, WorstWorld[0]);
    Check("projection prescribed force vs demo (max N)", WorstForce[0] < 1e-8, WorstForce[0]);
    Check("solid local positions vs demo (max m)", WorstLocal[1] < 2e-6, WorstLocal[1]);
    Check("solid world positions vs demo (max m)", WorstWorld[1] < 2e-6, WorstWorld[1]);
    Check("solid prescribed force vs demo (max N)", WorstForce[1] < 1e-8, WorstForce[1]);
    Check("solid mean Jacobian vs demo (max absolute)", WorstJacobian < 2e-6, WorstJacobian);
    Check("solid mean stress vs demo (max relative)", WorstStress < 2e-5, WorstStress);
    Check("solid contact counts match demo", WrongContacts == 0, WrongContacts);
    Check("all coordinates finite and rim beads pinned", Finite && BeadsPinned, Finite && BeadsPinned);
    {
        TyreContinuumPositions Rest{}, Local{};
        TyreDeformationRecord Record; Record.Deflection = 0.02f;
        for (int Sector = 0; Sector < 32; ++Sector)
            for (int Ring = 0; Ring < 5; ++Ring)
            {
                const int B = (Sector*5+Ring)*3;
                Local[B] = 0.02f; Local[B+1] = -0.01f; Local[B+2] = 0.03f;
            }
        const float Position[3] = {0.50855f, 0.0f, 0.0f}, Normal[3] = {1.0f, 0.0f, 0.0f};
        float Result[3], NormalOut[3];
        ResolveContinuumWheelVertex(Record, true, Rest, Local, Position, Normal, Result, NormalOut);
        const double ConstantError = std::max({std::abs(Result[0] - (Position[0]+0.02f)),
            std::abs(Result[1]+0.01f), std::abs(Result[2]-0.03f), std::abs(NormalOut[0]-1.0f)});
        Check("constant nodal field reproduces translation", ConstantError < 1e-6, ConstantError);
        for (int Sector = 0; Sector < 32; ++Sector)
            for (int Ring = 0; Ring < 5; ++Ring)
                Local[(Sector*5+Ring)*3] = 0.001f * (Sector + 2.0f*Ring);
        constexpr float HalfSector = 3.14159265358979323846f / 32.0f;
        const float Sample[3] = {0.50855f * std::cos(HalfSector), -0.5f * 0.118f / 1.35f,
                                 0.50855f * std::sin(HalfSector)};
        ResolveContinuumWheelVertex(Record, true, Rest, Local, Sample, Normal, Result, NormalOut);
        const double BilinearError = std::abs((Result[0]-Sample[0])-0.0035f);
        Check("sector/ring bilinear interpolation matches hand calculation", BilinearError < 1e-6, BilinearError);
        const double NormalLength = std::sqrt(NormalOut[0]*NormalOut[0] + NormalOut[1]*NormalOut[1] + NormalOut[2]*NormalOut[2]);
        Check("deformed shading normal is normalized", std::abs(NormalLength-1.0) < 1e-6, NormalLength);
        Record.Deflection = 0.0f;
        ResolveContinuumWheelVertex(Record, true, Rest, Local, Sample, Normal, Result, NormalOut);
        Check("unloaded tyre leaves vertices unchanged", Result[0] == Sample[0] && Result[1] == Sample[1] && Result[2] == Sample[2], Result[0]-Sample[0]);
        Record.Shredded = true;
        ResolveContinuumWheelVertex(Record, true, Rest, Local, Sample, Normal, Result, NormalOut);
        Check("shredded rubber collapses like demo shader", Result[0] == 0 && Result[1] == 0 && Result[2] == 0, Result[0]);
    }
    std::printf("SUMMARY passed %d failed %d\n", Passed, Failed);
    return Failed ? 1 : 0;
}
