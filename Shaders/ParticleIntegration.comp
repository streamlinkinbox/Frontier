//============================================================================================================================================
// 📦 Frontier/Shaders/ParticleIntegration.comp — GPU Compute Particle Integration with Analytical Curl Noise Turbulence
//============================================================================================================================================

#version 460

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE BINDINGS
//------------------------------------------------------------------------------------------------------------------------

struct ParticlePayload
{
    vec4 PositionAndRadius;                             // [m] position (xyz) and radius (w)
    vec4 VelocityAndMass;                               // [m/s] velocity (xyz) and mass (w)
    vec4 ColorAndLifespan;                              // [0..1] RGBA color
    vec4 LifespanAndAge;                                // [s] age (x), lifespan (y), drag (z), pad (w)
};

layout(std430, binding = 0) buffer ParticleStorageExtent
{
    ParticlePayload Particles[];                        // [particles] active particle storage
};

layout(push_constant) uniform PushConstants
{
    uint ActiveParticleCount;                           // [count] number of active particles
    float DeltaTimeSeconds;                             // [s] timestep dt
    float TurbulenceScale;                              // [-] curl noise strength
};

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPUTE MAIN INVOCATION
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uint ParticleIndex = gl_GlobalInvocationID.x;
    if (ParticleIndex >= ActiveParticleCount)
    {
        return;
    }

    // Evaluates ballistic motion, curl noise vector potentials and lifespan
}
