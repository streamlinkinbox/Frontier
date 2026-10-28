import {LightningDefaults} from './LightningSettings.js';
import {CloudDefaults} from './CloudSettings.js';
import { ResolveSunTransmittance } from "./AtmosphereIntegrator.js";
const MoonTransCache = new Map();
import { MoonBodies } from "./CelestialAtlas.js";
/* Native reference: SultanAladin/Frontier- Engine/Editor/*InspectorPanel.cpp.
   Renderer units: metres, degrees, seconds; intensity uses the exhibit scale. */
export const EnvironmentDefaults = {...CloudDefaults,...LightningDefaults,"SunGlow":1,"StarHalo":1,"CelestialRendererVersion":2,"MoonLightEnabled":true,"MoonLightIntensity":1,"SunColourSource":"Temperature","SunTint":[1,1,1],"SkyTint":[1,1,1],"AtmosphereEnabled":true,"HorizonGlow":1,"WhiteLine":1,"LineAtCivilOnly":true,"AirMass":true,"TierSamples":40,"MultipleScattering":true,"GroundAlbedo":[0.3,0.3,0.3],"SunEnabled":true,"SunDiscEnabled":true,"SunAutoPosition":false,"SunTime":12,"DayAnimate":false,"DayDuration":6,"SkyDate":"2026-10-20","Latitude":-26.18,"Longitude":28.32,"TimeZone":2,"MoonFollowSky":false,"MoonBody":0,"MoonRoll":0,"MoonPitch":0,"MoonSpin":0,"MoonGlow":0.2,"MoonInstances":[],"PlanetsEnabled":true,"PlanetBrightness":1,"TwinkleEnabled":true,"FlareGhostSpacing":1,"FlareGhostShape":6,"FlareAnamorphic":true,"FlareWarmStreak":false,"FlareBurst":true,"FlareAperture":6,"FlarePreviewX":0.28,"FlarePreviewY":0.43,"FogColour":[0.52,0.62,0.69],"AerialMie":0.5,"FlareEnabled": true, "FlareIntensity": 0.3, "FlareSpread": 1, "FlareGhosts": 6, "FlareGhostBrightness": 0.3, "FlareHaloRadius": 0.22, "FlareHaloWidth": 0.025, "FlareHaloBrightness": 0.2, "FlareStreak": 0.3, "FlareRotation": 0, "FlareRays": 6, "FlareChromatic": 0.2, "FlarePreset": "Cinematic","Rayleigh": 1, "Mie": 1, "Ozone": 1, "RayleighHeight": 8000, "MieHeight": 1200, "MieG": 0.8, "SunTemperature": 5778, "SunRadius": 0.27, "StarsEnabled": true, "StarLimit": 6, "StarBrightness": 2, "StarSize": 1, "TwinkleDepth": 0.25, "TwinkleRate": 1, "StarRotation": 0, "MilkyWay": 1, "MoonEnabled": true, "MoonAzimuth": -8, "MoonElevation": 38, "MoonPhase": 14.765, "MoonSize": 25, "MoonBrightness": 1, "FogEnabled": false, "FogDensity": 0.002, "FogHeight": 30, "FogScatter": 0.5, "AerialEnabled": true, "AerialDensity": 4e-05, "AerialStart": 100};
export function EncodeEnvironment(S, Light) {
 const Packed = [
 ...Light.SunDirection, S.SunRadius * Math.PI/180,
 S.StarsEnabled ? S.StarBrightness : 0, S.StarLimit, S.StarSize, S.StarRotation*Math.PI/180,
 S.TwinkleDepth, S.TwinkleRate, S.MilkyWay, S.SunEnabled===false?-18:S.SunElevation,
 Math.cos(S.MoonElevation*Math.PI/180)*Math.cos(S.MoonAzimuth*Math.PI/180), Math.cos(S.MoonElevation*Math.PI/180)*Math.sin(S.MoonAzimuth*Math.PI/180), Math.sin(S.MoonElevation*Math.PI/180), S.MoonSize*Math.PI/360,
 S.MoonPhase/29.53*2*Math.PI, S.MoonEnabled ? S.MoonBrightness : 0, S.FogEnabled ? S.FogDensity : 0, S.FogHeight,
 S.FogScatter, S.AerialEnabled ? S.AerialDensity : 0, S.AerialStart, S.SunTemperature,
 S.FlareEnabled ? S.FlareIntensity : 0, S.FlareSpread, S.FlareGhosts, S.FlareGhostBrightness,
 S.FlareHaloRadius, S.FlareHaloWidth, S.FlareHaloBrightness, S.FlareStreak,
 S.FlareRotation*Math.PI/180, S.FlareRays, S.FlareChromatic, S.FlarePreset==='Custom layers'?1:0,
 ...(Light.SolarIlluminance ?? [0,0,0]), S.SunGlow??1
 ];
 Packed[8] = S.TwinkleEnabled ? S.TwinkleDepth : 0;
 Packed.push(S.FlareGhostSpacing,S.FlareGhostShape,S.FlareAnamorphic?1:0,S.FlareWarmStreak?1:0);
 Packed.push(S.FlareBurst?1:0,S.FlareAperture,S.SunDiscEnabled?1:0,S.PlanetsEnabled?S.PlanetBrightness:0);
 for(let I=0;I<4;I++) {
  const M=I===0?S:S.MoonInstances?.[I-1];
  const B=MoonBodies[M?.MoonBody??0];const A=(M?.MoonAzimuth??0)*Math.PI/180,E=(M?.MoonElevation??0)*Math.PI/180;
  Packed.push(Math.cos(E)*Math.cos(A),Math.cos(E)*Math.sin(A),Math.sin(E),(M?.MoonRoll??0)*Math.PI/180);
  Packed.push((M?.MoonSize??25)*Math.PI/360,M?.MoonEnabled?M.MoonBrightness:0,(M?.MoonPhase??0)/29.53*2*Math.PI,M?.MoonGlow??0);
  Packed.push((M?.MoonSpin??0)*Math.PI/180,(B.Tilt+(M?.MoonPitch??0))*Math.PI/180,B.Haze,B.Gamma);
  const Key=JSON.stringify([A,E,S.Rayleigh,S.Mie,S.Ozone,S.RayleighHeight,S.MieHeight]);
  let Cached=MoonTransCache.get(I);
  if(!Cached || Cached.Key!==Key){Cached={Key,Value:ResolveSunTransmittance([Math.cos(E)*Math.cos(A),Math.cos(E)*Math.sin(A),Math.sin(E)],S)};MoonTransCache.set(I,Cached);}
  Packed.push(M?.MoonBody??0,...Cached.Value);
 }
 for(let I=0;I<4;I++) Packed.push(...(Light.Planets?.[I]?.Direction??[0,0,-1]),1);
 Packed.push(...S.FogColour,S.AerialMie);
 Packed.push(Light.PixelAngle??.0015,Light.Sidereal??0,S.Latitude*Math.PI/180,S.StarHalo??1);
 Packed.push(S.HorizonGlow,S.WhiteLine,S.LineAtCivilOnly?1:0,S.AtmosphereEnabled?1:0);
 return new Float32Array(Packed);
}
