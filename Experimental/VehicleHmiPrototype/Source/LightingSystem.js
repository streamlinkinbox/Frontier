//============================================================================================================================================
//                                                            LIGHTINGSYSTEM.JS
//============================================================================================================================================
// 🧩 World-space 3D clustered lighting (16x16x4 spatial grid with 64-bit per-cell light bitmasks) supporting omnidirectional
//    point lights, IES roadway street lamps, and automotive headlights/roof floodlights across both GTX and RTX pipelines.

import { EmitBoxFacets } from "./CourseStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

export const LightType = Object.freeze({
    Omni: 0,          // Omnidirectional point light (1/d² windowed falloff)
    StreetLamp: 1,    // Type II / Type III roadway batwing IES distribution
    LowBeam: 2,       // Automotive asymmetric low beam cutoff
    HighBeam: 3,      // Automotive collimated pencil beam
    Fog: 4,           // Low-mounted razor-sharp horizontal cutoff
    Floodlight: 5     // Broad multi-lens flood / roof bar
});

export const RoofFixtureType = Object.freeze({
    SlimLED: 0,
    RallyPod: 1
});

export const AutomotiveMode = Object.freeze({
    Off: 0,
    LowBeam: 1,
    HighBeam: 2,
    Fog: 3
});

export const MaxSceneLights = 64;
// 16 floats per light = 64 bytes
// PositionType:    vec4f [px, py, pz, type]
// DirectionReach:  vec4f [dx, dy, dz, reach]
// ColourIntensity: vec4f [r, g, b, intensity]
// Params:          vec4f [innerCos, outerCos, iesParam, active]
export const LightRecordFloats = 16;

// 16x16x4 spatial cluster grid over the proving grounds [-80..180] in X, [-80..80] in Y, [-2..28] in Z
export const ClusterGridBounds = Object.freeze({
    Minimum: [-80.0, -80.0, -2.0],
    Extent:  [260.0, 160.0, 30.0],
    ResX: 16,
    ResY: 16,
    ResZ: 4,
    CellCount: 1024,
    MaxLightsPerCell: 64
});

// Buffer layout (matches ClusteredLightBuffer in ClusteredLights.wgsl):
// - Lights:      64 * 16 = 1024 floats (4096 bytes)
// - GridMinimum: 4 floats (16 bytes) [.xyz = Minimum, .w = ActiveCount]
// - GridScale:   4 floats (16 bytes) [.xyz = Res / Extent, .w = CullEnabled]
// - CellMasks:   1024 cells * 2 u32 (vec2u 64-bit mask per cell, packed as 512 vec4u) = 2048 u32 (8192 bytes)
export const ClusterHeaderFloats = 8;
export const ClusterMaskWords    = ClusterGridBounds.CellCount * 2; // 2048 u32 words
export const LightBufferFloats   = MaxSceneLights * LightRecordFloats + ClusterHeaderFloats + ClusterMaskWords; // 3080 floats
export const LightBufferBytes    = LightBufferFloats * 4; // 12,320 bytes

// Default world positions of the 10 Point-Light Lamp Posts along the proving grounds
const DefaultPointLamps = Object.freeze([
    Object.freeze({ X: -38.0, Y:   7.78, Z: 5.35 }), // Start grid North-West
    Object.freeze({ X: -38.0, Y:  -7.78, Z: 5.35 }), // Start grid South-West
    Object.freeze({ X: -22.0, Y:   7.78, Z: 5.35 }), // Launch straight North
    Object.freeze({ X: -22.0, Y:  -7.78, Z: 5.35 }), // Launch straight South
    Object.freeze({ X:  -6.0, Y:   7.78, Z: 5.35 }), // Mid straight North
    Object.freeze({ X:  -6.0, Y:  -7.78, Z: 5.35 }), // Mid straight South
    Object.freeze({ X:  19.42, Y: 28.5,  Z: 5.35 }), // Hangar West apron
    Object.freeze({ X:  48.58, Y: 28.5,  Z: 5.35 }), // Hangar East apron
    Object.freeze({ X: -38.0, Y: -18.42, Z: 5.35 }), // Burnout box perimeter
    Object.freeze({ X:  12.0, Y: -18.42, Z: 5.35 })  // Smash alley corridor
]);

