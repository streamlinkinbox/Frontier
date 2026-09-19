//============================================================================================================================================
// 📦 Frontier/Shaders/FluidAdvection.comp — 3D Eulerian Navier-Stokes Velocity and Density Advection Compute Shader
//============================================================================================================================================

#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

layout(std430, binding = 0) readonly buffer VelocityInputExtent
{
    vec4 VelocityInput[];                               // [m/s] 3D velocity field input
};

layout(std430, binding = 1) writeonly buffer VelocityOutputExtent
{
    vec4 VelocityOutput[];                              // [m/s] 3D velocity field output
};

layout(std430, binding = 2) readonly buffer DensityInputExtent
{
    float DensityInput[];                               // [kg/m³] density field input
};

layout(std430, binding = 3) writeonly buffer DensityOutputExtent
{
    float DensityOutput[];                              // [kg/m³] density field output
};

layout(push_constant) uniform PushConstants
{
    uvec3 DomainDimensions;                             // [cells] 3D resolution (Nx, Ny, Nz)
    float CellWidth;                                    // [m] grid spacing dx
    float DeltaTimeSeconds;                             // [s] timestep dt
};

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPUTE MAIN INVOCATION
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec3 VoxelCoord = gl_GlobalInvocationID.xyz;
    if (VoxelCoord.x >= DomainDimensions.x || VoxelCoord.y >= DomainDimensions.y || VoxelCoord.z >= DomainDimensions.z)
    {
        return;
    }

    uint LinearVoxelIndex = VoxelCoord.z * (DomainDimensions.x * DomainDimensions.y) + VoxelCoord.y * DomainDimensions.x + VoxelCoord.x;
    // Performs semi-Lagrangian / MacCormack backtrace advection
}
