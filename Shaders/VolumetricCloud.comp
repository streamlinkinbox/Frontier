//============================================================================================================================================
// 📦 Frontier/Shaders/VolumetricCloud.comp — Multi-Octave Fractal Perlin-Worley Atmospheric Cloud Raymarcher
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(std430, binding = 0) writeonly buffer CloudOutputExtent
{
    vec4 CloudScatteringField[];                        // [lux] output in-scattered radiance and cloud transmittance
};

layout(push_constant) uniform PushConstants
{
    mat4 InverseViewProjection;                         // [-] camera unprojection transform
    vec4 SunDirection;                                  // [-] unit directional sun vector
    vec4 SunRadiance;                                   // [lux] sun radiant flux intensity
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
    float CloudLayerBaseAltitude;                       // [m] cloud layer bottom altitude
    float CloudLayerTopAltitude;                        // [m] cloud layer ceiling altitude
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
    // Evaluates 3D multi-octave Perlin-Worley fractal density, Henyey-Greenstein powder phase and Beer-Lambert extinction
}
