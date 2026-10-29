/*==========================================================================================================================================
                                                  PATHREPLAYRESERVOIR.SLANG
==========================================================================================================================================*/
// 🧩 Reservoir sampling of replayable diffuse or rough-opaque random sequences in a common positive-support domain.
// 📝 Every reused sequence is fully retraced in the current scene via ordered/legacy software BVH or VK_KHR_ray_query.
#include "LightInfluence.slang"
float2 EncodeReplaySeed(uint Seed) { return float2(float(Seed & 65535u), float(Seed >> 16u)); }
uint DecodeReplaySeed(float2 Packed) { return uint(Packed.x) | (uint(Packed.y) << 16u); }
// 📝 Keep float-to-int conversion defined even for extreme or non-finite reprojected coordinates.
bool ReplayMotionConvertible(float2 Pixel)
{
    return isfinite(Pixel.x) && isfinite(Pixel.y) && abs(Pixel.x) < 1073741824.0 && abs(Pixel.y) < 1073741824.0;
}
float3 ReplayDirection(float3 Normal, inout uint Bits)
{
    float Radius = sqrt(RandomUnit(Bits)), Azimuth = 6.283185307179586 * RandomUnit(Bits);
    float3 Tangent = Unit(cross(abs(Normal.z) < .999 ? float3(0,0,1) : float3(0,1,0), Normal));
    float3 Bitangent = cross(Normal, Tangent);
    return Unit(Tangent * (Radius * cos(Azimuth)) + Bitangent * (Radius * sin(Azimuth)) +
                Normal * sqrt(max(0, 1 - Radius * Radius)));
}
// 📝 The GGX NDF samples all microfacet normals, including ones whose reflections leave the upper hemisphere.
//    Such draws are zero-contribution events, not resampled. Cosine retains support over the whole hemisphere.
float ReplayImportancePdf(SurfacePoint Receiver, float3 Direction)
{
    float3 N = Unit(Receiver.NormalRoughness.xyz), V = Unit(Receiver.ViewMetallic.xyz);
    float NL = max(0, dot(N, Direction));
    if (NL <= 0) return 0;
    float3 H = Unit(V + Direction);
    float NH = max(0, dot(N, H)), VH = max(0, dot(V, H));
    float Ggx = VH > 0 ? EvaluateGgxDistribution(NH, Receiver.NormalRoughness.w) * NH / (4 * VH) : 0;
    return .5 * (NL / 3.141592653589793 + Ggx);
}
bool ReplayImportanceDirection(SurfacePoint Receiver, inout uint Bits, out float3 Direction, out float Pdf)
{
    float3 N = Unit(Receiver.NormalRoughness.xyz), V = Unit(Receiver.ViewMetallic.xyz);
    Direction = 0;
    Pdf = 0;
    if (RandomUnit(Bits) < .5)
        Direction = ReplayDirection(N, Bits);
    else
    {
        float U = RandomUnit(Bits), Azimuth = 6.283185307179586 * RandomUnit(Bits);
        float Roughness = Receiver.NormalRoughness.w, α = Roughness * Roughness, α2 = α * α;
        float Cosine = sqrt((1 - U) / (1 + (α2 - 1) * U));
        float Sine = sqrt(max(0, 1 - Cosine * Cosine));
        float3 Tangent = Unit(cross(abs(N.z) < .999 ? float3(0,0,1) : float3(0,1,0), N));
        float3 Half = Unit(Tangent * (Sine * cos(Azimuth)) + cross(N, Tangent) * (Sine * sin(Azimuth)) + N * Cosine);
        float ViewHalf = dot(V, Half);
        if (ViewHalf <= 0) return false;
        Direction = Unit(2 * ViewHalf * Half - V);
        if (dot(N, Direction) <= 0) return false;
    }
    Pdf = ReplayImportancePdf(Receiver, Direction);
    return Pdf > 0;
}
// 📝 Evaluate multi-strata PBR mixture density (or SampleGgx direction when SampleMode == true) using pure scalar
//    accumulators over Angles[] so SPIR-V compilers never emit dynamic local-array gather/scatter cascades.
bool EvaluateOrSampleReplayPbrLobes(SurfacePoint Receiver, bool SampleMode, float Selector, float U1, float U2,
                                    inout float3 Direction, out float Pdf)
{
    Pdf = 0.0;
    float3 N = Unit(Receiver.NormalRoughness.xyz), V = Unit(Receiver.ViewMetallic.xyz);
    float NV = saturate(dot(N, V));
    if (NV <= 0.0) return false;
    uint Address = MaterialAddress(Receiver);
    uint AuthoredCount = Address > 0u ? min(uint(Angles[Address - 1u]), 8u) : 0u;
    float DiffuseWeight = 0.5;
    float SingleRough = clamp(Receiver.NormalRoughness.w, 0.04, 1.0);
    float SpecSum = 0.0;

    if (AuthoredCount <= 1u)
    {
        float Ior = max(Receiver.AlbedoIor.w, 1.0), Ratio = (Ior - 1.0) / (Ior + 1.0);
        float Metallic = saturate(Receiver.ViewMetallic.w);
        float Dielectric = Ratio * Ratio * saturate(Receiver.ResidualSpecular.w) * 2.0;
        float3 BaseAlbedo = clamp(Receiver.AlbedoIor.xyz, 0.0, 1.0);
        float3 F0 = lerp(float3(Dielectric), BaseAlbedo, Metallic);
        float3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - NV, 5.0);
        float SpecEnergy = max(Power(Fresnel), 0.02);
        float DiffEnergy = (1.0 - Metallic) * Power(BaseAlbedo) * max(0.0, 1.0 - Power(Fresnel));
        if (AuthoredCount == 1u)
        {
            uint SheenBase = (Address - 1u) + 24u;
            float SheenAlpha = saturate(Angles[SheenBase + 3u]);
            if (SheenAlpha > 0.0)
            {
                float3 SheenRgb = saturate(float3(Angles[SheenBase], Angles[SheenBase + 1u], Angles[SheenBase + 2u]));
                DiffEnergy += SheenAlpha * Power(SheenRgb);
            }
        }
        float SpecWeight = clamp(SpecEnergy / (SpecEnergy + DiffEnergy + 1e-4), 0.15, 0.90);
        DiffuseWeight = 1.0 - SpecWeight;
        SpecSum = SpecWeight;
    }
    else
    {
        uint BaseOffset = Address - 1u;
        float Transmission = 1.0, DiffAccum = 0.0;
        for (int Layer = int(AuthoredCount) - 1; Layer >= 0; --Layer)
        {
            uint P = BaseOffset + 8u + uint(Layer) * 24u;
            float3 Albedo = Layer == 0
                ? clamp(Receiver.AlbedoIor.xyz, 0.0, 1.0)
                : saturate(float3(Angles[P], Angles[P + 1u], Angles[P + 2u]));
            float Metallic = saturate(Layer == 0 ? Receiver.ViewMetallic.w : Angles[P + 4u]);
            float Ior = max(Angles[P + 5u], 1.0), R0 = (Ior - 1.0) / (Ior + 1.0);
            R0 *= R0;
            float Coverage = saturate(Angles[P + 6u]);
            float3 F0 = lerp(float3(R0), Albedo, Metallic);
            float FresnelPower = Power(F0 + (1.0 - F0) * pow(1.0 - NV, 5.0));
            if (uint(Angles[P + 12u]) == 1u)
            {
                SpecSum += Transmission * Coverage * max(FresnelPower, 0.22);
                Transmission *= max(0.05, 1.0 - Coverage * FresnelPower);
            }
            else
            {
                SpecSum += Transmission * Coverage * max(FresnelPower, 0.04);
                float SheenAlpha = saturate(Angles[P + 19u]);
                float3 SheenRgb = saturate(float3(Angles[P + 16u], Angles[P + 17u], Angles[P + 18u]));
                DiffAccum += Transmission * Coverage *
                    ((1.0 - Metallic) * Power(Albedo) * max(0.0, 1.0 - FresnelPower) +
                     SheenAlpha * Power(SheenRgb));
                Transmission *= max(0.0, 1.0 - Coverage);
            }
        }
        float DiffBudget = max(DiffAccum, 0.08);
        DiffuseWeight = clamp(DiffBudget / (SpecSum + DiffBudget + 1e-6), 0.10, 0.80);
    }

    float SpecScale = (1.0 - DiffuseWeight) / max(SpecSum, 1e-6);

    if (SampleMode)
    {
        float3 Tangent = Unit(cross(abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0), N));
        float3 Bitangent = cross(N, Tangent);
        float Azimuth = 6.283185307179586 * U2;
        if (Selector < DiffuseWeight)
        {
            float Radius = sqrt(U1);
            Direction = Unit(Tangent * (Radius * cos(Azimuth)) + Bitangent * (Radius * sin(Azimuth)) +
                             N * sqrt(max(0.0, 1.0 - Radius * Radius)));
        }
        else
        {
            float ChosenRoughness = SingleRough;
            if (AuthoredCount > 1u)
            {
                uint BaseOffset = Address - 1u;
                float Transmission = 1.0, Cumulative = DiffuseWeight;
                for (int Layer = int(AuthoredCount) - 1; Layer >= 0; --Layer)
                {
                    uint P = BaseOffset + 8u + uint(Layer) * 24u;
                    float3 Albedo = Layer == 0
                        ? clamp(Receiver.AlbedoIor.xyz, 0.0, 1.0)
                        : saturate(float3(Angles[P], Angles[P + 1u], Angles[P + 2u]));
                    float LayerRough = clamp(Layer == 0 ? Receiver.NormalRoughness.w : Angles[P + 3u], 0.04, 1.0);
                    float Metallic = saturate(Layer == 0 ? Receiver.ViewMetallic.w : Angles[P + 4u]);
                    float Ior = max(Angles[P + 5u], 1.0), R0 = (Ior - 1.0) / (Ior + 1.0);
                    R0 *= R0;
                    float Coverage = saturate(Angles[P + 6u]);
                    float3 F0 = lerp(float3(R0), Albedo, Metallic);
                    float FresnelPower = Power(F0 + (1.0 - F0) * pow(1.0 - NV, 5.0));
                    bool IsCoating = uint(Angles[P + 12u]) == 1u;
                    float LobeWeight = SpecScale * Transmission * Coverage *
                                       max(FresnelPower, IsCoating ? 0.22 : 0.04);
                    Transmission *= IsCoating ? max(0.05, 1.0 - Coverage * FresnelPower) : max(0.0, 1.0 - Coverage);
                    Cumulative += LobeWeight;
                    ChosenRoughness = LayerRough;
                    if (Selector <= Cumulative || Layer == 0) break;
                }
            }
            float Alpha = ChosenRoughness * ChosenRoughness, Alpha2 = Alpha * Alpha;
            float Cosine = sqrt((1.0 - U1) / (1.0 + (Alpha2 - 1.0) * U1));
            float Sine = sqrt(max(0.0, 1.0 - Cosine * Cosine));
            float3 Half = Unit(Tangent * (Sine * cos(Azimuth)) + Bitangent * (Sine * sin(Azimuth)) + N * Cosine);
            float ViewHalf = dot(V, Half);
            if (ViewHalf <= 0.0) return false;
            Direction = Unit(2.0 * ViewHalf * Half - V);
        }
    }

    float NL = max(0.0, dot(N, Direction));
    if (NL <= 0.0) return false;
    float Density = DiffuseWeight * (NL / 3.141592653589793);
    float3 H = Unit(V + Direction);
    float NH = max(0.0, dot(N, H)), VH = max(0.0, dot(V, H));
    if (VH > 0.0)
    {
        if (AuthoredCount <= 1u)
        {
            Density += (1.0 - DiffuseWeight) * (EvaluateGgxDistribution(NH, SingleRough) * NH / (4.0 * VH));
        }
        else
        {
            uint BaseOffset = Address - 1u;
            float Transmission = 1.0;
            for (int Layer = int(AuthoredCount) - 1; Layer >= 0; --Layer)
            {
                uint P = BaseOffset + 8u + uint(Layer) * 24u;
                float3 Albedo = Layer == 0
                    ? clamp(Receiver.AlbedoIor.xyz, 0.0, 1.0)
                    : saturate(float3(Angles[P], Angles[P + 1u], Angles[P + 2u]));
                float LayerRough = clamp(Layer == 0 ? Receiver.NormalRoughness.w : Angles[P + 3u], 0.04, 1.0);
                float Metallic = saturate(Layer == 0 ? Receiver.ViewMetallic.w : Angles[P + 4u]);
                float Ior = max(Angles[P + 5u], 1.0), R0 = (Ior - 1.0) / (Ior + 1.0);
                R0 *= R0;
                float Coverage = saturate(Angles[P + 6u]);
                float3 F0 = lerp(float3(R0), Albedo, Metallic);
                float FresnelPower = Power(F0 + (1.0 - F0) * pow(1.0 - NV, 5.0));
                bool IsCoating = uint(Angles[P + 12u]) == 1u;
                float LobeWeight = SpecScale * Transmission * Coverage *
                                   max(FresnelPower, IsCoating ? 0.22 : 0.04);
                Transmission *= IsCoating ? max(0.05, 1.0 - Coverage * FresnelPower) : max(0.0, 1.0 - Coverage);
                Density += LobeWeight * (EvaluateGgxDistribution(NH, LayerRough) * NH / (4.0 * VH));
            }
        }
    }
    Pdf = Density;
    return Pdf > 0.0;
}
float ReplayActiveImportancePdf(SurfacePoint Receiver, float3 Direction)
{
    if ((Extent.Primary.w & 134217728u) == 0)
        return ReplayImportancePdf(Receiver, Direction);
    float Pdf;
    float3 DirCopy = Direction;
    return EvaluateOrSampleReplayPbrLobes(Receiver, false, 0.0, 0.0, 0.0, DirCopy, Pdf) ? Pdf : 0.0;
}
bool ReplayActiveImportanceDirection(SurfacePoint Receiver, inout uint Bits, out float3 Direction, out float Pdf)
{
    if ((Extent.Primary.w & 134217728u) == 0)
        return ReplayImportanceDirection(Receiver, Bits, Direction, Pdf);
    Direction = 0;
    float Selector = RandomUnit(Bits), U1 = RandomUnit(Bits), U2 = RandomUnit(Bits);
    return EvaluateOrSampleReplayPbrLobes(Receiver, true, Selector, U1, U2, Direction, Pdf);
}
float ReplayMisWeight(float Ratio, bool BsdfSample)
{
    if ((Extent.Primary.w & 33554432u) == 0)
        return BsdfSample ? (Ratio / (1.0 + Ratio)) : (1.0 / (1.0 + Ratio));
    if (!isfinite(Ratio) || Ratio <= 0.0) return BsdfSample ? 0.0 : 1.0;
    float Small = Ratio <= 1.0 ? Ratio : (1.0 / Ratio);
    float Squared = Small * Small;
    float Low = Squared / (1.0 + Squared);
    float High = 1.0 / (1.0 + Squared);
    return BsdfSample ? (Ratio <= 1.0 ? Low : High) : (Ratio <= 1.0 ? High : Low);
}
float3 EvaluateThinSheetPaneTransmit(FacetCorners F, float2 Barycentric, float3 Direction)
{
    uint Offset = uint(F.Emissive.w) - 1u;
    float3 PaneNormal =
        Unit(F.NormalA.xyz * (1.0 - Barycentric.x - Barycentric.y) + F.NormalB.xyz * Barycentric.x +
             F.NormalC.xyz * Barycentric.y);
    float Cosine = max(0.05, abs(dot(PaneNormal, Direction)));
    float PaneIor = max(Angles[Offset + 13u], 1.0), R0 = (PaneIor - 1.0) / (PaneIor + 1.0);
    R0 *= R0;
    float Reflectance = R0 + (1.0 - R0) * pow(1.0 - Cosine, 5.0);
    float3 AuthoredSigma = max(0.0, float3(Angles[Offset + 16u], Angles[Offset + 17u], Angles[Offset + 18u]));
    float AuthoredThickness = max(0.0, Angles[Offset + 19u]);
    float3 OpticalDepth = AuthoredThickness > 0.0 ? AuthoredSigma * AuthoredThickness
                                                  : float3(0.8, 0.15, 0.4) * 0.006;
    return ((1.0 - Reflectance) / (1.0 + Reflectance)) * exp(-OpticalDepth / Cosine);
}
// 📝 Test the open segment, skipping the sampled endpoint triangle and attenuating up to 8 thin-sheet panes.
float3 ReplaySegmentTransmittance(float3 Origin, float3 Direction, float Distance, uint Endpoint)
{
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
#ifdef FRONTIER_HARDWARE_RAY_QUERY
    if (Extent.Sampling.z == 0 || Distance <= 0.0) return float3(1.0);
    InterlockedAdd(Counters[2], 1);
    float3 Throughput = 1.0;
    uint Panes = 0u;
    RayDesc Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    Ray.TMin = AuthoredMaterials ? 0.0001 : 1e-6;
    Ray.TMax = Distance;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> Query;
    Query.TraceRayInline(SceneAcceleration, RAY_FLAG_FORCE_NON_OPAQUE, 0xffu, Ray);
    while (Query.Proceed())
    {
        if (Query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            uint Index = Query.CandidatePrimitiveIndex();
            float HitT = Query.CandidateTriangleRayT();
            if (Index == Endpoint || HitT <= 0.0 || HitT >= (AuthoredMaterials ? Distance - 1e-4 : Distance)) continue;
            if (!AuthoredMaterials)
            {
                Query.CommitNonOpaqueTriangleHit();
                return 0.0;
            }
            FacetCorners F = Facets[Index];
            uint Address = F.Emissive.w > 0.0 ? uint(F.Emissive.w) : 0u;
            if (Address == 0u || uint(Angles[Address]) != 2u || ++Panes > 8u)
            {
                Query.CommitNonOpaqueTriangleHit();
                return 0.0;
            }
            Throughput *= EvaluateThinSheetPaneTransmit(F, Query.CandidateTriangleBarycentrics(), Direction);
        }
    }
    return Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? float3(0.0) : Throughput;
#else
    if (Extent.Grid.w == 0 || Distance <= 0.0) return float3(1.0);
    InterlockedAdd(Counters[2], 1);
    float3 Throughput = 1.0;
    uint Panes = 0u;
    bool Ordered = (Extent.Primary.w & 8388608u) != 0;
    float MinT = AuthoredMaterials ? 0.0001 : 0.0;
    float MaxT = AuthoredMaterials ? Distance - 1e-4 : Distance;
    float3 AbsDir = abs(Direction);
    bool PreferLower = AbsDir.x >= AbsDir.y
        ? (AbsDir.x >= AbsDir.z ? Direction.x >= 0.0 : Direction.z >= 0.0)
        : (AbsDir.y >= AbsDir.z ? Direction.y >= 0.0 : Direction.z >= 0.0);
    uint Stack[64], Count = 1;
    Stack[0] = 0;
    uint MaxStack = min(64u, max(1u, Extent.Sampling.w));
    while (Count > 0)
    {
        FacetEnclosure Box = Enclosures[Stack[--Count]];
        ++ReplayThreadBoxTests;
        if (!IntersectsBox(Box, Origin, Direction, Distance)) continue;
        if (Box.Links.z != 0)
        {
            for (uint I = 0; I < Box.Links.y; ++I)
            {
                uint Index = Box.Links.x + I;
                if (Index != Endpoint)
                {
                    float3 Strike;
                    ++ReplayThreadTriTests;
                    if (FacetStrike(Facets[Index], Origin, Direction, MinT, MaxT, Strike))
                    {
                        if (!AuthoredMaterials) return 0.0;
                        FacetCorners F = Facets[Index];
                        uint Address = F.Emissive.w > 0.0 ? uint(F.Emissive.w) : 0u;
                        if (Address == 0u || uint(Angles[Address]) != 2u || ++Panes > 8u) return 0.0;
                        Throughput *= EvaluateThinSheetPaneTransmit(F, Strike.yz, Direction);
                    }
                }
            }
        }
        else
        {
            if (Count + 2 > MaxStack)
            {
                InterlockedAdd(Counters[3], 1);
                for (uint I = 0; I < Extent.Sampling.z; ++I)
                {
                    if (I != Endpoint)
                    {
                        float3 Strike;
                        ++ReplayThreadTriTests;
                        if (FacetStrike(Facets[I], Origin, Direction, MinT, MaxT, Strike))
                        {
                            if (!AuthoredMaterials) return 0.0;
                            FacetCorners F = Facets[I];
                            uint Address = F.Emissive.w > 0.0 ? uint(F.Emissive.w) : 0u;
                            if (Address == 0u || uint(Angles[Address]) != 2u || ++Panes > 8u) return 0.0;
                            Throughput *= EvaluateThinSheetPaneTransmit(F, Strike.yz, Direction);
                        }
                    }
                }
                return Throughput;
            }
            if (Ordered)
            {
                Stack[Count++] = PreferLower ? Box.Links.y : Box.Links.x;
                Stack[Count++] = PreferLower ? Box.Links.x : Box.Links.y;
            }
            else
            {
                Stack[Count++] = Box.Links.x;
                Stack[Count++] = Box.Links.y;
            }
        }
    }
    return Throughput;
#endif
}
// 📝 A half-uniform mixture bounds each emitter's selection probability below by 0.5 / FacetCount.
float ReplayFacetProbability(uint Entry, uint FacetCount)
{
    float Probability = Angles[Entry + 1];
    if ((Extent.Primary.w & 16384u) != 0)
        Probability = .5 * Probability + .5 / float(FacetCount);
    return Probability;
}

