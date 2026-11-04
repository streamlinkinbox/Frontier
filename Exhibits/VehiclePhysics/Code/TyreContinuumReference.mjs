// 🧩 Generate deterministic observations from the unchanged, current VehicleHMI tyre implementations.
import { CarcassWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreCarcassProjection.js';
import { SolidContinuumWheel } from '../../../Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js';

const F = (x) => Number(x).toPrecision(17);
const PressureCases = [0, 30000, 220000, 350000];
const DeflectionCases = [0, 0.014, 0.060];
for (const [Type, Constructor] of [['projection', CarcassWheel], ['solid', SolidContinuumWheel]])
    for (let Case = 0; Case < 12; ++Case)
    {
        const Wheel = new Constructor();
        const Pressure = PressureCases[Math.floor(Case / 3)];
        const BaseDeflection = DeflectionCases[Case % 3];
        const Centre = [1.25, -0.4, 0.52];
        Wheel.Reset(Centre, [0, 0, 0, 1]);
        for (let Step = 0; Step < 8; ++Step)
        {
            const Spin = Step * 0.19 + Case * 0.03;
            const Yaw = (Case - 5) * 0.07;
            const cs = Math.cos(Spin / 2), ss = Math.sin(Spin / 2);
            const cy = Math.cos(Yaw / 2), sy = Math.sin(Yaw / 2);
            const Orientation = [-sy * ss, cy * ss, sy * cs, cy * cs];
            const X = 0.04 * Math.sin(Case + Step), Y = 0.03 * Math.cos(Case - Step);
            const GroundNormal = [X, Y, Math.sqrt(1 - X * X - Y * Y)];
            const Input = { Center: [Centre[0] + 0.01 * Step, Centre[1], Centre[2]], Orientation, GroundNormal,
                Deflection: BaseDeflection * (0.5 + 0.1 * Step), Pressure,
                LateralForce: (Step % 2 ? -1 : 1) * (Case % 4) * 1500 };
            const Seconds = Step % 2 ? 1 / 60 : 1 / 120;
            const Force = Wheel.Step(Input, Seconds, 2);
            const Diagnostics = Type === 'solid' ? [Wheel.MeanJacobian, Wheel.MeanStress, Wheel.ContactNodes] : [0, 0, 0];
            console.log(['C', Type, Case, Step, ...[Seconds, Input.Deflection, Input.Pressure, Input.LateralForce,
                ...Input.Center, ...Orientation, ...GroundNormal, Force, ...Diagnostics].map(F)].join(' '));
            for (let N = 0; N < Wheel.NodeCount; ++N)
            {
                const B = N * 3;
                console.log(['N', ...Wheel.LocalPositions.slice(B, B + 3), ...Wheel.Positions.slice(B, B + 3)].map((v) => typeof v === 'number' ? F(v) : v).join(' '));
            }
        }
    }
