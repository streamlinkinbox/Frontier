// 🧩 Sample the unchanged demo XPBD and solid displacement fields for vertex projection comparisons.
import { CarcassWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreCarcassProjection.js';
import { SolidContinuumWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js';

const Fields = {};
for (const [Label, Constructor] of [['xpbd', CarcassWheel], ['solid', SolidContinuumWheel]])
{
    const Wheel = new Constructor();
    const Centre = [0, 0, .52];
    const Orientation = [0, 0, 0, 1];
    Wheel.Reset(Centre, Orientation);
    const Input = {Center: Centre, Orientation, GroundNormal: [0, 0, 1], Deflection: .045,
                   Pressure: 220000, LateralForce: 3000};
    for (let Step = 0; Step < 8; ++Step) Wheel.Step(Input, 1 / 60, 2);
    const Displacements = new Float32Array(Wheel.NodeCount * 4);
    for (let ParticleIndex = 0; ParticleIndex < Wheel.NodeCount; ++ParticleIndex)
        for (let Axis = 0; Axis < 3; ++Axis)
            Displacements[ParticleIndex * 4 + Axis] = Wheel.LocalPositions[ParticleIndex * 3 + Axis] -
                                                       Wheel.RestLocal[ParticleIndex * 3 + Axis];
    Fields[Label] = [...Displacements];
}
console.log(JSON.stringify(Fields));
