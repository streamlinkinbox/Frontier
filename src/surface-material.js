import * as THREE from 'three';
import {stoneHeightGLSL} from './surface-height.js';
import {mineralFlakesGLSL} from './surface-flakes.js';

export const surfaceDefaults = Object.freeze({surfaceEnabled:true,oxidation:38,weathering:55,grain:65,layerDepth:65,grainHeight:70,grainSize:240,mineralFlakes:60,flakeMetallic:20,flakeRoughness:40,flakeSpecular:70,flakeClearcoat:35,flakeCoatRoughness:20,flakeColor:65,flakeRelief:55,flakeIOR:1.5,peeling:25,crystals:35});
// One-click matte reference finish; only appearance, never formation geometry.
export const referenceStoneFinish=Object.freeze({grain:65,grainSize:140,layerDepth:85,grainHeight:85,mineralFlakes:25,flakeColor:25,flakeMetallic:0,flakeRoughness:88,flakeSpecular:45,flakeClearcoat:0});
export function surfaceSettings(settings={}) {
  const result={surfaceEnabled:settings.surfaceEnabled ?? surfaceDefaults.surfaceEnabled};
  for(const key of ['oxidation','weathering','grain','layerDepth','grainHeight','mineralFlakes','flakeMetallic','flakeRoughness','flakeSpecular','flakeClearcoat','flakeCoatRoughness','flakeColor','flakeRelief','peeling','crystals'])result[key]=THREE.MathUtils.clamp(Number.isFinite(settings[key])?settings[key]:surfaceDefaults[key],0,100);
  result.grainSize=THREE.MathUtils.clamp(Number.isFinite(settings.grainSize)?settings.grainSize:surfaceDefaults.grainSize,2,2000);
  result.flakeIOR=THREE.MathUtils.clamp(Number.isFinite(settings.flakeIOR)?settings.flakeIOR:surfaceDefaults.flakeIOR,1,2.5);
  return result;
}
const familyIndex={limestone:0,sandstone:1,slate:2};

