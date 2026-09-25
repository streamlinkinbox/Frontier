// Same invocation in cull and VS, including live material guard. Preview ONLY until
// primary/secondary surface correspondence is implemented for production lighting.
#include "Shaders/PatchPolicy.shared.h"
bool SelectPatchCoarse(ClusterRecord C, InstanceRecord I)
{
    if ((Control.z != 14u && Control.z != 15u) || (Control.w & 4u) == 0u || C.CoarseTriangleCount == 0u) return false;
    MaterialRecord M = Materials[I.MaterialIndex];
    if (M.Slabs.y != 1u || (M.Slabs.z & 54u) != 0u) return false;
    MaterialSlabRecord S = MaterialSlabs[M.Slabs.x];
    bool UncertainTextures=(S.TextureSlots1.x & 65535u) != 65535u || (S.TextureSlots1.x >> 16u) != 65535u || (S.TextureSlots0.w >> 16u) != 65535u;
    bool Opaque=PatchOpaque(int(M.Slabs.y),int(M.Slabs.z),S.Transmission.y,S.Subsurface.x,S.Geometry.y,
        UncertainTextures,any(notEqual(M.Emissive.xyz,vec3(0))), (floatBitsToUint(S.Slate.z) & 1u) != 0u);
    vec3 X=I.World[0].xyz, Y=I.World[1].xyz, Z=I.World[2].xyz;
    float XX=dot(X,X), YY=dot(Y,Y), ZZ=dot(Z,Z);
    // Frobenius norm bounds spectral scale even for shear/nonuniform transforms.
    float Scale=sqrt(XX+YY+ZZ);
    vec3 Center=(I.World*vec4(C.Sphere.xyz,1)).xyz;
    vec3 V=Center-CameraOrigin.xyz;
    float Radius=C.Sphere.w*Scale;
    float Depth=dot(V,CameraForward.xyz);
    float Lateral=max(abs(dot(V,CameraRight.xyz)),abs(dot(V,CameraUp.xyz)));
    bool Similarity=XX>0.0 && abs(XX-YY)<XX*1e-5 && abs(XX-ZZ)<XX*1e-5
        && abs(dot(X,Y))<XX*1e-5 && abs(dot(X,Z))<XX*1e-5 && abs(dot(Y,Z))<XX*1e-5 && dot(cross(X,Y),Z)>0.0;
    float Distance=length(V);
    bool Away=false;
    if(Similarity && C.Cone.w<1.0 && Distance>Radius)
        Away=dot(normalize(mat3(I.World)*C.Cone.xyz),V/Distance)>C.Cone.w+Radius/Distance;
    float Focal=float(Extent.y)/(2.0*CameraForward.w);
    return PatchChooseCoarse(true,Opaque,true,Away,C.CoarseError,Scale,Depth,Radius,Lateral,Focal,CameraOrigin.w);
}
