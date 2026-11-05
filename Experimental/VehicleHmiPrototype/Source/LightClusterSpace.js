//============================================================================================================================================
//                                                          LIGHTCLUSTERSPACE.JS
//============================================================================================================================================
// 🧩 Light records binned into a world-space grid: each cell's 32-bit mask names the lights whose reach sphere touches the cell.

import { MaxSceneLights, LightRecordFloats } from "./LightingSystem.js";
import { CourseConstants } from "./CourseStructure.js";
import { PhotometryBytes } from "./Photometry.js";

// 📝 The emissive region (EmissiveLightSpace.js fills it): a per-set triangle table, a cluster-instance table and the cell lists, appended after the photometric atlas.
export const EmissiveCapacity = Object.freeze({ Triangles: 160, Clusters: 224, Slots: 8 });
export const EmissiveTriangleStride = 48, EmissiveClusterStride = 32, EmissiveCellStride = 16;

// 📝 The grid covers the asphalt pad the lights stand on (CourseStructure's pad). Points outside it take every light
//    (LightClusterSpace.wgsl), so a light is never skipped; the cost is only the lost culling there.
const PadMinimum = CourseConstants.PadMinimum;
const PadMaximum = CourseConstants.PadMaximum;
export const LightClusterBounds = Object.freeze({
    Minimum:    Object.freeze([PadMinimum[0], PadMinimum[1], -2.0]),                                 // [m]
    Extent:     Object.freeze([PadMaximum[0] - PadMinimum[0], PadMaximum[1] - PadMinimum[1], 30.0]), // [m]
    Resolution: Object.freeze([16, 16, 4])                                                           // cells along x, y, z
});

const CellCount    = LightClusterBounds.Resolution[0] * LightClusterBounds.Resolution[1] * LightClusterBounds.Resolution[2];
const HeaderFloats = 8;                                // Minimum and Extent, each a vec4f
const LightFloats  = MaxSceneLights * LightRecordFloats;
const DynamicBytes = (HeaderFloats + LightFloats + CellCount) * 4;   // header, lights and masks: rewritten every frame
export const PhotometryOffset = DynamicBytes;                         // the atlas follows, uploaded once
export const EmissiveTrianglesOffset = DynamicBytes + PhotometryBytes;
export const EmissiveClustersOffset  = EmissiveTrianglesOffset + EmissiveCapacity.Triangles * EmissiveTriangleStride;
export const EmissiveCellsOffset     = EmissiveClustersOffset + EmissiveCapacity.Clusters * EmissiveClusterStride;
export const LightClusterBytes       = EmissiveCellsOffset + CellCount * EmissiveCellStride;   // under the 64 KiB uniform binding limit (tests/Photometry.test.mjs)

