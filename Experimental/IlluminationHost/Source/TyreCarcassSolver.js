//============================================================================================================================================
//                                                          TYRECARCASSSOLVER.JS
//============================================================================================================================================
// 🧩 Soft tyres: a flexible-ring carcass per wheel — inflation tension, sidewall support, belt bending — against the road, for the vertex stage.

// 📝 XPBD gives a convincing tyre at a cost no frame can carry, and a deformation authored in the material shader is
//    cheap but knows nothing: no pressure, no flat, no wave. Between the two sits the model tyre engineers reduce a
//    carcass to — the flexible ring on an elastic foundation (Gong; Pacejka, Tyre and Vehicle Dynamics, ch. 9; the
//    belt FTire and CDTire discretise). Per wheel, 96 nodes of belt in the wheel's own frame, each with a radial
//    deflection w, governed by
//
//        ρA·ẅ = T(p)·∂²w/∂s² − EI·∂⁴w/∂s⁴ − k(p)·w − c·ẇ ,   s = R·φ
//
//    | Term     | What it is                                    | Pressure                                   |
//    |----------|-----------------------------------------------|--------------------------------------------|
//    | T(p)     | hoop tension of the belt                      | inflation: T = p·R·w, plus the cords'       |
//    | k(p)     | radial support per metre of belt              | the air column plus the sidewalls' own     |
//    | EI       | bending of the steel belt                     | none                                       |
//
//    The road is a one-sided constraint: a node cannot sit below the ground under the hub, so the belt flattens into a
//    contact patch, and tension and bending carry the deflection out beyond it the way a real tread rolls in and out
//    of the patch. The nodes ride with the rubber (the wheel's frame spins), so nothing is advected: the patch walks
//    round the belt as the wheel turns, and at speed — when the tread outruns the belt's wave speed √(T/ρA) — a
//    standing wave trails the patch: about 210 km/h inflated, about 55 km/h flat.
//
//    The dynamics (VehicleSolver.js) carry the same tyre as a spring in series with each strut, K = K₀ + κ·p; this
//    ring is what the eye sees of it. Ported from the GI-Updated-Html-Version branch's TyreCarcassSolver, with two
//    additions: a shredded tyre (the rubber gone, the wheel on its rim) and a visual exaggeration of the deflection
//    for inspection — the dynamics never see the exaggeration.

export const CarcassNodes       = 96;                          // [-]
export const CarcassRecordBytes = 3 * 16 + CarcassNodes * 4;   // [B]  three vec4 of header, then the 96 deflections
export const VehicleHeaderBytes = 5 * 16;                      // [B]  VehicleRecord.Cabin, .Column, .Axis, .CabinFrame, .CabinShift
export const VehicleRecordBytes = VehicleHeaderBytes + 4 * CarcassRecordBytes;

export const CarcassSpecification = Object.freeze({
    BeltMassPerLength:   8.0,       // [kg/m]  belt and tread per metre of circumference — heavier than real for a stable step
    TensionPerPascal:    0.12,      // [N/Pa]  p·R·w with R 0.5 m and an effective width of 0.24 m
    ResidualTension:     1800.0,    // [N]     the cords and rubber, no air
    FoundationPerPascal: 7.5,       // [N/m²/Pa] radial support of the air column per metre of belt
    SidewallFoundation:  2.6e5,     // [N/m²]  the sidewalls' own radial support
    BendingStiffness:    4.0,       // [N·m²]  the steel belt
    DampingRatio:        0.55,      // [-]     of the foundation mode
    StepSeconds:         1 / 3600,  // [s]     the highest ring mode at 300 kPa is ~4900 rad/s, the step's limit 7200
    MaximumSteps:        150,       // [-]     per advance
    PinchClearance:      0.022,     // [m]     rubber left between rim flange and road when the tyre is pinched
    LateralStiffness:    6.0e4,     // [N/m]   sidewall lateral stiffness, no air
    LateralPerPascal:    0.6,       // [N/m/Pa]
    LateralCeiling:      0.045,     // [m]
    LateralSpread:       0.65,      // [rad]   half-width of the belt's lateral shift round the patch
    ReferencePressure:   220000.0   // [Pa]
});

export class TyreCarcassSolver
{
    constructor(Radius, RimRadius, HalfWidth)
    {
        this.Radius    = Radius;
        this.RimRadius = RimRadius;
        this.HalfWidth = HalfWidth;
        this.Wheels = [0, 1, 2, 3].map((Index) => ({
            Deflection: new Float64Array(CarcassNodes), Velocity: new Float64Array(CarcassNodes), Floor: new Float64Array(CarcassNodes),
            DownAngle: -Math.PI / 2, HubDistance: 0.0, LateralShift: 0.0, Bulge: 0.0, PressureRatio: 1.0, Shredded: false,
            Pinched: false, PatchNodes: 0, Peak: 0.0, WavePeak: 0.0
        }));
        this.Accumulator = 0.0;
        this.Steps = 0;
        this.Bytes = new ArrayBuffer(VehicleRecordBytes);
    }

