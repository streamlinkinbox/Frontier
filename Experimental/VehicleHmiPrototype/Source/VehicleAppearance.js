// Shared vehicle materials: the Garage, mirrors and world all use these records.
import { CourseMaterial, EncodeMaterialRecords, MaterialRecordBytes } from './MaterialSpecification.js';
// 📝 The paint shelf, from the reference configurator (interactive-vehicle-hmi-configurator):
//    name + finish. The HMI's only vehicle control for now — everything else about the car is fixed.
export const PaintOptions = Object.freeze([
    { Name: 'Nebula Silver', Hex: '#c2c8d0' }, { Name: 'Obsidian Black', Hex: '#17191d' },
    { Name: 'Command Red', Hex: '#8a1a1d' },   { Name: 'Tactical Blue', Hex: '#1c3f73' },
    { Name: 'Moab Sand', Hex: '#b2946a' },     { Name: 'Recon Green', Hex: '#33442f' },
    { Name: 'Arctic White', Hex: '#e7ebef' },  { Name: 'Copper Bronze', Hex: '#8c5a2e' }
]);
export const PaintColours = PaintOptions.map(Option => Option.Hex);
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
        this.DarkWheels = false;
        this.BlueRims = true;
        this.View = 0; // solid, ghost, wireframe, animated scan; inspection only, never affects physics
    }
    Apply({ Colour = this.Colour, Matte = this.Matte, DarkWheels = this.DarkWheels, BlueRims = this.BlueRims } = {}) {
        const Albedo = Colour === null ? null : LinearColour(Colour);
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
}
