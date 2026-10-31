/*==========================================================================================================================================
                                                 ORDEREDBVHREFLECTIONS.SLANG
==========================================================================================================================================*/
// 🧩 Zero-RT-core OrderedSoftwareBvh glossy/mirror triangle reflections + Beer-Lambert glazing fallback for GTX / lower-end GPUs.

struct OrderedBvhEnclosure
{
    float4 MinimumAndFirst;   // xyz = AABB min [m], w = asfloat(uint FirstFacet)
    float4 MaximumAndCount;   // xyz = AABB max [m], w = asfloat(uint FacetCount; 0 = internal node)
    uint4  BranchLinks;       // x = lower node, y = upper node, zw = reserved
};

struct OrderedBvhFacet
{
    float4 VertexA;
    float4 VertexB;
    float4 VertexC;
    float4 AlbedoAndRoughness;
};

struct OrderedBvhReflectionConstants
{
    float4 CameraOriginAndRoughnessCut;   // xyz = camera [m], w = max roughness for BVH trace
    float4 SunDirectionAndIlluminance;
    float4 SkyApexAndLimbTint;
    uint4  DimensionsAndNodeCount;        // xy = viewport [px], z = root node count, w = glazing panes
};

[[vk::binding(0, 0)]] StructuredBuffer<OrderedBvhEnclosure> BvhNodes;
[[vk::binding(1, 0)]] StructuredBuffer<OrderedBvhFacet>     BvhFacets;
[[vk::binding(2, 0)]] StructuredBuffer<float4>              ReceiverPosition;
[[vk::binding(3, 0)]] StructuredBuffer<float4>              ReceiverNormalRoughness;
[[vk::binding(4, 0)]] RWTexture2D<float4>                   OutSpecularReflections;
[[vk::push_constant]] ConstantBuffer<OrderedBvhReflectionConstants> Constants;

bool IntersectRayEnclosure(float3 Origin, float3 InvDir, float3 BoxMin, float3 BoxMax, float MaxT, out float NearT)
{
    float3 T0   = (BoxMin - Origin) * InvDir;
    float3 T1   = (BoxMax - Origin) * InvDir;
    float3 TMin = min(T0, T1);
    float3 TMax = max(T0, T1);
    NearT       = max(0.0, max(TMin.x, max(TMin.y, TMin.z)));
    float FarT  = min(MaxT, min(TMax.x, min(TMax.y, TMax.z)));
    return NearT <= FarT;
}

