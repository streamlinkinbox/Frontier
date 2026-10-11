//============================================================================================================================================
//                                                     DISTANCEFIELDRADIANCE.SLANG
//============================================================================================================================================
// 📦 Jacobi surface-radiance propagation: separate previous/next card images, direct light, sky and emissive multi-bounce transport.

#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x=8, local_size_y=8) in;
#include "DistanceFieldTransport.slang"
void main()
{
    ivec2 Pixel=ivec2(gl_GlobalInvocationID.xy), Extent=imageSize(NextRadiance);
    if (any(greaterThanEqual(Pixel,Extent))) return;
    uint Resolution=GeometryCounts.w;
    uint Facet=uint(Pixel.y)/Resolution*(uint(Extent.x)/Resolution)+uint(Pixel.x)/Resolution;
    if (Facet>=GeometryCounts.z || imageLoad(SurfaceNormal,Pixel).w<=0.0)
    { imageStore(NextRadiance,Pixel,vec4(0)); return; }
    vec3 Position=CardPosition(Facet,(vec2(Pixel%int(Resolution))+0.5)/float(Resolution));
    vec3 Normal=imageLoad(SurfaceNormal,Pixel).xyz;
    vec3 Diffuse=imageLoad(SurfaceDiffuse,Pixel).xyz;
    vec4 Emission=imageLoad(SurfaceEmission,Pixel);
    float Cosine=max(0.0,dot(Normal,normalize(PC.SunDirectionRadiance.xyz)));
    float Visibility=PC.SunDirectionRadiance.w>0.0 && Cosine>0.0 ? DistanceShadow(Position,Facets[Facet].Normal.xyz) : 1.0;
    vec3 Direct=PC.SunColour.xyz*PC.SunDirectionRadiance.w*Cosine*Visibility/3.14159265359;
    vec3 Indirect=GatherDistanceRadiance(Position,Normal,false)*Emission.w;
    vec3 Radiance=Emission.xyz+Diffuse*(Direct+Indirect);
    float Weight=PC.Tuning.z>0.5 ? 1.0 : 0.25;
    imageStore(NextRadiance,Pixel,vec4(mix(imageLoad(PreviousRadiance,Pixel).xyz,min(Radiance,vec3(65504.0)),Weight),1.0));
}
