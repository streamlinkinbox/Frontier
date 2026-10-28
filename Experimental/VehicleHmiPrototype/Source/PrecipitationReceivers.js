// Static geometry preparation only: simulation, collision and volume transfer run on the GPU.
export const PrecipitationLimits=Object.freeze({Capacity:32768,GroundCells:128,GroundEdge:1,CarCells:64,CarEdge:.125,VolumeUnit:1e-6,MaxDepth:30});
export function BakeVehicleSnowReceiver(Facets,N=64,Edge=.125){
 const Heights=new Float32Array(N*N).fill(-10000),Half=N*Edge*.5;
 for(const F of Facets){const [A,B,C]=F.Corners,D=(B[1]-C[1])*(A[0]-C[0])+(C[0]-B[0])*(A[1]-C[1]);if(Math.abs(D)<1e-10)continue;
  const Min=[0,1].map(K=>Math.max(0,Math.floor((Math.min(A[K],B[K],C[K])+Half)/Edge))),Max=[0,1].map(K=>Math.min(N-1,Math.floor((Math.max(A[K],B[K],C[K])+Half)/Edge)));
  for(let Y=Min[1];Y<=Max[1];Y++)for(let X=Min[0];X<=Max[0];X++){const PX=(X+.5)*Edge-Half,PY=(Y+.5)*Edge-Half,U=((B[1]-C[1])*(PX-C[0])+(C[0]-B[0])*(PY-C[1]))/D,V=((C[1]-A[1])*(PX-C[0])+(A[0]-C[0])*(PY-C[1]))/D;if(U>=-1e-6&&V>=-1e-6&&U+V<=1.000001)Heights[Y*N+X]=Math.max(Heights[Y*N+X],U*A[2]+V*B[2]+(1-U-V)*C[2]);}
 }return Heights;
}
