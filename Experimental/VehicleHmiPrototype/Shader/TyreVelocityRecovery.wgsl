struct Slate_TyreStepPush_std140_0
{
    @align(16) ParticleCount_0 : u32,
    @align(4) Rings_0 : u32,
    @align(8) Segments_0 : u32,
    @align(4) TyreCount_0 : u32,
    @align(16) SubstepSeconds_0 : f32,
    @align(4) SubstepIndex_0 : u32,
    @align(8) SubstepCount_0 : u32,
    @align(4) IterationIndex_0 : u32,
    @align(16) PassIndex_0 : u32,
    @align(4) CordOffset_0 : u32,
    @align(8) CordCount_0 : u32,
};

@binding(14) @group(0) var<uniform> Step_0 : Slate_TyreStepPush_std140_0;
struct Slate_TyreHubRecord_std430_0
{
    @align(4) BeginX_0 : f32,
    @align(4) BeginY_0 : f32,
    @align(4) BeginZ_0 : f32,
    @align(4) BeginRotationX_0 : f32,
    @align(4) BeginRotationY_0 : f32,
    @align(4) BeginRotationZ_0 : f32,
    @align(4) BeginRotationW_0 : f32,
    @align(4) EndX_0 : f32,
    @align(4) EndY_0 : f32,
    @align(4) EndZ_0 : f32,
    @align(4) EndRotationX_0 : f32,
    @align(4) EndRotationY_0 : f32,
    @align(4) EndRotationZ_0 : f32,
    @align(4) EndRotationW_0 : f32,
    @align(4) LinearVelocityX_0 : f32,
    @align(4) LinearVelocityY_0 : f32,
    @align(4) LinearVelocityZ_0 : f32,
    @align(4) AngularVelocityX_0 : f32,
    @align(4) AngularVelocityY_0 : f32,
    @align(4) AngularVelocityZ_0 : f32,
    @align(4) RestVolume_0 : f32,
    @align(4) GaugePressure_0 : f32,
    @align(4) InflationCompliance_0 : f32,
    @align(4) GroundHeight_0 : f32,
    @align(4) StaticFriction_0 : f32,
    @align(4) KineticFriction_0 : f32,
    @align(4) DeformationDamping_0 : f32,
    @align(4) GravityY_0 : f32,
    @align(4) ContactOffset_0 : f32,
    @align(4) AdiabaticIndex_0 : f32,
    @align(4) AmbientPressure_0 : f32,
    @align(4) Reserved_0 : f32,
};

@binding(7) @group(0) var<storage, read> Hubs_0 : array<Slate_TyreHubRecord_std430_0>;

struct Slate_TyreParticleRecord_std430_0
{
    @align(4) X_0 : f32,
    @align(4) Y_0 : f32,
    @align(4) Z_0 : f32,
    @align(4) InverseMass_0 : f32,
};

@binding(0) @group(0) var<storage, read_write> Particles_0 : array<Slate_TyreParticleRecord_std430_0>;

struct Slate_TyreMotionRecord_std430_0
{
    @align(4) PreviousX_0 : f32,
    @align(4) PreviousY_0 : f32,
    @align(4) PreviousZ_0 : f32,
    @align(4) ContactMultiplier_0 : f32,
};

@binding(1) @group(0) var<storage, read_write> Motions_0 : array<Slate_TyreMotionRecord_std430_0>;

struct Slate_TyreVelocityRecord_std430_0
{
    @align(4) VelocityX_0 : f32,
    @align(4) VelocityY_0 : f32,
    @align(4) VelocityZ_0 : f32,
    @align(4) FrictionMultiplier_0 : f32,
};

@binding(2) @group(0) var<storage, read_write> Velocities_0 : array<Slate_TyreVelocityRecord_std430_0>;

