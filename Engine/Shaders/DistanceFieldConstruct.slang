//============================================================================================================================================
//                                                     DISTANCEFIELDCONSTRUCT.SLANG
//============================================================================================================================================
// 📦 Populates three camera-snapped distance volumes from exact triangle closest points and resident material coefficients.

#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x=64) in;
#include "DistanceFieldTransport.slang"
void main()
{
    uint Index=gl_GlobalInvocationID.x;
    if (Index>=PC.Counts.x) return;
    vec3 Position=CellPosition(Index), Closest=vec3(0.0);
    float Best=1e30;
    uint Selected=0u, Branch=0u;
    while (Branch<GeometryCounts.y)
    {
        DistanceBranch Entry=Branches[Branch];
        if (BoxDistanceSquared(Position,Entry.Minimum.xyz,Entry.Maximum.xyz)>Best)
        { Branch=Entry.Range.x; continue; }
        for (uint Leaf=0u; Leaf<Entry.Range.z; ++Leaf)
        {
            uint Facet=Entry.Range.y+Leaf;
            vec3 Point=ClosestOnTriangle(Position,Facets[Facet]);
            float Squared=dot(Position-Point,Position-Point);
            if (Squared<Best) { Best=Squared; Closest=Point; Selected=Facet; }
        }
        ++Branch;
    }
    DistanceFacet Facet=Facets[Selected];
    float Distance=sqrt(Best);
    if (dot(Position-Closest,Facet.Normal.xyz)<0.0) Distance=-Distance;
    Field[Index].PositionDistance=vec4(Closest,Distance);
    Field[Index].Normal=vec4(Facet.Normal.xyz,ClipOrigins[Index/VolumeCount()].w);
    Field[Index].Albedo=vec4(clamp(Facet.Albedo.xyz,vec3(0.0),vec3(0.95)),1.0);
    Field[Index].Emission=vec4(max(Facet.Emission.xyz,vec3(0.0)),0.0);
}
