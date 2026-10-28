// Bilinear sample of the same averaged corners used by the mesh; shared by flake landing tests.
export function SampleSurfaceDepositDepth(Grid,X,Y,Surface='terrain'){
 const GX=X/Grid.Edge,GY=Y/Grid.Edge,CX=Math.floor(GX),CY=Math.floor(GY),FX=GX-CX,FY=GY-CY;let D=0;
 for(let YI=0;YI<2;YI++)for(let XI=0;XI<2;XI++){let H=0;for(let J=-1;J<=0;J++)for(let I=-1;I<=0;I++)H+=Grid.Depth([CX+XI+I,CY+YI+J],Surface)*.25;D+=H*(XI?FX:1-FX)*(YI?FY:1-FY);}return D;
}
// Shared corner samples create matching chunk seams. No mesh/scene object per landed flake.
export function BuildSurfaceDepositMesh(Grid,Chunk,Height){
 const {ChunkCells:N,Edge:E}=Grid,Start=Chunk.XY.map(V=>V*N),Vertices=[],Bounds={Min:[Infinity,Infinity,Infinity],Max:[-Infinity,-Infinity,-Infinity]};
 const Node=(X,Y)=>{
  let Depth=0;for(let J=-1;J<=0;J++)for(let I=-1;I<=0;I++)Depth+=Grid.Depth([X+I,Y+J],Chunk.Surface)*.25;
  const PX=X*E,PY=Y*E,Z=Height(PX,PY);if(!Number.isFinite(Z))throw Error('Receiver height is not finite');return {P:[PX,PY,Z+Depth+.003],Depth};
 };
 const Nodes=Array.from({length:(N+1)**2},(_,I)=>Node(Start[0]+I%(N+1),Start[1]+Math.floor(I/(N+1))));
 const Triangle=(NA,NB,NC)=>{const A=NA.P,B=NB.P,C=NC.P,U=B.map((V,I)=>V-A[I]),V=C.map((Q,I)=>Q-A[I]),Cross=[U[1]*V[2]-U[2]*V[1],U[2]*V[0]-U[0]*V[2],U[0]*V[1]-U[1]*V[0]],Length=Math.hypot(...Cross)||1,Normal=Cross.map(Q=>Q/Length);for(const Node of [NA,NB,NC]){const P=Node.P;Vertices.push(...P,...Normal,Node.Depth);for(let I=0;I<3;I++){Bounds.Min[I]=Math.min(Bounds.Min[I],P[I]);Bounds.Max[I]=Math.max(Bounds.Max[I],P[I]);}}};
 for(let Y=0;Y<N;Y++)for(let X=0;X<N;X++){const A=Nodes[Y*(N+1)+X],B=Nodes[Y*(N+1)+X+1],C=Nodes[(Y+1)*(N+1)+X],D=Nodes[(Y+1)*(N+1)+X+1];if(Math.max(A.Depth,B.Depth,C.Depth,D.Depth)<=0)continue;Triangle(A,B,D);Triangle(A,D,C);}
 return {Vertices:new Float32Array(Vertices),Bounds,VertexCount:Vertices.length/7};
}
export function DepositBoundsVisible(Bounds,M){
 if(!M||!Bounds||!Number.isFinite(Bounds.Min[0]))return false;
 const Clip=[];for(let Z=0;Z<2;Z++)for(let Y=0;Y<2;Y++)for(let X=0;X<2;X++){const P=[X?Bounds.Max[0]:Bounds.Min[0],Y?Bounds.Max[1]:Bounds.Min[1],Z?Bounds.Max[2]:Bounds.Min[2],1];Clip.push([0,1,2,3].map(R=>P.reduce((S,V,I)=>S+V*M[I*4+R],0)));}
 return ![P=>P[0]<-P[3],P=>P[0]>P[3],P=>P[1]<-P[3],P=>P[1]>P[3],P=>P[2]<0,P=>P[2]>P[3]].some(Outside=>Clip.every(Outside));
}
