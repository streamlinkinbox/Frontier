// 🧩 Serialize before/after states from the unchanged JavaScript hard-drop trajectory.
import { VehicleSolver } from '../../../Experimental/VehicleHmiReference/Source/VehicleSolver.js';

const Vehicle = new VehicleSolver([]);
Vehicle.AssignTyreModel('spring');
Vehicle.Respawn([-100, -34, 1.2], 0);
Vehicle.LinearVelocity[2] = -8;
const Snapshot = () => [
    ...Vehicle.Position, ...Vehicle.LinearVelocity, ...Vehicle.Orientation, ...Vehicle.AngularVelocity,
    Vehicle.SteerAngle, Vehicle.GearIndex, Vehicle.Boost, Vehicle.EngineRPM,
    ...Vehicle.Wheels.flatMap(Wheel => [Wheel.Length, Wheel.Compression, Wheel.Deflection, Wheel.Spin,
        Wheel.SpinAngle, Wheel.SlipRatio, Wheel.SlipAngle, Wheel.TyreDeflection, Wheel.DeflectionRate, Wheel.Radius,
        Number(Wheel.Contact), Number(Wheel.Pinched), Number(Wheel.ToppedOut), Number(Wheel.Shredded),
        Wheel.TyreForce, Wheel.Load]),
];
for (let Step = 0; Step < 720; ++Step)
{
    Vehicle.AssignTyrePressures([30000, 30000, 30000, 30000]);
    Vehicle.AssignGrip([1, 1, 1, 1]);
    const Before = Snapshot();
    Vehicle.Step({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1 / 480);
    // Use 17 significant digits for round-trip binary64 values.
    console.log([Step, ...[...Before, ...Snapshot()].map(Field => Number(Field).toPrecision(17))].join(' '));
}
