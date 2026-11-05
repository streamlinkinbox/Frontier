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

struct Slate_TyreRestRecord_std430_0
{
    @align(4) LocalX_0 : f32,
    @align(4) LocalY_0 : f32,
    @align(4) LocalZ_0 : f32,
    @align(4) TyreIndex_0 : f32,
};

@binding(3) @group(0) var<storage, read> Rests_0 : array<Slate_TyreRestRecord_std430_0>;

@binding(8) @group(0) var<storage, read_write> Accumulators_0 : array<atomic<i32>>;

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

fn Slate_TyreLeadingParticle_0( Ring_1 : u32,  Segment_1 : u32) -> bool
{
    var _S3 : bool;
    if(Ring_1 == u32(0))
    {
        _S3 = Segment_1 == u32(0);
    }
    else
    {
        _S3 = false;
    }
    return _S3;
}

fn Slate_SubstepFraction_0() -> f32
{
    return f32(Step_0.SubstepIndex_0 + u32(1)) / f32(Step_0.SubstepCount_0);
}

fn SquareRoot_0( Operand_0 : f32) -> f32
{
    return sqrt(Operand_0);
}

fn Slate_InterpolateHubPose_0( Hub_0 : ptr<function, Slate_TyreHubRecord_std430_0>,  Fraction_0 : f32,  CentreX_0 : ptr<function, f32>,  CentreY_0 : ptr<function, f32>,  CentreZ_0 : ptr<function, f32>,  RotationX_0 : ptr<function, f32>,  RotationY_0 : ptr<function, f32>,  RotationZ_0 : ptr<function, f32>,  RotationW_0 : ptr<function, f32>)
{
    (*CentreX_0) = (*Hub_0).BeginX_0 + ((*Hub_0).EndX_0 - (*Hub_0).BeginX_0) * Fraction_0;
    (*CentreY_0) = (*Hub_0).BeginY_0 + ((*Hub_0).EndY_0 - (*Hub_0).BeginY_0) * Fraction_0;
    (*CentreZ_0) = (*Hub_0).BeginZ_0 + ((*Hub_0).EndZ_0 - (*Hub_0).BeginZ_0) * Fraction_0;
    var _S4 : f32 = (*Hub_0).BeginRotationX_0;
    var _S5 : f32 = (*Hub_0).EndRotationX_0;
    var _S6 : f32 = (*Hub_0).BeginRotationY_0;
    var _S7 : f32 = (*Hub_0).EndRotationY_0;
    var _S8 : f32 = (*Hub_0).BeginRotationZ_0;
    var _S9 : f32 = (*Hub_0).EndRotationZ_0;
    var _S10 : f32 = (*Hub_0).BeginRotationW_0;
    var _S11 : f32 = (*Hub_0).EndRotationW_0;
    var Sign_0 : f32;
    if(((*Hub_0).BeginRotationX_0 * (*Hub_0).EndRotationX_0 + (*Hub_0).BeginRotationY_0 * (*Hub_0).EndRotationY_0 + (*Hub_0).BeginRotationZ_0 * (*Hub_0).EndRotationZ_0 + (*Hub_0).BeginRotationW_0 * (*Hub_0).EndRotationW_0) < 0.0f)
    {
        Sign_0 = -1.0f;
    }
    else
    {
        Sign_0 = 1.0f;
    }
    var MixedX_0 : f32 = _S4 + (Sign_0 * _S5 - _S4) * Fraction_0;
    var MixedY_0 : f32 = _S6 + (Sign_0 * _S7 - _S6) * Fraction_0;
    var MixedZ_0 : f32 = _S8 + (Sign_0 * _S9 - _S8) * Fraction_0;
    var MixedW_0 : f32 = _S10 + (Sign_0 * _S11 - _S10) * Fraction_0;
    var Length_0 : f32 = SquareRoot_0(MixedX_0 * MixedX_0 + MixedY_0 * MixedY_0 + MixedZ_0 * MixedZ_0 + MixedW_0 * MixedW_0);
    var Scale_0 : f32;
    if(Length_0 > 9.999999960041972e-13f)
    {
        Scale_0 = 1.0f / Length_0;
    }
    else
    {
        Scale_0 = 1.0f;
    }
    (*RotationX_0) = MixedX_0 * Scale_0;
    (*RotationY_0) = MixedY_0 * Scale_0;
    (*RotationZ_0) = MixedZ_0 * Scale_0;
    (*RotationW_0) = MixedW_0 * Scale_0;
    return;
}