// Tight world-space AABB for a light's emission volume (sphere for Omni, downward hemisphere for StreetLamp, cone for beams)
function ResolveLightWorldBounds(Light) {
    const [Px, Py, Pz] = Light.Position;
    const R = Light.Reach;

    if (Light.Type === LightType.Omni) {
        return {
            MinX: Px - R, MinY: Py - R, MinZ: Pz - R,
            MaxX: Px + R, MaxY: Py + R, MaxZ: Pz + R
        };
    }

    if (Light.Type === LightType.StreetLamp) {
        return {
            MinX: Px - R, MinY: Py - R, MinZ: Pz - R,
            MaxX: Px + R, MaxY: Py + R, MaxZ: Pz + 0.5
        };
    }

    // Directional cone (LowBeam, HighBeam, Fog, Floodlight): exact analytical cone AABB
    const OuterCos = Math.max(0.0, Math.min(0.9999, Light.Params[1]));
    if (OuterCos <= 0.05) {
        return {
            MinX: Px - R, MinY: Py - R, MinZ: Pz - R,
            MaxX: Px + R, MaxY: Py + R, MaxZ: Pz + R
        };
    }
    const OuterSin = Math.sqrt(Math.max(0.0, 1.0 - OuterCos * OuterCos));
    const [Dx, Dy, Dz] = Light.Direction;
    const Ux = Math.sqrt(Math.max(0.0, 1.0 - Dx * Dx));
    const Uy = Math.sqrt(Math.max(0.0, 1.0 - Dy * Dy));
    const Uz = Math.sqrt(Math.max(0.0, 1.0 - Dz * Dz));

    const MinDirX = (-Dx >= OuterCos) ? -1.0 : (Dx * OuterCos - Ux * OuterSin);
    const MaxDirX = ( Dx >= OuterCos) ?  1.0 : (Dx * OuterCos + Ux * OuterSin);
    const MinDirY = (-Dy >= OuterCos) ? -1.0 : (Dy * OuterCos - Uy * OuterSin);
    const MaxDirY = ( Dy >= OuterCos) ?  1.0 : (Dy * OuterCos + Uy * OuterSin);
    const MinDirZ = (-Dz >= OuterCos) ? -1.0 : (Dz * OuterCos - Uz * OuterSin);
    const MaxDirZ = ( Dz >= OuterCos) ?  1.0 : (Dz * OuterCos + Uz * OuterSin);

    return {
        MinX: Px + R * Math.min(0.0, MinDirX),
        MaxX: Px + R * Math.max(0.0, MaxDirX),
        MinY: Py + R * Math.min(0.0, MinDirY),
        MaxY: Py + R * Math.max(0.0, MaxDirY),
        MinZ: Pz + R * Math.min(0.0, MinDirZ),
        MaxZ: Pz + R * Math.max(0.0, MaxDirZ)
    };
}

export class LightingSystem {
    constructor() {
        this.Lights = [];
        this.VehicleLightMode = AutomotiveMode.LowBeam;
        this.RoofFixture = RoofFixtureType.SlimLED;
        this.RoofLightEnabled = false;
        this.CullEnabled = true;
        this.ActiveClusterCells = 0;

        this.RawBuffer  = new ArrayBuffer(LightBufferBytes);
        this.BufferData = new Float32Array(this.RawBuffer);
        this.BufferU32  = new Uint32Array(this.RawBuffer);
        this.MaskOffset = MaxSceneLights * LightRecordFloats + ClusterHeaderFloats; // 1032
    }

    SetVehicleMode(Mode) {
        this.VehicleLightMode = Mode;
    }

    CycleVehicleMode() {
        this.VehicleLightMode = (this.VehicleLightMode + 1) % 4;
        return this.VehicleLightMode;
    }

    ToggleRoofLight() {
        this.RoofLightEnabled = !this.RoofLightEnabled;
        return this.RoofLightEnabled;
    }

    SetRoofFixture(Type) {
        this.RoofFixture = Type;
    }

