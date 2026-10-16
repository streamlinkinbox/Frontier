import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { ParseIes, SampleIes, RetrievePhotometricAtlas, PhotometricProfiles, PhotometrySize } from '../Source/Photometry.js';
import { LightRig, ConstructSceneLights, CycleLightMode } from '../Source/LightRig.js';
import { ClusteredLighting, ClusterConfiguration } from '../Source/ClusteredLighting.js';
import { ConstructVehicleLightingFacets, ConstructStreetPoleFacets, ConstructStreetHeadFacets } from '../Source/LightingFixtures.js';
import { CourseMaterial, ConstructMaterialSpecification } from '../Source/MaterialSpecification.js';
import { ResolveLevelPlacements } from '../Source/LevelStructure.js';
import { ConstructRigidTransform } from '../Source/TransformProjection.js';

const ProfileText = (Name) => fs.readFileSync(new URL(`../Profiles/${Name}.ies`, import.meta.url), 'utf8');
const DefaultSettings = () => ({ LampMode: 'Off', SceneLightsEnabled: true, AutoSceneLights: true, SceneLightPower: 1,
    SunElevation: 64, LowBeamPower: 1, HighBeamPower: 1, FogBeamPower: 1, RoofEnabled: true,
    RoofBeamPower: 1, RoofProfile: 'Flood', BeamAim: -2, RoofAim: -4, EmissionScale: 1, SmokeEnabled: false });

// The delivered curves are demonstration photometry, not certified measurements.
test('LM-63 Type C files resample into a complete finite atlas; low, high and fog have distinct distributions', async () => {
    const Names = ['Roadway', 'Area', 'LowBeam', 'HighBeam', 'FogBeam', 'RoofFlood', 'RoofSpot'];
    const Profiles = Names.map((Name) => ParseIes(ProfileText(Name)));
    const Atlas = await RetrievePhotometricAtlas(async (Name) => ProfileText(Name));
    assert.equal(Atlas.length, (Names.length + 1) * PhotometrySize.Azimuth * PhotometrySize.Polar);
    assert.ok(Atlas.every((Value) => Number.isFinite(Value) && Value >= 0 && Value <= 1.00001));
    assert.equal(PhotometricProfiles.Roadway, 1);
    assert.ok(SampleIes(Profiles[2], 180, 6) > SampleIes(Profiles[2], 0, 6), 'low cutoff suppresses upward light');
    assert.ok(SampleIes(Profiles[2], 270, 25) > SampleIes(Profiles[2], 90, 25), 'left verge gets dipped-beam upsweep');
    assert.ok(SampleIes(Profiles[3], 0, 6) > SampleIes(Profiles[2], 0, 6), 'high beam reaches above the low cutoff');
    assert.ok(SampleIes(Profiles[4], 90, 40) > SampleIes(Profiles[3], 90, 40), 'fog beam spreads laterally');
    assert.ok(SampleIes(Profiles[6], 0, 20) < SampleIes(Profiles[5], 0, 20), 'roof spot differs from flood');
});

test('photometry rejects tilted, incomplete and non-Type-C IES; symmetric C-planes reflect correctly', () => {
    const Symmetric = 'IESNA:LM-63-2002\nTILT=NONE\n1 1000 1 3 2 1 2 1 1 1\n1 1 60\n0 90 180\n0 90\n1 2 3\n4 5 6\n';
    const P = ParseIes(Symmetric);
    assert.equal(SampleIes(P, 90, 90), SampleIes(P, 270, 90));
    assert.throws(() => ParseIes(Symmetric.replace('TILT=NONE', 'TILT=INCLUDE')), /TILT=NONE/);
    assert.throws(() => ParseIes(Symmetric.replace('3 2 1 2', '3 2 2 2')), /Type C/);
    assert.throws(() => ParseIes(Symmetric.slice(0, -8)), /incomplete/);
});

