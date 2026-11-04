
#pragma once
#include "IntrinsicParity.slang"
// 3D FEM / Shell / XPBD carcass nodal displacement field, volumetric tyre deformation, damage visibility and cabin articulation; common-domain by all raster paths.
struct VehicleProfile {
    float4 Cabin;                             // steering angle, see-through glass, reserved, lamp
    float4 Column;               
    float4 Axis;                 
    float4 CabinFrame;           
    float4 CabinShift;                        // tablet centre xyz, tablet-only scale w
    float4 Wheels[4];                  // x = shredded, y = carcassActive (1 = 3D FEM / Shell / XPBD), zw = reserved
    float4 Deformation[4];             // x = ContactAngle, y = Deflection, z = GaugePressure, w = LateralDistortion
    float4 CarcassDisplacements[640];  // 4 wheels * 32 circumferential sectors * 5 meridional rings: wheel-local u = x - X
};

static const float TyreRimRadius = 0.3448;
static const float TyreOuterRadius = 0.50855;
static const float TyreHalfWidth = 0.118;
static const float PI = 3.141592653589793;

struct DeformedVertex { float3 Position;  float3 Normal;  };

float3 SampleCarcassDisplacement(uint Wheel, uint Sector, uint Ring) {
    let Idx = Wheel * 160u + (Sector & 31u) * 5u + min(Ring, 4u);
    return VehicleState[0].CarcassDisplacements[Idx].xyz;
}

// 📝 Evaluates real-time GPU vertex deformation from either the 3D Hexahedral Neo-Hookean FEM / Shell / XPBD
//    32x5 nodal displacement lattice (Wheels[Wheel].y > 0.5) or the analytical volumetric spring profile.
DeformedVertex ResolveWheelVertex(uint Wheel, uint Family, float3 Position, float3 Normal) {
    var Result : DeformedVertex = {};
    Result.Position = Position;
    Result.Normal = Normal;

    if (Family == TyreFamily && VehicleState[0].Wheels[Wheel].x > 0.5) {
        Result.Position = float3(0.0);
        return Result;
    }

    if (Family != TyreFamily) {
        return Result;
    }

    let R = length(Position.xz);
    if (R <= TyreRimRadius + 1.0e-4) {
        return Result;
    }

    let Deform = VehicleState[0].Deformation[Wheel];
    let Deflection = Deform.y;
    if (Deflection <= 1.0e-5) {
        return Result;
    }

    let RadialFrac = clamp((R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius), 0.0, 1.0);
    let Phi = atan2(Position.z, Position.x);

    // Path A: 3D FEM / 2D Shell FEM / XPBD Carcass Nodal Displacement Field Interpolation
    if (VehicleState[0].Wheels[Wheel].y > 0.5) {
        let NormAngle = frac(Phi / (2.0 * PI) + 1.0) * 32.0;
        let S0 = ToUnsigned(floor(NormAngle)) & 31u;
        let S1 = (S0 + 1u) & 31u;
        let FracS = frac(NormAngle);

        // Map lateral Y [-0.118..+0.118] across the 5 meridional rings (0 = inner bead, 1 = inner shoulder, 2 = crown, 3 = outer shoulder, 4 = outer bead)
        let RingCoord = clamp((Position.y / TyreHalfWidth) * 1.35 + 2.0, 0.5, 3.5);
        let M0 = min(ToUnsigned(floor(RingCoord)), 3u);
        let M1 = M0 + 1u;
        let FracM = clamp(RingCoord - float(M0), 0.0, 1.0);

        let U00 = SampleCarcassDisplacement(Wheel, S0, M0);
        let U10 = SampleCarcassDisplacement(Wheel, S1, M0);
        let U01 = SampleCarcassDisplacement(Wheel, S0, M1);
        let U11 = SampleCarcassDisplacement(Wheel, S1, M1);

        let U0 = lerp(U00, U10, FracS);
        let U1 = lerp(U01, U11, FracS);
        let UFem = lerp(U0, U1, FracM);

        let RadialWeight = smoothstep(0.0, 0.88, RadialFrac);
        Result.Position  = Position + UFem * RadialWeight;

        // Tangential finite-difference normal perturbation across the deformed sector
        let DUSector = (U10 - U00) * RadialWeight;
        let Tangent = float3(-sin(Phi), 0.0, cos(Phi));
        let Tilt = dot(DUSector, float3(cos(Phi), 0.0, sin(Phi))) * 4.5;
        Result.Normal = normalize(Normal - Tangent * Tilt);
        return Result;
    }

    // Path B: Analytical Spring Volumetric Profile Fallback
    let ContactAngle = Deform.x;
    let Pressure = max(0.0, Deform.z);
    let LateralDistortion = Deform.w;

    var DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2.0 * PI * RoundEven(DPhi / (2.0 * PI));

    let PNorm = max(0.05, Pressure / 220000.0);
    let PatchSpan = clamp(0.44 / sqrt(max(0.15, PNorm)), 0.20, 1.15);

    var DR : float = 0.0;
    var DY : float = 0.0;
    let AbsDPhi = abs(DPhi);

    if (AbsDPhi < PatchSpan) {
        let U = AbsDPhi / PatchSpan;
        let Envelope = 0.5 * (1.0 + cos(U * PI));
        DR = -Deflection * Envelope * RadialFrac;

        let SidewallFactor = sin(RadialFrac * PI);
        let BulgeIntensity = 0.48 / (0.4 + 0.6 * sqrt(PNorm));
        DY = FloatSign(Position.y) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }

    let WaveOuter = PatchSpan * 1.5;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter) {
        let V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        let Wave = sin(V * PI) * (0.12 * Deflection * RadialFrac);
        DR = DR + Wave;
    }

    if (AbsDPhi < PatchSpan * 1.2) {
        let UShear = AbsDPhi / (PatchSpan * 1.2);
        let ShearEnv = 0.5 * (1.0 + cos(UShear * PI));
        DY = DY + LateralDistortion * ShearEnv * RadialFrac;
    }

    let NewR = max(TyreRimRadius, R + DR);
    Result.Position = float3(NewR * cos(Phi), Position.y + DY, NewR * sin(Phi));

    if (AbsDPhi < PatchSpan) {
        let Tilt = -sin((DPhi / PatchSpan) * PI) * (Deflection / (TyreOuterRadius * PatchSpan));
        Result.Normal = normalize(Normal + float3(-sin(Phi), 0.0, cos(Phi)) * Tilt);
    }

    return Result;
}

