import {ResolveCourseHeight} from './CourseStructure.js';
import {SampleSurfaceDepositDepth} from './SurfaceDepositMesh.js';
export const SnowfallDefaults=Object.freeze({Rate:100,Height:6,Radius:3,FallSpeed:2.4,DepositDepth:.0015,WindCoupling:.06});
// Real scene emitter. Uses the existing settling pool; no per-flake scene objects or lifetime deletion.
export class SnowfallModel{
 constructor(Deposits,{Seed=8701,Height=ResolveCourseHeight,Event=()=>{}}={}){
  this.Deposits=Deposits;this.Pool=Deposits.Pool;this.Grid=Deposits.Grid;this.Height=Height;this.Event=Event;this.Settings={...SnowfallDefaults};this.Seed=Seed;this.Enabled=false;this.Clock=0;this.EmitFraction=0;this.Centre=[0,0];
  const N=this.Pool.Capacity;this.State=new Uint8Array(N);this.Generation=new Uint32Array(N);this.Phase=new Float32Array(N);this.Size=new Float32Array(N);this.Speed=new Float32Array(N);this.RetryAt=new Float64Array(N);this.Sprites=new Float32Array(N*8);this.Live=0;this.Falling=0;this.Pending=0;this.Spawned=0;this.Landed=0;this.SpawnedVolume=0;this.CancelledVolume=0;
 }
 Random(){let S=this.Seed|0;S^=S<<13;S^=S>>>17;S^=S<<5;this.Seed=S;return (S>>>0)/4294967296;}
 Start(Camera){const Eye=Camera?.Position??[-47,0,3],F=Camera?.Forward??[1,0,0],N=Math.hypot(F[0],F[1])||1,Distance=F[2]<-.3?Math.max(3,Math.min(12,(Eye[2]-this.Height(Eye[0],Eye[1]))*N/-F[2])):12,Side=Math.min(5,Distance*.42);this.Centre=[Eye[0]+F[0]/N*Distance-F[1]/N*Side,Eye[1]+F[1]/N*Distance+F[0]/N*Side];this.Enabled=true;this.EmitFraction=0;this.Event('snowfall_start',{centre:this.Centre.slice(),settings:{...this.Settings}});}
 Stop(){if(this.Enabled)this.Event('snowfall_stop',{falling:this.Falling,pending:this.Pending});this.Enabled=false;this.EmitFraction=0;}
 Owns(I){return this.State[I]!==0&&this.Pool.Live[I]===1&&this.Pool.Generation[I]===this.Generation[I];}
 Spawn(){
  const R=Math.sqrt(this.Random())*this.Settings.Radius,A=this.Random()*Math.PI*2,X=this.Centre[0]+Math.cos(A)*R,Y=this.Centre[1]+Math.sin(A)*R;
  const Ground=this.Height(X,Y),H=this.Pool.Allocate([X,Y,Ground+this.Settings.Height*(.8+this.Random()*.4)],this.Settings.DepositDepth*this.Grid.Edge**2);if(!H)return false;
  const I=H.Index;this.Generation[I]=H.Generation;this.State[I]=1;this.Phase[I]=this.Random()*Math.PI*2;this.Size[I]=.025+this.Random()*.025;this.Speed[I]=.8+this.Random()*.4;this.Spawned++;this.SpawnedVolume+=this.Pool.Data[I*4+3];return true;
 }
 Advance(Seconds,Wind=[0,0,0]){
  const DT=Math.max(0,Math.min(.1,Number.isFinite(Seconds)?Seconds:0));this.Clock+=DT;
  for(const [K,Lo,Hi]of [['Rate',1,300],['Height',1,15],['Radius',1,8],['FallSpeed',.5,6],['DepositDepth',.0001,.01],['WindCoupling',0,.3]])this.Settings[K]=Math.max(Lo,Math.min(Hi,Number.isFinite(this.Settings[K])?this.Settings[K]:SnowfallDefaults[K]));
  if(this.Enabled&&!this.Pending){this.EmitFraction=Math.min(this.Pool.Capacity,this.EmitFraction+this.Settings.Rate*DT);const Count=Math.floor(this.EmitFraction);this.EmitFraction-=Count;for(let J=0;J<Count;J++)if(!this.Spawn())break;}
  this.Live=0;this.Falling=0;this.Pending=0;
  for(let I=0;I<this.State.length;I++){
   if(!this.Owns(I)){this.State[I]=0;continue;}const O=I*4,Data=this.Pool.Data;
   if(this.State[I]===1){
    const Steps=Math.max(1,Math.ceil(DT/.025)),Step=DT/Steps;
    for(let J=0;J<Steps;J++){
     const Phase=this.Phase[I]+this.Clock*1.3,WX=Number.isFinite(Wind[0])?Wind[0]:0,WY=Number.isFinite(Wind[1])?Wind[1]:0;
     Data[O]+=(Math.max(-4,Math.min(4,WX*this.Settings.WindCoupling))+.16*Math.sin(Phase))*Step;Data[O+1]+=(Math.max(-4,Math.min(4,WY*this.Settings.WindCoupling))+.13*Math.cos(Phase*1.17))*Step;Data[O+2]-=this.Settings.FallSpeed*this.Speed[I]*Step;
     const Ground=this.Height(Data[O],Data[O+1]);
     if(Data[O+2]<=Ground+this.Grid.MaxDepth+.005){const Surface=Ground+SampleSurfaceDepositDepth(this.Grid,Data[O],Data[O+1]);if(Data[O+2]<=Surface+.004){Data[O+2]=Surface+.004;this.State[I]=2;this.RetryAt[I]=0;break;}}
    }
   }
   if(this.State[I]===2&&this.Clock>=this.RetryAt[I]){const R=this.Deposits.Settle({Index:I,Generation:this.Generation[I]});if(R.Released){this.State[I]=0;this.Landed++;continue;}this.RetryAt[I]=this.Clock+.5;}
   if(!this.Owns(I)){this.State[I]=0;continue;}
   if(this.State[I]===1)this.Falling++;else this.Pending++;
   const P=this.Live++*8;this.Sprites.set([Data[O],Data[O+1],Data[O+2],this.Size[I],this.Phase[I]+this.Clock*.6,1,0,0],P);
  }
 }
 ClearAir(){this.Stop();let Volume=0;for(let I=0;I<this.State.length;I++){if(this.Owns(I)){Volume+=this.Pool.Data[I*4+3];this.Pool.Cancel({Index:I,Generation:this.Generation[I]});}this.State[I]=0;}this.CancelledVolume+=Volume;this.Live=0;this.Falling=0;this.Pending=0;this.Event('snowfall_air_cleared',{volume_m3:Volume});}
}