// 📝 Layout of LightClusterRecords (LightClusterSpace.wgsl): Minimum, Extent, Lights[32], CellMasks[1024], the photometric Atlas — one buffer (uniform in the ray-tracing
//    group, storage in the card relighting). Minimum.w is 1 once the atlas is uploaded.
export class LightClusterSpace
{
    constructor(Exchange)
    {
        this.Exchange = Exchange;
        // 📝 DIAGNOSTIC (?gridreach=): scales every light's reach in the cull. 1 is exact. Below 1 it deliberately drops light, which is how the executed tests prove that their
        //    "grid on = grid off" comparison can fail at all.
        this.ReachScale = 1.0;
        this.Bytes  = new ArrayBuffer(LightClusterBytes);
        this.Floats = new Float32Array(this.Bytes);
        this.Masks  = new Uint32Array(this.Bytes, (HeaderFloats + LightFloats) * 4, CellCount);
        const [X0, Y0, Z0] = LightClusterBounds.Minimum;
        const [SizeX, SizeY, SizeZ] = LightClusterBounds.Extent;
        this.Floats.set([X0, Y0, Z0, 0.0, SizeX, SizeY, SizeZ, 0.0], 0);
        this.Records = Exchange.ConstructRecords(LightClusterBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Light grid records");
    }

    // 📝 LightData: the packed light records of LightingSystem.BufferData. Each active light lands in every cell whose box lies
    //    within its reach; the test is conservative (the sphere-to-box distance), so no contributing light is dropped.
    Update(Device, LightData)
    {
        this.Floats.set(LightData.subarray(0, LightFloats), HeaderFloats);
        const [Minimum, Extent, Resolution] = [LightClusterBounds.Minimum, LightClusterBounds.Extent, LightClusterBounds.Resolution];
        const CellSize = Extent.map((Size, Axis) => Size / Resolution[Axis]);
        const Lights = [];
        for (let Index = 0; Index < MaxSceneLights; ++Index)
        {
            const Offset = Index * LightRecordFloats;
            if (LightData[Offset + 15] < 0.5)   // Params.w: active
                continue;
            Lights.push({ Index, Centre: [LightData[Offset], LightData[Offset + 1], LightData[Offset + 2]], Reach: LightData[Offset + 7] * this.ReachScale });
        }
        for (let Z = 0; Z < Resolution[2]; ++Z)
        for (let Y = 0; Y < Resolution[1]; ++Y)
        for (let X = 0; X < Resolution[0]; ++X)
        {
            const Cell = [X, Y, Z];
            const Low  = Cell.map((Index, Axis) => Minimum[Axis] + Index * CellSize[Axis]);
            const High = Low.map((Corner, Axis) => Corner + CellSize[Axis]);
            let Mask = 0;
            for (const Light of Lights)
            {
                let Distance2 = 0.0;
                for (let Axis = 0; Axis < 3; ++Axis)
                {
                    const Nearest = Math.min(Math.max(Light.Centre[Axis], Low[Axis]), High[Axis]);
                    Distance2 += (Light.Centre[Axis] - Nearest) ** 2;
                }
                if (Distance2 < Light.Reach * Light.Reach)
                    Mask |= 1 << Light.Index;
            }
            this.Masks[X + Resolution[0] * (Y + Resolution[1] * Z)] = Mask >>> 0;
        }
        Device.queue.writeBuffer(this.Records, 0, this.Bytes, 0, DynamicBytes);
    }

    // 📝 Uploads the emissive structure: the triangle table when it changed, the cluster table and cell lists every time, and the cluster count (Extent.w), which is 0 to
    //    switch the structure off. The records are those of EmissiveLightSpace.
    AssignEmissive(Device, Emissive, WithTriangles)
    {
        const Bytes = new Uint8Array(this.Bytes);
        Bytes.set(new Uint8Array(Emissive.ClusterBytes), EmissiveClustersOffset);
        Bytes.set(new Uint8Array(Emissive.Cells.buffer, Emissive.Cells.byteOffset, Emissive.Cells.byteLength), EmissiveCellsOffset);
        if (WithTriangles) Bytes.set(new Uint8Array(Emissive.TriangleBytes), EmissiveTrianglesOffset);
        this.Floats[7] = Emissive.Count;   // Extent.w
        Device.queue.writeBuffer(this.Records, 16, this.Bytes, 16, 16);
        if (WithTriangles) Device.queue.writeBuffer(this.Records, EmissiveTrianglesOffset, this.Bytes, EmissiveTrianglesOffset, EmissiveClustersOffset - EmissiveTrianglesOffset);
        Device.queue.writeBuffer(this.Records, EmissiveClustersOffset, this.Bytes, EmissiveClustersOffset, LightClusterBytes - EmissiveClustersOffset);
    }

    // 📝 Uploads the packed photometric atlas (Photometry.js) once and raises the flag the shaders test; until then every light uses its analytic beam shape.
    AssignPhotometry(Device, Atlas)
    {
        new Uint8Array(this.Bytes, PhotometryOffset, PhotometryBytes).set(Atlas);
        this.Floats[3] = 1.0;
        Device.queue.writeBuffer(this.Records, 0, this.Bytes, 0, 16);
        Device.queue.writeBuffer(this.Records, PhotometryOffset, this.Bytes, PhotometryOffset, PhotometryBytes);
        this.Photometric = true;
    }
}
