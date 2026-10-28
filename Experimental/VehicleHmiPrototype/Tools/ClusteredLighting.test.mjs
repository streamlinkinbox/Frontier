import test from "node:test";
import assert from "node:assert/strict";
import {
    LightingSystem,
    LightType,
    RoofFixtureType,
    AutomotiveMode,
    MaxSceneLights,
    LightBufferFloats,
    ConstructRoofFixtureFacets
} from "../Source/LightingSystem.js";
import { ResolveLevelPlacements } from "../Source/LevelStructure.js";
import { ConstructIdentityTransform } from "../Source/TransformProjection.js";

test("LightingSystem manages automotive light modes and roof fixtures correctly", () => {
    const sys = new LightingSystem();
    assert.equal(sys.VehicleLightMode, AutomotiveMode.LowBeam);
    assert.equal(sys.RoofLightEnabled, false);
    assert.equal(sys.RoofFixture, RoofFixtureType.SlimLED);

    // Cycle modes
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.HighBeam);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.Fog);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.Off);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.LowBeam);

    // Toggle roof light
    assert.equal(sys.ToggleRoofLight(), true);
    assert.equal(sys.RoofLightEnabled, true);
    assert.equal(sys.ToggleRoofLight(), false);
    assert.equal(sys.RoofLightEnabled, false);

    // Switch roof fixture
    sys.SetRoofFixture(RoofFixtureType.RallyPod);
    assert.equal(sys.RoofFixture, RoofFixtureType.RallyPod);
    sys.SetRoofFixture(RoofFixtureType.SlimLED);
    assert.equal(sys.RoofFixture, RoofFixtureType.SlimLED);
});

test("LightingSystem packs street lamps, point lights, automotive headlights, and 16x16x4 3D cluster bitmasks", () => {
    const sys = new LightingSystem();
    const placements = ResolveLevelPlacements();
    const poles = placements.filter(p => p.Shape === "PoleHead").map(p => ({
        X: p.Position[0],
        Y: p.Position[1],
        Z: p.Position[2]
    }));
    const pointLamps = placements.filter(p => p.Shape === "LampLantern").map(p => ({
        X: p.Position[0],
        Y: p.Position[1],
        Z: p.Position[2]
    }));
    assert.equal(poles.length, 20);
    assert.equal(pointLamps.length, 10);

    const vehicleTransform = ConstructIdentityTransform();
    const buffer = sys.Update(vehicleTransform, poles, pointLamps);

    assert.equal(buffer.length, LightBufferFloats);
    assert.ok(sys.Lights.length <= MaxSceneLights);
    assert.ok(sys.ActiveClusterCells > 0);
    assert.ok(sys.ActiveClusterCells < 1024);

    // Street lamps check
    const streetLamps = sys.Lights.filter(l => l.Type === LightType.StreetLamp);
    assert.equal(streetLamps.length, 20);
    assert.equal(streetLamps[0].Direction[2], -1.0);

    // Clustered omnidirectional point lights check
    const omniLights = sys.Lights.filter(l => l.Type === LightType.Omni);
    assert.equal(omniLights.length, 10);

    // Headlights check (Low beam default)
    const lowBeams = sys.Lights.filter(l => l.Type === LightType.LowBeam);
    assert.equal(lowBeams.length, 2);

    // Verify 3D cluster culling: start grid cell (-38, 0, 1.0) contains nearby point lights, not east-arena poles
    const [maskLoStart, maskHiStart] = sys.QueryClusterMask(-38.0, 0.0, 1.0);
    assert.ok((maskLoStart | maskHiStart) !== 0);
    // Point light 0 at (-38, 7.78, 5.0) is light index 20 -> bit 20 in maskLo
    assert.ok((maskLoStart & (1 << 20)) !== 0);
    // East arena pole 19 at (145, 24, 10.55) is light index 19 -> must be culled (0) at X = -38
    assert.equal(maskLoStart & (1 << 19), 0);

    // Verify cone culling: vehicle at origin (0, 0, 0) facing +X should not assign low-beam bits to cells behind it (-40, 0, 1)
    const lowBeamIdx0 = sys.Lights.findIndex(l => l.Type === LightType.LowBeam);
    assert.ok(lowBeamIdx0 >= 0 && lowBeamIdx0 < 32);
    const [maskLoAhead] = sys.QueryClusterMask(20.0, 0.0, 1.0);
    assert.ok((maskLoAhead & (1 << lowBeamIdx0)) !== 0);
    assert.equal(maskLoStart & (1 << lowBeamIdx0), 0);

    // Switch to High beam
    sys.SetVehicleMode(AutomotiveMode.HighBeam);
    sys.Update(vehicleTransform, poles, pointLamps);
    const highBeams = sys.Lights.filter(l => l.Type === LightType.HighBeam);
    assert.equal(highBeams.length, 2);

    // Enable roof light
    sys.ToggleRoofLight();
    sys.SetRoofFixture(RoofFixtureType.SlimLED);
    sys.Update(vehicleTransform, poles, pointLamps);
    const slimFloods = sys.Lights.filter(l => l.Type === LightType.Floodlight);
    assert.equal(slimFloods.length, 1);

    // Switch to Rally pods
    sys.SetRoofFixture(RoofFixtureType.RallyPod);
    sys.Update(vehicleTransform, poles, pointLamps);
    const rallyFloods = sys.Lights.filter(l => l.Type === LightType.Floodlight);
    assert.equal(rallyFloods.length, 2);
});

test("Roof fixture polygon generation produces valid geometry for SlimLED and RallyPod", () => {
    const slim = ConstructRoofFixtureFacets(RoofFixtureType.SlimLED);
    const rally = ConstructRoofFixtureFacets(RoofFixtureType.RallyPod);

    assert.ok(slim.length > 0);
    assert.ok(rally.length > 0);

    for (const facet of [...slim, ...rally]) {
        assert.equal(facet.Corners.length, 3);
        assert.equal(typeof facet.Family, "number");
        for (const pt of facet.Corners) {
            assert.equal(pt.length, 3);
            for (const coord of pt) {
                assert.ok(!Number.isNaN(coord));
                assert.ok(Number.isFinite(coord));
            }
        }
    }
});
