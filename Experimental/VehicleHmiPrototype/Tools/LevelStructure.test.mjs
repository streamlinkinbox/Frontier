import {test} from 'node:test';
import assert from 'node:assert/strict';
import {LevelShapes, ResolveLevelPlacements, ResolveConePlacements, ResolveBreakables, EmitLevelGroundFacets, LevelZones} from '../Source/LevelStructure.js';
import {ResolveCourseHeight} from '../Source/CourseStructure.js';

const Pad = {X0:-60, X1:156, Y0:-60, Y1:60};
test('new training ground: no drills export, free zones inside the pad', async () => {
    const Module = await import('../Source/LevelStructure.js');
    assert.equal(Module.TrainingDrills, undefined);
    assert.equal(Module.ArenaZones, undefined);
    assert.ok(LevelZones.length >= 5);
    for (const Zone of LevelZones) {
        assert.ok(Zone.Position[0] > Pad.X0 && Zone.Position[0] < Pad.X1, Zone.Name);
        assert.ok(Zone.Position[1] > Pad.Y0 && Zone.Position[1] < Pad.Y1, Zone.Name);
    }
});
test('boundary barrier fences all four sides and collides', () => {
    const Walls = ResolveLevelPlacements().filter(P => P.Name.startsWith('Boundary'));
    assert.equal(Walls.length, 4);
    assert.ok(Walls.every(W => W.Collider));
    assert.ok(Walls.some(W => W.Position[1] === 58.8) && Walls.some(W => W.Position[1] === -58.8));
    assert.ok(Walls.some(W => W.Position[0] === -58.8) && Walls.some(W => W.Position[0] === 154.8));
});
test('traffic lights, tunnel, garage and lamps are placed; nothing sits outside the pad', () => {
    const All = ResolveLevelPlacements();
    for (const Name of ['Junction light green','Tunnel light green','Tunnel lamp N0','Hangar lamp 0'])
        assert.ok(All.some(P => P.Name === Name), Name);
    assert.ok(All.filter(P => P.Name.startsWith('Light pole head')).length >= 18);
    for (const P of All) {
        assert.ok(P.Position[0] >= Pad.X0-1 && P.Position[0] <= Pad.X1+1, P.Name);
        assert.ok(P.Position[1] >= Pad.Y0-1 && P.Position[1] <= Pad.Y1+1, P.Name);
    }
});
test('cones and breakables are free-play sets inside the pad', () => {
    const Cones = ResolveConePlacements();
    assert.ok(Cones.length >= 30 && Cones.length <= 60);
    assert.deepEqual([...new Set(Cones.map(C=>C.Group))].sort(), ['garage','parking','ring','slalom','straight','tunnel']);
    for (const C of Cones) assert.ok(C.Position[0]>Pad.X0 && C.Position[0]<Pad.X1 && C.Position[1]>Pad.Y0 && C.Position[1]<Pad.Y1, C.Name);
    const Props = ResolveBreakables();
    assert.ok(Props.length >= 8);
    for (const P of Props) assert.ok(P.Position[0]>Pad.X0 && P.Position[0]<Pad.X1 && P.Position[1]>Pad.Y0 && P.Position[1]<Pad.Y1, P.Name);
});
test('ground paint exists and the terrain is flat tarmac (kerbs only)', () => {
    assert.ok(EmitLevelGroundFacets().length > 400);
    assert.equal(ResolveCourseHeight(25, 0), 0);       // old jump-ramp bump is gone
    assert.equal(ResolveCourseHeight(0, 0), 0);
    assert.ok(ResolveCourseHeight(0, 6.15) > 0);       // kerb strip still raised
    assert.ok(LevelShapes.BoundaryWall && LevelShapes.TrafficGreen);
});
