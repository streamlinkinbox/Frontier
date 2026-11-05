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

@binding(10) @group(0) var<storage, read_write> ShapeSums_0 : array<atomic<i32>>;

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
    var _S3 : f32 = (*Hub_0).BeginRotationX_0;
    var _S4 : f32 = (*Hub_0).EndRotationX_0;
    var _S5 : f32 = (*Hub_0).BeginRotationY_0;
    var _S6 : f32 = (*Hub_0).EndRotationY_0;
    var _S7 : f32 = (*Hub_0).BeginRotationZ_0;
    var _S8 : f32 = (*Hub_0).EndRotationZ_0;
    var _S9 : f32 = (*Hub_0).BeginRotationW_0;
    var _S10 : f32 = (*Hub_0).EndRotationW_0;
    var Sign_0 : f32;
    if(((*Hub_0).BeginRotationX_0 * (*Hub_0).EndRotationX_0 + (*Hub_0).BeginRotationY_0 * (*Hub_0).EndRotationY_0 + (*Hub_0).BeginRotationZ_0 * (*Hub_0).EndRotationZ_0 + (*Hub_0).BeginRotationW_0 * (*Hub_0).EndRotationW_0) < 0.0f)
    {
        Sign_0 = -1.0f;
    }
    else
    {
        Sign_0 = 1.0f;
    }
    var MixedX_0 : f32 = _S3 + (Sign_0 * _S4 - _S3) * Fraction_0;
    var MixedY_0 : f32 = _S5 + (Sign_0 * _S6 - _S5) * Fraction_0;
    var MixedZ_0 : f32 = _S7 + (Sign_0 * _S8 - _S7) * Fraction_0;
    var MixedW_0 : f32 = _S9 + (Sign_0 * _S10 - _S9) * Fraction_0;
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

fn Slate_ResolveIncidentCorner_0( Triangle_0 : u32,  RingB_0 : ptr<function, i32>,  SegmentB_0 : ptr<function, i32>,  RingC_0 : ptr<function, i32>,  SegmentC_0 : ptr<function, i32>)
{
    if(Triangle_0 == u32(0))
    {
        (*RingB_0) = i32(1);
        (*SegmentB_0) = i32(0);
        (*RingC_0) = i32(1);
        (*SegmentC_0) = i32(1);
    }
    else
    {
        if(Triangle_0 == u32(1))
        {
            (*RingB_0) = i32(1);
            (*SegmentB_0) = i32(1);
            (*RingC_0) = i32(0);
            (*SegmentC_0) = i32(1);
        }
        else
        {
            if(Triangle_0 == u32(2))
            {
                (*RingB_0) = i32(0);
                (*SegmentB_0) = i32(1);
                (*RingC_0) = i32(-1);
                (*SegmentC_0) = i32(0);
            }
            else
            {
                if(Triangle_0 == u32(3))
                {
                    (*RingB_0) = i32(-1);
                    (*SegmentB_0) = i32(0);
                    (*RingC_0) = i32(-1);
                    (*SegmentC_0) = i32(-1);
                }
                else
                {
                    if(Triangle_0 == u32(4))
                    {
                        (*RingB_0) = i32(-1);
                        (*SegmentB_0) = i32(-1);
                        (*RingC_0) = i32(0);
                        (*SegmentC_0) = i32(-1);
                    }
                    else
                    {
                        (*RingB_0) = i32(0);
                        (*SegmentB_0) = i32(-1);
                        (*RingC_0) = i32(1);
                        (*SegmentC_0) = i32(0);
                    }
                }
            }
        }
    }
    return;
}

fn Slate_LatticeIndex_0( TyreIndex_0 : u32,  Ring_1 : i32,  Segment_1 : i32,  Rings_1 : u32,  Segments_1 : u32) -> u32
{
    var _S11 : i32 = i32(Segments_1);
    var _S12 : i32 = Segment_1 % _S11;
    var Wrapped_0 : i32 = (_S12 + _S11) % _S11;
    return TyreIndex_0 * Rings_1 * Segments_1 + u32(Ring_1) * Segments_1 + u32(Wrapped_0);
}

