import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RayTracingSpace, TlasNodeCapacity } from '../Source/RayTracingSpace.js';
import { OccupantRole } from '../Source/FacetStructure.js';

// 📝 A TLAS over a hand-made scene, uploaded through a fake queue that mirrors every write into a CPU copy of the GPU's node buffer. The skip
//    (Renderer/Patches/0003-tlas-skip-static.patch) is correct when that copy is, at every step, what a build-every-frame TLAS would have put
//    there. A second space with the skip off is that reference.

const OccupantTotal = 40;

function ConstructWorld(OccupantCount)
{
    const Boxes = [];
    for (let Index = 0; Index < OccupantCount; ++Index)
        Boxes.push({ Minimum: [Index * 3.0, 0.0, 0.0], Maximum: [Index * 3.0 + 2.0, 1.5 + (Index % 3), 2.0] });
    return {
        Boxes,
        Occupants: Boxes.map(() => ({ Roles: OccupantRole.Raster, Inactive: false, Fragment: false })),
        GroundOccupant: OccupantCount - 1,
        VehicleOccupants: null,
        Queries: 0,
        QueryWorldBounds(Index) { ++this.Queries; return this.Boxes[Index]; }
    };
}

function ConstructSpace(Scene, SkipEnabled)
{
    const Writes = [];
    const Mirror = new Uint8Array((TlasNodeCapacity + 64) * 32);
    const Queue = { writeBuffer(Buffer, Offset, Data, DataOffset, Size)
    {
        Writes.push({ Buffer, Offset, Size });
        if (Buffer === Space.NodeBuffer)
            Mirror.set(new Uint8Array(Data, DataOffset, Size), Offset);
    } };
    const Space = new RayTracingSpace({ Device: { queue: Queue } });
    Space.TlasSkipEnabled = SkipEnabled;
    Space.NodeBuffer = { label: 'nodes' };
    Space.OccupantBlasRoot = new Int32Array(Scene.Occupants.length).map((_, Index) => TlasNodeCapacity + 4 * Index);
    Space.AllNodeBytes = new ArrayBuffer((TlasNodeCapacity + 64) * 32);
    Space.AllNodeScalars = new Float32Array(Space.AllNodeBytes);
    Space.AllNodeIntegers = new Uint32Array(Space.AllNodeBytes);
    for (const Name of ['ScratchOrder', 'ScratchOccIdx'])
        Space[Name] = new Uint32Array(OccupantTotal);
    for (const Name of ['ScratchMinX', 'ScratchMinY', 'ScratchMinZ', 'ScratchMaxX', 'ScratchMaxY', 'ScratchMaxZ', 'ScratchCentX', 'ScratchCentY', 'ScratchCentZ'])
        Space[Name] = new Float32Array(OccupantTotal);
    return { Space, Writes, Mirror };
}

const TlasBytes = (Entry) => Entry.Mirror.slice(0, Entry.Space.UsedTlasNodes * 32);
const SameBytes = (A, B) => A.length === B.length && A.every((Value, Index) => Value === B[Index]);

test('an unchanged second call skips: no build and no buffer write, and the buffer keeps the TLAS it had', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    const First = TlasBytes(Entry);
    assert.equal(Entry.Writes.length, 1);
    assert.equal(First.length, Entry.Space.UsedTlasNodes * 32);
    for (let Call = 0; Call < 5; ++Call)
        Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 1, 'skipped frames do not write');
    assert.equal(Entry.Space.TlasSkipCount, 5);
    assert.equal(Entry.Space.TlasRebuildCount, 1);
    assert.ok(SameBytes(TlasBytes(Entry), First));
});

test('a leaf that moves by one float, or a leaf that appears, rebuilds and uploads', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Scene.Boxes[3].Maximum[0] = Math.fround(Scene.Boxes[3].Maximum[0] + 1.0e-6);
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Writes.length, 2);
    Scene.Occupants[5].Inactive = true;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 3);
    Scene.Occupants[5].Inactive = false;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 4);
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 4, 'and holding still skips again');
});

