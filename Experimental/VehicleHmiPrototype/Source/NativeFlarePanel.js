import {InspectorCategories} from './InspectorCategories.js';
import {ObservatorySlider} from './ObservatorySlider.js';
import {El,Note,Button,Tiles,Select,Diagram} from './NativeEnvironmentControls.js';
import {NativeFlarePresets,RenderFlareImage} from './NativeLensFlareKernel.js';
export class NativeFlarePreview {
 constructor(){this.Key='';this.Busy=false;}
 Refresh(S){const Copy=Object.fromEntries(Object.entries(S).filter(([K])=>K.startsWith('Flare'))),Key=JSON.stringify(Copy);if(this.Busy||Key===this.Key)return;
  if(typeof Worker==='undefined')return;
  this.Worker??=new Worker(new URL('./NativeFlareWorker.js',import.meta.url),{type:'module'});this.Busy=true;
  this.Worker.onmessage=E=>{this.Pixels=E.data.Pixels;this.Key=E.data.Key;this.Busy=false;this.Canvas??=document.createElement('canvas');this.Canvas.width=640;this.Canvas.height=280;const X=this.Canvas.getContext('2d'),Image=X.createImageData(640,280);for(let I=0;I<640*280;I++){for(let C=0;C<3;C++)Image.data[I*4+C]=255*(Math.max(0,this.Pixels[I*3+C])/(1+Math.max(0,this.Pixels[I*3+C])))**(1/2.2);Image.data[I*4+3]=255;}X.putImageData(Image,0,0);this.OnReady?.();};
  this.Worker.onerror=()=>{this.Busy=false;this.Error='Flare preview worker failed';};this.Worker.postMessage({Settings:Copy,Key});
 }
 Draw(P,S){this.Refresh(S);P.rect(0,0,640,280,'#080808',14);if(this.Canvas)P.X.drawImage(this.Canvas,0,0,640,280);}
 ResolvePixels(S){const Key=JSON.stringify(Object.fromEntries(Object.entries(S).filter(([K])=>K.startsWith('Flare'))));return this.Pixels&&Key===this.Key?this.Pixels:RenderFlareImage(S);}
 Export(S){const Pixels=this.ResolvePixels(S);const Header=new TextEncoder().encode('PF\n640 280\n-1.0\n'),BottomUp=new Float32Array(Pixels.length);for(let Y=0;Y<280;Y++)BottomUp.set(Pixels.subarray(Y*640*3,(Y+1)*640*3),(279-Y)*640*3);const Blob0=new Blob([Header,BottomUp],{type:'application/octet-stream'}),URL0=URL.createObjectURL(Blob0),Link=document.createElement('a');Link.href=URL0;Link.download='LensFlare.pfm';Link.click();setTimeout(()=>URL.revokeObjectURL(URL0),1000);}
 Dispose(){this.OnReady=null;this.Worker?.terminate();this.Worker=null;this.Busy=false;}
}
// Centres use the same source-to-sensor axis and spacing law as LFRender.
export function FlareGhostLayout(S){const N=Math.round(Math.max(0,Math.min(24,S.FlareGhosts)));return Array.from({length:N},(_,I)=>{const K=1-(.6+(I+1)/N*(1.7+S.FlareGhostSpacing));return [.5+(S.FlarePreviewX-.5)*K,.5+(S.FlarePreviewY-.5)*K];});}
export function FlareRayPairs(S){return Math.floor(Math.max(2,Math.min(12,S.FlarePreset==='Custom layers'?S.FlareRays:S.FlareAperture))+.5);}
export function BuildNativeFlare(Host,C,P){
 const S=Host.Settings,Preview=new NativeFlarePreview();let Alive=true;
 P.classList.add('lens-optics');C.Disposers??=[];C.Disposers.push(()=>{Alive=false;Preview.Dispose();});Preview.OnReady=()=>{if(Alive)C.Synchronise();};
 const Bind=F=>{F.Static=true;C.Live.push(F);C.Bindings.push(F);F();};
 const Edit=(K,V,Custom=true)=>{S[K]=V;if(Custom)S.FlarePreset='Custom layers';C.Notify(K,S);C.Synchronise();};
 const Field=(Parent,K,Label,Min,Max,Step=.01,Unit='',Custom=true)=>ObservatorySlider(C,Parent,S,K,Label,[Min,Max],(Key,V)=>Edit(Key,V,Custom),Unit,Step);
 const Section=(Parent,Name,Caption)=>{const E=El('section','lo-instrument');E.append(El('span','lo-overline',Caption),El('h2','',Name));Parent.appendChild(E);return E;};
 const Head=El('header','lo-header');Head.append(El('span','lo-overline','ENVIRONMENT / CAMERA OPTICS'),El('h1','','Lens flare'),El('p','','Reflections, diffraction, and light across the sensor.'));P.appendChild(Head);
 const Actions=El('div','lo-actions');Head.appendChild(Actions);const Toggle=Button(Actions,'',()=>Edit('FlareEnabled',!S.FlareEnabled,false));Bind(()=>{Toggle.textContent=S.FlareEnabled?'● Enabled':'○ Disabled';Toggle.setAttribute('aria-pressed',String(!!S.FlareEnabled));});Toggle.setAttribute('aria-label','Toggle lens flare');Button(Actions,'Export HDR ↗',()=>Preview.Export(S));
 const Composite=Section(P,'Optical composite','01 / LIVE PROCEDURAL REFERENCE');
 Diagram(C,Composite,640,280,X=>Preview.Draw(X,S),E=>{if(E.Key){if(E.Key==='Home'){S.FlarePreviewX=.28;S.FlarePreviewY=.43;}else{S.FlarePreviewX+=(E.Key==='ArrowRight'?.02:E.Key==='ArrowLeft'?-.02:0)*E.Step;S.FlarePreviewY+=(E.Key==='ArrowDown'?.02:E.Key==='ArrowUp'?-.02:0)*E.Step;}}else{S.FlarePreviewX=E.Point[0]/640;S.FlarePreviewY=E.Point[1]/280;}S.FlarePreviewX=Math.max(.06,Math.min(.94,S.FlarePreviewX));S.FlarePreviewY=Math.max(.08,Math.min(.92,S.FlarePreviewY));C.Notify('FlarePreviewX',S);C.Synchronise();},'Drag the flare source; arrow keys move the preview light');
 const Footer=El('div','lo-preview-footer'),Pose=El('span');Footer.append(Pose);Composite.appendChild(Footer);Bind(()=>Pose.textContent=`SOURCE ${S.FlarePreviewX.toFixed(2)} / ${S.FlarePreviewY.toFixed(2)}`);Button(Footer,'Reset source',()=>{S.FlarePreviewX=.28;S.FlarePreviewY=.43;C.Notify('FlarePreviewX',S);C.Synchronise();});
 Note(Composite,'Drag the source. Authoring preview stays visible when scene flare is disabled; scene flare follows the Sun, camera occlusion and cabin transmission. Display-mapped thumbnail · export retains linear HDR.');
 const WorkerStatus=El('p','lo-worker-status');Composite.appendChild(WorkerStatus);const Status=()=>{WorkerStatus.textContent=Preview.Error??(Preview.Busy?'Rendering reference…':'');};C.Live.push(Status);C.Bindings.push(Status);
 const {Panes}=InspectorCategories(C,P,[['compose','Compose'],['ghosts','Ghosts'],['halo','Halo'],['rays','Rays & streaks']],'lens-optics');
 const Compose=Section(Panes.compose,'Light stack','02 / SOURCE & PRESET');
 Select(C,Compose,S,'FlarePreset','Optical preset',[...Object.keys(NativeFlarePresets).map(N=>[N,N]),['Custom layers','Custom layers']],Name=>{if(NativeFlarePresets[Name])Object.assign(S,NativeFlarePresets[Name]);});
 Field(Compose,'FlareIntensity','Intensity',0,5,.01,'×',false);Field(Compose,'FlareSpread','Spread',.25,2,.01,'×',false);Field(Compose,'FlareChromatic','Chromatic separation',0,1,.01,'',false);
 Note(Compose,'Presets retain their original renderer behaviour. Editing layer geometry or a layer switch selects Custom layers; global intensity, spread and chromatic separation do not.');
 function Plot(Parent,W,H,Key,Draw,Drag,Label){const CV=Diagram(C,Parent,W,H,Draw,Drag,Label),Old=C.Live.at(-1);let Last;const Update=()=>{if(!Alive||Parent.closest?.('[hidden]'))return;const K=JSON.stringify(Key());if(K===Last)return;Last=K;Old();};Update.Static=true;C.Live[C.Live.length-1]=Update;C.Bindings[C.Bindings.indexOf(Old)]=Update;return CV;}
 const Ghosts=Section(Panes.ghosts,'Internal reflections','03 / SENSOR-PLANE POSITIONS');
 Plot(Ghosts,640,300,()=>[S.FlareGhosts,S.FlareGhostSpacing,S.FlareGhostShape,S.FlarePreviewX,S.FlarePreviewY],X=>{
  X.rect(35,30,570,230,'#101318',12);X.line([35,145],[605,145],'#bac5da18',1,[3,5]);X.line([320,30],[320,260],'#bac5da18',1,[3,5]);const SX=35+S.FlarePreviewX*570,SY=30+S.FlarePreviewY*230;
  const Positions=FlareGhostLayout(S);X.line([SX,SY],[320-(SX-320)*2.8,145-(SY-145)*2.8],'#b8a8ce55',1);
  Positions.forEach(([U,V],I)=>{const CX=35+U*570,CY=30+V*230;if(CX<40||CX>600||CY<36||CY>253)return;const R=9+(I%4)*4;if(S.FlareGhostShape>=3){const Points=Array.from({length:S.FlareGhostShape+1},(_,J)=>[CX+R*Math.cos(J*Math.PI*2/S.FlareGhostShape),CY+R*Math.sin(J*Math.PI*2/S.FlareGhostShape)]);X.path(Points,'#c3afd2aa',1.5);}else X.circle(CX,CY,R,'#c3afd210','#c3afd2aa');});X.circle(SX,SY,5,'#f3deb2');X.circle(320,145,4,'#c1c9d5');X.text(35,291,'SOURCE → SENSOR CENTRE → REFLECTIONS','#9b9dab',18,'left');
 },E=>Edit('FlareGhostSpacing',Math.max(0,Math.min(3,E.Key?E.Key==='Home'?1:S.FlareGhostSpacing+(['ArrowLeft','ArrowDown'].includes(E.Key)?-.05:.05)*E.Step:E.Point[0]/640*3))),'Ghost optical axis · drag or use arrow keys to edit spacing');
 Note(Ghosts,'Positions follow the actual kernel’s optical axis; outlines are a geometry guide, not radiance or exact ghost sizes. Off-sensor centres are omitted.');
 Select(C,Ghosts,S,'FlareGhostShape','Ghost shape',[[0,'Round'],[6,'Hexagon'],[8,'Octagon']],()=>S.FlarePreset='Custom layers');
 Field(Ghosts,'FlareGhosts','Ghost count',0,24,1);Field(Ghosts,'FlareGhostBrightness','Ghost brightness',0,2,.01,'×');Field(Ghosts,'FlareGhostSpacing','Ghost spacing',0,3,.01,'×');
 const Halo=Section(Panes.halo,'Annular response','04 / RADIAL CROSS-SECTION');
 Plot(Halo,640,280,()=>[S.FlareHaloRadius,S.FlareHaloWidth,S.FlareHaloBrightness],X=>{const Scale=95/.6,R=S.FlareHaloRadius*Scale,Width=S.FlareHaloWidth*Scale;X.circle(220,130,105,null,'#bdbea91d');X.circle(220,130,R,null,'#e5d5ab66');X.circle(220,130,Math.max(0,R-Width),null,'#e5d5ab22');X.circle(220,130,R+Width,null,'#e5d5ab22');X.line([220,130],[220+R,130],'#e5d5ab99',2);X.circle(220+R,130,6,'#e5d5ab');X.text(470,100,S.FlareHaloRadius.toFixed(2),'#e5d5ab',40);X.text(470,130,'RADIUS / UV UNITS','#a3a393',18);X.text(470,180,'± '+S.FlareHaloWidth.toFixed(3),'#c0baa9',24);X.text(470,206,'RESPONSE SUPPORT','#a3a393',17);},E=>Edit('FlareHaloRadius',Math.max(.01,Math.min(.6,E.Key?E.Key==='Home'?.22:S.FlareHaloRadius+(['ArrowLeft','ArrowDown'].includes(E.Key)?-.01:.01)*E.Step:Math.hypot(E.Point[0]-220,E.Point[1]-130)/ (95/.6)))),'Halo radius guide · drag or use arrow keys');
 Field(Halo,'FlareHaloRadius','Halo radius',.01,.6,.01);Field(Halo,'FlareHaloWidth','Halo width',.005,.2,.001);Field(Halo,'FlareHaloBrightness','Halo brightness',0,2,.01,'×');Note(Halo,'Radius and ± width mark the smooth radial response support in aspect-corrected UV units. This enlarged guide is not a physical lens section; the composite above is the procedural result.');
 const Rays=Section(Panes.rays,'Diffraction & streaks','05 / LAYER MIXER');Tiles(C,Rays,S,[['FlareAnamorphic','Anamorphic','MoveHorizontal'],['FlareWarmStreak','Warm streak','TrendingUp'],['FlareBurst','Starburst','Sparkles']]);
 const Count=El('div','lo-ray-count');Rays.appendChild(Count);Bind(()=>Count.textContent=`${FlareRayPairs(S)} ray pairs · ${S.FlarePreset==='Custom layers'?'Custom ray control':'Legacy aperture control'}`);
 Field(Rays,'FlareRotation','Rotation',0,360,1,'°');Field(Rays,'FlareRays','Ray pairs',2,12,1);Field(Rays,'FlareStreak','Streak gain',0,3,.01,'×');Field(Rays,'FlareAperture','Aperture blades',2,12,1,'',false);
 Note(Rays,'Preset starbursts use Aperture blades; Custom layers uses Ray pairs. Rotation turns the warm streak and burst, while the cool anamorphic streak stays horizontal, matching the renderer.');
 return P;
}
