import { test } from 'node:test';
import assert from 'node:assert/strict';
import { GarageOrbit, GarageOrbitSpecification, ResolveVehicleFrame } from '../Source/GarageOrbit.js';
import { GarageView, SelectDrawRuns, EncodeOccupantMask, ConstructSparkleCloud } from '../Source/GarageView.js';
import { PickVehicle } from '../Source/VehiclePicking.js';

const Near = (Actual, Expected, Tolerance = 1.0e-9) => assert.ok(Math.abs(Actual - Expected) <= Tolerance, `${Actual} is not within ${Tolerance} of ${Expected}`);
const Identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const Stage = { Origin: [0, 0, 0], Forward: [1, 0, 0], Right: [0, -1, 0], Up: [0, 0, 1] };

test('the camera starts where the reference\'s does: 6.8 ahead, 7.6 to the right, 2.35 above a target 1.18 m up', () => {
    const Orbit = new GarageOrbit();
    const Camera = Orbit.ResolveCamera(Stage, 1.6);
    assert.deepEqual(Camera.Target, [0, 0, 1.18]);
    Near(Camera.Position[0], 6.8);
    Near(Camera.Position[1], -7.6);                                   // the vehicle's right is −y in a forward-x, up-z frame
    Near(Camera.Position[2], 1.18 + 2.35);
    Near(Camera.VerticalFieldOfView, 34 * Math.PI / 180);
    assert.equal(Camera.Aspect, 1.6);
});

test('a drag turns the view 2π per 400 px, spent by damping; it clamps at the polar limits and never pans', () => {
    const Orbit = new GarageOrbit();
    const Azimuth = Orbit.Azimuth, Elevation = Orbit.Elevation;
    Orbit.Rotate(100, 0);
    Orbit.Advance(1 / 60);                                            // one 60 Hz frame spends 6 % of what is pending
    Near(Orbit.Azimuth - Azimuth, Math.PI / 2 * 0.06);
    Orbit.Advance(20);
    Near(Orbit.Azimuth - Azimuth, Math.PI / 2, 1.0e-6);
    assert.equal(Orbit.Elevation, Elevation);
    Orbit.Rotate(0, 100000);
    Orbit.Advance(20);
    Near(Orbit.Elevation, Math.PI / 2 - 0.35);                        // minPolarAngle
    Orbit.Rotate(0, -1000000);
    Orbit.Advance(20);
    Near(Orbit.Elevation, Math.PI / 2 - Math.PI / 2.05);              // maxPolarAngle
    const Camera = Orbit.ResolveCamera(Stage, 1);
    assert.deepEqual(Camera.Target, [0, 0, 1.18]);                    // the target is fixed
    Near(Math.hypot(Camera.Position[0], Camera.Position[1], Camera.Position[2] - 1.18), Orbit.Distance, 1.0e-9);
});

test('the wheel dollies by 0.95 per notch and stops at 6 and 16 m', () => {
    const Orbit = new GarageOrbit();
    const Start = Orbit.Distance;
    Orbit.Zoom(-100);
    Near(Orbit.Distance, Start * 0.95);
    Orbit.Zoom(100);
    Near(Orbit.Distance, Start);
    Orbit.Zoom(-1000000);
    assert.equal(Orbit.Distance, 6);
    Orbit.Zoom(1000000);
    assert.equal(Orbit.Distance, 16);
    assert.equal(GarageOrbitSpecification.RotateExtent, 400);
});

test('auto-rotate turns the view 2π/60 rad/s per unit of speed, and only while it is on', () => {
    const Orbit = new GarageOrbit();
    for (let Frame = 0; Frame < 60; ++Frame) Orbit.Advance(1 / 60);
    const Still = Orbit.Azimuth;
    assert.equal(Orbit.Azimuth, Math.atan2(7.6, 6.8));
    Orbit.Spin = true;
    for (let Frame = 0; Frame < 1200; ++Frame) Orbit.Advance(1 / 60);   // twenty seconds
    const Rate = 2 * Math.PI / 60 * 0.8;
    Near(Orbit.Azimuth - Still, Rate * 20, 0.05);                       // less the little still pending in the damping
    assert.ok(Orbit.Azimuth - Still < Rate * 20);
});

test('a vehicle\'s frame stands on the ground under its centre of mass and keeps forward level', () => {
    const Body = Identity();
    Body[12] = 10; Body[13] = 5; Body[14] = 0.53;                     // 0.53 m up: the centre of mass height
    const Frame = ResolveVehicleFrame(Body, 0.53);
    assert.deepEqual(Frame.Origin, [10, 5, 0]);
    assert.deepEqual(Frame.Forward, [1, 0, 0]);
    assert.deepEqual(Frame.Up, [0, 0, 1]);
    assert.deepEqual(Frame.Right, [0, -1, 0]);
    const Pitched = Identity();                                       // forward tilted 30° nose-up
    Pitched[0] = Math.cos(Math.PI / 6); Pitched[2] = Math.sin(Math.PI / 6);
    assert.deepEqual(ResolveVehicleFrame(Pitched, 0).Forward, [1, 0, 0]);
    const Rolled = Identity();                                        // forward straight up: no heading, falls back to +x
    Rolled[0] = 0; Rolled[2] = 1;
    assert.deepEqual(ResolveVehicleFrame(Rolled, 0).Forward, [1, 0, 0]);
});

