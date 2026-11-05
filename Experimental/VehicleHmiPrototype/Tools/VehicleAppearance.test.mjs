import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleAppearance, LinearColour } from '../Source/VehicleAppearance.js';
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
test('Garage controls are paint only: swatches apply named finishes to the live vehicle', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924 },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    const Click = Id => Screen.Regions.find(Region => Region.Identifier === Id).Action();
    Click('colour:2');
    assert.equal(Screen.Appearance.Colour, '#8a1a1d');   // Command Red
    assert.equal(Screen.FinishColour, '#8a1a1d');
    for (const Removed of ['finish', 'wheels', 'blue-rims', 'inspection', 'preview-spin'])
        assert.ok(!Screen.Regions.find(Region => Region.Identifier === Removed), Removed + ' is not a Garage control any more');
    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('colour:')).length, 8);
});

test('paint drags only apply on the car body, and cancel safely', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924,
        Pick: () => ({ Occupant: 0, Family: CourseMaterial.BodyCoat }) },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    Screen.ForwardPointer('down', 790, 500);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.Colour, '#c2c8d0');   // Nebula Silver, dragged onto the body
    Screen.Preview.Pick = () => ({ Occupant: 1, Family: CourseMaterial.Hub });
    Screen.ForwardPointer('down', 790, 500);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.Colour, '#c2c8d0');   // paint on a wheel is not a paint surface
    Screen.ForwardPointer('down', 790, 500);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('cancel', 400, 450);
    assert.equal(Screen.DragMaterial, null);
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
