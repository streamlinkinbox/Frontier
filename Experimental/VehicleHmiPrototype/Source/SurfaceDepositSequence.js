import {SurfaceDepositGrid,SettlingParticlePool} from './SurfaceDepositGrid.js';
import {BuildSurfaceDepositMesh,DepositBoundsVisible} from './SurfaceDepositMesh.js';
import {PlumeGrades} from './PlumeSpace.js';
import {ResolveCourseHeight} from './CourseStructure.js';
export class SurfaceDepositSequence{
 constructor(Exchange,Shaders,Host){Object.assign(this,{Exchange,Shaders,Host});this.Grid=new SurfaceDepositGrid({Edge:PlumeGrades[0].CellExtent});this.Pool=new SettlingParticlePool();this.Surfaces=new Map([['terrain',ResolveCourseHeight]]);this.Meshes=new Map();this.Clock=0;this.NextBuild=0;this.Visible=true;this.Metrics={Built:0,Draws:0,Vertices:0,RebuildMs:0};}
 async ConstructPrograms(Frame){const E=this.Exchange;this.Layout=E.ConstructDescriptorLayout(['Uniform'],GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,'Settled surface deposit');this.Group=E.ConstructDescriptorGroup(this.Layout,[Frame]);this.Program=await this.Shaders.ConstructRasterProgram('SurfaceDepositRaster.wgsl',{VertexEntry:'DepositVertex',FragmentEntry:'DepositFragment',VertexLayouts:[{arrayStride:28,attributes:[{shaderLocation:0,offset:0,format:'float32x3'},{shaderLocation:1,offset:12,format:'float32x3'},{shaderLocation:2,offset:24,format:'float32'}]}],Primitive:{topology:'triangle-list',cullMode:'none'},DepthStencil:{format:'depth32float',depthWriteEnabled:true,depthCompare:'greater'},Targets:[{format:'rgba16float',blend:{color:{srcFactor:'src-alpha',dstFactor:'one-minus-src-alpha',operation:'add'},alpha:{srcFactor:'one',dstFactor:'zero',operation:'add'}}}]},[this.Layout]);}
 RegisterSurface(Id,Height){if(typeof Id!=='string'||!Id.length||Id.length>64||typeof Height!=='function'||this.Surfaces.has(Id)||this.Surfaces.size>=16)return false;this.Surfaces.set(Id,Height);return true;}
 Settle(Handle,Surface='terrain'){if(!this.Surfaces.has(Surface))return {Accepted:0,Released:false,Reason:'Unknown receiving surface'};return this.Pool.Settle(Handle,this.Grid,Surface);}
 Prepare(Seconds){this.Clock+=Math.max(0,Math.min(.1,Seconds||0));this.Metrics.RebuildMs=0;if(!this.Grid.Dirty.size||this.Clock<this.NextBuild)return;this.NextBuild=this.Clock+.2;const Start=performance.now();let Count=0;
  for(const Key of this.Grid.Dirty){if(Count++>=2)break;const Chunk=this.Grid.Chunks.get(Key);if(!Chunk){this.Grid.Dirty.delete(Key);continue;}
   let Mesh;try{Mesh=BuildSurfaceDepositMesh(this.Grid,Chunk,this.Surfaces.get(Chunk.Surface));}catch(Error){this.Grid.Status='Mesh build failed — deposited volume retained';this.Host.Telemetry?.Event('surface_deposit_mesh_error',{chunk:Key,message:String(Error.message??Error)});this.Grid.Dirty.delete(Key);continue;}let Entry=this.Meshes.get(Key);
   if(!Entry||Entry.Capacity<Mesh.Vertices.byteLength){Entry?.Buffer.destroy();Entry={Buffer:this.Exchange.ConstructRecords(Math.max(16,Mesh.Vertices.byteLength),GPUBufferUsage.VERTEX|GPUBufferUsage.COPY_DST,'Merged deposit chunk'),Capacity:Math.max(16,Mesh.Vertices.byteLength)};this.Meshes.set(Key,Entry);}
   if(Mesh.Vertices.length)this.Exchange.Device.queue.writeBuffer(Entry.Buffer,0,Mesh.Vertices);Object.assign(Entry,{VertexCount:Mesh.VertexCount,Bounds:Mesh.Bounds,Revision:Chunk.Revision});this.Grid.Dirty.delete(Key);this.Metrics.Built++;
  }this.Metrics.RebuildMs=performance.now()-Start;
 }
 Record(Encoder,Target,Depth,Metrics){this.Metrics.Draws=0;this.Metrics.Vertices=0;if(!this.Visible||this.Host.Settings.Inspection!==0||!this.Meshes.size)return;
  const Visible=[];for(const E of this.Meshes.values())if(E.VertexCount&&DepositBoundsVisible(E.Bounds,this.Host.PreviousUnjittered))Visible.push(E);if(!Visible.length)return;
  const P=Encoder.beginRenderPass({label:'Settled surface deposits',timestampWrites:Metrics?.QueryWrites('Surface deposits'),colorAttachments:[{view:Target,loadOp:'load',storeOp:'store'}],depthStencilAttachment:{view:Depth,depthLoadOp:'load',depthStoreOp:'store'}});P.setPipeline(this.Program);P.setBindGroup(0,this.Group);for(const E of Visible){P.setVertexBuffer(0,E.Buffer);P.draw(E.VertexCount);this.Metrics.Draws++;this.Metrics.Vertices+=E.VertexCount;}P.end();
 }
 TestPatch(){const H=this.Host,Eye=H.CameraEye??[-47,0,3],F=H.CameraState?.Forward??[1,0,0],L=Math.hypot(F[0],F[1])||1,Centre=[Eye[0]+F[0]/L*12-F[1]/L*5,Eye[1]+F[1]/L*12+F[0]/L*5],A=this.Grid.Address(Centre).Cell;let Accepted=0;
  for(let Y=-4;Y<=4;Y++)for(let X=-4;X<=4;X++){const P=[(A[0]+X+.5)*this.Grid.Edge,(A[1]+Y+.5)*this.Grid.Edge,0];P[2]=ResolveCourseHeight(P[0],P[1]);const Handle=this.Pool.Allocate(P,.02*this.Grid.Edge**2);if(Handle){const R=this.Settle(Handle);Accepted+=R.Accepted;}}
  H.Telemetry?.Event('deposit_test_patch',{volume_m3:Accepted,active_slots:this.Pool.Active,recycled_slots:this.Pool.Recycled,chunks:this.Grid.Chunks.size});return Accepted;
 }
 Clear(){for(const M of this.Meshes.values())M.Buffer.destroy();this.Meshes.clear();this.Grid.Clear();this.NextBuild=0;this.Metrics.Draws=0;this.Metrics.Vertices=0;this.Host.Telemetry?.Event('surface_deposits_cleared',{pending_slots:this.Pool.Active});}
}
