//============================================================================================================================================
//                                                             SCANSEQUENCE.JS
//============================================================================================================================================
// 🧩 The X-ray scan's clock, ported value for value from interactive-vehicle-hmi-configurator.zip (Truck.tsx "scan animation state machine"
//    and the scan-mode extras): a front travels from the nose to the tail, hold, fade, repeat; everything the front has passed turns from
//    solid shell into ghost, wireframe and particles.
//
//    Coordinates are the car's own: x forward from the middle of the car (the reference's x), heights above the ground it stands on.

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

import { ConvertHexToLinear } from "./PaintRipple.js";

export const ScanSpecification = Object.freeze({
    Cycle:         6.6,     // [s]  one full pass: sweep, hold, fade
    Sweep:         4.6,     // [s]  the front's travel
    Hold:          1.0,     // [s]  everything stays ghost
    FrontStart:    3.25,    // [m]  ahead of the nose
    FrontEnd:     -3.35,    // [m]  behind the tail
    FrontIdle:     3.3,     // [m]  uScanX before the first scan
    RampRate:      3.0,     // [1/s] THREE.MathUtils.damp lambda of the toggle ramp
    RampSettle:    0.002,   // [-]  the ramp snaps to its target inside this
    StepLimit:     0.05,    // [s]  useFrame: dt = Math.min(dtRaw, 0.05)
    SolidFloor:    0.02,    // [-]  below this ramp the solid materials are opaque again (wantTransparent)
    EdgeOpacity:   0.55,    // [-]  the wireframe's opacity at full amount
    EdgeMargin:    0.05,    // [m]  the wireframe lives only past front + margin (clipPlane constant)
    CurtainOpacity: [0.075, 0.30],   // [-]  the two curtain planes at full visibility
    CurtainIdle:   0.25,    // [-]  the curtain's share while the front rests (hold and fade)
    CurtainHeight: 1.18,    // [m]  the curtain group's height above the ground
    CurtainPlanes: [{ Across: 2.4, Up: 2.5 }, { Across: 0.55, Up: 2.5 }],   // [m]
    GhostColour:   "#a9dcec",       // holoMat uColor
    Accent:        "#38bdf8",       // holoMat uAccent, the curtain, the HUD: the drive mode's accent
    EdgeColour:    "#8fd8f2",       // SCAN
    ParticleCount: 130,
    ParticleBox:   [7.5, 3.2, 4.2], // [m]  along the car, up, across
    ParticleCentre: 1.35,           // [m]  above the ground
    ParticleSize:  2.2,
    ParticleSpeed: 0.35,
    ParticleOpacity: 0.55,
    ParticleColour: "#8fd8f2"
});

// 📐 THREE.ColorManagement's linear → sRGB OETF (colorspace_fragment).
export function ConvertLinearToSrgb(Value)
{
    return Value <= 0.0031308 ? Value * 12.92 : 1.055 * Math.pow(Value, 1.0 / 2.4) - 0.055;
}

// 📐 THREE.ACESFilmicToneMapping at exposure 1 (tonemapping_fragment): Stephen Hill's RRT and ODT fit behind the 0.6 prescale. react-three-fiber's
//    Canvas turns it on, and every built-in material (the wireframe, the curtain) and drei's Sparkles are mapped by it; holoMat is not.
export function ResolveAcesFilmic(Linear)
{
    const InputColumns  = [[0.59719, 0.07600, 0.02840], [0.35458, 0.90834, 0.13383], [0.04823, 0.01566, 0.83777]];
    const OutputColumns = [[1.60475, -0.10208, -0.00327], [-0.53108, 1.10813, -0.07276], [-0.07367, -0.00605, 1.07602]];
    const Multiply = (Columns, Vector) => [0, 1, 2].map((Row) => Columns[0][Row] * Vector[0] + Columns[1][Row] * Vector[1] + Columns[2][Row] * Vector[2]);
    const Fit = (X) => (X * (X + 0.0245786) - 0.000090537) / (X * (0.983729 * X + 0.4329510) + 0.238081);
    const Mapped = Multiply(OutputColumns, Multiply(InputColumns, Linear.map((Value) => Value / 0.6)).map(Fit));
    return Mapped.map((Value) => Math.min(Math.max(Value, 0.0), 1.0));
}

