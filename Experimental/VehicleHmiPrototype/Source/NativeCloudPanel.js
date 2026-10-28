import {BuildCloudInspectorDashboard} from './CloudInspectorDashboard.js';
import {BuildLightningControls} from './NativeLightningPanel.js';
import {El,Header,Card,Tiles,Metric,Note,Slider,Select,Grid,Diagram,Button} from './NativeEnvironmentControls.js';
import {CloudDefaults,CloudExtent,CloudRanges} from './CloudSettings.js';
import {BuildCloudLayerCard} from './CloudLayerCard.js';
import {PlaceLocalCloud,LocalCloudPresets} from './LocalCloudPlacement.js';
class Preview{
 constructor(C,Local){this.C=C;this.Local=Local;C.Disposers??=[];C.Disposers.push(()=>{this.Dead=true;this.Worker?.terminate();});}
 Refresh(S,Aspect){if(typeof Worker==='undefined'||this.Dead)return;const Settings=Object.fromEntries(Object.entries(CloudDefaults).map(([K,V])=>[K,S[K]??V]));const Keys=this.Local?['LocalCloudEnabled','LocalCloudX','LocalCloudY','LocalCloudZ','LocalCloudHalfX','LocalCloudHalfY','LocalCloudHalfZ','LocalCloudDensity','LocalCloudCoverage','LocalCloudScale','LocalCloudShape','LocalCloudForm','LocalCloudSoftness','LocalCloudDetail']:['CloudEnabled','CloudCoverage','CloudDensity','CloudScale','CloudType','CloudAnvil','CloudBase','CloudThickness','CloudCeiling','CloudDetail'];const Key=JSON.stringify([Aspect,...Keys.map(K=>Settings[K])]);
 if(Key===this.Key)return;this.Pending={Settings,Local:this.Local,Aspect,Key};if(this.Busy)return;this.Send();}
 Send(){const Job=this.Pending;this.Pending=null;this.Busy=true;this.Worker??=new Worker(new URL('./CloudPreviewWorker.js',import.meta.url),{type:'module'});
 this.Worker.onmessage=({data:R})=>{if(this.Dead)return;this.Busy=false;this.Key=R.Key;
 if(!this.Pending||this.Pending.Key===R.Key){for(const Name of ['Top','Side']){const Canvas=this[Name]??=document.createElement('canvas');Canvas.width=R.W;Canvas.height=R.H;const X=Canvas.getContext('2d'),Image=X.createImageData(R.W,R.H);Image.data.set(R[Name]);X.putImageData(Image,0,0);}this.C.Synchronise();}
 if(this.Pending&&this.Pending.Key!==this.Key)this.Send();else this.Pending=null;};
 this.Worker.onerror=()=>{this.Busy=false;this.Error=true;};this.Worker.postMessage(Job);}
 Draw(X,Name,W,H){if(this[Name])X.X.drawImage(this[Name],0,0,W,H);else {X.rect(0,0,W,H,'#1a232f',12);X.text(W/2,H/2,this.Error?'Preview unavailable':'Preparing density diagnostic…','#91a5bd',10);}}
}
function Axes(C,P,S,Prefix,Label,Half=false){const Row=El('label','ne-cloud-axes');Row.appendChild(El('span','',Label));const Inputs=El('div');Row.appendChild(Inputs);P.appendChild(Row);
 for(const A of ['X','Y','Z']){const Key=Prefix+A,I=El('input','ne-cloud-axis');I.type='number';I.step=1;I.min=CloudRanges[Key][0];I.max=CloudRanges[Key][1];I.setAttribute('aria-label',Label+' '+A);I.addEventListener('input',()=>{if(I.value==='')return;const V=+I.value;if(!Number.isFinite(V))return;S[Key]=Math.max(+I.min,Math.min(+I.max,V));C.Notify(Key,S);C.Synchronise();});I.addEventListener('blur',()=>I.value=S[Key]);const Sync=()=>{if(document.activeElement!==I)I.value=S[Key];};C.Bindings.push(Sync);Sync();Inputs.appendChild(I);}}
