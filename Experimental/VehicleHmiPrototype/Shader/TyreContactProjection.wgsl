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

struct Slate_TyreGradientRecord_std430_0
{
    @align(4) VolumeX_0 : f32,
    @align(4) VolumeY_0 : f32,
    @align(4) VolumeZ_0 : f32,
    @align(4) OwnedVolume_0 : f32,
    @align(4) RingX_0 : f32,
    @align(4) RingY_0 : f32,
    @align(4) RingZ_0 : f32,
    @align(4) RingEdge_0 : f32,
    @align(4) MeridianX_0 : f32,
    @align(4) MeridianY_0 : f32,
    @align(4) MeridianZ_0 : f32,
    @align(4) MeridianEdge_0 : f32,
};

@binding(4) @group(0) var<storage, read_write> Gradients_0 : array<Slate_TyreGradientRecord_std430_0>;

@binding(12) @group(0) var<storage, read_write> ShapeAdvances_0 : array<f32>;

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

@binding(8) @group(0) var<storage, read_write> Accumulators_0 : array<atomic<i32>>;

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

fn SquareRoot_0( Operand_0 : f32) -> f32
{
    return sqrt(Operand_0);
}

fn Slate_ProjectGroundContact_0( X_1 : f32,  Y_1 : f32,  Z_1 : f32,  InverseMass_1 : f32,  PreviousX_1 : f32,  PreviousZ_1 : f32,  GroundHeight_1 : f32,  ContactOffset_1 : f32,  StaticFriction_1 : f32,  KineticFriction_1 : f32,  CorrectionX_0 : ptr<function, f32>,  CorrectionY_0 : ptr<function, f32>,  CorrectionZ_0 : ptr<function, f32>,  NormalMultiplier_0 : ptr<function, f32>,  FrictionMultiplier_1 : ptr<function, f32>)
{
    (*CorrectionX_0) = 0.0f;
    (*CorrectionY_0) = 0.0f;
    (*CorrectionZ_0) = 0.0f;
    (*NormalMultiplier_0) = 0.0f;
    (*FrictionMultiplier_1) = 0.0f;
    var Penetration_0 : f32 = Y_1 - ContactOffset_1 - GroundHeight_1;
    var _S3 : bool;
    if(Penetration_0 >= 0.0f)
    {
        _S3 = true;
    }
    else
    {
        _S3 = InverseMass_1 <= 0.0f;
    }
    if(_S3)
    {
        return;
    }
    var Lift_0 : f32 = - Penetration_0;
    (*CorrectionY_0) = Lift_0;
    (*NormalMultiplier_0) = Lift_0 / InverseMass_1;
    var SlideX_0 : f32 = X_1 - PreviousX_1;
    var SlideZ_0 : f32 = Z_1 - PreviousZ_1;
    var Slide_0 : f32 = SquareRoot_0(SlideX_0 * SlideX_0 + SlideZ_0 * SlideZ_0);
    if(Slide_0 < 9.99999971718068537e-10f)
    {
        return;
    }
    var Removed_0 : f32;
    if(Slide_0 >= (StaticFriction_1 * Lift_0))
    {
        Removed_0 = KineticFriction_1 * Lift_0 / Slide_0;
    }
    else
    {
        Removed_0 = 1.0f;
    }
    if(Removed_0 > 1.0f)
    {
        Removed_0 = 1.0f;
    }
    (*CorrectionX_0) = - SlideX_0 * Removed_0;
    (*CorrectionZ_0) = - SlideZ_0 * Removed_0;
    (*FrictionMultiplier_1) = Slide_0 * Removed_0 / InverseMass_1;
    return;
}

fn Slate_ForceOfMultiplier_0( Multiplier_0 : f32,  Substep_0 : f32) -> f32
{
    return Multiplier_0 / (Substep_0 * Substep_0);
}

