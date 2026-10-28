import {InspectorCategories} from './InspectorCategories.js';
import {El,Button,Card,Metric,Tiles,Select,Header,Diagram,Grid,Note,DragAngles} from './NativeEnvironmentControls.js';
import {ObservatorySlider} from './ObservatorySlider.js';
import {NativeMoonPreview,LunarSphere} from './NativeMoonPreview.js';
import {MoonBodies} from './CelestialAtlas.js';
import {EnvironmentDefaults} from './EnvironmentSettings.js';
export const LunarCycle = 29.53;
const Ranges={MoonPhase:[0,LunarCycle],MoonAzimuth:[-180,180],MoonElevation:[-90,90],MoonRoll:[-180,180],MoonPitch:[-180,180],MoonSpin:[-180,180],MoonSize:[.1,180],MoonBrightness:[0,5],MoonGlow:[0,3],MoonLightIntensity:[0,5],PlanetBrightness:[0,5]};
export function LunarPhaseData(Days){const Day=((Days%LunarCycle)+LunarCycle)%LunarCycle,Angle=Day/LunarCycle*Math.PI*2;return {Day,Fraction:.5-.5*Math.cos(Angle),Name:['New moon','Waxing crescent','First quarter','Waxing gibbous','Full moon','Waning gibbous','Last quarter','Waning crescent'][Math.round(Day/LunarCycle*8)%8]};}
export function SetMoonInspectorValue(M,K,V){if(!Ranges[K]||!Number.isFinite(V))return false;const [Min,Max]=Ranges[K];M[K]=Math.max(Min,Math.min(Max,V));if(['MoonPhase','MoonAzimuth','MoonElevation'].includes(K))M.MoonFollowSky=false;return true;}
export function MoonAngularRuler(Diameter){return Math.log1p(Math.max(.1,Math.min(180,Diameter)))/Math.log(181);}
export function MoonInspectorTargets(S){S.MoonInstances??=[];while(S.MoonInstances.length<3)S.MoonInstances.push({...EnvironmentDefaults,MoonInstances:undefined,MoonEnabled:false});return [S,...S.MoonInstances.slice(0,3)];}
// Moon returns to a preview-first card editor. The accepted Stars inspector and
// its shared primitives are not changed by this independent implementation.
export function BuildMoonInspectorInfographics(H,C,P,BuildPlanets){
    const S=H.Settings,Targets=MoonInspectorTargets(S),State={Slot:Math.max(0,Math.min(3,H.MoonInspectorSlot??0))},M=Targets[State.Slot];
    P.classList.add('moon-cards');
    let Alive=true,TextureRevision=0;
    C.Disposers.push(()=>Alive=false);
    const Ready=()=>{TextureRevision++;if(Alive)C.Synchronise();};
    const Preview=new NativeMoonPreview(Ready);
    const Scratch=document.createElement('canvas');Scratch.width=256;Scratch.height=256;
    const Context=Scratch.getContext('2d');
    const Surface=()=>{Preview.Draw({X:Context,text:()=>{},line:()=>{},circle:()=>{},path:()=>{}},M);return Preview.Canvas;};
    const SurfaceKey=()=>[M.MoonBody,M.MoonPhase,M.MoonRoll,M.MoonPitch,M.MoonSpin,M.MoonBrightness,TextureRevision];
    const Bind=F=>{F.Static=true;C.Bindings.push(F);C.Live.push(F);F();};
    const Change=(K,V,Target=M)=>{if(SetMoonInspectorValue(Target,K,V)){C.Notify(K,Target);C.Synchronise();}};
    const Field=(Parent,K,Label,Unit='',Step=.01,Target=M)=>ObservatorySlider(C,Parent,Target,K,Label,Ranges[K],(Key,V)=>Change(Key,V,Target),Unit,Step);
    function Plot(Parent,W,H,Key,Draw,Drag,Label,MaxHz=Infinity){
        const Canvas=Diagram(C,Parent,W,H,Draw,Drag,Label),Redraw=C.Live[C.Live.length-1];
        let Last,LastDraw=-Infinity;
        const Update=()=>{if(!Alive||Parent.closest?.('[hidden]'))return;const Next=JSON.stringify(Key()),Now=performance.now();if(Last===Next||Now-LastDraw<1000/MaxHz)return;Last=Next;LastDraw=Now;Redraw();};
        Update.Static=MaxHz===Infinity;
        C.Live[C.Live.length-1]=Update;C.Bindings[C.Bindings.indexOf(Redraw)]=Update;
        return Canvas;
    }
    Header(C,P,'Moons','Scene bodies · phase · surface light','moon');
    const Settings=Card(P,'Moon settings','NativeMoon');
    const Instance=Select(C,Settings,State,'Slot','Active moon instance',Targets.map((T,I)=>[I,`${I+1} · ${MoonBodies[T.MoonBody??0]?.Name??'Luna'}`]),I=>{H.MoonInspectorSlot=I;H.Shell.Select('moon');});
    Instance.setAttribute('aria-label','Active moon instance');
    Tiles(C,Settings,M,[['MoonEnabled','Visible','Sun'],['MoonFollowSky','Follow sky','NativeOrbit']]);
    const {Panes}=InspectorCategories(C,P,[['phase','Phase'],['placement','Placement'],['light','Light & size'],['bodies','Bodies']],'moon-cards');

    const Phase=Card(Panes.phase,'Lunar phase','NativeMoon');
    const Summary=El('div','mc-phase-summary'),Age=El('strong'),PhaseName=El('span'),Illumination=El('span');
    Summary.append(Age,PhaseName,Illumination);Phase.appendChild(Summary);
    Bind(()=>{Age.textContent=M.MoonPhase.toFixed(1)+' d';PhaseName.textContent=LunarPhaseData(M.MoonPhase).Name;Illumination.textContent=(LunarPhaseData(M.MoonPhase).Fraction*100).toFixed(1)+'% lit';});
    Plot(Phase,640,440,SurfaceKey,X=>{
        const Canvas=Surface();
        if(Canvas)X.X.drawImage(Canvas,135,10,370,370);else X.text(320,190,'Loading registered surface…','#96a8bf',22);
        const A=(M.MoonRoll??0)*Math.PI/180;
        X.line([320-Math.sin(A)*191,195-Math.cos(A)*191],[320-Math.sin(A)*204,195-Math.cos(A)*204],'#b7c9df88',1.5);
        X.text(320,424,MoonBodies[M.MoonBody??0]?.Name??'Luna','#acbdd3',23);
    },null,'Moon phase preview · original registered texture',4);
    Field(Phase,'MoonPhase','Phase age','days');
    const PhaseStops=El('div','mc-phase-stops');Phase.appendChild(PhaseStops);
    for(const [Label,Day]of [['New',0],['First',LunarCycle/4],['Full',LunarCycle/2],['Last',LunarCycle*3/4]]){
        const B=Button(PhaseStops,Label,()=>Change('MoonPhase',Day));B.setAttribute('aria-label',Label+' Moon phase');
        Bind(()=>B.setAttribute('aria-pressed',String(Math.abs(LunarPhaseData(M.MoonPhase).Day-Day)<.01)));
    }
    Note(Phase,'Original textured sphere · isolated phase preview. Editing phase switches off Follow sky.');

    const Catalogue=Card(Panes.bodies,'Moon catalogue','NativeMoon'),Strip=El('div','ne-catalogue');Catalogue.appendChild(Strip);
    Button(Catalogue,'‹',()=>Strip.scrollBy({left:-124,behavior:'smooth'}),'ne-catalogue-prev').setAttribute('aria-label','Previous Moon bodies');
    Button(Catalogue,'›',()=>Strip.scrollBy({left:124,behavior:'smooth'}),'ne-catalogue-next').setAttribute('aria-label','Next Moon bodies');
    MoonBodies.forEach((B,I)=>{
        const Item=Button(Strip,'',()=>{M.MoonBody=I;M.MoonSize=B.Size;C.Notify('MoonBody',M);C.Synchronise();},'ne-body');Item.setAttribute('aria-label','Choose '+B.Name);
        const Canvas=El('canvas');Canvas.width=176;Canvas.height=176;Item.append(Canvas,El('span','',B.Name));
        const Thumb=new NativeMoonPreview(Ready);
        Bind(()=>{if(Catalogue.closest?.('[hidden]'))return;Item.classList.toggle('selected',M.MoonBody===I);Item.setAttribute('aria-pressed',String(M.MoonBody===I));const X=Canvas.getContext('2d');if(!X?.beginPath)return;X.setTransform(176/256,0,0,176/256,0,0);X.clearRect(0,0,256,256);Thumb.Draw({X,text:()=>{},line:()=>{}},{...M,MoonBody:I,MoonPhase:14.765,MoonBrightness:1,MoonRoll:0,MoonPitch:0,MoonSpin:0});});
    });

    const LightRow=Grid(Panes.light,'two');
    const Light=Card(LightRow,'Moonlight','NativeMoon');
    Metric(C,Light,()=>M.MoonBrightness.toFixed(2),'Disc brightness multiplier · not calibrated lux','×');
    Plot(Light,256,215,()=>[...SurfaceKey(),M.MoonGlow],X=>{X.glow(128,104,82,'168,188,219',.075*M.MoonGlow);Preview.Draw(X,M);},null,'Moonlight preview · textured disc',4);
    Field(Light,'MoonBrightness','Disc brightness','×');Field(Light,'MoonGlow','Lunar glow','×');
    if(State.Slot===0){Tiles(C,Light,S,[['MoonLightEnabled','Illuminate scene','Sun']]);Field(Light,'MoonLightIntensity','Moonlight power','×',.01,S);Note(Light,'Primary Moon supplies the existing directional scene light. Phase, horizon and atmosphere attenuate it; artistic size does not increase its power.');}
    else Note(Light,'Visual body only. Scene lighting comes from the primary Moon.');
    const Size=Card(LightRow,'Moon size','NativeMoon');
    Metric(C,Size,()=>M.MoonSize.toFixed(2),'Angular diameter · compressed visual comparison','°');
    Plot(Size,256,215,()=>[...SurfaceKey(),M.MoonSize],X=>Preview.Draw(X,M,true),null,'Moon size comparison · original textured sphere',4);
    Field(Size,'MoonSize','Angular diameter','°');Button(Size,'Earth Moon reference / 0.52°',()=>Change('MoonSize',.52));

    const Placement=Grid(Panes.placement,'two'),Position=Card(Placement,'Lunar position','NativeMoon');
    Note(Position,'Drag the celestial globe to place this body.');
    const Angles=DragAngles(M,'MoonAzimuth','MoonElevation');
    Plot(Position,360,257,()=>[M.MoonAzimuth,M.MoonElevation,...SurfaceKey()],X=>{Surface();LunarSphere(X,M,Preview);},E=>{M.MoonFollowSky=false;Angles(E);C.Notify('MoonAzimuth',M);C.Synchronise();},'Lunar position globe · drag or use arrow keys',4);
    Field(Position,'MoonAzimuth','Moon azimuth','°',.1);Field(Position,'MoonElevation','Moon elevation','°',.1);
    const Rotation=Card(Placement,'Moon rotation','NativeMoon');
    Note(Rotation,'Surface orientation is independent of sky position.');
    Plot(Rotation,256,215,SurfaceKey,X=>Preview.Draw(X,M,false,true),E=>{
        if(E.Key){if(E.Key==='Home'){M.MoonRoll=0;Change('MoonPitch',0);}else if(['ArrowLeft','ArrowRight'].includes(E.Key))Change('MoonRoll',M.MoonRoll+(E.Key==='ArrowLeft'?-1:1)*E.Step);else Change('MoonPitch',M.MoonPitch+(E.Key==='ArrowUp'?1:-1)*E.Step);}
        else {SetMoonInspectorValue(M,'MoonRoll',M.MoonRoll+E.DX*.8);Change('MoonPitch',M.MoonPitch-E.DY*.8);}
    },'Moon rotation preview · drag or use arrow keys',4);
    Field(Rotation,'MoonRoll','Surface roll','°',.1);Field(Rotation,'MoonPitch','Surface pitch','°',.1);Field(Rotation,'MoonSpin','Surface spin','°',.1);
    Button(Rotation,'Reset surface frame',()=>{M.MoonRoll=M.MoonPitch=M.MoonSpin=0;C.Notify('MoonRoll',M);C.Synchronise();});
    const Neighbours=El('details','mc-neighbours');Neighbours.appendChild(El('summary','','Bright planets / orbital placement'));Panes.bodies.appendChild(Neighbours);BuildPlanets(Neighbours);
    return P;
}