fn Slate_TriangleVolumeGradient_0( BX_0 : f32,  BY_0 : f32,  BZ_0 : f32,  CX_0 : f32,  CY_0 : f32,  CZ_0 : f32,  GradientX_0 : ptr<function, f32>,  GradientY_0 : ptr<function, f32>,  GradientZ_0 : ptr<function, f32>)
{
    (*GradientX_0) = (*GradientX_0) + (BY_0 * CZ_0 - BZ_0 * CY_0) / 6.0f;
    (*GradientY_0) = (*GradientY_0) + (BZ_0 * CX_0 - BX_0 * CZ_0) / 6.0f;
    (*GradientZ_0) = (*GradientZ_0) + (BX_0 * CY_0 - BY_0 * CX_0) / 6.0f;
    return;
}

fn Slate_TriangleSweptVolume_0( AX_0 : f32,  AY_0 : f32,  AZ_0 : f32,  BX_1 : f32,  BY_1 : f32,  BZ_1 : f32,  CX_1 : f32,  CY_1 : f32,  CZ_1 : f32) -> f32
{
    return (AX_0 * (BY_1 * CZ_1 - BZ_1 * CY_1) + AY_0 * (BZ_1 * CX_1 - BX_1 * CZ_1) + AZ_0 * (BX_1 * CY_1 - BY_1 * CX_1)) / 6.0f;
}

fn Slate_EdgeLengthGradient_0( X_1 : f32,  Y_1 : f32,  Z_1 : f32,  OtherX_0 : f32,  OtherY_0 : f32,  OtherZ_0 : f32,  GradientX_1 : ptr<function, f32>,  GradientY_1 : ptr<function, f32>,  GradientZ_1 : ptr<function, f32>) -> f32
{
    var DeltaX_0 : f32 = X_1 - OtherX_0;
    var DeltaY_0 : f32 = Y_1 - OtherY_0;
    var DeltaZ_0 : f32 = Z_1 - OtherZ_0;
    var Length_1 : f32 = SquareRoot_0(DeltaX_0 * DeltaX_0 + DeltaY_0 * DeltaY_0 + DeltaZ_0 * DeltaZ_0);
    if(Length_1 < 9.99999971718068537e-10f)
    {
        return 0.0f;
    }
    (*GradientX_1) = (*GradientX_1) + DeltaX_0 / Length_1;
    (*GradientY_1) = (*GradientY_1) + DeltaY_0 / Length_1;
    (*GradientZ_1) = (*GradientZ_1) + DeltaZ_0 / Length_1;
    return Length_1;
}

fn Slate_ShapeEntryScale_0( Row_0 : u32,  Column_0 : u32) -> f32
{
    var _S13 : bool = Row_0 == u32(0);
    var _S14 : bool;
    if(_S13)
    {
        _S14 = Column_0 == u32(0);
    }
    else
    {
        _S14 = false;
    }
    if(_S14)
    {
        return 4.0e+09f;
    }
    if(_S13)
    {
        _S14 = true;
    }
    else
    {
        _S14 = Column_0 == u32(0);
    }
    if(_S14)
    {
        return 2.68435456e+08f;
    }
    return 1.048576e+06f;
}

