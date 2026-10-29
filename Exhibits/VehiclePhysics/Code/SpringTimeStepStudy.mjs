// 🧩 Investigate timestep sensitivity of the unchanged demo spring vehicle; this is not a supported host-rate change.
import { VehicleSolver } from '../../../Experimental/VehicleHmiReference/Source/VehicleSolver.js';

const Command = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };
const Cases = [
    { Name: 'settling', Height: 0.53, Speed: 0.0, Pressure: 220000 },
    { Name: 'moderate drop', Height: 0.80, Speed: -2.0, Pressure: 220000 },
    { Name: 'hard drop', Height: 1.20, Speed: -8.0, Pressure: 30000 },
];

function Integrate(Case, Substeps)
{
    const Vehicle = new VehicleSolver([]);
    Vehicle.AssignTyreModel('spring');
    Vehicle.Respawn([-100, -34, Case.Height], 0);
    Vehicle.LinearVelocity[2] = Case.Speed;
    const Samples = [];
    let PeakForce = 0, PeakLoad = 0;
    for (let Interval = 0; Interval < 720; ++Interval)
    {
        for (let Substep = 0; Substep < Substeps; ++Substep)
        {
            Vehicle.AssignTyrePressures(Array(4).fill(Case.Pressure));
            Vehicle.AssignGrip([1, 1, 1, 1]);
            Vehicle.Step(Command, 1 / (480 * Substeps));
            for (const Wheel of Vehicle.Wheels)
            {
                PeakForce = Math.max(PeakForce, Wheel.TyreForce);
                PeakLoad = Math.max(PeakLoad, Wheel.Load);
            }
        }
        Samples.push({ Position: Vehicle.Position.slice(), Velocity: Vehicle.LinearVelocity.slice(),
                       Angular: Vehicle.AngularVelocity.slice(), Force: Vehicle.Wheels.map(W => W.TyreForce),
                       Load: Vehicle.Wheels.map(W => W.Load) });
    }
    return { Samples, PeakForce, PeakLoad };
}

for (const Case of Cases)
{
    const Runs = [1, 2, 4, 8].map(Substeps => Integrate(Case, Substeps));
    const Reference = Runs[3]; // Finest sampled run, not an exact or measured physical solution.
    for (let I = 0; I < Runs.length; ++I)
    {
        let Position = 0, Velocity = 0, Angular = 0, Force = 0, Load = 0;
        for (let Step = 0; Step < 720; ++Step)
        {
            const A = Runs[I].Samples[Step], B = Reference.Samples[Step];
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Position = Math.max(Position, Math.abs(A.Position[Axis]-B.Position[Axis]));
                Velocity = Math.max(Velocity, Math.abs(A.Velocity[Axis]-B.Velocity[Axis]));
                Angular = Math.max(Angular, Math.abs(A.Angular[Axis]-B.Angular[Axis]));
            }
            for (let Wheel = 0; Wheel < 4; ++Wheel)
            {
                Force = Math.max(Force, Math.abs(A.Force[Wheel]-B.Force[Wheel]));
                Load = Math.max(Load, Math.abs(A.Load[Wheel]-B.Load[Wheel]));
            }
        }
        console.log(`${Case.Name}: 1/${480 * (2 ** I)} s; peak tyre ${Runs[I].PeakForce} N, load ${Runs[I].PeakLoad} N; ` +
            `vs 1/3840 max position ${Position} m, velocity ${Velocity} m/s, angular ${Angular} rad/s, ` +
            `force ${Force} N, load ${Load} N`);
    }
}
console.log('Scope: direct JS Step with prescribed pressure/grip, no thermal evolution; 1/3840 is not ground truth.');
