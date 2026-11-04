// 🧩 Observe actual demo vehicle wiring, including switching among spring, XPBD and solid shapes.
import { VehicleSolver } from '../../../Experimental/VehicleHmiReference/Source/VehicleSolver.js';
const F = (x) => Number(x).toPrecision(17);
for (const Scenario of ['xpbd', 'solid', 'switch'])
{
    const Vehicle = new VehicleSolver([]);
    Vehicle.AssignTyreModel(Scenario === 'switch' ? 'spring' : Scenario);
    Vehicle.Respawn([-100, -34, 0.53], 0);
    for (let Step = 0; Step < 60; ++Step)
    {
        if (Scenario === 'switch')
        {
            if (Step === 10 || Step === 40) Vehicle.AssignTyreModel('xpbd');
            if (Step === 25) Vehicle.AssignTyreModel('solid');
            if (Step === 55) Vehicle.AssignTyreModel('spring');
        }
        Vehicle.AssignTyrePressures([220000, 30000, 220000, 30000]);
        Vehicle.AssignGrip([1, 1, 1, 1]);
        const Command = {Throttle: Step < 20 ? 0 : 0.25, Brake: 0, Steer: Step < 35 ? 0 : 0.1, Handbrake: false};
        Vehicle.Advance(Command, 1 / 120);
        const Values = [...Vehicle.Position, ...Vehicle.LinearVelocity, ...Vehicle.Orientation, ...Vehicle.AngularVelocity];
        for (const Wheel of Vehicle.Wheels) Values.push(Wheel.TyreForce, Wheel.TyreDeflection, Wheel.Load, Wheel.Length);
        console.log(['V', Scenario, Step, Vehicle.TyreModel, ...Values.map(F)].join(' '));
        if (Vehicle.TyreModel !== 'spring')
            for (const Wheel of Vehicle.Wheels)
                for (let N = 0; N < Wheel.Carcass.NodeCount; ++N)
                {
                    const B = N * 3;
                    console.log(['N', ...Wheel.Carcass.LocalPositions.slice(B, B+3), ...Wheel.Carcass.Positions.slice(B, B+3)].map((v) => typeof v === 'number' ? F(v) : v).join(' '));
                }
    }
}
