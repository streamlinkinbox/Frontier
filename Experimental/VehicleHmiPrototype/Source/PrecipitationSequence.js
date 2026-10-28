import {CelestialTypes,CelestialBudgets,CelestialEmissionRate,CelestialTypeIndex} from '../../Precipitation/CelestialReference.js';
import {PrecipitationDefaults} from '../../Precipitation/Component.js';
import {InvertTransform} from './TransformProjection.js';
import {BakeVehicleSnowReceiver,PrecipitationLimits as L} from './PrecipitationReceivers.js';
export class PrecipitationSequence{
 constructor(H){this.Host=H;this.Exchange=H.Exchange;this.Settings={...PrecipitationDefaults,Budget:25000,CloudSource:0,Restitution:.35,RestTime:1.6,Opacity:.85,Streak:.7,SpawnFromClouds:true};this.Region=[-104,-64,L.GroundEdge,L.GroundCells];this.Data=new Float32Array(96);this.Counts=new Uint32Array(16);this.Tick=0;this.HighWater=0;this.Active=false;this.Resident=false;this.Warm=false;this.ClearRequested=false;this.ReadPending=false;this.Epoch=0;this.ActivityRevision=0;this.LastRead=-Infinity;}
 get Live(){return this.Active?L.Capacity:0;}
 async ConstructPrograms(Frame){
  const H=this.Host,E=this.Exchange,S=H.Shaders,U=GPUBufferUsage;
  this.Params=E.ConstructRecords(384,U.UNIFORM|U.COPY_DST,'Precipitation component');
  this.Flakes=E.ConstructRecords(L.Capacity*64,U.STORAGE|U.VERTEX|U.COPY_DST|U.COPY_SRC,'GPU precipitation slots');
  this.Deposits=E.ConstructRecords((L.GroundCells**2+L.CarCells**2)*4,U.STORAGE|U.COPY_DST|U.COPY_SRC,'Persistent terrain + vehicle snow volume');
  this.RoofData=BakeVehicleSnowReceiver(H.Scene.FacetSets[H.Scene.VehicleBodySet].Facets,L.CarCells,L.CarEdge);
  this.Roof=E.ConstructRecordsFrom(this.RoofData,U.STORAGE,'Actual vehicle upper-surface receiver');
  this.Stats=E.ConstructRecords(64,U.STORAGE|U.COPY_DST|U.COPY_SRC,'GPU precipitation counters');this.Readback=E.ConstructRecords(64,U.MAP_READ|U.COPY_DST,'Precipitation counters readback');
  this.Grains=E.ConstructRecords(65536*32,U.STORAGE|U.COPY_DST|U.COPY_SRC,'Stopped precipitation opaque mesh grains');
  this.MeshDraw=E.ConstructRecordsFrom(new Uint32Array([24,0,0,0]),U.STORAGE|U.INDIRECT|U.COPY_SRC,'Settled mesh indirect draw');
  this.Layout=E.ConstructDescriptorLayout(['Uniform','Texture3D','Sampler','Uniform','Storage','Storage','StorageRead','Storage','Storage','Storage'],GPUShaderStage.COMPUTE,'GPU precipitation');
  this.Group=E.ConstructDescriptorGroup(this.Layout,[H.Clouds.Params,H.Clouds.Shape.createView(),H.Clouds.Sampler,this.Params,this.Flakes,this.Deposits,this.Roof,this.Stats,this.Grains,this.MeshDraw]);
  this.Compute=await S.ConstructComputeProgram('PrecipitationCompute.wgsl','AdvancePrecipitation',[this.Layout]);
  this.SurfaceLayout=E.ConstructDescriptorLayout(['Uniform','Uniform','StorageRead'],GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,'Snow mounds');this.SurfaceGroup=E.ConstructDescriptorGroup(this.SurfaceLayout,[Frame,this.Params,this.Grains]);
  this.Surface=await S.ConstructRasterProgram('PrecipitationSurface.wgsl',{VertexEntry:'DepositVertex',FragmentEntry:'DepositFragment',Primitive:{topology:'triangle-list',cullMode:'none'},DepthStencil:{format:'depth32float',depthWriteEnabled:true,depthCompare:'greater'},Targets:[{format:'rgba16float'}]},[this.SurfaceLayout]);
  this.SpriteLayout=E.ConstructDescriptorLayout(['Uniform','Uniform'],GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,'GPU precipitation sprites');this.SpriteGroup=E.ConstructDescriptorGroup(this.SpriteLayout,[Frame,this.Params]);
  this.Sprite=await S.ConstructRasterProgram('PrecipitationRaster.wgsl',{VertexEntry:'SnowVertex',FragmentEntry:'SnowFragment',VertexLayouts:[{arrayStride:64,stepMode:'instance',attributes:[{shaderLocation:0,offset:0,format:'float32x4'},{shaderLocation:1,offset:16,format:'float32x4'},{shaderLocation:2,offset:32,format:'float32x4'}]}],Primitive:{topology:'triangle-list',cullMode:'none'},DepthStencil:{format:'depth32float',depthWriteEnabled:false,depthCompare:'greater'},Targets:[{format:'rgba16float',blend:{color:{srcFactor:'one',dstFactor:'one',operation:'add'},alpha:{srcFactor:'one',dstFactor:'one',operation:'max'}}}]},[this.SpriteLayout]);
 }
 Start(){this.ActivityRevision++;this.HighWater=Math.max(this.HighWater,Math.min(L.Capacity,this.Settings.Budget));if(!this.Resident){const P=this.Host.Vehicle.Position;this.Region=[Math.floor(P[0])-64,Math.floor(P[1])-64,1,128];}this.Settings.Enabled=true;this.Active=true;this.Resident=true;this.Host.Telemetry?.Event('precipitation_start',{region:this.Region,settings:{...this.Settings}});}
 Stop(){this.ActivityRevision++;this.Settings.Enabled=false;this.Host.Telemetry?.Event('precipitation_stop');}
 Clear(){this.ActivityRevision++;this.Epoch++;this.HighWater=0;this.PreviousInverse=null;this.Settings.Enabled=false;this.ClearRequested=true;this.Resident=false;this.Active=false;this.Warm=false;this.Counts.fill(0);this.Host.Telemetry?.Event('precipitation_clear');}
 Prepare(Delta){
  if(globalThis.document?.hidden)Delta=0;
  const H=this.Host,S=this.Settings,W=H.Clouds.Wind;S.Budget=Math.max(1024,Math.min(L.Capacity,Math.round(S.Budget)||25000));S.Category=CelestialTypeIndex(S.Category);S.Restitution=Math.max(0,Math.min(1,S.Restitution));S.RestTime=Math.max(.1,Math.min(10,S.RestTime));
  const Type=CelestialTypes[S.Category],Camera=H.CameraState??{Position:H.Vehicle.Position,Forward:[1,0,0]},Eye=Camera.Position,F=Camera.Forward??[1,0,0],FL=Math.hypot(F[0],F[1])||1;
  const Ceiling=H.Settings.CloudBase+H.Settings.CloudThickness*.5,Fade=1-Math.min(1,Math.max(0,(Eye[2]-(Ceiling-150))/150));this.EmissionRate=CelestialEmissionRate(S,Fade);
  const Top=Math.min(Ceiling,S.SpawnFromClouds?Math.min(Eye[2]+400,Math.max(Eye[2]+30,H.Settings.CloudBase)):Eye[2]+60);
  if(S.Enabled)this.HighWater=Math.max(this.HighWater,S.Budget);const R=this.Data;R.fill(0);const B=H.Vehicle.QueryBodyTransform(),Inverse=InvertTransform(B);R.set(B);R.set(Inverse,16);R.set(this.PreviousInverse??Inverse,60);this.PreviousInverse=Inverse;R.set(this.Region,32);R.set([-4,-4,L.CarEdge,L.CarCells],36);
  R.set([Math.max(0,Math.min(.1,Delta||0)),+(S.Enabled&&S.RateMillimetresPerHour>0),S.Budget,Math.min(1,this.EmissionRate/S.Budget*Math.max(0,Math.min(.1,Delta||0)))],40);R.set([Type.vt,0,L.MaxDepth,++this.Tick%1000000],44);R.set([W.Velocity[0],W.Velocity[1],H.Settings.WindShear??.15,H.Settings.WindVeer??8],48);R.set([W.Time,H.Settings.WindEnabled===false?0:H.Settings.WindTurbulence??.15,0,S.CloudSource],52);R.set([S.SizeScale,S.WindDrift,+S.FollowWind,0],56);
  R.set([Type.vt,Type.drag,S.Restitution,S.Category],76);R.set([...Eye,45],80);R.set([F[0]/FL,F[1]/FL,+S.SpawnFromClouds,Top],84);R.set([S.Opacity,S.Streak,S.RestTime,S.Accumulation],88);this.Exchange.Device.queue.writeBuffer(this.Params,0,R);

  if(this.Active&&!this.ReadPending&&performance.now()-this.LastRead>1000)this.Poll();
 }
 Poll(){const Epoch=this.Epoch,Revision=this.ActivityRevision;this.LastRead=performance.now();this.ReadPending=true;const D=this.Exchange.Device,E=D.createCommandEncoder();E.copyBufferToBuffer(this.Stats,0,this.Readback,0,64);D.queue.submit([E.finish()]);this.Readback.mapAsync(GPUMapMode.READ).then(()=>{if(Epoch===this.Epoch)this.Counts.set(new Uint32Array(this.Readback.getMappedRange()));this.Readback.unmap();if(Revision===this.ActivityRevision&&!this.Settings.Enabled&&!this.Counts[4])this.Active=false;}).catch(E=>this.Host.Telemetry?.Event('precipitation_read_error',{message:String(E)})).finally(()=>this.ReadPending=false);}
 RecordSimulation(E,Timed=true){if(this.ClearRequested){E.clearBuffer(this.Flakes);E.clearBuffer(this.Deposits);E.clearBuffer(this.Stats);E.clearBuffer(this.MeshDraw,4,4);this.ClearRequested=false;}if(!this.Active)return;E.clearBuffer(this.Stats,16,16);E.clearBuffer(this.Stats,56,4);const P=E.beginComputePass({label:'GPU cloud precipitation',timestampWrites:Timed?this.Host.Metrics?.QueryWrites('Precipitation simulation'):undefined});P.setPipeline(this.Compute);P.setBindGroup(0,this.Group);P.dispatchWorkgroups(Math.ceil(this.HighWater/64));P.end();this.Warm=false;}
 RecordSurface(E,Target,Depth){if(!this.Resident||this.Host.Settings.Inspection!==0)return;const P=E.beginRenderPass({label:'Stopped precipitation mesh grains',timestampWrites:this.Host.Metrics?.QueryWrites('Snow mounds'),colorAttachments:[{view:Target,loadOp:'load',storeOp:'store'}],depthStencilAttachment:{view:Depth,depthLoadOp:'load',depthStoreOp:'store'}});P.setPipeline(this.Surface);P.setBindGroup(0,this.SurfaceGroup);P.drawIndirect(this.MeshDraw,0);P.end();}
 Record(E,Target,Depth){if(!this.Active||this.Host.Settings.Inspection!==0)return;const P=E.beginRenderPass({label:'GPU cloud snowflakes',timestampWrites:this.Host.Metrics?.QueryWrites('Cloud precipitation'),colorAttachments:[{view:Target,loadOp:'load',storeOp:'store'}],depthStencilAttachment:{view:Depth,depthReadOnly:true}});P.setPipeline(this.Sprite);P.setBindGroup(0,this.SpriteGroup);P.setVertexBuffer(0,this.Flakes);P.draw(6,this.HighWater);P.end();}
}
export function BuildPrecipitationInspector(H,C,B){
 const P=H.Precipitation,S=P.Settings,Card=C.Card(B,'Precipitation · celestial GPU port','Layers',{Index:1,Accent:'#cfe3f5'});H.PrecipitationInspector=Card;
 C.Muted(Card,'Celestial demo behavior, ported to GPU: 45 m camera-local falling volume, terminal-speed drag, round snow/hail and rain streaks. Hail rebounds up to three times, comes to rest, then becomes opaque 3D mesh grains. No white surface coating.');
 const Start=C.Button(Card,S.Enabled?'Stop precipitation':'Start precipitation','Sparkles',()=>{if(S.Enabled)P.Stop();else P.Start();Sync();});function Sync(){Start.textContent=S.Enabled?'Stop precipitation':'Start precipitation';}C.Live.push(Sync);
 C.Button(Card,'Clear precipitation & settled meshes','X',()=>{P.Clear();Sync();});
 C.Select(Card,S,'Category',CelestialTypes.map((T,I)=>[T.Name,I]),{Label:'Precipitation'});
 C.Select(Card,S,'Budget',Object.entries(CelestialBudgets).map(([Name,N])=>[Name+' · '+N,N]),{Label:'Budget'});
 C.Switch(Card,S,'SpawnFromClouds',{Label:'Spawn from clouds',Detail:'Cloud-masked camera-local volume, as in the demo. Off: uniform volume up to 60 m above camera.'});
 C.Select(Card,S,'CloudSource',[['Auto · global, otherwise local',0],['Global cloud layer',1],['Local cloud volume',2]],{Label:'Cloud source'});
 for(const [Key,Label,Minimum,Maximum,Step,Unit] of [['RateMillimetresPerHour','Intensity',0,100,.5,'mm/h'],['Density','Density',.1,6,.1,'×'],['SizeScale','Particle size',.3,3,.05,'×'],['WindDrift','Wind drift',0,1,.01,''],['Restitution','Restitution',0,1,.01,''],['RestTime','Rest time before mesh conversion',.1,10,.1,'s'],['Accumulation','Accumulation',0,1,.01,''],['Streak','Motion streak',0,1,.01,''],['Opacity','Opacity',.1,1,.01,'']])C.Slider(Card,S,Key,{Label,Minimum,Maximum,Step,Unit});
 C.Switch(Card,S,'FollowWind',{Label:'Follow shared Wind Field'});
 C.Readout(Card,()=>{const Q=P.Counts;return [`${Q[4]} GPU particles · ${Q[5]} rebounding · ${Q[14]} at rest`,`${Q[13]} bounce events · ${Q[12]} / 65536 settled mesh grains`,`${Q[1]} ground conversions · ${Q[2]} vehicle conversions · ${Q[6]} retained at budget`,`${CelestialTypes[S.Category].vt} m/s terminal speed · ${S.Restitution*CelestialTypes[S.Category].bounce} effective restitution`];});
 C.Muted(Card,'Snow/ice converts only after final contact, zero velocity and the rest interval. Rain/drizzle rest splashes retire instead of creating ice. Settled grains are opaque triangle meshes, not a connected height-field blanket or decal. Vehicle grains stay body-local. Stop drains existing particles; Clear explicitly removes everything.');
 C.Muted(Card,'Bounded GPU pools: 32768 moving slots reserved; 65536 stopped mesh instances. Mesh-budget overflow keeps stopped slots instead of deleting their ice. Ground receiving map stays world-fixed (128 × 128 m on first Start); body receiver assumes authored upper surfaces. No arbitrary obstacle collision, mesh union, sliding/melting, snow physics or disk persistence yet.');
}
