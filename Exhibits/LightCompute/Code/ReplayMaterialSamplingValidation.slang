/*==========================================================================================================================================
                                          REPLAYMATERIALSAMPLINGVALIDATION.SLANG
==========================================================================================================================================*/
// 🧩 Compare the Engine authored-material sampler and BRDF with a cosine proposal and analytic integrands.

#include "PathReplayReservoir.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                              MATCHED MATERIAL PROPOSALS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64, 1, 1)] void InspectReplayMaterialSampling(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint Receiver = Receivers[Pixel];
    uint Bits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
    float Selector = RandomUnit(Bits), Radial = RandomUnit(Bits), AzimuthDraw = RandomUnit(Bits);
    float3 Direction = 0;
    float Density = 0;
    bool Accepted = EvaluateOrSampleReplayPbrLobes(Receiver, true, Selector, Radial, AzimuthDraw, Direction, Density);
    float ImportanceRadiance = 0, IntegrandEstimate = 0, DensityError = -1;
    float3 Normal = Unit(Receiver.NormalRoughness.xyz);
    if (Accepted)
    {
        float NormalLight = max(0, dot(Normal, Direction));
        float EvaluatedDensity = ReplayActiveImportancePdf(Receiver, Direction);
        ImportanceRadiance = Brdf(Receiver, Direction).x * NormalLight / Density;
        IntegrandEstimate = NormalLight / (3.141592653589793 * Density);
        DensityError = abs(Density - EvaluatedDensity) / max(Density, 1e-20);
    }
    float3 Tangent = Unit(cross(abs(Normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0), Normal));
    float3 Bitangent = cross(Normal, Tangent);
    float Radius = sqrt(Radial), Azimuth = 6.283185307179586 * AzimuthDraw;
    float3 CosineDirection = Unit(Tangent * (Radius * cos(Azimuth)) + Bitangent * (Radius * sin(Azimuth)) +
                                  Normal * sqrt(max(0, 1 - Radius * Radius)));
    float CosineRadiance = 3.141592653589793 * Brdf(Receiver, CosineDirection).x;
    OutputColour[Pixel] = float4(ImportanceRadiance, CosineRadiance, IntegrandEstimate, DensityError);
}
