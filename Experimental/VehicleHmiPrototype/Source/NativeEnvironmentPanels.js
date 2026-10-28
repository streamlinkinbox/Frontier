import {BuildFogInspectorDashboard} from './FogInspectorDashboard.js';
import {BuildStarsInspectorInfographics} from './StarsInspectorInfographics.js';
import {BuildMoonInspectorInfographics} from './MoonInspectorInfographics.js';
import {BuildAtmosphereInspectorDashboard} from './AtmosphereInspectorDashboard.js';
import {BuildSunInspectorDashboard} from './SunInspectorDashboard.js';
import {EnvironmentIconURL} from './EnvironmentIconAssets.js';
import {BuildLocalFog} from './NativeLocalFogPanel.js';
import {BuildNativeClouds} from './NativeCloudPanel.js';
// Faithful native-panel section order, typography, controls and reference diagrams.
// Source snapshots live in Reference/NativeEnvironment; renderer extensions are labelled.
import {El,Note,Card,Metric,Button,Tiles,Slider,Select,Colour,Details,Grid,Header,Diagram,Orbit,DayCurve,Illuminance,RotationDial,DragAngles} from './NativeEnvironmentControls.js';
import {EnvironmentDefaults} from './EnvironmentSettings.js';
import {MoonBodies} from './CelestialAtlas.js';
import {NativeMoonPreview,LunarSphere} from './NativeMoonPreview.js';
import {SkyLutPreview} from './SkyLutPreview.js';
import {SolveCelestial} from './CelestialSolver.js';
import {BuildNativeFlare} from './NativeFlarePanel.js';
const Ends=(P,A,B)=>{const E=El('div','ne-ends');E.append(El('span','',A),El('span','',B));P.appendChild(E);};
const Section=(P,T)=>P.appendChild(El('div','ne-section',T));
function Bake(C,P,Title,A,EnabledKey=null){const B=Card(P,Title,'NativeBake'),R=El('div','ne-bake-row');B.appendChild(R);Tiles(C,R,A,[[EnabledKey,'Use baked image','NativeBake']]);const Right=El('div');R.appendChild(Right);if(EnabledKey)Button(Right,'Bake atmosphere',()=>{A.MultipleScattering=true;A.ForceAtmosphereBake=(A.ForceAtmosphereBake??0)+1;});else{const E=Button(Right,'Bake / export',()=>{});E.disabled=true;}Note(Right,EnabledKey?'Dirty-gated GPU bake · smooth dome lighting':'Procedural source · no separate image target in the native panel');Note(B,EnabledKey?'RGBA16F · transmittance + multi-scattering + sky view':'Native source binding unavailable');return B;}
export function BuildNativeSun(Host,C,P){return BuildSunInspectorDashboard(Host,C,P);}
export function BuildNativeAtmosphere(Host,C,P){return BuildAtmosphereInspectorDashboard(Host,C,P,Target=>BuildAtmosphereBakeCards(Host,C,Target));}
function BuildAtmosphereBakeCards(Host,C,P){const S=Host.Settings;Section(P,'BAKING');S.UseBakedAtmosphere??=true;Bake(C,P,'Atmosphere bake',S,'UseBakedAtmosphere');
 const B=Card(P,'Baked atmosphere','NativeOrbit'),Mode={View:'Profile'},Tabs=El('div','ne-lut-tabs');B.appendChild(Tabs);const Previews=new Map();let Current=null;
 for(const [View,Name]of [['Profile','Air'],['Panorama','Sky'],['Transmission','Transmit'],['Sphere','Sphere']]){const T=Button(Tabs,Name,()=>{Mode.View=View;C.Synchronise();});const U=()=>T.classList.toggle('selected',Mode.View===View);C.Bindings.push(U);U();}
 const Image=El('div','ne-preview');B.appendChild(Image);Diagram(C,Image,640,320,(X)=>{if(!Previews.has(Mode.View))Previews.set(Mode.View,new SkyLutPreview(Host.Atmosphere,{Mode:Mode.View}));Current=Previews.get(Mode.View);Current.Draw(X.X,640,320);});Note(B,'Decoded GPU pixels · scattering only. Solar disc, Moon, stars and fog remain separate.');
 const Diagnostics=Details(P,'GPU lookup tables');
 for(const [View,Title,W,H]of [['TransLut','Transmittance · 256 × 64',256,64],['MultiLut','Multiple scattering · 32 × 32',32,32]]) {
  Note(Diagnostics,Title);const D=new SkyLutPreview(Host.Atmosphere,{Mode:View});Diagram(C,Diagnostics,640,200,X=>{if(Diagnostics.open)D.Draw(X.X,640,200);});
 }
}
function Spectrum(P,X,Y,W){const G=P.X.createLinearGradient(X,Y,X+W,Y);['#927fb0','#7f9dc9','#94b5a9','#c5b27e','#c28b7c'].forEach((C,I)=>G.addColorStop(I/4,C));P.rect(X,Y,W,3,G,1.5);}
export function BuildNativeStars(Host,C,P){return BuildStarsInspectorInfographics(Host,C,P);}
export function BuildNativeMoon(Host,C,P){return BuildMoonInspectorInfographics(Host,C,P,Target=>BuildPlanets(Host,C,Target));}
function BuildPlanets(Host,C,P){const S=Host.Settings,B=Card(P,'Bright planets · orbital placement','NativeOrbit');Tiles(C,B,S,[['PlanetsEnabled','Planets','NativeOrbit']]);Slider(C,B,S,'PlanetBrightness','Brightness',0,5,.01,'×');const Solved=SolveCelestial(S);for(const Planet of Solved.Planets){const Row=El('div','ne-planet-row');Row.append(El('span','',Planet.Name),El('small','',`${Planet.Azimuth.toFixed(1)}° az · ${Planet.Elevation.toFixed(1)}° el`));B.appendChild(Row);}Note(B,'Low-precision orbital positions, not decorative Moon presets. Date and observer location are shared with the Sun. Suitable for visual placement, not navigation.');}
export function BuildNativeFog(Host,C,P,Aerial=false){return BuildFogInspectorDashboard(Host,C,P,Aerial);}
export const NativePanelBuilders={'local-fog':(H,C,P)=>{Header(C,P,'Local Volumetric Fog','FINITE SCATTERING MEDIUM','local-fog');BuildLocalFog(H,C,P);},clouds:BuildNativeClouds,'local-cloud':(H,C,P)=>BuildNativeClouds(H,C,P,true),sun:BuildNativeSun,atmosphere:BuildNativeAtmosphere,stars:BuildNativeStars,moon:BuildNativeMoon,flare:BuildNativeFlare,fog:BuildNativeFog,aerial:(H,C,P)=>BuildNativeFog(H,C,P,true)};