    Update(VehicleTransform, Poles = [], PointLamps = DefaultPointLamps, TransientLight = null) {
        this.Lights = [];

        // 1. Roadway Street Lamp Poles (Type II/III batwing IES distribution)
        // Emitter sits 25 cm below PoleHead bottom face so shadow rays never self-occlude against the fixture housing
        for (const Pole of Poles) {
            if (this.Lights.length >= MaxSceneLights - 16) break;
            const Pos = [Pole.X, Pole.Y, (Pole.Z || 10.8) - 0.25];
            this.Lights.push({
                Type: LightType.StreetLamp,
                Position: Pos,
                Direction: [0.0, 0.0, -1.0],
                Colour: [1.0, 0.88, 0.65],
                Intensity: 45.0,
                Reach: 28.0,
                Params: [0.70, 0.20, 1.0, 1.0]
            });
        }

        // 2. Clustered Omnidirectional Point Lights on the LampPost / LampLantern fixtures
        const ActivePointLamps = PointLamps || DefaultPointLamps;
        for (let Index = 0; Index < ActivePointLamps.length; ++Index) {
            if (this.Lights.length >= MaxSceneLights - 4) break;
            const Lamp = ActivePointLamps[Index];
            const Pos = [Lamp.X, Lamp.Y, (Lamp.Z || 5.35) - 0.35];
            const IsCool = (Index & 1) === 1;
            this.Lights.push({
                Type: LightType.Omni,
                Position: Pos,
                Direction: [0.0, 0.0, -1.0],
                Colour: IsCool ? [0.80, 0.90, 1.0] : [1.0, 0.78, 0.48],
                Intensity: 42.0,
                Reach: 24.0,
                Params: [-1.0, -1.0, 0.0, 1.0]
            });
        }

        // 3. Automotive Headlights attached to the ControlVehicle
        if (VehicleTransform) {
            const M = VehicleTransform;
            const Fwd  = [M[0], M[1], M[2]];
            const Left = [M[4], M[5], M[6]];
            const Up   = [M[8], M[9], M[10]];
            const Pos  = [M[12], M[13], M[14]];

            const TransformLocal = (Offset) => [
                Pos[0] + Fwd[0] * Offset[0] + Left[0] * Offset[1] + Up[0] * Offset[2],
                Pos[1] + Fwd[1] * Offset[0] + Left[1] * Offset[1] + Up[1] * Offset[2],
                Pos[2] + Fwd[2] * Offset[0] + Left[2] * Offset[1] + Up[2] * Offset[2]
            ];

            // Offset 25 cm ahead of bumper surface (X = 2.45m) so shadow rays never self-occlude against the bumper
            const HeadlightL = TransformLocal([2.45, 0.65, 0.22]);
            const HeadlightR = TransformLocal([2.45, -0.65, 0.22]);

            if (this.VehicleLightMode === AutomotiveMode.LowBeam) {
                const LowBeam = (P) => ({
                    Type: LightType.LowBeam,
                    Position: P,
                    Direction: Fwd,
                    Colour: [0.92, 0.96, 1.0],
                    Intensity: 35.0,
                    Reach: 60.0,
                    Params: [0.90, 0.60, 0.0, 1.0]
                });
                this.Lights.push(LowBeam(HeadlightL), LowBeam(HeadlightR));
            } else if (this.VehicleLightMode === AutomotiveMode.HighBeam) {
                const HighBeam = (P) => ({
                    Type: LightType.HighBeam,
                    Position: P,
                    Direction: Fwd,
                    Colour: [0.95, 0.98, 1.0],
                    Intensity: 75.0,
                    Reach: 130.0,
                    Params: [0.98, 0.85, 0.0, 1.0]
                });
                this.Lights.push(HighBeam(HeadlightL), HighBeam(HeadlightR));
            } else if (this.VehicleLightMode === AutomotiveMode.Fog) {
                const FogL = TransformLocal([2.40, 0.70, 0.08]);
                const FogR = TransformLocal([2.40, -0.70, 0.08]);
                const FogBeam = (P) => ({
                    Type: LightType.Fog,
                    Position: P,
                    Direction: Fwd,
                    Colour: [1.0, 0.90, 0.50],
                    Intensity: 40.0,
                    Reach: 45.0,
                    Params: [0.85, 0.40, 0.0, 1.0]
                });
                this.Lights.push(FogBeam(FogL), FogBeam(FogR));
            }

            // 4. Roof Floodlight Component (Slim LED Lightbar or Rally Pod)
            if (this.RoofLightEnabled) {
                const RoofPos = TransformLocal([0.45, 0.0, 0.98]);
                if (this.RoofFixture === RoofFixtureType.SlimLED) {
                    this.Lights.push({
                        Type: LightType.Floodlight,
                        Position: RoofPos,
                        Direction: Fwd,
                        Colour: [0.90, 0.95, 1.0],
                        Intensity: 85.0,
                        Reach: 100.0,
                        Params: [Math.cos(30 * Math.PI / 180), Math.cos(45 * Math.PI / 180), 0.0, 1.0]
                    });
                } else {
                    const PodL = TransformLocal([0.45, 0.28, 0.98]);
                    const PodR = TransformLocal([0.45, -0.28, 0.98]);
                    const RallyBeam = (P) => ({
                        Type: LightType.Floodlight,
                        Position: P,
                        Direction: Fwd,
                        Colour: [1.0, 0.98, 0.92],
                        Intensity: 90.0,
                        Reach: 120.0,
                        Params: [Math.cos(22 * Math.PI / 180), Math.cos(38 * Math.PI / 180), 1.0, 1.0]
                    });
                    this.Lights.push(RallyBeam(PodL), RallyBeam(PodR));
                }
            }
        }

        // A bounded transient weather source shares real scene lighting; never changes exposure.
        if (TransientLight && this.Lights.length < MaxSceneLights) this.Lights.push(TransientLight);

        // Pack light records + 3D cluster grid header + 16x16x4 64-bit cell bitmasks
        this.BufferData.fill(0);
        const ActiveCount = Math.min(this.Lights.length, MaxSceneLights);
        for (let i = 0; i < ActiveCount; i++) {
            const L = this.Lights[i];
            const Offset = i * LightRecordFloats;
            this.BufferData[Offset + 0]  = L.Position[0];
            this.BufferData[Offset + 1]  = L.Position[1];
            this.BufferData[Offset + 2]  = L.Position[2];
            this.BufferData[Offset + 3]  = L.Type;

            this.BufferData[Offset + 4]  = L.Direction[0];
            this.BufferData[Offset + 5]  = L.Direction[1];
            this.BufferData[Offset + 6]  = L.Direction[2];
            this.BufferData[Offset + 7]  = L.Reach;

            this.BufferData[Offset + 8]  = L.Colour[0];
            this.BufferData[Offset + 9]  = L.Colour[1];
            this.BufferData[Offset + 10] = L.Colour[2];
            this.BufferData[Offset + 11] = L.Intensity;

            this.BufferData[Offset + 12] = L.Params[0];
            this.BufferData[Offset + 13] = L.Params[1];
            this.BufferData[Offset + 14] = L.Params[2];
            this.BufferData[Offset + 15] = L.Params[3];
        }

        const HeaderOffset = MaxSceneLights * LightRecordFloats;
        const { Minimum, Extent, ResX, ResY, ResZ } = ClusterGridBounds;
        this.BufferData[HeaderOffset + 0] = Minimum[0];
        this.BufferData[HeaderOffset + 1] = Minimum[1];
        this.BufferData[HeaderOffset + 2] = Minimum[2];
        this.BufferData[HeaderOffset + 3] = ActiveCount;

        this.BufferData[HeaderOffset + 4] = ResX / Extent[0];
        this.BufferData[HeaderOffset + 5] = ResY / Extent[1];
        this.BufferData[HeaderOffset + 6] = ResZ / Extent[2];
        this.BufferData[HeaderOffset + 7] = this.CullEnabled ? 1.0 : 0.0;

        this.BuildClusterGrid(ActiveCount);
        return this.BufferData;
    }