export function BuildNativeClouds(Host,C,P,Local=false){const S=Host.Settings,Prefix=Local?'LocalCloud':'Cloud',Preview0=new Preview(C,Local);if(!Local)return BuildCloudInspectorDashboard(Host,C,P,Preview0);Header(C,P,'Clouds',Local?'LOCAL VOLUMETRIC CLOUD · bounded volume':'GLOBAL VOLUMETRIC CLOUDS · atmospheric layer');
 const Settings=Card(P,'Cloud settings','NativeClouds');Tiles(C,Settings,S,[[Prefix+'Enabled','Enabled','Power'],[Prefix+'FollowWind','Follow wind','Power']]);
 const Place=()=>{PlaceLocalCloud(S,Host.CameraEye,Host.CameraState?.Forward??Host.Clouds?.LastForward);C.Notify('LocalCloudEnabled',S);C.Synchronise();};
 if(Local){
  const Actions=Card(P,'Local volume · quick start','NativeClouds');Note(Actions,'Demo-based local cloud material in movable Box/Ellipsoid bounds. Wind advects the density through this placed volume. Presets place it in view. Raise Centre Z to lift it clear of the ground.');
  const Row=El('div','ne-cloud-actions');Actions.appendChild(Row);
  for(const Name of Object.keys(LocalCloudPresets))Button(Row,Name,()=>{Object.assign(S,LocalCloudPresets[Name]);Place();});
  Button(Actions,'Place in front of camera',Place,'ne-cloud-place');
 }else Button(P,'Place a local cloud →',()=>{Object.assign(S,LocalCloudPresets.Puff);Place();Host.Shell.Select('local-cloud');},'ne-cloud-place');
 const Coverage=Card(P,'Cloud coverage','NativeClouds');Metric(C,Coverage,()=>Math.round(S[Prefix+'Coverage']*100)+'%');Note(Coverage,Local?'One continuous density field within the local bounds':'One continuous density field · gaps close as coverage increases');
 const Top=Diagram(C,Coverage,640,364,X=>{Preview0.Refresh(S,Math.max(1,(P.clientWidth-48)/182)||2);Preview0.Draw(X,'Top',640,364);X.text(16,26,'TOP-DOWN DENSITY','#c2d3e5',20,'left');});Top.classList.add('ne-cloud-top');
 const Percent={get Value(){return S[Prefix+'Coverage']*100;},set Value(V){S[Prefix+'Coverage']=V/100;}};Slider(C,Coverage,Percent,'Value','Coverage',0,100,1,'%');Note(Coverage,'Static density diagnostic · not measured sky cover or a lit render');
 const G=Grid(P,'two ne-cloud-altitudes'),Base=Card(G,Local?'Local bounds':'Cloud base','NativeClouds');
 if(Local){const Bounds=Note(Base,'');const SyncBounds=()=>Bounds.textContent=`${S.LocalCloudHalfX*2} × ${S.LocalCloudHalfY*2} × ${S.LocalCloudHalfZ*2} m`;C.Bindings.push(SyncBounds);SyncBounds();Diagram(C,Base,340,200,X=>{const Max=Math.max(S.LocalCloudHalfX,S.LocalCloudHalfY,S.LocalCloudHalfZ),RX=S.LocalCloudHalfX/Max*110,RY=S.LocalCloudHalfY/Max*40,RZ=S.LocalCloudHalfZ/Max*75;
 X.ellipse(170,104,RX,RY,'#a8cddd0c','#a8cddd80');
 for(const Axis of [0,1]){const Points=[];for(let I=0;I<=64;I++){const A=I/64*Math.PI*2;Points.push([170+Math.cos(A)*(Axis?RX*.45:RX),104-Math.sin(A)*RZ+Math.cos(A)*(Axis?RY:0)]);}X.path(Points,'#a8cddd90');}
 X.line([170,22],[170,181],'#7d98a344',1,[2,4]);X.circle(170,104,3,'#d8edf5');X.text(170,194,'Outer envelope · Z up','#919191',9);});Axes(C,Base,S,'LocalCloud','Placement origin · X / Y / Z (m)');Axes(C,Base,S,'LocalCloudHalf','Half extents · X / Y / Z (m)',true);
 }else{Metric(C,Base,()=>(S.CloudBase/1000).toFixed(2)+' km');Note(Base,'World Z altitude · drag the base line');Diagram(C,Base,340,200,X=>{Preview0.Draw(X,'Side',340,200);for(let I=0;I<=4;I++){const Y=I*40+10;X.line([30,Y],[325,Y],'#8ca2bd33');X.text(22,Y,String(Math.round(S.CloudCeiling/1000*(1-I/4))),'#919191',9);}const Y=180-S.CloudBase/S.CloudCeiling*170;X.line([30,Y],[325,Y],'#cee3f9',1.3);},E=>{S.CloudBase=Math.max(100,Math.min(S.CloudCeiling,E.Key?S.CloudBase+(E.Key==='ArrowUp'?100:-100)*E.Step:(180-E.Point[1])/170*S.CloudCeiling));});Note(Base,'0 m world datum · not terrain-relative AGL');Slider(C,Base,S,'CloudBase','Base',100,20000,1,'m',V=>S.CloudBase=Math.min(V,S.CloudCeiling));}
 if(Local){const Thickness=Card(G,'Volume section','NativeClouds');Metric(C,Thickness,()=>`${Math.round(S.LocalCloudZ-S.LocalCloudHalfZ)}–${Math.round(S.LocalCloudZ+S.LocalCloudHalfZ)} m`);Note(Thickness,'X–Z section · actual bounded density');Diagram(C,Thickness,640,354,X=>Preview0.Draw(X,'Side',640,354));Note(Thickness,'Demo density profile and multi-octave procedural erosion. Bounds define the placed volume; wind advects its density.');}
 else BuildCloudLayerCard(C,G,S);
 const Body=Card(P,'Cloud body','NativeClouds');Slider(C,Body,S,Prefix+'Density','Density',0,4,.01,'×');Slider(C,Body,S,Prefix+'Scale','Feature Scale',Local?2:.2,Local?200:3,Local?1:.01,Local?'m':'×');Slider(C,Body,S,Prefix+'Anisotropy','Forward scattering',-.8,.8,.01);
 if(!Local){const R=El('div','ne-cloud-type-row');Body.appendChild(R);Select(C,R,S,'CloudType','Type',['Stratus','Stratocumulus','Cumulus','Cumulonimbus','Altostratus','Cirrus'].map((N,I)=>[I,N]));Slider(C,R,S,'CloudAnvil','Anvil',0,1,.01);Slider(C,Body,S,'CloudCeiling','Ceiling',4000,20000,1,'m',V=>S.CloudBase=Math.min(V,S.CloudBase));}
 const Detail=Card(P,'Density & light response','NativeClouds');Slider(C,Detail,S,Local?'LocalCloudDetail':'CloudDetail','Edge erosion',0,1,.01);if(Local){Select(C,Detail,S,'LocalCloudShape','Shape',[[0,'Ellipsoid'],[1,'Box']]);Select(C,Detail,S,'LocalCloudForm','Formation',[[0,'Stratiform'],[1,'Cumuliform'],[2,'Wispy']]);Slider(C,Detail,S,'LocalCloudSoftness','Edge softness',.05,1,.01);}Slider(C,Detail,S,'CloudLightSteps','Sun-shadow taps',0,4,1);Slider(C,Detail,S,'CloudAbsorption','Light absorption',0,1,.01);Slider(C,Detail,S,'CloudPowder','Powder response',0,1,.01);
 const Quality=Card(P,'Cloud rendering · resolution & budget','NativeClouds');Slider(C,Quality,S,'CloudResolution','Cloud resolution',25,100,1,'% / axis');
 Metric(C,Quality,()=>`${(S.CloudResolution*S.CloudResolution/100).toFixed(1)}% pixels`,'Shared sky/local target scale · 50% width × 50% height ≈ 25% of full-resolution pixels');
 const Size=Note(Quality,'');const SyncSize=()=>{const E=CloudExtent(...(Host.ScreenExtent??[1920,1080]),S.CloudResolution);Size.textContent=`Cloud trace ${E[0]} × ${E[1]} · 100% is the full-resolution reference. Depth-aware upsampling; every target pixel traces each frame. Use higher resolution for close volumes.`;};C.Live.push(SyncSize);SyncSize();
 const Timing=Note(Quality,'');const SyncTiming=()=>{Timing.textContent=Host.Metrics?.Enabled?`Cloud GPU pass ${(Host.Metrics.Durations?.Clouds??0).toFixed(2)} ms · presentation reconstruction is included in the existing display pass.`:'GPU timestamps unavailable on this adapter.';};C.Live.push(SyncTiming);SyncTiming();
 Slider(C,Quality,S,'CloudSteps','Sky base samples',8,64,1,'samples');Slider(C,Quality,S,'CloudLocalSteps','Local base samples',8,64,1,'samples');
 const Samples=Note(Quality,'');C.Live.push(()=>{Samples.textContent=`Thickness-aware sky sampling: ${Host.Clouds?.UniformData?.[21]??S.CloudSteps} samples per complete layer ray · hard cap 96. Base samples apply up to 1.2 km depth; thicker layers scale the count, rather than stretching the gaps.`;});
 Note(Quality,'Stable world-space cloud steps · long or slanted rays may use up to 96 samples. Depth clipping changes only the last partial step. Independent bounded renderer · no temporal history or 3D lighting cache. Low sample counts can reveal bands, especially inside close volumes.');
 const Shadows=Card(P,'Cloud shadows','NativeClouds');Tiles(C,Shadows,S,[['CloudShadows','Cast shadows','Sun']]);
 Note(Shadows,'Shared by sky and local clouds · soft shadows on direct sunlight only. Ambient sky and artificial lights stay unchanged.');
 Slider(C,Shadows,S,'CloudShadowStrength','Shadow strength',0,1,.01);
 Slider(C,Shadows,S,'CloudShadowSoftness','Shadow softness',0,200,1,'m');
 Slider(C,Shadows,S,'CloudShadowDistance','Shadow field span',1000,16000,100,'m');
 Slider(C,Shadows,S,'CloudShadowSamples','Shadow samples',4,12,1,'/ medium');
 const ShadowStatus=Note(Shadows,'');C.Live.push(()=>{ShadowStatus.textContent=`128 × 128 world-space field · wind updates at most 8 Hz · ${Host.Clouds?.Shadows?.Active?'active':'inactive'}${Host.Metrics?.Enabled?' · GPU '+(Host.Metrics.Durations?.['Cloud shadows']??0).toFixed(2)+' ms (update pass)':''}`;});
 BuildLightningControls(Host,C,P);
 BuildSharedWind(C,P,S,Host);Note(P,'Bounded sun self-shadow taps and detail erosion share the low-resolution pass. Ground shadows retain the 8 Hz projected field. Cloud-to-flare attenuation and volumetric geometry shadows are not yet supported.');
 C.Synchronise();
}

export function BuildSharedWind(C,P,S,Host){const Wind=Card(P,'Wind Field','Wind');Note(Wind,'Shared force field · edit Force, Steadiness, Shear and Driving in Forcefield → Wind. This medium keeps its own Follow wind switch.');Button(Wind,'Open Wind ForceField',()=>Host.Shell.Select('wind'));}
