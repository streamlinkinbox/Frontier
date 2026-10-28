import {WorldGridCell,WrapGridCell,SurfaceChunkKey} from './SpatialGrid.js';
// Durable, sparse, world-anchored surface layer. Deliberately NOT the moving/fading gas buffer.
export class SurfaceDepositGrid{
 constructor({Edge=.4,ChunkCells=16,MaxChunks=128,MaxDepth=2}={}){
  if(!Number.isFinite(Edge)||Edge<=0||!Number.isInteger(ChunkCells)||ChunkCells<2||ChunkCells>64||!Number.isInteger(MaxChunks)||MaxChunks<1||MaxChunks>1024||!Number.isFinite(MaxDepth)||MaxDepth<=0)throw Error('Invalid deposit grid limits');
  Object.assign(this,{Edge,ChunkCells,MaxChunks,MaxDepth});this.Chunks=new Map();this.Dirty=new Set();this.Volume=0;this.Deposits=0;this.Status='Empty';
 }
 Address(Position,Surface='terrain'){
  const Cell=WorldGridCell(Position.slice(0,2),this.Edge),Chunk=Cell.map(V=>Math.floor(V/this.ChunkCells)),Local=WrapGridCell(Cell,[this.ChunkCells,this.ChunkCells]);
  return {Cell,Chunk,Local,Index:Local[1]*this.ChunkCells+Local[0],Key:SurfaceChunkKey(Surface,...Chunk)};
 }
 Depth(Cell,Surface='terrain'){
  const Chunk=Cell.map(V=>Math.floor(V/this.ChunkCells)),Local=WrapGridCell(Cell,[this.ChunkCells,this.ChunkCells]);return (this.Chunks.get(SurfaceChunkKey(Surface,...Chunk))?.Volumes[Local[1]*this.ChunkCells+Local[0]]??0)/(this.Edge*this.Edge);
 }
 Deposit(Position,Volume,Surface='terrain'){
  if(!Array.isArray(Position)||Position.length<2||!Position.every(V=>Number.isFinite(V)&&Math.abs(V)<=1e7)||!Number.isFinite(Volume)||Volume<=0||typeof Surface!=='string'||!Surface.length||Surface.length>64)return {Accepted:0,Remaining:Volume,Reason:'Invalid deposit'};
  const A=this.Address(Position,Surface);let C=this.Chunks.get(A.Key);
  if(!C){if(this.Chunks.size>=this.MaxChunks){this.Status='Chunk budget full — new deposits remain in particle slots';return {Accepted:0,Remaining:Volume,Reason:'Chunk budget'};}C={Key:A.Key,Surface,XY:A.Chunk,Volumes:new Float64Array(this.ChunkCells**2),Revision:0};this.Chunks.set(A.Key,C);}
  const Free=Math.max(0,this.MaxDepth*this.Edge*this.Edge-C.Volumes[A.Index]),Accepted=Math.min(Free,Volume);
  if(Accepted>0){C.Volumes[A.Index]+=Accepted;C.Revision++;this.Volume+=Accepted;this.Deposits++;this.Dirty.add(A.Key);
   // Smoothed mesh corners depend on adjacent cells, including across existing chunk boundaries.
   for(let Y=-1;Y<=1;Y++)for(let X=-1;X<=1;X++){const K=SurfaceChunkKey(Surface,A.Chunk[0]+X,A.Chunk[1]+Y);if(this.Chunks.has(K))this.Dirty.add(K);}
  }
  const Remaining=Volume-Accepted;this.Status=Remaining>0?'Cell depth limit — remainder retained in particle slot':'Retaining deposits in RAM';return {Accepted,Remaining,Reason:Remaining>0?'Cell depth limit':null};
 }
 Clear(){this.Chunks.clear();this.Dirty.clear();this.Volume=0;this.Deposits=0;this.Status='Empty';}
}
// A bounded handoff pool, not a new snowfall emitter. Handles prevent stale/double deposits.
export class SettlingParticlePool{
 constructor(Capacity=1024){if(!Number.isInteger(Capacity)||Capacity<1||Capacity>65536)throw Error('Invalid particle pool capacity');this.Capacity=Capacity;this.Data=new Float64Array(Capacity*4);this.Live=new Uint8Array(Capacity);this.Generation=new Uint32Array(Capacity);this.Free=Array.from({length:Capacity},(_,I)=>Capacity-1-I);this.Active=0;this.Recycled=0;this.Receivers=new Array(Capacity);}
 Allocate(Position,Volume){if(!Array.isArray(Position)||Position.length!==3||!Position.every(V=>Number.isFinite(V)&&Math.abs(V)<=1e7)||!Number.isFinite(Volume)||Volume<=0||!this.Free.length)return null;const I=this.Free.pop();this.Generation[I]++;this.Live[I]=1;this.Receivers[I]=undefined;this.Data.set([...Position,Volume],I*4);this.Active++;return {Index:I,Generation:this.Generation[I]};}
 Valid(H){return !!H&&Number.isInteger(H.Index)&&H.Index>=0&&H.Index<this.Capacity&&this.Live[H.Index]===1&&this.Generation[H.Index]===H.Generation;}
 Settle(Handle,Grid,Surface='terrain'){
  if(!this.Valid(Handle))return {Accepted:0,Remaining:0,Released:false,Reason:'Stale particle handle'};
  const I=Handle.Index,O=I*4;
  if(this.Receivers[I]!==undefined&&this.Receivers[I]!==Surface)return {Accepted:0,Remaining:this.Data[O+3],Released:false,Reason:'Receiver mismatch'};
  this.Receivers[I]=Surface;const Result=Grid.Deposit(Array.from(this.Data.subarray(O,O+3)),this.Data[O+3],Surface);this.Data[O+3]=Result.Remaining;
  if(Result.Remaining<=0){this.Data.fill(0,O,O+4);this.Live[I]=0;this.Free.push(I);this.Active--;this.Recycled++;return {...Result,Released:true};}
  return {...Result,Released:false};
 }
 Cancel(H){if(!this.Valid(H))return false;const I=H.Index;this.Data.fill(0,I*4,I*4+4);this.Live[I]=0;this.Receivers[I]=undefined;this.Free.push(I);this.Active--;return true;}
 // Retrying an explicitly retained remainder is safe; clearing the grid alone does not discard it.
 Retry(Grid){for(let I=0;I<this.Capacity;I++)if(this.Live[I]&&this.Receivers[I]!==undefined)this.Settle({Index:I,Generation:this.Generation[I]},Grid,this.Receivers[I]??'terrain');}
}
