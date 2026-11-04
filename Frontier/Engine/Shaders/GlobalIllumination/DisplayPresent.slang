//============================================================================================================================================
//                                                          DISPLAYPRESENT.SLANG
//============================================================================================================================================
// 🧩 Exposure and ACES tonal projection, written to the swapchain through an sRGB view.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"
  [[vk::binding(3, 0)]] Texture2D<float4> FlareVisibility;
  [[vk::binding(4, 0)]] Texture2D<float4> FlareImage;
  [[vk::binding(5, 0)]] SamplerState FlareSampler;

  [[vk::binding(1, 0)]] Texture2D<float4>                ResolvedRadiance;
  [[vk::binding(2, 0)]] StructuredBuffer<float>       Adaptation;   // fixed [4] span

#include "CloudPresent.slang"

struct PresentVarying
{
    float4 Position : SV_Position; 
};


[shader("vertex")]
PresentVarying PresentVertex(uint Vertex : SV_VulkanVertexID)
{
    let Corner = float2(float((Vertex << 1u) & 2u), float(Vertex & 2u));
    var Varying : PresentVarying = {};
    Varying.Position = float4(Corner * 2.0 - 1.0, 0.0, 1.0);
    return Varying;
}

// 📝 Narkowicz's ACES filmic fit.
float3 ProjectFilmic(float3 Radiance)
{
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((Radiance * (a * Radiance + b)) / (Radiance * (c * Radiance + d) + e), float3(0.0), float3(1.0));
}

[shader("fragment")]
float4 PresentFragment(PresentVarying Varying) : SV_Target0
{
    let Pixel = int2(Varying.Position.xy);
    var Radiance = AddClouds(Varying.Position.xy,LoadTexel(ResolvedRadiance, Pixel, 0).rgb);
    Radiance=AddLightning(Varying.Position.xy,Radiance);
    if (Refresh.Flare.x>0.0 && Refresh.InspectionMode==0u){
        let Visibility = LoadTexel(FlareVisibility, int2(0), 0).r;
        if (Visibility>0.0){Radiance+=FlareImage.SampleLevel(FlareSampler, Varying.Position.xy*Refresh.InverseScreenExtent, 0.0).rgb*Visibility;}
    }
    var Exposure = Adaptation[0];
    if (Exposure <= 0.0)
    {
        Exposure = 1.0;
    }
    var Colour = ProjectFilmic(Radiance * Exposure);
    if (Refresh.InspectionMode == 6u || Refresh.InspectionMode == 8u || Refresh.InspectionMode == 9u || Refresh.InspectionMode >= 10u)
    {
        Colour = clamp(Radiance, float3(0.0), float3(1.0));
    }
    return float4(Colour, 1.0);
}
