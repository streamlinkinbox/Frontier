//============================================================================================================================================
// 📦 Frontier/Shaders/MaterialDecode.comp — Visibility Identifier Decoding and Material Surface Attribute Evaluation
//============================================================================================================================================

#version 460

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(std430, binding = 0) readonly buffer VisibilityStorageExtent
{
    uint VisibilityField[];                             // [tokens] packed 32-bit visibility identifiers
};

layout(std430, binding = 1) readonly buffer VertexStorageExtent
{
    float Vertices[];                                   // [m] vertex attribute storage
};

layout(std430, binding = 2) readonly buffer TriangleIndexExtent
{
    uint Indices[];                                     // [indices] 32-bit triangle vertex indices
};

layout(std430, binding = 3) writeonly buffer SurfaceAttributeExtent
{
    vec4 WorldPositions[];                              // [m] reconstructed world positions
};

layout(push_constant) uniform PushConstants
{
    mat4 InverseViewProjection;                         // [-] inverse view projection transform
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
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
    uint Ident = VisibilityField[LinearPixelIndex];

    if (Ident == 0xFFFFFFFFu)
    {
        return;
    }

    uint PrimitiveToken = Ident & 0x3FFFu;
    // Decodes triangle indices and evaluates analytic barycentrics
}
