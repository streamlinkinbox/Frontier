// 🧩 Measure same-source sensitivity of the JavaScript spring model to tiny initial perturbations.
import { VehicleSolver } from '../../../Experimental/VehicleHmiReference/Source/VehicleSolver.js';

function Integrate(InitialHeightDelta, HullContactsEnabled)
{
    const Vehicle = new VehicleSolver([]);
    Vehicle.AssignTyreModel('spring');
    Vehicle.Respawn([-100, -34, 1.2 + InitialHeightDelta], 0);
    Vehicle.LinearVelocity[2] = -8;
    // Diagnostic control only: the unmodified source normally resolves its sphere hull contacts.
    if (!HullContactsEnabled) Vehicle.ResolveHullContacts = () => {};
    const Samples = [];
    for (let Step = 0; Step < 720; ++Step)
    {
        Vehicle.AssignTyrePressures([30000, 30000, 30000, 30000]);
        Vehicle.AssignGrip([1, 1, 1, 1]);
        Vehicle.Step({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1 / 480);
        Samples.push({ Position: Vehicle.Position.slice(), Velocity: Vehicle.LinearVelocity.slice(),
                       Angular: Vehicle.AngularVelocity.slice(), Force: Vehicle.Wheels.map(W => W.TyreForce),
                       Load: Vehicle.Wheels.map(W => W.Load), Contact: Vehicle.Wheels.map(W => W.Contact) });
    }
    return Samples;
}

for (const HullContactsEnabled of [true, false])
{
    const Reference = Integrate(0, HullContactsEnabled);
    const Perturbations = HullContactsEnabled ? [0, 1e-15, 1e-12, 1e-9, 1e-6] : [0, 1e-15];
    for (const InitialHeightDelta of Perturbations)
    {
        const Samples = Integrate(InitialHeightDelta, HullContactsEnabled);
        let Position = 0, Velocity = 0, Angular = 0, Force = 0, Load = 0, ContactMismatches = 0, First = -1;
        for (let Step = 0; Step < 720; ++Step)
        {
            const A = Reference[Step], B = Samples[Step];
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Position = Math.max(Position, Math.abs(A.Position[Axis]-B.Position[Axis]));
                Velocity = Math.max(Velocity, Math.abs(A.Velocity[Axis]-B.Velocity[Axis]));
                Angular = Math.max(Angular, Math.abs(A.Angular[Axis]-B.Angular[Axis]));
            }
            for (let Wheel = 0; Wheel < 4; ++Wheel)
            {
                const Difference = Math.abs(A.Force[Wheel]-B.Force[Wheel]);
                Force = Math.max(Force, Difference);
                if (First < 0 && Difference > 0.01) First = Step;
                Load = Math.max(Load, Math.abs(A.Load[Wheel]-B.Load[Wheel]));
                ContactMismatches += Number(A.Contact[Wheel] !== B.Contact[Wheel]);
            }
        }
        if (InitialHeightDelta === 0 && (Position !== 0 || Velocity !== 0 || Angular !== 0 || Force !== 0 ||
                                         Load !== 0 || ContactMismatches !== 0))
            throw new Error('Identical initial conditions did not repeat exactly');
        console.log(`hull ${HullContactsEnabled ? 'on' : 'off'}, initial dz=${InitialHeightDelta} m: ` +
            `first force >0.01 N step ${First}, max position ${Position} m, velocity ${Velocity} m/s, ` +
            `angular ${Angular} rad/s, force ${Force} N, load ${Load} N, contact mismatches ${ContactMismatches}`);
    }
}
console.log('Scope: unchanged JS equations with one diagnostic hull override; perturbations compare simulations, not measurements.');
