//! include "FrameDescriptor.wgsl"
struct GeometryCluster {Minimum:vec3f,Occupant:u32,Maximum:vec3f,FirstIndex:u32,IndexCount:u32,Pad0:u32,Pad1:u32,Pad2:u32}
struct ClusterDraw {IndexCount:u32,InstanceCount:u32,FirstIndex:u32,BaseVertex:u32,FirstInstance:u32}
@group(0) @binding(1) var<storage,read> ClusterOccupants:array<OccupantRecord>;
@group(0) @binding(2) var<storage,read> GeometryClusters:array<GeometryCluster>;
@group(0) @binding(3) var<storage,read_write> ClusterDraws:array<ClusterDraw>;
@compute @workgroup_size(64)
fn CullGeometryClusters(@builtin(global_invocation_id) ID:vec3u){
 let I=ID.x;if(I>=arrayLength(&GeometryClusters)){return;}let C=GeometryClusters[I];let M=Frame.ViewProjection*ClusterOccupants[C.Occupant].LocalToWorld;
 var Outside=array<u32,6>(0u,0u,0u,0u,0u,0u);
 for(var K=0u;K<8u;K++){let P=vec3f(select(C.Minimum.x,C.Maximum.x,(K&1u)!=0u),select(C.Minimum.y,C.Maximum.y,(K&2u)!=0u),select(C.Minimum.z,C.Maximum.z,(K&4u)!=0u));let Q=M*vec4f(P,1);
  Outside[0]+=select(0u,1u,Q.x < -Q.w);Outside[1]+=select(0u,1u,Q.x>Q.w);Outside[2]+=select(0u,1u,Q.y < -Q.w);Outside[3]+=select(0u,1u,Q.y>Q.w);Outside[4]+=select(0u,1u,Q.z<0.0);Outside[5]+=select(0u,1u,Q.z>Q.w);
 }
 var Visible=true;for(var K=0u;K<6u;K++){if(Outside[K]==8u){Visible=false;}}
 ClusterDraws[I]=ClusterDraw(C.IndexCount,select(0u,1u,Visible),C.FirstIndex,0u,0u);
}
