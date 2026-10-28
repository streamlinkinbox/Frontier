import {test} from 'node:test';
import assert from 'node:assert/strict';
import {FlareGhostLayout,FlareRayPairs} from '../Source/NativeFlarePanel.js';
import {EnvironmentDefaults} from '../Source/EnvironmentSettings.js';
test('ghost guide uses actual kernel reflection centres',()=>{const S={...EnvironmentDefaults,FlareGhosts:4,FlareGhostSpacing:1},P=FlareGhostLayout(S);assert.equal(P.length,4);for(let I=0;I<4;I++){const K=1-(.6+(I+1)/4*2.7);assert.deepEqual(P[I],[.5+(S.FlarePreviewX-.5)*K,.5+(S.FlarePreviewY-.5)*K]);}assert.deepEqual(FlareGhostLayout({...S,FlareGhosts:0}),[]);assert.equal(FlareGhostLayout({...S,FlareGhosts:99}).length,24);});
test('ray readout respects renderer legacy aperture versus custom ray semantics',()=>{const S={...EnvironmentDefaults,FlareRays:10,FlareAperture:6};assert.equal(FlareRayPairs(S),6);assert.equal(FlareRayPairs({...S,FlarePreset:'Custom layers'}),10);assert.equal(FlareRayPairs({...S,FlareAperture:20}),12);});
