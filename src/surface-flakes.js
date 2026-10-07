// World-space cellular mineral laminae. No sampled maps or screen-space random
// glints: every plate keeps its own shape, tint and tilted normal as the view moves.
export const mineralFlakesGLSL=/* glsl */`
struct RockFlake {
  float coverage;
  vec2 slope;
  float tint;
  float edge;
  vec3 color;
  float relief;
  float finish;
};
RockFlake rockFlakeLayer(vec2 p, float footprint, ivec3 seed) {
  RockFlake result=RockFlake(0.0,vec2(0.0),0.5,0.0,vec3(.5),0.0,.5);
  ivec2 cell=ivec2(floor(p));vec2 f=fract(p);
  float front=-1.0;
  for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){
    ivec3 id=ivec3(cell+ivec2(x,y),0)+seed;
    float priority=rockHash(id);if(priority<.24)continue;
    vec2 center=vec2(x,y)+vec2(rockHash(id+ivec3(7,3,11)),rockHash(id+ivec3(13,9,2)));
    float angle=rockHash(id+ivec3(2,17,5))*6.2831853;
    mat2 rotation=mat2(cos(angle),sin(angle),-sin(angle),cos(angle));
    vec2 local=rotation*(f-center);
    vec2 axes=vec2(.8+rockHash(id+ivec3(31,2,7))*.65, .75+priority*.8);
    vec2 d=abs(local)*axes;
    // Six irregular bevel-like edges, with flat tilted interiors rather than
    // rounded pebble bumps. Overlapping cells choose the uppermost plate.
    float bevel=dot(d,vec2(.60+priority*.25,.55+rockHash(id+ivec3(17,3,9))*.35));
    float cut=dot(local,normalize(vec2(priority-.35,.65)))*1.18;
    float distance=max(max(d.x,d.y),max(bevel,cut));
    float radius=.27+rockHash(id+ivec3(3,29,19))*.38;
    float aa=max(.012,footprint*.85);
    float coverage=1.0-smoothstep(radius-aa,radius+aa,distance);
    if(coverage>.001&&priority>front){
      front=priority;
      vec2 tilt=vec2(rockHash(id+ivec3(23,7,3)),rockHash(id+ivec3(5,41,7)))-.5;
      result.coverage=coverage;
      result.slope=transpose(rotation)*tilt*1.35;
      result.tint=rockHash(id+ivec3(43,11,7));
      // Independent RGB, finish and relief hashes: colour is not tied to height.
      result.color=vec3(rockHash(id+ivec3(61,3,17)),rockHash(id+ivec3(7,67,23)),rockHash(id+ivec3(19,11,73)));
      result.relief=step(1.0-uFlakeRelief,rockHash(id+ivec3(79,13,31)))*step(.001,uFlakeRelief);
      result.finish=rockHash(id+ivec3(83,17,37));


      result.edge=(1.0-smoothstep(aa,aa+.05,abs(distance-radius)))*coverage;
      result.slope=(result.slope+transpose(rotation)*(sign(local)*axes)*result.edge*.35)*result.relief;
    }
  }
  return result;
}
RockFlake rockFlakeStack(vec2 p,float footprint,ivec3 seed){
  RockFlake result=RockFlake(0.0,vec2(0.0),.5,0.0,vec3(.5),0.0,.5);
  // Independently seeded basal chips, middle laminae and fine mineral flecks.
  // Foreground coverage occludes older plates, not an additive noise sum.
  for(int layer=0;layer<3;layer++){
    float scale=layer==0?.60:layer==1?1.65:4.8;
    float visibility=rockFilter(scale,footprint);
    if(visibility<.001)continue;
    RockFlake flake=rockFlakeLayer(p*scale,footprint*scale,seed+ivec3(layer*71,layer*43,layer*113));
    float alpha=flake.coverage*visibility*(layer==0?.85:layer==1?.75:.52);
    result.coverage=mix(result.coverage,1.0,alpha);
    result.slope=mix(result.slope,flake.slope,alpha);
    result.tint=mix(result.tint,flake.tint,alpha);
    result.edge=mix(result.edge,flake.edge,alpha);
    result.color=mix(result.color,flake.color,alpha);
    result.relief=mix(result.relief,flake.relief,alpha);
    result.finish=mix(result.finish,flake.finish,alpha);
  }
  return result;
}
struct RockMineral {
  float coverage;
  vec3 slope;
  float tint;
  float edge;
  vec3 color;
  float relief;
  float finish;
};
RockMineral rockMineralLayers(vec3 p,vec3 n,float frequency,float footprint,vec3 seed){
  RockMineral result=RockMineral(0.0,vec3(0.0),0.0,0.0,vec3(0.0),0.0,0.0);
  vec3 weights=pow(abs(n),vec3(6.0));weights/=max(dot(weights,vec3(1.0)),.0001);
  // Triplanar world projection: no UV seams, independent plane seeds, and no
  // dependence on the camera. Unresolved layers fade before evaluation.
  for(int axis=0;axis<3;axis++){
    float weight=weights[axis];if(weight<.015)continue;
    vec2 projected=axis==0?p.yz:axis==1?p.xz:p.xy;
    RockFlake flake=rockFlakeStack(projected*frequency,footprint*frequency,ivec3(seed)+ivec3(axis*137,axis*59,axis*23));
    vec3 slope=axis==0?vec3(0.0,flake.slope):axis==1?vec3(flake.slope.x,0.0,flake.slope.y):vec3(flake.slope,0.0);
    result.coverage+=flake.coverage*weight;
    result.slope+=slope*weight;
    result.tint+=flake.tint*weight;
    result.edge+=flake.edge*weight;
    result.color+=flake.color*weight;
    result.relief+=flake.relief*weight;
    result.finish+=flake.finish*weight;
  }
  return result;
}
`;
