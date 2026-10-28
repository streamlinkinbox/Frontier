/*==========================================================================================================================================
                                                         MATERIALENERGY.SLANG
==========================================================================================================================================*/
// 🧩 Independently integrate the real shared Slang scattering evaluator in a Vulkan compute dispatch.

#include "../../../Engine/Render/Shader/ScatteringEvaluation.slang"
struct EnergyExtent
{
    uint4 Dimensions;
    float4 Minimum, Maximum;
    uint4 Grid, Sampling, History, Reconstruction, Primary;
};
[[vk::binding(4,0)]] StructuredBuffer<float> EnergyCoefficients;
[[vk::binding(6,0)]] RWStructuredBuffer<float4> EnergyOutput;
[[vk::push_constant]] ConstantBuffer<EnergyExtent> Energy;
float4 ReadEnergyCoefficients(uint Offset)
{
    return float4(EnergyCoefficients[Offset],EnergyCoefficients[Offset+1],EnergyCoefficients[Offset+2],EnergyCoefficients[Offset+3]);
}
[shader("compute")][numthreads(64,1,1)]
void IntegrateMaterialEnergy(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Case = Dispatch.x;
    if (Case >= Energy.Dimensions.x) return;
    uint Offset = Case*200;
    PreviewScattering S = (PreviewScattering)0;
    S.Configuration.x = uint(EnergyCoefficients[Offset+1]);
    for (uint I = 0; I < S.Configuration.x; ++I)
    {
        uint P = Offset+8+I*24;
        S.Strata[I].AlbedoRoughness = ReadEnergyCoefficients(P);
        S.Strata[I].Optics = ReadEnergyCoefficients(P+4);
        S.Strata[I].AbsorptionThickness = ReadEnergyCoefficients(P+8);
        S.Strata[I].Classification = uint4(ReadEnergyCoefficients(P+12));
        S.Strata[I].Sheen = ReadEnergyCoefficients(P+16);
        S.Strata[I].SheenFinish = ReadEnergyCoefficients(P+20);
    }
    float NV = EnergyCoefficients[Offset];
    float3 View = float3(sqrt(1-NV*NV),0,NV), Sum = 0;
    const uint Resolution = 256;
    [loop] for (uint I = 0; I < Resolution; ++I)
    {
        float NL = (float(I)+.5)/Resolution, Radius = sqrt(1-NL*NL);
        float3 Row = 0;
        [loop] for (uint J = 0; J < Resolution; ++J)
        {
            float Angle = 2*π*(float(J)+.5)/Resolution;
            float3 Light = float3(Radius*cos(Angle),Radius*sin(Angle),NL);
            Row += EvaluateScatteringSequence(S,float3(0,0,1),View,Light,float3(1))*NL*(2*π/(Resolution*Resolution));
        }
        Sum += Row;
    }
    EnergyOutput[Case] = float4(Sum,1);
}
