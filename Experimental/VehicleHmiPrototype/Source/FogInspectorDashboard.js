import {El,Button,Diagram} from './NativeEnvironmentControls.js';
import {InspectorCategories} from './InspectorCategories.js';
import {ObservatorySlider} from './ObservatorySlider.js';
const Ranges={FogDensity:[0,.05],FogHeight:[1,500],FogScatter:[0,1],AerialDensity:[0,.001],AerialStart:[0,2000],AerialMie:[0,1]};
const Clamp=(V,A,B)=>Math.max(A,Math.min(B,V));
export function SetFogInspectorValue(S,K,V){if(!Ranges[K]||!Number.isFinite(V))return false;S[K]=Clamp(V,...Ranges[K]);return true;}
// CPU transcription of FogMedium.wgsl, with either geometry medium isolated.
// These are diagnostic paths, independent of the scene's visibility toggles.
export function FogPathTransmission(S,Camera,Position,Kind='height'){
    const Distance=Math.hypot(...Position.map((V,I)=>V-Camera[I])),H=Math.max(S.FogHeight,.1),Delta=(Position[2]-Camera[2])/H;
    const Column=Math.abs(Delta)>.001?(1-Math.exp(Clamp(-Delta,-50,50)))/Delta:1;
    const HeightOD=Kind==='aerial'?0:S.FogDensity*Distance*Math.exp(Clamp(-Camera[2]/H,-50,20))*Column;
    const AerialOD=Kind==='height'?0:S.AerialDensity*Math.max(0,Distance-S.AerialStart);
    return [.45,.7,1].map(Band=>Math.exp(-Math.min(80,HeightOD*HeightOD+(Band*(1-S.AerialMie)+S.AerialMie)*AerialOD)));
}
export function FogForwardWeight(Angle,Scatter){return Math.max(0,Math.cos(Angle))**8*Scatter;}
export function FogNeutralContrast(T){return T[0]*.2126+T[1]*.7152+T[2]*.0722;}
export function BuildFogInspectorDashboard(H,C,P,Aerial=false){
    const S=H.Settings,Kind=Aerial?'aerial':'height',Enabled=Aerial?'AerialEnabled':'FogEnabled',Range=Aerial?20000:500;
    const Probe={Distance:Aerial?8000:200,Height:2};let Alive=true;
    C.Disposers.push(()=>Alive=false);P.classList.add('fog-dashboard',Aerial?'fog-aerial':'fog-height');
    const Bind=F=>{F.Static=true;C.Bindings.push(F);C.Live.push(F);F();};
    const Change=(K,V)=>{if(SetFogInspectorValue(S,K,V)){C.Notify(K,S);C.Synchronise();}};
    const Field=(Parent,K,Label,Unit='',Step=.01)=>ObservatorySlider(C,Parent,S,K,Label,Ranges[K],Change,Unit,Step);
    const Diagnostic=(Parent,K,Label,Limits,Step=1)=>ObservatorySlider(C,Parent,Probe,K,Label,Limits,()=>C.Synchronise(),'m',Step);
    const Header=El('header','fg-header');Header.append(El('span','fg-eyebrow','ENVIRONMENT / GEOMETRY MEDIA'),El('h1','',Aerial?'Atmospheric fog':'Height fog'),El('p','',Aerial?'Distance, a clear start, then spectral extinction.':'A low-lying medium. A measurable loss of contrast.'));P.appendChild(Header);
    const Power=Button(Header,'',()=>{S[Enabled]=!S[Enabled];C.Notify(Enabled,S);C.Synchronise();},'fg-power');Power.setAttribute('aria-label',Aerial?'Toggle atmospheric fog':'Toggle height fog');
    Bind(()=>{Power.textContent=S[Enabled]?'● On':'○ Off';Power.setAttribute('aria-pressed',String(!!S[Enabled]));});
    const {Panes}=InspectorCategories(C,P,[['visibility','Visibility'],['medium','Medium'],['response',Aerial?'Spectrum':'Colour & sun']],'fog-'+Kind);
    function Section(Parent,Name,Caption,Class){const E=El('section',Class),Head=El('div','fg-section-head');Head.append(El('h2','',Name),El('span','',Caption));E.appendChild(Head);Parent.appendChild(E);return E;}
    function Note(Parent,Text){const E=El('p','fg-note',Text);Parent.appendChild(E);return E;}
    function Plot(Parent,W,H,Key,Draw,Drag,Label){const Canvas=Diagram(C,Parent,W,H,Draw,Drag,Label),Redraw=C.Live[C.Live.length-1];let Last;const Update=()=>{if(!Alive||Parent.closest?.('[hidden]'))return;const K=JSON.stringify(Key());if(K===Last)return;Last=K;Redraw();};Update.Static=true;C.Live[C.Live.length-1]=Update;C.Bindings[C.Bindings.indexOf(Redraw)]=Update;return Canvas;}
    const T=Distance=>FogPathTransmission(S,[0,0,Probe.Height],[Distance,0,Probe.Height],Kind);
    const ModelKey=()=>[S.FogDensity,S.FogHeight,S.AerialDensity,S.AerialStart,S.AerialMie,Probe.Height,Probe.Distance];
    const Transect=Section(Panes.visibility,'A walk into the fog','NEUTRAL CONTRAST TARGETS','fg-transect');
    const Readout=El('div','fg-readout'),Value=El('strong'),Unit=El('span','','% contrast retained');Readout.append(Value,Unit);Transect.appendChild(Readout);
    Bind(()=>Value.textContent=(FogNeutralContrast(T(Probe.Distance))*100).toFixed(1));
    Plot(Transect,640,425,ModelKey,X=>{
        X.path([[45,350],[305,110],[590,350]],'#b1c7c328',1);X.line([40,350],[600,350],'#b1c7c336');
        for(let I=5;I>=0;I--){const Distance=Range*I/5,Scale=1/(1+I*.38),W=90*Scale,H=145*Scale,CX=100+I*90,CY=315-I*28,Channels=T(Distance);
            const RGB=Linear=>Channels.map(V=>Math.round((Linear*V+.16*(1-V))**(1/2.2)*255));
            X.rect(CX-W/2-4,CY-H-4,W+8,H+8,'#131b1c',7);
            X.rect(CX-W/2,CY-H,W,H,`rgb(${RGB(.08).join(',')})`,3);
            X.rect(CX-W/2,CY-H,W/2,H,`rgb(${RGB(.8).join(',')})`,3);
            X.text(CX,CY+23,Distance>=1000?(Distance/1000).toFixed(1)+' km':Distance.toFixed(0)+' m','#a8bbb7',18);
        }
        const PX=45+Probe.Distance/Range*550;
        X.line([45,390],[595,390],'#b4cfca44',2);
        X.circle(PX,390,10,'#d5e4dc');X.line([PX,370],[PX,405],'#d5e4dc66');
        X.text(45,36,'NEAR / CLEAR','#94aaa4',19,'left');X.text(595,36,'FAR / ATTENUATED','#94aaa4',19,'right');
    },E=>{Probe.Distance=Clamp(E.Key?E.Key==='Home'?0:Probe.Distance+(['ArrowLeft','ArrowDown'].includes(E.Key)?-1:1)*Range/100*E.Step:(E.Point[0]-45)/550*Range,0,Range);C.Synchronise();},'Fog distance transect · drag the ruler or use arrow keys');
    Diagnostic(Transect,'Distance','Probe distance',[0,Range]);Diagnostic(Transect,'Height','Probe height',[0,1000]);
    Note(Transect,'Fixed neutral targets under fixed reference light, not a scene render. Horizontal paths use the renderer’s extinction law at the chosen world height. Probe edits do not change the scene.');
    const Status=El('p','fg-status');Panes.visibility.appendChild(Status);Bind(()=>Status.textContent=S[Enabled]?'Scene medium enabled · affects geometry in Final image view, not the sky.':'Scene medium is off · the diagnostic still shows its configured extinction.');

    const Medium=Section(Panes.medium,Aerial?'The clear interval':'Density terraces',Aerial?'DISTANCE GATE':'WORLD HEIGHT / RELATIVE DENSITY',Aerial?'fg-distance-gate':'fg-terraces');
    if(!Aerial){
        Plot(Medium,640,455,()=>[S.FogHeight],X=>{
            const Top=Math.max(200,S.FogHeight*6);
            for(let I=7;I>=0;I--){const Z=I/7*Top,Rho=Math.exp(-Z/S.FogHeight),Y=380-I*43,W=310*Rho,Left=110;
                X.path([[Left,Y],[Left+310,Y],[Left+345,Y-17],[Left+35,Y-17],[Left,Y]],'#a8c8be21',1);
                if(W>0.2){const Ctx=X.X;Ctx.beginPath();Ctx.moveTo(Left,Y);Ctx.lineTo(Left+W,Y);Ctx.lineTo(Left+W+35,Y-17);Ctx.lineTo(Left+35,Y-17);Ctx.closePath();Ctx.fillStyle=`rgba(164,198,182,${.14+Rho*.5})`;Ctx.fill();}
                X.text(78,Y-2,Math.round(Z)+' m','#92aaa0',19,'right');X.text(590,Y-2,(Rho*100).toFixed(1)+'%','#bdd1c4',19,'right');
            }
            X.text(110,435,'SEA LEVEL → UPPER MEDIUM','#8ba396',19,'left');
        },null,'Fog density terraces · exponential falloff with world height');
        Field(Medium,'FogDensity','Fog density','/m',.0001);Field(Medium,'FogHeight','Falloff height','m',1);
        Note(Medium,'Each terrace is a world altitude. Filled width is exp(−height / falloff height), relative to sea level. The altitude range adapts to the falloff setting; density remains the sea-level coefficient.');
    }else{
        Plot(Medium,640,265,()=>[S.AerialStart,S.AerialDensity],X=>{
            const PX=48+S.AerialStart/2000*544;X.rect(48,40,PX-48,145,'#a8c8be0c',8);X.line([48,185],[592,185],'#a8c8be55',1);X.line([PX,30],[PX,195],'#d8e1bb',2,[3,6]);X.circle(PX,30,8,'#d8e1bb');
            X.path(Array.from({length:81},(_,I)=>{const D=I/80*2000;return [48+I/80*544,175-120*(1-Math.exp(-S.AerialDensity*Math.max(0,D-S.AerialStart)))];}),'#b4d1cd',2.5);
            X.text(48,225,'0 m','#8ba396',20,'left');X.text(592,225,'2,000 m','#8ba396',20,'right');X.text(320,261,'NO EXTINCTION BEFORE THE START','#8ba396',19);
        },E=>Change('AerialStart',E.Key?E.Key==='Home'?0:S.AerialStart+(['ArrowLeft','ArrowDown'].includes(E.Key)?-10:10)*E.Step:(E.Point[0]-48)/544*2000),'Aerial clear-start gate · drag or use arrow keys');
        Field(Medium,'AerialDensity','Aerial density','/m',.00001);Field(Medium,'AerialStart','Clear start distance','m',10);Note(Medium,'The clear interval is distance from the camera, not world altitude. The schematic curves grey-reference attenuation; channel-specific transmission is in Spectrum.');
    }
    const Response=Section(Panes.response,Aerial?'Three spectral shutters':'In-scattered colour',Aerial?'RGB TRANSMISSION':'LINEAR RGB SOURCE',Aerial?'fg-shutters':'fg-colour');
    if(Aerial){
        Plot(Response,640,340,ModelKey,X=>{
            const Values=T(Probe.Distance);
            for(let I=0;I<3;I++){const Left=65+I*190,V=Values[I],Colour=['#c9a397','#b9cca1','#a6c7d3'][I];X.rect(Left,40,130,205,'#101617',15);for(let J=0;J<12;J++){const Y=48+J*16;X.rect(Left+8,Y,114,10,'#ffffff08',3);const Fill=Clamp(V*12-J,0,1);if(Fill>0)X.rect(Left+8,Y,114*Fill,10,Colour,3);}X.text(Left+65,285,(V*100).toFixed(1)+'%',Colour,30);X.text(Left+65,325,['RED','GREEN','BLUE'][I],'#8fa79d',19);}
        },null,'Aerial RGB extinction shutters at the selected probe distance');
        Field(Response,'AerialMie','Mie blend','',.01);Diagnostic(Response,'Distance','Spectrum probe distance',[0,Range]);Note(Response,'Spectral extinction blends RGB coefficients [0.45, 0.70, 1.00] toward equal grey extinction as Mie blend increases. These are the existing shader coefficients, not measured wavelengths.');
    }else{
        const Tint=El('label','fg-tint'),Chip=El('i'),Hex=El('strong'),Input=El('input');Input.type='color';Input.setAttribute('aria-label','Fog tint');Tint.append(Chip,El('span','','Fog tint'),Hex,Input);Response.appendChild(Tint);
        Bind(()=>{const V='#'+S.FogColour.map(C=>Math.round(Clamp(C,0,1)**(1/2.2)*255).toString(16).padStart(2,'0')).join('');Input.value=V;Chip.style.background=V;Hex.textContent=V.toUpperCase();});Input.addEventListener('input',()=>{S.FogColour=[1,3,5].map(I=>(parseInt(Input.value.slice(I,I+2),16)/255)**2.2);C.Notify('FogColour',S);C.Synchronise();});
        Note(Response,'Picker values are sRGB-encoded; the renderer stores linear RGB. This is the tint of in-scattered light, not a change to transmission.');
        const Forward=Section(Panes.response,'Facing the Sun','FORWARD WEIGHT / cos⁸','fg-forward');
        Plot(Forward,640,260,()=>[S.FogScatter],X=>{X.line([45,195],[595,195],'#a8c8be33');for(let I=0;I<61;I++){const Angle=(-90+I*3)*Math.PI/180,V=FogForwardWeight(Angle,S.FogScatter);X.rect(45+I*9,195-V*150,5,V*150,'#cfdab29c',2);}X.text(45,235,'−90°','#8ba396',20,'left');X.text(320,235,'TOWARD SUN','#b8c8ad',20);X.text(595,235,'+90°','#8ba396',20,'right');},null,'Fog forward-scattering angular weight');
        Field(Forward,'FogScatter','Sun scatter','',.01);Note(Forward,'The exact directional weight is max(0, cos(angle))⁸ × Sun scatter. Scene lighting multiplies this contribution; it is not a radiance measurement and does not change extinction.');
    }
    return P;
}
