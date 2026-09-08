import common from "../shaders/common.wgsl?raw";
import materials from "../shaders/materials.wgsl?raw";
import render from "../shaders/render.wgsl?raw";
import kernels from "./kernels.wgsl?raw";
import { glUniformHeader, toGLSL } from "../shaders";

export type FoamPass =
  | "geometry"
  | "flow"
  | "density"
  | "particles"
  | "splat"
  | "air"
  | "bubble"
  | "compose"
  | "copy";
export const passBindings: Record<FoamPass, number[]> = {
  geometry: [0, 1, 2, 16],
  flow: [0, 2, 11, 12, 16],
  density: [0, 2, 10, 11, 12, 16],
  particles: [0, 1, 2, 11, 12, 15, 16, 17],
  splat: [0, 15, 16, 17],
  air: [0, 2, 13, 15, 16, 17, 19],
  bubble: [0, 2, 13, 15, 16, 17, 19],
  compose: [0, 2, 16, 18, 19, 20, 21, 22],
  copy: [0, 2, 10, 16],
};
export const textureNames: Record<number, string> = {
  1: "field",
  10: "foamDensity",
  11: "foamMotion",
  12: "foamGeometry",
  13: "foamLighting",
  14: "foamAtlas",
  15: "foamPositions",
  17: "foamVelocities",
  18: "foamScene",
  19: "foamHits",
  20: "foamTransmitted",
  21: "foamAir",
  22: "foamBubble",
};
const water = render.slice(0, render.indexOf("fn waterSurfaceHit"));
const extra = `
@group(0) @binding(15) var foamPositions:texture_2d<f32>;
@group(0) @binding(16) var<uniform> f:FoamParams;
@group(0) @binding(17) var foamVelocities:texture_2d<f32>;
@group(0) @binding(18) var foamScene:texture_2d<f32>;
@group(0) @binding(19) var foamHits:texture_2d<f32>;
@group(0) @binding(20) var foamTransmitted:texture_2d<f32>;
@group(0) @binding(21) var foamAir:texture_2d<f32>;
@group(0) @binding(22) var foamBubble:texture_2d<f32>;
`;
const display = `
fn foamHitAt(uv:vec2f) -> vec4f {
  let pixel:vec2i=clamp(vec2i(floor(uv*u.viewport.xy)),vec2i(0),vec2i(u.viewport.xy)-vec2i(1));
  return textureLoad(foamHits,pixel,0);
}

fn foamRadius(id:i32,kind:f32) -> f32 {
  let random:f32=hash3(vec3i(id,691,83));
  if(kind<1.5){return max(96./f.grid.y*1.2,(.17+random*.18)*u.foam.z);}
  if(kind<2.5){return .025+random*.045;}
  return .035+random*.065;
}
fn foamParticleClip(id:i32,vertex:i32,surface:bool) -> vec4f {
  let particle:FoamPair=readFoamParticle(id);let p:vec3f=particle.a.xyz;let kind:f32=particle.b.w;
  if(particle.a.w<=0.||kind<.5||(surface&&kind>1.5)){return vec4f(2.,2.,0.,1.);}
  let corner:vec2f=foamCorner(vertex);let radius:f32=foamRadius(id,kind);
  if(surface){return vec4f(foamUV(p.xz+corner*radius)*2.-vec2f(1.),0.,1.);}
  let relative:vec3f=p-u.eye.xyz;let z:f32=dot(relative,u.forward.xyz);
  if(z<.15){return vec4f(2.,2.,0.,1.);}
  var displacement:vec2f=corner*radius;
  if(kind>1.5&&kind<2.5){
    let vel:vec2f=vec2f(dot(particle.b.xyz,u.right.xyz),dot(particle.b.xyz,u.up.xyz));
    let direction:vec2f=vel/max(length(vel),.001);
    displacement=vec2f(-direction.y,direction.x)*corner.x*radius+direction*corner.y*(radius+min(.1,length(vel)/90.));
  }
  let xy:vec2f=vec2f(dot(relative,u.right.xyz),dot(relative,u.up.xyz))+displacement;
  return vec4f(xy/vec2f(z*u.right.w*u.forward.w,z*u.right.w),0.,1.);
}
fn foamParticleData(id:i32) -> vec4f {
  let p:FoamPair=readFoamParticle(id);
  let age:f32=clamp(1.-p.a.w/(u.foam.w*1.35),0.,1.);
  return vec4f(age,length(p.a.xyz-u.eye.xyz),p.b.w,foamRadius(id,p.b.w));
}
fn foamSplat(corner:vec2f,data:vec4f) -> vec4f {
  let d:f32=dot(corner,corner);let alive:f32=select(0.,1.,data.z>.5&&data.z<1.5&&d<1.);
  let density:f32=exp(-d*4.)*.07*(32768./(f.grid.z*f.grid.w))/(.77*data.w*data.w)*alive*(1.-smoothstep(.85,1.,data.x));
  return vec4f(density,density*data.x*u.foam.w,0.,0.);
}
fn foamOptical(pixel:vec2f,corner:vec2f,world:vec3f,data:vec4f,kind:f32) -> vec4f {
  let d:f32=dot(corner,corner);
  if(d>1.||abs(data.z-kind)>.1){return vec4f(0.);}
  let uv:vec2f=pixel/ceil(u.viewport.xy*.5);
  let hits:vec4f=foamHitAt(uv);
  let clearance:f32=hits.x-data.y;
  if(clearance<=0.){return vec4f(0.);}
  if(kind>2.5&&hits.z<.5&&(hits.y>=data.y||hits.w<=0.)){return vec4f(0.);}
  if(kind<2.5&&hits.z<.5&&hits.y<data.y){return vec4f(0.);}
  var waterLength:f32=0.;
  if(kind>2.5){waterLength=select(max(0.,data.y-hits.y),data.y,hits.z>.5);}
  else if(hits.z>.5){waterLength=min(hits.y,data.y);}
  let turbidity:f32=clamp((1.-u.flags.x)*.8,0.,1.);
  let coeff:vec3f=mix(vec3f(3.,.75,.4),vec3f(1.8,2.6,4.2),turbidity)/max(u.waterOptics.x,.5);
  let light:vec3f=textureSampleLevel(foamLighting,linearSampler,foamUV(world.xz),0.).rgb;
  let amount:f32=exp(-d*4.)*smoothstep(0.,.15,clearance)*(1.-smoothstep(.8,1.,data.x));
  let tau:f32=amount*select(.75,.45,kind>2.5)*(98304./(f.grid.z*f.grid.w));
  return vec4f(vec3f(.82,.86,.83)*light*exp(-coeff*waterLength)*tau,tau);
}
fn foamComposite(pixel:vec2f) -> vec4f {
  let uv:vec2f=pixel/u.viewport.xy;
  var col:vec3f=textureSampleLevel(foamScene,linearSampler,uv,0.).rgb;
  let center:vec4f=foamHitAt(uv);
  let transmitted:vec4f=textureSampleLevel(foamTransmitted,linearSampler,uv,0.);
  // Depth-aware 4-tap upsample: no foreground foam halo over a rock silhouette.
  let size:vec2f=ceil(u.viewport.xy*.5);let coord:vec2f=uv*size-.5;
  let cell:vec2f=floor(coord);let blend:vec2f=fract(coord);
  var air:vec4f=vec4f(0.);var bubbles:vec4f=vec4f(0.);var sum:f32=0.;
  for(var i:i32=0;i<4;i++){
    let offset:vec2f=vec2f(f32(i%2),f32(i/2));let at:vec2f=(cell+offset+.5)/size;
    let h:vec4f=foamHitAt(at);
    let w:f32=mix(1.-blend.x,blend.x,offset.x)*mix(1.-blend.y,blend.y,offset.y)*exp(-abs(h.x-center.x)*3.);
    air+=textureSampleLevel(foamAir,linearSampler,at,0.)*w;
    bubbles+=textureSampleLevel(foamBubble,linearSampler,at,0.)*w;sum+=w;
  }
  air/=max(sum,.0001);bubbles/=max(sum,.0001);
  let bubbleAlpha:f32=1.-exp(-bubbles.a);
  col+=bubbleAlpha*(bubbles.rgb/max(bubbles.a,.0001)*transmitted.a-transmitted.rgb);
  let alpha:f32=1.-exp(-air.a);
  col=mix(col,air.rgb/max(air.a,.0001),alpha);
  col=vec3f(1.)-exp(-max(col,vec3f(0.))*u.viewport.z*1.30);
  col=vec3f(linearToSRGB(col.r),linearToSRGB(col.g),linearToSRGB(col.b));
  let screen:vec2f=uv*2.-1.;col*=1.-dot(screen*vec2f(.65,1.),screen*vec2f(.65,1.))*.045;
  return vec4f(col,1.);
}
`;
const entries = `
struct FoamOutput { @location(0) a:vec4f, @location(1) b:vec4f, };
struct SplatVertex { @builtin(position) position:vec4f, @location(0) corner:vec2f, @location(1) world:vec3f, @location(2) data:vec4f, };
@vertex fn quadVertex(@builtin(vertex_index) id:u32) -> @builtin(position) vec4f {
  let x=f32((id<<1u)&2u);let y=f32(id&2u);return vec4f(x*2.-1.,y*2.-1.,0.,1.);
}
@vertex fn splatVertex(@builtin(vertex_index) vertex:u32,@builtin(instance_index) id:u32) -> SplatVertex {
  var clip=foamParticleClip(i32(id),i32(vertex),true);clip.y=-clip.y;
  return SplatVertex(clip,foamCorner(i32(vertex)),readFoamParticle(i32(id)).a.xyz,foamParticleData(i32(id)));
}
@vertex fn opticalVertex(@builtin(vertex_index) vertex:u32,@builtin(instance_index) id:u32) -> SplatVertex {
  return SplatVertex(foamParticleClip(i32(id),i32(vertex),false),foamCorner(i32(vertex)),readFoamParticle(i32(id)).a.xyz,foamParticleData(i32(id)));
}
@fragment fn geometryMain(@builtin(position) p:vec4f) -> FoamOutput {let pair=foamMapGeometry(p.xy/f.grid.x);return FoamOutput(pair.a,pair.b);}
@fragment fn flowMain(@builtin(position) p:vec4f) -> @location(0) vec4f {return foamFlowStep(p.xy/f.grid.x);}
@fragment fn densityMain(@builtin(position) p:vec4f) -> @location(0) vec4f {return foamDensityStep(p.xy/f.grid.y);}
@fragment fn particlesMain(@builtin(position) p:vec4f) -> FoamOutput {let pair=foamParticleStep(i32(p.x)+i32(p.y)*i32(f.grid.z));return FoamOutput(pair.a,pair.b);}
@fragment fn splatMain(v:SplatVertex) -> @location(0) vec4f {return foamSplat(v.corner,v.data);}
@fragment fn airMain(v:SplatVertex) -> @location(0) vec4f {return foamOptical(v.position.xy,v.corner,v.world,v.data,2.);}
@fragment fn bubbleMain(v:SplatVertex) -> @location(0) vec4f {return foamOptical(v.position.xy,v.corner,v.world,v.data,3.);}
@fragment fn composeMain(@builtin(position) p:vec4f) -> @location(0) vec4f {return foamComposite(p.xy);}
@fragment fn copyMain(@builtin(position) p:vec4f) -> @location(0) vec4f {return textureSampleLevel(foamDensity,linearSampler,p.xy/f.grid.y,0.);}
`;
export const gpuFoamShader =
  common + extra + materials + water + kernels + display + entries;
