//============================================================================================================================================
//                                                         COMPAREREFERENCES.MJS
//============================================================================================================================================
// 🧩 Reproduce the bounded light-list comparison without claiming GPU performance or hardware ray tracing.

import Assert from 'node:assert/strict';
import Fs from 'node:fs';
import Path from 'node:path';
import {fileURLToPath} from 'node:url';
import {LightingSystem, ClusterGridBounds, ClusterCellBytes, ClusterIndexBytes, LightBufferBytes}
    from '../../../Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/LightingSystem.js';
import {LightClusterSpace, LightClusterBytes}
    from '../../../Experimental/SlateReference/B/Exhibits/IlluminationHost/Source/LightClusterSpace.js';

const A = new LightingSystem();
A.Lights = Array.from({length:32}, () => ({Position:[0,0,10], Reach:10000}));
A.AssignClusters();
Assert(A.ClusterOverflow > 0);
Assert.equal(A.ClusterCells[3 * 4 + 1],16);
// 📝 This verifies CPU binning/storage only. The device below merely captures an upload request.
globalThis.GPUBufferUsage = {STORAGE:1,COPY_DST:2};
let Uploaded = 0;
const B = new LightClusterSpace({ConstructRecords:Bytes => ({Bytes})});
const Packed = new Float32Array(32 * 16);
for (let Index = 0; Index < 32; ++Index)
{
    Packed.set([0,0,10,0],Index * 16);
    Packed[Index * 16 + 7] = 10000;
    Packed[Index * 16 + 15] = 1;
}
B.Update({queue:{writeBuffer:(_Destination,_Offset,Bytes) => Uploaded = Bytes.byteLength}},Packed);
Assert.equal(B.Masks[0],0xFFFFFFFF);
Assert.equal(Uploaded,LightClusterBytes);
const Report = {
    execution:'CPU light-bin reproduction; not GPU speed, Dawn validation or Windows execution',
    A:{cluster_dimensions:[ClusterGridBounds.ResX,ClusterGridBounds.ResY,ClusterGridBounds.ResZ],
        test_lights:32,retained_per_cell:16,dropped_assignments:A.ClusterOverflow,
        light_and_cluster_bytes:LightBufferBytes + ClusterCellBytes + ClusterIndexBytes},
    B:{test_lights:32,retained_per_cell:32,first_cell_mask:B.Masks[0],light_and_cluster_bytes:LightClusterBytes},
    interpretation:'B retains all 32 supported lights in this case; A drops assignments beyond its 16-light cell cap. This does not measure runtime.'
};
const Output = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)),'../Results/LightBinComparison.json');
Fs.writeFileSync(Output,JSON.stringify(Report,null,2)+'\n');
console.log(JSON.stringify(Report,null,2));
