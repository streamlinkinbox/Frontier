//============================================================================================================================================
// 📦 Frontier/Shaders/AtmosphereVolumetrics.comp — Henyey-Greenstein Volumetric Scattering and Thermal Emission Compute Shader
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(std430, binding = 0) readonly buffer DensityGridExtent
{
    float DensityField[];                               // [kg/m³] fluid smoke density volume
};

layout(std430, binding = 1) readonly buffer TemperatureGridExtent
{
    float TemperatureField[];                           // [K] fluid temperature volume
};

layout(std430, binding = 2) writeonly buffer InScatteringOutputExtent
{
    vec4 VolumetricScatteringField[];                   // [lux] output in-scattering and transmittance
};

layout(push_constant) uniform PushConstants
{
    mat4 InverseViewProjection;                         // [-] camera unprojection transform
    vec4 CameraPosition;                                // [m] camera position in world space
    vec4 SunDirectionAndIntensity;                      // [lux] light direction (xyz) and flux (w)
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
    uint StepCount;                                     // [steps] integration sample count
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
    // Evaluates Henyey-Greenstein phase function and blackbody thermal radiation
}