// 📝 Rodrigues: Vector turned by Angle about the unit Axis.
float3 TurnAboutAxis(float3 Vector, float3 Axis, float Angle)
{
    let C = cos(Angle);
    let S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

// 📝 A steering-wheel facet (car-body refresh) turned about the column by the wheel's angle.
DeformedVertex TurnSteeringWheel(float3 Position, float3 Normal)
{
    var Turned : DeformedVertex = {};
    let Axis = normalize(VehicleState[0].Axis.xyz);
    let Pivot = VehicleState[0].Column.xyz;
    Turned.Position = Pivot + TurnAboutAxis(Position - Pivot, Axis, VehicleState[0].Cabin.x);
    Turned.Normal = TurnAboutAxis(Normal, Axis, VehicleState[0].Cabin.x);
    return Turned;
}

float3 PlaceTablet(float3 Position) {
    return VehicleState[0].CabinShift.xyz + (Position - VehicleState[0].CabinShift.xyz) * VehicleState[0].CabinShift.w;
}
// Only the steering assembly articulates. HUD sizing never moves physical cabin geometry.
DeformedVertex DeformBodyVertex(uint Family, float3 Position, float3 Normal)
{
    var Moved : DeformedVertex = {};
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Family == TabletHousingFamily || Family == DisplayGlassFamily) { Moved.Position = PlaceTablet(Position); }
    if (Family == SteeringWheelFamily)
    {
        Moved = TurnSteeringWheel(Position, Normal);
    }
    return Moved;
}