    BuildClusterGrid(ActiveCount) {
        const { Minimum, Extent, ResX, ResY, ResZ, CellCount } = ClusterGridBounds;
        const CellSizeX = Extent[0] / ResX;
        const CellSizeY = Extent[1] / ResY;
        const CellSizeZ = Extent[2] / ResZ;
        const InvCellX  = 1.0 / CellSizeX;
        const InvCellY  = 1.0 / CellSizeY;
        const InvCellZ  = 1.0 / CellSizeZ;
        const MaskBase  = this.MaskOffset;

        for (let i = 0; i < ActiveCount; i++) {
            const L = this.Lights[i];
            if (L.Params[3] < 0.5 || L.Reach <= 0.0) continue;

            const [Px, Py, Pz] = L.Position;
            const R   = L.Reach;
            const RSq = R * R;
            const Bounds = ResolveLightWorldBounds(L);

            const Ix0 = Math.max(0, Math.min(ResX - 1, Math.floor((Bounds.MinX - Minimum[0]) * InvCellX)));
            const Ix1 = Math.max(0, Math.min(ResX - 1, Math.floor((Bounds.MaxX - Minimum[0]) * InvCellX)));
            const Iy0 = Math.max(0, Math.min(ResY - 1, Math.floor((Bounds.MinY - Minimum[1]) * InvCellY)));
            const Iy1 = Math.max(0, Math.min(ResY - 1, Math.floor((Bounds.MaxY - Minimum[1]) * InvCellY)));
            const Iz0 = Math.max(0, Math.min(ResZ - 1, Math.floor((Bounds.MinZ - Minimum[2]) * InvCellZ)));
            const Iz1 = Math.max(0, Math.min(ResZ - 1, Math.floor((Bounds.MaxZ - Minimum[2]) * InvCellZ)));

            const WordOffset = i >= 32 ? 1 : 0;
            const Bit = (1 << (i & 31)) >>> 0;

            for (let iz = Iz0; iz <= Iz1; iz++) {
                const CellMinZ = Minimum[2] + iz * CellSizeZ;
                const CellMaxZ = CellMinZ + CellSizeZ;
                const Dz = Pz < CellMinZ ? (CellMinZ - Pz) : (Pz > CellMaxZ ? (Pz - CellMaxZ) : 0.0);
                const DzSq = Dz * Dz;
                if (DzSq > RSq) continue;

                for (let iy = Iy0; iy <= Iy1; iy++) {
                    const CellMinY = Minimum[1] + iy * CellSizeY;
                    const CellMaxY = CellMinY + CellSizeY;
                    const Dy = Py < CellMinY ? (CellMinY - Py) : (Py > CellMaxY ? (Py - CellMaxY) : 0.0);
                    const DyDzSq = DzSq + Dy * Dy;
                    if (DyDzSq > RSq) continue;

                    const RowBase = MaskBase + ((iz * ResY + iy) * ResX) * 2 + WordOffset;
                    for (let ix = Ix0; ix <= Ix1; ix++) {
                        const CellMinX = Minimum[0] + ix * CellSizeX;
                        const CellMaxX = CellMinX + CellSizeX;
                        const Dx = Px < CellMinX ? (CellMinX - Px) : (Px > CellMaxX ? (Px - CellMaxX) : 0.0);
                        if (DyDzSq + Dx * Dx <= RSq) {
                            this.BufferU32[RowBase + ix * 2] |= Bit;
                        }
                    }
                }
            }
        }

        let ActiveCells = 0;
        for (let c = 0; c < CellCount; c++) {
            if ((this.BufferU32[MaskBase + c * 2] | this.BufferU32[MaskBase + c * 2 + 1]) !== 0) {
                ActiveCells++;
            }
        }
        this.ActiveClusterCells = ActiveCells;
    }