// 📝 The colour a tone-mapped material of this hex lands on in the reference's sRGB canvas, as the encoded bytes it blends with.
export function ResolveDisplayColour(Hex)
{
    return ResolveAcesFilmic(ConvertHexToLinear(Hex)).map(ConvertLinearToSrgb);
}

// 📐 Truck.tsx easeInOutCubic.
export function EaseInOutCubic(X)
{
    return X < 0.5 ? 4.0 * X * X * X : 1.0 - Math.pow(-2.0 * X + 2.0, 3.0) / 2.0;
}

// 📐 THREE.MathUtils.damp.
export function DampValue(Value, Target, Lambda, Seconds)
{
    return Value + (Target - Value) * (1.0 - Math.exp(-Lambda * Seconds));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

export class ScanSequence
{
    constructor()
    {
        this.Enabled        = false;
        this.Ramp           = 0.0;
        this.SweepSeconds   = 0.0;
        this.Front          = ScanSpecification.FrontIdle;   // uScanX
        this.Amount         = 0.0;                           // uHoloAmt
        this.CycleAmount    = 1.0;
        this.Sweeping       = false;
        this.Transparent    = false;
        this.Clock          = 0.0;
    }

    // 📝 useEffect(() => { if (scan) sweepT.current = 0 }, [scan]): turning the scan on restarts the sweep; turning it off keeps the front where it was.
    Assign(Enabled)
    {
        if (Enabled && !this.Enabled)
        {
            this.SweepSeconds = 0.0;
        }
        this.Enabled = Enabled;
    }

    Advance(RawSeconds)
    {
        const Spec    = ScanSpecification;
        const Seconds = Math.min(RawSeconds, Spec.StepLimit);
        this.Clock   += RawSeconds;
        const Target  = this.Enabled ? 1.0 : 0.0;
        this.Ramp     = DampValue(this.Ramp, Target, Spec.RampRate, Seconds);
        if (Math.abs(this.Ramp - Target) < Spec.RampSettle)
        {
            this.Ramp = Target;
        }
        let CycleAmount = 1.0;
        let Sweeping    = false;
        if (this.Enabled)
        {
            this.SweepSeconds += Seconds;
            const Phase = this.SweepSeconds % Spec.Cycle;
            const Travel = Phase < Spec.Sweep ? EaseInOutCubic(Phase / Spec.Sweep) : 1.0;
            Sweeping = Phase < Spec.Sweep;
            this.Front = Spec.FrontStart + (Spec.FrontEnd - Spec.FrontStart) * Travel;
            CycleAmount = Phase < Spec.Sweep + Spec.Hold
                        ? 1.0
                        : Math.max(0.0, 1.0 - (Phase - Spec.Sweep - Spec.Hold) / (Spec.Cycle - Spec.Sweep - Spec.Hold));
        }
        this.CycleAmount = CycleAmount;
        this.Sweeping    = Sweeping;
        this.Amount      = this.Ramp * (this.Enabled ? CycleAmount : 1.0);
        this.Transparent = this.Ramp > Spec.SolidFloor;
    }

    // 📝 The curtain's two plane opacities: (0.075, 0.30) × ramp × cycle × (sweeping ? 1 : 0.25).
    ResolveCurtain()
    {
        const Spec = ScanSpecification;
        const Visibility = this.Ramp * this.CycleAmount * (this.Sweeping ? 1.0 : Spec.CurtainIdle);
        return Spec.CurtainOpacity.map((Opacity) => Opacity * Visibility);
    }

    ResolveEdgeOpacity()
    {
        return ScanSpecification.EdgeOpacity * this.Amount;
    }

    // 📝 Anything of the scan is drawn at all: the solid fade and the ghost need Amount, the curtain needs the ramp.
    get Visible()
    {
        return this.Amount > 0.0 || this.Ramp > 0.0;
    }
}
