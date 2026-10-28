import {test} from 'node:test';import assert from 'node:assert/strict';
import {CloudDashboardData,SetCloudDashboardValue} from '../Source/CloudInspectorDashboard.js';
import {CloudDefaults} from '../Source/CloudSettings.js';
test('cloud observatory derives its readouts from current renderer inputs, never invented telemetry',()=>{
 const H={Settings:{...CloudDefaults,CloudResolution:50,CloudBase:1500,CloudThickness:2000,CloudCeiling:2500},ScreenExtent:[1920,1080]};const D=CloudDashboardData(H);assert.equal(D.Top,2500);assert.equal(D.Depth,1000);assert.equal(D.Clipped,true);assert.equal(D.PixelFraction,.25);assert.deepEqual(D.Extent,[960,540]);assert.equal(D.GpuMs,null);H.Metrics={Enabled:true,Durations:{Clouds:.18}};assert.equal(CloudDashboardData(H).GpuMs,.18);H.Metrics.Durations.Clouds=0;assert.equal(CloudDashboardData(H).GpuMs,null);H.Metrics.Enabled=false;assert.equal(CloudDashboardData(H).GpuMs,null);
});
test('direct inspector fields preserve renderer bounds and base/ceiling invariants',()=>{
 const S={...CloudDefaults};assert.ok(SetCloudDashboardValue(S,'CloudCoverage',2));assert.equal(S.CloudCoverage,1);SetCloudDashboardValue(S,'CloudDensity',-2);assert.equal(S.CloudDensity,0);SetCloudDashboardValue(S,'CloudCeiling',4000);SetCloudDashboardValue(S,'CloudBase',9000);assert.equal(S.CloudBase,4000);SetCloudDashboardValue(S,'CloudSteps',30.7);assert.equal(S.CloudSteps,31);assert.equal(SetCloudDashboardValue(S,'CloudCoverage',NaN),false);assert.equal(SetCloudDashboardValue(S,'ExposureCompensation',1),false);
});
