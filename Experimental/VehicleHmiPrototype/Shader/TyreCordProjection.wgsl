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
struct Slate_TyreCordRecord_std430_0
{
    @align(4) ParticleA_0 : u32,
    @align(4) ParticleB_0 : u32,
    @align(4) RestLength_0 : f32,
    @align(4) Compliance_0 : f32,
};

@binding(5) @group(0) var<storage, read> Cords_0 : array<Slate_TyreCordRecord_std430_0>;

struct Slate_TyreParticleRecord_std430_0
{
    @align(4) X_0 : f32,
    @align(4) Y_0 : f32,
    @align(4) Z_0 : f32,
    @align(4) InverseMass_0 : f32,
};

@binding(0) @group(0) var<storage, read_write> Particles_0 : array<Slate_TyreParticleRecord_std430_0>;

@binding(6) @group(0) var<storage, read_write> CordMultipliers_0 : array<f32>;

fn SquareRoot_0( Operand_0 : f32) -> f32
{
    return sqrt(Operand_0);
}

fn Slate_ProjectCord_0( AX_0 : f32,  AY_0 : f32,  AZ_0 : f32,  InverseMassA_0 : f32,  BX_0 : f32,  BY_0 : f32,  BZ_0 : f32,  InverseMassB_0 : f32,  RestLength_1 : f32,  Compliance_1 : f32,  Substep_0 : f32,  Multiplier_0 : ptr<function, f32>,  CorrectionAX_0 : ptr<function, f32>,  CorrectionAY_0 : ptr<function, f32>,  CorrectionAZ_0 : ptr<function, f32>,  CorrectionBX_0 : ptr<function, f32>,  CorrectionBY_0 : ptr<function, f32>,  CorrectionBZ_0 : ptr<function, f32>)
{
    var DeltaX_0 : f32 = AX_0 - BX_0;
    var DeltaY_0 : f32 = AY_0 - BY_0;
    var DeltaZ_0 : f32 = AZ_0 - BZ_0;
    var Length_0 : f32 = SquareRoot_0(DeltaX_0 * DeltaX_0 + DeltaY_0 * DeltaY_0 + DeltaZ_0 * DeltaZ_0);
    (*CorrectionAX_0) = 0.0f;
    (*CorrectionAY_0) = 0.0f;
    (*CorrectionAZ_0) = 0.0f;
    (*CorrectionBX_0) = 0.0f;
    (*CorrectionBY_0) = 0.0f;
    (*CorrectionBZ_0) = 0.0f;
    if(Length_0 < 9.99999971718068537e-10f)
    {
        return;
    }
    var ScaledCompliance_0 : f32 = Compliance_1 / (Substep_0 * Substep_0);
    var Residual_0 : f32 = Length_0 - RestLength_1;
    var Denominator_0 : f32 = InverseMassA_0 + InverseMassB_0 + ScaledCompliance_0;
    if(Denominator_0 <= 0.0f)
    {
        return;
    }
    var Advance_0 : f32 = (- Residual_0 - ScaledCompliance_0 * (*Multiplier_0)) / Denominator_0;
    var Along_0 : f32 = Advance_0 / Length_0;
    (*Multiplier_0) = (*Multiplier_0) + Advance_0;
    var _S1 : f32 = InverseMassA_0 * Along_0;
    (*CorrectionAX_0) = _S1 * DeltaX_0;
    (*CorrectionAY_0) = _S1 * DeltaY_0;
    (*CorrectionAZ_0) = _S1 * DeltaZ_0;
    var _S2 : f32 = - InverseMassB_0 * Along_0;
    (*CorrectionBX_0) = _S2 * DeltaX_0;
    (*CorrectionBY_0) = _S2 * DeltaY_0;
    (*CorrectionBZ_0) = _S2 * DeltaZ_0;
    return;
}

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
    var _S3 : u32 = Dispatched_0.x;
    if(_S3 >= (Step_0.CordCount_0))
    {
        return;
    }
    var CordIndex_0 : u32 = Step_0.CordOffset_0 + _S3;
    var Cord_0 : Slate_TyreCordRecord_std430_0 = Cords_0[CordIndex_0];
    var A_0 : Slate_TyreParticleRecord_0;
    A_0.X_0 = Particles_0[Cord_0.ParticleA_0].X_0;
    A_0.Y_0 = Particles_0[Cord_0.ParticleA_0].Y_0;
    A_0.Z_0 = Particles_0[Cord_0.ParticleA_0].Z_0;
    A_0.InverseMass_0 = Particles_0[Cord_0.ParticleA_0].InverseMass_0;
    var B_0 : Slate_TyreParticleRecord_0;
    B_0.X_0 = Particles_0[Cord_0.ParticleB_0].X_0;
    B_0.Y_0 = Particles_0[Cord_0.ParticleB_0].Y_0;
    B_0.Z_0 = Particles_0[Cord_0.ParticleB_0].Z_0;
    B_0.InverseMass_0 = Particles_0[Cord_0.ParticleB_0].InverseMass_0;
    var Multiplier_1 : f32;
    var _S4 : f32;
    if((Step_0.IterationIndex_0) == u32(0))
    {
        _S4 = 0.0f;
    }
    else
    {
        _S4 = CordMultipliers_0[CordIndex_0];
    }
    Multiplier_1 = _S4;
    var CorrectionAX_1 : f32 = 0.0f;
    var CorrectionAY_1 : f32 = 0.0f;
    var CorrectionAZ_1 : f32 = 0.0f;
    var CorrectionBX_1 : f32 = 0.0f;
    var CorrectionBY_1 : f32 = 0.0f;
    var CorrectionBZ_1 : f32 = 0.0f;
    Slate_ProjectCord_0(A_0.X_0, A_0.Y_0, A_0.Z_0, A_0.InverseMass_0, B_0.X_0, B_0.Y_0, B_0.Z_0, B_0.InverseMass_0, Cord_0.RestLength_0, Cord_0.Compliance_0, Step_0.SubstepSeconds_0, &(Multiplier_1), &(CorrectionAX_1), &(CorrectionAY_1), &(CorrectionAZ_1), &(CorrectionBX_1), &(CorrectionBY_1), &(CorrectionBZ_1));
    CordMultipliers_0[CordIndex_0] = Multiplier_1;
    if((A_0.InverseMass_0) > 0.0f)
    {
        A_0.X_0 = A_0.X_0 + CorrectionAX_1;
        A_0.Y_0 = A_0.Y_0 + CorrectionAY_1;
        A_0.Z_0 = A_0.Z_0 + CorrectionAZ_1;
        Particles_0[Cord_0.ParticleA_0].X_0 = A_0.X_0;
        Particles_0[Cord_0.ParticleA_0].Y_0 = A_0.Y_0;
        Particles_0[Cord_0.ParticleA_0].Z_0 = A_0.Z_0;
        Particles_0[Cord_0.ParticleA_0].InverseMass_0 = A_0.InverseMass_0;
    }
    if((B_0.InverseMass_0) > 0.0f)
    {
        B_0.X_0 = B_0.X_0 + CorrectionBX_1;
        B_0.Y_0 = B_0.Y_0 + CorrectionBY_1;
        B_0.Z_0 = B_0.Z_0 + CorrectionBZ_1;
        Particles_0[Cord_0.ParticleB_0].X_0 = B_0.X_0;
        Particles_0[Cord_0.ParticleB_0].Y_0 = B_0.Y_0;
        Particles_0[Cord_0.ParticleB_0].Z_0 = B_0.Z_0;
        Particles_0[Cord_0.ParticleB_0].InverseMass_0 = B_0.InverseMass_0;
    }
    return;
}