test('a BLAS root that changes with the bounds unchanged rebuilds', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Entry.Space.OccupantBlasRoot[4] = -1;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    Entry.Space.OccupantBlasRoot[4] = TlasNodeCapacity + 99;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Writes.length, 3);
});

test('bounds that agree as floats but centre differently still rebuild: the centroid is part of the key', () => {
    const Scene = ConstructWorld(12);
    Scene.Boxes[2] = { Minimum: [-3.0 + 1.0e-8, 0, 0], Maximum: [3.0 + 2.0e-8, 1, 1] };
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Scene.Boxes[2] = { Minimum: [-3.0 + 3.0e-8, 0, 0], Maximum: [3.0 + 5.0e-8, 1, 1] };
    assert.equal(Math.fround(Scene.Boxes[2].Minimum[0]), Math.fround(-3.0 + 1.0e-8), 'the stored bounds are the same floats');
    assert.notEqual(Math.fround(0.5 * (Scene.Boxes[2].Minimum[0] + Scene.Boxes[2].Maximum[0])), Math.fround(0.5 * (-3.0 + 1.0e-8 + 3.0 + 2.0e-8)), 'the centroids are not');
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Writes.length, 2);
});

test('the ground leaf is in the key: it moving, or going away, rebuilds', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Scene.Boxes[11].Minimum[1] = -0.5;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    Scene.Occupants[11].Inactive = true;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Space.ActiveInstances, 11);
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 1);
});

test('a NaN bound never skips', () => {
    const Scene = ConstructWorld(12);
    Scene.Boxes[1].Maximum[2] = Number.NaN;
    const Entry = ConstructSpace(Scene, true);
    for (let Call = 0; Call < 3; ++Call)
        Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Writes.length, 3);
});

test('a new node buffer is uploaded to even when nothing moved, and so is one that never received an upload', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 1);
    const Replacement = { label: 'nodes, re-created' };
    Entry.Space.NodeBuffer = Replacement;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Writes.length, 2);
    assert.equal(Entry.Writes[1].Buffer, Replacement);

    // 📝 The failure that matters most: the host builds the TLAS before the GPU buffer exists, the buffer arrives later, and nothing has moved.
    const Early = ConstructSpace(Scene, true);
    Early.Space.NodeBuffer = null;
    Early.Space.UpdateTlas(Scene);
    assert.equal(Early.Writes.length, 0);
    Early.Space.NodeBuffer = { label: 'arrives late' };
    Early.Space.UpdateTlas(Scene);
    assert.equal(Early.Writes.length, 1, 'the first call with a buffer uploads');
    assert.equal(Early.Space.TlasSkipCount, 0);
});

test('turning the skip off forgets the key: turning it on again cannot skip against a TLAS the buffer no longer holds', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 1);
    Entry.Space.TlasSkipEnabled = false;
    Scene.Boxes[2].Maximum[1] += 5.0;
    Entry.Space.UpdateTlas(Scene);
    Scene.Boxes[2].Maximum[1] -= 5.0;           // back to the inputs of the TLAS that was uploaded first
    Entry.Space.UpdateTlas(Scene);
    Entry.Space.TlasSkipEnabled = true;
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 1, 'the first call after turning it on builds');
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 2);
    const Reference = ConstructSpace(Scene, false);
    Reference.Space.UpdateTlas(Scene);
    assert.ok(SameBytes(TlasBytes(Entry), TlasBytes(Reference)));
});

test('a forced rebuild builds, uploads the same bytes, and the key stays honest afterwards', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    const First = TlasBytes(Entry);
    Entry.Space.TlasForceRebuild = true;
    Entry.Space.UpdateTlas(Scene);
    Entry.Space.TlasForceRebuild = false;
    assert.equal(Entry.Writes.length, 2);
    assert.ok(SameBytes(TlasBytes(Entry), First));
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 0, 'forcing forgets the key');
    Entry.Space.UpdateTlas(Scene);
    assert.equal(Entry.Space.TlasSkipCount, 1);
});

