// Verify the recovered demo's XPBD force contract without confusing it with measured tyre calibration.
import assert from 'node:assert/strict';
import { CarcassWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreCarcassProjection.js';

for (const Pressure of [0, 30000, 220000, 350000])
{
    for (const Deflection of [0.01, 0.02, 0.04])
    {
        const Wheel = new CarcassWheel();
        const Force = Wheel.Step({Center: [0, 0, 0.49], Orientation: [0, 0, 0, 1],
            GroundNormal: [0, 0, 1], Pressure, Deflection, LateralForce: 0}, 1 / 120, 2);
        assert.equal(Wheel.NodeCount, 160);
        assert.equal(Force, Deflection * (25000 + 0.88 * Pressure));
        assert.ok(Wheel.LocalPositions.every(Number.isFinite));
        const BottomCrown = (24 * 5 + 2) * 3 + 2;
        const Flattening = Wheel.LocalPositions[BottomCrown] - Wheel.RestLocal[BottomCrown];
        assert.ok(Flattening > 0);
        console.log(JSON.stringify({Pressure, Deflection, Force, Flattening, Nodes: Wheel.NodeCount}));
    }
}
console.log('PASS 12 recovered-demo XPBD cases: 160 particles, finite deformation, exact prescribed spring-force law.');
console.log('Not real-tyre validation: output force is assigned from deflection/pressure, not summed constraint reactions.');
