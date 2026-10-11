//============================================================================================================================================
//                                                     DISTANCEFIELDGIRESOLVE.SLANG
//============================================================================================================================================
// 📦 Raster primary visibility, shared OpenPBR materials, distance-field diffuse transport and exact mesh reflections/refraction.

#extension GL_GOOGLE_include_directive : require
#ifndef FRONTIER_CONSTANT_MATERIALS
#extension GL_EXT_nonuniform_qualifier : require
#endif

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba8, binding=0) uniform writeonly image2D OutputImage;
layout(rgba32f, binding=1) uniform readonly image2D SurfaceImage;
layout(rgba16f, binding=2) uniform readonly image2D NormalImage;
#include "DistanceFieldTransport.slang"
#include "DistanceFieldMaterial.slang"
layout(rgba16f, binding=21) uniform readonly image2D GatherIrradiance;
layout(rgba16f, binding=22) uniform readonly image2D GatherNormal;

// The visibility id packing SurfaceResolve.slang writes into SurfaceImage.w — identical to ReSTIRViewport's.
const uint kVisibilityInvalid = 0xFFFFFFFFu;
uint VisibilityInstance(uint Packed)  { return (Packed >> 14) & 0x3FFFFu; }
uint VisibilityPrimitive(uint Packed) { return Packed & 0x3FFFu; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------
// Feature flags — MUST match Engine/Shaders/ReSTIRViewport.slang so a single ControlCentreSettings drives both paths.
const uint kFeatureGlobalIllumination = 1u;    // bit 0: distance-field diffuse gather runs
// Reflection mode (Counts.w): 0 = Off, 1 = Sky, 2 = Raytraced.
const uint kReflectOff = 0u, kReflectSky = 1u, kReflectRaytraced = 2u;

// Horizon → zenith gradient about the sky ambient, plus a broad solar aureole. Roughness widens the lobe, which
//    is what a prefiltered cube would have done for a glossy reflection.
vec3 SkyAlongApprox(vec3 Direction, float Roughness)
{
    const vec3  up      = vec3(0.0, 0.0, 1.0);                       // world is RH Z-up, metres
    const float height  = clamp(dot(Direction, up) * 0.5 + 0.5, 0.0, 1.0);
    const vec3  zenith  = PC.SkyAmbient.xyz;
    const vec3  horizon = PC.SkyAmbient.xyz * vec3(1.35, 1.22, 1.05);  // Rayleigh reddens toward the horizon
    vec3 dome = mix(horizon, zenith, height * height);
    const float sunCos  = clamp(dot(Direction, normalize(PC.SunDirectionRadiance.xyz)), 0.0, 1.0);
    const float sharp   = mix(2048.0, 8.0, clamp(Roughness, 0.0, 1.0));
    dome += PC.SunColour.xyz * PC.SunDirectionRadiance.w * pow(sunCos, sharp) * 0.02;
    return dome;
}

vec3 GatherField(vec3 Position, vec3 Normal)
{
    return 3.14159265359 * GatherDistanceRadiance(Position,Normal,true) * PC.Tuning.y;
}

// Reconstruct linear incident irradiance only. Material textures, direct shadows, emission,
// specular and tone mapping remain outside this filter. Surface identity and plane/normal
// agreement prevent borrowing light across silhouettes or through nearby walls.
vec3 ReconstructIrradiance(ivec2 Pixel, vec3 Position, uint Visibility)
{
    vec4 Center=imageLoad(GatherIrradiance,Pixel);
#if FRONTIER_SDF_REFERENCE_SAMPLES > 0
    return Center.xyz;
#endif
    vec3 Normal=imageLoad(GatherNormal,Pixel).xyz;
    ivec2 Extent=min(imageSize(SurfaceImage),ivec2(PC.RenderExtent.xy));
    vec3 Sum=vec3(0.0); float Total=0.0;
    for (int Row=-3;Row<=3;++Row) for (int Column=-3;Column<=3;++Column)
    {
        ivec2 Neighbor=Pixel+ivec2(Column,Row);
        if (any(lessThan(Neighbor,ivec2(0))) || any(greaterThanEqual(Neighbor,Extent))) continue;
        vec4 Surface=imageLoad(SurfaceImage,Neighbor);
        uint Packed=floatBitsToUint(Surface.w);
        if (Packed==0xFFFFFFFFu || VisibilityInstance(Packed)!=VisibilityInstance(Visibility)) continue;
        vec4 Guide=imageLoad(GatherNormal,Neighbor);
        float Alignment=dot(Normal,Guide.xyz);
        if (Guide.w==0.0 || Alignment<0.9) continue;
        vec3 Delta=Surface.xyz-Position;
        float PlaneDistance=abs(dot(Delta,Normal));
        float Tolerance=max(0.001,length(Delta)*0.1);
        if (PlaneDistance>Tolerance) continue;
        float Weight=exp(-float(Column*Column+Row*Row)/8.0)*pow(max(Alignment,0.0),32.0);
        Sum+=imageLoad(GatherIrradiance,Neighbor).xyz*Weight; Total+=Weight;
    }
    return Total>0.0 ? Sum/Total : Center.xyz;
}

// Schlick Fresnel for the specular/reflection weight.
vec3 Fresnel(vec3 f0, float cosTheta){ float m = clamp(1.0 - cosTheta, 0.0, 1.0); float m2 = m*m; return f0 + (vec3(1.0) - f0) * (m2*m2*m); }

// Shade a ray hit: the real triangle, the real slab, the engine's BSDF. Shared by the reflection ray here and
//    by SurfelIrradianceUpdate.slang's bounce — the two must agree about what a surface does with light.
vec3 ShadeHit(vec3 hp, uint FacetSlot, vec3 wo)
{
    DistanceFacet Facet=Facets[FacetSlot];
    GpuInstance inst=Instances[floatBitsToUint(Facet.Alpha.w)];
    GpuMaterial mat=Materials[inst.MaterialIndex];
    SurfaceFrame F;
    ShadingRecord m=ResolveMaterial(mat,inst,floatBitsToUint(Facet.Beta.w),hp,wo,0.0,F);
    vec3 woLoc = ToLocal(F, wo);
    if (woLoc.z <= 0.0) return F.Emission;
    ResolvedLayers layers = ResolveLayers(m, woLoc);

    vec3 Lo = F.Emission;
    vec3 L = normalize(PC.SunDirectionRadiance.xyz);
    vec3 wiLoc = ToLocal(F, L);
    if (wiLoc.z > 0.0 && PC.SunDirectionRadiance.w > 0.0)
        Lo += EvaluateBsdf(m, layers, woLoc, wiLoc) * (PC.SunColour.xyz * PC.SunDirectionRadiance.w) * wiLoc.z * DistanceShadow(hp,F.Geometric);
    Lo += AmbientResponse(m, layers, woLoc) * ((PC.Counts.z & kFeatureGlobalIllumination) != 0u
        ? kInvPi * GatherField(hp, F.Normal) : PC.SkyAmbient.xyz);
    return Lo;
}

vec3 Reflection(vec3 Position, SurfaceFrame Surface, vec3 View, ShadingRecord Material, ResolvedLayers Layers, uint Mode)
{
    if (Mode==kReflectOff || Material.SpecularWeight<=0.0) return vec3(0.0);
    vec3 Outgoing=ToLocal(Surface,View);
    vec3 OutgoingAnisotropic=Layers.AnisoBasis*Outgoing;
    vec3 Result=vec3(0.0);
    uint Samples=Material.SpecularRoughness<0.05 ? 1u : 16u;
    for (uint Sample=0u; Sample<Samples; ++Sample)
    {
        vec2 Sequence=vec2(fract(float(Sample)*0.61803398875), (float(Sample)+0.5)/float(Samples));
        bool Haze=fract(float(Sample)*0.754877666)>1.0-Material.HazinessWeight;
        vec3 HalfDirection=SampleGgxVndf(OutgoingAnisotropic,Haze ? Layers.HazeAlpha : Layers.SpecularAlpha,Sequence);
        vec3 IncomingAnisotropic=reflect(-OutgoingAnisotropic,HalfDirection);
        vec3 Incoming=transpose(Layers.AnisoBasis)*IncomingAnisotropic;
        if (Incoming.z<=0.0) continue;
        float Density=mix(GgxVndfPdf(OutgoingAnisotropic,HalfDirection,Layers.SpecularAlpha),
            GgxVndfPdf(OutgoingAnisotropic,HalfDirection,Layers.HazeAlpha),Material.HazinessWeight) /
            (4.0*max(dot(OutgoingAnisotropic,HalfDirection),1e-6));
        vec3 Direction=Surface.Tangent*Incoming.x+Surface.Bitangent*Incoming.y+Surface.Normal*Incoming.z;
        vec3 Incident=SkyAlongApprox(Direction,Material.SpecularRoughness);
        if (Mode==kReflectRaytraced)
        {
            vec3 Origin=Position+Surface.Geometric*0.002;
            DistanceRayHit Hit=TraceDistanceMesh(Origin,Direction,1e20);
            if (Hit.Facet!=0xFFFFFFFFu) Incident=ShadeHit(Origin+Direction*Hit.Travel,Hit.Facet,-Direction);
        }
        Result+=Incident*EvaluateBaseSpecular(Material,Layers,Outgoing,Incoming)*Incoming.z/max(Density,1e-8);
    }
    return Result/float(Samples);
}


vec3 Transmission(vec3 Position, vec3 Normal, vec3 View, ShadingRecord Material, bool ThinWalled)
{
    float Ior=max(Material.SpecularIor,1.01);
    vec3 Direction=refract(-View,Normal,1.0/Ior);
    if (dot(Direction,Direction)<1e-8) return vec3(0.0);
    vec3 Origin=Position-Normal*0.002, Attenuation=vec3(1.0);
    bool Inside=!ThinWalled;
    if (ThinWalled)
    {
        // 📝 Parallel foil entry/exit preserves the far-field direction. Solid media use geometric exit distance below.
        Direction=-View;
        if (Material.TransmissionDepth>0.0 && Material.TransmissionThickness>0.0)
            Attenuation=exp(log(max(Material.TransmissionColor,vec3(1e-5)))*Material.TransmissionThickness/
                            (Material.TransmissionDepth*max(abs(dot(View,Normal)),1e-4)));
    }
    for (uint Bounce=0u; Bounce<4u; ++Bounce)
    {
        DistanceRayHit Hit=TraceDistanceMesh(Origin,Direction,1e20);
        if (Hit.Facet==0xFFFFFFFFu) return Attenuation*SkyAlongApprox(Direction,Material.SpecularRoughness);
        if (Inside && Material.TransmissionDepth>0.0)
            Attenuation*=exp(log(max(Material.TransmissionColor,vec3(1e-5)))*Hit.Travel/Material.TransmissionDepth);
        vec3 Point=Origin+Direction*Hit.Travel;
        if (!Inside) return Attenuation*ShadeHit(Point,Hit.Facet,-Direction);
        vec3 ExitNormal=Facets[Hit.Facet].Normal.xyz;
        if (dot(ExitNormal,Direction)>0.0) ExitNormal=-ExitNormal;
        vec3 Transmitted=refract(Direction,ExitNormal,Ior);
        if (dot(Transmitted,Transmitted)<1e-8) Direction=reflect(Direction,ExitNormal);
        else { Direction=Transmitted; Inside=false; }
        Origin=Point+Direction*0.002;
    }
    return Attenuation*SkyAlongApprox(Direction,Material.SpecularRoughness);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(SurfaceImage);
    if (any(greaterThanEqual(pixel,min(size,ivec2(PC.RenderExtent.xy))))) return;

    vec4 surf = imageLoad(SurfaceImage, pixel);
    vec3 P = surf.xyz;
    uint visibility = floatBitsToUint(surf.w);
    // A miss (no geometry) shows the sky directly.
    if (visibility == kVisibilityInvalid || VisibilityInstance(visibility) >= uint(Instances.length()))
    {
        vec3 V = normalize(P - PC.CameraEye.xyz);
        vec3 sky = SkyAlongApprox(V, 0.0);
        imageStore(OutputImage, pixel, vec4(pow(sky/(sky+vec3(1.0)), vec3(1.0/2.2)), 1.0));
        return;
    }

    // ── the receiving material, decoded ─────────────────────────────────────────────────────────────────────
    // Same unpack, same instance row, same ResolveMaterial call as ReSTIRViewport.slang's primary hit. The
    //    shading frame comes back from the decode, so the normal map and the tangent basis are the raytraced
    //    path's, not a geometric normal read out of an rgba16f image.
    GpuInstance  inst = Instances[VisibilityInstance(visibility)];
    GpuMaterial  mat  = Materials[inst.MaterialIndex];
    vec3  V         = normalize(PC.CameraEye.xyz - P);
    float coneWidth = length(PC.CameraEye.xyz - P) * 0.0015;   // ≈ one pixel's spread at the hit [m]
    SurfaceFrame F;
    ShadingRecord m = ResolveMaterial(mat, inst, VisibilityPrimitive(visibility), P, V, coneWidth, F);
    vec3 N = F.Normal;

    uint flags = PC.Counts.z;
    uint reflMode = PC.Counts.w;

    // An emitter is its own radiance, as in the raytraced kernel.
    if (dot(F.Emission, F.Emission) > 0.0)
    {
        vec3 e = F.Emission * PC.SkyAmbient.w;
        imageStore(OutputImage, pixel, vec4(pow(e/(e+vec3(1.0)), vec3(1.0/2.2)), 1.0));
        return;
    }

    vec3 woLoc = ToLocal(F, V);
    if (woLoc.z <= 0.0) { imageStore(OutputImage, pixel, vec4(0.0, 0.0, 0.0, 1.0)); return; }
    ResolvedLayers layers = ResolveLayers(m, woLoc);

    // --- direct (always, sharp) — the full lobe stack, not a Lambert ---
    vec3 col = vec3(0.0);
    {
        vec3 L = normalize(PC.SunDirectionRadiance.xyz);
        vec3 wiLoc = ToLocal(F, L);
        if (wiLoc.z > 0.0 && PC.SunDirectionRadiance.w > 0.0)
            col += EvaluateBsdf(m, layers, woLoc, wiLoc) * (PC.SunColour.xyz * PC.SunDirectionRadiance.w) * wiLoc.z * DistanceShadow(P,F.Geometric);
    }

    // --- indirect diffuse: an irradiance through this material's hemispherical response ---
    vec3 response = AmbientResponse(m, layers, woLoc);
    if ((flags & kFeatureGlobalIllumination) != 0u)
        col += response * ReconstructIrradiance(pixel,P,visibility) * PC.Tuning.y;        // distance-field diffuse transport
    else
        col += response * PC.SkyAmbient.xyz;                   // plain raster: sky ambient fill (never black)

    // Use the authored specular lobe and its GGX PDF, not an unconditional 4% perfect mirror.
    col += Reflection(P,F,V,m,layers,reflMode);

    if (m.TransmissionWeight>0.0)
    {
        float FresnelWeight=pow((max(m.SpecularIor,1.01)-1.0)/(max(m.SpecularIor,1.01)+1.0),2.0);
        FresnelWeight+= (1.0-FresnelWeight)*pow(1.0-max(dot(N,V),0.0),5.0);
        col+=m.TransmissionWeight*(1.0-FresnelWeight)*Transmission(P,N,V,m,F.ThinWalled);
    }
    // exposure + Reinhard + gamma
    col *= PC.SkyAmbient.w;
    vec3 mapped = pow(col / (col + vec3(1.0)), vec3(1.0/2.2));
    imageStore(OutputImage, pixel, vec4(mapped, 1.0));
}