test('a build that fails half way leaves nothing to skip on', () => {
    const Scene = ConstructWorld(12);
    const Entry = ConstructSpace(Scene, true);
    Entry.Space.UpdateTlas(Scene);
    Scene.Boxes[0].Maximum[0] += 1.0;
    let Remaining = 2;                              // the key reads the ground once; the build reads it again, and that read throws
    const Query = Scene.QueryWorldBounds.bind(Scene);
    Scene.QueryWorldBounds = (Index) =>
    {
        if (Index === Scene.GroundOccupant && --Remaining === 0)
            throw new Error('fails inside the build');
        return Query(Index);
    };
    assert.throws(() => Entry.Space.UpdateTlas(Scene), /fails inside the build/);
    Scene.QueryWorldBounds = Query;
    Entry.Space.UpdateTlas(Scene);                  // identical to the failed call's inputs; must not skip
    assert.equal(Entry.Space.TlasSkipCount, 0);
    assert.equal(Entry.Writes.length, 2);
    const Reference = ConstructSpace(Scene, false);
    Reference.Space.UpdateTlas(Scene);
    assert.ok(SameBytes(TlasBytes(Entry), TlasBytes(Reference)));
});

test('over a long random walk the buffer holds, after every step, the TLAS a build-every-frame renderer would have uploaded', () => {
    let Seed = 20260505;
    const Random = () => { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; };
    const Scene = ConstructWorld(30);
    const Skipping = ConstructSpace(Scene, true);
    const Always = ConstructSpace(Scene, false);
    Skipping.Space.UpdateTlas(Scene);
    Always.Space.UpdateTlas(Scene);
    let Holds = 0, Moves = 0, Toggles = 0, Reroots = 0, Replacements = 0;
    for (let Step = 0; Step < 400; ++Step)
    {
        const Kind = Random();
        if (Kind < 0.55)
            ++Holds;                                                        // nothing moves: the case the skip is for
        else if (Kind < 0.80)
        {
            const Box = Scene.Boxes[Math.floor(Random() * 29)];             // a leaf moves (never the ground)
            const Delta = (Random() - 0.5) * (Random() < 0.5 ? 1.0e-7 : 2.0);
            for (let Axis = 0; Axis < 3; ++Axis) { Box.Minimum[Axis] += Delta; Box.Maximum[Axis] += Delta; }
            ++Moves;
        }
        else if (Kind < 0.90)
        {
            const Occupant = Scene.Occupants[Math.floor(Random() * 29)];     // a leaf leaves or returns
            Occupant.Inactive = !Occupant.Inactive;
            ++Toggles;
        }
        else if (Kind < 0.96)
        {
            const Index = Math.floor(Random() * 29);                        // a BLAS root changes (fragments appearing and clearing)
            const Root = Random() < 0.5 ? -1 : TlasNodeCapacity + Math.floor(Random() * 500);
            Skipping.Space.OccupantBlasRoot[Index] = Root;
            Always.Space.OccupantBlasRoot[Index] = Root;
            ++Reroots;
        }
        else
        {
            const Replacement = { label: `nodes ${Step}` };                  // the GPU buffer is re-created
            Skipping.Space.NodeBuffer = Replacement;
            Always.Space.NodeBuffer = Replacement;
            Skipping.Mirror.fill(0);
            Always.Mirror.fill(0);
            ++Replacements;
        }
        Skipping.Space.UpdateTlas(Scene);
        Always.Space.UpdateTlas(Scene);
        assert.equal(Skipping.Space.UsedTlasNodes, Always.Space.UsedTlasNodes, `step ${Step}`);
        assert.equal(Skipping.Space.ActiveInstances, Always.Space.ActiveInstances, `step ${Step}`);
        assert.ok(SameBytes(TlasBytes(Skipping), TlasBytes(Always)), `the buffers differ after step ${Step}`);
    }
    assert.ok(Holds > 150 && Moves > 60 && Toggles > 20 && Reroots > 10 && Replacements > 5, `${Holds} ${Moves} ${Toggles} ${Reroots} ${Replacements}`);
    assert.ok(Skipping.Space.TlasSkipCount > 80, `${Skipping.Space.TlasSkipCount} skips`);
    assert.ok(Skipping.Writes.length < Always.Writes.length);
    assert.equal(Always.Space.TlasSkipCount, 0);
});