    // 📝 Contacts: per wheel { InContact, HubDistance [m], DownAngle [rad, wheel frame], Pressure [Pa gauge],
    //    LateralForce [N], Shredded }. Advances the four rings by fixed steps over Seconds.
    Advance(Contacts, Seconds)
    {
        const Specification = CarcassSpecification;
        this.Accumulator = Math.min(this.Accumulator + Math.max(0.0, Seconds), Specification.StepSeconds * Specification.MaximumSteps);
        const Steps = Math.floor(this.Accumulator / Specification.StepSeconds);
        this.Accumulator -= Steps * Specification.StepSeconds;
        this.Steps = Steps;
        Contacts.forEach((Contact, Index) => this.AdvanceWheel(this.Wheels[Index], Contact, Steps));
    }

    AdvanceWheel(Wheel, Contact, Steps)
    {
        const Specification = CarcassSpecification;
        const Pressure = Math.max(0.0, Contact.Pressure);
        const Tension = Specification.ResidualTension + Specification.TensionPerPascal * Pressure;
        const Foundation = Specification.SidewallFoundation + Specification.FoundationPerPascal * Pressure;
        const Mass = Specification.BeltMassPerLength;
        const Damping = 2.0 * Specification.DampingRatio * Math.sqrt(Foundation * Mass);
        const Arc = this.Radius * 2.0 * Math.PI / CarcassNodes;
        const TensionTerm = Tension / (Arc * Arc), BendingTerm = Specification.BendingStiffness / (Arc * Arc * Arc * Arc);
        const Step = Specification.StepSeconds;
        const Pinch = this.Radius - this.RimRadius - Specification.PinchClearance;
        const W = Wheel.Deflection, V = Wheel.Velocity, Floor = Wheel.Floor;
        const Count = CarcassNodes;
        Wheel.DownAngle = Contact.DownAngle;
        Wheel.HubDistance = Contact.InContact && Number.isFinite(Contact.HubDistance) ? Contact.HubDistance : 0.0;
        Wheel.Pinched = false;
        Wheel.Shredded = !!Contact.Shredded;
        if (Wheel.Shredded)
        {
            W.fill(0.0);
            V.fill(0.0);
            Wheel.PatchNodes = 0;
            Wheel.Peak = 0.0;
            Wheel.WavePeak = 0.0;
            Wheel.LateralShift = 0.0;
            Wheel.PressureRatio = 0.0;
            return;
        }
        // ① the road under the hub as a floor for each node: r·cos θ ≤ d, so w ≥ R − d / cos θ near the patch
        for (let Node = 0; Node < Count; ++Node)
        {
            Floor[Node] = -Infinity;
            if (!Contact.InContact)
                continue;
            let θ = (2.0 * Math.PI * Node) / Count - Contact.DownAngle;
            θ = Math.atan2(Math.sin(θ), Math.cos(θ));
            if (Math.abs(θ) < 1.2)
                Floor[Node] = this.Radius - Contact.HubDistance / Math.cos(θ);
        }
        // ② semi-implicit steps of the ring, the floor and the pinch projected after each
        for (let Iteration = 0; Iteration < Steps; ++Iteration)
        {
            for (let Node = 0; Node < Count; ++Node)
            {
                const P2 = W[(Node + Count - 2) % Count], P1 = W[(Node + Count - 1) % Count], C = W[Node];
                const N1 = W[(Node + 1) % Count], N2 = W[(Node + 2) % Count];
                const Force = TensionTerm * (N1 - 2.0 * C + P1) - BendingTerm * (N2 - 4.0 * N1 + 6.0 * C - 4.0 * P1 + P2)
                            - Foundation * C - Damping * V[Node];
                V[Node] += Force / Mass * Step;
            }
            for (let Node = 0; Node < Count; ++Node)
            {
                W[Node] += V[Node] * Step;
                if (W[Node] < Floor[Node])
                {
                    W[Node] = Floor[Node];
                    if (V[Node] < 0.0)
                        V[Node] = 0.0;
                }
                if (W[Node] > Pinch)
                {
                    W[Node] = Pinch;
                    if (V[Node] > 0.0)
                        V[Node] = 0.0;
                    if (Floor[Node] > Pinch)
                        Wheel.Pinched = true;
                }
            }
        }
        // ③ what the shader reads besides the ring: lateral shift and the sidewalls' bulge gain
        //    The bulge is √(3·h·w/8) × gain, held to the 0.36·h an inextensible arc can reach (TyreCarcass.wgsl): the
        //    air carries most of an inflated tyre's load and its sidewalls bow out a quarter of that, about 7 mm at the
        //    static 14 mm; empty, the walls take it all — about 6 cm at a 64 mm flat.
        const Ratio = Math.min(1.5, Pressure / Specification.ReferencePressure);
        const Lateral = Specification.LateralStiffness + Specification.LateralPerPascal * Pressure;
        const Shift = Contact.InContact ? Math.max(-Specification.LateralCeiling, Math.min(Specification.LateralCeiling, (Contact.LateralForce || 0.0) / Lateral)) : 0.0;
        Wheel.LateralShift += (Shift - Wheel.LateralShift) * 0.35;
        Wheel.PressureRatio = Ratio;
        Wheel.Bulge = 0.25 + 0.75 * Math.max(0.0, 1.0 - Ratio);
        let Peak = 0.0, Patch = 0, Wave = 0.0;
        for (let Node = 0; Node < Count; ++Node)
        {
            Peak = Math.max(Peak, W[Node]);
            if (W[Node] <= Floor[Node] + 1.0e-5)
                ++Patch;
            Wave = Math.max(Wave, Math.abs(W[Node]) * (Floor[Node] === -Infinity ? 1.0 : 0.0));
        }
        Wheel.Peak = Peak;
        Wheel.PatchNodes = Patch;
        Wheel.WavePeak = Wave;
    }

