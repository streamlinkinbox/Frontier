// Shared vehicle materials, paint/rim wave transitions and X-ray inspection state for the Garage, mirrors and world.
import { CourseMaterial, EncodeMaterialRecords, MaterialRecordBytes } from './MaterialSpecification.js';

export const PaintOptions = Object.freeze([
    { Id: 'silver', Name: 'Nebula Silver',  Hex: '#c2c8d0', Metallic: true },
    { Id: 'black',  Name: 'Obsidian Black', Hex: '#17191d', Metallic: true },
    { Id: 'red',    Name: 'Command Red',    Hex: '#8a1a1d', Metallic: true },
    { Id: 'blue',   Name: 'Tactical Blue',  Hex: '#1c3f73', Metallic: true },
    { Id: 'sand',   Name: 'Moab Sand',      Hex: '#b2946a', Metallic: false },
    { Id: 'green',  Name: 'Recon Green',    Hex: '#33442f', Metallic: false },
    { Id: 'white',  Name: 'Arctic White',   Hex: '#e7ebef', Metallic: true },
    { Id: 'copper', Name: 'Copper Bronze',  Hex: '#8c5a2e', Metallic: true }
]);

export const PaintColours = PaintOptions.map(Option => Option.Hex);

export const WheelOptions = Object.freeze([
    { Id: 'trail',    Name: 'Trail Beadlock', Size: '22"', Spokes: 6,  Hex: '#2b2e33', Machined: false },
    { Id: 'sport',    Name: 'Machined Sport', Size: '24"', Spokes: 5,  Hex: '#c9ced4', Machined: true },
    { Id: 'blackout', Name: 'Blackout Mesh',  Size: '22"', Spokes: 10, Hex: '#0e0f11', Machined: false },
    { Id: 'bronze',   Name: 'Bronze Forged',  Size: '24"', Spokes: 7,  Hex: '#9a6a2f', Machined: true }
]);

export const InspectionView = Object.freeze({
    Solid:     0,
    Ghost:     1,
    Wireframe: 2,
    Scan:      3
});

export const InspectionLabels = Object.freeze(['Solid', 'Ghost', 'Wireframe', 'X-Ray Scan']);

export function SmoothStep(Edge0, Edge1, Value) {
    const Span = Edge1 - Edge0;
    if (Math.abs(Span) < 1e-9) return Value >= Edge1 ? 1 : 0;
    const T = Math.max(0, Math.min(1, (Value - Edge0) / Span));
    return T * T * (3 - 2 * T);
}

export function EaseOutCubic(Fraction) {
    const K = Math.max(0, Math.min(1, Fraction));
    return 1 - (1 - K) ** 3;
}

export function EaseInOutCubic(Fraction) {
    const K = Math.max(0, Math.min(1, Fraction));
    return K < 0.5 ? 4 * K ** 3 : 1 - ((-2 * K + 2) ** 3) / 2;
}

export function ResolveRippleCoverage(Distance, Radius, Feather = 0.55) {
    if (Radius <= 0.001) return 0;
    return 1 - SmoothStep(Radius - Feather, Radius, Distance);
}

// 📐 Matches SurfaceReflectance.wgsl ResolvePaintWaveCoverage 1:1:
//    Mode 1 ("sweep") sweeps side-to-side from nose (+X) across to tail (-X);
//    Mode 2 ("radial") expands radially outwards in 3D from the dropped point until it fills the entire vehicle.
export function ResolvePaintWaveCoverage(Point, Ripple) {
    if (!Ripple || !Ripple.Active) return 0;
    if (Ripple.ModeCode === 1 || Ripple.Mode === 'sweep') {
        const Signed = (Point[0] - 0.08 * Math.abs(Point[1])) - Ripple.SweepFront;
        return SmoothStep(-0.55, 0.0, Signed);
    }
    const Dx = Point[0] - Ripple.Centre[0], Dy = Point[1] - Ripple.Centre[1], Dz = Point[2] - Ripple.Centre[2];
    return ResolveRippleCoverage(Math.hypot(Dx, Dy, Dz), Ripple.Radius, 0.55);
}

