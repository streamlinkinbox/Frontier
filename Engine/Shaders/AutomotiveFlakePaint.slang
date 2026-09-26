// Automotive paint v2: finite, object/UV-locked flakes beneath an independent coat.
// Include after MaterialEvaluation.slang (uses its existing thin-film Fresnel).
// Original finite-cell approximation, NOT an implementation of DB23 binomial glints.
// All directions in the unperturbed material tangent frame; +Z is the surface normal.
// UV coordinates and derivatives are in metres. Returns BRDF, without incident cosine.
#ifndef FRONTIER_AUTOMOTIVE_FLAKE_PAINT
#define FRONTIER_AUTOMOTIVE_FLAKE_PAINT
#ifdef FRONTIER_NATIVE_FLAKES
#define AP_INPUT(T) const T&
#else
#define AP_INPUT(T) T
#endif
struct AutomotivePaintParameters {
    vec3 Pigment;
    vec3 FlakeReflectance;
    float Density;           // expected flakes per cell, [0,16]; extra independent populations above 1
    float DiameterMm;        // largest in-plane flake diameter
    float NormalSpread;     // RMS slope spread, NOT clearcoat roughness
    float FlakeRoughness;    // Beckmann slope width of an individual flake
    float CoatRoughness;    // GGX perceptual roughness
    float CoatWeight;
    vec3 CoatTint;           // artistic coating tint, independent of pigment and flake palette
    float CoatTintStrength;
    float PearlWeight;
    float FilmThicknessNm;
    float FilmIor;
    uint Seed;
};
// Up to eight weighted colour families, each with a linear-RGB endpoint range.
// Count=0 (or all weights zero) retains the legacy FlakeReflectance fallback.
struct AutomotiveFlakePalette {
    int Count;
    vec3 Minimum[8];
    vec3 Maximum[8];
    float Weight[8];
};
AutomotiveFlakePalette AutomotivePaletteDefaults() {
    AutomotiveFlakePalette palette;palette.Count=0;
    for(int i=0;i<8;++i){palette.Minimum[i]=vec3(0.72,0.77,0.82);palette.Maximum[i]=palette.Minimum[i];palette.Weight[i]=1.0;}
    return palette;
}
AutomotiveFlakePalette AutomotiveRgbPalette() {
    AutomotiveFlakePalette p=AutomotivePaletteDefaults();p.Count=3;
    p.Minimum[0]=vec3(0.42,0.006,0.004);p.Maximum[0]=vec3(0.95,0.055,0.018);
    p.Minimum[1]=vec3(0.004,0.24,0.025);p.Maximum[1]=vec3(0.035,0.8,0.15);
    p.Minimum[2]=vec3(0.003,0.025,0.35);p.Maximum[2]=vec3(0.025,0.18,0.95);
    return p;
}
struct AutomotiveFlakeSurface {
    uint FacetKey;
    vec3 Colour;
    vec3 MeanColour;
    vec2 Slope;
    float Weight;
    float MeanWeight;
    float Unresolved;
    float Alpha;
    float PopulationAlpha;
};
float APRange(float x,float lo,float hi) { return !(x>=lo)?lo:min(x,hi); }
AutomotivePaintParameters AutomotivePaintDefaults() {
    AutomotivePaintParameters p;
    p.Pigment=vec3(0.014,0.045,0.13);p.FlakeReflectance=vec3(0.72,0.77,0.82);
    p.Density=0.8;p.DiameterMm=0.35;p.NormalSpread=0.24;p.FlakeRoughness=0.065;
    p.CoatRoughness=0.23;p.CoatWeight=1.0;p.CoatTint=vec3(1.0);p.CoatTintStrength=0.0;p.PearlWeight=0.0;p.FilmThicknessNm=420.0;p.FilmIor=1.48;p.Seed=17u;
    return p;
}
uint APHash(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
float APRandom(uint x) { return float(APHash(x)>>8)*(1.0/16777216.0); }
AutomotiveFlakeSurface AutomotivePrepareFlakes(AP_INPUT(AutomotivePaintParameters) p,vec2 uv,vec2 dx,vec2 dy) {
    AutomotiveFlakeSurface s;
    const float radius=0.36;
    float frequency=2.0*radius/(APRange(p.DiameterMm,0.02,2.0)*0.001);
    if(!(abs(uv.x)<1000.0&&abs(uv.y)<1000.0))uv=vec2(0.0);
    vec2 q=uv*frequency;float cx=float(floor(q.x)),cy=float(floor(q.y));
    float density=APRange(p.Density,0.0,16.0);
    float footprint=max(length(dx),length(dy))*frequency;
    footprint=APRange(footprint,0.0,10000.0);
    float aa=max(0.0001,min(0.5,footprint*0.6));
    s.Weight=0.0;uint key=0u;
    // Full-cell jitter with a fixed 3x3 neighbourhood, rather than visible centred dots.
    // Overlapping flakes select the topmost (largest coverage) facet; no additive energy.
    for(int layer=0;layer<16;++layer){
      float occupancy=clamp(density-float(layer),0.0,1.0);
      if(occupancy<=0.0)break;
      for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){
        float nx=cx+float(ox),ny=cy+float(oy);
        uint ix=uint(nx-float(floor(nx/65536.0))*65536.0),iy=uint(ny-float(floor(ny/65536.0))*65536.0);
        uint candidate=APHash(ix^APHash(iy)^p.Seed);
        if(layer>0)candidate=APHash(candidate^APHash(uint(layer)*0x9e3779b9u));
        vec2 centre=vec2(nx,ny)+vec2(APRandom(candidate+1u),APRandom(candidate+2u));
        vec2 local=q-centre;
        float angle=6.28318530718*APRandom(candidate+3u),c=cos(angle),sn=sin(angle);
        vec2 ell=vec2(c*local.x+sn*local.y,(-sn*local.x+c*local.y)/0.65);
        float occupied=APRandom(candidate)<occupancy?1.0:0.0;
        float coverage=occupied*(1.0-smoothstep(radius-aa,radius+aa,length(ell)));
        if(coverage>s.Weight){s.Weight=coverage;key=candidate;}
    }
    }
    s.FacetKey=key;s.Colour=clamp(p.FlakeReflectance,vec3(0.0),vec3(0.98));s.MeanColour=s.Colour;
    // Poisson coverage approximation for overlaps, not an exact footprint integral.
    s.MeanWeight=1.0-exp(-density*3.14159265359*radius*radius*0.65);
    s.Unresolved=smoothstep(0.35,1.5,footprint);
    float spread=APRange(p.NormalSpread,0.0,0.7);
    float radial=spread*sqrt(-float(log(max(0.000001,APRandom(key+4u)))));
    float azimuth=6.28318530718*APRandom(key+5u);
    s.Slope=radial*vec2(cos(azimuth),sin(azimuth));
    s.Alpha=APRange(p.FlakeRoughness,0.025,0.5);
    s.PopulationAlpha=sqrt(s.Alpha*s.Alpha+spread*spread);
    return s;
}
AutomotiveFlakeSurface AutomotiveApplyFlakePalette(AutomotiveFlakeSurface s,AP_INPUT(AutomotiveFlakePalette) palette) {
    int count=int(APRange(float(palette.Count),0.0,8.0));
    float total=0.0;vec3 average=vec3(0.0);
    for(int i=0;i<8;++i){if(i>=count)break;
        float weight=APRange(palette.Weight[i],0.0,1000.0);
        vec3 lo=clamp(palette.Minimum[i],vec3(0.0),vec3(0.98)),hi=clamp(palette.Maximum[i],vec3(0.0),vec3(0.98));
        total+=weight;average+=(lo+hi)*(0.5*weight);
    }
    if(total<=0.0)return s;
    s.MeanColour=average/total;
    float pick=min(APRandom(s.FacetKey+0xa511e9b3u),0.999999)*total;
    float cumulative=0.0;
    for(int i=0;i<8;++i){if(i>=count)break;
        cumulative+=APRange(palette.Weight[i],0.0,1000.0);
        if(pick<cumulative){
            s.Colour=mix(clamp(palette.Minimum[i],vec3(0.0),vec3(0.98)),clamp(palette.Maximum[i],vec3(0.0),vec3(0.98)),APRandom(s.FacetKey+0x63d83595u));
            break;
        }
    }
    return s;
}
vec3 AutomotiveCoatTint(AP_INPUT(AutomotivePaintParameters) p) {
    return mix(vec3(1.0),clamp(p.CoatTint,vec3(0.0),vec3(1.0)),APRange(p.CoatTintStrength,0.0,1.0));
}
vec3 AutomotiveFlakeFresnel(AP_INPUT(AutomotivePaintParameters) p,vec3 colour,float vh) {
    vec3 f=FresnelSchlick(colour,vh);
    float pearl=APRange(p.PearlWeight,0.0,1.0);
    if(pearl>0.0)f=mix(f,ThinFilmReflectance(vh,APRange(p.FilmIor,1.01,2.5),APRange(p.FilmThicknessNm,0.0,1500.0)*0.001,f,false),pearl);
    return f;
}
float APBeckmann(vec3 h,vec2 centre,float alpha) {
    if(!(h.z>0.0001))return 0.0;
    vec2 d=vec2(h.x,h.y)/h.z-centre;
    float z2=h.z*h.z;
    return exp(-dot(d,d)/(alpha*alpha))/(3.14159265359*alpha*alpha*z2*z2);
}
float APSmithG1(float cosine,float alpha) {
    float c2=cosine*cosine;
    return 2.0*cosine/(cosine+sqrt(alpha*alpha+(1.0-alpha*alpha)*c2));
}
float APGgx(vec3 h,float alpha) {
    float a2=alpha*alpha,t=h.z*h.z*(a2-1.0)+1.0;
    return a2/(3.14159265359*t*t);
}
vec3 AutomotiveEvaluateCoat(AP_INPUT(AutomotivePaintParameters) p,vec3 v,vec3 l) {
    if(!(v.z>0.0001&&l.z>0.0001))return vec3(0.0);
    vec3 sum=v+l;float len=length(sum);if(!(len>0.0001&&len<3.0))return vec3(0.0);
    vec3 h=sum/len;float r=APRange(p.CoatRoughness,0.06,0.7),a=r*r;
    float f=FresnelDielectric(clamp(dot(v,h),0.0,1.0),1.5);
    return AutomotiveCoatTint(p)*(APRange(p.CoatWeight,0.0,1.0)*f*APGgx(h,a)*APSmithG1(v.z,a)*APSmithG1(l.z,a)/(4.0*v.z*l.z));
}
vec3 AutomotiveEvaluatePaint(AP_INPUT(AutomotivePaintParameters) p,AP_INPUT(AutomotiveFlakeSurface) s,vec3 v,vec3 l) {
    if(!(v.z>0.0001&&l.z>0.0001))return vec3(0.0);
    vec3 sum=v+l;float len=length(sum);if(!(len>0.0001&&len<3.0))return vec3(0.0);
    vec3 h=sum/len;float vh=clamp(dot(v,h),0.0,1.0);
    float coverage=mix(s.Weight,s.MeanWeight,s.Unresolved);
    float discrete=s.Weight*APBeckmann(h,s.Slope,s.Alpha);
    float population=s.MeanWeight*APBeckmann(h,vec2(0.0),s.PopulationAlpha);

    // Macro masking is a bounded approximation for the tilted, finite flake population.
    float masking=APSmithG1(v.z,s.PopulationAlpha)*APSmithG1(l.z,s.PopulationAlpha);
    vec3 resolvedF=AutomotiveFlakeFresnel(p,s.Colour,vh),populationF=AutomotiveFlakeFresnel(p,s.MeanColour,vh);
    vec3 flakes=mix(resolvedF*discrete,populationF*population,s.Unresolved)*(masking/(4.0*v.z*l.z));
    vec3 base=(1.0-coverage)*clamp(p.Pigment,vec3(0.0),vec3(0.95))*0.31830988618+flakes;
    float coat=APRange(p.CoatWeight,0.0,1.0);
    float transmission=(1.0-coat*FresnelDielectric(v.z,1.5))*(1.0-coat*FresnelDielectric(l.z,1.5));
    // Thin-layer two-interface attenuation. No refracted-direction/multiple-bounce solver claim.
    // Artistic attenuation/tinted reflection; not a spectral dye/Beer-Lambert coating model.
    vec3 tint=mix(vec3(1.0),AutomotiveCoatTint(p),coat);
    return base*transmission*tint+AutomotiveEvaluateCoat(p,v,l);
}
#endif