    // 📝 The VehicleRecord TyreCarcass.wgsl reads: the cabin header (Header: { SteeringAngle, SeeThroughGlass,
    //    Exaggeration, Lamp, Column [xyz], Axis [xyz], Cockpit { Anchor, Scale, Offset } }) then four carcass records —
    //    shape, pose, contact (hub height over the road, pressure ratio), 96 deflections. Deformation off sends
    //    the rings flat and drops the road floor (the tyres draw round) while the dynamics keep their soft tyre.
    EncodeRecord(Header, Deform = true)
    {
        const Scalars = new Float32Array(this.Bytes);
        const Cockpit = Header.Cockpit || { Anchor: [0.0, 0.0, 0.0], Scale: 1.0, Offset: [0.0, 0.0, 0.0] };
        Scalars.set([Header.SteeringAngle || 0.0, Header.SeeThroughGlass ? 1.0 : 0.0, Header.Exaggeration ?? 1.0, Header.Lamp ?? 0.0], 0);
        Scalars.set([...(Header.Column || [0.0, 0.0, 0.0]), Header.ScanX ?? 3.2], 4);
        Scalars.set([...(Header.Axis || [1.0, 0.0, 0.0]), Header.HoloAmount ?? 0.0], 8);
        Scalars.set([...Cockpit.Anchor, Cockpit.Scale], 12);
        Scalars.set([...Cockpit.Offset, Header.WireframeWeight ?? 0.75], 16);
        this.Wheels.forEach((Wheel, Index) =>
        {
            const Offset = (VehicleHeaderBytes + Index * CarcassRecordBytes) / 4;
            Scalars.set([this.Radius, this.RimRadius, this.HalfWidth, Wheel.Shredded ? 1.0 : 0.0], Offset);
            Scalars.set([Wheel.DownAngle, Deform ? Wheel.LateralShift : 0.0, CarcassSpecification.LateralSpread, Wheel.Bulge], Offset + 4);
            Scalars.set([Deform ? Wheel.HubDistance : 0.0, 0.0, 0.0, Wheel.PressureRatio], Offset + 8);
            for (let Node = 0; Node < CarcassNodes; ++Node)
                Scalars[Offset + 12 + Node] = Deform ? Wheel.Deflection[Node] : 0.0;
        });
        return this.Bytes;
    }

    // 📝 Readings for the inspector and the HMI: patch length [m], peak deflection [m], standing-wave amplitude [m].
    QueryReadings()
    {
        const Arc = this.Radius * 2.0 * Math.PI / CarcassNodes;
        return this.Wheels.map((Wheel) => ({ Patch: Wheel.PatchNodes * Arc, Peak: Wheel.Peak, Wave: Wheel.WavePeak, Pinched: Wheel.Pinched,
                                             Shredded: Wheel.Shredded, Ratio: Wheel.PressureRatio }));
    }

    Reset()
    {
        for (const Wheel of this.Wheels)
        {
            Wheel.Deflection.fill(0.0);
            Wheel.Velocity.fill(0.0);
        }
    }
}
