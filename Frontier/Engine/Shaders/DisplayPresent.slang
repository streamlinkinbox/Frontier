// DisplayPresent.slang - HDR surface presentation to swapchain.
// Fullscreen triangle with exposure-scaled linear radiance readout.

struct PresentUniforms
{
    float Exposure;
};

[[vk::binding(0, 0)]]
ConstantBuffer<PresentUniforms> Present;

[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
Texture2D<float4> Surface;

[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
SamplerState SurfaceSampler;

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 Uv           : TEXCOORD0;
};

[shader("vertex")]
VertexOutput PresentVertex(uint VertexId : SV_VertexID)
{
    VertexOutput Output;
    Output.Uv           = float2((VertexId << 1) & 2, VertexId & 2);
    Output.ClipPosition = float4(Output.Uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return Output;
}

[shader("fragment")]
float4 PresentFragment(VertexOutput Input) : SV_Target
{
    float3 Radiance = Surface.Sample(SurfaceSampler, Input.Uv).rgb * Present.Exposure;
    return float4(Radiance, 1.0);
}
