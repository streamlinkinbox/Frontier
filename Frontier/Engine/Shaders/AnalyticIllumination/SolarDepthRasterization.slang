//============================================================================================================================================
//                                                     SOLARDEPTHRASTERIZATION.SLANG
//============================================================================================================================================
// 🧩 The sun's depth map: every rastered occupant projected orthographically along the sun (depth only, standard depth).

#pragma once
#include "AnalyticLayouts.slang"

[shader("vertex")]
float4 SolarDepthVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1,
                        [[vk::location(2)]] float3 Normal : LOCATION2, [[vk::location(3)]] uint OccupantOffset : LOCATION3,
                        [[vk::location(4)]] uint OccupantIndex : LOCATION4) : SV_Position
{
    let World = mul(float4(Position, 1.0), Occupants[OccupantIndex + OccupantOffset].LocalToWorld);
    return mul(World, Analytic.SolarViewProjection);
}
