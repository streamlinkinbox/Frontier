
#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "RefreshDescriptor.slang"
struct GeometryCluster {float3 Minimum;uint Occupant;float3 Maximum;uint FirstIndex;uint IndexCount;uint Pad0;uint Pad1;uint Pad2;};
struct ClusterCommand {uint IndexCount;uint InstanceCount;uint FirstIndex;uint BaseVertex;uint FirstInstance;};
  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile> ClusterOccupants;
  [[vk::binding(2, 0)]] StructuredBuffer<GeometryCluster> GeometryClusters;
  [[vk::binding(3, 0)]] RWStructuredBuffer<ClusterCommand> ClusterCommands;
 
[shader("compute")]
[numthreads(64, 1, 1)]
void CullGeometryClusters(uint3 ID : SV_DispatchThreadID) {
 let I = ID.x;if (I>=ArrayCount(GeometryClusters)){return;}let C = GeometryClusters[I];let M = mul(ClusterOccupants[C.Occupant].LocalToWorld, Refresh.ViewProjection);
 var Outside : uint[6] = {0u,0u,0u,0u,0u,0u};
 for (var K = 0u; K<8u; K++){let P = float3(SelectBranch(C.Minimum.x,C.Maximum.x,(K&1u)!=0u),SelectBranch(C.Minimum.y,C.Maximum.y,(K&2u)!=0u),SelectBranch(C.Minimum.z,C.Maximum.z,(K&4u)!=0u));let Q = mul(float4(P,1), M);
  Outside[0]+=SelectBranch(0u,1u,Q.x < -Q.w);Outside[1]+=SelectBranch(0u,1u,Q.x>Q.w);Outside[2]+=SelectBranch(0u,1u,Q.y < -Q.w);Outside[3]+=SelectBranch(0u,1u,Q.y>Q.w);Outside[4]+=SelectBranch(0u,1u,Q.z<0.0);Outside[5]+=SelectBranch(0u,1u,Q.z>Q.w);
 }
 var Visible = true;for (var K = 0u; K<6u; K++){if (Outside[K]==8u){Visible=false;}}
 ClusterCommands[I]=ClusterCommand(C.IndexCount,SelectBranch(0u,1u,Visible),C.FirstIndex,0u,0u);
}
