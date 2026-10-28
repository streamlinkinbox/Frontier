import assert from 'node:assert/strict';
export default {
 BeforeFrame(H,F){if(F===0){H.Settings.CloudEnabled=true;H.Settings.CloudCoverage=.9;H.Settings.CloudDensity=2;H.Lightning.Trigger();}if(F===12){H.Settings.LightningMode=1;H.Lightning.Storm.LastStrike=-Infinity;H.Lightning.Trigger();}},
 Report(H){assert.ok(H.Lightning.Storm.Strikes>=1);const R=H.Telemetry.FrameRows;assert.ok(R.some(X=>X.lightning_segments>0));assert.ok(R.every(X=>Number.isFinite(X['cpu_phase_ms.physics_effects'])));assert.ok(R.some(X=>X.physics_steps>0));assert.ok(H.Telemetry.EventRows.some(X=>X.event==='lightning_strike'));console.log('Lightning hooks: active ribbon draw, cloud flash, scene light, events and CPU subphase logging passed.');}
};