fn Slate_ResolveLatticePosition_0( Particle_0 : u32,  Tyre_0 : ptr<function, u32>,  Ring_0 : ptr<function, u32>,  Segment_0 : ptr<function, u32>)
{
    var PerTyre_0 : u32 = Step_0.Rings_0 * Step_0.Segments_0;
    var _S1 : u32 = Particle_0 / PerTyre_0;
    (*Tyre_0) = _S1;
    var _S2 : u32 = (Particle_0 - _S1 * PerTyre_0) / Step_0.Segments_0;
    (*Ring_0) = _S2;
    (*Segment_0) = Particle_0 - (*Tyre_0) * PerTyre_0 - _S2 * Step_0.Segments_0;
    return;
}

fn Slate_RigidPointVelocity_0( Hub_0 : ptr<function, Slate_TyreHubRecord_std430_0>,  X_1 : f32,  Y_1 : f32,  Z_1 : f32,  VelocityX_1 : ptr<function, f32>,  VelocityY_1 : ptr<function, f32>,  VelocityZ_1 : ptr<function, f32>)
{
    var ArmX_0 : f32 = X_1 - (*Hub_0).EndX_0;
    var ArmY_0 : f32 = Y_1 - (*Hub_0).EndY_0;
    var ArmZ_0 : f32 = Z_1 - (*Hub_0).EndZ_0;
    var _S3 : f32 = (*Hub_0).AngularVelocityY_0;
    var _S4 : f32 = (*Hub_0).AngularVelocityZ_0;
    (*VelocityX_1) = (*Hub_0).LinearVelocityX_0 + (*Hub_0).AngularVelocityY_0 * ArmZ_0 - (*Hub_0).AngularVelocityZ_0 * ArmY_0;
    var _S5 : f32 = (*Hub_0).AngularVelocityX_0;
    (*VelocityY_1) = (*Hub_0).LinearVelocityY_0 + _S4 * ArmX_0 - (*Hub_0).AngularVelocityX_0 * ArmZ_0;
    (*VelocityZ_1) = (*Hub_0).LinearVelocityZ_0 + _S5 * ArmY_0 - _S3 * ArmX_0;
    return;
}

fn Slate_RecoverVelocity_0( X_2 : f32,  Y_2 : f32,  Z_2 : f32,  PreviousX_1 : f32,  PreviousY_1 : f32,  PreviousZ_1 : f32,  RigidX_0 : f32,  RigidY_0 : f32,  RigidZ_0 : f32,  Damping_0 : f32,  Substep_0 : f32,  VelocityX_2 : ptr<function, f32>,  VelocityY_2 : ptr<function, f32>,  VelocityZ_2 : ptr<function, f32>)
{
    var Inverse_0 : f32 = 1.0f / Substep_0;
    var Retained_0 : f32 = 1.0f - Damping_0 * Substep_0;
    var Retained_1 : f32;
    if(Retained_0 < 0.0f)
    {
        Retained_1 = 0.0f;
    }
    else
    {
        Retained_1 = Retained_0;
    }
    (*VelocityX_2) = RigidX_0 + ((X_2 - PreviousX_1) * Inverse_0 - RigidX_0) * Retained_1;
    (*VelocityY_2) = RigidY_0 + ((Y_2 - PreviousY_1) * Inverse_0 - RigidY_0) * Retained_1;
    (*VelocityZ_2) = RigidZ_0 + ((Z_2 - PreviousZ_1) * Inverse_0 - RigidZ_0) * Retained_1;
    return;
}

struct Slate_TyreVelocityRecord_0
{
     VelocityX_0 : f32,
     VelocityY_0 : f32,
     VelocityZ_0 : f32,
     FrictionMultiplier_0 : f32,
};

struct Slate_TyreMotionRecord_0
{
     PreviousX_0 : f32,
     PreviousY_0 : f32,
     PreviousZ_0 : f32,
     ContactMultiplier_0 : f32,
};

