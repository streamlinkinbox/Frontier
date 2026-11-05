import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleAppearance, LinearColour, PaintOptions } from '../Source/VehicleAppearance.js';
import { ConstructMaterialSpecification, CourseMaterial } from '../Source/MaterialSpecification.js';
import { ScreenSequence } from '../Source/Cabin/ScreenSequence.js';
test('paint and rim records update the shared world material buffer only', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Glass = structuredClone(Materials[CourseMaterial.VehicleGlass]);
    const Appearance = new VehicleAppearance(Materials, (Offset, Data) => Writes.push([Offset, Data]));
    Appearance.Apply({ Colour: '#a4d8be', Matte: true, DarkWheels: true });
    assert.deepEqual(Materials[CourseMaterial.BodyCoat].Albedo, LinearColour('#a4d8be'));
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, 0.7);
    assert.deepEqual(Writes.map(([Offset]) => Offset), [6 * 64, 10 * 64]);
    assert.ok(Writes.every(([, Data]) => Data.byteLength === 64));
    assert.deepEqual(Materials[CourseMaterial.VehicleGlass], Glass);
    Appearance.Apply({ Matte: false });
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, 0.0775);
    assert.throws(() => Appearance.Apply({ Colour: 'bad' }));
});
// 📝 A tablet panel with only its drawing stubbed: the real regions, pointer logic and appearance run against a recorded preview.
function ConstructGaragePanel(Preview = {}) {
    const Screen = Object.create(ScreenSequence.prototype);
    const Calls = { Rotate: [], Scan: [] };
    Object.assign(Screen, { Regions: [], Hovered: '', Dirty: false, Notice: '', DragMaterial: null, Pointer: null, Application: 'garage',
        Preview: { External: true, Loaded: true, Scan: { Enabled: false },
            ResolveDropCentre: (U, V) => [U ?? -1, V ?? -1, 0], Rotate: (X, Y) => Calls.Rotate.push([X, Y]),
            AssignScan(On) { Calls.Scan.push(On); this.Scan.Enabled = On; }, ...Preview },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule', 'RasterSwatch', 'RasterMoveIcon']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    return { Screen, Calls, Click: Id => Screen.Regions.find(Region => Region.Identifier === Id).Action() };
}

test('Garage controls are the paint shelf, the finish and the X-ray scan; a tap ripples from the nose', () => {
    const { Screen, Click } = ConstructGaragePanel();
    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('colour:')).length, 8);
    for (const Present of ['finish:gloss', 'finish:matte', 'scan-toggle', 'preview-orbit'])
        assert.ok(Screen.Regions.find(Region => Region.Identifier === Present), Present + ' is a Garage control');
    for (const Removed of ['finish', 'wheels', 'blue-rims', 'inspection', 'preview-spin'])
        assert.ok(!Screen.Regions.find(Region => Region.Identifier === Removed), Removed + ' is not a Garage control any more');
    Click('colour:2');
    assert.equal(Screen.Appearance.Option.Name, 'Command Red');
    assert.equal(Screen.Appearance.Colour, '#8a1a1d');
    assert.equal(Screen.Appearance.Ripple.Active, true);
    assert.deepEqual(Screen.Appearance.Ripple.Centre, [-1, -1, 0]);   // no drop point: the preview's nose default
    Click('finish:matte');
    assert.equal(Screen.Appearance.Finish, 'matte');
    assert.equal(Screen.Appearance.Ripple.Target.Roughness, 0.72);
});

test('a swatch dragged more than 7 px and released anywhere on the stage paints from the drop point', () => {
    const { Screen } = ConstructGaragePanel();
    Screen.ForwardPointer('down', 1080, 380);                 // Tactical Blue, index 3
    Screen.ForwardPointer('move', 1075, 380);
    assert.equal(Screen.DragMaterial, null);                  // 5 px: still a tap
    Screen.ForwardPointer('move', 1060, 380);                 // 20 px: picked up
    assert.equal(Screen.DragMaterial.Option.Name, 'Tactical Blue');
    Screen.ForwardPointer('up', 398, 448);                    // anywhere on the stage, car or not
    assert.equal(Screen.Appearance.Option.Name, 'Tactical Blue');
    assert.equal(Screen.Appearance.Ripple.Active, true);
    assert.deepEqual(Screen.Appearance.Ripple.Centre, [(398 - 78) / 640, (448 - 248) / 400, 0]);
    assert.equal(Screen.DragMaterial, null);
});

