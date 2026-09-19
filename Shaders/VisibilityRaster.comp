//============================================================================================================================================
// 📦 Frontier/Shaders/VisibilityRaster.comp — Tile Meshlet Cluster Software Compute Rasterizer Shader
//============================================================================================================================================

#version 460
#extension GL_EXT_shader_atomic_float : enable

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(std430, binding = 0) readonly buffer VertexStorageExtent
{
    float Vertices[];                                   // [m] packed vertex attribute components
};

layout(std430, binding = 1) readonly buffer TriangleIndexExtent
{
    uint Indices[];                                     // [indices] 32-bit triangle vertex indices
};

layout(std430, binding = 2) writeonly buffer VisibilityStorageExtent
{
    uint VisibilityField[];                             // [tokens] packed 32-bit visibility identifiers
};

layout(std430, binding = 3) buffer DepthStorageExtent
{
    uint DepthFieldAtomic[];                            // [depth] fixed-point 32-bit depth field
};

layout(push_constant) uniform PushConstants
{
    mat4 ViewProjection;                                // [-] concatenated camera transform
    uint ViewportWidth;                                 // [px] viewport width
    uint ViewportHeight;                                // [px] viewport height
    uint InstanceToken;                                 // [token] current meshlet instance token
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
    // Compute rasterizer checks sub-pixel edge equations against atomic depth
}
