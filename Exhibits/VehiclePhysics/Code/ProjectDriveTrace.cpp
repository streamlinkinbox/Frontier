/*==========================================================================================================================================
                                              PROJECT DRIVE TRACE COMPARISON
                                     ------------------------------------------------------------------
            The same deterministic input stream is built against the untouched reference and imported sources.
==========================================================================================================================================*/
#include "PacejkaMagicFormula.h"
#include "TyreSlipDynamics.h"
#include "Drivetrain.h"

#include <cmath>
#include <cstdio>

int main()
{
    using namespace Frontier::Vehicle;
    const float Loads[] = {0, 500, 1500, 3000, 5000, 9000, 15000};
    const float Cambers[] = {-0.12f, -0.04f, 0, 0.04f, 0.12f};
    const float Slips[] = {-0.8f, -0.3f, -0.1f, -0.02f, 0, 0.02f, 0.1f, 0.3f, 0.8f, 1.3f};
    const float Angles[] = {-0.6f, -0.2f, -0.05f, 0, 0.05f, 0.2f, 0.6f};
    int ForceCount = 0, SlipCount = 0, DriveCount = 0;
    for (int Tune = 0; Tune < 3; ++Tune)
    {
        PacejkaParameters P;
        if (Tune == 1)
        {
            P.Lx = 0.85f; P.Ly = 1.1f; P.Ltr = 0.7f;
            P.pHx1 = 0.003f; P.pHy1 = 0.002f; P.pVy3 = 0.03f; P.rVy1 = 0.03f;
        }
        if (Tune == 2)
        {
            P.Fz0 = 4.0f; P.MuLoadSensitivityLong = 0.15f; P.MuLoadSensitivityLat = 0.2f;
            P.qDz1 = 0.08f; P.qBz1 = 9.0f; P.rBy4 = 0.05f; P.rVy2 = 0.02f;
        }
        PacejkaMagicFormula Model; Model.AssignParameters(P);
        for (float Load : Loads) for (float Camber : Cambers)
            for (float Kappa : Slips) for (float Alpha : Angles)
            {
                const float Fx = Model.LongitudinalForce(Kappa, Camber, Load);
                const float Fy = Model.LateralForce(Alpha, Camber, Load);
                const TyreForces F = Model.Combined(Kappa, Alpha, Camber, Load);
                if (!(std::isfinite(Fx) && std::isfinite(Fy) && std::isfinite(F.Fx) &&
                      std::isfinite(F.Fy) && std::isfinite(F.Mz))) return 1;
                std::printf("F %d %.9g %.9g %.9g %.9g %.9g\n", Tune, Fx, Fy, F.Fx, F.Fy, F.Mz);
                ++ForceCount;
            }
        for (int Solver = 0; Solver < 2; ++Solver)
        {
            TyreSlipDynamics Dynamics(Model);
            Dynamics.AssignSolver(Solver ? SlipSolver::RelaxationLength : SlipSolver::NewtonReference);
            Dynamics.AssignForceLag(Solver != 0);
            SlipState State;
            for (int Step = 0; Step < 720; ++Step)
            {
                const float T = Step / 240.0f;
                WheelKinematics K;
                K.Vx = 8.0f * std::sin(T * 1.7f); K.Vsy = 0.8f * std::sin(T * 2.3f);
                K.OmegaR = K.Vx + 0.5f * std::cos(T * 1.2f);
                K.CamberRad = 0.05f * std::sin(T); K.Fz_N = 3500.0f + 2000.0f * std::sin(T * 0.8f);
                const auto R = Dynamics.Step(K, State, 1.0f/240.0f);
                if (!(std::isfinite(R.Kappa) && std::isfinite(R.AlphaRad) && std::isfinite(R.Forces.Fx) &&
                      std::isfinite(R.Forces.Fy) && std::isfinite(R.Forces.Mz))) return 2;
                std::printf("S %d %d %.9g %.9g %.9g %.9g %.9g %d %d\n", Tune, Solver,
                            R.Kappa, R.AlphaRad, R.Forces.Fx, R.Forces.Fy, R.Forces.Mz, R.Iterations, R.ForceEvals);
                ++SlipCount;
            }
        }
    }
    for (int Induction = 0; Induction < 3; ++Induction)
        for (int Differential = 0; Differential < 4; ++Differential)
        {
            Drivetrain Drive; Drive.Reset(1200.0f);
            Drive.AssignInduction(static_cast<InductionType>(Induction));
            DifferentialParameters Diff; Diff.Mode = static_cast<DifferentialMode>(Differential); Diff.VectoringBias = 0.4f;
            Drive.AssignDifferential(Diff);
            for (int Step = 0; Step < 720; ++Step)
            {
                const float T = Step / 240.0f;
                DrivetrainInputs In;
                In.dt = 1.0f / 240.0f; In.Throttle = Step < 400 ? 0.85f : 0.1f;
                In.GearIndex = Step < 240 ? 3 : (Step < 480 ? 4 : 5);
                In.DrivenWheelRPM = 150.0f + 110.0f*T; In.LeftWheelRPM = In.DrivenWheelRPM + 25.0f;
                In.RightWheelRPM = In.DrivenWheelRPM - 25.0f;
                const auto R = Drive.Step(In);
                if (!(std::isfinite(R.EngineRPM) && std::isfinite(R.TurboRPM) && std::isfinite(R.BoostPressure_Bar) &&
                      std::isfinite(R.GearboxOutputTorque_Nm) && std::isfinite(R.LeftDriveTorque_Nm) &&
                      std::isfinite(R.RightDriveTorque_Nm))) return 3;
                std::printf("D %d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d\n", Induction, Differential,
                    R.EngineRPM, R.TurboRPM, R.BoostPressure_Bar, R.EngineTorque_Nm, R.GearboxOutputTorque_Nm,
                    R.LeftDriveTorque_Nm, R.RightDriveTorque_Nm, int(R.ClutchLocked));
                ++DriveCount;
            }
        }
    std::fprintf(stderr, "Trace: %d force/torque cases, %d transient-slip steps, %d drivetrain steps\n",
                 ForceCount, SlipCount, DriveCount);
    return 0;
}
