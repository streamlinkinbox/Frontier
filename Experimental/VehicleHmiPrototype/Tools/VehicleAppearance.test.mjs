import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleAppearance, LinearColour, InspectionView } from '../Source/VehicleAppearance.js';
import { ConstructMaterialSpecification, CourseMaterial } from '../Source/MaterialSpecification.js';
import { ScreenSequence } from '../Source/Cabin/ScreenSequence.js';
import { EmitLobbyGroundFacets, LobbyShapes } from '../Source/LobbyStructure.js';
import { StudioPreviewSequence, ResolveStudioExtent, ResolveStudioBudget, ConstructStudioLighting } from '../Source/StudioPreviewSequence.js';
import { EmissiveLightSpace } from '../Source/EmissiveLightSpace.js';
import { LightType, AutomotiveMode } from '../Source/LightingSystem.js';

function ConstructHeadlessGarage(Pick = () => ({ Occupant: 0, Family: CourseMaterial.BodyCoat, BodyPoint: [1.35, -0.58, 0.44] })) {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, {
        Context: null,
        Regions: [],
        Hovered: '',
        Pressed: '',
        Pointer: null,
        DragMaterial: null,
        FinishColour: '#c2c8d0',
        WheelId: 'beadlock',
        Preview: { External: true, Facets: 2924, Spin: false, Pick, Orbit() {} },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {})
    });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    return Screen;
}

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

test('Garage controls expose paint swatches, gloss/matte coat, wheel finishes, and Solid/Ghost/Wireframe/X-Ray Scan views', () => {
    const Screen = ConstructHeadlessGarage();
    const Click = Id => Screen.Regions.find(Region => Region.Identifier === Id).Action();

    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('colour:')).length, 8);
    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('wheel:')).length, 4);
    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('view:')).length, 4);

    // Tapping a paint swatch sweeps side-to-side across the vehicle until it fills the whole car.
    Click('colour:2');   // Command Red
    assert.equal(Screen.Appearance.Colour, '#8a1a1d');
    assert.equal(Screen.FinishColour, '#8a1a1d');
    assert.equal(Screen.Appearance.Ripple.Active, true);
    assert.equal(Screen.Appearance.Ripple.Mode, 'sweep');
    assert.equal(Screen.Appearance.Ripple.ModeCode, 1);

    Screen.Appearance.Advance(0.45);
    assert.ok(Screen.Appearance.ResolvePaintCoverage([2.6, 0.0, 0.3]) > 0.95, 'front fender already covered mid-sweep');
    assert.equal(Screen.Appearance.ResolvePaintCoverage([-2.6, 0.0, 0.3]), 0, 'rear quarter not yet reached mid-sweep');

    Screen.Appearance.Advance(1.0);
    assert.equal(Screen.Appearance.Ripple.Active, false);
    assert.deepEqual(Screen.Appearance.Materials[CourseMaterial.BodyCoat].Albedo, LinearColour('#8a1a1d'));

    // Switching inspection views: Ghost, Wireframe, X-Ray Scan, Solid.
    Click('view:1');
    assert.equal(Screen.Appearance.View, InspectionView.Ghost);
    Screen.Appearance.Advance(0.5);
    assert.ok(Screen.Appearance.Scan.HoloAmount > 0.5);

    Click('view:2');
    assert.equal(Screen.Appearance.View, InspectionView.Wireframe);

    Click('view:3');
    assert.equal(Screen.Appearance.View, InspectionView.Scan);
    assert.equal(Screen.Appearance.Scan.ScanX, 3.25);
    Screen.Appearance.Advance(2.3);
    assert.ok(Screen.Appearance.Scan.ScanX < 0.5 && Screen.Appearance.Scan.ScanX > -0.8, 'scan wave sweeps front-to-back');

    Click('view:0');
    assert.equal(Screen.Appearance.View, InspectionView.Solid);
});

