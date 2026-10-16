// Exercises a live mode change through input -> materials -> cluster upload ->
// card recapture/relighting -> direct and GI pipelines (Dawn null validates GPU
// commands and WGSL, not rendered pixel values).
import assert from 'node:assert/strict';
import { CourseMaterial as M } from '../Source/MaterialSpecification.js';
import { PhotometricProfiles as P } from '../Source/Photometry.js';
const Seen = new Set();
const PressI = (Shift = false) => {
    DispatchWindow('keydown', { code: 'KeyI', shiftKey: Shift });
    DispatchWindow('keyup', { code: 'KeyI' });
};
export default {
    BeforeFrame(Host, Frame) {
        if (Frame === 0) PressI();                         // off -> low
        if (Frame === 2) { PressI(true); assert.equal(Host.Settings.LampMode, 'Low'); }
        if (Frame === 4) PressI();                         // low -> high + roof
        if (Frame === 8) Host.Settings.RoofProfile = 'Spot';
        if (Frame === 12) PressI();                        // high -> fog
        if (Frame === 16) PressI();                        // fog -> off
        if (Frame === 20) Host.Settings.SceneLightsEnabled = false;
        if (Frame === 24) Host.Settings.SceneLightsEnabled = true;
        if (Frame > 0 && Host.Settings.InsetEnabled) Seen.add('inset');
        if (Frame > 0 && Host.Cards.LocalTiles.length > 0) Seen.add('nearby cards');
        if (Frame > 0 && Host.LightRig.SceneLights.length >= 20 && Host.Lighting.ClusterCount > 0) Seen.add('clusters');
        if (Frame === 1 && Host.Settings.LampMode === 'Low' && Host.LightRig.Lights.some((L) => L.Profile === P.LowBeam)
            && Host.Materials[M.Headlight].Emission[0] > 0) Seen.add('low');
        if (Frame === 5 && Host.LightRig.Lights.some((L) => L.Kind === 'Roof')
            && Host.Materials[M.RoofLED].Emission[0] > 0) Seen.add('roof');
        if (Frame === 9 && Host.LightRig.Lights.some((L) => L.Profile === P.RoofSpot)) Seen.add('roof spot');
        if (Frame === 13 && Host.LightRig.Lights.some((L) => L.Profile === P.FogBeam)
            && Host.Materials[M.FogLens].Emission[0] > 0 && Host.Materials[M.RoofLED].Emission[0] === 0) Seen.add('fog');
        if (Frame === 17 && Host.Settings.LampMode === 'Off'
            && Host.Materials[M.Headlight].Emission[0] === 0 && !Host.LightRig.Lights.some((L) => L.Kind === 'Vehicle')) Seen.add('off');
        if (Frame === 21 && Host.Materials[M.LampWarm].Emission[0] === 0
            && !Host.LightRig.Lights.some((L) => L.Kind === 'Scene')) Seen.add('scene off');
        if (Frame === 25 && Host.Materials[M.LampWarm].Emission[0] > 0
            && Host.LightRig.Lights.some((L) => L.Kind === 'Scene')) Seen.add('scene on');
    },
    Report(Host) {
        for (const Name of ['inset', 'clusters', 'low', 'roof', 'roof spot', 'fog', 'off', 'scene off', 'scene on', 'nearby cards'])
            assert.ok(Seen.has(Name), `missing lighting integration: ${Name}`);
        assert.equal(Host.LightRig.LastKey, Host.LightRig.QueryLightingKey());
        console.log('lighting integration:', [...Seen].join(', '));
    }
};