test('draw runs split around the occupants a view leaves out', () => {
    const Runs = [{ FacetSet: 0, First: 0, Count: 1 }, { FacetSet: 1, First: 1, Count: 4 }, { FacetSet: 2, First: 5, Count: 1 }, { FacetSet: 3, First: 6, Count: 24 }];
    assert.deepEqual(SelectDrawRuns(Runs, (Index) => Index === 0 || Index === 2 || Index === 3 || Index === 5),
        [{ FacetSet: 0, First: 0, Count: 1 }, { FacetSet: 1, First: 2, Count: 2 }, { FacetSet: 2, First: 5, Count: 1 }]);
    assert.deepEqual(SelectDrawRuns(Runs, () => true), Runs);
    assert.deepEqual(SelectDrawRuns(Runs, () => false), []);
    assert.deepEqual(SelectDrawRuns(Runs, (Index) => Index !== 7), [...Runs.slice(0, 3), { FacetSet: 3, First: 6, Count: 1 }, { FacetSet: 3, First: 8, Count: 22 }]);
});

test('an occupant mask is 128 bits in four words', () => {
    assert.deepEqual([...EncodeOccupantMask([0, 5, 31])], [0x80000021, 0, 0, 0]);
    assert.deepEqual([...EncodeOccupantMask([32, 64, 127])], [0, 1, 1, 0x80000000]);
    assert.deepEqual([...EncodeOccupantMask([-1, 128, 1000])], [0, 0, 0, 0]);
    assert.deepEqual([...EncodeOccupantMask([])], [0, 0, 0, 0]);
});

test('the sparkle cloud is drei\'s: 130 points in a 7.5 × 3.2 × 4.2 m box about 1.35 m up, the same every time', () => {
    const Cloud = ConstructSparkleCloud();
    assert.equal(Cloud.length, 130 * 4);
    assert.deepEqual([...ConstructSparkleCloud()], [...Cloud]);
    let Sum = [0, 0, 0];
    for (let Index = 0; Index < 130; ++Index) {
        const [X, Y, Z] = [Cloud[Index * 4], Cloud[Index * 4 + 1], Cloud[Index * 4 + 2]];
        assert.ok(Math.abs(X) <= 3.75 && Math.abs(Y - 1.35) <= 1.6 && Math.abs(Z) <= 2.1);
        Sum = [Sum[0] + X, Sum[1] + Y, Sum[2] + Z];
    }
    assert.ok(Math.abs(Sum[0] / 130) < 0.6 && Math.abs(Sum[1] / 130 - 1.35) < 0.4 && Math.abs(Sum[2] / 130) < 0.4);
    assert.notDeepEqual([...ConstructSparkleCloud(130, [7.5, 3.2, 4.2], 3.0)], [...Cloud]);
});

// A scene of one triangle two metres ahead of an identity view, on the body.
function ConstructPickingScene(BodyShift = 0) {
    const Body = Identity();
    Body[12] = BodyShift;
    const Occupant = (FacetSet, Transform = Identity()) => ({ FacetSet, Transform });
    return { Occupants: [Occupant(0, Body), Occupant(1), Occupant(1), Occupant(1), Occupant(1)],
        FacetSets: [{ Facets: [{ Family: 6, Corners: [[-1, -1, 2], [1, -1, 2], [0, 1, 2]] }] }, { Facets: [] }] };
}

test('picking reports the occupant, the family and the world point under a view point', () => {
    const Hit = PickVehicle(ConstructPickingScene(), Identity(), 0.5, 0.5);
    assert.equal(Hit.Occupant, 0);
    assert.equal(Hit.Family, 6);
    Near(Hit.Distance, 2);
    assert.deepEqual(Hit.Point.map((Value) => Math.round(Value * 1e9) / 1e9), [0, 0, 2]);
    assert.equal(PickVehicle(ConstructPickingScene(), Identity(), 0.95, 0.05), null);   // beside the triangle
    assert.equal(PickVehicle(ConstructPickingScene(), Identity(), 1.5, 0.5), null);     // off the stage
    assert.equal(PickVehicle(ConstructPickingScene(), null, 0.5, 0.5), null);
    // the occupant is shifted 10 m: a view shifted with it sees the same hit, and the point is in the world
    const View = Identity();
    View[12] = 10;
    const Shifted = PickVehicle(ConstructPickingScene(10), View, 0.5, 0.5);
    assert.deepEqual(Shifted.Point.map((Value) => Math.round(Value * 1e9) / 1e9), [10, 0, 2]);
});

test('a ripple begins where the drop meets the car, else 2.75 m ahead of the middle and 1.87 m above the ground', () => {
    const View = Object.create(GarageView.prototype);
    View.Stage = Stage;
    View.Pick = (U, V) => U === 0.25 && V === 0.75 ? { Point: [1, 2, 3] } : null;
    assert.deepEqual(View.ResolveDropCentre(0.25, 0.75), [1, 2, 3]);
    assert.deepEqual(View.ResolveDropCentre(0.9, 0.9), [2.75, 0, 1.87]);      // a miss
    assert.deepEqual(View.ResolveDropCentre(), [2.75, 0, 1.87]);               // a tap
    View.Stage = { Origin: [10, 5, 0], Forward: [0, 1, 0], Right: [1, 0, 0], Up: [0, 0, 1] };
    assert.deepEqual(View.ResolveDropCentre(), [10, 7.75, 1.87]);
});
