/*============================================================================================================================================
                                                        BAKEFIELDEVALUATION.SLANG
============================================================================================================================================*/
// 🧩 Per-texel mesh-bake field evaluation over the flattened high BVH: primary projection ray, cosine-weighted
//    hemisphere occlusion/bent sampling, curvature probes, thickness ray and bevel normal averaging. Mirrors
//    Engine/ContentInterchange/BakeField.cpp's EvaluateTexel statement-for-statement so the CPU reference
//    (BakeFieldValidation) stays the executable specification of this shader.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Layouts mirror Engine/ContentInterchange/BakeField.h field-for-field (16-byte rows, std430); the C++
// static_asserts there and CompileBakeField.mjs's reflection check guard this ABI from both sides.
struct BakeTriangleSlot
{
    float4 A;       // [m] - First vertex position; w = high part index
    float4 B;       // [m] - Second vertex position
    float4 C;       // [m] - Third vertex position
    float4 NormalA; // [-] - First vertex normal
    float4 NormalB; // [-] - Second vertex normal
    float4 NormalC; // [-] - Third vertex normal
};
struct BakeExtentSlot
{
    float4 Lower; // [m] - Axis-aligned minimum corner
    float4 Upper; // [m] - Axis-aligned maximum corner
    uint4  Index; // [idx] - x/y = sub-extents (interior) or first/count (leaf); z = leaf marker
};
struct BakeTexelSurface
{
    float4 WorldPosition; // [m] - Low-surface position under this texel
    float4 WorldNormal;   // [-] - Low-surface normal; w = covered marker
};
struct BakeTexelField
{
    float4 HitPosition; // [m] - Primary-ray hit position; w = hit marker
    float4 HitNormal;   // [-] - Primary-ray hit normal; w = hit part index
    float4 Bent;        // [-] - Bent normal; w = hemisphere visibility
    float4 Bevel;       // [-] - Bevel-blended normal; w = bevel mask
    float4 Scalars;     // [-] - Secondary visibility / signed curvature / thickness distance / marker
};
struct BakeFieldUniforms
{
    uint  TexelCount;
    uint  Samples;
    uint  BevelSamples;
    uint  NeedMask;
    float Front;
    float Back;
    float AoDistance;
    float SecondaryAoDistance;
    float CurvatureRadius;
    float BevelRadius;
    float BevelStrength;
    float Epsilon;
};
// 📝 NeedMask bits, mirroring BakeField.h's BakeFieldNeed.
static const uint NeedHemisphere = 1;
static const uint NeedSecondary  = 2;
static const uint NeedCurvature  = 4;
static const uint NeedThickness  = 8;
static const uint NeedBevel      = 16;
// 📝 Traversal stack bound shared with BakeField.cpp: the median-split build halves triangle count per
// extent, so 64 levels cover any triangle count that fits in memory.
static const uint StackDepth = 64;

[[vk::push_constant]] ConstantBuffer<BakeFieldUniforms> Uniforms;
[[vk::binding(0, 0)]] StructuredBuffer<BakeTriangleSlot> Triangles;
[[vk::binding(1, 0)]] StructuredBuffer<BakeExtentSlot> Extents;
[[vk::binding(2, 0)]] StructuredBuffer<BakeTexelSurface> Surfaces;
[[vk::binding(3, 0)]] RWStructuredBuffer<BakeTexelField> Fields;

//------------------------------------------------------------------------------------------------------------------------
//                                                        VECTOR ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

