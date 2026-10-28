// Lossless spatial leaf clusters. No triangle removal, simplification or material merging.
export function BuildGeometryClusters(Set,Limit=128){
 if(!Number.isInteger(Limit)||Limit<1)throw new Error('Cluster triangle limit must be positive');
 const Facets=Set.Facets,Order=Facets.map((F,I)=>({I,C:[0,1,2].map(A=>F.Corners.reduce((S,P)=>S+P[A],0)/3)}));
 // Spatial median splits avoid reordering the live scene vertex records (fracture / BLAS share those records).
 const Leaves=[];
 const Split=Items=>{if(Items.length<=Limit){Leaves.push(Items);return;}const Min=[Infinity,Infinity,Infinity],Max=[-Infinity,-Infinity,-Infinity];for(const T of Items)for(let A=0;A<3;A++){Min[A]=Math.min(Min[A],T.C[A]);Max[A]=Math.max(Max[A],T.C[A]);}let Axis=0;for(let A=1;A<3;A++)if(Max[A]-Min[A]>Max[Axis]-Min[Axis])Axis=A;Items.sort((A,B)=>A.C[Axis]-B.C[Axis]||A.I-B.I);const Mid=Items.length>>1;Split(Items.slice(0,Mid));Split(Items.slice(Mid));};
 if(Order.length)Split(Order);
 const Indices=[],Clusters=[];for(const Items of Leaves){const Min=[Infinity,Infinity,Infinity],Max=[-Infinity,-Infinity,-Infinity],FirstIndex=Indices.length;for(const T of Items)for(let K=0;K<3;K++){Indices.push(Set.FirstVertex+T.I*3+K);const P=Facets[T.I].Corners[K];for(let A=0;A<3;A++){Min[A]=Math.min(Min[A],P[A]);Max[A]=Math.max(Max[A],P[A]);}}Clusters.push({Min,Max,FirstIndex,IndexCount:Items.length*3});}
 return {Indices:new Uint32Array(Indices),Clusters};
}
