import {BuildTransformInspector} from './EditorTransforms.js';
import {El,Button,Diagram} from './NativeEnvironmentControls.js';
import {VehicleWheelNames,VehicleSystemNames,TyreComponentNames,VehicleInspectorState} from './VehicleComponents.js';
import {VehicleGeometry,EngineSheet,TransmissionSheet,TurbochargerSheet,ControlVehicleSockets} from './VehicleSpecification.js';
import {CabinLayout} from './CabinStructure.js';

export function BuildVehicleSystemsInspector(H,C,Parent,Builders){
 const State=VehicleInspectorState(H),S=H.Settings,V=H.Vehicle,Root=El('div','vehicle-systems');Parent.appendChild(Root);
 const Open=(System,Sub=State.TyreComponent)=>{State.System=System;State.TyreComponent=Sub;H.Shell.Select('vehicle');};
 const Path=El('p','vs-path');Path.textContent='ASSEMBLY / CONTROLVEHICLE / '+(VehicleSystemNames.find(([K])=>K===State.System)?.[1]??'Overview').toUpperCase();Root.appendChild(Path);
 function Navigation(Entries,Active,Choose,Label,Class){const Nav=El('nav',Class);Nav.setAttribute('role','tablist');Nav.setAttribute('aria-label',Label);Root.appendChild(Nav);for(const [I,[Key,Name]]of Entries.entries()){const B=Button(Nav,Name,()=>Choose(Key),'vs-tab');B.id=Label.replaceAll(' ','-')+'-'+Key;B.setAttribute('role','tab');B.setAttribute('aria-selected',String(Key===Active));B.setAttribute('aria-controls','vehicle-component-pane');B.tabIndex=Key===Active?0:-1;B.addEventListener('keydown',E=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(E.key))return;E.preventDefault();const J=E.key==='Home'?0:E.key==='End'?Entries.length-1:(I+(E.key==='ArrowLeft'?-1:1)+Entries.length)%Entries.length;Choose(Entries[J][0]);document.getElementById(Label.replaceAll(' ','-')+'-'+Entries[J][0])?.focus();});}return Nav;}
 Navigation(VehicleSystemNames,State.System,K=>Open(K),'Vehicle systems','vs-system-nav');
 const Body=El('div','vs-component-pane');Body.id='vehicle-component-pane';Body.setAttribute('role','tabpanel');Body.setAttribute('aria-labelledby','Vehicle-systems-'+State.System);
 function Card(Name,Glyph='Layers'){return C.Card(Body,Name,Glyph,{Accent:'#c9c785'});}
 function Shared(Text){const E=El('p','vs-scope',Text);Body.appendChild(E);}
 function MeshReceipt(Parts){const Card0=Card('Geometry ownership','Boxes');for(const Part of Parts){const M=Part.Mesh,Count=M.FacetCount??M.FacetIndices?.length??0;C.Readout(Card0,()=>[`${Part.Id} · ${Count.toLocaleString()} triangles · occupant ${M.Occupant} / facet set ${M.FacetSet}`]);}C.Muted(Card0,'Reference groups over the original buffers, not duplicate geometry. Shared left/right wheel buffers retain independent wheel transforms, deformation and damage.');}
 const Registry=H.Scene.VehicleComponents;
 if(State.System==='tyres'){
  Navigation(VehicleWheelNames.map((N,I)=>[String(I),N]),String(State.Wheel),K=>{State.Wheel=+K;Open('tyres');},'Wheel selection','vs-wheel-nav');
  Navigation(TyreComponentNames,State.TyreComponent,K=>Open('tyres',K),'Tyre components','vs-tyre-nav');
 }
 Root.appendChild(Body);
 if(State.System==='overview'){
  const Assembly=Card('Vehicle assembly','Car');C.Muted(Assembly,'One vehicle, four wheel assemblies. Inspect a system above; wheel effects live under Tyres, not as unrelated scene objects.');
  const Tree=El('div','vs-assembly-tree');Assembly.appendChild(Tree);
  for(const [Name,System,Sub]of [['Chassis → collision & body geometry','chassis'],['Engine → turbo & transmission','engine'],['Tyres × 4 → rubber + rim','tyres','tyre'],['    Tyre marks → road deposit','tyres','marks'],['    Smoke & fire → thermal sources','tyres','smoke'],['    Rim sparks → metal contact','tyres','sparks'],['Suspension → four struts & contacts','suspension'],['Lights → headlights & roof fixture','lights'],['HMI → tablet, cluster & cabin','hmi'],['Mirrors → left, right & rear-view','mirrors']])Button(Tree,Name,()=>Open(System,Sub),'vs-assembly-link');
  Builders.Controls(H,C,Body,'overview');
 }else if(State.System==='chassis'){
  const Chassis=Card('Chassis & collision','Shield');C.Stats(Chassis,[['Mass',()=>VehicleGeometry.Mass,'kg'],['CoM height',()=>VehicleGeometry.CoMHeight,'m',2],['Hull samples',()=>V.HullPoints?.length??0],['Hull contacts',()=>V.HullContacts??0]]);
  C.Stats(Chassis,[['Point radius',()=>V.HullPointRadius??0,'m',3],['Jolt vehicle body',()=>H.Bodies.CarBody?'created':'not active']]);
  C.Switch(Chassis,State,'ShowCollision',{Label:'Show vehicle collision overlay',Detail:'Actual sampled hull, wheel radii and contact normals · diagnostic x-ray',OnChange:()=>H.Shell.VehicleOverlay?.Draw()});
  Diagram(C,Chassis,640,340,X=>{X.rect(20,20,600,300,'#15191a',14);const Set=H.Scene.FacetSets[H.Scene.Occupants[H.Scene.VehicleOccupants.Body].FacetSet],Project=P=>[320+P[0]*78,245-P[2]*95];const Step=Math.max(1,Math.ceil(Set.Facets.length/1000));for(let I=0;I<Set.Facets.length;I+=Step){const F=Set.Facets[I];X.path([...F.Corners,F.Corners[0]].map(Project),'#8b998a20',.7);}for(const P of V.HullPoints??[])X.circle(...Project(P),1.7,'#cfcc86b0');X.text(40,50,'BODY-LOCAL SIDE VIEW / ACTUAL HULL SAMPLES','#b7bd9b',18,'left');X.text(40,302,'Sampled mesh wireframe · full collision-point set','#8a9989',17,'left');},null,'Vehicle collision hull · body-local side view');
  C.Muted(Chassis,'Course/static contacts use sampled body-hull points and their support radius. Jolt uses the convex hull of those points for loose-body interaction; dots are its input points, not a tessellation of the convex surface. Wheel contact is solved separately. Overlay is a diagnostic x-ray, not a scene mesh.');
  BuildTransformInspector(H,'vehicle',C,Body);
  if(Registry)MeshReceipt(Registry.BodyParts.filter(N=>['chassis','cabin'].includes(N.Id)));
  Shared('Chassis scrapes also feed the shared metal-spark renderer. Rim emitters are organized under Tyres → Rim sparks.');Builders.Controls(H,C,Body,'sparks');
 }else if(State.System==='engine'){
  const Power=Card('Engine & turbocharger','Gauge');C.Stats(Power,[['Engine speed',()=>V.QueryTelemetry().EngineRPM,'rpm',0],['Boost',()=>V.Boost,'bar',2],['Throttle',()=>V.Throttle*100,'%',0],['Gear',()=>V.QueryTelemetry().Gear]]);
  C.Stats(Power,[['Idle',()=>EngineSheet.IdleRPM,'rpm'],['Redline',()=>EngineSheet.RedlineRPM,'rpm'],['Boost limit',()=>TurbochargerSheet.WastegateMaxBoost,'bar',2],['Final drive',()=>TransmissionSheet.FinalDriveRatio,'',2]]);
  Diagram(C,Power,640,240,X=>{X.line([50,185],[600,185],'#bbc5a333');X.path(EngineSheet.TorqueCurve.map(([RPM,T])=>[50+RPM/8500*550,185-T/550*140]),'#c7ce94',2);const RPM=V.QueryTelemetry().EngineRPM;X.line([50+RPM/8500*550,30],[50+RPM/8500*550,190],'#e3d3a5',1,[4,5]);X.text(50,222,'800 → 8,500 rpm / authored torque curve (N·m)','#a8b293',18,'left');},null,'Engine authored torque curve and current RPM');
  C.Muted(Power,'Real powertrain solver and immutable specification data. Engine/turbo/transmission do not have separate authored meshes in this asset. These are readouts, not cosmetic tuning sliders.');
 }else if(State.System==='tyres'){
  const I=State.Wheel,W=()=>V.Wheels[I],T=()=>H.Thermal.Wheels[I];
  const Selected=Card(VehicleWheelNames[I]+' · wheel assembly','CircleDot');C.Stats(Selected,[['Contact',()=>W().Contact?'grounded':'airborne'],['Temperature',()=>T().Surface-273.15,'°C',1],['Pressure',()=>T().GaugePressure/1000,'kPa',1],['Load',()=>W().Load,'N',0]]);
  C.Readout(Selected,()=>[H.QueryTyreLive().States[I]||'Tyre intact',`${I<2?'Steered / front':'Driven / rear'} · independent transform & damage`]);
  if(State.TyreComponent==='tyre'){
   if(Registry)MeshReceipt(Registry.Wheels[I].Children);
   C.Stats(Selected,[['Slip ratio',()=>W().SlipRatio,'',3],['Slip angle',()=>W().SlipAngle*180/Math.PI,'°',1],['Deflection',()=>W().TyreDeflection*1000,'mm',1],['Tread',()=>H.Thermal.QueryTreadFractions()[I]*100,'%',1]]);
   Builders.Controls(H,C,Body,'tyre');
  }else if(State.TyreComponent==='marks'){
   Shared('OWNER: selected tyre contact → shared road-deposit atlas. Strength, enable and clear affect all tyre marks; the atlas is not duplicated for each wheel.');Builders.Marks(H,C,Body);
  }else if(State.TyreComponent==='smoke'){
   Shared('OWNER: selected tyre thermal/combustion source → shared vehicle-following gas domain. Controls below affect all tyre sources, and the diagnostic test emitter is not a tyre.');C.Stats(Selected,[['Ignited',()=>T().Ignited?'yes':'no'],['Sliding work',()=>W().SlidingEnergy,'J',0]]);C.Switch(Selected,S,'SmokeEnabled',{Label:'Shared tyre smoke / fire domain'});Builders.Gas(H,C,Body);
  }else if(State.TyreComponent==='sparks'){
   Shared('OWNER: selected rim contact → shared metal-spark renderer. Only actual rim contact emits rim sparks; body scrapes use the same renderer. Settings below are shared, not per-wheel emission overrides.');C.Switch(Selected,S,'Sparks',{Label:'Metal-contact sparks (shared)'});C.Readout(Selected,()=>H.Sparks.QueryReadout());Builders.Controls(H,C,Body,'sparks');
  }else Builders.Controls(H,C,Body,'burnout');
 }else if(State.System==='suspension'){
  const Struts=Card('Four-corner suspension','MoveVertical');C.Muted(Struts,'Live solver struts, calibrated from sprung mass and authored sockets. There are no dedicated animated suspension meshes in the supplied archive. Specifications below are read-only; changing them without recalibration would not be a valid suspension edit.');
  for(let I=0;I<4;I++){const R=Card(VehicleWheelNames[I]+' · strut','Activity');C.Stats(R,[['Length',()=>V.Wheels[I].Length*1000,'mm',1],['Compression',()=>V.Wheels[I].Compression*1000,'mm',1],['Wheel load',()=>V.Wheels[I].Load,'N',0],['Contact',()=>V.Wheels[I].Contact?'yes':'no']]);C.Stats(R,[['Spring rate',()=>V.Struts[I].SpringRate,'N/m',0],['Damping',()=>V.Struts[I].DampingRate,'N·s/m',0],['Frequency',()=>V.Struts[I].NaturalFrequency,'Hz',1],['Damping ratio',()=>V.Struts[I].DampingRatio,'',2]]);C.Muted(R,'Mount (authored metres): '+ControlVehicleSockets.SuspensionMount[I].map(N=>N.toFixed(4)).join(' / '));}
 }else if(State.System==='lights'){
  Builders.Controls(H,C,Body,'lights');if(Registry)MeshReceipt(Registry.BodyParts.filter(N=>N.Id==='lights'));
 }else if(State.System==='hmi'){
  Builders.Cabin(H,C,Body,'hmi');if(Registry)MeshReceipt(Registry.BodyParts.filter(N=>['hmi','cabin'].includes(N.Id)));
 }else if(State.System==='mirrors'){
  Builders.Cabin(H,C,Body,'mirrors');const Mirrors=Card('Mirror fixtures','Eye');for(const Mirror of CabinLayout.Mirrors)C.Readout(Mirrors,()=>[`${Mirror.Name} · ${(Mirror.Width*1000).toFixed(0)} × ${(Mirror.Height*1000).toFixed(0)} mm`]);if(Registry)MeshReceipt(Registry.BodyParts.filter(N=>N.Id==='mirrors'));
 }
 return Root;
}
