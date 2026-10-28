import {FindLightningCloudAnchor} from './LightningCloudAnchor.js';
import {makeBolt,mulberry32} from '../../ParticleEditor/js/lightning.js';
import {LightningDefaults,LightningEnvelope} from './LightningSettings.js';
export class LightningStorm{
 constructor({Seed=20261021,Event=()=>{},Audio=null}={}){this.Random=mulberry32(Seed);this.Event=Event;this.Audio=Audio;this.Clock=0;this.Next=Infinity;this.Strikes=0;this.LastStrike=-Infinity;this.Light=new Float32Array(12);this.Bolts=[];this.Pending=false;}
 Request(){this.Pending=true;}
 Clear(){this.Bolts=[];this.Light.fill(0);this.SceneLight=null;this.Pending=false;this.Next=Infinity;this.Audio?.Stop();}
 Advance(Delta,Settings,Camera,CloudUniform){
  const S={...LightningDefaults,...Settings};this.Clock+=Math.max(0,Math.min(.1,Number.isFinite(Delta)?Delta:0));
  if(!S.LightningEnabled){this.WasAuto=false;this.Clear();return;}
  if(S.LightningAuto&&(!this.WasAuto||!Number.isFinite(this.Next)))this.Next=this.Clock+Math.max(2,S.LightningIntervalMin);
  this.WasAuto=S.LightningAuto;
  if(this.Pending||S.LightningAuto&&this.Clock>=this.Next){this.Pending=false;if(this.Clock-this.LastStrike>=.5)this.Strike(S,Camera,CloudUniform);}
  this.Bolts=this.Bolts.filter(B=>this.Clock-B.Start<.36);this.Light.fill(0);this.SceneLight=null;
  for(const B of this.Bolts){B.Energy=LightningEnvelope(this.Clock-B.Start)*S.LightningBrightness;
   if(B.Energy>this.Light[3]){this.Light.set([...B.Source,B.Energy*12*S.LightningCloudGlow,.72,.82,1,S.LightningRadius,...B.CloudEnd,1]);
    this.SceneLight={Type:0,Position:B.Source,Direction:[0,0,-1],Colour:[.72,.82,1],Intensity:1.5e7*B.Energy,Reach:Math.max(4000,S.LightningRadius*2),Params:[1,0,0,1]};}
  }
 }
 Strike(S,Camera,CloudUniform){
  const R=this.Random,Eye=Camera.Position,Forward=Camera.Forward??[1,0,0],H=Math.hypot(Forward[0],Forward[1]);const F=H>.001?[Forward[0]/H,Forward[1]/H]:[1,0];
  const J=S.LightningAuto?(R()-.5)*.7:0,DX=F[0]*Math.cos(J)-F[1]*Math.sin(J),DY=F[0]*Math.sin(J)+F[1]*Math.cos(J);
  const X=Eye[0]+DX*S.LightningDistance,Y=Eye[1]+DY*S.LightningDistance,Base=Math.max(100,S.CloudBase??1500),Top=Math.min(S.CloudCeiling??14000,Base+Math.max(100,S.CloudThickness??1200));
  const Anchor=FindLightningCloudAnchor(S,[X,Y,Base+(Top-Base)*.35],CloudUniform);
  const Source=Anchor.Position;const Intra=S.LightningMode===1;const Local=Anchor.Medium==='local';
  const SX=Source[0],SY=Source[1],Jitter=Local?Math.min(150,Math.min(S.LocalCloudHalfX??300,S.LocalCloudHalfY??300)*.3):150;
  const Start=[SX+(R()-.5)*Jitter,SY+(R()-.5)*Jitter,Source[2]];
  // A weather-scale horizontal channel, not a tiny 500 m patch around one point light.
  const Angle=Math.atan2(DY,DX)+(R()<.5?-1:1)*(.3+R()*.2)*Math.PI,Spread=Math.max(400,Math.min(10000,S.LightningSpread??4200))*(.75+R()*.5);
  let CloudEnd;
  if(Local){
   const CX=S.LocalCloudX??SX,CY=S.LocalCloudY??SY,CZ=S.LocalCloudZ??Source[2],HX=S.LocalCloudHalfX??300,HY=S.LocalCloudHalfY??300,HZ=S.LocalCloudHalfZ??200;
   let AX=Math.cos(Angle),AY=Math.sin(Angle);if((SX-CX)*AX+(SY-CY)*AY>0){AX=-AX;AY=-AY;}
   CloudEnd=[CX+AX*HX*.85,CY+AY*HY*.85,Math.max(CZ-HZ*.75,Math.min(CZ+HZ*.75,Source[2]+(R()-.5)*HZ*.5))];
  }else CloudEnd=[SX+Math.cos(Angle)*Spread,SY+Math.sin(Angle)*Spread,Math.max(Base+(Top-Base)*.12,Math.min(Top-(Top-Base)*.12,Source[2]+(R()-.5)*(Top-Base)*.3))];
  const End=Intra?CloudEnd:[SX,SY,0],ChannelLength=Math.hypot(...CloudEnd.map((V,I)=>V-Source[I]));
  // Editor is Y-up; scene is Z-up. The discharge geometry and cloud-light capsule share endpoints.
  const ToEditor=P=>[P[0],P[2],P[1]],ToWorld=P=>[P[0],P[2],P[1]],Scale=Math.max(200,Intra?ChannelLength*.22:Start[2]-End[2]);
  const Build=(A,B,Budget,Width=1)=>makeBolt(R,ToEditor(A),ToEditor(B),{amp:S.LightningRoughness/15*Scale,levels:Math.round(S.LightningDetail),width:S.LightningWidth/15*Scale*Width,branchChance:S.LightningBranches,maxSegments:Budget}).map(Q=>({a:ToWorld(Q.a),b:ToWorld(Q.b),w:Q.w}));
  const Segments=Build(Start,End,Intra?1024:640);
  if(!Intra)Segments.push(...Build(Start,CloudEnd,384,.65));
  this.Bolts=[{Segments,Start:this.Clock,Source,CloudEnd,Anchor,Energy:0}];this.LastStrike=this.Clock;this.Strikes++;this.Next=this.Clock+Math.max(2,S.LightningIntervalMin)+R()*Math.max(0,S.LightningIntervalMax-S.LightningIntervalMin);
  const SoundPoint=Intra?Source:End,Distance=Math.hypot(...SoundPoint.map((V,I)=>V-Eye[I]));const Pan=(-F[1]*(SoundPoint[0]-Eye[0])+F[0]*(SoundPoint[1]-Eye[1]))/Math.max(1,Distance);
  const Sound=this.Audio?.Strike(Distance,Pan)??false;this.Event('lightning_strike',{strike:this.Strikes,mode:Intra?'in_cloud':'cloud_to_ground',position:Source,cloud_medium:Anchor.Medium,cloud_channel_end:CloudEnd,cloud_channel_length_m:ChannelLength,cloud_density_at_source:Anchor.Density,cloud_source_embedded:Anchor.Embedded,segments:Segments.length,listener_distance_m:Distance,thunder_delay_s:Distance/343,audio_scheduled:Sound});
 }
}