// 📝 Manual normalize (not the normalize() intrinsic): BakeField.cpp divides by sqrt(dot(v, v)) behind the
// same 1e-20 guard, and matching that arithmetic keeps GPU fields within float-associativity noise of the
// CPU reference instead of an rsqrt approximation's wider error.
float3 NormalizeField(float3 Value)
{
    float Length = sqrt(dot(Value, Value));
    if (Length > 1e-20) Value /= Length;
    return Value;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FLAT BVH QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool RayBox(float3 Origin, float3 InvDirection, float3 Minimum, float3 Maximum, float MinDistance, float MaxDistance)
{
    float Near = MinDistance, Far = MaxDistance;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float T0 = (Minimum[Axis] - Origin[Axis]) * InvDirection[Axis];
        float T1 = (Maximum[Axis] - Origin[Axis]) * InvDirection[Axis];
        if (T0 > T1) { float Swap = T0; T0 = T1; T1 = Swap; }
        Near = max(Near, T0);
        Far = min(Far, T1);
        if (Far < Near) return false;
    }
    return true;
}
// 📝 Möller–Trumbore, identical tolerances to BakeField.cpp's RayTriangle.
bool RayTriangle(float3 Origin, float3 Direction, float3 A, float3 B, float3 C, float MinDistance, float MaxDistance,
    out float OutDistance, out float3 OutBarycentric)
{
    OutDistance = 0.0;
    OutBarycentric = float3(0.0, 0.0, 0.0);
    float3 Edge1 = B - A;
    float3 Edge2 = C - A;
    float3 PVector = cross(Direction, Edge2);
    float Determinant = dot(Edge1, PVector);
    if (abs(Determinant) < 1e-12) return false;
    float InverseDeterminant = 1.0 / Determinant;
    float3 TVector = Origin - A;
    float U = dot(TVector, PVector) * InverseDeterminant;
    if (U < -1e-5 || U > 1.0 + 1e-5) return false;
    float3 QVector = cross(TVector, Edge1);
    float V = dot(Direction, QVector) * InverseDeterminant;
    if (V < -1e-5 || U + V > 1.0 + 1e-5) return false;
    float Distance = dot(Edge2, QVector) * InverseDeterminant;
    if (Distance < MinDistance || Distance > MaxDistance) return false;
    OutDistance = Distance;
    OutBarycentric = float3(1.0 - U - V, U, V);
    return true;
}

struct BakeRayHit
{
    bool   Found;
    float  Distance;
    float3 Normal;
    float3 Position;
    uint   PartIndex;
};
BakeRayHit CastRayField(float3 Origin, float3 Direction, float MinDistance, float MaxDistance, uint ExtentCount)
{
    BakeRayHit Result;
    Result.Found = false;
    Result.Distance = 0.0;
    Result.Normal = float3(0.0, 0.0, 0.0);
    Result.Position = float3(0.0, 0.0, 0.0);
    Result.PartIndex = 0;
    if (ExtentCount == 0) return Result;
    float3 InvDirection = float3(1.0 / Direction.x, 1.0 / Direction.y, 1.0 / Direction.z);
    float BestDistance = MaxDistance;
    uint Stack[StackDepth];
    uint Top = 0;
    Stack[Top++] = 0;
    while (Top > 0)
    {
        BakeExtentSlot Extent = Extents[Stack[--Top]];
        if (!RayBox(Origin, InvDirection, Extent.Lower.xyz, Extent.Upper.xyz, MinDistance, BestDistance)) continue;
        if (Extent.Index.z != 0)
        {
            for (uint Index = 0; Index < Extent.Index.y; ++Index)
            {
                BakeTriangleSlot Triangle = Triangles[Extent.Index.x + Index];
                float Distance;
                float3 Barycentric;
                if (RayTriangle(Origin, Direction, Triangle.A.xyz, Triangle.B.xyz, Triangle.C.xyz, MinDistance,
                        BestDistance, Distance, Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true;
                    Result.Distance = Distance;
                    Result.PartIndex = uint(Triangle.A.w);
                    Result.Position = Origin + Direction * Distance;
                    Result.Normal = NormalizeField(Barycentric.x * Triangle.NormalA.xyz +
                        Barycentric.y * Triangle.NormalB.xyz + Barycentric.z * Triangle.NormalC.xyz);
                }
            }
        }
        else
        {
            if (Top < StackDepth) Stack[Top++] = Extent.Index.x;
            if (Top < StackDepth) Stack[Top++] = Extent.Index.y;
        }
    }
    return Result;
}

float BoxDistanceSquared(float3 Point, float3 Minimum, float3 Maximum)
{
    float Sum = 0.0;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float Value = Point[Axis];
        if (Value < Minimum[Axis]) Sum += (Minimum[Axis] - Value) * (Minimum[Axis] - Value);
        else if (Value > Maximum[Axis]) Sum += (Value - Maximum[Axis]) * (Value - Maximum[Axis]);
    }
    return Sum;
}
// 📝 Ericson 5.1.5 nearest point on triangle, same region walk as BakeField.cpp.
float3 ClosestPointOnTriangle(float3 Point, float3 A, float3 B, float3 C)
{
    float3 Ab = B - A;
    float3 Ac = C - A;
    float3 Ap = Point - A;
    float D1 = dot(Ab, Ap), D2 = dot(Ac, Ap);
    if (D1 <= 0.0 && D2 <= 0.0) return A;
    float3 Bp = Point - B;
    float D3 = dot(Ab, Bp), D4 = dot(Ac, Bp);
    if (D3 >= 0.0 && D4 <= D3) return B;
    float Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0.0 && D1 >= 0.0 && D3 <= 0.0) return A + Ab * (D1 / (D1 - D3));
    float3 Cp = Point - C;
    float D5 = dot(Ab, Cp), D6 = dot(Ac, Cp);
    if (D6 >= 0.0 && D5 <= D6) return C;
    float Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0.0 && D2 >= 0.0 && D6 <= 0.0) return A + Ac * (D2 / (D2 - D6));
    float Va = D3 * D6 - D5 * D4;
    if (Va <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0) return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
    float Denominator = 1.0 / (Va + Vb + Vc);
    return A + Ab * (Vb * Denominator) + Ac * (Vc * Denominator);
}
float3 PlaneBarycentric(float3 Point, float3 A, float3 B, float3 C)
{
    float3 V0 = B - A;
    float3 V1 = C - A;
    float3 V2 = Point - A;
    float D00 = dot(V0, V0), D01 = dot(V0, V1), D11 = dot(V1, V1), D20 = dot(V2, V0), D21 = dot(V2, V1);
    float Denominator = D00 * D11 - D01 * D01;
    if (abs(Denominator) < 1e-20) return float3(1.0, 0.0, 0.0);
    float V = (D11 * D20 - D01 * D21) / Denominator;
    float W = (D00 * D21 - D01 * D20) / Denominator;
    return float3(1.0 - V - W, V, W);
}

