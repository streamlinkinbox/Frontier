import {EnvironmentIconURL} from './EnvironmentIconAssets.js';
import {ConstructSlider,SliderDigits,SnapSlider} from './SliderControl.js';
import { AssignStyle } from "./EditorControls.js";
// Native geometry, spacing and typography: Engine/Editor/*InspectorPanel.cpp.
// DOM controls replace ImGui input hit regions; visual dimensions remain in CSS pixels.
import { ConstructGlyph } from './EditorIcons.js';
export const El=(Tag,Class='',Text='')=>{const E=document.createElement(Tag);E.className=Class;if(Text)E.textContent=Text;return E;};
export function Note(P,T){const E=El('p','ne-note',T);P.appendChild(E);return E;}
export function Card(P,Title,Icon='NativeOrbit'){const E=El('section','ne-card');E.dataset.nativeCard=Title;const H=El('h3');H.append(ConstructGlyph(Icon,17,1.5),El('span','',Title));E.appendChild(H);P.appendChild(E);return E;}
export function Metric(C,P,Read,Caption='',Unit='') {
 const E=El('div','ne-metric');const Value=El('span'),U=El('small','',Unit);E.append(Value,U);P.appendChild(E);
 const Update=()=>{Value.textContent=Read();};Update();C.Live.push(Update);C.Bindings.push(Update);if(Caption)Note(P,Caption);return E;
}
export function Button(P,Text,Action,Class=''){const E=El('button','ne-button '+Class,Text);E.type='button';E.addEventListener('click',Action);P.appendChild(E);return E;}
export function Tiles(C,P,S,Entries){const Row=El('div','ne-tiles');P.appendChild(Row);for(const [Key,Label,Icon='Sun'] of Entries){const B=Button(Row,'',()=>{if(Key){S[Key]=!S[Key];if(Key.startsWith('Flare')&&Key!=='FlareEnabled')S.FlarePreset='Custom layers';C.Notify(Key,S);C.Synchronise();}});const Mark=El('span','ne-tile-symbol');Mark.appendChild(ConstructGlyph(Icon,18));const Text=El('span','',Label),State=El('small');B.append(Mark,Text,State);B.disabled=!Key;const Update=()=>{B.classList.toggle('on',!!S[Key]);B.classList.toggle('off',!!Key&&!S[Key]);State.textContent=Key?(S[Key]?'ON':'OFF'):'UNAVAILABLE';};Update();C.Live.push(Update);C.Bindings.push(Update);}return Row;}
export const NativeSliderDigits=SliderDigits;
export const SnapNativeSlider=SnapSlider;
export function Slider(C,P,S,Key,Label,Min,Max,Step=.01,Unit='',OnChange=null){
 const Control=ConstructSlider(P,S,Key,{Label,Minimum:Min,Maximum:Max,Step,Unit,OnInput:V=>{OnChange?.(V);C.Notify(Key,S);C.Synchronise();}});
 C.Bindings.push(Control.Refresh);C.Live.push(Control.Refresh);return Control.Element;
}
export function Select(C,P,S,Key,Label,Options,Change=null){const E=El('label','ne-control');E.appendChild(El('span','ne-control-label',Label));const Input=El('select','ne-select');for(const [Value,Name] of Options){const O=El('option','',Name);O.value=Value;Input.appendChild(O);}Input.value=String(S[Key]);Input.addEventListener('change',()=>{S[Key]=typeof S[Key]==='number'?+Input.value:Input.value;Change?.(S[Key]);C.Notify(Key,S);C.Synchronise();});E.appendChild(Input);P.appendChild(E);const Update=()=>Input.value=String(S[Key]);C.Bindings.push(Update);return Input;}
export function Colour(C,P,S,Key,Label){const E=El('label','ne-control');E.appendChild(El('span','ne-control-label',Label));const I=El('input','ne-colour');I.type='color';I.value='#'+S[Key].map(V=>Math.round(Math.max(0,Math.min(1,V))**(1/2.2)*255).toString(16).padStart(2,'0')).join('');I.addEventListener('input',()=>{S[Key]=[1,3,5].map(N=>(parseInt(I.value.slice(N,N+2),16)/255)**2.2);C.Notify(Key,S);});E.appendChild(I);P.appendChild(E);}
export function Details(P,Title){const D=El('details','ne-details'),S=El('summary','',Title);D.appendChild(S);P.appendChild(D);return D;}
export function Grid(P,Class=''){const E=El('div','ne-grid '+Class);P.appendChild(E);return E;}
export function Header(C,P,Title,Subtitle,Icon){const H=El('header','ne-header');H.append(El('div','ne-breadcrumb','Inspector / Environment'),El('h1','',Title),El('p','ne-note',Subtitle));if(Icon){const I=El('img','ne-header-icon');I.src=EnvironmentIconURL(Icon);I.alt='';H.appendChild(I);}P.appendChild(H);return H;}
export function Diagram(C,P,W,H,Draw,Drag=null,Label='Interactive diagram') {
 const Canvas=El('canvas','ne-diagram');Canvas.width=W*2;Canvas.height=H*2;Canvas.style.aspectRatio=`${W}/${H}`;Canvas.setAttribute('aria-label',Label);Canvas.setAttribute('role',Drag?'group':'img');P.appendChild(Canvas);
 const Context=Canvas.getContext('2d');
 const Update=()=>{if(!Context?.beginPath)return;Context.setTransform(2,0,0,2,0,0);Context.clearRect(0,0,W,H);Draw(new Pen(Context,W,H),Canvas);};
 Update.Static=P.dataset?.nativeCard!=='Twinkle';
 C.Live.push(Update);C.Bindings.push(Update);Update();
 if(Drag){Canvas.tabIndex=0;Canvas.style.touchAction='none';let Last=null;
 const Point=E=>{const B=Canvas.getBoundingClientRect();return [(E.clientX-B.left)/B.width*W,(E.clientY-B.top)/B.height*H];};
 Canvas.addEventListener('pointerdown',E=>{if(E.button!==0)return;Last=Point(E);Canvas.focus();Canvas.setPointerCapture(E.pointerId);Drag({Point:Last,Start:true,DX:0,DY:0});Update();});
 Canvas.addEventListener('pointermove',E=>{if(!Last)return;const P=Point(E);Drag({Point:P,DX:P[0]-Last[0],DY:P[1]-Last[1]});Last=P;Update();});
 for(const Event of ['pointerup','pointercancel','lostpointercapture'])Canvas.addEventListener(Event,()=>{Last=null;P.dispatchEvent?.(new globalThis.Event('environment-edit',{bubbles:true}));});
 Canvas.addEventListener('keydown',E=>{if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(E.key)){E.preventDefault();Drag({Key:E.key,Step:E.shiftKey?10:1});Update();}});
 }
 return Canvas;
}
export class Pen {
 constructor(X,W,H){this.X=X;this.W=W;this.H=H;}
 line(A,B,C='#555',Width=1,Dash=[]){const X=this.X;X.beginPath();X.strokeStyle=C;X.lineWidth=Width;X.setLineDash(Dash);X.moveTo(...A);X.lineTo(...B);X.stroke();X.setLineDash([]);}
 path(P,C='#999',Width=1,Dash=[]){const X=this.X;X.beginPath();X.strokeStyle=C;X.lineWidth=Width;X.setLineDash(Dash);P.forEach((V,I)=>I?X.lineTo(...V):X.moveTo(...V));X.stroke();X.setLineDash([]);}
 circle(X,Y,R,C,Stroke=null){this.ellipse(X,Y,R,R,C,Stroke);}
 ellipse(X,Y,RX,RY,Fill,Stroke=null){const C=this.X;C.beginPath();C.ellipse(X,Y,RX,RY,0,0,Math.PI*2);if(Fill){C.fillStyle=Fill;C.fill();}if(Stroke){C.strokeStyle=Stroke;C.lineWidth=1;C.stroke();}}
 text(X,Y,T,C='#919191',Size=10,Align='center'){this.X.fillStyle=C;this.X.font=`${Size}px "DM Sans", sans-serif`;this.X.textAlign=Align;this.X.fillText(T,X,Y);}
 rect(X,Y,W,H,C,Radius=0){const Q=this.X;Q.beginPath();Q.roundRect(X,Y,W,H,Radius);Q.fillStyle=C;Q.fill();}
 glow(X,Y,R,C='255,218,135',A=.3){const G=this.X.createRadialGradient(X,Y,0,X,Y,R);G.addColorStop(0,`rgba(${C},${A})`);G.addColorStop(1,`rgba(${C},0)`);this.circle(X,Y,R,G);}
}
export function Orbit(P,Az,El){
 const A=(Az-135)*Math.PI/180,Point=T=>[180-103*Math.cos(T)*Math.cos(A),143-103*Math.sin(T)+103*.45*Math.cos(T)*Math.sin(A)];
 P.ellipse(180,143,103,49,'#15151599','#707070a5');
 for(let I=0;I<17;I++){const X=77+I*13,DX=(X-180)/103;if(Math.abs(DX)<=1){const H=49*Math.sqrt(1-DX*DX);P.line([X,143-H],[X,143+H],'#ffffff0c',.65);}const Y=94+I*13,DY=(Y-143)/49;if(Math.abs(DY)<=1){const W=103*Math.sqrt(1-DY*DY);P.line([180-W,Y],[180+W,Y],'#ffffff0c',.65);}}
 P.path(Array.from({length:81},(_,I)=>Point(Math.PI+I*Math.PI/80)),'#77736b80',1,[2,3]);P.path(Array.from({length:81},(_,I)=>Point(I*Math.PI/80)),'#e2ac5b',1.8);
 const S=Point(El*Math.PI/180),H=Point(0);P.line(S,H,'#b8aa8e88',.8,[2,4]);P.ellipse(S[0],H[1],6,2.3,'#c5c0b650');P.circle(...H,5.5,'#181818','#f1efe7');if(El>=0)P.glow(...S,23);P.circle(...S,8,El<0?'#9b805c':'#f4cd75');if(El>=0)P.circle(...S,4.4,'#fff4c7');
 for(const [X,Y,T] of [[180,24,'12h'],[180,269,'24h'],[180,81,'N'],[180,210,'S'],[47,147,'W'],[313,147,'E']])P.text(X,Y,T,'#9c9b95');
}
export function DayCurve(P,Time){const F=T=>60-42*Math.sin((T-6)/24*Math.PI*2);P.rect(18,10,116,98,'#8397c109',4);P.rect(366,10,116,98,'#8397c109',4);P.line([18,60],[482,60],'#8c806b44',1,[3,5]);P.path(Array.from({length:97},(_,I)=>[18+I/96*464,F(I/4)]),'#a8a1a173',1.3);P.path(Array.from({length:49},(_,I)=>[134+I/48*232,F(6+I/4)]),'#dac18a',1.5);const X=18+Time/24*464,Y=F(Time);P.line([X,10],[X,110],'#c0ad8c73',1,[2,4]);P.circle(X,Y,11,'#e9c88918');P.circle(X,Y,4,'#efd19a');P.text(28,120,'NIGHT','#7a7770',8,'left');P.text(235,120,'DAY','#7a7770',8,'left');P.text(442,120,'NIGHT','#7a7770',8,'left');}
export function Illuminance(P,Fraction){for(const Y of [24,50,77])P.line([14,Y],[346,Y],'#ffffff0b',1,[2,5]);const Y=T=>76-56*(1-Math.cos(T*Math.PI/2));P.path(Array.from({length:61},(_,I)=>[14+I/60*332,Y(I/60)]),'#595246',1.2);P.path(Array.from({length:61},(_,I)=>[14+Fraction*I/60*332,Y(Fraction*I/60)]),'#dac18b',1.6);P.line([14+Fraction*332,Y(Fraction)+6],[14+Fraction*332,88],'#c8af7a59',1,[2,4]);P.circle(14+Fraction*332,Y(Fraction),3.4,'#efe2c2');}
export function RotationDial(P,Angle){for(const R of [34,62,88])P.circle(170,111,R,null,'#b39bcc26');for(let I=0;I<24;I++){const A=I*Math.PI/12;P.line([170+Math.sin(A)*83,111-Math.cos(A)*83],[170+Math.sin(A)*88,111-Math.cos(A)*88],'#9e87b599',.8);}const A=Angle*Math.PI/180,R=(X,Y)=>[170+X*Math.cos(A)-Y*Math.sin(A),111+X*Math.sin(A)+Y*Math.cos(A)],Pts=[[-32,-41],[10,-56],[31,-20],[42,21],[8,57],[-40,39]].map(V=>R(...V));P.path(Pts,'#b49bcc80');Pts.forEach((V,I)=>P.circle(...V,I%2?2:3,'#c8b8e5'));P.line([170,111],R(0,-88),'#c4a8e259');P.circle(...R(0,-88),6,'#312638','#ddc8f1');P.circle(170,111,3,'#d4c1e7');for(const [X,Y,T] of [[170,12,'0°'],[272,115,'90°'],[170,217,'180°'],[63,115,'270°'],[170,237,'Drag the ring · celestial pole']])P.text(X,Y,T,'#919191',9);}
export function DragAngles(S,Az,El,Disabled=()=>false){return E=>{if(Disabled())return;if(E.Key){const Step=E.Step;if(E.Key==='Home'){S[Az]=S[El]=0;}else {S[Az]+=(E.Key==='ArrowRight'?Step:E.Key==='ArrowLeft'?-Step:0);S[El]+=(E.Key==='ArrowUp'?Step:E.Key==='ArrowDown'?-Step:0);}}else{S[Az]+=E.DX*.7;S[El]-=E.DY*.65;}S[Az]=((S[Az]+180)%360+360)%360-180;S[El]=Math.max(-90,Math.min(90,S[El]));};}