export function LinearColour(Hex) {
    if (!/^#[0-9a-f]{6}$/i.test(Hex)) throw new TypeError('Expected #RRGGBB');
    return [1, 3, 5].map(Start => {
        const Value = parseInt(Hex.slice(Start, Start + 2), 16) / 255;
        return Value <= 0.04045 ? Value / 12.92 : ((Value + 0.055) / 1.055) ** 2.4;
    });
}

export class VehicleAppearance {
    constructor(Materials, Upload) {
        this.Materials = Materials;
        this.Upload = Upload;
        this.Colour = null;
        this.Matte = false;
        this.Metallic = true;
        this.DarkWheels = false;
        this.BlueRims = true;
        this.WheelId = null;
        this.RimColour = null;
        this.MachinedWheels = false;
        this.View = InspectionView.Solid; // inspection only, never affects physics

        this.Ripple = {
            Active: false, Mode: 'sweep', ModeCode: 0,
            Centre: [3.10, 0.0, 0.35], Radius: 0, MaximumRadius: 6.2,
            SweepStart: 3.30, SweepEnd: -3.85, SweepFront: 3.30,
            Elapsed: 0, Duration: 1.35,
            Colour: null, Matte: false, Metallic: true,
            NextColour: [1, 1, 1], Roughness: 0.0775, Metalness: 0.0, CoatWeight: 1.0
        };

        this.WheelRipple = {
            Active: false, Centre: [1.72, 1.05, 0.0], Radius: 0, MaximumRadius: 5.8,
            Elapsed: 0, Duration: 1.40,
            WheelId: null, RimColour: null, Machined: false,
            NextColour: [0.5, 0.5, 0.5], Roughness: 0.22, Metalness: 1.0
        };

        this.Scan = {
            Elapsed: 0, Ramp: 0, HoloAmount: 0, ScanX: 3.25, Sweeping: false, Clock: 0
        };
    }

    // 📝 Tap/click (DropPoint = null) sweeps side-to-side across the vehicle; drag-drop (DropPoint = [x,y,z])
    //    expands radially outwards from the exact dropped position until the entire vehicle is covered.
    BeginPaintRipple(Colour, DropPoint = null, { Matte = this.Matte, Metallic } = {}) {
        const TargetHex = Colour ?? this.Colour;
        const TargetAlbedo = TargetHex !== null ? LinearColour(TargetHex) : [...this.Materials[CourseMaterial.BodyCoat].Albedo];
        if (this.Ripple.Active) {
            this.Apply({ Colour: this.Ripple.Colour, Matte: this.Ripple.Matte, Metallic: this.Ripple.Metallic });
        }
        const Option = TargetHex ? PaintOptions.find(Paint => Paint.Hex.toLowerCase() === TargetHex.toLowerCase()) : null;
        const IsMetallic = Metallic !== undefined ? !!Metallic : (Option ? Option.Metallic : this.Metallic);
        const HasDrop = Array.isArray(DropPoint) && DropPoint.length >= 3;
        if (TargetHex !== null) this.Colour = TargetHex;
        this.Matte = !!Matte;
        this.Metallic = IsMetallic;
        Object.assign(this.Ripple, {
            Active: true,
            Mode: HasDrop ? 'radial' : 'sweep',
            ModeCode: HasDrop ? 2 : 1,
            Centre: HasDrop ? [DropPoint[0], DropPoint[1], DropPoint[2]] : [3.10, 0.0, 0.35],
            Radius: 0,
            SweepFront: this.Ripple.SweepStart,
            Elapsed: 0,
            Colour: TargetHex,
            Matte: !!Matte,
            Metallic: IsMetallic,
            NextColour: TargetAlbedo,
            Roughness: Matte ? 0.7 : 0.0775,
            Metalness: (!Matte && IsMetallic) ? 0.24 : 0.0,
            CoatWeight: Matte ? 0.0 : 1.0
        });
    }

    AdvancePaintRipple(Seconds) {
        const Ripple = this.Ripple;
        if (!Ripple.Active) return false;
        Ripple.Elapsed += Math.max(0, Seconds);
        const K = Math.min(Ripple.Elapsed / Ripple.Duration, 1);
        const SweepEase = EaseOutCubic(K);
        const RadialEase = 1 - Math.pow(1 - K, 2.1);
        Ripple.Radius = RadialEase * Ripple.MaximumRadius;
        Ripple.SweepFront = Ripple.SweepStart + (Ripple.SweepEnd - Ripple.SweepStart) * SweepEase;
        if (K >= 1) {
            Ripple.Active = false;
            Ripple.ModeCode = 0;
            this.Apply({ Colour: Ripple.Colour, Matte: Ripple.Matte, Metallic: Ripple.Metallic });
            return true;
        }
        return false;
    }

    ResolvePaintCoverage(Point) {
        return ResolvePaintWaveCoverage(Point, this.Ripple);
    }

    BeginWheelRipple(RimColour, DropPoint = null, { Machined = false, WheelId = null } = {}) {
        const TargetAlbedo = LinearColour(RimColour);
        if (this.WheelRipple.Active) {
            this.Apply({ RimColour: this.WheelRipple.RimColour, MachinedWheels: this.WheelRipple.Machined, WheelId: this.WheelRipple.WheelId });
        }
        const HasDrop = Array.isArray(DropPoint) && DropPoint.length >= 3;
        if (WheelId) this.WheelId = WheelId;
        this.RimColour = RimColour;
        this.MachinedWheels = !!Machined;
        Object.assign(this.WheelRipple, {
            Active: true,
            Centre: HasDrop ? [DropPoint[0], DropPoint[1], DropPoint[2]] : [1.72, 1.05, 0.0],
            Radius: 0,
            Elapsed: 0,
            WheelId,
            RimColour,
            Machined: !!Machined,
            NextColour: TargetAlbedo,
            Roughness: Machined ? 0.18 : 0.42,
            Metalness: Machined ? 0.95 : 0.75
        });
    }

    AdvanceWheelRipple(Seconds) {
        const Run = this.WheelRipple;
        if (!Run.Active) return false;
        Run.Elapsed += Math.max(0, Seconds);
        const K = Math.min(Run.Elapsed / Run.Duration, 1);
        Run.Radius = EaseOutCubic(K) * Run.MaximumRadius;
        if (K >= 1) {
            Run.Active = false;
            this.Apply({ RimColour: Run.RimColour, MachinedWheels: Run.Machined, WheelId: Run.WheelId });
            return true;
        }
        return false;
    }

    SetInspectionView(View) {
        const Clamped = ((Math.round(View) % 4) + 4) % 4;
        if (Clamped === InspectionView.Scan && this.View !== InspectionView.Scan) {
            this.Scan.Elapsed = 0;
            this.Scan.ScanX = 3.25;
        }
        this.View = Clamped;
    }

    // 📝 Scan cycle (Truck.tsx): 4.6 s front-to-back sweep (+3.25 m → −3.35 m), 1.0 s full X-ray hold, 1.0 s fade back.
    AdvanceInspection(Seconds) {
        const Dt = Math.max(0, Seconds);
        const Scan = this.Scan;
        Scan.Clock += Dt;
        const Target = this.View !== InspectionView.Solid ? 1 : 0;
        Scan.Ramp += (Target - Scan.Ramp) * (1 - Math.exp(-4.5 * Dt));
        if (Math.abs(Scan.Ramp - Target) < 0.002) Scan.Ramp = Target;

        const Cycle = 6.6, Sweep = 4.6, Hold = 1.0;
        let CycleAmount = 1;
        Scan.Sweeping = false;
        if (this.View === InspectionView.Scan) {
            Scan.Elapsed += Dt;
            const Phase = Scan.Elapsed % Cycle;
            const Fraction = Phase < Sweep ? EaseInOutCubic(Phase / Sweep) : 1;
            Scan.Sweeping = Phase < Sweep;
            Scan.ScanX = 3.25 + (-3.35 - 3.25) * Fraction;
            CycleAmount = Phase < Sweep + Hold ? 1 : Math.max(0, 1 - (Phase - Sweep - Hold) / (Cycle - Sweep - Hold));
        } else if (this.View === InspectionView.Ghost || this.View === InspectionView.Wireframe) {
            Scan.ScanX = -10.0;
        }
        Scan.HoloAmount = Scan.Ramp * (this.View === InspectionView.Scan ? CycleAmount : 1);
    }

    Advance(Seconds) {
        const CommittedPaint = this.AdvancePaintRipple(Seconds);
        const CommittedWheel = this.AdvanceWheelRipple(Seconds);
        this.AdvanceInspection(Seconds);
        return CommittedPaint || CommittedWheel;
    }

    Apply(Options = {}) {
        const { Colour = this.Colour, Matte = this.Matte, Metallic, DarkWheels = this.DarkWheels, BlueRims = this.BlueRims,
                RimColour, MachinedWheels, WheelId } = Options;
        const ExplicitLegacyRim = Object.prototype.hasOwnProperty.call(Options, 'DarkWheels')
                               || Object.prototype.hasOwnProperty.call(Options, 'BlueRims');
        const Albedo = Colour === null ? null : LinearColour(Colour);
        const Body = this.Materials[CourseMaterial.BodyCoat];
        if (Albedo) Body.Albedo = Albedo;
        const Option = Colour ? PaintOptions.find(Paint => Paint.Hex.toLowerCase() === Colour.toLowerCase()) : null;
        const IsMetallic = Metallic !== undefined ? !!Metallic : (Option ? Option.Metallic : false);
        Body.Roughness = Matte ? 0.7 : 0.0775;
        Body.CoatWeight = Matte ? 0 : 1;
        Body.Metalness = (!Matte && IsMetallic && Option) ? 0.24 : 0;

        const Hub = this.Materials[CourseMaterial.Hub];
        if (RimColour !== undefined) {
            if (RimColour === null) {
                this.RimColour = null;
                this.WheelId = null;
            } else {
                const Machined = MachinedWheels !== undefined ? !!MachinedWheels : this.MachinedWheels;
                Hub.Albedo = LinearColour(RimColour);
                Hub.Emission = [0, 0, 0];
                Hub.Metalness = Machined ? 0.95 : 0.75;
                Hub.Roughness = Machined ? 0.18 : 0.42;
                this.RimColour = RimColour;
                this.MachinedWheels = Machined;
                this.WheelId = WheelId ?? this.WheelId;
            }
        }
        if (RimColour === undefined || RimColour === null) {
            if (RimColour === undefined && this.RimColour && !ExplicitLegacyRim) {
                Hub.Albedo = LinearColour(this.RimColour);
                Hub.Emission = [0, 0, 0];
                Hub.Metalness = this.MachinedWheels ? 0.95 : 0.75;
                Hub.Roughness = this.MachinedWheels ? 0.18 : 0.42;
            } else {
                this.RimColour = null;
                this.WheelId = null;
                Hub.Albedo = DarkWheels ? [0.025, 0.028, 0.032] : BlueRims ? [0.06, 0.22, 0.62] : [0.62, 0.63, 0.65];
                Hub.Emission = BlueRims && !DarkWheels ? [0, 0.03327756, 0.12] : [0, 0, 0];
                this.BlueRims = BlueRims;
                Hub.Metalness = 1;
                Hub.Roughness = DarkWheels ? 0.38 : 0.2;
                this.DarkWheels = DarkWheels;
            }
        }
        this.Colour = Colour;
        this.Matte = Matte;
        this.Metallic = IsMetallic;
        for (const Family of [CourseMaterial.BodyCoat, CourseMaterial.Hub])
            this.Upload(Family * MaterialRecordBytes, EncodeMaterialRecords([this.Materials[Family]]));
    }
}
