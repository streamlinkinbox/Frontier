
#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "AtmosphereMedium.slang"
  [[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Target;
 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateTransmittance(uint3 ID : SV_DispatchThreadID) {
 let Size = TextureExtent(Target); if (any(ID.xy>=Size)){return;}
 let UV = (float2(ID.xy)+0.5)/float2(Size);
 let H = UV.y*UV.y*(AtmosphereRadius-GroundRadius);
 let Mu = UV.x*2.0-1.0;
 let P = float3(0.0,0.0,GroundRadius+max(1.0,H));
 let D = float3(sqrt(max(0.0,1.0-Mu*Mu)),0.0,Mu);
 Target[int2(ID.xy)] = float4(MarchTransmittance(P,D),1.0);
}
