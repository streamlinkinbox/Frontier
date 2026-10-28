import {LightningRanges} from './LightningSettings.js';
import {CloudDefaults,CloudRanges} from './CloudSettings.js';
import {EnvironmentDefaults} from './EnvironmentSettings.js';
const StorageKey='Frontier.Environment.v1';
const Extras={SunElevation:38,SunAzimuth:145,SolarIlluminance:12,SkyScale:1};
const Defaults={...EnvironmentDefaults,...Extras};
export function SanitiseEnvironment(Input){const Out={};if(!Input||typeof Input!=='object')return Out;
 for(const [K,Default]of Object.entries(Defaults)){
  const V=Input[K];if(V===undefined)continue;
  if(K==='MoonInstances'){if(Array.isArray(V))Out[K]=V.slice(0,3).map(M=>{const R=SanitiseEnvironment({...M,MoonInstances:undefined});delete R.MoonInstances;return {...structuredClone(EnvironmentDefaults),...R,MoonInstances:undefined};});}
  else if(typeof Default==='number'&&typeof V==='number'&&Number.isFinite(V))Out[K]=Math.max(-1e6,Math.min(1e6,V));
  else if(typeof Default==='boolean'&&typeof V==='boolean')Out[K]=V;
  else if(typeof Default==='string'&&typeof V==='string'&&V.length<100)Out[K]=V;
  else if(Array.isArray(Default)&&Array.isArray(V)&&V.length===3&&V.every(C=>typeof C==='number'&&Number.isFinite(C)))Out[K]=V.map(C=>Math.max(0,Math.min(1,C)));
 }
 for(const [K,Lo,Hi]of [['SunGlow',0,3],['StarHalo',0,2],['MoonLightIntensity',0,5],['StarBrightness',0,5],['MoonBody',0,5],['MoonPhase',0,29.53],['MoonSize',.1,180],['MoonBrightness',0,5],['MoonElevation',-90,90],['MoonPitch',-180,180],['SunElevation',-90,90],['SunRadius',.05,2],['SunTemperature',2000,10000],['RayleighHeight',1000,16000],['MieHeight',500,8000],['MieG',-.95,.95],['FogHeight',1,500],['FogDensity',0,.05],['AerialDensity',0,.001],['Latitude',-90,90],['Longitude',-180,180],['StarLimit',0,8],['StarSize',.2,3],['FlareGhosts',0,24],['FlareGhostShape',0,8],['FlareSpread',.25,2],['FlareIntensity',0,5],['DayDuration',.01,24],['TierSamples',8,64]])if(K in Out)Out[K]=Math.max(Lo,Math.min(Hi,Out[K]));
 for(const [K,[Lo,Hi]]of Object.entries({...CloudRanges,...LightningRanges}))if(K in Out)Out[K]=Math.max(Lo,Math.min(Hi,Out[K]));
 for(const K of ['LightningDetail','LightningMode','LightningCloudTarget'])if(K in Out)Out[K]=Math.round(Out[K]);
 if('MoonBody'in Out)Out.MoonBody=Math.round(Out.MoonBody);
 if(Out.SkyDate&&!/^\d{4}-\d{2}-\d{2}$/.test(Out.SkyDate))delete Out.SkyDate;
 return Out;
}
export function LoadEnvironment(Settings){try{const Input=JSON.parse(localStorage.getItem(StorageKey)||'null');Object.assign(Settings,SanitiseEnvironment(Input));if(Input&&Input.FogModelVersion!==1){for(const K of Object.keys(CloudDefaults))if(K.startsWith('LocalFog')||K==='FogModelVersion')Settings[K]=structuredClone(CloudDefaults[K]);}if(Input&&Input.CloudRendererVersion!==2)for(const K of ['CloudResolution','CloudSteps','CloudLocalSteps','CloudRendererVersion'])Settings[K]=CloudDefaults[K];if(Input&&Input.CelestialRendererVersion!==2)for(const K of ['CelestialRendererVersion','MoonBody','MoonSize','StarBrightness','StarSize','StarLimit'])Settings[K]=EnvironmentDefaults[K];}catch{}}
export function SaveEnvironment(Settings){try{const Value={};for(const K of Object.keys(Defaults))Value[K]=Settings[K];localStorage.setItem(StorageKey,JSON.stringify(Value));}catch{}}