struct BakeClosestPoint
{
    bool   Found;
    float  Distance;
    float3 Point;
    float3 Normal;
    uint   PartIndex;
};
BakeClosestPoint ClosestPointField(float3 Point, float MaxDistance, uint ExtentCount)
{
    BakeClosestPoint Result;
    Result.Found = false;
    Result.Distance = 0.0;
    Result.Point = float3(0.0, 0.0, 0.0);
    Result.Normal = float3(0.0, 0.0, 0.0);
    Result.PartIndex = 0;
    if (ExtentCount == 0) return Result;
    float BestDistanceSquared = MaxDistance * MaxDistance;
    uint Stack[StackDepth];
    uint Top = 0;
    Stack[Top++] = 0;
    while (Top > 0)
    {
        BakeExtentSlot Extent = Extents[Stack[--Top]];
        if (BoxDistanceSquared(Point, Extent.Lower.xyz, Extent.Upper.xyz) > BestDistanceSquared) continue;
        if (Extent.Index.z != 0)
        {
            for (uint Index = 0; Index < Extent.Index.y; ++Index)
            {
                BakeTriangleSlot Triangle = Triangles[Extent.Index.x + Index];
                float3 Closest = ClosestPointOnTriangle(Point, Triangle.A.xyz, Triangle.B.xyz, Triangle.C.xyz);
                float3 Delta = Closest - Point;
                float DistanceSquared = dot(Delta, Delta);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    Result.Found = true;
                    Result.Distance = sqrt(DistanceSquared);
                    Result.PartIndex = uint(Triangle.A.w);
                    Result.Point = Closest;
                    float3 Weights = PlaneBarycentric(Closest, Triangle.A.xyz, Triangle.B.xyz, Triangle.C.xyz);
                    Result.Normal = NormalizeField(Weights.x * Triangle.NormalA.xyz + Weights.y * Triangle.NormalB.xyz +
                        Weights.z * Triangle.NormalC.xyz);
                }
            }
        }
        else
        {
            // 📝 Farther sub-extent pushed first so the nearer is visited first, as BakeField.cpp.
            float LeftDistance = BoxDistanceSquared(Point, Extents[Extent.Index.x].Lower.xyz, Extents[Extent.Index.x].Upper.xyz);
            float RightDistance = BoxDistanceSquared(Point, Extents[Extent.Index.y].Lower.xyz, Extents[Extent.Index.y].Upper.xyz);
            if (LeftDistance <= RightDistance)
            {
                if (Top < StackDepth) Stack[Top++] = Extent.Index.y;
                if (Top < StackDepth) Stack[Top++] = Extent.Index.x;
            }
            else
            {
                if (Top < StackDepth) Stack[Top++] = Extent.Index.x;
                if (Top < StackDepth) Stack[Top++] = Extent.Index.y;
            }
        }
    }
    return Result;
}

