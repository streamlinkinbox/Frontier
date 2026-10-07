// Reference-inspired scalar stone relief in metres. No sampled maps.
// Four independent relief bands: broad chipped stone, smaller shoulders,
// sandy rind and fine grit. Keep their amplitudes explicit so a cleanup of
// the fracture pattern cannot silently remove the relief budget again.
export const stoneReliefBands=Object.freeze([
  Object.freeze({frequency:3.0,amplitude:.035}),
  Object.freeze({frequency:8.1,amplitude:.012}),
  Object.freeze({frequency:21.87,amplitude:.0042}),
  Object.freeze({frequency:59.049,amplitude:.0014})
]);
export const stoneHeightGLSL=/* glsl */`
float rockSegmentDistance(vec2 p,vec2 a,vec2 b){
  vec2 ab=b-a;
  float t=clamp(dot(p-a,ab)/max(dot(ab,ab),.00001),0.0,1.0);
  return length(p-a-ab*t);
}
float rockBrokenFractures(vec2 p,ivec3 seed,float footprint){
  // Sparse OPEN, piecewise straight fracture paths. Never a field isocontour,
  // closed cell border or sinusoid: no looping / worm-like whorls.
  const float frequency=.85;
  vec2 q=p*frequency,cell=floor(q);float recess=0.0;
  for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){
    ivec3 id=ivec3(ivec2(cell)+ivec2(x,y),0)+seed;
    float activation=rockHash(id);if(activation<.74)continue;
    float angle=rockHash(id+ivec3(7,29,3))*6.2831853;
    vec2 direction=vec2(cos(angle),sin(angle));
    vec2 start=cell+vec2(x,y)+vec2(.2)+vec2(rockHash(id+ivec3(31,3,7)),rockHash(id+ivec3(5,17,43)))*.6-direction*.32;
    // Pixel footprint widens the edge only, never increases groove depth.
    float core=(.0015+rockHash(id+ivec3(11,53,7))*.0018)*frequency;
    float aa=max(.0008,footprint*frequency*.65);
    float chipped=.7+.3*rockField(vec3(p*7.0,0.0)+vec3(seed)).x;
    float broken=smoothstep(.20,.43,rockField(vec3(p*3.3,0.0)+vec3(seed)).x);
    for(int segment=0;segment<4;segment++){
      ivec3 sid=id+ivec3(segment*47,segment*17,segment*71);
      float turn=(rockHash(sid+ivec3(13,5,19))-.5)*.95;
      mat2 bend=mat2(cos(turn),sin(turn),-sin(turn),cos(turn));
      direction=bend*direction;
      vec2 end=start+direction*(.16+rockHash(sid+ivec3(3,61,11))*.12);
      float distance=rockSegmentDistance(q,start,end);
      float profile=1.0-smoothstep(core,core+aa+.003*frequency,distance);

      // Narrow, shallow, locally interrupted grooves, not outlined trenches.

      recess=max(recess,profile*chipped*broken*(segment==0||segment==3?.7:1.0));
      if(segment==2&&activation>.89){
        vec2 branchDirection=vec2(-direction.y,direction.x)*.7+direction*.35;
        vec2 branchEnd=start+branchDirection*.21;
        float branch=1.0-smoothstep(core*.6,core*.6+aa+.002*frequency,rockSegmentDistance(q,start,branchEnd));
        recess=max(recess,branch*broken*.45);
      }
      start=end;
    }
  }
  return recess*rockFilter(12.0,footprint);
}
vec2 rockStoneHeight(vec3 p,vec3 surfaceNormal,vec3 seed,float footprint){
  mat3 rotation=mat3(.36,.48,-.8,-.8,.60,0.0,.48,.64,.60);
  vec3 q=rotation*p;
  float height=0.0;
  // Continuous multi-scale relief, NOT contour strokes. Frequencies and
  // metre-height amplitudes are paired to preserve readable slope at each
  // scale. The previous .0045 m / 1.4 Hz band was nearly flat in lighting.
  for(int octave=0;octave<4;octave++){
    float frequency=octave==0?${stoneReliefBands[0].frequency.toFixed(3)}:octave==1?${stoneReliefBands[1].frequency.toFixed(3)}:octave==2?${stoneReliefBands[2].frequency.toFixed(3)}:${stoneReliefBands[3].frequency.toFixed(3)};
    float amplitude=octave==0?${stoneReliefBands[0].amplitude.toFixed(4)}:octave==1?${stoneReliefBands[1].amplitude.toFixed(4)}:octave==2?${stoneReliefBands[2].amplitude.toFixed(4)}:${stoneReliefBands[3].amplitude.toFixed(4)};
    float visibility=rockFilter(frequency*1.2,footprint);
    vec3 domain=q*vec3(1.0,1.2,.85)*frequency+seed+float(octave)*31.0;
    float field=rockField(domain).x;
    height+=(field-.5)*amplitude*visibility;
  }
  // Only the few open fractures interrupt the continuous matte stone rind.
  vec3 weights=pow(abs(surfaceNormal),vec3(8.0));
  weights/=max(dot(weights,vec3(1.0)),.0001);
  float recess=0.0;
  for(int axis=0;axis<3;axis++){
    float weight=weights[axis];if(weight<.015)continue;
    vec2 plane=axis==0?p.yz:axis==1?p.xz:p.xy;
    recess+=rockBrokenFractures(plane,ivec3(seed)+ivec3(axis*137,axis*59,axis*23),footprint)*weight;
  }
  height-=recess*.0018;
  return vec2(height,recess);
}
// Surface-gradient bump derived from a scalar metre-valued height field.
vec3 rockHeightNormal(vec3 p,vec3 n,float height){
  vec3 dx=dFdx(p),dy=dFdy(p);
  vec3 r1=cross(dy,n),r2=cross(n,dx);
  float determinant=dot(dx,r1);
  vec3 gradient=sign(determinant)*(dFdx(height)*r1+dFdy(height)*r2);
  // Extreme settings must not produce black grazing-facing speckle normals.
  float area=max(abs(determinant),1e-12);
  float slope=length(gradient)/area;
  gradient*=min(1.0,1.05/max(slope,.0001));
  return normalize(area*n-gradient);
}
`;
