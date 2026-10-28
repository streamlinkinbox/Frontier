// CPU transcription of native MoonAtlasPreview. Uses the same registered 2K textures
// and sphere-normal / phase conventions as CelestialSky.wgsl, never painted circles.
import { MoonBodies } from './CelestialAtlas.js';
const Maps=new Map();
function MapFor(Body) {
 if(Maps.has(Body))return Maps.get(Body);
 const State={Ready:false,Waiters:new Set()};Maps.set(Body,State);
 if(typeof Image==='undefined')return State;
 const ImageElement=new Image();ImageElement.onload=()=>{const C=document.createElement('canvas');C.width=ImageElement.width;C.height=ImageElement.height;const X=C.getContext('2d');X.drawImage(ImageElement,0,0);State.Pixels=X.getImageData(0,0,C.width,C.height).data;State.Width=C.width;State.Height=C.height;State.Ready=true;for(const Wake of State.Waiters)Wake();State.Waiters.clear();};ImageElement.src=`Assets/Celestial/${MoonBodies[Body].Key}_2k.jpg`;return State;
}
export class NativeMoonPreview {
 constructor(OnReady=null){this.Key='';this.OnReady=OnReady;}
 Draw(P,S,Comparison=false,Guide=false) {
  const Body=MoonBodies[S.MoonBody??0],Map=MapFor(S.MoonBody??0),Key=JSON.stringify([S.MoonBody,S.MoonPhase,S.MoonRoll,S.MoonPitch,S.MoonSpin,S.MoonBrightness,Map.Ready]);
  if(!Map.Ready&&this.OnReady)Map.Waiters.add(this.OnReady);
  if(Map.Ready && Key!==this.Key){this.Key=Key;const Canvas=this.Canvas??=document.createElement('canvas');Canvas.width=256;Canvas.height=256;const X=Canvas.getContext('2d'),Image=X.createImageData(256,256);const A=(S.MoonRoll??0)*Math.PI/180,Pitch=(Body.Tilt+(S.MoonPitch??0))*Math.PI/180,Spin=(S.MoonSpin??0)*Math.PI/180,Phase=S.MoonPhase/29.53*Math.PI*2;for(let Y=0;Y<256;Y++)for(let V=0;V<256;V++){
   const NX=(V+.5-128)/126,NY=(128-Y-.5)/126,R2=NX*NX+NY*NY;if(R2>1)continue;const NZ=Math.sqrt(1-R2),RX=NX*Math.cos(A)-NY*Math.sin(A),RY=NX*Math.sin(A)+NY*Math.cos(A),TY=RY*Math.cos(Pitch)-NZ*Math.sin(Pitch),TZ=RY*Math.sin(Pitch)+NZ*Math.cos(Pitch);const U=((Math.atan2(RX,TZ)+Spin)/(2*Math.PI)+.5+2)%1,T=.5-Math.asin(Math.max(-1,Math.min(1,TY)))/Math.PI;const Offset=(Math.min(Map.Height-1,Math.floor(T*Map.Height))*Map.Width+Math.min(Map.Width-1,Math.floor(U*Map.Width)))*4;
   const Wrap=Body.Haze*.3,NDL=Math.max(0,(RX*Math.sin(Phase)-NZ*Math.cos(Phase)+Wrap)/(1+Wrap)),Limb=1-.5*(1-NZ)**2*Math.max(Body.Haze,.35);const Light=(NDL**.8*Limb+.012)*S.MoonBrightness;
   for(let C=0;C<3;C++){const Alb=(Map.Pixels[Offset+C]/255)**(2.2*Body.Gamma);Image.data[(Y*256+V)*4+C]=Math.round(255*Math.min(1,Alb*Light)**(1/2.2));}Image.data[(Y*256+V)*4+3]=255;
  }X.putImageData(Image,0,0);}
  const R=Comparison?20+54*Math.sqrt(S.MoonSize/(S.MoonSize+12)):60;
  if(Comparison){P.circle(128,104,64,null,'#aab9cc22');}
  if(this.Canvas)P.X.drawImage(this.Canvas,128-R,104-R,R*2,R*2);else P.text(128,104,'Loading native texture…','#869bb6',10);
  const Roll=(S.MoonRoll??0)*Math.PI/180;P.line([128-Math.sin(Roll)*(R+4),104-Math.cos(Roll)*(R+4)],[128-Math.sin(Roll)*(R+10),104-Math.cos(Roll)*(R+10)],'#c5d9f2');
  if(Guide){const Q=[];for(let I=0;I<=120;I++){const A=I*Math.PI/60;Q.push([128+Math.cos(A)*R,104+Math.sin(A)*Math.sin((S.MoonPitch??0)*Math.PI/180)*R]);}P.path(Q,'#d3e2f059',.7,[2,3]);}
  P.text(128,202,Comparison?`${S.MoonSize.toFixed(2)}° angular diameter`:`Roll ${(S.MoonRoll??0).toFixed(0)}° · Pitch ${(S.MoonPitch??0).toFixed(0)}°`,'#869bb6',9);
 }
}
export function LunarSphere(P,S,Preview) {
 const Project=(Az,El)=>{const A=Az*Math.PI/180,B=El*Math.PI/180,X=Math.sin(A)*Math.cos(B),Z=Math.cos(A)*Math.cos(B),Y=Math.sin(B);return [180+85*X,117-85*(Y*Math.cos(.34)-Z*Math.sin(.34)),Z*Math.cos(.34)+Y*Math.sin(.34)];};
 P.circle(180,117,102,null,'#91a9c022');for(let I=0;I<36;I++){const A=I*Math.PI/18;P.line([180+102*Math.sin(A),117-102*Math.cos(A)],[180+(102-(I%3===0?5:2))*Math.sin(A),117-(102-(I%3===0?5:2))*Math.cos(A)],'#8098b073',.7);}
 const G=P.X.createRadialGradient(155,90,2,180,117,100);G.addColorStop(0,'#455263');G.addColorStop(.6,'#252f3c');G.addColorStop(1,'#151b25');P.circle(180,117,85,G,'#7897ba55');
 for(const Lat of [-60,-30,0,30,60])P.path(Array.from({length:73},(_,I)=>Project(I*5,Lat).slice(0,2)),Lat===0?'#b5d5f08c':'#7a99b533',Lat===0?1.1:.6);
 for(const Lon of [0,30,60,90,120,150])P.path(Array.from({length:73},(_,I)=>Project(Lon,I*5).slice(0,2)),'#90afc730',.6);
 const Q=Project(S.MoonAzimuth,S.MoonElevation);P.path(Array.from({length:37},(_,I)=>Project(S.MoonAzimuth,I*S.MoonElevation/36).slice(0,2)),'#aacfeb',1.4,Q[2]<0?[3,3]:[]);P.line([180,117],Q.slice(0,2),'#c8def259',1,[2,4]);P.circle(Q[0],Q[1],17,'#b6d2ed0c');if(Preview.Canvas)P.X.drawImage(Preview.Canvas,Q[0]-12,Q[1]-12,24,24);P.circle(180,117,2,'#c5d8e5');for(const [X,Y,T]of [[180,9,'N'],[68,121,'W'],[292,121,'E'],[180,231,'S'],[180,252,Q[2]<0?'Far side · dashed guide':'Near side · drag to orbit']])P.text(X,Y,T,'#8ca2bd',9);
}