fn Slate_WeightedDot_0( Weight_0 : f32,  AX_1 : f32,  AY_1 : f32,  AZ_1 : f32,  BX_2 : f32,  BY_2 : f32,  BZ_2 : f32) -> f32
{
    return Weight_0 * (AX_1 * BX_2 + AY_1 * BY_2 + AZ_1 * BZ_2);
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

fn Slate_ShapeValueScale_0( Row_1 : u32) -> f32
{
    var _S15 : f32;
    if(Row_1 == u32(0))
    {
        _S15 = 5.0e+08f;
    }
    else
    {
        _S15 = 1.0e+06f;
    }
    return _S15;
}

struct Slate_TyreGradientRecord_0
{
     VolumeX_0 : f32,
     VolumeY_0 : f32,
     VolumeZ_0 : f32,
     OwnedVolume_0 : f32,
     RingX_0 : f32,
     RingY_0 : f32,
     RingZ_0 : f32,
     RingEdge_0 : f32,
     MeridianX_0 : f32,
     MeridianY_0 : f32,
     MeridianZ_0 : f32,
     MeridianEdge_0 : f32,
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
    var _S16 : Slate_TyreHubRecord_std430_0 = Hubs_0[Tyre_1];
    var CentreX_1 : f32 = 0.0f;
    var CentreY_1 : f32 = 0.0f;
    var CentreZ_1 : f32 = 0.0f;
    var RotationX_1 : f32 = 0.0f;
    var RotationY_1 : f32 = 0.0f;
    var RotationZ_1 : f32 = 0.0f;
    var RotationW_1 : f32 = 1.0f;
    Slate_InterpolateHubPose_0(&(_S16), Slate_SubstepFraction_0(), &(CentreX_1), &(CentreY_1), &(CentreZ_1), &(RotationX_1), &(RotationY_1), &(RotationZ_1), &(RotationW_1));
    var Own_0 : Slate_TyreParticleRecord_std430_0 = Particles_0[Particle_1];
    var _S17 : f32 = Particles_0[Particle_1].X_0 - CentreX_1;
    var _S18 : f32 = Particles_0[Particle_1].Y_0 - CentreY_1;
    var _S19 : f32 = Particles_0[Particle_1].Z_0 - CentreZ_1;
    var Gradient_0 : Slate_TyreGradientRecord_0;
    Gradient_0.VolumeX_0 = 0.0f;
    Gradient_0.VolumeY_0 = 0.0f;
    Gradient_0.VolumeZ_0 = 0.0f;
    Gradient_0.OwnedVolume_0 = 0.0f;
    Gradient_0.RingX_0 = 0.0f;
    Gradient_0.RingY_0 = 0.0f;
    Gradient_0.RingZ_0 = 0.0f;
    Gradient_0.RingEdge_0 = 0.0f;
    Gradient_0.MeridianX_0 = 0.0f;
    Gradient_0.MeridianY_0 = 0.0f;
    Gradient_0.MeridianZ_0 = 0.0f;
    Gradient_0.MeridianEdge_0 = 0.0f;
    var Triangle_1 : u32 = u32(0);
    for(;;)
    {
        if(Triangle_1 < u32(6))
        {
        }
        else
        {
            break;
        }
        var RingB_1 : i32 = i32(0);
        var SegmentB_1 : i32 = i32(0);
        var RingC_1 : i32 = i32(0);
        var SegmentC_1 : i32 = i32(0);
        Slate_ResolveIncidentCorner_0(Triangle_1, &(RingB_1), &(SegmentB_1), &(RingC_1), &(SegmentC_1));
        var AbsoluteB_0 : i32 = i32(Ring_2) + RingB_1;
        var AbsoluteC_0 : i32 = i32(Ring_2) + RingC_1;
        var _S20 : bool;
        if(AbsoluteB_0 < i32(0))
        {
            _S20 = true;
        }
        else
        {
            _S20 = AbsoluteC_0 < i32(0);
        }
        var _S21 : bool;
        if(_S20)
        {
            _S21 = true;
        }
        else
        {
            _S21 = AbsoluteB_0 >= i32(Step_0.Rings_0);
        }
        var _S22 : bool;
        if(_S21)
        {
            _S22 = true;
        }
        else
        {
            _S22 = AbsoluteC_0 >= i32(Step_0.Rings_0);
        }
        if(_S22)
        {
            Triangle_1 = Triangle_1 + u32(1);
            continue;
        }
        var _S23 : u32 = Slate_LatticeIndex_0(Tyre_1, AbsoluteB_0, i32(Segment_2) + SegmentB_1, Step_0.Rings_0, Step_0.Segments_0);
        var _S24 : u32 = Slate_LatticeIndex_0(Tyre_1, AbsoluteC_0, i32(Segment_2) + SegmentC_1, Step_0.Rings_0, Step_0.Segments_0);
        var BX_3 : f32 = Particles_0[_S23].X_0 - CentreX_1;
        var BY_3 : f32 = Particles_0[_S23].Y_0 - CentreY_1;
        var BZ_3 : f32 = Particles_0[_S23].Z_0 - CentreZ_1;
        var CX_2 : f32 = Particles_0[_S24].X_0 - CentreX_1;
        var CY_2 : f32 = Particles_0[_S24].Y_0 - CentreY_1;
        var CZ_2 : f32 = Particles_0[_S24].Z_0 - CentreZ_1;
        var _S25 : f32 = Gradient_0.VolumeX_0;
        var _S26 : f32 = Gradient_0.VolumeY_0;
        var _S27 : f32 = Gradient_0.VolumeZ_0;
        Slate_TriangleVolumeGradient_0(BX_3, BY_3, BZ_3, CX_2, CY_2, CZ_2, &(_S25), &(_S26), &(_S27));
        Gradient_0.VolumeX_0 = _S25;
        Gradient_0.VolumeY_0 = _S26;
        Gradient_0.VolumeZ_0 = _S27;
        if(Triangle_1 < u32(2))
        {
            Gradient_0.OwnedVolume_0 = Gradient_0.OwnedVolume_0 + Slate_TriangleSweptVolume_0(_S17, _S18, _S19, BX_3, BY_3, BZ_3, CX_2, CY_2, CZ_2);
        }
        Triangle_1 = Triangle_1 + u32(1);
    }
    var _S28 : u32 = Slate_LatticeIndex_0(Tyre_1, i32(Ring_2), i32(Segment_2) - i32(1), Step_0.Rings_0, Step_0.Segments_0);
    var _S29 : u32 = Slate_LatticeIndex_0(Tyre_1, i32(Ring_2), i32(Segment_2) + i32(1), Step_0.Rings_0, Step_0.Segments_0);
    var _S30 : f32 = Gradient_0.RingX_0;
    var _S31 : f32 = Gradient_0.RingY_0;
    var _S32 : f32 = Gradient_0.RingZ_0;
    var _S33 : f32 = Slate_EdgeLengthGradient_0(Own_0.X_0, Own_0.Y_0, Own_0.Z_0, Particles_0[_S28].X_0, Particles_0[_S28].Y_0, Particles_0[_S28].Z_0, &(_S30), &(_S31), &(_S32));
    var _S34 : f32 = _S30;
    var _S35 : f32 = _S31;
    var _S36 : f32 = _S32;
    var _S37 : f32 = Slate_EdgeLengthGradient_0(Own_0.X_0, Own_0.Y_0, Own_0.Z_0, Particles_0[_S29].X_0, Particles_0[_S29].Y_0, Particles_0[_S29].Z_0, &(_S34), &(_S35), &(_S36));
    Gradient_0.RingX_0 = _S34;
    Gradient_0.RingY_0 = _S35;
    Gradient_0.RingZ_0 = _S36;
    Gradient_0.RingEdge_0 = _S37;
    if(Ring_2 > u32(0))
    {
        var _S38 : u32 = Slate_LatticeIndex_0(Tyre_1, i32(Ring_2) - i32(1), i32(Segment_2), Step_0.Rings_0, Step_0.Segments_0);
        var _S39 : f32 = Gradient_0.MeridianX_0;
        var _S40 : f32 = Gradient_0.MeridianY_0;
        var _S41 : f32 = Gradient_0.MeridianZ_0;
        var _S42 : f32 = Slate_EdgeLengthGradient_0(Own_0.X_0, Own_0.Y_0, Own_0.Z_0, Particles_0[_S38].X_0, Particles_0[_S38].Y_0, Particles_0[_S38].Z_0, &(_S39), &(_S40), &(_S41));
        Gradient_0.MeridianX_0 = _S39;
        Gradient_0.MeridianY_0 = _S40;
        Gradient_0.MeridianZ_0 = _S41;
    }
    if((Ring_2 + u32(1)) < (Step_0.Rings_0))
    {
        var _S43 : u32 = Slate_LatticeIndex_0(Tyre_1, i32(Ring_2) + i32(1), i32(Segment_2), Step_0.Rings_0, Step_0.Segments_0);
        var _S44 : f32 = Gradient_0.MeridianX_0;
        var _S45 : f32 = Gradient_0.MeridianY_0;
        var _S46 : f32 = Gradient_0.MeridianZ_0;
        var _S47 : f32 = Slate_EdgeLengthGradient_0(Own_0.X_0, Own_0.Y_0, Own_0.Z_0, Particles_0[_S43].X_0, Particles_0[_S43].Y_0, Particles_0[_S43].Z_0, &(_S44), &(_S45), &(_S46));
        Gradient_0.MeridianX_0 = _S44;
        Gradient_0.MeridianY_0 = _S45;
        Gradient_0.MeridianZ_0 = _S46;
        Gradient_0.MeridianEdge_0 = _S47;
    }
    Gradients_0[Particle_1].VolumeX_0 = Gradient_0.VolumeX_0;
    Gradients_0[Particle_1].VolumeY_0 = Gradient_0.VolumeY_0;
    Gradients_0[Particle_1].VolumeZ_0 = Gradient_0.VolumeZ_0;
    Gradients_0[Particle_1].OwnedVolume_0 = Gradient_0.OwnedVolume_0;
    Gradients_0[Particle_1].RingX_0 = Gradient_0.RingX_0;
    Gradients_0[Particle_1].RingY_0 = Gradient_0.RingY_0;
    Gradients_0[Particle_1].RingZ_0 = Gradient_0.RingZ_0;
    Gradients_0[Particle_1].RingEdge_0 = Gradient_0.RingEdge_0;
    Gradients_0[Particle_1].MeridianX_0 = Gradient_0.MeridianX_0;
    Gradients_0[Particle_1].MeridianY_0 = Gradient_0.MeridianY_0;
    Gradients_0[Particle_1].MeridianZ_0 = Gradient_0.MeridianZ_0;
    Gradients_0[Particle_1].MeridianEdge_0 = Gradient_0.MeridianEdge_0;
    var RingRow_0 : u32 = u32(1) + Ring_2;
    var MeridianRow_0 : u32 = u32(1) + Step_0.Rings_0 + Segment_2;
    var Base_0 : u32 = Tyre_1 * u32(9312);
    var Values_0 : u32 = Base_0 + u32(9216);
    var VX_0 : f32 = Gradient_0.VolumeX_0;
    var VY_0 : f32 = Gradient_0.VolumeY_0;
    var VZ_0 : f32 = Gradient_0.VolumeZ_0;
    var RX_0 : f32 = Gradient_0.RingX_0;
    var RY_0 : f32 = Gradient_0.RingY_0;
    var RZ_0 : f32 = Gradient_0.RingZ_0;
    var MX_0 : f32 = Gradient_0.MeridianX_0;
    var MY_0 : f32 = Gradient_0.MeridianY_0;
    var MZ_0 : f32 = Gradient_0.MeridianZ_0;
    var _S48 : u32 = Base_0 + RingRow_0 * u32(96);
    var RingRing_0 : u32 = _S48 + RingRow_0;
    var MeridianMeridian_0 : u32 = Base_0 + MeridianRow_0 * u32(96) + MeridianRow_0;
    var VolumeRing_0 : u32 = Base_0 + RingRow_0;
    var VolumeMeridian_0 : u32 = Base_0 + MeridianRow_0;
    var RingMeridian_0 : u32 = _S48 + MeridianRow_0;
    var DiagonalScale_0 : f32 = Slate_ShapeEntryScale_0(RingRow_0, RingRow_0);
    var CrossScale_0 : f32 = Slate_ShapeEntryScale_0(u32(0), RingRow_0);
    var _S49 : i32 = atomicAdd(&(ShapeSums_0[Base_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, Gradient_0.VolumeX_0, Gradient_0.VolumeY_0, Gradient_0.VolumeZ_0, Gradient_0.VolumeX_0, Gradient_0.VolumeY_0, Gradient_0.VolumeZ_0), Slate_ShapeEntryScale_0(u32(0), u32(0))));
    var _S50 : i32 = atomicAdd(&(ShapeSums_0[RingRing_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, RX_0, RY_0, RZ_0, RX_0, RY_0, RZ_0), DiagonalScale_0));
    var _S51 : i32 = atomicAdd(&(ShapeSums_0[MeridianMeridian_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, MX_0, MY_0, MZ_0, MX_0, MY_0, MZ_0), DiagonalScale_0));
    var _S52 : i32 = atomicAdd(&(ShapeSums_0[VolumeRing_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, VX_0, VY_0, VZ_0, RX_0, RY_0, RZ_0), CrossScale_0));
    var _S53 : i32 = atomicAdd(&(ShapeSums_0[VolumeMeridian_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, VX_0, VY_0, VZ_0, MX_0, MY_0, MZ_0), CrossScale_0));
    var _S54 : i32 = atomicAdd(&(ShapeSums_0[RingMeridian_0]), Slate_EncodeFixed_0(Slate_WeightedDot_0(Own_0.InverseMass_0, RX_0, RY_0, RZ_0, MX_0, MY_0, MZ_0), DiagonalScale_0));
    var _S55 : i32 = atomicAdd(&(ShapeSums_0[Values_0]), Slate_EncodeFixed_0(Gradient_0.OwnedVolume_0, Slate_ShapeValueScale_0(u32(0))));
    var _S56 : i32 = atomicAdd(&(ShapeSums_0[Values_0 + RingRow_0]), Slate_EncodeFixed_0(Gradient_0.RingEdge_0, Slate_ShapeValueScale_0(RingRow_0)));
    var _S57 : i32 = atomicAdd(&(ShapeSums_0[Values_0 + MeridianRow_0]), Slate_EncodeFixed_0(Gradient_0.MeridianEdge_0, Slate_ShapeValueScale_0(MeridianRow_0)));
    return;
}

