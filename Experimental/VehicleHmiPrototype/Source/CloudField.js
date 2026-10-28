// CPU diagnostic of the new baked shape field, matching CloudTrace with optional renderer advection offsets.
import {BuildCloudField} from './CloudSettings.js';
const Clamp=V=>Math.max(0,Math.min(1,V));let Field;
export function SetCloudDiagnosticField(Data){if(Data instanceof Uint8Array&&Data.length===64**3)Field=Data;}
function Sample(A,N,P){const Q=P.map(V=>V*N-.5),I=Q.map(Math.floor),F=Q.map((V,J)=>V-I[J]);let S=0;const Wrap=V=>(V%N+N)%N;
 for(let Z=0;Z<(P.length===3?2:1);Z++)for(let Y=0;Y<2;Y++)for(let X=0;X<2;X++){const At=P.length===3?(Wrap(I[2]+Z)*N+Wrap(I[1]+Y))*N+Wrap(I[0]+X):Wrap(I[1]+Y)*N+Wrap(I[0]+X);S+=A[At]/255*(X?F[0]:1-F[0])*(Y?F[1]:1-F[1])*(P.length===3?(Z?F[2]:1-F[2]):1);}return S;}
const Fract=V=>V-Math.floor(V),Smooth=(A,B,V)=>{const T=Clamp((V-A)/(B-A));return T*T*(3-2*T);};
export function LocalCloudNoise(P){const B=P.map(Math.floor),F=P.map((V,I)=>{const T=V-B[I];return T*T*(3-2*T);});let Sum=0;
 const Hash=P=>{let Q=P.map(V=>Fract(V*.1031));const D=Q[0]*(Q[2]+31.32)+Q[1]*(Q[1]+31.32)+Q[2]*(Q[0]+31.32);Q=Q.map(V=>V+D);return Fract((Q[0]+Q[1])*Q[2]);};
 for(let Z=0;Z<2;Z++)for(let Y=0;Y<2;Y++)for(let X=0;X<2;X++)Sum+=Hash([B[0]+X,B[1]+Y,B[2]+Z])*(X?F[0]:1-F[0])*(Y?F[1]:1-F[1])*(Z?F[2]:1-F[2]);return Sum;
}
export function CloudProfile(H,Type=2,Anvil=.5){if(H<=0||H>=1)return 0;let V=Clamp(H*8)*Clamp((1-H)*3);if(Type<1.5)V=Clamp(H*12)*Clamp((1-H)*8);if(Type>2.5&&Type<3.5)V*=1+Anvil*H*.5;if(Type>3.5)V*=Math.max(0,1-(H*2-1)**2);return V;}
export function CloudDensity(S,P,Local=false,Advection=[0,0,0]){Field??=BuildCloudField();
 if(Local){if(!S.LocalCloudEnabled)return 0;const Q=P.map((V,I)=>(V-S['LocalCloud'+['X','Y','Z'][I]])/S['LocalCloudHalf'+['X','Y','Z'][I]]);const Margin=1-(S.LocalCloudShape>.5?Math.max(...Q.map(Math.abs)):Math.hypot(...Q));const Mask=Smooth(0,Math.max(.05,S.LocalCloudSoftness),Margin);if(Mask<=0)return 0;
 const H=Clamp(Q[2]*.5+.5);let Profile=Smooth(0,.1,H)*(1-Smooth(.75,1,H));if(S.LocalCloudForm>.5&&S.LocalCloudForm<1.5)Profile=Smooth(0,.08,H)*(1-Smooth(.4,1,H))*1.15;if(S.LocalCloudForm>1.5)Profile=Smooth(0,.3,H)*(1-Smooth(.6,1,H))*.6;
 const Advected=P.map((V,I)=>V-Advection[I]);const UV=[Advected[0],Advected[2],Advected[1]].map(V=>V/Math.max(1,S.LocalCloudScale));let Shape=0;
 for(const [Scale,Offset,Weight]of [[1,[0,0,0],.5],[2.02,[3.1,1.7,9.2],.25],[4.1,[7.7,2.2,1.1],.125],[8.3,[1.3,8.8,4.4],.0625]])Shape+=LocalCloudNoise(UV.map((V,I)=>V*Scale+Offset[I]))*Weight;
 const Base=Clamp((Shape/.9375-(1-S.LocalCloudCoverage))/Math.max(.001,S.LocalCloudCoverage))*Profile*Mask;if(Base<=0)return 0;
 const Detail=LocalCloudNoise(UV.map(V=>V*9))*.6+LocalCloudNoise(UV.map(V=>V*19+5))*.4;const T=Clamp(H*3),Erode=(Detail+(1-2*Detail)*T)*S.LocalCloudDetail*.45;return Clamp(Base-Erode*(1-Base))*S.LocalCloudDensity;}

 if(!S.CloudEnabled)return 0;const Top=Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness),H=(P[2]-S.CloudBase)/Math.max(1,Top-S.CloudBase);if(Top<=S.CloudBase||H<=0||H>=1)return 0;
 const UV=P.map((V,I)=>(V-Advection[I])/Math.max(7200*S.CloudScale,1));const Shape=Sample(Field,64,UV)-(S.CloudDetail??.3)*(1-Sample(Field,64,UV.map((V,I)=>V*4.1+[.21,.47,.13][I])))*.22;const E=Clamp((Shape-(1-S.CloudCoverage))/Math.max(.08,S.CloudCoverage*.65));return E*E*(3-2*E)*CloudProfile(H,S.CloudType,S.CloudAnvil)*S.CloudDensity;
}
export function CloudDensityImages(S,Local,Aspect=2,W=192,H=96){const Top=new Uint8ClampedArray(W*H*4),Side=new Uint8ClampedArray(W*H*4),Low=Local?S.LocalCloudZ-S.LocalCloudHalfZ:S.CloudBase,High=Local?S.LocalCloudZ+S.LocalCloudHalfZ:Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness);
 for(let Y=0;Y<H;Y++)for(let X=0;X<W;X++){
 const U=(X+.5)/W,V=(Y+.5)/H,HalfX=Local?Math.max(S.LocalCloudHalfX,S.LocalCloudHalfY*Aspect):3000,PX=Local?S.LocalCloudX+(U*2-1)*HalfX:(U-.5)*6000,PY=Local?S.LocalCloudY+(1-V*2)*HalfX/Aspect:(.5-V)*6000/Aspect;
 let Sum=0;for(let K=0;K<8;K++)Sum+=CloudDensity(S,[PX,PY,Low+(K+.5)/8*(High-Low)],Local);
 const D=CloudDensity(S,[Local?S.LocalCloudX+(U*2-1)*S.LocalCloudHalfX:PX,Local?S.LocalCloudY:0,(Local?High:S.CloudCeiling)-V*((Local?High:S.CloudCeiling)-(Local?Low:0))],Local);
 for(const [Pixels,A]of [[Top,Sum/8],[Side,D]]){const F=1-Math.exp(-Math.max(0,A)*3),I=(Y*W+X)*4;Pixels[I]=26+F*180;Pixels[I+1]=35+F*181;Pixels[I+2]=47+F*182;Pixels[I+3]=255;}
 }return {Top,Side,W,H};}
