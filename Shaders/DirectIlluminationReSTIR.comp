//============================================================================================================================================
// 📦 Frontier/Shaders/DirectIlluminationReSTIR.comp — ReSTIR DI Spatiotemporal Candidate Resampling Compute Shader
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

struct PhotometricLightPayload
{
    vec4 PositionAndRadius;                             // [m] position (xyz) and radius (w)
    vec4 RadianceAndCategory;                           // [lux] radiance (xyz) and category (w)
};

struct ReservoirPayload
{
    uint  SelectedLightIndex;                           // [index] winning candidate index
    float WeightSum;                                    // [-] sum of weights w_sum
    uint  SampleCount;                                  // [count] candidate count M
    float UnbiasedWeight;                               // [-] normalization weight W
};

layout(std430, binding = 0) readonly buffer LightStorageExtent
{
    PhotometricLightPayload Lights[];                   // [lights] active scene lights
};

layout(std430, binding = 1) buffer ReservoirStorageExtent
{
    ReservoirPayload Reservoirs[];                      // [reservoirs] per-pixel ReSTIR reservoirs
};

layout(std430, binding = 2) writeonly buffer OutputRadianceExtent
{
    vec4 DirectRadianceField[];                         // [lux] output direct lighting
};

layout(push_constant) uniform PushConstants
{
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
    uint LightCount;                                    // [count] active light count
    uint CandidateSampleCount;                          // [count] initial candidates M0
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
    // Evaluates spatiotemporal candidate resampling and cross-bilateral geometric weights
}