@compute
@workgroup_size(128, 1, 1)
fn main(@builtin(global_invocation_id) Dispatched_0 : vec3<u32>)
{
    var Particle_1 : u32 = Dispatched_0.x;
    if(Particle_1 >= (Step_0.ParticleCount_0))
    {
        return;
    }
    var Tyre_1 : u32 = u32(0);
    var Ring_1 : u32 = u32(0);
    var Segment_1 : u32 = u32(0);
    Slate_ResolveLatticePosition_0(Particle_1, &(Tyre_1), &(Ring_1), &(Segment_1));
    var _S6 : Slate_TyreHubRecord_std430_0 = Hubs_0[Tyre_1];
    var Current_0 : Slate_TyreParticleRecord_std430_0 = Particles_0[Particle_1];
    var Motion_0 : Slate_TyreMotionRecord_std430_0 = Motions_0[Particle_1];
    var RigidX_1 : f32 = 0.0f;
    var RigidY_1 : f32 = 0.0f;
    var RigidZ_1 : f32 = 0.0f;
    Slate_RigidPointVelocity_0(&(_S6), Particles_0[Particle_1].X_0, Particles_0[Particle_1].Y_0, Particles_0[Particle_1].Z_0, &(RigidX_1), &(RigidY_1), &(RigidZ_1));
    var Velocity_0 : Slate_TyreVelocityRecord_0;
    Velocity_0.VelocityX_0 = Velocities_0[Particle_1].VelocityX_0;
    Velocity_0.VelocityY_0 = Velocities_0[Particle_1].VelocityY_0;
    Velocity_0.VelocityZ_0 = Velocities_0[Particle_1].VelocityZ_0;
    Velocity_0.FrictionMultiplier_0 = Velocities_0[Particle_1].FrictionMultiplier_0;
    if((Particles_0[Particle_1].InverseMass_0) <= 0.0f)
    {
        Velocity_0.VelocityX_0 = RigidX_1;
        Velocity_0.VelocityY_0 = RigidY_1;
        Velocity_0.VelocityZ_0 = RigidZ_1;
    }
    else
    {
        var _S7 : f32 = Velocity_0.VelocityX_0;
        var _S8 : f32 = Velocity_0.VelocityY_0;
        var _S9 : f32 = Velocity_0.VelocityZ_0;
        Slate_RecoverVelocity_0(Current_0.X_0, Current_0.Y_0, Current_0.Z_0, Motion_0.PreviousX_0, Motion_0.PreviousY_0, Motion_0.PreviousZ_0, RigidX_1, RigidY_1, RigidZ_1, _S6.DeformationDamping_0, Step_0.SubstepSeconds_0, &(_S7), &(_S8), &(_S9));
        Velocity_0.VelocityX_0 = _S7;
        Velocity_0.VelocityY_0 = _S8;
        Velocity_0.VelocityZ_0 = _S9;
    }
    Velocities_0[Particle_1].VelocityX_0 = Velocity_0.VelocityX_0;
    Velocities_0[Particle_1].VelocityY_0 = Velocity_0.VelocityY_0;
    Velocities_0[Particle_1].VelocityZ_0 = Velocity_0.VelocityZ_0;
    Velocities_0[Particle_1].FrictionMultiplier_0 = Velocity_0.FrictionMultiplier_0;
    var Started_0 : Slate_TyreMotionRecord_0;
    Started_0.PreviousX_0 = Motion_0.PreviousX_0;
    Started_0.PreviousY_0 = Motion_0.PreviousY_0;
    Started_0.PreviousZ_0 = Motion_0.PreviousZ_0;
    Started_0.ContactMultiplier_0 = Motion_0.ContactMultiplier_0;
    Started_0.PreviousX_0 = Current_0.X_0;
    Started_0.PreviousY_0 = Current_0.Y_0;
    Started_0.PreviousZ_0 = Current_0.Z_0;
    Motions_0[Particle_1].PreviousX_0 = Started_0.PreviousX_0;
    Motions_0[Particle_1].PreviousY_0 = Started_0.PreviousY_0;
    Motions_0[Particle_1].PreviousZ_0 = Started_0.PreviousZ_0;
    Motions_0[Particle_1].ContactMultiplier_0 = Started_0.ContactMultiplier_0;
    return;
}

