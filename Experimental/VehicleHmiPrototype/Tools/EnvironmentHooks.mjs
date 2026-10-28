import assert from 'node:assert/strict';
export default {
 BeforeFrame(Host, Frame) {
  const S=Host.Settings;
  const Panels=['sun','atmosphere','moon','stars','flare','fog','aerial'];
  if(Frame<Panels.length)Host.Shell.Select(Panels[Frame]);
  if(Frame===1){S.MultipleScattering=false;S.MoonInstances[0]={...S,MoonInstances:undefined,MoonEnabled:true,MoonBody:1,MoonAzimuth:100};}
  if(Frame===3){S.MultipleScattering=true;S.MoonRoll=23;S.MoonPitch=-12;}

  if(Frame===0) {S.FlareEnabled=true;S.FlareGhosts=24;S.FlareIntensity=1;}
  if(Frame===2) {S.SunElevation=-18;S.MoonElevation=45;S.MoonPhase=14.765;}
  if(Frame===4) {S.MoonElevation=-30;S.FogEnabled=true;S.FogDensity=.01;}
  if(Frame===6) {S.SunElevation=12;S.Mie=3;S.SunTemperature=3200;}
  if(Frame===8) {S.FlareEnabled=false;S.AerialEnabled=false;S.MoonEnabled=false;}
  if(Frame===10) {S.SunElevation=64;S.Rayleigh=1.5;}
 },
 Report(Host) {
  assert.equal(Host.Settings.SunElevation,64);
  assert.ok(Host.Atmosphere.LastKey);
  console.log('Environment: day/night, lunar horizon, dense fog, flare toggle and physical-medium changes exercised');
 }
};
