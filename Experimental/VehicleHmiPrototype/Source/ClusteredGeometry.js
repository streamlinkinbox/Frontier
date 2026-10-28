import {BuildGeometryClusters} from './GeometryClusters.js';
// GPU-frustum-tested, lossless static clusters. Mutable/composite/deformed geometry stays on the established path.
export class ClusteredGeometry{
 constructor(E,Shaders){this.Exchange=E;this.Shaders=Shaders;this.Cache=new Map();this.CandidateCount=0;this.Revision=0;}
 async ConstructPrograms(Frame,Occupants){const E=this.Exchange;this.Frame=Frame;this.Occupants=Occupants;this.Layout=E.ConstructDescriptorLayout(['Uniform','StorageRead','StorageRead','Storage'],GPUShaderStage.COMPUTE,'Geometry cluster culling');this.Program=await this.Shaders.ConstructComputeProgram('GeometryClusterCull.wgsl','CullGeometryClusters',[this.Layout]);}
 Prepare(Scene){
  const Key=Scene.DrawRuns.map(R=>`${R.FacetSet}:${R.First}:${R.Count}`).join('|');if(Key===this.Key)return;this.Key=Key;
  for(const B of this.Buffers??[])B.destroy();this.Draws=[];const Indices=[],Records=[],Offsets=new Map();
  for(const Run of Scene.DrawRuns){const Set=Scene.FacetSets[Run.FacetSet];const Eligible=Set.Facets.length>=256&&!Set.VertexCapacity&&Array.from({length:Run.Count},(_,I)=>Scene.Occupants[Run.First+I]).every(O=>!(O.Roles&8));
   if(!Eligible){this.Draws.push({Run});continue;}
   let Built=this.Cache.get(Run.FacetSet);if(!Built){Built=BuildGeometryClusters(Set,this.TriangleLimit??128);this.Cache.set(Run.FacetSet,Built);}
   let Offset=Offsets.get(Run.FacetSet);if(Offset===undefined){Offset=Indices.length;Offsets.set(Run.FacetSet,Offset);for(const Index of Built.Indices)Indices.push(Index);}
   for(let O=Run.First;O<Run.First+Run.Count;O++)for(const C of Built.Clusters){const Id=Records.length;Records.push({...C,FirstIndex:C.FirstIndex+Offset,Occupant:O});this.Draws.push({Cluster:Id,Occupant:O});}
  }
  this.DebugRecords=Records;this.DebugValues=null;this.DebugRevision=null;this.DebugVisible=null;this.DebugSampleTime=null;
  this.CandidateCount=Records.length;this.IndexCount=Indices.length;this.Revision++;
  const E=this.Exchange;const Make=(Data,Usage,Label)=>{const B=E.ConstructRecords(Math.max(4,Data.byteLength),Usage|GPUBufferUsage.COPY_DST,Label);if(Data.byteLength)E.Device.queue.writeBuffer(B,0,Data);return B;};
  this.Indices=Make(new Uint32Array(Indices),GPUBufferUsage.INDEX,'Cluster triangle indices');
  this.Instances=Make(Uint32Array.from(Scene.Occupants,(_,I)=>I),GPUBufferUsage.VERTEX,'Cluster instance identities');
  this.Buffers=[this.Indices,this.Instances];if(!Records.length)return;
  const Data=new Float32Array(Records.length*12),U=new Uint32Array(Data.buffer);Records.forEach((C,I)=>{const P=I*12;Data.set(C.Min,P);U[P+3]=C.Occupant;Data.set(C.Max,P+4);U[P+7]=C.FirstIndex;U[P+8]=C.IndexCount;});
  this.Records=Make(Data,GPUBufferUsage.STORAGE,'Geometry cluster bounds');this.Commands=E.ConstructRecords(Records.length*20,GPUBufferUsage.STORAGE|GPUBufferUsage.INDIRECT|GPUBufferUsage.COPY_SRC,'GPU cluster draws');this.Buffers.push(this.Records,this.Commands);
  this.Group=E.ConstructDescriptorGroup(this.Layout,[this.Frame,this.Occupants,this.Records,this.Commands]);
 }
 RecordCull(Encoder,QueryWrites){if(!this.CandidateCount)return;const P=Encoder.beginComputePass({label:'Geometry cluster frustum culling',timestampWrites:QueryWrites?.()});P.setPipeline(this.Program);P.setBindGroup(0,this.Group);P.dispatchWorkgroups(Math.ceil(this.CandidateCount/64));P.end();
 if(this.DebugEnabled&&!this.DebugPending&&performance.now()-(this.DebugTime??-Infinity)>250){this.DebugTime=performance.now();const Size=this.CandidateCount*20;this.DebugRead=this.Exchange.ConstructRecords(Size,GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ,"Debug cluster commands");Encoder.copyBufferToBuffer(this.Commands,0,this.DebugRead,0,Size);this.DebugPending=true;this.DebugQueued=true;this.CopyRevision=this.Revision;}
 }
 async CollectDebug(){
  if(!this.DebugQueued)return;this.DebugQueued=false;const B=this.DebugRead,Revision=this.CopyRevision;
  try{await B.mapAsync(GPUMapMode.READ);const U=new Uint32Array(B.getMappedRange().slice(0));B.unmap();if(Revision===this.Revision){this.DebugValues=Array.from({length:U.length/5},(_,I)=>U[I*5+1]>0);this.DebugVisible=this.DebugValues.filter(Boolean).length;this.DebugRevision=Revision;this.DebugSampleTime=this.DebugTime;}}
  catch(E){this.DebugError=String(E.message??E);}finally{B.destroy();this.DebugPending=false;}
 }
 RecordDraws(Pass,Scene){Pass.setIndexBuffer(this.Indices,'uint32');for(const D of this.Draws){if(D.Run){Pass.setVertexBuffer(1,this.Instances);const R=D.Run,S=Scene.FacetSets[R.FacetSet];Pass.draw(S.VertexCount,R.Count,S.FirstVertex,R.First);}else{Pass.setVertexBuffer(1,this.Instances,D.Occupant*4,4);Pass.drawIndexedIndirect(this.Commands,D.Cluster*20);}}}
}
