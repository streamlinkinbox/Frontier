//============================================================================================================================================
//                                                     DISTANCEFIELDTRANSPORT.SLANG
//============================================================================================================================================
// 📦 Shared std430 scene/clipmap records, continuous distance sampling and exact secondary triangle traversal.

#ifndef FRONTIER_DISTANCE_TRANSPORT
#define FRONTIER_DISTANCE_TRANSPORT
struct DistanceFacet { vec4 Alpha; vec4 Beta; vec4 Gamma; vec4 Normal; vec4 Albedo; vec4 Emission; };
struct DistanceBranch { vec4 Minimum; vec4 Maximum; uvec4 Range; };
struct DistanceVoxel { vec4 PositionDistance; vec4 Normal; vec4 Albedo; vec4 Emission; };
layout(std430, binding=3) buffer FieldExtent { DistanceVoxel Field[]; };
layout(rgba32f, binding=4) uniform readonly image2D PreviousRadiance;
layout(std430, binding=5) readonly buffer DistanceConstants { vec4 ClipOrigins[3]; uvec4 GeometryCounts; };
layout(std430, binding=6) readonly buffer DistanceBranches { DistanceBranch Branches[]; };
layout(std430, binding=7) readonly buffer DistanceFacets { DistanceFacet Facets[]; };
layout(rgba32f, binding=10) uniform image2D NextRadiance;
layout(rgba32f, binding=8) uniform image2D SurfaceDiffuse;
layout(rgba32f, binding=9) uniform image2D SurfaceNormal;
layout(rgba32f, binding=11) uniform image2D SurfaceEmission;
layout(push_constant) uniform DistancePush
{
    vec4 SunDirectionRadiance;
    vec4 SunColour;
    vec4 SkyAmbient;
    vec4 Tuning;
    vec4 CameraEye;
    uvec4 Counts;
    uvec4 RenderExtent;
} PC;