bool IntersectRayFacet(float3 Origin, float3 Direction, OrderedBvhFacet Facet, inout float BestT, out float3 HitNormal)
{
    HitNormal    = float3(0.0, 0.0, 1.0);
    float3 Edge1 = Facet.VertexB.xyz - Facet.VertexA.xyz;
    float3 Edge2 = Facet.VertexC.xyz - Facet.VertexA.xyz;
    float3 PVec  = cross(Direction, Edge2);
    float  Det   = dot(Edge1, PVec);
    if (abs(Det) < 1e-7)
        return false;
    float  InvDet = 1.0 / Det;
    float3 TVec   = Origin - Facet.VertexA.xyz;
    float  BaryU  = dot(TVec, PVec) * InvDet;
    if (BaryU < 0.0 || BaryU > 1.0)
        return false;
    float3 QVec  = cross(TVec, Edge1);
    float  BaryV = dot(Direction, QVec) * InvDet;
    if (BaryV < 0.0 || BaryU + BaryV > 1.0)
        return false;
    float HitT = dot(Edge2, QVec) * InvDet;
    if (HitT <= 1e-3 || HitT >= BestT)
        return false;
    BestT     = HitT;
    HitNormal = normalize(cross(Edge1, Edge2));
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceOrderedSoftwareBvhReflections(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x >= Constants.DimensionsAndNodeCount.x || Dispatch.y >= Constants.DimensionsAndNodeCount.y)
        return;

    uint   Pixel      = Dispatch.y * Constants.DimensionsAndNodeCount.x + Dispatch.x;
    float4 PosHit     = ReceiverPosition[Pixel];
    float4 NormRough  = ReceiverNormalRoughness[Pixel];
    if (PosHit.w <= 0.0 || NormRough.w > Constants.CameraOriginAndRoughnessCut.w)
    {
        OutSpecularReflections[Dispatch.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 Normal    = normalize(NormRough.xyz);
    float3 ViewDir   = normalize(Constants.CameraOriginAndRoughnessCut.xyz - PosHit.xyz);
    float3 RayDir    = reflect(-ViewDir, Normal);
    float3 RayOrigin = PosHit.xyz + Normal * 0.02;
    float3 InvDir    = 1.0 / select(abs(RayDir) < 1e-6, float3(1e-6, 1e-6, 1e-6), RayDir);

    uint   Stack[32];
    int    StackTop  = 0;
    Stack[0]         = 0u;
    float  BestT     = 1e6;
    uint   HitFacet  = 0xffffffffu;
    float3 HitNormal = float3(0.0, 0.0, 1.0);

    while (StackTop >= 0 && Constants.DimensionsAndNodeCount.z > 0u)
    {
        uint NodeIndex = Stack[StackTop--];
        OrderedBvhEnclosure Node = BvhNodes[NodeIndex];
        float EntryT = 0.0;
        if (!IntersectRayEnclosure(RayOrigin, InvDir, Node.MinimumAndFirst.xyz, Node.MaximumAndCount.xyz, BestT, EntryT))
            continue;

        uint LeafCount = asuint(Node.MaximumAndCount.w);
        if (LeafCount > 0u)
        {
            uint First = asuint(Node.MinimumAndFirst.w);
            for (uint Offset = 0u; Offset < LeafCount; ++Offset)
            {
                float3 CandidateNormal;
                if (IntersectRayFacet(RayOrigin, RayDir, BvhFacets[First + Offset], BestT, CandidateNormal))
                {
                    HitFacet  = First + Offset;
                    HitNormal = CandidateNormal;
                }
            }
        }
        else if (StackTop < 30)
        {
            float LeftNear = 0.0, RightNear = 0.0;
            OrderedBvhEnclosure Lower = BvhNodes[Node.BranchLinks.x];
            OrderedBvhEnclosure Upper = BvhNodes[Node.BranchLinks.y];
            bool HitL = IntersectRayEnclosure(RayOrigin, InvDir, Lower.MinimumAndFirst.xyz, Lower.MaximumAndCount.xyz, BestT, LeftNear);
            bool HitR = IntersectRayEnclosure(RayOrigin, InvDir, Upper.MinimumAndFirst.xyz, Upper.MaximumAndCount.xyz, BestT, RightNear);
            if (HitL && HitR)
            {
                // Front-to-back ordered traversal shrinks BestT early on GTX warps.
                if (LeftNear < RightNear) { Stack[++StackTop] = Node.BranchLinks.y; Stack[++StackTop] = Node.BranchLinks.x; }
                else                      { Stack[++StackTop] = Node.BranchLinks.x; Stack[++StackTop] = Node.BranchLinks.y; }
            }
            else if (HitL) Stack[++StackTop] = Node.BranchLinks.x;
            else if (HitR) Stack[++StackTop] = Node.BranchLinks.y;
        }
    }

    float3 ReflectedRadiance;
    if (HitFacet != 0xffffffffu)
    {
        float3 Albedo = BvhFacets[HitFacet].AlbedoAndRoughness.xyz;
        float  SunDot = max(0.0, dot(HitNormal, normalize(Constants.SunDirectionAndIlluminance.xyz)));
        ReflectedRadiance = Albedo * (0.18 + Constants.SunDirectionAndIlluminance.w * SunDot);
    }
    else
    {
        ReflectedRadiance = Constants.SkyApexAndLimbTint.xyz * lerp(0.45, 1.0, saturate(RayDir.z * 0.5 + 0.5));
    }

    OutSpecularReflections[Dispatch.xy] = float4(ReflectedRadiance, HitFacet != 0xffffffffu ? 1.0 : 0.25);
}