test('faceted street luminaires and roof bar contain separate metal, seals, reflectors, lenses and dies', () => {
    const Roof = ConstructVehicleLightingFacets();
    const Head = ConstructStreetHeadFacets(), Pole = ConstructStreetPoleFacets();
    const Count = (Facets, Material) => Facets.filter((Facet) => Facet.Family === Material).length;
    assert.equal(Count(Roof, CourseMaterial.RoofLED), 9 * 12);
    assert.equal(Count(Head, CourseMaterial.LampWarm), 8 * 12);
    assert.ok(Count(Roof, CourseMaterial.LampGasket) > 0 && Count(Head, CourseMaterial.LampDiffuser) > 0);
    assert.ok(Count(Roof, CourseMaterial.FogLens) > 0 && Count(Pole, CourseMaterial.LampPole) > 0);
    assert.ok(Pole.some((Facet) => Facet.Family === CourseMaterial.LampPole
        && Facet.Corners.some(([X, , Z]) => X > 0.7 && Z > 10.855)), 'the arm joins the shifted pole head');
    const High = Math.max(...Roof.flatMap((Facet) => Facet.Corners.map((Point) => Point[2])));
    assert.ok(High > 1.5 && High < 1.7, 'bar sits on, not above or inside, the 1.35 m model-space roof');
    const Placements = ResolveLevelPlacements();
    assert.ok(Placements.some((P) => P.Shape === 'Pole' && P.Position[0] === -38 && P.Position[1] === 8));
    assert.equal(Placements.filter((P) => P.Shape === 'PoleHead').length, 21);
});

test('vehicle modes switch actual optics and translated/rotated light positions, not just an emissive mesh', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Exchange = { Device: { queue: { writeBuffer: (...Args) => Writes.push(Args) } } };
    const Settings = DefaultSettings();
    const Rig = new LightRig(Materials, {}, Exchange, Settings);
    const Transform = ConstructRigidTransform([10, -3, 0.53], [0, 0, Math.sin(Math.PI / 4), Math.cos(Math.PI / 4)]);
    assert.deepEqual(['Off', 'Low', 'High', 'Fog', 'Off'], Array.from({ length: 5 }, (_, Index) => {
        if (Index) Settings.LampMode = CycleLightMode(Settings.LampMode);
        return Settings.LampMode;
    }));
    assert.equal(Rig.SceneActive(), false, 'daylight automation switches street sources off');
    Rig.SynchroniseMaterials();
    assert.equal(Materials[CourseMaterial.LampWarm].Emission[0], 0);
    Settings.AutoSceneLights = false;
    assert.equal(Rig.SceneActive(), true, 'manual mode forces street lights on in daylight');
    Settings.AutoSceneLights = true;
    Settings.SunElevation = -14;
    assert.equal(Rig.SceneActive(), true);
    Rig.SynchroniseMaterials();
    assert.equal(Materials[CourseMaterial.LampWarm].Emission[0], 14);

    Settings.LampMode = 'Low';
    Rig.SynchroniseMaterials();
    let Lights = Rig.Resolve(Transform, null, null, null);
    assert.equal(Lights.filter((L) => L.Kind === 'Vehicle').length, 2);
    assert.equal(Lights.filter((L) => L.Kind === 'Roof').length, 0);
    assert.ok(Lights[0].Position[1] > -3, 'car headlamp moved +X into world +Y after yaw rotation');
    assert.ok(Materials[CourseMaterial.Headlight].Emission[0] > 0);

    Settings.LampMode = 'High'; Settings.RoofProfile = 'Spot';
    Rig.SynchroniseMaterials(); Lights = Rig.Resolve(Transform, null, null, null);
    assert.equal(Lights.filter((L) => L.Kind === 'Roof').length, 3);
    assert.ok(Lights.filter((L) => L.Kind === 'Roof').every((L) => L.Profile === PhotometricProfiles.RoofSpot));
    assert.ok(Materials[CourseMaterial.RoofLED].Emission[2] > 0);
    Settings.RoofEnabled = false;
    Rig.SynchroniseMaterials();
    assert.equal(Rig.Resolve(Transform, null, null, null).filter((L) => L.Kind === 'Roof').length, 0);
    assert.deepEqual(Materials[CourseMaterial.RoofLED].Emission, [0, 0, 0]);

    Settings.LampMode = 'Fog';
    Rig.SynchroniseMaterials(); Lights = Rig.Resolve(Transform, null, null, null);
    assert.equal(Lights.filter((L) => L.Profile === PhotometricProfiles.FogBeam).length, 2);
    assert.ok(Materials[CourseMaterial.FogLens].Emission[0] > 0);
    Settings.LampMode = 'Off'; Settings.SceneLightsEnabled = false;
    Rig.SynchroniseMaterials(); Lights = Rig.Resolve(Transform, null, null, null);
    assert.equal(Lights.length, 0);
    assert.deepEqual(Materials[CourseMaterial.Headlight].Emission, [0, 0, 0]);
    assert.deepEqual(Materials[CourseMaterial.FogLens].Emission, [0, 0, 0]);
    assert.ok(Writes.length > 5 && Writes.every(([, Offset, Data]) => Offset % 48 === 0 && Data.byteLength === 48));
});

