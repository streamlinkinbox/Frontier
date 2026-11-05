// Shared vehicle materials: the Garage, mirrors and world all use these records.
import { CourseMaterial, EncodeMaterialRecords, MaterialRecordBytes } from './MaterialSpecification.js';
import { PaintRipple, ConvertHexToLinear, ResolvePaintTarget, GlossFinish, MatteFinish } from './PaintRipple.js';
// 📝 The paint shelf, from the reference configurator (interactive-vehicle-hmi-configurator, data/config.ts COLORS): name, hex and whether the
//    pigment is metallic (metalness 0.9, roughness 0.26) or solid (metalness 0.35, roughness 0.45). The HMI's only vehicle control for now.
export const PaintOptions = Object.freeze([
    { Name: 'Nebula Silver', Hex: '#c2c8d0', Metallic: true },  { Name: 'Obsidian Black', Hex: '#17191d', Metallic: true },
    { Name: 'Command Red', Hex: '#8a1a1d', Metallic: true },    { Name: 'Tactical Blue', Hex: '#1c3f73', Metallic: true },
    { Name: 'Moab Sand', Hex: '#b2946a', Metallic: false },     { Name: 'Recon Green', Hex: '#33442f', Metallic: false },
    { Name: 'Arctic White', Hex: '#e7ebef', Metallic: true },   { Name: 'Copper Bronze', Hex: '#8c5a2e', Metallic: true }
]);
export const PaintColours = PaintOptions.map(Option => Option.Hex);
export const LinearColour = ConvertHexToLinear;
export class VehicleAppearance {
    constructor(Materials, Upload) {
        this.Materials = Materials;
        this.Upload = Upload;
        this.Colour = null;
        this.Matte = false;
        this.Finish = GlossFinish;
        this.DarkWheels = false;
        this.BlueRims = true;
        this.View = 0; // solid, ghost, wireframe, animated scan; inspection only, never affects physics
        // 📝 The configurator's paint transition. The ripple outlives the Garage: closing the panel mid-wave lets it finish on the car.
        this.Ripple = new PaintRipple((Target) => this.Commit(Target));
    }
    Apply({ Colour = this.Colour, Matte = this.Matte, DarkWheels = this.DarkWheels, BlueRims = this.BlueRims } = {}) {
        const Albedo = Colour === null ? null : ConvertHexToLinear(Colour);
        const Body = this.Materials[CourseMaterial.BodyCoat];
        if (Albedo) Body.Albedo = Albedo;
        Body.Roughness = Matte ? 0.7 : 0.0775;
        Body.CoatWeight = Matte ? 0 : 1;
        const Hub = this.Materials[CourseMaterial.Hub];
        Hub.Albedo = DarkWheels ? [0.025, 0.028, 0.032] : BlueRims ? [0.06, 0.22, 0.62] : [0.62, 0.63, 0.65];
        Hub.Emission = BlueRims && !DarkWheels ? [0, 0.03327756, 0.12] : [0,0,0];
        this.BlueRims = BlueRims;
        Hub.Metalness = 1;
        Hub.Roughness = DarkWheels ? 0.38 : 0.2;
        this.Colour = Colour; this.Matte = Matte; this.DarkWheels = DarkWheels;
        for (const Family of [CourseMaterial.BodyCoat, CourseMaterial.Hub])
            this.Upload(Family * MaterialRecordBytes, EncodeMaterialRecords([this.Materials[Family]]));
    }
    // 📝 A paint change as the reference does it: the choice shows on the panel at once, the car changes by a ripple from Centre (world, [m]).
    Paint(Option, Finish, Centre) {
        this.Option = Option;
        this.Colour = Option.Hex;
        this.Finish = Finish === MatteFinish ? MatteFinish : GlossFinish;
        this.Matte = this.Finish === MatteFinish;
        this.Ripple.Begin(Centre, ResolvePaintTarget(Option, this.Finish));
    }
    Advance(Seconds) {
        this.Ripple.Advance(Seconds);
    }
    // 📝 The end of a ripple: colour, roughness and metalness (finishRun) and the clear coat (the `after` callback) become the body coat's own.
    Commit(Target) {
        const Body = this.Materials[CourseMaterial.BodyCoat];
        Body.Albedo = Target.Albedo.slice();
        Body.Roughness = Target.Roughness;
        Body.Metalness = Target.Metalness;
        Body.CoatWeight = Target.CoatWeight;
        Body.CoatRoughness = Target.CoatRoughness;
        this.Upload(CourseMaterial.BodyCoat * MaterialRecordBytes, EncodeMaterialRecords([Body]));
    }
}
