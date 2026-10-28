import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { WorldGridCell, WrapGridCell, RecentreGridWindow } from '../../../Experimental/VehicleHmiPrototype/Source/SpatialGrid.js';

const Executable = process.argv[2];
assert.ok(Executable, 'supply the compiled validation executable');
let Seed = 918273;
function Random() { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; }
const Cases = [];
for (let Index = 0; Index < 512; Index++) {
    const Edge = [0.25, 0.5, 1, 2][Index % 4];
    const Focus = Index < 8 ? [[-0.25, 0], [0, 0], [4, -4], [-4, 4], [8, 0], [8.25, 0], [-8, 0], [-8.25, 0]][Index] : [Math.floor(Random() * 1600 - 800) / 4, Math.floor(Random() * 1600 - 800) / 4];
    const Extent = [32, 64];
    const Origin = [Math.floor(Random() * 100 - 50), Math.floor(Random() * 100 - 50)];
    const Force = Index % 5 === 0;
    const Cell = Array.from({ length: 3 }, () => Math.floor(Random() * 2000 - 1000));
    const WrapExtent = [32, 64, 20];
    const Expected = [...WorldGridCell(Focus, Edge), ...RecentreGridWindow(Focus, Edge, Extent, Origin, Force), ...WrapGridCell(Cell, WrapExtent)];
    Cases.push({ Input: [...Focus, Edge, ...Extent, ...Origin, Number(Force), ...Cell, ...WrapExtent].join(' '), Expected });
}
// Exact guard-band thresholds and the first cell beyond them, with a fixed origin.
for (const Focus of [[8, 32], [7, 32], [24, 32], [25, 32], [-1, -1]]) {
    const Edge = 1, Extent = [32, 64], Origin = [0, 0], Cell = [-1, -64, 20], WrapExtent = [32, 64, 20];
    const Expected = [...WorldGridCell(Focus, Edge), ...RecentreGridWindow(Focus, Edge, Extent, Origin, false), ...WrapGridCell(Cell, WrapExtent)];
    Cases.push({ Input: [...Focus, Edge, ...Extent, ...Origin, 0, ...Cell, ...WrapExtent].join(' '), Expected });
}
const Invalid = ['0 0 0 32 64 0 0 0 0 0 0 32 64 20', '0 0 1 32 64 0 0 0 0 0 0 0 64 20'];
const Output = execFileSync(Executable, { input: [...Cases.map(Case => Case.Input), ...Invalid].join('\n') + '\n', encoding: 'utf8' }).trim().split('\n');
assert.equal(Output.length, Cases.length + Invalid.length);
for (let Index = 0; Index < Cases.length; Index++) assert.deepEqual(Output[Index].split(' ').map(Number), Cases[Index].Expected, `case ${Index}`);
assert.deepEqual(Output.slice(-2), ['invalid', 'invalid']);
console.log(`PASS ${Cases.length} native/reference cell-window cases, 2 rejected invalid inputs`);