fn Slate_RotateByQuaternion_0( QuaternionX_0 : f32,  QuaternionY_0 : f32,  QuaternionZ_0 : f32,  QuaternionW_0 : f32,  X_1 : f32,  Y_1 : f32,  Z_1 : f32,  RotatedX_0 : ptr<function, f32>,  RotatedY_0 : ptr<function, f32>,  RotatedZ_0 : ptr<function, f32>)
{
    var CrossX_0 : f32 = QuaternionY_0 * Z_1 - QuaternionZ_0 * Y_1 + QuaternionW_0 * X_1;
    var CrossY_0 : f32 = QuaternionZ_0 * X_1 - QuaternionX_0 * Z_1 + QuaternionW_0 * Y_1;
    var CrossZ_0 : f32 = QuaternionX_0 * Y_1 - QuaternionY_0 * X_1 + QuaternionW_0 * Z_1;
    (*RotatedX_0) = X_1 + 2.0f * (QuaternionY_0 * CrossZ_0 - QuaternionZ_0 * CrossY_0);
    (*RotatedY_0) = Y_1 + 2.0f * (QuaternionZ_0 * CrossX_0 - QuaternionX_0 * CrossZ_0);
    (*RotatedZ_0) = Z_1 + 2.0f * (QuaternionX_0 * CrossY_0 - QuaternionY_0 * CrossX_0);
    return;
}

fn Slate_PredictParticle_0( X_2 : f32,  Y_2 : f32,  Z_2 : f32,  VelocityX_1 : f32,  VelocityY_1 : f32,  VelocityZ_1 : f32,  GravityY_1 : f32,  Substep_0 : f32,  PredictedX_0 : ptr<function, f32>,  PredictedY_0 : ptr<function, f32>,  PredictedZ_0 : ptr<function, f32>)
{
    (*PredictedX_0) = X_2 + VelocityX_1 * Substep_0;
    (*PredictedY_0) = Y_2 + (VelocityY_1 + GravityY_1 * Substep_0) * Substep_0;
    (*PredictedZ_0) = Z_2 + VelocityZ_1 * Substep_0;
    return;
}

struct Slate_TyreVelocityRecord_0
{
     VelocityX_0 : f32,
     VelocityY_0 : f32,
     VelocityZ_0 : f32,
     FrictionMultiplier_0 : f32,
};