test('releasing a paint drag over the panel, or cancelling it, leaves the car as it was', () => {
    const { Screen } = ConstructGaragePanel();
    Screen.ForwardPointer('down', 790, 380);
    Screen.ForwardPointer('move', 900, 600);
    Screen.ForwardPointer('up', 900, 600);                    // over the panel: cancels
    assert.equal(Screen.Appearance.Option, null);
    assert.equal(Screen.Appearance.Ripple.Active, false);
    assert.equal(Screen.DragMaterial, null);
    Screen.ForwardPointer('down', 790, 380);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('cancel', 400, 450);
    assert.equal(Screen.DragMaterial, null);
    assert.equal(Screen.Appearance.Option, null);
});

test('dragging the stage turns the camera in pixels; the toggle runs the scan', () => {
    const { Screen, Calls, Click } = ConstructGaragePanel();
    Screen.ForwardPointer('down', 300, 400);
    Screen.ForwardPointer('move', 340, 380);
    Screen.ForwardPointer('move', 345, 380);
    Screen.ForwardPointer('up', 345, 380);
    assert.deepEqual(Calls.Rotate, [[40, -20], [5, 0]]);
    Click('scan-toggle');
    assert.deepEqual(Calls.Scan, [true]);
    Click('scan-toggle');
    assert.deepEqual(Calls.Scan, [true, false]);
});

test('the opening state is the configurator\'s: Nebula Silver gloss, committed at once, wheels as asked', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Appearance = new VehicleAppearance(Materials, (Offset, Data) => Writes.push(Offset));
    Appearance.Reset({ DarkWheels: true, BlueRims: false });
    const Body = Materials[CourseMaterial.BodyCoat];
    assert.equal(Appearance.Option.Name, 'Nebula Silver');
    assert.deepEqual(Body.Albedo, LinearColour('#c2c8d0'));
    assert.equal(Body.Roughness, 0.26);
    assert.equal(Body.Metalness, 0.9);
    assert.equal(Body.CoatWeight, 1);
    assert.equal(Body.CoatRoughness, 0.12);
    assert.equal(Appearance.Ripple.Active, false);
    assert.equal(Appearance.DarkWheels, true);
});

test('paint ripples commit the finish when the wave has run, and not before', () => {
    const Materials = ConstructMaterialSpecification();
    const Appearance = new VehicleAppearance(Materials, () => {});
    Appearance.Reset();
    const Silver = structuredClone(Materials[CourseMaterial.BodyCoat]);
    Appearance.Paint(PaintOptions[2], 'matte', [1, 2, 3]);
    for (let Step = 0; Step < 26; Step++) Appearance.Advance(0.05);   // 1.30 s of the 1.35 s
    assert.deepEqual(Materials[CourseMaterial.BodyCoat], Silver);
    Appearance.Advance(0.05);
    const Body = Materials[CourseMaterial.BodyCoat];
    assert.deepEqual(Body.Albedo, LinearColour('#8a1a1d'));
    assert.equal(Body.Roughness, 0.72);
    assert.equal(Body.CoatWeight, 0.1);
    assert.equal(Body.CoatRoughness, 0.65);
    assert.equal(Appearance.Ripple.Active, false);
});

test('rim finish changes affect selected accent faces, never the black rim base or tyre', () => {
    const Materials = ConstructMaterialSpecification();
    const Base = structuredClone(Materials[CourseMaterial.RimBase]);
    const Tyre = structuredClone(Materials[CourseMaterial.Tyre]);
    const Appearance = new VehicleAppearance(Materials, () => {});
    for (const Finish of [{DarkWheels:true}, {DarkWheels:false,BlueRims:false}, {DarkWheels:false,BlueRims:true}]) {
        Appearance.Apply(Finish);
        assert.deepEqual(Materials[CourseMaterial.RimBase],Base);
        assert.deepEqual(Materials[CourseMaterial.Tyre],Tyre);
    }
    assert.ok(Materials[CourseMaterial.Hub].Emission[2] > 0);
});
