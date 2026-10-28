import {El,Button,Diagram} from './NativeEnvironmentControls.js';
import {InspectorCategories} from './InspectorCategories.js';
import {ObservatorySlider} from './ObservatorySlider.js';
import {CloudRanges} from './CloudSettings.js';
const Wrap=A=>((A%360)+360)%360,Clamp=(V,A,B)=>Math.max(A,Math.min(B,V));
const Keys=['CloudWindSpeed','CloudWindBearing','WindGust','WindTurbulence','WindShear','WindVeer'];
export const WindFromBearing=S=>Wrap(S.CloudWindBearing+180);
export function WindGustFactor(Gust,Phase){return 1+Gust*(.65*Math.sin(Phase)+.35*Math.sin(Phase*2.17));}
// Diagnostic transcription of WindSequence's following-receiver velocity, not a new simulation clock.
export function SampleWindDiagnostic(S,Altitude,Time=0,Phase=0,RespectEnabled=false){
 const H=Math.max(0,Altitude||0)/1000,A=(S.CloudWindBearing+(S.WindVeer??8)*H)*Math.PI/180;
 const Speed=RespectEnabled&&S.WindEnabled===false?0:S.CloudWindSpeed*WindGustFactor(S.WindGust??.25,Phase),Turb=(S.WindTurbulence??.15)*Speed,Shear=1+(S.WindShear??.15)*H;
 return [Math.sin(A)*Speed*Shear+Math.sin(Time*.31+H)*Turb,Math.cos(A)*Speed*Shear+Math.sin(Time*.27+H*3)*Turb,Math.sin(Time*.22+H)*Turb*.12];
}
export function SetWindInspectorValue(S,K,V){if(!Keys.includes(K)||!Number.isFinite(V))return false;S[K]=K==='CloudWindBearing'?Wrap(V):Clamp(V,...CloudRanges[K]);return true;}
export function BuildWindField(H,C,P){
 const S=H.Settings;let Alive=true;
 C.NativeReadKey=()=>JSON.stringify([Keys.map(K=>S[K]),S.WindEnabled,S.CloudFollowWind,S.CloudEnabled,S.LocalCloudFollowWind,S.LocalCloudEnabled,S.LocalFogFollowWind,S.LocalFogEnabled,H.Precipitation?.Settings.FollowWind,H.Precipitation?.Settings.Enabled,H.Precipitation?.Settings.WindDrift]);
 C.Disposers??=[];C.Disposers.push(()=>Alive=false);P.classList.add('wind-instruments');
 const Edit=(K,V)=>{if(SetWindInspectorValue(S,K,V)){C.Notify(K,S);C.Synchronise();}};
 const Bind=F=>{F.Static=true;C.Bindings.push(F);C.Live.push(F);F();};
 const Field=(Parent,K,Name,Unit='',Step=.01)=>ObservatorySlider(C,Parent,S,K,Name,CloudRanges[K],Edit,Unit,Step);
 const Header=El('header','wi-header');Header.append(El('span','wi-eyebrow','FORCEFIELD / SHARED WEATHER'),El('h1','','Wind'),El('p','','A direction, a pulse, a changing column of air.'));P.appendChild(Header);
 const Power=Button(Header,'',()=>{S.WindEnabled=!S.WindEnabled;C.Notify('WindEnabled',S);C.Synchronise();},'wi-power');Power.setAttribute('aria-label','Toggle shared wind');Bind(()=>{Power.textContent=S.WindEnabled?'● On':'○ Off';Power.setAttribute('aria-pressed',String(!!S.WindEnabled));});
 const {Panes}=InspectorCategories(C,P,[['flow','Flow'],['variation','Variability'],['height','Altitude'],['receivers','Receivers']],'shared-wind');
 const Section=(Parent,Name,Caption)=>{const E=El('section','wi-card');E.append(El('span','wi-eyebrow',Caption),El('h2','',Name));Parent.appendChild(E);return E;};
 const Note=(Parent,Text)=>{const E=El('p','wi-note',Text);Parent.appendChild(E);return E;};
 function Plot(Parent,W,Height,Key,Draw,Drag,Label){const Canvas=Diagram(C,Parent,W,Height,Draw,Drag,Label),Old=C.Live.at(-1);let Last;const Update=()=>{if(!Alive||Parent.closest?.('[hidden]'))return;const K=JSON.stringify(Key());if(K===Last)return;Last=K;Old();};Update.Static=true;C.Live[C.Live.length-1]=Update;C.Bindings[C.Bindings.indexOf(Old)]=Update;return Canvas;}
 const Flow=Section(Panes.flow,'Direction of travel','01 / BASE FLOW');
 const Summary=El('div','wi-summary'),Speed=El('strong'),Unit=El('span','','m/s'),Bearing=El('span','wi-bearing');Summary.append(Speed,Unit,Bearing);Flow.appendChild(Summary);Bind(()=>{Speed.textContent=S.CloudWindSpeed.toFixed(1);Bearing.textContent=`FROM ${WindFromBearing(S).toFixed(0)}°`;});
 Plot(Flow,640,480,()=>[S.CloudWindSpeed,S.CloudWindBearing],X=>{
  const A=S.CloudWindBearing*Math.PI/180,U=[Math.sin(A),-Math.cos(A)],V=[-U[1],U[0]],Point=(Along,Across)=>[320+U[0]*Along+V[0]*Across,235+U[1]*Along+V[1]*Across],R=155;
  X.circle(320,235,R,null,'#c1d6b82d');X.circle(320,235,98,null,'#c1d6b814');
  for(let I=0;I<24;I++){const B=I/24*Math.PI*2;X.line([320+Math.sin(B)*150,235-Math.cos(B)*150],[320+Math.sin(B)*155,235-Math.cos(B)*155],'#bfd1b258',1);}
  for(let J=-2;J<=2;J++){const Across=J*39,L=Math.sqrt(Math.max(0,135**2-Across**2));X.path(Array.from({length:37},(_,I)=>Point(-L+2*L*I/36,Across+Math.sin(I/36*Math.PI)*J*3)),'#b9c9a12a',1.5);if(S.CloudWindSpeed>0){const End=Point(L*.48,Across),Back=Point(L*.48-12,Across);X.line(Back,End,'#c7d8ac',2);X.path([Point(L*.48-9,Across-5),End,Point(L*.48-9,Across+5)],'#c7d8ac',2);}}
  const From=Point(-R*S.CloudWindSpeed/40,0);X.line([320,235],From,'#dfe4ba66',1,[4,6]);X.circle(...From,9,'#e1e5bc');X.circle(320,235,4,'#94ab8a');
  for(const [PX,PY,T]of [[320,54,'N / +Y'],[516,240,'E'],[320,433,'S'],[126,240,'W']])X.text(PX,PY,T,'#a7b69b',19);
  X.text(320,475,'DOT = WIND FROM / ARROWS = TRAVEL TO','#91a38c',17);
 },E=>{if(E.Key){if(E.Key==='Home'){Edit('CloudWindBearing',180);Edit('CloudWindSpeed',7);}else if(['ArrowUp','ArrowDown'].includes(E.Key))Edit('CloudWindSpeed',S.CloudWindSpeed+(E.Key==='ArrowUp'?.1:-.1)*E.Step);else Edit('CloudWindBearing',S.CloudWindBearing+(E.Key==='ArrowRight'?1:-1)*E.Step);}else{const DX=E.Point[0]-320,DY=235-E.Point[1];S.CloudWindBearing=Wrap(Math.atan2(DX,DY)*180/Math.PI+180);S.CloudWindSpeed=Clamp(Math.hypot(DX,DY)/155*40,0,40);C.Notify('CloudWindBearing',S);C.Synchronise();}},'Wind flow compass · drag source bearing and speed; arrows edit bearing or speed');
 Field(Flow,'CloudWindSpeed','Base speed','m/s',.1);
 const From={get Value(){return WindFromBearing(S);},set Value(V){S.CloudWindBearing=Wrap(V+180);}};
 ObservatorySlider(C,Flow,From,'Value','Bearing from',[0,360],(_,V)=>Edit('CloudWindBearing',V+180),'°',1);
 Note(Flow,'Meteorological bearing is where wind comes FROM; the arrows show where it travels TO. Drag the source dot radially for base speed. Base-only reference: gusts, altitude and turbulence are excluded.');
 const Live=El('p','wi-live');Panes.flow.appendChild(Live);const ReadLive=()=>{if(!Alive)return;Live.textContent=S.WindEnabled?`LIVE BASE VECTOR · ${Math.hypot(...(H.Clouds?.Wind?.Velocity??[0,0,0])).toFixed(2)} m/s · current shared gust clock`:'SCENE WIND OFF · configured references remain visible';};C.Live.push(ReadLive);C.Bindings.push(ReadLive);ReadLive();

 const Gusts=Section(Panes.variation,'The gust envelope','02 / SPEED MODULATION');
 Plot(Gusts,640,290,()=>[S.CloudWindSpeed,S.WindGust],X=>{const Max=Math.max(1,S.CloudWindSpeed*(1+S.WindGust)),Y=V=>230-V/Max*180;X.rect(45,Y(S.CloudWindSpeed*(1+S.WindGust)),550,Math.max(1,Y(S.CloudWindSpeed*(1-S.WindGust))-Y(S.CloudWindSpeed*(1+S.WindGust))),'#b9d1a611',8);X.line([45,Y(S.CloudWindSpeed)],[595,Y(S.CloudWindSpeed)],'#c3d1ac44',1,[3,5]);X.path(Array.from({length:161},(_,I)=>[45+I/160*550,Y(S.CloudWindSpeed*WindGustFactor(S.WindGust,I/160*40*(.35+S.WindGust*.4)))]),'#cedab4',2);X.text(45,27,'SPEED / m/s','#93a38a',18,'left');X.text(595,27,Max.toFixed(1)+' MAX BOUND','#c4ceb0',18,'right');X.text(45,273,'0 s','#93a38a',18,'left');X.text(595,273,'40 s','#93a38a',18,'right');},null,'Wind gust reference · fixed settings and zero starting phase');
 Field(Gusts,'WindGust','Gust strength');Note(Gusts,'Exact two-sine speed modulation under fixed settings, starting at phase zero. Shading is the guaranteed ±gust bound, not a measured history or a forecast of the running clock.');
 const Turbulence=Section(Panes.variation,'Cross-flow perturbation','03 / THREE VELOCITY COMPONENTS');
 Plot(Turbulence,640,240,()=>[S.CloudWindSpeed,S.WindTurbulence],X=>{const Amplitude=S.CloudWindSpeed*S.WindTurbulence;for(let J=0;J<3;J++){const Y=45+J*66;X.line([95,Y],[590,Y],'#b9cba220');X.path(Array.from({length:81},(_,I)=>{const T=I/80*20,V=Math.sin(T*[.31,.27,.22][J])*(J===2?.12:1);return [95+I/80*495,Y-V*S.WindTurbulence*(S.CloudWindSpeed>0?24:0)];}),['#d2d7aa','#abc9bb','#a7bccb'][J],2);X.text(45,Y+5,['X','Y','Z'][J],'#a9baa3',20);X.text(590,Y+29,(Amplitude*(J===2?.12:1)).toFixed(2)+' m/s bound','#91a18c',16,'right');}},null,'Wind perturbation components · fixed base-speed amplitude at sea level');
 Field(Turbulence,'WindTurbulence','Turbulence');Note(Turbulence,'Normalized sea-level traces at fixed base speed; labels give velocity bounds. X/Y use the existing .31/.27 rad/s oscillations; vertical motion uses .22 rad/s at 12% amplitude. Live gusts scale the actual perturbation.');

 const Height=Section(Panes.height,'Wind through altitude','04 / STEADY VECTOR LADDER');
 Plot(Height,640,390,()=>[S.CloudWindSpeed,S.CloudWindBearing,S.WindShear,S.WindVeer],X=>{const Scale=Math.max(1,S.CloudWindSpeed*(1+S.WindShear*3));X.line([270,40],[270,330],'#a8bd9b33',1,[3,5]);for(let I=0;I<6;I++){const Z=I*600,Y=330-I*55,A=(S.CloudWindBearing+S.WindVeer*Z/1000)*Math.PI/180,Speed=S.CloudWindSpeed*(1+S.WindShear*Z/1000),L=Speed/Scale*120,End=[270+Math.sin(A)*L,Y-Math.cos(A)*L*.28];X.text(90,Y+6,Z?((Z/1000).toFixed(1)+' km'):'0 m','#9aad94',19);X.line([270,Y],End,'#c7d8ae',2.5);if(L>0)X.circle(...End,4,'#dce3be');X.text(590,Y+6,Speed.toFixed(1)+' m/s','#c8d3b6',20,'right');}X.text(45,380,'PLAN DIRECTIONS / +Y NORTH · 0–3 km','#91a18c',18,'left');},null,'Altitude wind vectors · steady shear and veer reference');
 Field(Height,'WindShear','Speed gain per km','×/km');Field(Height,'WindVeer','Direction veer per km','°/km',1);Note(Height,'Steady speed = base × (1 + shear × altitude in km). Travel bearing turns by veer × altitude in km. Negative altitude is clamped to sea level. Gusts and turbulence are excluded here; vectors are plan-view glyphs, not vertically tilted air.');

 const Receivers=Section(Panes.receivers,'Where the field goes','05 / SHARED ADVECTION ROUTING');
 function Receiver(Name,Target,Key,Enabled,Caption){const Row=El('div','wi-receiver'),Text=El('div');Text.append(El('strong','',Name),El('span','',Caption));Row.appendChild(Text);const Toggle=Button(Row,'',()=>{Target[Key]=!Target[Key];C.Notify(Key,Target);C.Synchronise();},'wi-follow');Toggle.setAttribute('aria-label',Name+' follow shared wind');const Status=El('small');Row.appendChild(Status);Bind(()=>{Toggle.textContent=Target[Key]?'Linked':'Unlinked';Toggle.setAttribute('aria-pressed',String(!!Target[Key]));Status.textContent=Enabled()?'Receiver enabled':'Receiver off';});Receivers.appendChild(Row);}
 Receiver('Clouds',S,'CloudFollowWind',()=>S.CloudEnabled,'Density travel at cloud-base altitude');
 Receiver('Local cloud',S,'LocalCloudFollowWind',()=>S.LocalCloudEnabled,'Placed bounds stay fixed; density travels');
 Receiver('Local fog',S,'LocalFogFollowWind',()=>S.LocalFogEnabled,'Unlinked uses its own authored drift');
 if(H.Precipitation?.Settings){const Precip=H.Precipitation.Settings;Receiver('Precipitation',Precip,'FollowWind',()=>Precip.Enabled,'GPU falling particles · shared altitude field');ObservatorySlider(C,Receivers,Precip,'WindDrift','Precipitation wind drift',[0,1],(K,V)=>{Precip[K]=V;C.Notify(K,Precip);C.Synchronise();},'',.01);}
 Note(Receivers,'Linking a receiver does not enable it. Cloud/fog follow switches affect integrated density travel, not placement. Static height fog has no advected density. Tyre smoke/fire retains its separate gas-wind controls; no fake shared-wind binding is shown.');
 return P;
}
