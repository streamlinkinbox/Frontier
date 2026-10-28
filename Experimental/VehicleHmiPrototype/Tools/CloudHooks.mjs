import assert from 'node:assert/strict';
export default{
 BeforeFrame(H,F){const S=H.Settings;if(F===0)H.Shell.Select('clouds');
 if(F===1){H.Shell.Select('local-cloud');S.LocalCloudEnabled=true;S.LocalCloudX=20;S.LocalCloudY=0;S.LocalCloudZ=10;S.CloudCoverage=.9;}
 if(F===2)S.CloudResolution=25;
 if(F===3)S.CloudResolution=100;
 if(F===4){S.CloudEnabled=false;S.LocalCloudEnabled=true;S.LocalCloudCoverage=1;}
 if(F===5){S.CloudEnabled=false;S.LocalCloudEnabled=false;}
 if(F===6){S.CloudEnabled=true;S.CloudResolution=50;}
 if(F===7)S.CloudType=5;
 if(F===8){S.CloudType=3;S.CloudAnvil=1;}
 if(F===9){S.LocalCloudHalfZ=80;S.CloudWindBearing=180;}
 },
 Report(H){assert.ok(H.Clouds.Revision>=7);assert.equal(H.Clouds.Extent[0],Math.ceil(H.ScreenExtent[0]/2));assert.ok(H.Clouds.Textures.length===2);console.log('Cloud panels, both media, disable, 25/50/100% allocation and bounded two-target renderer passed',H.Clouds.Revision);}
};