struct Slate_TyreParticleRecord_0
{
     X_0 : f32,
     Y_0 : f32,
     Z_0 : f32,
     InverseMass_0 : f32,
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
    var Ring_2 : u32 = u32(0);
    var Segment_2 : u32 = u32(0);
    Slate_ResolveLatticePosition_0(Particle_1, &(Tyre_1), &(Ring_2), &(Segment_2));
    var _S12 : Slate_TyreHubRecord_std430_0 = Hubs_0[Tyre_1];
    var Current_0 : Slate_TyreParticleRecord_std430_0 = Particles_0[Particle_1];
    var Rest_0 : Slate_TyreRestRecord_std430_0 = Rests_0[Particle_1];
    var _S13 : bool;
    if((Step_0.SubstepIndex_0) == u32(0))
    {
        _S13 = Slate_TyreLeadingParticle_0(Ring_2, Segment_2);
    }
    else
    {
        _S13 = false;
    }
    if(_S13)
    {
        var Base_0 : u32 = Tyre_1 * u32(16);
        var Slot_0 : u32 = u32(0);
        for(;;)
        {
            if(Slot_0 < u32(16))
            {
            }
            else
            {
                break;
            }
            atomicStore(&(Accumulators_0[Base_0 + Slot_0]), i32(0));
            Slot_0 = Slot_0 + u32(1);
        }
        atomicStore(&(Accumulators_0[Base_0 + u32(7)]), i32(2147483647));
        atomicStore(&(Accumulators_0[Base_0 + u32(8)]), i32(-2147483647));
        atomicStore(&(Accumulators_0[Base_0 + u32(9)]), i32(2147483647));
        atomicStore(&(Accumulators_0[Base_0 + u32(10)]), i32(-2147483647));
    }
    Motions_0[Particle_1].ContactMultiplier_0 = 0.0f;
    var Velocity_0 : Slate_TyreVelocityRecord_0;
    Velocity_0.VelocityX_0 = Velocities_0[Particle_1].VelocityX_0;
    Velocity_0.VelocityY_0 = Velocities_0[Particle_1].VelocityY_0;
    Velocity_0.VelocityZ_0 = Velocities_0[Particle_1].VelocityZ_0;
    Velocity_0.FrictionMultiplier_0 = Velocities_0[Particle_1].FrictionMultiplier_0;
    Velocity_0.FrictionMultiplier_0 = 0.0f;
    var Predicted_0 : Slate_TyreParticleRecord_0;
    Predicted_0.X_0 = Current_0.X_0;
    Predicted_0.Y_0 = Current_0.Y_0;
    Predicted_0.Z_0 = Current_0.Z_0;
    Predicted_0.InverseMass_0 = Current_0.InverseMass_0;
    if((Current_0.InverseMass_0) <= 0.0f)
    {
        var CentreX_1 : f32 = 0.0f;
        var CentreY_1 : f32 = 0.0f;
        var CentreZ_1 : f32 = 0.0f;
        var RotationX_1 : f32 = 0.0f;
        var RotationY_1 : f32 = 0.0f;
        var RotationZ_1 : f32 = 0.0f;
        var RotationW_1 : f32 = 1.0f;
        Slate_InterpolateHubPose_0(&(_S12), Slate_SubstepFraction_0(), &(CentreX_1), &(CentreY_1), &(CentreZ_1), &(RotationX_1), &(RotationY_1), &(RotationZ_1), &(RotationW_1));
        var CarriedX_0 : f32 = 0.0f;
        var CarriedY_0 : f32 = 0.0f;
        var CarriedZ_0 : f32 = 0.0f;
        Slate_RotateByQuaternion_0(RotationX_1, RotationY_1, RotationZ_1, RotationW_1, Rest_0.LocalX_0, Rest_0.LocalY_0, Rest_0.LocalZ_0, &(CarriedX_0), &(CarriedY_0), &(CarriedZ_0));
        Predicted_0.X_0 = CentreX_1 + CarriedX_0;
        Predicted_0.Y_0 = CentreY_1 + CarriedY_0;
        Predicted_0.Z_0 = CentreZ_1 + CarriedZ_0;
    }
    else
    {
        var PredictedX_1 : f32 = 0.0f;
        var PredictedY_1 : f32 = 0.0f;
        var PredictedZ_1 : f32 = 0.0f;
        Slate_PredictParticle_0(Current_0.X_0, Current_0.Y_0, Current_0.Z_0, Velocity_0.VelocityX_0, Velocity_0.VelocityY_0, Velocity_0.VelocityZ_0, _S12.GravityY_0, Step_0.SubstepSeconds_0, &(PredictedX_1), &(PredictedY_1), &(PredictedZ_1));
        Predicted_0.X_0 = PredictedX_1;
        Predicted_0.Y_0 = PredictedY_1;
        Predicted_0.Z_0 = PredictedZ_1;
    }
    Particles_0[Particle_1].X_0 = Predicted_0.X_0;
    Particles_0[Particle_1].Y_0 = Predicted_0.Y_0;
    Particles_0[Particle_1].Z_0 = Predicted_0.Z_0;
    Particles_0[Particle_1].InverseMass_0 = Predicted_0.InverseMass_0;
    Velocities_0[Particle_1].VelocityX_0 = Velocity_0.VelocityX_0;
    Velocities_0[Particle_1].VelocityY_0 = Velocity_0.VelocityY_0;
    Velocities_0[Particle_1].VelocityZ_0 = Velocity_0.VelocityZ_0;
    Velocities_0[Particle_1].FrictionMultiplier_0 = Velocity_0.FrictionMultiplier_0;
    return;
}

