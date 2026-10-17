import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ResolveLevelPlacements, ResolveConePlacements, ResolveBreakables, EmitLevelGroundFacets, TrainingDrills, LevelShapes} from '../Source/LevelStructure.js';
import {ResolveCourseHeight} from '../Source/CourseStructure.js';

test('redesign keeps every original system: drills, zones, cones, props', () => {
    assert.equal(TrainingDrills.length, 10);
    assert.ok(ResolveConePlacements().length >= 120);
    assert.ok(ResolveBreakables().length >= 11);
    assert.ok(EmitLevelGroundFacets().length > 400);
});
test('redesign additions: boundary fence on all four sides, colliding', () => {
    const Walls = ResolveLevelPlacements().filter(P => P.Name.startsWith('Boundary'));
    assert.equal(Walls.length, 4);
    assert.ok(Walls.every(W => W.Collider));
    assert.ok(Walls.some(W => W.Position[1] === 59.0) && Walls.some(W => W.Position[1] === -59.0));
    assert.ok(Walls.some(W => W.Position[0] === -59.0) && Walls.some(W => W.Position[0] === 155.2));
});
test('redesigned east wing: stations moved, nothing removed', async () => {
    const {TrainingDrills, ResolveConePlacements, SpinBox, DriftRings, BowlingPins, MooseLayout} = await import('../Source/LevelStructure.js');
    assert.equal(TrainingDrills.length, 10);
    const Slalom = TrainingDrills.find(D=>D.Id==='slalom');
    assert.equal(Slalom.Start.Axis, 'x'); assert.ok(Slalom.Teleport.Position[1] === 42);   // horizontal, north band
    assert.equal(MooseLayout.EntryX, 104);
    assert.equal(BowlingPins.Head[0], 146);
    assert.deepEqual(SpinBox.Minimum, [90, 8]);
    assert.deepEqual(DriftRings.Centres[0], [132, 4]);
    const Cones = ResolveConePlacements();
    assert.ok(Cones.filter(C=>C.Group==='slalom').every(C=>C.Position[1] > 36));
    assert.ok(Cones.every(C=>C.Position[0] < 154.5 && C.Position[1] < 58.5 && C.Position[1] > -58.5));  // inside the fence
});
test('redesign additions: traffic lights at the junction and tunnel portal, green lit', () => {
    const All = ResolveLevelPlacements();
    for (const Name of ['Junction light green','Junction light head','Tunnel light green','Tunnel light post'])
        assert.ok(All.some(P => P.Name === Name), Name);
    assert.ok(LevelShapes.TrafficGreen && LevelShapes.BoundaryWall);
});
test('terrain unchanged by the redesign: kerbs and the jump ramp still read', () => {
    assert.ok(ResolveCourseHeight(25, 0) > 0.5);
    assert.ok(ResolveCourseHeight(0, 6.15) > 0);
    assert.equal(ResolveCourseHeight(0, 0), 0);
});