test('paint drags expand radially outwards from the dropped 3D body point, reject wheels, and cancel safely', () => {
    const DropPoint = [1.42, -0.64, 0.48];
    const Screen = ConstructHeadlessGarage(() => ({ Occupant: 0, Family: CourseMaterial.BodyCoat, BodyPoint: DropPoint }));
    const Swatch = Screen.Regions.find(Region => Region.Identifier === 'colour:3');   // Tactical Blue
    const StartX = Swatch.Left + 20, StartY = Swatch.Top + 20;

    Screen.ForwardPointer('down', StartX, StartY);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);

    assert.equal(Screen.Appearance.Colour, '#1c3f73');
    assert.equal(Screen.Appearance.Ripple.Active, true);
    assert.equal(Screen.Appearance.Ripple.Mode, 'radial');
    assert.equal(Screen.Appearance.Ripple.ModeCode, 2);
    assert.deepEqual(Screen.Appearance.Ripple.Centre, DropPoint);

    Screen.Appearance.Advance(0.25);
    assert.ok(Screen.Appearance.ResolvePaintCoverage(DropPoint) > 0.99, 'drop origin covered immediately');
    assert.equal(Screen.Appearance.ResolvePaintCoverage([-2.5, 0.8, 0.2]), 0, 'far corner still unpainted early in radial wave');

    Screen.Appearance.Advance(1.20);
    assert.equal(Screen.Appearance.Ripple.Active, false);
    assert.deepEqual(Screen.Appearance.Materials[CourseMaterial.BodyCoat].Albedo, LinearColour('#1c3f73'));

    // Dropping body paint onto a wheel rejects cleanly without altering the body colour.
    const RedSwatch = Screen.Regions.find(Region => Region.Identifier === 'colour:2');
    Screen.Preview.Pick = () => ({ Occupant: 1, Family: CourseMaterial.Hub, BodyPoint: [1.7, 1.0, 0.0] });
    Screen.ForwardPointer('down', RedSwatch.Left + 20, RedSwatch.Top + 20);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.Colour, '#1c3f73');

    // Pointer cancel clears DragMaterial cleanly.
    Screen.ForwardPointer('down', RedSwatch.Left + 20, RedSwatch.Top + 20);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('cancel', 400, 450);
    assert.equal(Screen.DragMaterial, null);
});

test('rim finish changes affect selected accent faces, never the black rim base or tyre', () => {
    const Materials = ConstructMaterialSpecification();
    const Base = structuredClone(Materials[CourseMaterial.RimBase]);
    const Tyre = structuredClone(Materials[CourseMaterial.Tyre]);
    const Appearance = new VehicleAppearance(Materials, () => {});
    for (const Finish of [{ DarkWheels: true }, { DarkWheels: false, BlueRims: false }, { DarkWheels: false, BlueRims: true }, { RimColour: '#9a6a2f', MachinedWheels: true }]) {
        Appearance.Apply(Finish);
        assert.deepEqual(Materials[CourseMaterial.RimBase], Base);
        assert.deepEqual(Materials[CourseMaterial.Tyre], Tyre);
    }
    Appearance.Apply({ DarkWheels: false, BlueRims: true });
    assert.ok(Materials[CourseMaterial.Hub].Emission[2] > 0);
});

test('Garage studio grade selects progressive ReSTIR render extents', () => {
    assert.deepEqual(ResolveStudioExtent('low'), [256, 160]);
    assert.deepEqual(ResolveStudioExtent('medium'), [768, 480]);
    assert.deepEqual(ResolveStudioExtent('high'), [960, 600]);
    assert.deepEqual(ResolveStudioExtent('unknown'), [960, 600]);
    assert.deepEqual(ResolveStudioBudget('low'), { MaxBounces: 1, InitialCandidates: 1, ReflectionBounces: 1 });
    assert.deepEqual(ResolveStudioBudget('medium'), { MaxBounces: 2, InitialCandidates: 2, ReflectionBounces: 1 });
    assert.deepEqual(ResolveStudioBudget('high'), { MaxBounces: 3, InitialCandidates: 2, ReflectionBounces: 2 });
    assert.deepEqual(ResolveStudioBudget('unknown'), { MaxBounces: 3, InitialCandidates: 2, ReflectionBounces: 2 });
});

