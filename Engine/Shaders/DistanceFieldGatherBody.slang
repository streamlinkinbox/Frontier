//============================================================================================================================================
//                                                       DISTANCEFIELDGATHERBODY.SLANG
//============================================================================================================================================
// 📦 Material-independent incoming diffuse lighting and geometry guides for spatial irradiance reconstruction.

#extension GL_GOOGLE_include_directive : require
#ifndef FRONTIER_CONSTANT_MATERIALS
#extension GL_EXT_nonuniform_qualifier : require
#endif
layout(local_size_x=16, local_size_y=16) in;
layout(rgba32f, binding=1) uniform readonly image2D SurfaceImage;
layout(rgba16f, binding=21) uniform writeonly image2D GatherIrradiance;
layout(rgba16f, binding=22) uniform writeonly image2D GatherNormal;
#include "DistanceFieldTransport.slang"
#include "DistanceFieldMaterial.slang"
void main()
{
    ivec2 Pixel=ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(Pixel,min(imageSize(SurfaceImage),ivec2(PC.RenderExtent.xy))))) return;
    imageStore(GatherIrradiance,Pixel,vec4(0.0));
    imageStore(GatherNormal,Pixel,vec4(0.0));
    vec4 Surface=imageLoad(SurfaceImage,Pixel);
    uint Packed=floatBitsToUint(Surface.w), Instance=Packed>>14u;
    if (Packed==0xFFFFFFFFu || Instance>=uint(Instances.length())) return;
    GpuInstance Placement=Instances[Instance];
    SurfaceFrame Frame;
    vec3 View=normalize(PC.CameraEye.xyz-Surface.xyz);
    ShadingRecord Material=ResolveMaterial(Materials[Placement.MaterialIndex],Placement,Packed&0x3FFFu,
        Surface.xyz,View,length(PC.CameraEye.xyz-Surface.xyz)*0.0015,Frame);
    if (dot(Frame.Normal,View)<=0.0 || dot(Frame.Emission,Frame.Emission)>0.0) return;
    vec3 Incoming=GatherDistanceRadiance(Surface.xyz,Frame.Normal,true);
    imageStore(GatherIrradiance,Pixel,vec4(clamp(Incoming,vec3(0.0),vec3(65504.0)),1.0));
    imageStore(GatherNormal,Pixel,vec4(Frame.Normal,1.0));
}
