// Linear RGB coefficients in inverse metres. Representative artistic presets,
// not spectrally measured samples. All use water's approximate visible IOR.
export const WATER_PRESETS = {
  clean: {name:'Clean water', description:'Clear transmission, subtle blue absorption, and crisp reflections.', absorption:[.28,.045,.018], scattering:.018, color:[.022,.10,.12], roughness:.065, micro:.012, film:0, bed:'#b7c7c5', depth:null, wind:0},
  ocean: {name:'Ocean water', description:'Blue-green attenuation, wind-driven surface detail, and broader sun glints.', absorption:[.65,.12,.045], scattering:.16, color:[.012,.14,.21], roughness:.18, micro:.095, film:0, bed:'#8d9a8f', depth:null, wind:1},
  swamp: {name:'Swamp water', description:'Strong tannin absorption, green backscatter, and patchy surface algae.', absorption:[1.9,1.1,3.4], scattering:1.1, color:[.09,.14,.026], roughness:.28, micro:.009, film:.8, bed:'#4c5033', depth:null, wind:0},
  puddle: {name:'Puddle', description:'A shallow wet-stone bed, very little optical depth, and mirror-like reflections.', absorption:[.38,.22,.12], scattering:.045, color:[.085,.08,.057], roughness:.04, micro:.006, film:0, bed:'#655f54', depth:.065, wind:0},
  dirty: {name:'Dirty water', description:'Suspended sediment scatters light, obscures the bed, and warms the water to brown.', absorption:[.8,1.45,2.8], scattering:2.8, color:[.28,.16,.055], roughness:.22, micro:.018, film:.13, bed:'#766143', depth:null, wind:0}
};
export const waterVertex = `
  uniform mat4 uReflectionMatrix;
  varying vec3 vPosition; varying vec3 vNormal; varying vec4 vReflection;
  void main(){vec4 wp=modelMatrix*vec4(position,1.);vPosition=wp.xyz;
    vNormal=normalize(mat3(modelMatrix)*normal);vReflection=uReflectionMatrix*wp;
    gl_Position=projectionMatrix*viewMatrix*wp;}
`;
export const waterFragment = `
  uniform float uTime,uLevel,uBedY,uRoughness,uMicro,uScattering,uFilm,uIOR,uNear,uFar;
  uniform vec3 uAbsorption,uScatterColor,uBedColor;
  uniform sampler2D uEnvironment,uSceneColor,uSceneDepth,uReflection;
  uniform mat4 uViewProjection,uView;
  uniform vec2 uDuck;
  varying vec3 vPosition,vNormal; varying vec4 vReflection;
  #include <packing>
  const float PI=3.14159265359;
  // ---- anti-tiling hash/noise (2026 fix) ----
  // IQ-style hash12 with mod289 permutation — no sin periodicity tiling
  float hash12(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
  }
  float hash21(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453123); }
  float valueNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f*f*(3.0-2.0*f);
    float a = hash12(i);
    float b = hash12(i+vec2(1.0,0.0));
    float c = hash12(i+vec2(0.0,1.0));
    float d = hash12(i+vec2(1.0,1.0));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
  }
  // 2D rotation matrix for domain warp (23 deg)
  mat2 rot23(){ float c=0.9205, s=0.3907; return mat2(c,-s,s,c); }
  float fbm(vec2 p){
    // 3 octaves, decorrelated frequency/rotation, domain warp
    float v=0.0; float amp=0.5; mat2 R=rot23();
    for(int i=0;i<3;i++){
      v += amp * valueNoise(p);
      p = R * p * 2.07 + vec2(5.11, 3.77);
      amp *= 0.48;
    }
    return v;
  }
  float fbmWarp(vec2 p){
    // domain-warped FBM — breaks axis alignment
    vec2 q = vec2(fbm(p+vec2(0.0,0.0)), fbm(p+vec2(5.2,1.3)));
    vec2 r = vec2(fbm(p + 3.7*q + vec2(1.7,9.2)), fbm(p + 3.7*q + vec2(8.3,2.8)));
    return fbm(p + 3.0*r);
  }
  vec2 environmentUV(vec3 r){return vec2(atan(r.z,r.x)/(2.*PI)+.5,asin(clamp(r.y,-1.,1.))/PI+.5);}
  float smith(float nd,float a2){return 2.*nd/(nd+sqrt(a2+(1.-a2)*nd*nd));}
  void main(){
    if(distance(vPosition.xz,vec2(-2.4,-1.65))<.64||distance(vPosition.xz,vec2(2.7,1.65))<.45)discard;
    vec2 p=vPosition.xz;
    // ---- micro / capillary detail: two decorrelated octaves with flow ----
    // flow vector varies slowly to avoid static grid; footprint fade unchanged
    vec2 flow = vec2(uTime*0.07, uTime*0.04);
    vec2 micro = vec2(0.0);
    {
      // octave 1: large capillary (~0.12 m)
      vec2 p1 = (p + flow) * 7.3;
      micro += vec2(
        fbmWarp(p1*0.85 + vec2(0.0, uTime*0.55)) - 0.5,
        fbmWarp(p1*1.15 + vec2(3.1, uTime*0.41)) - 0.5
      ) * 1.0;
      // octave 2: small ripple (~0.05 m), orthogonal bias
      vec2 p2 = mat2(0.866,-0.5,0.5,0.866) * p * 18.5 + flow*1.7;
      micro += vec2(
        fbmWarp(p2 + vec2(uTime*0.62, 0.0)) - 0.5,
        fbmWarp(p2.yx*1.07 + vec2(uTime*0.37, 0.0)) - 0.5
      ) * 0.42;
    }
    float footprint=max(length(dFdx(p)),length(dFdy(p)));
    float detailFilter=1.-smoothstep(.04,.20,footprint);
    // micro normal scaled by preset uMicro, with stochastic jitter to break last grid
    vec3 N=normalize(vNormal+vec3(micro.x,0.,micro.y)*uMicro*detailFilter);
    float variance=dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N));
    float roughness=clamp(sqrt(uRoughness*uRoughness+min(.18,variance*.6)),.025,.65);
    vec3 V=normalize(cameraPosition-vPosition);if(dot(N,V)<0.)N=-N;
    float nv=max(dot(N,V),.001);
    float f0=pow((uIOR-1.)/(uIOR+1.),2.);
    float F=f0+(1.-f0)*pow(1.-nv,5.);
    // Snell's law, then intersect the known planar bed. Scene depth rejects foreground samples.
    vec3 ray=refract(-V,N,1./uIOR);
    float depth=max(.015,vPosition.y-uBedY);
    float path=depth/max(.08,-ray.y);
    vec3 bedPoint=vPosition+ray*path;
    vec4 projected=uViewProjection*vec4(bedPoint,1.);
    vec2 refrUV=projected.xy/projected.w*.5+.5;
    vec3 bed=uBedColor;
    if(all(greaterThan(refrUV,vec2(.002)))&&all(lessThan(refrUV,vec2(.998)))){
      float sceneDistance=-perspectiveDepthToViewZ(texture2D(uSceneDepth,refrUV).x,uNear,uFar);
      float surfaceDistance=-(uView*vec4(vPosition,1.)).z;
      if(sceneDistance>surfaceDistance+.015)bed=texture2D(uSceneColor,refrUV).rgb;
    }
    vec3 extinction=uAbsorption+vec3(uScattering);
    vec3 transmittance=exp(-extinction*path);
    vec3 transmission=bed*transmittance+uScatterColor*(vec3(1.)-transmittance);
    // ---- caustic: two decorrelated layers, no single sin grid ----
    {
      float c1 = fbmWarp(bedPoint.xz*2.2 + vec2(uTime*0.12, uTime*0.08));
      float c2 = fbmWarp(bedPoint.zx*3.1 - vec2(uTime*0.09, uTime*0.11));
      float caustic = pow(c1 * c2, 2.4);
      // modulate by depth to avoid tiling flash on shallow rim
      caustic *= smoothstep(0.02, 0.12, depth);
      transmission+=vec3(.13,.13,.085)*caustic*0.9*transmittance*transmittance;
    }
    vec3 R=reflect(-V,N);
    vec3 reflected=texture2D(uEnvironment,environmentUV(R),roughness*7.).rgb;
    vec2 reflectUV=vReflection.xy/vReflection.w;
    reflectUV+=N.xz*.018;
    float valid=step(.002,reflectUV.x)*step(.002,reflectUV.y)*step(reflectUV.x,.998)*step(reflectUV.y,.998);
    reflected=mix(reflected,texture2D(uReflection,clamp(reflectUV,.001,.999),roughness*6.).rgb,valid);
    vec3 color=transmission*(1.-F)+reflected*F;
    // Cook–Torrance GGX direct sun specular, dielectric Fresnel and Smith masking.
    vec3 L=normalize(vec3(-.5,1.,.35)),H=normalize(V+L);
    float nl=max(dot(N,L),.001),nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
    float a=max(.015,roughness*roughness),a2=a*a;
    float denom=nh*nh*(a2-1.)+1.;
    float D=a2/(PI*denom*denom);
    float G=smith(nv,a2)*smith(nl,a2);
    float fresnelSun=f0+(1.-f0)*pow(1.-vh,5.);
    float spec=min(18.,D*G*fresnelSun/(4.*nv*nl));
    color+=vec3(1.,.93,.77)*spec*nl*2.1;
    // Local contact-darkening under the floating body is a shading approximation.
    color*=1.-.18*exp(-dot(p-uDuck,p-uDuck)/.28);
    // ---- algae film: warped FBM, not single noise(p*2.5) ----
    {
      vec2 fp = p*1.6 + vec2(fbm(p*0.7+vec2(0.0,uTime*0.02)), fbm(p*0.7+vec2(1.7,uTime*0.015))) * 0.9;
      float fn = fbmWarp(fp*1.8);
      float film=smoothstep(.48,.72,fn)*uFilm;
      film *= 1.0 - 0.35*fbmWarp(p*6.0); // break uniform blobs
      color=mix(color,vec3(.12,.18,.035),film*.6);
    }
    gl_FragColor=vec4(color,1.);
    #include <tonemapping_fragment>
    #include <colorspace_fragment>
  }
`;
