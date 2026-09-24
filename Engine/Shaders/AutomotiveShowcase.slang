// Native/GPU showcase adapter. Include inside MaterialEvaluation after its Fresnel/GGX helpers.
// The outer dielectric stays neutral: CoatColor is absorption beneath it, not metallic F0.
#ifndef FRONTIER_AUTOMOTIVE_SHOWCASE_FUNCTIONS
#define FRONTIER_AUTOMOTIVE_SHOWCASE_FUNCTIONS
#ifdef __cplusplus
#ifndef FRONTIER_NATIVE_FLAKES
#define FRONTIER_NATIVE_FLAKES 1
#endif
#endif
#include "AutomotiveFlakePaint.slang"
AutomotivePaintParameters AutomotiveShowcasePaint(AP_INPUT(ShadingRecord) m) {
    AutomotivePaintParameters p=AutomotivePaintDefaults();
    float t=APRange(m.AutomotiveData.y,0.0,1.0);
    p.Pigment=m.BaseColor;p.FlakeReflectance=m.SpecularColor;
    p.Density=m.AutomotiveDensity;p.CoatRoughness=m.CoatRoughness;p.CoatWeight=m.CoatWeight;
    p.FlakeRoughness=m.SpecularRoughness;p.NormalSpread=0.18+0.12*t;
    p.PearlWeight=m.ThinFilmWeight;p.FilmThicknessNm=m.ThinFilmThickness*1000.0;p.FilmIor=m.ThinFilmIor;
    p.DiameterMm=m.AutomotiveData.x==2.0?0.45+0.65*t:0.08+0.22*t;
    return p;
}
AutomotiveFlakePalette AutomotiveShowcasePalette(AP_INPUT(ShadingRecord) m) {
    if(m.AutomotiveData.x==2.0)return AutomotiveRgbPalette();
    return AutomotivePaletteDefaults(); // use the slab's F0 for silver / copper
}
vec3 AutomotiveShowcaseCoat(AP_INPUT(ShadingRecord) m,vec3 wo,vec3 wi) {
    if(wo.z<=0.0001||wi.z<=0.0001)return vec3(0.0);
    vec3 h=normalize(wo+wi);float r=APRange(m.CoatRoughness,0.06,0.7),a=r*r;
    return vec3(APRange(m.CoatWeight,0.0,1.0)*FresnelDielectric(dot(wo,h),APRange(m.CoatIor,1.01,2.5))*APGgx(h,a)*APSmithG1(wo.z,a)*APSmithG1(wi.z,a)/(4.0*wo.z*wi.z));
}
vec3 AutomotiveShowcaseEvaluatePrepared(AP_INPUT(ShadingRecord) m,AutomotivePaintParameters p,AP_INPUT(AutomotiveFlakeSurface) s,vec3 wo,vec3 wi) {
    if(wo.z<=0.0001||wi.z<=0.0001)return vec3(0.0);
    // Use the native material's IOR (not the artistic reflection-tint control in the web lab).
    float eta=APRange(m.CoatIor,1.01,2.5),coat=APRange(m.CoatWeight,0.0,1.0);
    float co=sqrt(max(0.0001,1.0-(1.0-wo.z*wo.z)/(eta*eta)));
    float ci=sqrt(max(0.0001,1.0-(1.0-wi.z*wi.z)/(eta*eta)));
    vec3 absorption=pow(clamp(m.CoatColor,vec3(0.0001),vec3(1.0)),vec3(0.5/co+0.5/ci));
    float transmission=(1.0-coat*FresnelDielectric(wo.z,eta))*(1.0-coat*FresnelDielectric(wi.z,eta));
    vec3 reflection=AutomotiveShowcaseCoat(m,wo,wi);
    p.CoatWeight=0.0;
    return reflection+transmission*mix(vec3(1.0),absorption,coat)*AutomotiveEvaluatePaint(p,s,wo,wi);
}
vec3 AutomotiveShowcaseEvaluate(AP_INPUT(ShadingRecord) m,vec3 wo,vec3 wi) {
    if(wo.z<=0.0001||wi.z<=0.0001)return vec3(0.0);
    AutomotivePaintParameters p=AutomotiveShowcasePaint(m);
    float footprint=max(0.0,m.AutomotiveFootprint);
    AutomotiveFlakeSurface s=AutomotivePrepareFlakes(p,vec2(m.AutomotiveData.z,m.AutomotiveData.w),vec2(footprint,0.0),vec2(0.0,footprint));
    s=AutomotiveApplyFlakePalette(s,AutomotiveShowcasePalette(m));
    return AutomotiveShowcaseEvaluatePrepared(m,p,s,wo,wi);
}
// Matching full-support proposal: half cosine, half smooth-coat GGX VNDF.
// Valid (but not glint-optimised) importance sampling; below-horizon GGX draws are null events.
float AutomotiveShowcasePdf(AP_INPUT(ShadingRecord) m,vec3 wo,vec3 wi) {
    if(wo.z<=0.0001||wi.z<=0.0)return 0.0;
    vec3 h=normalize(wo+wi);float r=APRange(m.CoatRoughness,0.06,0.7);
    return 0.5*wi.z*kInvPi+0.5*GgxVndfPdf(wo,h,vec2(r*r))/(4.0*max(dot(wo,h),0.000001));
}
vec4 AutomotiveShowcaseSample(AP_INPUT(ShadingRecord) m,vec3 wo,vec4 u) {
    if(wo.z<=0.0001)return vec4(0.0);
    vec3 wi;
    if(u.x<0.5){float r=sqrt(u.y),phi=2.0*kPi*u.z;wi=vec3(r*cos(phi),r*sin(phi),sqrt(max(0.0,1.0-u.y)));}
    else{float r=APRange(m.CoatRoughness,0.06,0.7);wi=reflect(-wo,SampleGgxVndf(wo,vec2(r*r),vec2(u.y,u.z)));}
    if(wi.z<=0.0)return vec4(0.0);
    return vec4(wi,AutomotiveShowcasePdf(m,wo,wi));
}
#endif
