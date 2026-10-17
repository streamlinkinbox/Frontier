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