uint VolumeCount() { return GeometryCounts.x * GeometryCounts.x * GeometryCounts.x; }
uint CellIndex(uvec3 Cell, uint Level)
{
    return Level * VolumeCount() + (Cell.z * GeometryCounts.x + Cell.y) * GeometryCounts.x + Cell.x;
}
vec3 CellPosition(uint Index)
{
    uint Level = Index / VolumeCount();
    uint Local = Index % VolumeCount();
    uvec3 Cell = uvec3(Local % GeometryCounts.x, (Local / GeometryCounts.x) % GeometryCounts.x, Local / (GeometryCounts.x * GeometryCounts.x));
    return ClipOrigins[Level].xyz + (vec3(Cell) + 0.5) * ClipOrigins[Level].w;
}
int SelectClip(vec3 Position, out vec3 Coordinate)
{
    for (int Level = 0; Level < 3; ++Level)
    {
        Coordinate = (Position - ClipOrigins[Level].xyz) / ClipOrigins[Level].w - 0.5;
        if (all(greaterThanEqual(Coordinate, vec3(0.0))) && all(lessThan(Coordinate, vec3(GeometryCounts.x - 1u)))) return Level;
    }
    return -1;
}
float SampleSceneDistance(vec3 Position, out float Cell)
{
    vec3 Coordinate;
    int Level = SelectClip(Position, Coordinate);
    if (Level < 0) { Cell = ClipOrigins[2].w; return 1e20; }
    Cell = ClipOrigins[Level].w;
    uvec3 Base = uvec3(floor(Coordinate));
    vec3 Fraction = fract(Coordinate);
    float Distance = 0.0;
    for (uint Corner = 0u; Corner < 8u; ++Corner)
    {
        uvec3 Offset = uvec3(Corner & 1u, (Corner >> 1u) & 1u, (Corner >> 2u) & 1u);
        vec3 Weight = mix(vec3(1.0) - Fraction, Fraction, vec3(Offset));
        Distance += abs(Field[CellIndex(Base + Offset, uint(Level))].PositionDistance.w) * Weight.x * Weight.y * Weight.z;
    }
    return abs(Distance);
}
// 📝 Every facet owns a non-overlapping rectangular card. The square-to-triangle warp uses every texel;
//    authored UVs are interpolated separately by ResolveMaterial, so mirrored/repeated UVs never alias cards.
ivec2 CardOrigin(uint Facet)
{
    uint Columns=uint(imageSize(NextRadiance).x)/GeometryCounts.w;
    return ivec2(Facet%Columns,Facet/Columns)*int(GeometryCounts.w);
}
vec3 CardPosition(uint Facet, vec2 Coordinate)
{
    float Root=sqrt(clamp(Coordinate.x,0.0,1.0));
    vec3 Weights=vec3(1.0-Root,Root*(1.0-Coordinate.y),Root*Coordinate.y);
    return Facets[Facet].Alpha.xyz*Weights.x+Facets[Facet].Beta.xyz*Weights.y+Facets[Facet].Gamma.xyz*Weights.z;
}
vec2 CardCoordinate(uint Facet, vec3 Position)
{
    vec3 EdgeAlpha=Facets[Facet].Beta.xyz-Facets[Facet].Alpha.xyz;
    vec3 EdgeBeta=Facets[Facet].Gamma.xyz-Facets[Facet].Alpha.xyz;
    vec3 Delta=Position-Facets[Facet].Alpha.xyz;
    float Alpha=dot(EdgeAlpha,EdgeAlpha), Beta=dot(EdgeAlpha,EdgeBeta), Gamma=dot(EdgeBeta,EdgeBeta);
    float Denominator=max(Alpha*Gamma-Beta*Beta,1e-20);
    vec2 Weights=max(vec2(Gamma*dot(Delta,EdgeAlpha)-Beta*dot(Delta,EdgeBeta),
                          Alpha*dot(Delta,EdgeBeta)-Beta*dot(Delta,EdgeAlpha))/Denominator,vec2(0.0));
    float Sum=Weights.x+Weights.y;
    return clamp(vec2(Sum*Sum,Weights.y/max(Sum,1e-10)),vec2(0.0),vec2(1.0));
}
vec3 SampleCachedRadiance(uint Facet, vec3 Position, bool Current)
{
    vec2 Coordinate=clamp(CardCoordinate(Facet,Position)*float(GeometryCounts.w)-0.5,vec2(0.0),vec2(GeometryCounts.w-1u));
    ivec2 Lower=ivec2(floor(Coordinate)), Origin=CardOrigin(Facet);
    vec2 Fraction=fract(Coordinate);
    vec3 Radiance=vec3(0.0);
    for (uint Corner=0u;Corner<4u;++Corner)
    {
        ivec2 Offset=ivec2(Corner&1u,(Corner>>1u)&1u);
        ivec2 Pixel=Origin+min(Lower+Offset,ivec2(GeometryCounts.w-1u));
        vec2 Weight=mix(vec2(1.0)-Fraction,Fraction,vec2(Offset));
        Radiance+=(Current ? imageLoad(NextRadiance,Pixel).xyz : imageLoad(PreviousRadiance,Pixel).xyz)*Weight.x*Weight.y;
    }
    return Radiance;
}
float SurfaceCoverage(uint Facet, vec3 Position)
{
    ivec2 Pixel=CardOrigin(Facet)+ivec2(clamp(CardCoordinate(Facet,Position)*float(GeometryCounts.w),vec2(0.0),vec2(GeometryCounts.w-1u)));
    return imageLoad(SurfaceNormal,Pixel).w;
}
float BoxDistanceSquared(vec3 Position, vec3 Minimum, vec3 Maximum)
{
    vec3 Delta = max(max(Minimum - Position, Position - Maximum), vec3(0.0));
    return dot(Delta, Delta);
}
vec3 ClosestOnTriangle(vec3 Position, DistanceFacet Facet)
{
    vec3 Alpha=Facet.Alpha.xyz, Beta=Facet.Beta.xyz, Gamma=Facet.Gamma.xyz;
    vec3 EdgeAlpha=Beta-Alpha, EdgeBeta=Gamma-Alpha, Delta=Position-Alpha;
    float D1=dot(EdgeAlpha,Delta), D2=dot(EdgeBeta,Delta);
    if (D1<=0.0 && D2<=0.0) return Alpha;
    Delta=Position-Beta;
    float D3=dot(EdgeAlpha,Delta), D4=dot(EdgeBeta,Delta);
    if (D3>=0.0 && D4<=D3) return Beta;
    float AreaGamma=D1*D4-D3*D2;
    if (AreaGamma<=0.0 && D1>=0.0 && D3<=0.0) return Alpha+EdgeAlpha*(D1/(D1-D3));
    Delta=Position-Gamma;
    float D5=dot(EdgeAlpha,Delta), D6=dot(EdgeBeta,Delta);
    if (D6>=0.0 && D5<=D6) return Gamma;
    float AreaBeta=D5*D2-D1*D6;
    if (AreaBeta<=0.0 && D2>=0.0 && D6<=0.0) return Alpha+EdgeBeta*(D2/(D2-D6));
    float AreaAlpha=D3*D6-D5*D4;
    if (AreaAlpha<=0.0 && D4-D3>=0.0 && D5-D6>=0.0) return Beta+(Gamma-Beta)*((D4-D3)/((D4-D3)+(D5-D6)));
    return Alpha+(EdgeAlpha*AreaBeta+EdgeBeta*AreaGamma)/(AreaAlpha+AreaBeta+AreaGamma);
}
struct DistanceRayHit { float Travel; uint Facet; };
DistanceRayHit TraceDistanceMesh(vec3 Origin, vec3 Direction, float Maximum)
{
    DistanceRayHit Hit; Hit.Travel=Maximum; Hit.Facet=0xFFFFFFFFu;
    vec3 Reciprocal=1.0/(sign(Direction)*max(abs(Direction),vec3(1e-20))+vec3(equal(Direction,vec3(0.0)))*1e-20);
    uint Branch=0u;
    while (Branch<GeometryCounts.y)
    {
        DistanceBranch Entry=Branches[Branch];
        vec3 Near=(Entry.Minimum.xyz-Origin)*Reciprocal, Far=(Entry.Maximum.xyz-Origin)*Reciprocal;
        vec3 Lower=min(Near,Far), Upper=max(Near,Far);
        if (max(max(Lower.x,Lower.y),max(Lower.z,0.0)) > min(min(Upper.x,Upper.y),min(Upper.z,Hit.Travel)))
        { Branch=Entry.Range.x; continue; }
        for (uint Index=0u; Index<Entry.Range.z; ++Index)
        {
            uint Slot=Entry.Range.y+Index;
            DistanceFacet Triangle=Facets[Slot];
            vec3 EdgeAlpha=Triangle.Beta.xyz-Triangle.Alpha.xyz, EdgeBeta=Triangle.Gamma.xyz-Triangle.Alpha.xyz;
            vec3 Cross=cross(Direction,EdgeBeta);
            float Determinant=dot(EdgeAlpha,Cross);
            if (abs(Determinant)<1e-10) continue;
            vec3 Delta=Origin-Triangle.Alpha.xyz;
            float Beta=dot(Delta,Cross)/Determinant;
            vec3 Second=cross(Delta,EdgeAlpha);
            float Gamma=dot(Direction,Second)/Determinant, Travel=dot(EdgeBeta,Second)/Determinant;
            if (Beta>=0.0 && Gamma>=0.0 && Beta+Gamma<=1.0 && Travel>1e-4 && Travel<Hit.Travel)
            { if (SurfaceCoverage(Slot,Origin+Direction*Travel)>0.0) { Hit.Travel=Travel; Hit.Facet=Slot; } }
        }
        ++Branch;
    }
    return Hit;
}
// 📝 The clipmap's nearest-face sign is not a solid winding classification. A penumbra must not
// follow its interpolated zero sheets or camera-snapped voxel boundaries. Refine the continuous
// surface distance for shadows; retain the clipmap as the diffuse traversal accelerator.
float ShadowSurfaceDistance(vec3 Point, vec3 Receiver, vec3 ReceiverNormal)
{
    float Best=1e30;
    uint Branch=0u;
    while (Branch<GeometryCounts.y)
    {
        DistanceBranch Entry=Branches[Branch];
        if (BoxDistanceSquared(Point,Entry.Minimum.xyz,Entry.Maximum.xyz)>Best)
        { Branch=Entry.Range.x; continue; }
        for (uint Leaf=0u; Leaf<Entry.Range.z; ++Leaf)
        {
            uint Slot=Entry.Range.y+Leaf;
            DistanceFacet Facet=Facets[Slot];
            // A coplanar receiving surface cannot cast a shadow on itself. Without this exclusion
            // even an empty plane darkens when the Sun is lower than the penumbra cone angle.
            if (abs(dot(Facet.Normal.xyz,ReceiverNormal))>0.9999 &&
                abs(dot(Facet.Normal.xyz,Receiver-Facet.Alpha.xyz))<0.003) continue;
            vec3 Closest=ClosestOnTriangle(Point,Facet);
            if (SurfaceCoverage(Slot,Closest)<=0.0) continue;
            Best=min(Best,dot(Point-Closest,Point-Closest));
        }
        ++Branch;
    }
    return sqrt(Best);
}
float DistanceShadow(vec3 Position, vec3 Normal)
{
    vec3 Direction=normalize(PC.SunDirectionRadiance.xyz);
    vec3 Origin=Position+Normal*0.002;
    // Exact umbra confirmation also covers thin casters, clipmap exits and march-budget exhaustion.
    if (TraceDistanceMesh(Origin,Direction,1e20).Facet!=0xFFFFFFFFu) return 0.0;
    float Visibility=1.0, Travel=0.004;
    float Maximum=length(Origin-Branches[0].Minimum.xyz)+length(Branches[0].Maximum.xyz-Branches[0].Minimum.xyz);
    for (uint Step=0u; Step<64u && Travel<Maximum; ++Step)
    {
        float Distance=ShadowSurfaceDistance(Origin+Direction*Travel,Position,Normal);
        if (Distance>1e14) break;
        Visibility=min(Visibility,Distance/(max(PC.Tuning.x,0.0001)*Travel));
        Travel+=max(Distance*0.5,0.002);
    }
    return clamp(Visibility,0.0,1.0);
}
vec3 HemisphereDirection(vec3 Normal, uint Sample, uint SampleCount, vec2 Shift)
{
    float Height=fract((float(Sample)+0.5)/float(SampleCount)+Shift.x);
    float Angle=6.28318530718*fract(float(Sample)*0.61803398875+Shift.y);
    vec3 Tangent=normalize(abs(Normal.z)<0.9 ? cross(Normal,vec3(0,0,1)) : cross(Normal,vec3(0,1,0)));
    return Tangent*(cos(Angle)*sqrt(1.0-Height))+cross(Normal,Tangent)*(sin(Angle)*sqrt(1.0-Height))+Normal*sqrt(Height);
}
// Sample actual textured surface cards as well as cosine directions. The balance heuristic
// prevents a small bright card from becoming a sharp translated image of itself at low ray counts.
float BranchSolidAngle(uint Slot, vec3 Position, vec3 Normal)
{
    DistanceBranch Entry=Branches[Slot];
    vec3 Center=(Entry.Minimum.xyz+Entry.Maximum.xyz)*0.5-Position;
    vec3 Extent=(Entry.Maximum.xyz-Entry.Minimum.xyz)*0.5;
    if (dot(Center,Normal)+dot(Extent,abs(Normal))<=0.0) return 0.0;
    float Area=max(Extent.x*Extent.y+Extent.y*Extent.z+Extent.z*Extent.x,1e-12);
    return Area/(dot(Center,Center)+Area);
}
float FacetSolidAngle(uint Slot, vec3 Position, vec3 Normal)
{
    DistanceFacet Facet=Facets[Slot];
    vec3 Delta=(Facet.Alpha.xyz+Facet.Beta.xyz+Facet.Gamma.xyz)/3.0-Position;
    float Height=max(dot(Facet.Alpha.xyz-Position,Normal),max(dot(Facet.Beta.xyz-Position,Normal),dot(Facet.Gamma.xyz-Position,Normal)));
    if (Height<=0.0) return 0.0;
    float Area=0.5*length(cross(Facet.Beta.xyz-Facet.Alpha.xyz,Facet.Gamma.xyz-Facet.Alpha.xyz));
    float DistanceSquared=max(dot(Delta,Delta),1e-12);
    return Area*min(1.0,Height/sqrt(DistanceSquared))*abs(dot(Facet.Normal.xyz,Delta))/
        (sqrt(DistanceSquared)*(DistanceSquared+Area*0.1));
}
// The preorder BVH splits each contiguous facet range in half. Both sampling and PDF
// evaluation follow that same partition; no array, recursion, or per-pixel all-facet scan.
uint SelectRadianceFacet(vec3 Position, vec3 Normal, float Sequence, uint Requested, out float Probability)
{
    uint Branch=0u, First=0u, Count=GeometryCounts.z;
    Probability=1.0;
    if (Count==0u) { Probability=0.0; return 0xFFFFFFFFu; }
    while (Branches[Branch].Range.z==0u)
    {
        uint Left=Branch+1u, Right=Branches[Left].Range.x, Half=Count/2u;
        float Alpha=BranchSolidAngle(Left,Position,Normal), Beta=BranchSolidAngle(Right,Position,Normal);
        if (Alpha+Beta<=0.0) { Probability=0.0; return 0xFFFFFFFFu; }
        float Split=Alpha/(Alpha+Beta);
        bool ChooseLeft=Requested==0xFFFFFFFFu ? Sequence<Split : Requested<First+Half;
        if (ChooseLeft)
        {
            Probability*=Split; Sequence/=max(Split,1e-20); Branch=Left; Count=Half;
        }
        else
        {
            Probability*=1.0-Split; Sequence=(Sequence-Split)/max(1.0-Split,1e-20);
            Branch=Right; First+=Half; Count-=Half;
        }
    }
    DistanceBranch Entry=Branches[Branch];
    vec4 Weights=vec4(0.0);
    float Sum=0.0;
    for (uint Index=0u; Index<Entry.Range.z; ++Index)
    { Weights[Index]=FacetSolidAngle(Entry.Range.y+Index,Position,Normal); Sum+=Weights[Index]; }
    if (Sum<=0.0) { Probability=0.0; return 0xFFFFFFFFu; }
    float Target=clamp(Sequence,0.0,0.999999)*Sum, Accumulated=0.0;
    for (uint Index=0u; Index<Entry.Range.z; ++Index)
    {
        Accumulated+=Weights[Index];
        if (Requested==Entry.Range.y+Index || (Requested==0xFFFFFFFFu && Target<Accumulated))
        { Probability*=Weights[Index]/Sum; return Entry.Range.y+Index; }
    }
    Probability=0.0; return 0xFFFFFFFFu;
}
float RadianceDirectionDensity(uint Facet, vec3 Origin, vec3 Point, float Probability)
{
    vec3 Delta=Point-Origin;
    float Area=0.5*length(cross(Facets[Facet].Beta.xyz-Facets[Facet].Alpha.xyz,Facets[Facet].Gamma.xyz-Facets[Facet].Alpha.xyz));
    return Probability*dot(Delta,Delta)/max(Area*abs(dot(Facets[Facet].Normal.xyz,normalize(Delta))),1e-20);
}
#ifndef FRONTIER_SDF_REFERENCE_SAMPLES
#define FRONTIER_SDF_REFERENCE_SAMPLES 0
#endif
uint ScrambleSample(uint Value)
{
    Value^=Value>>16u; Value*=0x7feb352du; Value^=Value>>15u;
    Value*=0x846ca68bu; return Value^(Value>>16u);
}
vec4 GatherSampleShift(bool Current)
{
    if (!Current) return vec4(0.0);
    uint Seed=ScrambleSample(gl_GlobalInvocationID.x)^ScrambleSample(gl_GlobalInvocationID.y+0x9e3779b9u);
    return vec4(ScrambleSample(Seed),ScrambleSample(Seed+1u),ScrambleSample(Seed+2u),ScrambleSample(Seed+3u))/4294967296.0;
}
vec3 GatherDistanceRadiance(vec3 Position, vec3 Normal, bool Current)
{
    float Cell; SampleSceneDistance(Position,Cell);
    vec3 Origin=Position+Normal*0.002, Result=vec3(0.0);
    // Pixel-decorrelated quadrature removes repeated projected silhouettes. The gather is
    // reconstructed as incident irradiance before material evaluation, not blurred after tonemapping.
    uint SampleCount=Current ? 32u : 16u;
#if FRONTIER_SDF_REFERENCE_SAMPLES > 0
    if (Current) SampleCount=uint(FRONTIER_SDF_REFERENCE_SAMPLES);
#endif
    vec4 Shift=GatherSampleShift(Current);
    for (uint Sample=0u; Sample<SampleCount; ++Sample)
    {
        vec3 Direction=HemisphereDirection(Normal,Sample,SampleCount,Shift.xy);
        float Travel=Cell;
        vec3 Incoming=PC.SkyAmbient.xyz;
        bool Resolved=false;
        DistanceRayHit GatherHit; GatherHit.Facet=0xFFFFFFFFu; GatherHit.Travel=0.0;
        for (uint Step=0u; Step<48u; ++Step)
        {
            vec3 Point=Origin+Direction*Travel;
            float Distance=SampleSceneDistance(Point,Cell);
            if (Distance>1e19)
            {
                DistanceRayHit Hit=TraceDistanceMesh(Origin,Direction,1e20);
                if (Hit.Facet!=0xFFFFFFFFu) Incoming=SampleCachedRadiance(Hit.Facet,Origin+Direction*Hit.Travel,Current);
                GatherHit=Hit;
                Resolved=true;
                break;
            }
            if (Distance<Cell*0.7)
            {
                // 📝 Exact triangle refinement avoids shading a voxel boundary as a surface.
                DistanceRayHit Hit=TraceDistanceMesh(Origin,Direction,Travel+Cell*2.0);
                if (Hit.Facet!=0xFFFFFFFFu)
                {
                    Incoming=SampleCachedRadiance(Hit.Facet,Origin+Direction*Hit.Travel,Current);
                    GatherHit=Hit;
                    Resolved=true;
                    break;
                }
            }
            Travel+=max(Distance*0.75,Cell*0.5);
        }
        // The coarse field is an accelerator, never the visibility authority on budget exhaustion.
        if (!Resolved)
        {
            DistanceRayHit FinalHit=TraceDistanceMesh(Origin,Direction,1e20);
            GatherHit=FinalHit;
            Incoming=FinalHit.Facet==0xFFFFFFFFu ? PC.SkyAmbient.xyz :
                SampleCachedRadiance(FinalHit.Facet,Origin+Direction*FinalHit.Travel,Current);
        }
        float CosineDensity=max(dot(Normal,Direction),0.0)/3.14159265359;
        float AreaDensity=0.0;
        if (GatherHit.Facet!=0xFFFFFFFFu)
        {
            float Probability;
            SelectRadianceFacet(Origin,Normal,0.0,GatherHit.Facet,Probability);
            AreaDensity=RadianceDirectionDensity(GatherHit.Facet,Origin,Origin+Direction*GatherHit.Travel,Probability);
        }
        Result+=Incoming*(CosineDensity/max(CosineDensity+AreaDensity,1e-20))/float(SampleCount);
        float Probability;
        uint Facet=SelectRadianceFacet(Origin,Normal,fract((float(Sample)+0.5)/float(SampleCount)+Shift.z),0xFFFFFFFFu,Probability);
        if (Facet==0xFFFFFFFFu || Probability<=0.0) continue;
        vec2 Coordinate=fract(vec2(0.5)+float(Sample)*vec2(0.754877666,0.569840296)+Shift.yw);
        vec3 Point=CardPosition(Facet,Coordinate), Delta=Point-Origin;
        float TravelToCard=length(Delta);
        if (TravelToCard<1e-4) continue;
        Direction=Delta/TravelToCard;
        CosineDensity=max(dot(Normal,Direction),0.0)/3.14159265359;
        if (CosineDensity<=0.0) continue;
        DistanceRayHit CardHit=TraceDistanceMesh(Origin,Direction,TravelToCard+0.002);
        if (CardHit.Facet!=Facet) continue;
        AreaDensity=RadianceDirectionDensity(Facet,Origin,Point,Probability);
        Result+=SampleCachedRadiance(Facet,Point,Current)*
            (CosineDensity/max(CosineDensity+AreaDensity,1e-20))/float(SampleCount);
    }
    return Result;
}
#endif
