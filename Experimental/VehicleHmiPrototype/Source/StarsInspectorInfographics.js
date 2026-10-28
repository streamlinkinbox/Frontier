import {El, Button} from './NativeEnvironmentControls.js';
import {CelestialInspectorUI} from './CelestialInspectorUI.js';
const Ranges = {StarLimit:[0,8], StarBrightness:[0,5], StarSize:[.2,3], StarHalo:[0,2], StarRotation:[0,360], TwinkleDepth:[0,1], TwinkleRate:[.2,3], MilkyWay:[0,3]};
export function SetStarsInspectorValue(S, K, V) {
    if (!Ranges[K] || !Number.isFinite(V)) return false;
    const [Min, Max] = Ranges[K];
    S[K] = K === 'StarRotation' ? ((V % 360) + 360) % 360 : Math.max(Min, Math.min(Max, V));
    return true;
}
export function StarChartPoint(Record, Rotation = 0) {
    const Radius = Math.sqrt((1 - Math.max(-1, Math.min(1, Record[2]))) / 2);
    const A = Math.atan2(Record[1], Record[0]) - Rotation * Math.PI / 180;
    return [Math.cos(A) * Radius, -Math.sin(A) * Radius];
}
export function StarsCatalogueData(Records, Limit) {
    const Bins = Array(10).fill(0), EligibleBins = Array(10).fill(0), Threshold = 10 ** (-.4 * Limit);
    let Eligible = 0;
    for (const R of Records) {
        const Magnitude = -2.5 * Math.log10(Math.max(1e-30, R[3]));
        const Bin = Math.max(0, Math.min(9, Math.floor(Magnitude + 2)));
        Bins[Bin]++;
        if (R[3] >= Threshold) {Eligible++;EligibleBins[Bin]++;}
    }
    return {Bins, EligibleBins, Eligible, Total:Records.length};
}
export function StarScintillation(Time, S, Record) {
    const Phase = Record[0] * 91.7 + Record[1] * 213.1 + Record[2] * 57.3;
    const T = Time * S.TwinkleRate;
    const Signal = .6 * Math.sin(T * 6.283185 + Phase) + .28 * Math.sin(T * 11.71 + Phase * 1.7) + .12 * Math.sin(T * 19.37 + Phase * 2.3);
    return 1 + (S.TwinkleEnabled ? S.TwinkleDepth : 0) * .35 * Signal;
}
export function BuildStarsInspectorInfographics(H, C, P) {
    const S = H.Settings, Records = H.StarCatalogue?.Records ?? [];
    const UI = CelestialInspectorUI(C, P, {Title:'Stars', Subtitle:'A catalogue of light. Not a field of noise.', Theme:'stars-infographics', Tabs:[['field','Star map'],['population','Population'],['signal','Scintillation']]});
    const {Panes, Bind, Section, Note, Metric, Toggle, Field, Plot} = UI;
    const Change = (K,V) => {if (SetStarsInspectorValue(S,K,V)) {C.Notify(K,S);C.Synchronise();}};
    const Input = (Parent,K,Label,Unit='',Step=.01,Scale=1) => Field(Parent,S,K,Label,Ranges[K],Change,Unit,Step,Scale);
    let LastLimit, Cached;
    const Data = () => {if (LastLimit !== S.StarLimit) {LastLimit=S.StarLimit;Cached=StarsCatalogueData(Records,S.StarLimit);}return Cached;};
    const Atlas = Section(Panes.field, 'Celestial atlas', 'EQUAL-AREA / ALL SKY', 'st-atlas');
    const Count = El('div','st-count');Atlas.appendChild(Count);
    Metric(Count,'Catalogue entries',()=>Data().Eligible.toLocaleString(),'meet magnitude cutoff');
    Plot(Atlas,640,580,()=>[S.StarLimit,S.StarRotation,S.StarSize,S.StarBrightness,Records.length],X=>{
        const Centre=[320,290],Radius=228;
        for(const Dec of [-60,-30,0,30,60])X.circle(...Centre,Math.sqrt((1-Math.sin(Dec*Math.PI/180))/2)*Radius,null,Dec===0?'#b8c6e04a':'#b8c6e019');
        for(let I=0;I<12;I++){const A=I*Math.PI/6-S.StarRotation*Math.PI/180;X.line(Centre,[320+Math.cos(A)*Radius,290+Math.sin(A)*Radius],'#b8c6e018',1,[2,6]);}
        X.circle(...Centre,Radius,null,'#c4cce34a');
        const Threshold=10**(-.4*S.StarLimit);
        for(const R of Records){if(R[3]<Threshold)continue;const Q=StarChartPoint(R,S.StarRotation),Colour=R.slice(4,7).map(V=>Math.round(Math.min(1,Math.max(0,V))**(1/2.2)*255));X.circle(320+Q[0]*Radius,290+Q[1]*Radius,Math.max(.55,Math.min(3.2,.65+Math.sqrt(R[3])*.65))*S.StarSize,`rgba(${Colour.join(',')},${Math.min(1,(.25+Math.sqrt(R[3])*.35)*S.StarBrightness)})`);}
        X.circle(...Centre,5,null,'#d3dced99');X.line([306,290],[334,290],'#d3dced66');X.line([320,276],[320,304],'#d3dced66');
        for(const [A,Text]of [[0,'0h'],[Math.PI/2,'6h'],[Math.PI,'12h'],[Math.PI*1.5,'18h']])X.text(320+Math.cos(A-S.StarRotation*Math.PI/180)*258,290-Math.sin(A-S.StarRotation*Math.PI/180)*258,Text,'#9ca9c1',20);
        X.text(28,556,'NORTH POLE / CENTRE','#8695af',18,'left');X.text(612,556,'DRAG TO ROTATE','#8695af',18,'right');
    },E=>{if(E.Key)Change('StarRotation',E.Key==='Home'?0:S.StarRotation+(E.Key==='ArrowLeft'?-1:1)*E.Step);else Change('StarRotation',Math.atan2(290-E.Point[1],E.Point[0]-320)*180/Math.PI);},'Catalogue planisphere · drag or use arrow keys to rotate');
    Note(Atlas,'Actual catalogue directions and colours. Equal-area projection about the north celestial pole; not your local horizon or a lit camera view.');
    const Switches=El('div','ci-switches');Panes.field.appendChild(Switches);Toggle(Switches,S,'StarsEnabled','Star field');
    const Visibility=El('p','ci-notice');Panes.field.appendChild(Visibility);
    Bind(()=>Visibility.textContent=!S.StarsEnabled?'Star renderer disabled · atlas remains a catalogue reference.':S.SunEnabled!==false&&S.SunElevation>=-3?'Daylight masks the field. Preview a clear night to inspect it.':S.CloudEnabled?'Night sky · clouds can obscure catalogue stars.':'Clear night configuration · look above the horizon.');
    Button(Panes.field,'Preview clear night  ↗',()=>{S.SunAutoPosition=false;S.DayAnimate=false;S.SunElevation=-18;S.CloudEnabled=false;S.LocalCloudEnabled=false;S.LocalFogEnabled=false;S.StarsEnabled=true;C.Notify('SunElevation',S);C.Synchronise();},'ci-action');
    Note(Panes.field,'Night preview lowers the Sun to −18° and disables global/local clouds and local fog. Moonlight is retained.');
    Input(Panes.field,'StarRotation','Catalogue rotation','°',1);

    const Population=Section(Panes.population,'Magnitude census','ACTUAL CATALOGUE COUNTS','st-census');
    const Summary=El('div','st-summary');Population.appendChild(Summary);Metric(Summary,'Eligible',()=>Data().Eligible.toLocaleString());Metric(Summary,'Catalogue total',()=>Records.length.toLocaleString());
    Plot(Population,640,320,()=>[S.StarLimit,Records.length],X=>{
        const Bins=Data().Bins,Peak=Math.max(1,...Bins),Cut=40+(S.StarLimit+2)/10*560;
        for(let I=0;I<10;I++){const Height=Bins[I]/Peak*180,Left=45+I*56;X.rect(Left,230-Height,39,Height,'#b3c6e332',4);const EligibleHeight=Data().EligibleBins[I]/Peak*180;X.rect(Left,230-EligibleHeight,39,EligibleHeight,'#b3c6e3aa',4);X.text(Left+19,260,String(I-2),'#8e9cb5',20);}
        X.line([Cut,30],[Cut,240],'#ebd8ab',2,[3,6]);X.circle(Cut,30,7,'#ebd8ab');X.text(40,303,'BRIGHTER','#8e9cb5',20,'left');X.text(600,303,'FAINTER / mag','#8e9cb5',20,'right');
    },E=>Change('StarLimit',E.Key?E.Key==='Home'?0:S.StarLimit+(E.Key==='ArrowLeft'?-.1:.1)*E.Step:Math.round(((E.Point[0]-40)/560*10-2)*10)/10),'Magnitude cutoff editor · drag or use arrow keys');
    Input(Population,'StarLimit','Limiting magnitude','mag',.1);Note(Population,'Muted bars: total; bright bars: eligible. One-magnitude bins, clipped to −2…8. Eligible is the flux cutoff count, not visible screen pixels; daylight, horizon and clouds are excluded.');
    const Scale=Section(Panes.population,'Point spread','RENDERER CONTROLS','st-point-spread');
    Input(Scale,'StarBrightness','Star brightness','×');Input(Scale,'StarSize','Point size','×');Input(Scale,'StarHalo','Star halo','×');Input(Scale,'MilkyWay','Milky Way','×');

    const Signal=Section(Panes.signal,'One star, four seconds','SHADER-MATCHED REFERENCE','st-signal');
    Toggle(Signal,S,'TwinkleEnabled','Scintillation');
    const Sample=Records.reduce((A,B)=>B[3]>A[3]?B:A,Records[0]??[1,0,0,1,1,1,1]);
    Plot(Signal,640,330,()=>[S.TwinkleEnabled,S.TwinkleDepth,S.TwinkleRate,Math.floor(performance.now()/125)],X=>{
        const T=performance.now()/1000;
        for(const V of [.7,1,1.3]){const Y=165-(V-1)*300;X.line([65,Y],[606,Y],'#b9b9d827',1,[2,6]);X.text(50,Y+5,V.toFixed(1),'#9497ad',20,'right');}
        const Points=Array.from({length:161},(_,I)=>[65+I/160*541,165-(StarScintillation(T-4+I/40,S,Sample)-1)*300]);
        X.path(Points,'#d7c4e5',2.4);X.circle(606,Points[160][1],6,'#ebd8f4');X.text(65,300,'−4 s','#9497ad',20,'left');X.text(606,300,'NOW','#9497ad',20,'right');
    },null,'Four-second reference scintillation trace',true);
    Input(Signal,'TwinkleDepth','Twinkle depth','%',1,100);Input(Signal,'TwinkleRate','Twinkle rate','Hz',.1);
    Note(Signal,'Exact three-frequency shader signal for the brightest catalogue entry, sampled at 8 Hz using the UI clock. Relative gain only; not measured GPU brightness or a live frame capture.');
    return UI.Root;
}
