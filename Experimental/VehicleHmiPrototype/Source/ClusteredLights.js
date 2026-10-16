import { ResolveLevelPlacements } from './LevelStructure.js';
import { ResolveCoMAuthoredZ } from './VehicleSpecification.js';
import { RoofLightMount } from './LightingFixtures.js';
import { TransformPoint, TransformDirection, NormalizeVector } from './TransformProjection.js';
export const LocalLightCapacity=32, ClusterCount=256;
// Frame suffix: origin vec4, metadata vec4, 32 lights × 4 vec4, 256 masks × vec4, 64 IES vec4.
export const LocalLightingFloats=8+LocalLightCapacity*16+ClusterCount*4+256;
export const LightModes=['Off','Low','High','Fog'];
export const LightDefaults=Object.freeze({ LocalLights:true, StreetLights:true, LightMode:'Off', LightIntensity:1,
    FrontLights:true,RoofLights:true,RoofPower:0.65,LowCandela:24000,HighCandela:90000,FogCandela:12000,BeamPitch:2,BeamWidth:1,RoofProfile:'Automotive',StreetPower:1 });
export function BuildLightClusters(Lights, Origin) {
    const Masks=new Uint32Array(ClusterCount*4);
    for(let Z=0;Z<4;Z++)for(let Y=0;Y<8;Y++)for(let X=0;X<8;X++) {
        const Low=[Origin[0]+X*16,Origin[1]+Y*16,Origin[2]+Z*8];let Mask=0;
        Lights.forEach((L,I)=>{
            const D=L.Position.reduce((S,P,A)=>S+Math.max(Low[A]-P,0,P-Low[A]-(A===2?8:16))**2,0);
            if(D<L.Reach**2) Mask=(Mask|(1<<I))>>>0;
        });
        Masks[(X+8*(Y+8*Z))*4]=Mask;
    }
    return Masks;
}
export class ClusteredLights {
    constructor(){this.IES=null;this.Metrics={Count:0,Clusters:0,References:0};this.Static=ResolveLevelPlacements().filter(P=>['PoleHead','TunnelLamp'].includes(P.Shape));}
    Build(Vehicle,S,Camera) {
        const Lights=[];
        const Add=L=>{if(Lights.length<LocalLightCapacity)Lights.push(L);};
        if(S.LocalLights && S.LightMode!=='Off' && !Vehicle.Hidden) {
            const Mode={Low:2,High:3,Fog:4}[S.LightMode]||2;
            const Pitch=(S.BeamPitch+(Mode===3?-1.5:Mode===4?3:0))*Math.PI/180;
            const Sources=[];
            if(S.RoofLights)for(const Y of [0])Sources.push({Position:[0.38,Y,RoofLightMount[2]],Roof:true});
            if(S.FrontLights)for(const Y of [-0.96,0.96])Sources.push({Position:[3.16,Y,0.54-ResolveCoMAuthoredZ()],Roof:false});
            for(const Source of Sources) Add({ Position:TransformPoint(Vehicle.QueryBodyTransform(),Source.Position),
                Direction:NormalizeVector(TransformDirection(Vehicle.QueryBodyTransform(),[Math.cos(Pitch),0,-Math.sin(Pitch)])),
                Up:NormalizeVector(TransformDirection(Vehicle.QueryBodyTransform(),[Math.sin(Pitch),0,Math.cos(Pitch)])),
                Type:Source.Roof ? (S.RoofProfile==='IES'&&this.IES?6:S.RoofProfile==='Spot'?1:S.RoofProfile==='Point'?0:Mode) : Mode,Reach:Mode===3?110:Mode===4?35:70,
                Colour:(Mode===4?[1,0.72,0.35]:[0.88,0.94,1]).map(C=>C*S.LightIntensity*(Source.Roof?S.RoofPower:1)*S[`${S.LightMode}Candela`]*0.02),
                Outer:Math.cos((Mode===3?18:35)*S.BeamWidth*Math.PI/180),Inner:Math.cos(10*S.BeamWidth*Math.PI/180),Width:S.BeamWidth });
        }
        if(S.LocalLights&&S.StreetLights)for(const P of this.Static) {
            const Tunnel=P.Shape==='TunnelLamp';
            Add({ Position:[P.Position[0],P.Position[1],P.Position[2]-(Tunnel?0.08:0.04)],Direction:[0,0,-1],Up:[Math.cos(P.Yaw),Math.sin(P.Yaw),0],
                Type:Tunnel?0:5,Reach:Tunnel?12:23,Colour:(Tunnel?[0.82,0.9,1]:[1,0.82,0.62]).map(C=>C*(Tunnel?38:95)*S.StreetPower),Outer:0,Inner:1,Width:1 });
        }
        const Origin=[Math.floor(Camera[0]/16)*16-64,Math.floor(Camera[1]/16)*16-64,Math.floor(Camera[2]/8)*8-8];
        const Data=new Float32Array(LocalLightingFloats),U=new Uint32Array(Data.buffer);
        Data.set([...Origin,16]);Data.set([Lights.length,8,8,4],4);
        Lights.forEach((L,I)=>Data.set([...L.Position,L.Reach,...L.Direction,L.Type,...L.Colour,L.Outer,...L.Up,L.Inner],8+I*16));
        const Masks=BuildLightClusters(Lights,Origin);U.set(Masks,8+32*16);
        Data.set(this.IES?.Table||new Float32Array(256),8+32*16+256*4);
        this.Lights=Lights;this.Origin=Origin;
        this.Metrics={Count:Lights.length,Clusters:Array.from(Masks).filter(Boolean).length,References:Array.from(Masks).reduce((N,X)=>{while(X){N++;X=(X&(X-1))>>>0;}return N;},0)};
        return Data;
    }
}