fn Slate_SubstepFraction_0() -> f32
{
    return f32(Step_0.SubstepIndex_0 + u32(1)) / f32(Step_0.SubstepCount_0);
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

fn Slate_EncodeFixed_0( Magnitude_0 : f32,  Scale_1 : f32) -> i32
{
    var Scaled_0 : f32 = Magnitude_0 * Scale_1;
    if(Scaled_0 >= 2.14748352e+09f)
    {
        return i32(2147483520);
    }
    if(Scaled_0 <= -2.14748352e+09f)
    {
        return i32(-2147483520);
    }
    return i32(Scaled_0);
}

struct Slate_TyreParticleRecord_0
{
     X_0 : f32,
     Y_0 : f32,
     Z_0 : f32,
     InverseMass_0 : f32,
};

struct Slate_TyreMotionRecord_0
{
     PreviousX_0 : f32,
     PreviousY_0 : f32,
     PreviousZ_0 : f32,
     ContactMultiplier_0 : f32,
};

struct Slate_TyreVelocityRecord_0
{
     VelocityX_0 : f32,
     VelocityY_0 : f32,
     VelocityZ_0 : f32,
     FrictionMultiplier_0 : f32,
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
    var _S12 : Slate_TyreHubRecord_std430_0 = Hubs_0[Tyre_1];
    var Base_0 : u32 = Tyre_1 * u32(16);
    var Current_0 : Slate_TyreParticleRecord_0;
    Current_0.X_0 = Particles_0[Particle_1].X_0;
    Current_0.Y_0 = Particles_0[Particle_1].Y_0;
    Current_0.Z_0 = Particles_0[Particle_1].Z_0;
    Current_0.InverseMass_0 = Particles_0[Particle_1].InverseMass_0;
    if((Current_0.InverseMass_0) <= 0.0f)
    {
        return;
    }
    var Rows_0 : u32 = Tyre_1 * u32(96);
    var _S13 : u32 = Rows_0 + u32(1);
    var _S14 : f32 = Current_0.X_0 + Current_0.InverseMass_0 * (ShapeAdvances_0[Rows_0] * Gradients_0[Particle_1].VolumeX_0 + ShapeAdvances_0[_S13 + Ring_1] * Gradients_0[Particle_1].RingX_0 + ShapeAdvances_0[_S13 + Step_0.Rings_0 + Segment_1] * Gradients_0[Particle_1].MeridianX_0);
    var _S15 : f32 = Current_0.Y_0 + Current_0.InverseMass_0 * (ShapeAdvances_0[Rows_0] * Gradients_0[Particle_1].VolumeY_0 + ShapeAdvances_0[_S13 + Ring_1] * Gradients_0[Particle_1].RingY_0 + ShapeAdvances_0[_S13 + Step_0.Rings_0 + Segment_1] * Gradients_0[Particle_1].MeridianY_0);
    var _S16 : f32 = Current_0.Z_0 + Current_0.InverseMass_0 * (ShapeAdvances_0[Rows_0] * Gradients_0[Particle_1].VolumeZ_0 + ShapeAdvances_0[_S13 + Ring_1] * Gradients_0[Particle_1].RingZ_0 + ShapeAdvances_0[_S13 + Step_0.Rings_0 + Segment_1] * Gradients_0[Particle_1].MeridianZ_0);
    var Motion_0 : Slate_TyreMotionRecord_0;
    Motion_0.PreviousX_0 = Motions_0[Particle_1].PreviousX_0;
    Motion_0.PreviousY_0 = Motions_0[Particle_1].PreviousY_0;
    Motion_0.PreviousZ_0 = Motions_0[Particle_1].PreviousZ_0;
    Motion_0.ContactMultiplier_0 = Motions_0[Particle_1].ContactMultiplier_0;
    var Velocity_0 : Slate_TyreVelocityRecord_0;
    Velocity_0.VelocityX_0 = Velocities_0[Particle_1].VelocityX_0;
    Velocity_0.VelocityY_0 = Velocities_0[Particle_1].VelocityY_0;
    Velocity_0.VelocityZ_0 = Velocities_0[Particle_1].VelocityZ_0;
    Velocity_0.FrictionMultiplier_0 = Velocities_0[Particle_1].FrictionMultiplier_0;
    var Penetration_1 : f32 = _S12.GroundHeight_0 + _S12.ContactOffset_0 - _S15;
    var CorrectionX_1 : f32 = 0.0f;
    var CorrectionY_1 : f32 = 0.0f;
    var CorrectionZ_1 : f32 = 0.0f;
    var NormalMultiplier_1 : f32 = 0.0f;
    var FrictionMultiplier_2 : f32 = 0.0f;
    Slate_ProjectGroundContact_0(_S14, _S15, _S16, Current_0.InverseMass_0, Motion_0.PreviousX_0, Motion_0.PreviousZ_0, _S12.GroundHeight_0, _S12.ContactOffset_0, _S12.StaticFriction_0, _S12.KineticFriction_0, &(CorrectionX_1), &(CorrectionY_1), &(CorrectionZ_1), &(NormalMultiplier_1), &(FrictionMultiplier_2));
    Current_0.X_0 = _S14 + CorrectionX_1;
    Current_0.Y_0 = _S15 + CorrectionY_1;
    Current_0.Z_0 = _S16 + CorrectionZ_1;
    Particles_0[Particle_1].X_0 = Current_0.X_0;
    Particles_0[Particle_1].Y_0 = Current_0.Y_0;
    Particles_0[Particle_1].Z_0 = Current_0.Z_0;
    Particles_0[Particle_1].InverseMass_0 = Current_0.InverseMass_0;
    if(NormalMultiplier_1 <= 0.0f)
    {
        return;
    }
    Motion_0.ContactMultiplier_0 = Motion_0.ContactMultiplier_0 + NormalMultiplier_1;
    Velocity_0.FrictionMultiplier_0 = Velocity_0.FrictionMultiplier_0 + FrictionMultiplier_2;
    Motions_0[Particle_1].PreviousX_0 = Motion_0.PreviousX_0;
    Motions_0[Particle_1].PreviousY_0 = Motion_0.PreviousY_0;
    Motions_0[Particle_1].PreviousZ_0 = Motion_0.PreviousZ_0;
    Motions_0[Particle_1].ContactMultiplier_0 = Motion_0.ContactMultiplier_0;
    Velocities_0[Particle_1].VelocityX_0 = Velocity_0.VelocityX_0;
    Velocities_0[Particle_1].VelocityY_0 = Velocity_0.VelocityY_0;
    Velocities_0[Particle_1].VelocityZ_0 = Velocity_0.VelocityZ_0;
    Velocities_0[Particle_1].FrictionMultiplier_0 = Velocity_0.FrictionMultiplier_0;
    var Share_0 : f32 = 1.0f / f32(Step_0.SubstepCount_0);
    var ForceX_0 : f32 = Slate_ForceOfMultiplier_0(CorrectionX_1 / Current_0.InverseMass_0, Step_0.SubstepSeconds_0) * Share_0;
    var ForceY_0 : f32 = Slate_ForceOfMultiplier_0(NormalMultiplier_1, Step_0.SubstepSeconds_0) * Share_0;
    var ForceZ_0 : f32 = Slate_ForceOfMultiplier_0(CorrectionZ_1 / Current_0.InverseMass_0, Step_0.SubstepSeconds_0) * Share_0;
    var CentreX_1 : f32 = 0.0f;
    var CentreY_1 : f32 = 0.0f;
    var CentreZ_1 : f32 = 0.0f;
    var RotationX_1 : f32 = 0.0f;
    var RotationY_1 : f32 = 0.0f;
    var RotationZ_1 : f32 = 0.0f;
    var RotationW_1 : f32 = 1.0f;
    Slate_InterpolateHubPose_0(&(_S12), Slate_SubstepFraction_0(), &(CentreX_1), &(CentreY_1), &(CentreZ_1), &(RotationX_1), &(RotationY_1), &(RotationZ_1), &(RotationW_1));
    var ArmX_0 : f32 = Current_0.X_0 - CentreX_1;
    var ArmY_0 : f32 = Current_0.Y_0 - CentreY_1;
    var ArmZ_0 : f32 = Current_0.Z_0 - CentreZ_1;
    var _S17 : i32 = atomicAdd(&(Accumulators_0[Base_0]), Slate_EncodeFixed_0(ForceX_0, 4096.0f));
    var _S18 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(1)]), Slate_EncodeFixed_0(ForceY_0, 4096.0f));
    var _S19 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(2)]), Slate_EncodeFixed_0(ForceZ_0, 4096.0f));
    var _S20 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(3)]), Slate_EncodeFixed_0(ArmY_0 * ForceZ_0 - ArmZ_0 * ForceY_0, 4096.0f));
    var _S21 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(4)]), Slate_EncodeFixed_0(ArmZ_0 * ForceX_0 - ArmX_0 * ForceZ_0, 4096.0f));
    var _S22 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(5)]), Slate_EncodeFixed_0(ArmX_0 * ForceY_0 - ArmY_0 * ForceX_0, 4096.0f));
    var _S23 : i32 = atomicAdd(&(Accumulators_0[Base_0 + u32(6)]), i32(1));
    var _S24 : i32 = atomicMin(&(Accumulators_0[Base_0 + u32(7)]), Slate_EncodeFixed_0(Current_0.X_0, 1.0e+06f));
    var _S25 : i32 = atomicMax(&(Accumulators_0[Base_0 + u32(8)]), Slate_EncodeFixed_0(Current_0.X_0, 1.0e+06f));
    var _S26 : i32 = atomicMin(&(Accumulators_0[Base_0 + u32(9)]), Slate_EncodeFixed_0(Current_0.Z_0, 1.0e+06f));
    var _S27 : i32 = atomicMax(&(Accumulators_0[Base_0 + u32(10)]), Slate_EncodeFixed_0(Current_0.Z_0, 1.0e+06f));
    var _S28 : i32 = atomicMax(&(Accumulators_0[Base_0 + u32(11)]), Slate_EncodeFixed_0(Penetration_1, 1.0e+06f));
    return;
}