    QueryClusterMask(X, Y, Z) {
        const { Minimum, Extent, ResX, ResY, ResZ } = ClusterGridBounds;
        const Rx = (X - Minimum[0]) * (ResX / Extent[0]);
        const Ry = (Y - Minimum[1]) * (ResY / Extent[1]);
        const Rz = (Z - Minimum[2]) * (ResZ / Extent[2]);
        if (Rx < 0.0 || Rx >= ResX || Ry < 0.0 || Ry >= ResY || Rz < 0.0 || Rz >= ResZ) {
            return [0, 0];
        }
        const Ix = Math.max(0, Math.min(ResX - 1, Math.floor(Rx)));
        const Iy = Math.max(0, Math.min(ResY - 1, Math.floor(Ry)));
        const Iz = Math.max(0, Math.min(ResZ - 1, Math.floor(Rz)));
        const Flat = (Iz * ResY + Iy) * ResX + Ix;
        const Base = this.MaskOffset + Flat * 2;
        return [this.BufferU32[Base] >>> 0, this.BufferU32[Base + 1] >>> 0];
    }
}

export function ConstructRoofFixtureFacets(Type) {
    const Result = [];
    const Rebase = ResolveCoMAuthoredZ();
    const Box = (Centre, Size, Family) => {
        for (const F of EmitBoxFacets(...Size, Family)) {
            Result.push({
                Family,
                Corners: F.Corners.map(P => [
                    P[0] + Centre[0],
                    P[1] + Centre[1],
                    P[2] + Centre[2] - Rebase
                ])
            });
        }
    };

    if (Type === RoofFixtureType.SlimLED) {
        Box([0.25, 0.44, 1.35], [0.10, 0.04, 0.05], CourseMaterial.SteelDark);
        Box([0.25, -0.44, 1.35], [0.10, 0.04, 0.05], CourseMaterial.SteelDark);
        Box([0.25, 0.0, 1.38], [0.12, 0.94, 0.07], CourseMaterial.VehiclePlastic);
        Box([0.31, 0.0, 1.38], [0.02, 0.88, 0.05], CourseMaterial.Headlight);
    } else {
        for (const Y of [0.32, -0.32]) {
            Box([0.25, Y, 1.35], [0.12, 0.08, 0.05], CourseMaterial.SteelDark);
            Box([0.25, Y, 1.40], [0.16, 0.20, 0.16], CourseMaterial.VehiclePlastic);
            Box([0.33, Y, 1.40], [0.03, 0.16, 0.14], CourseMaterial.Headlight);
        }
    }
    return Result;
}
