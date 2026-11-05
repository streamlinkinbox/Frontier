//============================================================================================================================================
//                                                              PAINTRIPPLE.JS
//============================================================================================================================================
// 🧩 The configurator's paint transition, ported value for value from interactive-vehicle-hmi-configurator.zip (Truck.tsx, "circular material
//    ripple runtime" and makeScannable()): an expanding sphere about the drop point turns the body coat's colour, roughness and metalness
//    from the finish it had into the one chosen; when the sphere has grown to its full radius the finish is committed.
//
//    The sphere itself is evaluated by the shaders (ApplyPaintRipple in ViewWorld.wgsl) from the twelve floats this module writes into the
//    frame record, so every view of the car — the studio camera, the Garage preview — shows the same wave.

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

export const PaintRippleSpecification = Object.freeze({
    Seconds:   1.35,   // [s]  Truck.tsx beginRun(…, 1.35, 6.4)
    Radius:    6.4,    // [m]  the radius the wave reaches
    Softness:  0.55,   // [m]  the soft edge: smoothstep(R − 0.55, R, distance)
    StepLimit: 0.05    // [s]  useFrame: dt = Math.min(dtRaw, 0.05)
});

export const GlossFinish = "gloss";
export const MatteFinish = "matte";

// 📐 THREE.Color.set(hex): sRGB to linear, the reference's own conversion (ColorManagement.enabled), no headroom applied.
export function ConvertHexToLinear(Hex)
{
    if (!/^#[0-9a-f]{6}$/i.test(Hex))
    {
        throw new TypeError("Expected #RRGGBB");
    }
    return [1, 3, 5].map((Start) =>
    {
        const Value = parseInt(Hex.slice(Start, Start + 2), 16) / 255;
        return Value <= 0.04045 ? Value / 12.92 : ((Value + 0.055) / 1.055) ** 2.4;
    });
}

// 📝 What a paint option becomes in a finish (Truck.tsx, the "paint" branch of the apply request):
//    roughness  matte 0.72, otherwise metallic 0.26, otherwise 0.45;   metalness  metallic 0.9, otherwise 0.35;
//    clearcoat  matte 0.1 over roughness 0.65, otherwise 1 over 0.12 (set when the ripple ends: the reference's `after`).
export function ResolvePaintTarget(Option, Finish)
{
    const Matte = Finish === MatteFinish;
    return {
        Albedo:        ConvertHexToLinear(Option.Hex),
        Roughness:     Matte ? 0.72 : (Option.Metallic ? 0.26 : 0.45),
        Metalness:     Option.Metallic ? 0.9 : 0.35,
        CoatWeight:    Matte ? 0.1 : 1.0,
        CoatRoughness: Matte ? 0.65 : 0.12
    };
}

// 📐 easeOutCubic: 1 − (1 − k)³.
export function ResolveRippleRadius(Elapsed, Seconds, Radius)
{
    const Fraction = Math.min(Elapsed / Seconds, 1.0);
    return (1.0 - Math.pow(1.0 - Fraction, 3.0)) * Radius;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

export class PaintRipple
{
    // Commit(Target) writes the finished finish into the body coat's material record.
    constructor(Commit)
    {
        this.Commit   = Commit;
        this.Active   = false;
        this.Elapsed  = 0.0;
        this.Seconds  = PaintRippleSpecification.Seconds;
        this.Radius   = PaintRippleSpecification.Radius;
        this.Centre   = [0.0, 0.0, 0.0];
        this.Target   = null;
        this.Reached  = 0.0;   // [m] the wave's present radius
        this.Finished = 0;     // [-] ripples that ran to their end (a test and a readout)
    }

    // 📝 A new run commits the one in flight first ("commit any in-flight ripple instantly"), then starts from radius 0 at the new centre.
    Begin(Centre, Target, Seconds = PaintRippleSpecification.Seconds, Radius = PaintRippleSpecification.Radius)
    {
        if (this.Active)
        {
            this.Complete();
        }
        this.Target  = Target;
        this.Seconds = Seconds;
        this.Radius  = Radius;
        this.Centre  = Centre.slice();
        this.Elapsed = 0.0;
        this.Reached = 0.0;
        this.Active  = true;
    }

    Advance(Seconds)
    {
        if (!this.Active)
        {
            return;
        }
        this.Elapsed += Math.min(Seconds, PaintRippleSpecification.StepLimit);
        this.Reached  = ResolveRippleRadius(this.Elapsed, this.Seconds, this.Radius);
        if (this.Elapsed / this.Seconds >= 1.0)
        {
            this.Complete();
        }
    }

    Complete()
    {
        if (this.Target)
        {
            this.Commit(this.Target);
        }
        this.Active  = false;
        this.Reached = 0.0;
        this.Finished++;
    }

    // 📝 FrameRecord.RippleCentre, RippleNext, RippleFinish (SceneRecords.wgsl): twelve floats. The radius is zero while no run is active,
    //    which the shader reads as "no ripple".
    Encode(Scalars, Offset)
    {
        const Next = this.Target || { Albedo: [0.0, 0.0, 0.0], Roughness: 0.0, Metalness: 0.0 };
        Scalars.set([this.Centre[0], this.Centre[1], this.Centre[2], this.Active ? this.Reached : 0.0,
                     Next.Albedo[0], Next.Albedo[1], Next.Albedo[2], Next.Roughness,
                     Next.Metalness, PaintRippleSpecification.Softness, 0.0, 0.0], Offset);
    }
}