test('standalone Garage studio builds the Lobby set, emissive softbox clusters, and five architectural floods', () => {
    const Triangle = { Family: CourseMaterial.Headlight, Corners: [[0, 0, 0], [1, 0, 0], [0, 1, 0]] };
    const Source = {
        FacetSets: [{ Facets: [Triangle] }, { Facets: [] }, { Facets: [] }],
        VehicleBodySet: 0,
        VehicleOccupants: { Wheels: [0, 1, 2, 3] },
        Occupants: [{ FacetSet: 1 }, { FacetSet: 2 }, { FacetSet: 1 }, { FacetSet: 2 }]
    };
    const Preview = new StudioPreviewSequence({}, {});
    const Scene = Preview.ConstructStandaloneStudioScene(Source);
    assert.equal(Scene.FacetSets[Scene.VehicleBodySet].Facets[0].Family, CourseMaterial.StudioHeadlight);
    assert.equal(Scene.DynamicCount, 5, 'hero body and four independently moving wheels');
    assert.ok(Scene.Occupants.some(Occupant => Occupant.Name === 'Lobby studio ground'));
    assert.ok(Scene.Occupants.some(Occupant => Occupant.Name === 'Hero plates'));
    assert.ok(Scene.Occupants.some(Occupant => Occupant.Name.toLowerCase().includes('cyclorama')));
    assert.ok(Scene.DrawRuns.length > 0);

    const Materials = ConstructMaterialSpecification();
    const Emissive = new EmissiveLightSpace();
    Emissive.Construct(Scene, Materials);
    assert.ok(Emissive.TriangleCount > 0, 'studio softbox facets are sampled as emissive BVH lights');
    assert.ok(Emissive.Update(Scene, Materials, 1.6));
    assert.ok(Emissive.Count > 0);

    const Lights = ConstructStudioLighting();
    Lights.Update(new Float32Array(16), []);
    assert.equal(Lights.VehicleLightMode, AutomotiveMode.Off);
    assert.equal(Lights.Lights.length, 5);
    assert.ok(Lights.Lights.every(Light => Light.Type === LightType.Floodlight && Light.Intensity > 0));
    assert.equal(Lights.Lights[0].Intensity, 950);
    assert.ok(Lights.Lights.slice(1).every(Light => Light.Intensity === 430));
});

test('Garage studio material slots reserve headlight shading and independent wheel-wave target/control', () => {
    const Materials = ConstructMaterialSpecification();
    assert.ok(Materials[CourseMaterial.StudioHeadlight].CoatWeight >= 0.99);
    assert.ok(Materials[CourseMaterial.StudioHeadlight].CoatRoughness <= 0.03);
    assert.notEqual(Materials[CourseMaterial.WheelWaveTarget], Materials[CourseMaterial.Hub]);
    assert.notEqual(Materials[CourseMaterial.WheelWaveControl], Materials[CourseMaterial.Hub]);
});

test('Garage Lobby studio stage emits floor, hero turntable, seam ring, cove wall, and overhead softbox', () => {
    const FullLobby = EmitLobbyGroundFacets();
    const GarageStage = EmitLobbyGroundFacets({ Guests: false });
    assert.ok(GarageStage.length > 0 && GarageStage.length < FullLobby.length);
    const Families = new Set(GarageStage.map(Facet => Facet.Family));
    assert.ok(Families.has(CourseMaterial.StudioSurround));
    assert.ok(Families.has(CourseMaterial.StudioFloor));
    assert.ok(Families.has(CourseMaterial.StudioTurntable));
    assert.ok(Families.has(CourseMaterial.Surround));

    const CoveFamilies = new Set(LobbyShapes.Cyclorama.Emit().map(Facet => Facet.Family));
    const SoftboxFamilies = new Set(LobbyShapes.Softbox.Emit().map(Facet => Facet.Family));
    assert.ok(CoveFamilies.has(CourseMaterial.StudioWall));
    assert.ok(SoftboxFamilies.has(CourseMaterial.StudioSoftbox));
    assert.ok(SoftboxFamilies.has(CourseMaterial.StudioFrame));
});

