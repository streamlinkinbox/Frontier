/*==========================================================================================================================================
                                                       DIELECTRICBOUNDARY.SLANG
==========================================================================================================================================*/
// 🧩 Dispatch the production dielectric boundary evaluation and return direction, Fresnel and TIR metadata.
#include "../../../Engine/Render/Shader/ScatteringEvaluation.slang"
#include "../../../Engine/Render/Shader/DielectricRefraction.slang"
struct BoundaryExtent
{
    uint4 Dimensions;
    float4 Minimum, Maximum;
    uint4 Grid, Sampling, History, Reconstruction, Primary;
};
[[vk::binding(4,0)]] StructuredBuffer<float> BoundaryCoefficients;
[[vk::binding(6,0)]] RWStructuredBuffer<float4> BoundaryOutput;
[[vk::push_constant]] ConstantBuffer<BoundaryExtent> Boundary;
[shader("compute")][numthreads(64,1,1)]
void EvaluateDielectricBoundary(uint3 Dispatch : SV_DispatchThreadID)
{
    uint I = Dispatch.x;
    if (I >= Boundary.Dimensions.x) return;
    uint P = I * 8;
    float3 Incident = float3(BoundaryCoefficients[P], BoundaryCoefficients[P+1], BoundaryCoefficients[P+2]);
    float3 Normal = float3(BoundaryCoefficients[P+4], BoundaryCoefficients[P+5], BoundaryCoefficients[P+6]);
    DielectricRefraction R = EvaluateDielectricRefraction(Incident, Normal, BoundaryCoefficients[P+3], BoundaryCoefficients[P+7]);
    BoundaryOutput[I] = float4(R.Direction, R.Reflectance);
    BoundaryOutput[16 * Boundary.Dimensions.x + I] = float4(R.RadianceScale, R.Valid ? 1 : 0, R.TotalInternalReflection ? 1 : 0, 0);
}