test('cluster storage and GPU upload bound per-cell indices, match 64-byte light records and resize cleanly', () => {
    globalThis.GPUBufferUsage = { STORAGE: 1 };
    const Calls = [], E = {
        ConstructRecords: (Size) => ({ Size, destroy() { this.Destroyed = true; } }),
        ConstructDescriptorGroup: (Layout, Resources) => ({ Layout, Resources }),
        Device: { queue: { writeBuffer(...Args) { Calls.push(Args); } } }
    };
    const Lighting = new ClusteredLighting(E, {}, {});
    Lighting.LightRecords = {}; Lighting.ConfigurationRecords = {};
    Lighting.PhotometryRecords = {}; Lighting.Layout = {}; Lighting.BuildLayout = {};
    Lighting.Resize(1280, 720);
    const C = ClusterConfiguration;
    assert.equal(Lighting.ClusterCount, 40 * 23 * C.Slices);
    assert.equal(Lighting.ClusterRecords.Size, Lighting.ClusterCount * (1 + C.PerCluster) * 4);
    const Previous = Lighting.ClusterRecords;
    Lighting.Resize(1024, 512);
    assert.equal(Previous.Destroyed, true);
    const Lights = ConstructSceneLights();
    Lighting.Upload([...Lights, ...Lights], { VerticalFieldOfView: Math.PI / 3, Aspect: 2 });
    assert.equal(Lighting.Count, C.Capacity);
    const LightData = Calls[0][2];
    assert.equal(LightData.length, C.Capacity * 16);
    assert.equal(LightData[3], Lights[0].Reach);
    assert.equal(LightData[15], PhotometricProfiles.Area);
    const Config = Calls[1][2];
    assert.equal(new Uint32Array(Config)[2], C.Capacity);
    assert.equal(new Uint32Array(Config)[3], C.Slices);
    assert.equal(new Float32Array(Config)[4], 1024);
    assert.equal(new Float32Array(Config)[5], 512);
    assert.ok(Lighting.QueryReadout()[1].includes('clusters'));
});

test('GI card and screen shaders both read the new local-light transport, not legacy fire/transient slots', () => {
    const Read = (Name) => fs.readFileSync(new URL(`../Shader/${Name}.wgsl`, import.meta.url), 'utf8');
    assert.ok(Read('CardDirect').includes('Cluster.LightCount'));
    assert.ok(Read('DirectRadiance').includes('ClusterMembers[Base + 1u + Member]'));
    assert.ok(Read('ForwardRaster').includes('ResolveLocalArrival'));
    assert.ok(Read('LightTransport').includes('TraverseSceneShadow'));
    assert.ok(!/TransientLights|FireLightPosition/.test(Read('SceneRecords')));
});