const select = `float select(float a,float b,bool s){return s?b:a;} vec2 select(vec2 a,vec2 b,bool s){return s?b:a;} vec3 select(vec3 a,vec3 b,bool s){return s?b:a;} vec4 select(vec4 a,vec4 b,bool s){return s?b:a;}`;
const glBase =
  glUniformHeader +
  `
layout(std140) uniform FoamParamsBlock { vec4 tick;vec4 grid;vec4 focus; } f;
uniform sampler2D foamPositions;uniform sampler2D foamVelocities;
uniform sampler2D foamScene;uniform sampler2D foamHits;uniform sampler2D foamTransmitted;uniform sampler2D foamAir;uniform sampler2D foamBubble;
` +
  select +
  toGLSL(
    common.slice(common.indexOf("const WORLD_MIN")) +
      materials +
      water +
      kernels.replace(/struct FoamParams[^}]+};/, "") +
      display,
  );
export function glFoamShaders(kind: FoamPass): {
  vertex: string;
  fragment: string;
} {
  const splat = ["splat", "air", "bubble"].includes(kind);
  const vertex = splat
    ? glBase +
      `
out vec2 vCorner;out vec3 vWorld;out vec4 vData;
void main(){gl_Position=foamParticleClip(gl_InstanceID,gl_VertexID,${kind === "splat" ? "true" : "false"});vCorner=foamCorner(gl_VertexID);vWorld=readFoamParticle(gl_InstanceID).a.xyz;vData=foamParticleData(gl_InstanceID);}`
    : `#version 300 es
void main(){float x=float((gl_VertexID<<1)&2);float y=float(gl_VertexID&2);gl_Position=vec4(x*2.-1.,y*2.-1.,0.,1.);}`;
  const code: Record<FoamPass, string> = {
    geometry:
      "FoamPair p=foamMapGeometry(gl_FragCoord.xy/f.grid.x);color=p.a;extraColor=p.b;",
    flow: "color=foamFlowStep(gl_FragCoord.xy/f.grid.x);",
    density: "color=foamDensityStep(gl_FragCoord.xy/f.grid.y);",
    particles:
      "FoamPair p=foamParticleStep(int(gl_FragCoord.x)+int(gl_FragCoord.y)*int(f.grid.z));color=p.a;extraColor=p.b;",
    splat: "color=foamSplat(vCorner,vData);",
    air: "color=foamOptical(gl_FragCoord.xy,vCorner,vWorld,vData,2.);",
    bubble: "color=foamOptical(gl_FragCoord.xy,vCorner,vWorld,vData,3.);",
    compose: "color=foamComposite(gl_FragCoord.xy);",
    copy: "color=textureLod(foamDensity,gl_FragCoord.xy/f.grid.y,0.);",
  };
  return {
    vertex,
    fragment:
      glBase +
      (splat ? "in vec2 vCorner;in vec3 vWorld;in vec4 vData;" : "") +
      "layout(location=0) out vec4 color;" +
      (["geometry", "particles"].includes(kind)
        ? "layout(location=1) out vec4 extraColor;"
        : "") +
      `void main(){${code[kind]}}`,
  };
}
