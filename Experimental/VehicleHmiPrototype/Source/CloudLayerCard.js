import {El,Card,Metric,Note,Slider,Diagram} from './NativeEnvironmentControls.js';
import {CloudProfile} from './CloudField.js';
export function CloudLayerReadout(S){const Top=Math.max(S.CloudBase,Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness));return {Base:S.CloudBase,Top,Depth:Top-S.CloudBase,Clipped:S.CloudBase+S.CloudThickness>S.CloudCeiling};}
export function BuildCloudLayerCard(C,P,S){
 const Card0=Card(P,'Layer thickness','NativeClouds');Card0.classList.add('ne-layer-stack');
 const Hero=El('div','ne-layer-hero');Card0.appendChild(Hero);Metric(C,Hero,()=>(S.CloudThickness/1000).toFixed(2),'','km');
 const Status=El('span','ne-layer-status');Hero.appendChild(Status);
 const Intro=Note(Card0,'Lift the top of the stack to grow the layer.');
 Diagram(C,Card0,340,210,X=>{
  const R=CloudLayerReadout(S),Bottom=166,Top=Bottom-120*Math.sqrt(R.Depth/6000);
  X.rect(0,0,340,210,'#191d2080',14);
  for(let I=0;I<6;I++){const Y=26+I*28;X.line([15,Y],[326,Y],'#76838b20',1,[2,5]);}
  // An axonometric stack of translucent cloud strata, not the previous profile graph.
  X.ellipse(215,Bottom+12,89,18,'#0c0f1250');
  if(R.Depth>0)for(let I=0;I<=18;I++){
   const H=(I+.5)/19,Profile=Math.min(1,CloudProfile(H,S.CloudType,S.CloudAnvil));
   const Y=Bottom-(Bottom-Top)*H,RX=58+Profile*28;
   X.ellipse(214+Math.sin(H*7)*4,Y,RX,15,'#bdd5df12','#b8d1de24');
   X.ellipse(183,Y-3,RX*.45,11,'#e1eff514');
  }
  X.line([58,Top],[58,Bottom],'#a8c7d7',1.5);X.line([50,Top],[71,Top],'#b7d8e9',2);X.line([50,Bottom],[71,Bottom],'#b7d8e9',2);
  X.line([73,Top],[300,Top],'#cae7f2a0',1,[3,4]);X.line([73,Bottom],[300,Bottom],'#cae7f24a',1,[3,4]);
  X.rect(48,Top-5,20,10,'#bcdbe8',5);
  X.text(14,Top-13,'TOP','#b9d4e0',10,'left');X.text(14,Bottom+26,'BASE','#899ba6',10,'left');
  X.text(322,Math.max(15,Top-11),(R.Top/1000).toFixed(2)+' km','#d3e5ed',11,'right');
 },E=>{S.CloudThickness=Math.round(Math.max(100,Math.min(6000,E.Key==='Home'?100:E.Key?S.CloudThickness+(['ArrowUp','ArrowRight'].includes(E.Key)?100:-100)*E.Step:6000*(Math.max(0,Math.min(120,166-E.Point[1]))/120)**2)));C.Notify('CloudThickness',S);C.Synchronise();},'Layer thickness stack · drag top or use arrow keys');
 const Readouts=El('div','ne-layer-readouts');Card0.appendChild(Readouts);
 const Base=El('div'),Top=El('div');Base.appendChild(El('small','','BASE ALTITUDE'));Top.appendChild(El('small','','EFFECTIVE TOP'));const B=El('strong'),T=El('strong');Base.appendChild(B);Top.appendChild(T);Readouts.append(Base,Top);
 Slider(C,Card0,S,'CloudThickness','Thickness',100,6000,1,'m');
 const Caption=Note(Card0,'');const Sync=()=>{const R=CloudLayerReadout(S);B.textContent=(R.Base/1000).toFixed(2)+' km';T.textContent=(R.Top/1000).toFixed(2)+' km';Status.textContent=R.Clipped?'CEILING LIMITED':'LAYER DEPTH';Status.classList.toggle('clipped',R.Clipped);Caption.textContent=R.Clipped?`${Math.round(R.Depth)} m rendered · ceiling clips ${Math.round(S.CloudThickness-R.Depth)} m of requested depth.`:'World Z altitude · stack is vertically exaggerated for editing.';};C.Bindings.push(Sync);Sync();return Card0;
}
