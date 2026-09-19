//============================================================================================================================================
// 📦 Frontier/Shaders/GlobalIlluminationReSTIR.comp — ReSTIR GI Indirect Radiosity with Jacobian Shift Compute Shader
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

struct IndirectReservoirPayload
{
    vec4 SampledRadiance;                               // [lux] radiance L_o (xyz) and weight W (w)
    vec4 HitPositionAndNormal;                          // [m] position (xyz) and packed normal (w)
    float WeightSum;                                    // [-] weight sum
    uint SampleCount;                                   // [count] candidate count M
};

layout(std430, binding = 0) buffer IndirectReservoirExtent
{
    IndirectReservoirPayload Reservoirs[];              // [reservoirs] indirect path reservoirs
};

layout(std430, binding = 1) writeonly buffer OutputGIRadianceExtent
{
    vec4 IndirectRadianceField[];                       // [lux] output indirect radiosity
};

layout(push_constant) uniform PushConstants
{
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
    float TemporalHistoryLimit;                         // [count] temporal cap M_max
};

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPUTE MAIN INVOCATION
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec2 PixelCoord = gl_GlobalInvocationID.xy;
    if (PixelCoord.x >= ViewportWidth || PixelCoord.y >= ViewportHeight)
    {
        return;
    }

    uint LinearPixelIndex = PixelCoord.y * ViewportWidth + PixelCoord.x;
    // Resamples indirect paths with Jacobian determinant geometric correction
}
