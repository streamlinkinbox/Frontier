/*==========================================================================================================================================
                                           REPLAYRIGIDTAILBREAKDOWN.SLANG
==========================================================================================================================================*/
// 🧩 Mirror the Engine's path loop solely to record per-bounce terms; compare its total against the Engine function.
void StoreRigidTailBounce(uint Pixel, uint Bounce, float3 Direct, float3 Hit, float State, float Facet)
{
    OutputColour[ReplaySlot(5, Pixel + 16 + Bounce)] = float4(Direct, State);
    OutputColour[ReplaySlot(6, Pixel + 16 + Bounce)] = float4(Hit, Facet);
}

float3 TraceRigidTailBreakdown(SurfacePoint Receiver, uint Seed, uint Pixel)
{
    for (uint Bounce = 0; Bounce < 4; ++Bounce) StoreRigidTailBounce(Pixel, Bounce, 0, 0, -1, -1);
    uint Bits = Seed;
    float3 Normal = Unit(Receiver.NormalRoughness.xyz);
    float3 Origin = Receiver.PositionHit.xyz + Normal * .001;
    float3 Throughput = 1, Radiance = 0;
    bool RoughOpaque = (Extent.Primary.w & 64u) != 0, SampleFacets = (Extent.Primary.w & 128u) != 0;
    bool ImportanceRough = (Extent.Primary.w & 4096u) != 0;
    SurfacePoint Current = Receiver;
    for (uint Bounce = 0; Bounce < Extent.Primary.z; ++Bounce)
    {
        float3 Direct = 0, Hit = 0;
        if (SampleFacets)
        {
            Direct = Throughput * ReplayFacetRadiance(Current, Origin, RoughOpaque, Bits);
            Radiance += Direct;
        }
        float3 Direction, Strike;
        if (ImportanceRough)
        {
            float Pdf;
            if (!ReplayImportanceDirection(Current, Bits, Direction, Pdf))
            {
                StoreRigidTailBounce(Pixel, Bounce, Direct, Hit, 0, -1);
                break;
            }
            Throughput *= Brdf(Current, Direction) * (max(0, dot(Normal, Direction)) / Pdf);
        }
        else
        {
            Direction = ReplayDirection(Normal, Bits);
            Throughput *= RoughOpaque ? 3.141592653589793 * Brdf(Current, Direction) : clamp(Current.AlbedoIor.xyz, 0, 1);
        }
        if (max(Throughput.x, max(Throughput.y, Throughput.z)) <= 0)
        {
            StoreRigidTailBounce(Pixel, Bounce, Direct, Hit, 0, -1);
            break;
        }
        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        if (Index == 0xffffffffu)
        {
            Hit = Throughput * max(Extent.Minimum.xyz, 0);
            Radiance += Hit;
            StoreRigidTailBounce(Pixel, Bounce, Direct, Hit, 1, -1);
            break;
        }
        FacetCorners F = Facets[Index];
        if (!SampleFacets || (Extent.Primary.w & 256u) != 0)
        {
            Hit = Throughput * max(F.Emissive.xyz, 0);
            Radiance += Hit;
        }
        else if ((Extent.Primary.w & 1024u) != 0 && any(F.Emissive.xyz > 0) && Extent.Reconstruction.z > 0)
        {
            float3 AreaNormal = cross(F.B.xyz - F.A.xyz, F.C.xyz - F.A.xyz);
            float DoubleArea = length(AreaNormal), DistanceSquared = Strike.x * Strike.x;
            if (DoubleArea > 0 && DistanceSquared > 0)
            {
                float ReceiverCosine = max(0, dot(Normal, Direction));
                float SourceCosine = abs(dot(AreaNormal / DoubleArea, Direction));
                float InverseProbability = ReplayFacetInverseProbability(Index);
                if (InverseProbability == 0)
                    Hit = Throughput * max(F.Emissive.xyz, 0);
                else
                {
                    float Ratio = ImportanceRough
                        ? ReplayImportancePdf(Current, Direction) * SourceCosine * (.5 * DoubleArea) *
                          InverseProbability / DistanceSquared
                        : ReceiverCosine * SourceCosine * (.5 * DoubleArea) * InverseProbability /
                          (3.141592653589793 * DistanceSquared);
                    Hit = Throughput * max(F.Emissive.xyz, 0) * (Ratio / (1 + Ratio));
                }
                Radiance += Hit;
            }
        }
        StoreRigidTailBounce(Pixel, Bounce, Direct, Hit, any(F.Emissive.xyz > 0) ? 3 : 2, float(Index));
        Normal = Unit(F.NormalA.xyz * (1 - Strike.y - Strike.z) + F.NormalB.xyz * Strike.y + F.NormalC.xyz * Strike.z);
        if (dot(Normal, Direction) > 0) Normal = -Normal;
        float3 HitPosition = Origin + Direction * Strike.x;
        Current = (SurfacePoint)0;
        Current.PositionHit = float4(HitPosition, 1);
        Current.NormalRoughness = float4(Normal, F.AlbedoRoughness.w);
        Current.ViewMetallic = float4(-Direction, F.OpticalToken.x);
        Current.AlbedoIor = float4(F.AlbedoRoughness.xyz, F.OpticalToken.y);
        Current.ResidualSpecular.w = F.OpticalToken.z;
        if ((Extent.Primary.w & 2048u) != 0 && F.Emissive.w > 0)
            ApplyRayMaterial(Current, F, Strike.yz);
        Origin = HitPosition + Normal * .001;
    }
    return Radiance;
}
