import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { ExtractFeatureEdges, ConstructScanEdges, EdgeFamilies } from '../Source/VehicleEdges.js';
import { CourseMaterial } from '../Source/MaterialSpecification.js';

const Triangle = (...Corners) => ({ Corners });

test('a crease past the threshold keeps the shared edge, a flat join drops it, boundaries are always kept (THREE.EdgesGeometry)', () => {
    const A = [0, 0, 0], B = [1, 0, 0], C = [0, 1, 0];
    const Folded = ExtractFeatureEdges([Triangle(A, B, C), Triangle(C, B, [1, 1, 1])], 14);
    assert.equal(Folded.length / 6, 5);                               // four boundaries and the crease
    const Flat = ExtractFeatureEdges([Triangle(A, B, C), Triangle(C, B, [1, 1, 0])], 14);
    assert.equal(Flat.length / 6, 4);                                 // the four boundaries only
    const Gentle = ExtractFeatureEdges([Triangle(A, B, C), Triangle(C, B, [1, 1, 0.1])], 14);   // about 8° of crease
    assert.equal(Gentle.length / 6, 4);
    const Wide = ExtractFeatureEdges([Triangle(A, B, C), Triangle(C, B, [1, 1, 0.1])], 5);
    assert.equal(Wide.length / 6, 5);
});

test('corners that weld at four decimals are one vertex, and triangles that collapse are skipped', () => {
    const A = [0, 0, 0], B = [1, 0, 0], C = [0, 1, 0];
    // the neighbour's copies of B and C are a hair off (they weld to the same points): the shared edge is matched, flat, and dropped
    const Welded = ExtractFeatureEdges([Triangle(A, B, C), Triangle([0.00002, 0.99999, 0], [0.99998, 0.00003, 0], [1, 1, 0])], 14);
    assert.equal(Welded.length / 6, 4);
    const Apart = ExtractFeatureEdges([Triangle(A, B, C), Triangle([0.0002, 0.9999, 0], [0.9998, 0.0003, 0], [1, 1, 0])], 14);
    assert.equal(Apart.length / 6, 6);                                // four decimals is the limit: 2e-4 apart is two vertices
    const Collapsed = ExtractFeatureEdges([Triangle(A, [0.00001, 0, 0], C), Triangle(A, B, C)], 14);
    assert.equal(Collapsed.length / 6, 3);                            // only the second triangle's boundary
    const Back = ExtractFeatureEdges([Triangle(A, B, C), Triangle(A, C, B)], 14);
    assert.equal(Back.length / 6, 3);                                 // the same face seen from behind: a 180° crease on every edge
});

test('the car\'s wireframe: 1739 segments over the body panels, plastics and tyres, tagged with occupant and family', async () => {
    const Root = path.resolve(import.meta.dirname, '..');
    globalThis.location = { search: '?lobby=1', href: 'http://localhost/?lobby=1' };
    globalThis.fetch = async (Location) => {
        const Bytes = fs.readFileSync(path.join(Root, String(Location).split('?')[0]));
        return { ok: true, status: 200, arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength), text: async () => Bytes.toString('utf8') };
    };
    const { SceneStructure } = await import('../Source/FacetStructure.js');
    const Scene = new SceneStructure();
    await Scene.ConstructScene('Archive', 0);
    const Edges = ConstructScanEdges(Scene);
    assert.equal(Edges.Count, 1739);
    assert.equal(Edges.Records.length, 1739 * 8);
    const Tags = new Uint32Array(Edges.Records.buffer);
    const Counts = new Map();
    for (let Index = 0; Index < Edges.Count; ++Index) {
        const Tag = Tags[Index * 8 + 3];
        Counts.set(Tag, (Counts.get(Tag) ?? 0) + 1);
        assert.equal(Tags[Index * 8 + 7], 0);
    }
    const Tag = (Occupant, Family) => Occupant | (Family << 8);
    assert.equal(Counts.get(Tag(0, CourseMaterial.BodyCoat)), 376);
    assert.equal(Counts.get(Tag(0, CourseMaterial.VehiclePlastic)), 692);
    assert.equal(Counts.get(Tag(0, CourseMaterial.Tyre)), 159);
    for (const Wheel of [1, 2, 3, 4]) assert.equal(Counts.get(Tag(Wheel, CourseMaterial.Tyre)), 128);
    assert.equal(Counts.size, 7);                                     // no rims, hubs, glass or lamps
    assert.deepEqual(EdgeFamilies.map(({ Threshold }) => Threshold), [14, 14, 28]);
    for (let Index = 0; Index < Edges.Records.length; ++Index) if (Index % 8 !== 3) assert.ok(Number.isFinite(Edges.Records[Index]));
});
