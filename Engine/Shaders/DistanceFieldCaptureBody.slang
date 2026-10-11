//============================================================================================================================================
//                                                       DISTANCEFIELDCAPTUREBODY.SLANG
//============================================================================================================================================
// 📦 Captures authored UV textures, normal maps, cutouts and emissive slabs into surface-card images.

#extension GL_GOOGLE_include_directive : require
#ifndef FRONTIER_CONSTANT_MATERIALS
#extension GL_EXT_nonuniform_qualifier : require
#endif
layout(local_size_x=8, local_size_y=8) in;
#include "DistanceFieldTransport.slang"
#include "DistanceFieldMaterial.slang"
void main()
{
    ivec2 Pixel=ivec2(gl_GlobalInvocationID.xy), Extent=imageSize(SurfaceDiffuse);
    if (any(greaterThanEqual(Pixel,Extent))) return;
    uint Resolution=GeometryCounts.w;
    uint Facet=uint(Pixel.y)/Resolution*(uint(Extent.x)/Resolution)+uint(Pixel.x)/Resolution;
    if (Facet>=GeometryCounts.z)
    {
        imageStore(SurfaceDiffuse,Pixel,vec4(0)); imageStore(SurfaceNormal,Pixel,vec4(0)); imageStore(SurfaceEmission,Pixel,vec4(0)); return;
    }
    vec2 Coordinate=(vec2(Pixel%int(Resolution))+0.5)/float(Resolution);
    vec3 Position=CardPosition(Facet,Coordinate), Normal=Facets[Facet].Normal.xyz;
    GpuInstance Placement=Instances[floatBitsToUint(Facets[Facet].Alpha.w)];
    GpuMaterial Material=Materials[Placement.MaterialIndex];
    SurfaceFrame Surface;
    float Area=0.5*length(cross(Facets[Facet].Beta.xyz-Facets[Facet].Alpha.xyz,Facets[Facet].Gamma.xyz-Facets[Facet].Alpha.xyz));
    float TexelWidth=sqrt(Area)/float(Resolution);
    ShadingRecord Reflectance=ResolveMaterial(Material,Placement,floatBitsToUint(Facets[Facet].Beta.w),Position,Normal,TexelWidth,Surface);
    float Coverage=Surface.Opacity;
    // MaterialFlag::AlphaMask is bit 1; fully transparent texels never participate in secondary visibility.
    if ((Material.Slabs.z&2u)!=0u) Coverage=Coverage>=Material.AlphaCutoff ? 1.0 : 0.0;
    vec3 Diffuse=Reflectance.BaseColor*(1.0-Reflectance.Metalness)*(1.0-Reflectance.TransmissionWeight);
    imageStore(SurfaceDiffuse,Pixel,vec4(clamp(Diffuse,vec3(0),vec3(0.95)),Reflectance.SpecularRoughness));
    imageStore(SurfaceNormal,Pixel,vec4(Surface.Normal,Coverage));
    imageStore(SurfaceEmission,Pixel,vec4(max(Surface.Emission,vec3(0)),Surface.Occlusion));
}