// Analytic world-space value field + its exact gradient. No texture reads, UVs,
// image assets or framebuffer dependencies. Integer hashes are stable in WebGL2.
export const fieldGLSL=/* glsl */`
varying vec3 vRockWorld;
uniform float uSurfaceEnabled;
uniform float uOxidation;
uniform float uWeathering;
uniform float uGrain;
uniform float uLayerDepth;
uniform float uGrainHeight;
uniform float uGrainSize;
uniform float uMineralFlakes;
uniform float uFlakeMetallic;
uniform float uFlakeRoughness;
uniform float uFlakeSpecular;
uniform float uFlakeClearcoat;
uniform float uFlakeCoatRoughness;
uniform float uFlakeColor;
uniform float uFlakeRelief;
uniform float uFlakeIOR;
uniform float uPeeling;
uniform float uCrystals;
uniform float uRockFamily;
uniform vec3 uSurfaceSeed;
float rockHash(ivec3 cell) {
  uvec3 q=uvec3(cell);
  uint h=q.x*1597334677u ^ q.y*3812015801u ^ q.z*2798796415u;
  h=(h^(h>>16u))*2246822519u;h=(h^(h>>13u))*3266489917u;
  return float(h^(h>>16u))/4294967295.0;
}
vec4 rockField(vec3 p) {
  ivec3 cell=ivec3(floor(p));vec3 f=fract(p);
  vec3 u=f*f*f*(f*(f*6.0-15.0)+10.0);
  vec3 du=30.0*f*f*(f*(f-2.0)+1.0);
  float a=rockHash(cell),b=rockHash(cell+ivec3(1,0,0));
  float c=rockHash(cell+ivec3(0,1,0)),d=rockHash(cell+ivec3(1,1,0));
  float e=rockHash(cell+ivec3(0,0,1)),f1=rockHash(cell+ivec3(1,0,1));
  float g=rockHash(cell+ivec3(0,1,1)),h=rockHash(cell+ivec3(1,1,1));
  float ab=mix(a,b,u.x),cd=mix(c,d,u.x),ef=mix(e,f1,u.x),gh=mix(g,h,u.x);
  float lower=mix(ab,cd,u.y),upper=mix(ef,gh,u.y);
  vec3 grad=vec3(mix(mix(b-a,d-c,u.y),mix(f1-e,h-g,u.y),u.z),mix(cd-ab,gh-ef,u.z),upper-lower)*du;
  return vec4(mix(lower,upper,u.z),grad);
}
// Jittered cells produce discrete angular aggregates, not a uniform bumpy field.
vec4 rockAggregate(vec3 p) {
  ivec3 cell=ivec3(floor(p));vec3 f=fract(p);float nearest=10.0;vec3 delta=vec3(0.0);
  for(int z=-1;z<=1;z++)for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
    ivec3 offset=ivec3(x,y,z),c=cell+offset;
    vec3 center=vec3(offset)+vec3(rockHash(c),rockHash(c+ivec3(31,7,11)),rockHash(c+ivec3(3,43,17)));
    vec3 d=f-center;
    vec3 axes=vec3(.8+rockHash(c+ivec3(9,2,3))*.6,.8+rockHash(c+ivec3(2,9,3))*.6,.8+rockHash(c+ivec3(2,3,9))*.6);
    float radius=.65+rockHash(c+ivec3(13,17,29))*.65;
    float distance=(length(d*axes)*.60+dot(abs(d),axes)*.40)/radius;
    if(distance<nearest){nearest=distance;delta=(normalize(d*axes+vec3(.0001))*axes*.60+sign(d)*axes*.40)/radius;}
  }
  return vec4(nearest,delta);
}
// Finite, sparse trickles with independent starts, widths and lengths. No
// repeating bedding bands or cliff-height vertical stripes.
float rockRunoff(vec3 p,vec3 seed,float footprint){
  vec2 cell=floor(p.xz*.65);float stain=0.0;
  float broken=smoothstep(.24,.45,rockField(p*vec3(2.0,1.3,2.0)+seed).x);
  for(int x=-1;x<=1;x++)for(int z=-1;z<=1;z++)for(int y=0;y<=1;y++){
    float level=floor(p.y/8.0)+float(y);
    ivec3 id=ivec3(cell.x+float(x),level,cell.y+float(z))+ivec3(seed);
    float h=rockHash(id);if(h<.72)continue;
    vec2 center=(vec2(id.x,id.z)-seed.xz+vec2(rockHash(id+ivec3(3,7,1)),rockHash(id+ivec3(5,1,9))))/.65;
    // Local pockets at nonperiodic elevations source a short, broken trail.
    float start=(level+rockHash(id+ivec3(7,19,5)))*8.0;
    float lengthM=1.0+rockHash(id+ivec3(29,1,3))*6.0;
    float down=start-p.y;
    float t=clamp(down/lengthM,0.0,1.0);
    center+=vec2(sin(down*1.1+h*13.0),cos(down*.8+h*9.0))*.025;
    float width=mix(.065,.012,t)*( .6+rockHash(id+ivec3(8,3,2)));
    float line=1.0-smoothstep(width,width+max(.025,footprint),length(p.xz-center));
    stain=max(stain,line*smoothstep(0.0,.18,down)*(1.0-smoothstep(.65,1.0,t))*broken);
  }
  return stain;
}
float rockFilter(float frequency,float footprint){return 1.0-smoothstep(.25,.65,frequency*footprint);}
${mineralFlakesGLSL}
${stoneHeightGLSL}
`;
export const surfaceGLSL=/* glsl */`
float rockFlakeMask=0.0;
float rockFlakeFinish=.5;
if(uSurfaceEnabled>.5){
  vec3 p=vRockWorld;
  vec3 nw=inverseTransformDirection(normal,viewMatrix);
  vec3 stoneFaceNormal=nw;
  float up=max(nw.y,0.0),wall=1.0-abs(nw.y);
  float footprint=max(length(dFdx(p)),length(dFdy(p)));
  bool sand=uRockFamily>.5&&uRockFamily<1.5;
  bool slate=uRockFamily>1.5;
  vec3 seed=uSurfaceSeed;
  // Rotate the analytic domain so cubic interpolation axes do not align with
  // the rock faces. Transform its analytic gradient back to world space.
  mat3 domain=mat3(.36,.48,-.8, -.8,.60,0.0, .48,.64,.60);
  vec3 q=domain*p;
  float mineral=rockField(q*.24+seed).x;
  float rind=rockField(q*1.7+seed+vec3(12.0,1.0,3.0)).x;
  float pocket=smoothstep(.62,.83,rockField(q*.46+seed+vec3(21.0,8.0,2.0)).x);
  float fracture=1.0-smoothstep(.015,.055+footprint*.1,abs(rind-.48));
  float iron=pocket*(.4+.6*rind)+fracture*pocket*.5;
  float streak=rockRunoff(p,seed,footprint)*wall;
  vec3 substrate=diffuseColor.rgb*mix(.91,1.07,mineral);
  vec3 rust=slate?vec3(.27,.13,.062):sand?vec3(.46,.24,.105):vec3(.40,.22,.10);
  float stain=clamp((iron*.85+streak*pocket*.5)*uOxidation,0.0,.75);
  diffuseColor.rgb=mix(substrate,rust,stain);
  float aged=smoothstep(.48,.75,rind)*uWeathering;
  float fresh=(1.0-smoothstep(.25,.40,rind))*uPeeling;
  diffuseColor.rgb*=1.0-streak*uWeathering*.16;
  diffuseColor.rgb=mix(diffuseColor.rgb,substrate*1.10,fresh*.32);
  vec3 powder=sand?vec3(.49,.39,.24):slate?vec3(.23,.26,.27):vec3(.53,.50,.39);
  diffuseColor.rgb=mix(diffuseColor.rgb,powder,up*up*aged*.20);
  // Size is explicit millimetres. These are exaggerated aggregate chips, not
  // a claim that sandstone sand particles are centimetres across.
  float frequency=1000.0/(uGrainSize*.32)*(slate?1.3:sand?.85:1.0);
  float grainVisibility=rockFilter(frequency,footprint);
  vec4 aggregate=vec4(.5,0.0,0.0,0.0);
  if(grainVisibility>.001){
  vec3 grainDomain=q*frequency+seed;
  grainDomain+=vec3(rockField(grainDomain*.41).x,rockField(grainDomain*.41+vec3(13.0,7.0,29.0)).x,rockField(grainDomain*.41+vec3(3.0,31.0,5.0)).x)*1.2;
  aggregate=rockAggregate(grainDomain);
  }
  float boundary=smoothstep(.30,.72,aggregate.x);
  float grains=1.0-boundary;
  float distribution=smoothstep(.27,.58,rockField(q*3.1+seed).x);
  float grainAmount=uGrain*grainVisibility*(.42+.58*distribution);
  float fineFrequency=max(frequency*4.3,125.0);
  vec4 fine=rockField(q*fineFrequency+seed+vec3(7.0,3.0,21.0));
  float fineVisibility=rockFilter(fineFrequency,footprint);
  float pitFrequency=frequency*.23;
  vec4 pits=rockField(q*pitFrequency+seed+vec3(2.0,19.0,7.0));
  float pitVisibility=rockFilter(pitFrequency,footprint);
  float pitMask=(1.0-smoothstep(.20,.34,pits.x))*distribution;
  diffuseColor.rgb*=1.0+(grains-.5)*grainAmount*.30-pitMask*pitVisibility*uWeathering*.18;
  vec3 gradient=transpose(domain)*aggregate.yzw*grainAmount*.18*boundary*(1.0-boundary)*4.0;
  gradient+=transpose(domain)*fine.yzw*uGrain*fineVisibility*.065;
  gradient+=transpose(domain)*pits.yzw*.12*uWeathering*pitVisibility*pitMask;
  // A separate plate stack over the existing rough aggregate substrate.
  // Larger basal chips, medium flakes and fine flecks retain discrete normals
  // and mineral colours, like layered paint flakes but entirely dielectric.
  RockMineral flakes=RockMineral(0.0,vec3(0.0),.5,0.0,vec3(.5),0.0,.5);
  if(uMineralFlakes>.001)flakes=rockMineralLayers(p,nw,1000.0/uGrainSize,footprint,seed);
  float flakeAmount=flakes.coverage*uMineralFlakes;
  rockFlakeMask=flakeAmount;rockFlakeFinish=flakes.finish;
  metalnessFactor=uFlakeMetallic*flakeAmount;
  vec3 flakeColor=substrate*mix(.70,1.38,flakes.tint);
  vec3 paleMineral=slate?vec3(.48,.52,.53):sand?vec3(.64,.54,.37):vec3(.67,.65,.55);
  flakeColor=mix(flakeColor,paleMineral,smoothstep(.65,.9,flakes.tint)*.32);
  vec3 uniqueColor=mix(substrate*.7,vec3(.75),flakes.color);
  flakeColor=mix(flakeColor,uniqueColor,uFlakeColor);
  diffuseColor.rgb=mix(diffuseColor.rgb,flakeColor,flakeAmount*.85);
  diffuseColor.rgb*=1.0-flakes.edge*flakeAmount*.20;
  // Flat cells suppress underlying bump; raised cells carry their own tilt and bevel.
  gradient=mix(gradient,flakes.slope*(slate?.68:.52),flakeAmount);
  gradient-=nw*dot(nw,gradient);
  nw=normalize(nw-gradient);
  // Height-field relief is evaluated after flake blending so flakes cannot
  // erase the stone relief. Flat flakes still mask grain height locally.
  vec2 stone=rockStoneHeight(p,stoneFaceNormal,seed,footprint);
  float raisedCoverage=1.0-flakeAmount*(1.0-flakes.relief);
  float continuousGrain=rockField(q*frequency+seed+vec3(17.0,3.0,41.0)).x;
  // Grain height is independent of the colour/normal "strength" control.
  // Two strength sliders must not multiply each other into near-zero relief.
  float grainHeight=(smoothstep(.18,.82,continuousGrain)-.5)*min(uGrainSize*.00032,.12)*uGrainHeight*grainVisibility*.65;
  grainHeight+=(fine.x-.5)*.0018*uGrainHeight*fineVisibility;
  float totalHeight=stone.x*uLayerDepth*1.8+grainHeight*raisedCoverage;
  // Derive height normals on the geometric face, then combine their tangent
  // slope with the existing detail. A noisy normal is not a valid derivative
  // frame: using it here amplified grain speckles at grazing angles.
  vec3 heightNormal=rockHeightNormal(p,stoneFaceNormal,totalHeight);
  vec3 heightSlope=heightNormal/max(dot(heightNormal,stoneFaceNormal),.35)-stoneFaceNormal;
  nw=normalize(nw+heightSlope);
  diffuseColor.rgb*=1.0-stone.y*uLayerDepth*.08;
  normal=normalize(mat3(viewMatrix)*nw);
  float baseRoughness=slate?.82:sand?.94:.89;
  roughnessFactor=clamp(mix(baseRoughness+aged*.04,clamp(uFlakeRoughness+(flakes.finish-.5)*.22,.06,1.0),flakeAmount*.85), .06,1.0);
  roughnessFactor=min(1.0,sqrt(roughnessFactor*roughnessFactor+(1.0-grainVisibility)*uGrain*.03));
}
`;
export function createRockMaterial(settings={}) {
  const material=new THREE.MeshPhysicalMaterial({vertexColors:true,roughness:1,flatShading:true,metalness:0,clearcoat:1,clearcoatRoughness:.2,ior:1.5,specularIntensity:1});
  const config=surfaceSettings(settings);
  const seed=(settings.seed??28491)>>>0;
  const uniforms={
    uSurfaceEnabled:{value:Number(config.surfaceEnabled)},uOxidation:{value:config.oxidation/100},
    uLayerDepth:{value:config.layerDepth/100},uGrainHeight:{value:config.grainHeight/100},uWeathering:{value:config.weathering/100},uGrain:{value:config.grain/100},uGrainSize:{value:config.grainSize},
    uMineralFlakes:{value:config.mineralFlakes/100},uPeeling:{value:config.peeling/100},uCrystals:{value:config.crystals/100},
    uRockFamily:{value:familyIndex[settings.geology]??0},
    uSurfaceSeed:{value:new THREE.Vector3(seed%137,(seed>>>8)%179,(seed>>>16)%113)}
  };
  uniforms.uFlakeMetallic={value:config.flakeMetallic/100};
  uniforms.uFlakeRoughness={value:config.flakeRoughness/100};
  uniforms.uFlakeSpecular={value:config.flakeSpecular/100};
  uniforms.uFlakeClearcoat={value:config.flakeClearcoat/100};
  uniforms.uFlakeCoatRoughness={value:config.flakeCoatRoughness/100};
  uniforms.uFlakeColor={value:config.flakeColor/100};
  uniforms.uFlakeRelief={value:config.flakeRelief/100};
  uniforms.uFlakeIOR={value:config.flakeIOR};
  material.userData.surfaceUniforms=uniforms;
  material.userData.surfaceSettings=config;
  material.customProgramCacheKey=()=> 'frontier-mapless-weathering-v7';
  material.onBeforeCompile=shader=>{
    Object.assign(shader.uniforms,uniforms);
    shader.vertexShader=shader.vertexShader.replace('#include <common>','#include <common>\nvarying vec3 vRockWorld;');
    shader.vertexShader=shader.vertexShader.replace('#include <project_vertex>',`#include <project_vertex>
      vec4 rockWorld=vec4(transformed,1.0);
      #ifdef USE_INSTANCING
        rockWorld=instanceMatrix*rockWorld;
      #endif
      vRockWorld=(modelMatrix*rockWorld).xyz;`);
    shader.fragmentShader=shader.fragmentShader.replace('#include <common>',`#include <common>\n${fieldGLSL}`);
    shader.fragmentShader=shader.fragmentShader.replace('#include <normal_fragment_maps>',`#include <normal_fragment_maps>\n${surfaceGLSL}`);
    // Keep Three's energy-conserving physical BRDF; substitute only the
    // per-cell parameters, leaving the surrounding rock uncoated/dielectric.
    let physical=THREE.ShaderChunk.lights_physical_fragment;
    physical=physical.replace('material.ior = ior;', 'material.ior = mix(1.5,uFlakeIOR,rockFlakeMask);');
    physical=physical.replace('float specularIntensityFactor = specularIntensity;', 'float specularIntensityFactor = mix(1.0,uFlakeSpecular,rockFlakeMask);');
    physical=physical.replace('material.clearcoat = clearcoat;', 'material.clearcoat = uFlakeClearcoat*rockFlakeMask;');
    physical=physical.replace('material.clearcoatRoughness = clearcoatRoughness;', 'material.clearcoatRoughness = clamp(uFlakeCoatRoughness+(rockFlakeFinish-.5)*.12,.04,1.0);');
    shader.fragmentShader=shader.fragmentShader.replace('#include <lights_physical_fragment>',physical);
    shader.fragmentShader=shader.fragmentShader.replace('#include <clearcoat_normal_fragment_maps>', '#include <clearcoat_normal_fragment_maps>\n#ifdef USE_CLEARCOAT\n clearcoatNormal=normalize(mix(clearcoatNormal,normal,rockFlakeMask));\n#endif');
  };
  return material;
}
export function updateRockSurface(group,values){
  const config=surfaceSettings(values),seen=new Set();
  group.traverse(o=>{
    if(!o.material||seen.has(o.material))return;seen.add(o.material);
    const uniforms=o.material.userData.surfaceUniforms;if(!uniforms)return;
    for(const key of ['oxidation','weathering','grain','layerDepth','grainHeight','mineralFlakes','flakeMetallic','flakeRoughness','flakeSpecular','flakeClearcoat','flakeCoatRoughness','flakeColor','flakeRelief','peeling','crystals'])uniforms['u'+key[0].toUpperCase()+key.slice(1)].value=config[key]/100;
    uniforms.uFlakeIOR.value=config.flakeIOR;
    uniforms.uGrainSize.value=config.grainSize;
    uniforms.uSurfaceEnabled.value=Number(config.surfaceEnabled);
    o.material.userData.surfaceSettings={...config};
  });
  return config;
}
