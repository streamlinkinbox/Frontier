# 🧩 Derive actual triangle hits, segment visibility and diffuse directional-light suffix from the measured area estimator.
from pathlib import Path
import sys

Source = Path('Exhibits/LightCompute/Code/SecondaryReconnectionGpu.slang').read_text()
Start = Source.index('bool SecondaryVisible(')
End = Source.index('[shader("compute")]', Start)
Source = Source[:Start] + '''uint TriangleQuery(float3 Origin, float3 Direction, float Maximum, uint Counter, out float3 Strike)
{
    InterlockedAdd(Counters[Counter], 1);
    ++TriangleQueryCount;
    return NearestFacet(Origin, Direction, .00001, Maximum, Strike);
}
bool SecondaryVisible(float2 Point, float ReceiverX, float Blocker)
{
    float3 Origin = float3(ReceiverX, 0, 0), Delta = float3(Point, 1) - Origin;
    float Distance = length(Delta);
    float3 Hit;
    return TriangleQuery(Origin, Delta / Distance, Distance - .0001, 15, Hit) == 0xffffffffu;
}
float TriangleOutgoing(float2 Point)
{
    float3 Strike;
    bool Visible = TriangleQuery(float3(Point, 1), float3(.8, 0, -.6), 100, 16, Strike) == 0xffffffffu;
    // Lambertian .7/pi times directional intensity pi/.6 times cos(theta)=.6.
    float Albedo = 0, Cosine = 0;
    for (uint Index = 0; Index < Extent.Sampling.z; ++Index)
        if (Facets[Index].A.z == 1)
        {
            Albedo = Facets[Index].AlbedoRoughness.x;
            Cosine = max(0, dot(normalize(Facets[Index].NormalA.xyz), float3(.8, 0, -.6)));
        }
    return Visible ? Albedo * Cosine / .6 : 0;
}
float SecondaryAngular(float2 Point, float ReceiverX, float Blocker)
{
    return SecondaryVisible(Point, ReceiverX, Blocker)
        ? .6 * TriangleOutgoing(Point) * SecondaryCosine(Point, ReceiverX) / 3.141592653589793 : 0;
}
float2 SecondaryProposal(inout uint Bits, float Mixture, out bool CosineProposal)
{
    float Choice = RandomUnit(Bits), U = RandomUnit(Bits), V = RandomUnit(Bits);
    CosineProposal = Choice < Mixture;
    if (!CosineProposal)
    {
        uint Which = uint((Choice - Mixture) / (1 - Mixture) >= .5), Found = 0;
        for (uint Index = 0; Index < Extent.Sampling.z; ++Index)
        {
            if (Facets[Index].A.z != 1) continue;
            if (Found++ != Which) continue;
            float Root = sqrt(U);
            return (Facets[Index].A.xyz + Root * (1 - V) * (Facets[Index].B.xyz - Facets[Index].A.xyz) +
                    Root * V * (Facets[Index].C.xyz - Facets[Index].A.xyz)).xy;
        }
        return float2(1000);
    }
    float Radius = sqrt(U / (1 - U)), Phi = 6.283185307179586 * V;
    float3 Direction = normalize(float3(Radius * cos(Phi), Radius * sin(Phi), 1));
    float3 Strike;
    float ReceiverX = TriangleProposalOrigin;
    uint Hit = TriangleQuery(float3(ReceiverX, 0, 0), Direction, 100000, 14, Strike);
    if (Hit == 0xffffffffu || Facets[Hit].A.z != 1) return float2(1000);
    return float2(ReceiverX, 0) + Direction.xy * Strike.x;
}

''' + Source[End:]
# Private per-invocation origin is set before any candidate queries; no cross-invocation storage.
Source = Source.replace('float SecondaryCosine(', 'static float TriangleProposalOrigin = 0;\nstatic uint TriangleQueryCount = 0;\n\nfloat SecondaryCosine(', 1)
Source = Source.replace('    float Mixture =', '''    TriangleProposalOrigin = Method == 6 ? ReceiverX : 0;
    if (Method == 7)
    {
        float2 Point = (float2((Pixel % 4096u) % 64u, (Pixel % 4096u) / 64u) + .5) / 64 - .5;
        float Outgoing = TriangleOutgoing(Point);
        OutputColour[Pixel] = float4(Outgoing, .7, 8, float(TriangleQueryCount));
        return;
    }
    float Mixture =''', 1)
Source = Source.replace('        if (Method == 6) Point.x += ReceiverX;\n', '')
Source = Source.replace('Fresh += Point.x >= -.1 ? .42 / 8 : 0;', 'Fresh += .6 * TriangleOutgoing(Point) / 8;')
Source = Source.replace('GenerateSecondaryVertices', 'GenerateTriangleVertices').replace('ResolveSecondaryVertices', 'ResolveTriangleVertices')
Path(sys.argv[1]).write_text(Source)

Source = Source.replace('float(Present), 8, Sum', 'float(Present), 8, float(TriangleQueryCount)')
Path(sys.argv[1]).write_text(Source)
