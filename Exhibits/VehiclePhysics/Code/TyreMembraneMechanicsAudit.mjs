// 🧩 Independent finite-difference checks of two physical limitations in the demo's membrane formulation.
import { ShellContinuumWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js';

const Wheel = new ShellContinuumWheel();
const [A11, A22, , Area] = Wheel.ElemMetric.slice(0, 4); // Sector 0, first meridional patch.
const E1 = 1800, E2 = 1200, Nu = 0.38;
const U = Math.sqrt(A11) * 1.10, V = Math.sqrt(A22) * 1.05;
const Force = (X, Y) => {
    const Strain1 = 0.5 * (X * X - A11) / A11;
    const Strain2 = 0.5 * (Y * Y - A22) / A22;
    return [E1 * (Strain1 + Nu * Strain2) * Area * X,
            E2 * (Strain2 + Nu * Strain1) * Area * Y];
};
const H1 = Math.sqrt(A11) * 1e-5, H2 = Math.sqrt(A22) * 1e-5;
const F1Y = (Force(U, V + H2)[0] - Force(U, V - H2)[0]) / (2 * H2);
const F2X = (Force(U + H1, V)[1] - Force(U - H1, V)[1]) / (2 * H1);
const ReciprocalGap = Math.abs(F1Y - F2X) / Math.max(Math.abs(F1Y), Math.abs(F2X));

// The bending hinge measures a local-Y curvature change. Rigidly rotate the entire reference ring about Z.
const Angle = 0.30, Cos = Math.cos(Angle), Sin = Math.sin(Angle);
const RotateY = (Node) => {
    const Offset = Node * 3;
    return Wheel.RestLocal[Offset] * Sin + Wheel.RestLocal[Offset + 1] * Cos;
};
const ReferenceCurvature = Wheel.RestLocal[4] + Wheel.RestLocal[10] - 2 * Wheel.RestLocal[7];
const RotatedCurvature = RotateY(1) + RotateY(3) - 2 * RotateY(2);
const HingeDifference = RotatedCurvature - ReferenceCurvature;
if (!(ReciprocalGap > 0.20) || !(Math.abs(HingeDifference) > 0.005))
    throw new Error('The documented membrane counterexamples did not reproduce');
console.log(`TYRE_MEMBRANE_MECHANICS cell=0 A11=${A11.toPrecision(9)} A22=${A22.toPrecision(9)} ` +
    `cross_derivative_12=${F1Y.toPrecision(9)} cross_derivative_21=${F2X.toPrecision(9)} ` +
    `reciprocity_gap=${ReciprocalGap.toPrecision(9)} rigid_rotation_rad=${Angle} ` +
    `hinge_curvature_change_m=${HingeDifference.toPrecision(9)} source_counterexamples=1`);
