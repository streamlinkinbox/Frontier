import assert from 'node:assert/strict';
export default{
 BeforeFrame(H,F){if(F===0){Object.assign(H.Settings,{SunElevation:-18,SunAutoPosition:false,DayAnimate:false,CloudEnabled:false,LocalCloudEnabled:false,FlareEnabled:false,MoonBody:0,MoonSize:25,MoonAzimuth:-8,MoonElevation:38,SolarIlluminance:0});H.FreeCamera=true;H.Flight.Eye=[-47,0,3];H.Flight.Yaw=-8*Math.PI/180;H.Flight.Pitch=38*Math.PI/180;H.Shell.Select('stars');}if(F===1)H.Shell.Select('moon');},
 Report(H){assert.equal(H.Atmosphere.Light.Source,'Moon');assert.ok(H.Atmosphere.Light.Illuminance.some(V=>V>0));assert.equal(H.Settings.MoonSize,25);assert.equal(H.Settings.MoonBody,0);console.log('Clear night stars and Earth Moon, 25 degrees; moonlight active with solar power zero.',H.Atmosphere.Light.Illuminance);}
};