// 📝 The Engine CDF is ordered by facet index. Return zero for a source outside that distribution.
float ReplayFacetInverseProbability(uint FacetIndex)
{
    uint FacetCount = Extent.Reconstruction.z;
    if ((Extent.Primary.w & 8192u) == 0) return float(FacetCount);
    uint Offset = asuint(Extent.Maximum.y), Low = 0, High = FacetCount;
    while (Low < High)
    {
        uint Mid = (Low + High) / 2;
        if (uint(Angles[Offset + 1 + Mid * 4]) < FacetIndex) Low = Mid + 1;
        else High = Mid;
    }
    if (Low >= FacetCount || uint(Angles[Offset + 1 + Low * 4]) != FacetIndex) return 0;
    return 1 / ReplayFacetProbability(Offset + 1 + Low * 4, FacetCount);
}

// 📝 Select an emissive facet by the optional Engine CDF or uniformly, then trace visibility and compute NEE weight.
bool SampleReplayFacetDirect(SurfacePoint Receiver, float3 Origin, inout uint Bits,
                             out float3 OutDirection, out float3 OutFactor)
{
    OutDirection = 0;
    OutFactor = 0;
    uint FacetCount = Extent.Reconstruction.z;
    if (FacetCount == 0) return false;
    uint Offset = asuint(Extent.Maximum.y), FacetIndex;
    float InverseProbability;
    if ((Extent.Primary.w & 8192u) != 0)
    {
        bool UniformDraw = false;
        if ((Extent.Primary.w & 16384u) != 0) UniformDraw = RandomUnit(Bits) < .5;
        uint Selected;
        if (UniformDraw)
        {
            uint Threshold = (0u - FacetCount) % FacetCount, SampleBits;
            do { SampleBits = NextRandom(Bits); } while (SampleBits < Threshold);
            Selected = SampleBits % FacetCount;
        }
        else
        {
            float Draw = RandomUnit(Bits);
            uint Low = 0, High = FacetCount - 1;
            while (Low < High)
            {
                uint Mid = (Low + High) / 2;
                if (Draw < Angles[Offset + 1 + Mid * 4 + 3]) High = Mid;
                else Low = Mid + 1;
            }
            Selected = Low;
        }
        uint Entry = Offset + 1 + Selected * 4;
        FacetIndex = uint(Angles[Entry]);
        InverseProbability = 1 / ReplayFacetProbability(Entry, FacetCount);
    }
    else
    {
        // 📝 Rejection removes modulo bias for a uniformly distributed 32-bit draw.
        uint Threshold = (0u - FacetCount) % FacetCount, SampleBits;
        do { SampleBits = NextRandom(Bits); } while (SampleBits < Threshold);
        FacetIndex = uint(Angles[Offset + SampleBits % FacetCount]);
        InverseProbability = float(FacetCount);
    }
    FacetCorners F = Facets[FacetIndex];
    float3 EdgeA = F.B.xyz - F.A.xyz, EdgeB = F.C.xyz - F.A.xyz;
    float3 AreaNormal = cross(EdgeA, EdgeB);
    float DoubleArea = length(AreaNormal);
    float Root = sqrt(RandomUnit(Bits)), Along = RandomUnit(Bits);
    float3 Point = F.A.xyz * (1 - Root) + F.B.xyz * (Root * (1 - Along)) + F.C.xyz * (Root * Along);
    float3 Delta = Point - Origin;
    float DistanceSquared = dot(Delta, Delta);
    if (DoubleArea <= 0 || DistanceSquared <= 1e-10) return false;
    float Distance = sqrt(DistanceSquared);
    float3 Direction = Delta / Distance;
    float3 N = Unit(Receiver.NormalRoughness.xyz);
    float DirectionCosine = max(0, dot(N, Direction));
    float SourceCosine = abs(dot(AreaNormal / DoubleArea, Direction));
    if (DirectionCosine <= 0 || SourceCosine <= 0) return false;
    float3 Transmittance = ReplaySegmentTransmittance(Origin, Direction, Distance, FacetIndex);
    if (all(Transmittance <= 0)) return false;
    float Measure = DirectionCosine * SourceCosine * (.5 * DoubleArea) * InverseProbability / DistanceSquared;
    float MisWeight = 1;
    if ((Extent.Primary.w & 1024u) != 0)
    {
        float Ratio = (Extent.Primary.w & 4096u) != 0
            ? ReplayActiveImportancePdf(Receiver, Direction) * SourceCosine * (.5 * DoubleArea) *
              InverseProbability / DistanceSquared
            : Measure / 3.141592653589793;
        MisWeight = ReplayMisWeight(Ratio, false);
    }
    OutDirection = Direction;
    OutFactor = Transmittance * max(F.Emissive.xyz, 0) * (Measure * MisWeight);
    return true;
}
float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed)
{
    uint Bits = Seed;
    float3 Normal = Unit(Receiver.NormalRoughness.xyz);
    float3 Origin = Receiver.PositionHit.xyz + Normal * .001;
    float3 Throughput = 1, Radiance = 0;
    bool RoughOpaque = (Extent.Primary.w & 64u) != 0, SampleFacets = (Extent.Primary.w & 128u) != 0;
    bool ImportanceRough = (Extent.Primary.w & 4096u) != 0;
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    SurfacePoint Current = Receiver;
    for (uint Bounce = 0; Bounce < Extent.Primary.z; ++Bounce)
    {
        bool ThinSheet = AuthoredMaterials && MaterialAddress(Current) > 0 && MaterialTransport(Current) == 2u;
        float3 NeeDirection = 0, NeeFactor = 0;
        bool HasNee = SampleFacets && !ThinSheet &&
                      SampleReplayFacetDirect(Current, Origin, Bits, NeeDirection, NeeFactor);

        float3 Direction = 0, Strike = 0;
        float BouncePdf = 0.0, BounceScale = 3.141592653589793;
        bool ValidBounce = true;
        if (ThinSheet)
        {
            uint Offset = MaterialAddress(Current) - 1u;
            float3 IncomingDir = -Unit(Current.ViewMetallic.xyz);
            float Cosine = max(0.05, saturate(dot(Normal, -IncomingDir)));
            float PaneIor = max(Angles[Offset + 13u], 1.0), R0 = (PaneIor - 1.0) / (PaneIor + 1.0);
            R0 *= R0;
            float F = R0 + (1.0 - R0) * pow(1.0 - Cosine, 5.0);
            float SheetReflect = (2.0 * F) / (1.0 + F);
            float3 AuthoredSigma = max(0.0, float3(Angles[Offset + 16u], Angles[Offset + 17u], Angles[Offset + 18u]));
            float AuthoredThickness = max(0.0, Angles[Offset + 19u]);
            float3 OpticalDepth = AuthoredThickness > 0.0 ? AuthoredSigma * AuthoredThickness
                                                          : float3(0.8, 0.15, 0.4) * 0.006;
            float3 SheetTransmit = ((1.0 - F) / (1.0 + F)) * exp(-OpticalDepth / Cosine);
            float ReflectProb = clamp(SheetReflect, 0.08, 0.92);
            float Selector = RandomUnit(Bits);
            (void)RandomUnit(Bits);
            (void)RandomUnit(Bits);
            if (Selector < ReflectProb)
            {
                Direction = Unit(reflect(IncomingDir, Normal));
                Throughput *= SheetReflect / ReflectProb;
                Origin = Current.PositionHit.xyz + Normal * 0.002;
            }
            else
            {
                Direction = IncomingDir;
                Throughput *= SheetTransmit / (1.0 - ReflectProb);
                Origin = Current.PositionHit.xyz - Normal * 0.002;
            }
        }
        else
        {
            if (ImportanceRough)
            {
                ValidBounce = ReplayActiveImportanceDirection(Current, Bits, Direction, BouncePdf);
                if (ValidBounce) BounceScale = max(0.0, dot(Normal, Direction)) / BouncePdf;
            }
            else Direction = ReplayDirection(Normal, Bits);

            float3 NeeBrdf = clamp(Current.AlbedoIor.xyz, 0, 1) / 3.141592653589793;
            float3 BounceBrdf = clamp(Current.AlbedoIor.xyz, 0, 1);
            if (RoughOpaque)
            {
                for (uint EvalSlot = 0; EvalSlot < 2u; ++EvalSlot)
                {
                    bool Active = EvalSlot == 0u ? HasNee : ValidBounce;
                    if (!Active) continue;
                    float3 EvalVal = Brdf(Current, EvalSlot == 0u ? NeeDirection : Direction);
                    if (EvalSlot == 0u) NeeBrdf = EvalVal;
                    else BounceBrdf = BounceScale * EvalVal;
                }
            }
            if (HasNee) Radiance += Throughput * NeeBrdf * NeeFactor;
            if (!ValidBounce) break;
            Throughput *= BounceBrdf;
        }
        if (max(Throughput.x, max(Throughput.y, Throughput.z)) <= 0) break;
        InterlockedAdd(Counters[8], 1);
        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        if (Index == 0xffffffffu)
        {
            Radiance += Throughput * max(Extent.Minimum.xyz, 0); // Uniform environment, not an importance map.
            break;
        }
        FacetCorners F = Facets[Index];
        // 📝 Without balance weighting (or after a delta thin-sheet bounce), emissive hits receive full weight.
        if (ThinSheet || !SampleFacets || (Extent.Primary.w & 256u) != 0)
            Radiance += Throughput * max(F.Emissive.xyz, 0);
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
                    Radiance += Throughput * max(F.Emissive.xyz, 0);
                else
                {
                    float Ratio = ImportanceRough
                        ? BouncePdf * SourceCosine * (.5 * DoubleArea) * InverseProbability / DistanceSquared
                        : ReceiverCosine * SourceCosine * (.5 * DoubleArea) * InverseProbability /
                          (3.141592653589793 * DistanceSquared);
                    Radiance += Throughput * max(F.Emissive.xyz, 0) * ReplayMisWeight(Ratio, true);
                }
            }
        }
        float3 GeometricNormal =
            Unit(F.NormalA.xyz * (1 - Strike.y - Strike.z) + F.NormalB.xyz * Strike.y + F.NormalC.xyz * Strike.z);
        if (dot(GeometricNormal, Direction) > 0) GeometricNormal = -GeometricNormal;
        float3 HitPosition = Origin + Direction * Strike.x;
        Current = (SurfacePoint)0;
        Current.PositionHit = float4(HitPosition, 1);
        Current.NormalRoughness = float4(GeometricNormal, F.AlbedoRoughness.w);
        Current.ViewMetallic = float4(-Direction, F.OpticalToken.x);
        Current.AlbedoIor = float4(F.AlbedoRoughness.xyz, F.OpticalToken.y);
        Current.ResidualSpecular.w = F.OpticalToken.z;
        if (((Extent.Primary.w & 2048u) != 0 || AuthoredMaterials) && F.Emissive.w > 0)
        {
            ApplyRayMaterial(Current, F, Strike.yz);
            if (AuthoredMaterials &&
                (dot(Current.NormalRoughness.xyz, GeometricNormal) < 0.05 ||
                 dot(Current.NormalRoughness.xyz, Current.ViewMetallic.xyz) <= 1e-4))
                Current.NormalRoughness.xyz = GeometricNormal;
        }
        Normal = Unit(Current.NormalRoughness.xyz);
        Origin = HitPosition + GeometricNormal * .001;
    }
    return Radiance;
}
float ReplayEffectiveSpecularRoughness(SurfacePoint R)
{
    uint Address = MaterialAddress(R);
    if ((Extent.Primary.w & 134217728u) == 0 || Address == 0u)
        return clamp(R.NormalRoughness.w, 0.04, 1.0);
    if (MaterialTransport(R) == 2u) return 0.04;
    uint Offset = Address - 1u;
    uint Count = min(uint(Angles[Offset]), 8u);
    float Effective = clamp(R.NormalRoughness.w, 0.04, 1.0);
    for (uint Index = 1u; Index < Count; ++Index)
    {
        uint P = Offset + 8u + Index * 24u;
        if (Angles[P + 6u] > 0.05)
            Effective = min(Effective, clamp(Angles[P + 3u], 0.04, 1.0));
    }
    return Effective;
}
// 📝 Every half is an exactly representable uint16, avoiding float-to-uint conversion of corrupt history.
bool ReplaySeedHalfValid(float HalfBits)
{
    return isfinite(HalfBits) && HalfBits >= 0 && HalfBits <= 65535 && floor(HalfBits) == HalfBits;
}
bool ReplayReservoirHistoryValid(float4 SourceSlot)
{
    return ReplaySeedHalfValid(SourceSlot.x) && ReplaySeedHalfValid(SourceSlot.y) &&
        isfinite(SourceSlot.z) && SourceSlot.z > 0 &&
        isfinite(SourceSlot.w) && SourceSlot.w >= 1 && SourceSlot.w <= 71 &&
        floor(SourceSlot.w) == SourceSlot.w;
}
bool ReplaySeedBankValid(float4 First, float4 Second)
{
    float Count = Second.z;
    return isfinite(Count) && Count >= 1 && Count <= 3 && floor(Count) == Count &&
        ReplaySeedHalfValid(First.x) && ReplaySeedHalfValid(First.y) &&
        (Count < 2 || (ReplaySeedHalfValid(First.z) && ReplaySeedHalfValid(First.w))) &&
        (Count < 3 || (ReplaySeedHalfValid(Second.x) && ReplaySeedHalfValid(Second.y)));
}
// 📝 Stratified 4-tap Vogel disk (2.15 px to 6.0 px angular-adaptive radius): r_0 >= 0.76 px guarantees non-zero offsets.
int2 ProjectSpatialDiskOffset(uint Pixel, uint SequenceSeed, uint TapIndex)
{
    uint SpatialBits = (SequenceSeed * 1664525u + 1013904223u) ^ (Pixel * 2246822519u + 3266489917u);
    float Azimuth = 6.283185307179586 * RandomUnit(SpatialBits) + float(TapIndex) * 2.39996323;
    float MaxRadius = clamp(float(min(Extent.History.x, Extent.History.y)) * (6.0 / 192.0), 2.15, 6.0);
    float Radius = MaxRadius * sqrt((float(TapIndex) + 0.5) * 0.25);
    return int2(floor(float2(cos(Azimuth), sin(Azimuth)) * Radius + 0.5));
}
// 📝 Symmetric tangent-plane and normal gate over deterministic primary hits; preserves grazing roads without bias.
bool EvaluateSpatialSurfaceCompatibility(SurfacePoint Receiver, SurfacePoint DonorReceiver)
{
    bool RoughEnabled = (Extent.Primary.w & 64u) != 0;
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    if (DonorReceiver.PositionHit.w == 0 || DonorReceiver.MotionPositionToken.w <= 0 ||
        (DonorReceiver.ViewMetallic.w != 0 && !RoughEnabled))
        return false;
    if (AuthoredMaterials && MaterialTransport(Receiver) != MaterialTransport(DonorReceiver))
        return false;
    float ReceiverNormalSquared = dot(Receiver.NormalRoughness.xyz, Receiver.NormalRoughness.xyz);
    float DonorNormalSquared = dot(DonorReceiver.NormalRoughness.xyz, DonorReceiver.NormalRoughness.xyz);
    if (ReceiverNormalSquared <= 1e-12 || DonorNormalSquared <= 1e-12) return false;
    float3 ReceiverNormal = Receiver.NormalRoughness.xyz * rsqrt(ReceiverNormalSquared);
    float3 DonorNormal = DonorReceiver.NormalRoughness.xyz * rsqrt(DonorNormalSquared);
    float NormalFloor = AuthoredMaterials && ReplayEffectiveSpecularRoughness(Receiver) < 0.14 ? 0.98480775 : 0.90630779;
    if (dot(ReceiverNormal, DonorNormal) < NormalFloor) return false;
    float3 Separation = DonorReceiver.PositionHit.xyz - Receiver.PositionHit.xyz;
    float Footstep = isfinite(Receiver.MotionPixel.z) ? max(Receiver.MotionPixel.z, 0.0) : 0.0;
    float PlaneAllowance = max(0.05, 2.0 * min(Footstep, 0.25));
    if (abs(dot(Separation, ReceiverNormal)) > PlaneAllowance || abs(dot(Separation, DonorNormal)) > PlaneAllowance)
        return false;
    if (RoughEnabled &&
        abs(ReplayEffectiveSpecularRoughness(Receiver) - ReplayEffectiveSpecularRoughness(DonorReceiver)) >
            (AuthoredMaterials ? 0.18 : 0.25))
        return false;
    return true;
}
float ReplayTarget(float3 Radiance) { return max(0, Power(Radiance)) + Extent.Maximum.x; }
void FlushSecondaryTraversalCounters(uint Pixel)
{
    if (ReplayThreadBoxTests > 0) InterlockedAdd(Counters[18], ReplayThreadBoxTests);
    if (ReplayThreadTriTests > 0) InterlockedAdd(Counters[19], ReplayThreadTriTests);
    if ((Extent.Primary.w & 2097152u) != 0)
    {
        float4 Prior = OutputColour[ReplaySlot(9, Pixel)];
        OutputColour[ReplaySlot(9, Pixel)] =
            float4(Prior.xy, Prior.zw + float2(float(ReplayThreadBoxTests), float(ReplayThreadTriTests)));
    }
}
[shader("compute")][numthreads(64,1,1)] void GenerateReplayReservoirs(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    ReplayThreadBoxTests = 0;
    ReplayThreadTriTests = 0;
    if ((Extent.Primary.w & 2097152u) != 0 && Extent.Primary.x == 0)
        OutputColour[ReplaySlot(9, Pixel)] = 0;
    SurfacePoint R = Receivers[Pixel];
    float4 Packed = 0, SelectedRadiance = 0;
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 &&
        (R.ViewMetallic.w == 0 || (Extent.Primary.w & 64u) != 0))
    {
        uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
        uint Selected = NextRandom(SeedBits);
        float3 Radiance = EvaluateReplayPath(R, Selected);
        float Target = ReplayTarget(Radiance), Sum = Target, Count = 1;
        for (uint CandidateIndex = 1; CandidateIndex < max(Extent.Sampling.y, 1u); ++CandidateIndex)
        {
            uint Candidate = NextRandom(SeedBits);
            float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);
            float CandidateTarget = ReplayTarget(CandidateRadiance);
            Sum += CandidateTarget;
            Count += 1;
            if (RandomUnit(SelectionBits) * Sum < CandidateTarget)
            {
                Selected = Candidate;
                Radiance = CandidateRadiance;
                Target = CandidateTarget;
            }
        }
        bool TemporalReplayed = false;
        uint TemporalSeed = 0;
        if (Extent.Reconstruction.x != 0 && Extent.History.z != 0 && Extent.History.w == 0 &&
            R.MotionPixel.w != 0 && ReplayMotionConvertible(R.MotionPixel.xy))
        {
            int2 P = int2(floor(R.MotionPixel.xy + .5));
            if (all(P >= 0) && all(P < int2(Extent.History.xy)))
            {
                uint Previous = uint(P.y) * Extent.History.x + uint(P.x);
                float4 Source = OutputColour[ReplaySlot(0, Previous)];
                float4 Guide = OutputColour[ReplaySlot(1, Previous)];
                float3 PriorNormal = OutputColour[ReplaySlot(4, Previous)].xyz;
                float3 MotionNormal = R.MotionNormal.xyz;
                float PriorLength = dot(PriorNormal, PriorNormal), MotionLength = dot(MotionNormal, MotionNormal);
                bool MatchingPosition = all(Guide.xyz == R.MotionPositionToken.xyz);
                if ((Extent.Primary.w & 512u) == 0 && PriorLength > 1e-12 && MotionLength > 1e-12)
                {
                    float3 Delta = Guide.xyz - R.MotionPositionToken.xyz;
                    float Radius = clamp(R.MotionPixel.z, 0.0, .25);
                    MatchingPosition = dot(Delta, Delta) <= Radius * Radius &&
                                       dot(PriorNormal, MotionNormal) >= .9 * sqrt(PriorLength * MotionLength);
                }
                if (ReplayReservoirHistoryValid(Source) && Guide.w == R.MotionPositionToken.w && MatchingPosition)
                {
                    uint Candidate = DecodeReplaySeed(Source.xy);
                    float3 Replayed = EvaluateReplayPath(R, Candidate);
                    float ReplayedTarget = ReplayTarget(Replayed);
                    float Retained = min(Source.w, float(max(1u, Extent.Reconstruction.w)));
                    float Weight = ReplayedTarget * Source.z * Retained;
                    if (isfinite(Weight) && isfinite(Sum + Weight))
                    {
                        Sum += Weight;
                        Count += Retained;
                        if (RandomUnit(SelectionBits) * Sum < Weight)
                        {
                            Selected = Candidate;
                            Radiance = Replayed;
                            Target = ReplayedTarget;
                        }
                        TemporalReplayed = true;
                        TemporalSeed = Candidate;
                        InterlockedAdd(Counters[20], 1);
                    }
                    else InterlockedAdd(Counters[21], 1);
                }
                else InterlockedAdd(Counters[21], 1);
            }
        }
        if ((Extent.Primary.w & 262144u) != 0 && (Extent.Reconstruction.x != 0 || Extent.Reconstruction.y != 0))
        {
            float4 BankFirst = OutputColour[ReplaySlot(13, Pixel)];
            float4 BankSecond = OutputColour[ReplaySlot(14, Pixel)];
            if (ReplaySeedBankValid(BankFirst, BankSecond))
            {
                uint RetainedCount = uint(BankSecond.z);
                uint RetainedEntries = Extent.Reconstruction.x != 0 ? min(RetainedCount, 1u) : RetainedCount;
                for (uint RetainedIndex = 0; RetainedIndex < RetainedEntries; ++RetainedIndex)
                {
                    float2 PackedSeed = RetainedIndex == 0 ? BankFirst.xy :
                                        (RetainedIndex == 1 ? BankFirst.zw : BankSecond.xy);
                    uint Candidate = DecodeReplaySeed(PackedSeed);
                    if (TemporalReplayed && Candidate == TemporalSeed) continue;
                    float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);
                    float CandidateTarget = ReplayTarget(CandidateRadiance);
                    Sum += CandidateTarget;
                    Count += 1;
                    if (RandomUnit(SelectionBits) * Sum < CandidateTarget)
                    {
                        Selected = Candidate;
                        Radiance = CandidateRadiance;
                        Target = CandidateTarget;
                    }
                }
            }
        }
        Packed = float4(EncodeReplaySeed(Selected), Sum / (Count * Target), Count);
        SelectedRadiance = float4(Radiance, Target);
    }
    OutputColour[ReplaySlot(2, Pixel)] = Packed;
    OutputColour[ReplaySlot(3, Pixel)] = SelectedRadiance;
    FlushSecondaryTraversalCounters(Pixel);
}
[shader("compute")][numthreads(64,1,1)] void ResolveReplayReservoirs(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    ReplayThreadBoxTests = 0;
    ReplayThreadTriTests = 0;
    SurfacePoint R = Receivers[Pixel];
    float4 Local = OutputColour[ReplaySlot(2, Pixel)];
    float3 Chosen = 0;
    float ChosenTarget = Extent.Maximum.x, Sum = 0, Count = 0;
    uint SelectionBits = (Extent.Sampling.x * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    static const int2 Offsets[5] = {int2(0,0), int2(-1,0), int2(1,0), int2(0,-1), int2(0,1)};
    uint ProposalCount = Extent.Reconstruction.y != 0 ? 5u : 1u;
    bool DiskEnabled = (Extent.Primary.w & 1048576u) != 0;
    if (ReplayReservoirHistoryValid(Local))
        for (uint I = 0; I < ProposalCount; ++I)
        {
            int2 Offset = I == 0 ? int2(0, 0) :
                (DiskEnabled ? ProjectSpatialDiskOffset(Pixel, Extent.Sampling.x, I - 1u) : Offsets[I]);
            int2 P = Centre + Offset;
            if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
            uint Donor = uint(P.y) * Extent.History.x + uint(P.x);
            if (I != 0 && DiskEnabled && !EvaluateSpatialSurfaceCompatibility(R, Receivers[Donor]))
            {
                InterlockedAdd(Counters[9], 1);
                continue;
            }
            float4 Source = OutputColour[ReplaySlot(2, Donor)];
            if (!ReplayReservoirHistoryValid(Source)) continue;
            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :
                                       EvaluateReplayPath(R, DecodeReplaySeed(Source.xy));
            float Target = ReplayTarget(Radiance), Weight = Target * Source.z * Source.w;
            if (!isfinite(Weight) || !isfinite(Sum + Weight)) continue;
            Sum += Weight; Count += Source.w;
            if (RandomUnit(SelectionBits) * Sum < Weight)
            {
                Chosen = Radiance; ChosenTarget = Target;
            }
            if (I != 0) InterlockedAdd(Counters[22], 1);
        }
    float3 Indirect = Count > 0 ? Chosen * (Sum / (Count * ChosenTarget)) : float3(0);
    OutputColour[Pixel] = float4(Indirect + R.ResidualSpecular.xyz, 1);
    // Persist only pre-spatial history; cross-pixel candidates are retraced, not assumed statistically independent.
    OutputColour[ReplaySlot(0, Pixel)] = Local;
    OutputColour[ReplaySlot(1, Pixel)] = float4(R.PositionHit.xyz, R.PositionHit.w != 0 ? R.MotionPositionToken.w : 0);
    OutputColour[ReplaySlot(4, Pixel)] = float4(R.NormalRoughness.xyz, 0);
    FlushSecondaryTraversalCounters(Pixel);
}
