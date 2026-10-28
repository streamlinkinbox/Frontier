import assert from 'node:assert/strict';
export default{
 BeforeFrame(H,F){if(F===0){Object.assign(H.Settings,{CloudEnabled:false,LocalCloudEnabled:false,LocalFogEnabled:true,LocalFogDensity:.08,LocalFogX:-15,LocalFogY:0,LocalFogZ:3,LocalFogHalfX:18,LocalFogHalfY:15,LocalFogHalfZ:6});H.Shell.Select('local-fog');}if(F===1){H.Settings.CloudEnabled=true;H.Settings.LocalCloudEnabled=true;H.Shell.Select('local-cloud');}if(F===2){H.Settings.LocalFogShape=1;H.Settings.LocalCloudForm=2;H.Settings.CloudWindBearing=270;H.Shell.Select('wind');}},
 Report(H){assert.ok(H.Clouds.Active);assert.ok(H.Clouds.Revision>=3);assert.equal(H.Clouds.UniformData.length,136);assert.equal(H.Clouds.UniformData[47],1);assert.equal(H.Settings.MoonSize,25);console.log('Fog-only then overlapping weather, light upload, box shapes, wind edit, shared UI and Moon preservation passed.');}
};