// 📝 Deterministic tangent frame around a hit normal, 1:1 with BakeField.cpp's Hemisphere.
void HemisphereBasis(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    Tangent = NormalizeField(cross(abs(Normal.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0), Normal));
    Bitangent = cross(Normal, Tangent);
}
// 📝 Local high-normal average behind the bevel maps, 1:1 with BakeField.cpp's AverageNormalFlat.
float3 AverageNormalField(float3 Point, float3 Normal, float3 Facing, float Radius, uint Samples, float Epsilon,
    uint ExtentCount)
{
    if (Radius <= Epsilon) return Normal;
    float3 Tangent, Bitangent;
    HemisphereBasis(Normal, Tangent, Bitangent);
    float3 Sum = Normal * 2.0;
    float Weight = 2.0;
    float SearchRadius = Radius * 1.8;
    for (uint K = 0; K < Samples; ++K)
    {
        float Angle = (float(K) / float(max(1u, Samples))) * 6.283185307179586;
        float3 Query = Point + Tangent * cos(Angle) * Radius + Bitangent * sin(Angle) * Radius - Normal * Radius * 0.3;
        BakeClosestPoint Near = ClosestPointField(Query, SearchRadius, ExtentCount);
        if (!Near.Found || Near.Distance > SearchRadius) continue;
        if (dot(Near.Normal, Normal) < -0.1 || dot(Near.Normal, Facing) < -1e-6) continue;
        float SampleWeight = 1.0 - clamp(Near.Distance / SearchRadius, 0.0, 1.0);
        Sum += Near.Normal * SampleWeight;
        Weight += SampleWeight;
    }
    return NormalizeField(Sum / Weight);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PER-TEXEL FIELD EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Statement-for-statement mirror of BakeField.cpp's EvaluateTexel; keep the two in lockstep.
BakeTexelField EvaluateTexel(BakeTexelSurface Surface, uint ExtentCount)
{
    BakeTexelField Field;
    float3 WorldPosition = Surface.WorldPosition.xyz;
    float3 WorldNormal = Surface.WorldNormal.xyz;
    bool Covered = Surface.WorldNormal.w > 0.5;

    bool HasHit = false;
    BakeRayHit Hit;
    Hit.Found = false;
    Hit.Distance = 0.0;
    Hit.Normal = float3(0.0, 0.0, 0.0);
    Hit.Position = float3(0.0, 0.0, 0.0);
    Hit.PartIndex = 0;
    if (Covered)
    {
        float EnvelopeLength = Uniforms.Back + Uniforms.Front;
        float3 RayOrigin = WorldPosition - WorldNormal * Uniforms.Back;
        Hit = CastRayField(RayOrigin, WorldNormal, 0.0, EnvelopeLength, ExtentCount);
        HasHit = Hit.Found;
    }
    float3 NH = HasHit ? Hit.Normal : WorldNormal;
    float3 Q = HasHit ? Hit.Position : WorldPosition;

    Field.HitPosition = float4(Hit.Position, HasHit ? 1.0 : 0.0);
    Field.HitNormal = float4(Hit.Normal, float(Hit.PartIndex));
    Field.Bent = float4(NH, 1.0);
    Field.Bevel = float4(NH, 0.0);
    Field.Scalars = float4(1.0, 0.0, 0.0, 0.0);

    float Epsilon = Uniforms.Epsilon;
    if (HasHit && (Uniforms.NeedMask & NeedHemisphere) != 0)
    {
        float3 Tangent, Bitangent;
        HemisphereBasis(Hit.Normal, Tangent, Bitangent);
        float3 Origin = Hit.Position + Hit.Normal * Epsilon * 3.0;
        uint Samples = max(1u, Uniforms.Samples);
        float SampleDistance = (Uniforms.NeedMask & NeedSecondary) != 0
            ? max(Uniforms.AoDistance, Uniforms.SecondaryAoDistance) : Uniforms.AoDistance;
        uint Blocked = 0, SecondaryBlocked = 0;
        float3 BentSum = float3(0.0, 0.0, 0.0);
        float Seed = fmod(Hit.Position.x * 0.754877666 + Hit.Position.y * 0.569840296 + Hit.Position.z * 0.438579021, 1.0);
        for (uint S = 0; S < Samples; ++S)
        {
            float U = (float(S) + 0.5) / float(Samples);
            float Z = sqrt(max(0.0, 1.0 - U)), R = sqrt(U);
            float Angle = float(S) * 2.399963229728653 + Seed * 6.283185307179586;
            float3 Direction = Hit.Normal * Z + Tangent * cos(Angle) * R + Bitangent * sin(Angle) * R;
            BakeRayHit Obstruction = CastRayField(Origin, Direction, Epsilon, SampleDistance, ExtentCount);
            bool Occluded = Obstruction.Found && Obstruction.Distance <= Uniforms.AoDistance;
            if (Occluded) ++Blocked;
            else BentSum += Direction;
            if (Obstruction.Found && Obstruction.Distance <= Uniforms.SecondaryAoDistance) ++SecondaryBlocked;
        }
        Field.Bent.w = 1.0 - float(Blocked) / float(Samples);
        Field.Scalars.x = 1.0 - float(SecondaryBlocked) / float(Samples);
        float3 Bent = NH;
        if (dot(BentSum, BentSum) < 1e-12) Bent = Hit.Normal;
        else Bent = NormalizeField(BentSum);
        Field.Bent.xyz = Bent;
    }
    if (HasHit && (Uniforms.NeedMask & NeedCurvature) != 0)
    {
        float3 Tangent, Bitangent;
        HemisphereBasis(Hit.Normal, Tangent, Bitangent);
        float Sum = 0.0;
        uint Count = 0;
        for (uint S = 0; S < 4; ++S)
        {
            float Angle = float(S) * 1.5707963267948966;
            float3 Direction = Tangent * cos(Angle) + Bitangent * sin(Angle);
            float3 Query = Hit.Position + Direction * Uniforms.CurvatureRadius;
            BakeClosestPoint Near = ClosestPointField(Query, Uniforms.CurvatureRadius * 2.0, ExtentCount);
            if (Near.Found)
            {
                Sum += dot(Near.Normal - Hit.Normal, Direction);
                ++Count;
            }
        }
        Field.Scalars.y = Count != 0 ? Sum / float(Count) : 0.0;
    }
    if (HasHit && (Uniforms.NeedMask & NeedThickness) != 0)
    {
        float3 InwardOrigin = Q - Hit.Normal * Epsilon * 3.0;
        BakeRayHit Inward = CastRayField(InwardOrigin, -Hit.Normal, Epsilon, 3.402823e38, ExtentCount);
        Field.Scalars.z = Inward.Found ? Inward.Distance : 0.0;
        Field.Scalars.w = Inward.Found ? 1.0 : 0.0;
    }
    if (HasHit && (Uniforms.NeedMask & NeedBevel) != 0)
    {
        float3 Averaged = AverageNormalField(Q, Hit.Normal, WorldNormal, Uniforms.BevelRadius,
            max(1u, Uniforms.BevelSamples), Epsilon, ExtentCount);
        float InverseStrength = 1.0 - clamp(Uniforms.BevelStrength, 0.0, 1.0);
        float3 BevelNormal = NormalizeField(Averaged * Uniforms.BevelStrength + Hit.Normal * InverseStrength);
        Field.Bevel = float4(BevelNormal, clamp((1.0 - dot(Hit.Normal, BevelNormal)) * 8.0, 0.0, 1.0));
    }
    return Field;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MAIN
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void BakeFieldCompute(uint3 DispatchThread : SV_DispatchThreadID)
{
    uint Texel = DispatchThread.x;
    if (Texel >= Uniforms.TexelCount) return;
    uint ExtentCount, ExtentStride;
    Extents.GetDimensions(ExtentCount, ExtentStride);
    Fields[Texel] = EvaluateTexel(Surfaces[Texel], ExtentCount);
}
