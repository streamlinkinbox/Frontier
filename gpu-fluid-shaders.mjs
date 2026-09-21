const camera=`
struct Camera {view:mat4x4<f32>,projection:mat4x4<f32>,world:mat4x4<f32>,inverseProjection:mat4x4<f32>,viewport:vec4<f32>,body:vec4<f32>,rock:vec4<f32>,domain:vec4<f32>,appearance:vec4<f32>}
@group(0) @binding(0) var<uniform> cam:Camera;
`;
export const particleShader=camera+`
struct Particle {p:vec4<f32>,v:vec4<f32>}
@group(0) @binding(1) var<storage,read> particles:array<Particle>;
struct VertexOut {@builtin(position) clip:vec4<f32>,@location(0) uv:vec2<f32>,@location(1) @interpolate(flat) center:vec3<f32>}
@vertex fn vertex(@builtin(vertex_index) vertex:u32,@builtin(instance_index) instance:u32)->VertexOut{
  let corners=array<vec2<f32>,6>(vec2<f32>(-1.,-1.),vec2<f32>(1.,-1.),vec2<f32>(-1.,1.),vec2<f32>(-1.,1.),vec2<f32>(1.,-1.),vec2<f32>(1.,1.));
  let uv=corners[vertex];let center=(cam.view*vec4<f32>(particles[instance].p.xyz,1.)).xyz;
  let pos=center+vec3<f32>(uv*cam.domain.w,0.);
  return VertexOut(cam.projection*vec4<f32>(pos,1.),uv,center);
}
struct FragmentOut {@location(0) distance:f32,@builtin(frag_depth) depth:f32}
@fragment fn fragment(in:VertexOut)->FragmentOut{
  let r2=dot(in.uv,in.uv);if(r2>=1.){discard;}
  let z=in.center.z+sqrt(1.-r2)*cam.domain.w;
  let projected=cam.projection*vec4<f32>(0.,0.,z,1.);
  return FragmentOut(-z,projected.z/projected.w);
}`;
const fullScreen=`
@vertex fn vertex(@builtin(vertex_index) index:u32)->@builtin(position) vec4<f32>{
  let positions=array<vec2<f32>,3>(vec2<f32>(-1.,-1.),vec2<f32>(3.,-1.),vec2<f32>(-1.,3.));
  return vec4<f32>(positions[index],0.,1.);
}`;
export const blurShader=camera+fullScreen+`
@group(0) @binding(1) var depth:texture_2d<f32>;
fn smoothDepth(position:vec4<f32>,axis:vec2<i32>)->f32{
  let xy=vec2<i32>(position.xy);let center=textureLoad(depth,xy,0).r;if(center==0.){return 0.;}
  let dimensions=vec2<i32>(textureDimensions(depth));var sum=0.;var weight=0.;
  for(var i=-4;i<=4;i++){
    let point=clamp(xy+axis*i,vec2<i32>(0),dimensions-1);let sample=textureLoad(depth,point,0).r;if(sample==0.){continue;}
    let delta=(sample-center)/max(.035,cam.domain.w*1.8);
    let w=exp(-f32(i*i)/8.-delta*delta*2.);sum+=sample*w;weight+=w;
  }
  return sum/max(weight,.001);
}
@fragment fn horizontal(@builtin(position) position:vec4<f32>)->@location(0) f32{return smoothDepth(position,vec2<i32>(1,0));}
@fragment fn vertical(@builtin(position) position:vec4<f32>)->@location(0) f32{return smoothDepth(position,vec2<i32>(0,1));}
`;
export const surfaceShader=camera+fullScreen+`
@group(0) @binding(1) var depth:texture_2d<f32>;
@group(0) @binding(2) var sceneCache:texture_2d<f32>;
struct Hit {distance:f32,normal:vec3<f32>,color:vec3<f32>}
fn sphere(origin:vec3<f32>,direction:vec3<f32>,center:vec3<f32>,radius:f32,color:vec3<f32>,previous:Hit)->Hit{
  let offset=origin-center;let b=dot(offset,direction);let c=dot(offset,offset)-radius*radius;let d=b*b-c;
  if(d<0.){return previous;}var t=-b-sqrt(d);if(t<.001){t=-b+sqrt(d);}
  if(t>.001&&t<previous.distance){return Hit(t,normalize(origin+direction*t-center),color);}return previous;
}
// Test the four wall slabs together; derive a normal only for the nearest hit.
// Keep the original box ordering and strict '<' tie rule at wall corners.
fn walls(origin:vec3<f32>,direction:vec3<f32>,previous:Hit)->Hit{
  let wallHeight=cam.domain.y*(1.28/3.);let halfHeight=wallHeight*.5;
  let cx=vec4<f32>(cam.domain.x+.09,-cam.domain.x-.09,0.,0.);
  let cz=vec4<f32>(0.,0.,cam.domain.z+.09,-cam.domain.z-.09);
  let hx=vec4<f32>(.09,.09,cam.domain.x,cam.domain.x);
  let hz=vec4<f32>(cam.domain.z+.18,cam.domain.z+.18,.09,.09);
  let inv=1./(direction+vec3<f32>(.0000001));
  let ax=(cx-hx-origin.x)*inv.x;let bx=(cx+hx-origin.x)*inv.x;
  let az=(cz-hz-origin.z)*inv.z;let bz=(cz+hz-origin.z)*inv.z;
  let ay=(0.-origin.y)*inv.y;let by=(wallHeight-origin.y)*inv.y;
  let near=max(max(min(ax,bx),vec4<f32>(min(ay,by))),min(az,bz));
  let far=min(min(max(ax,bx),vec4<f32>(max(ay,by))),max(az,bz));
  let valid=(near>vec4<f32>(0.)) & (near<=far) & (near<vec4<f32>(previous.distance));
  let distances=select(vec4<f32>(previous.distance),near,valid);
  let distance=min(min(distances.x,distances.y),min(distances.z,distances.w));
  if(distance>=previous.distance){return previous;}
  var i=0u;
  if(distances.x!=distance){i=1u;if(distances.y!=distance){i=2u;if(distances.z!=distance){i=3u;}}}
  let point=(origin+direction*distance-vec3<f32>(cx[i],halfHeight,cz[i]))/vec3<f32>(hx[i],halfHeight,hz[i]);
  let ap=abs(point);var normal=vec3<f32>(0.,0.,sign(point.z));
  if(ap.x>ap.y&&ap.x>ap.z){normal=vec3<f32>(sign(point.x),0.,0.);}else if(ap.y>ap.z){normal=vec3<f32>(0.,sign(point.y),0.);}
  return Hit(distance,normal,vec3<f32>(.12,.19,.23));
}
fn scene(origin:vec3<f32>,direction:vec3<f32>)->Hit{
  var hit=Hit(cam.appearance.x,vec3<f32>(0.,1.,0.),vec3<f32>(.014,.026,.036));
  let t=-origin.y/direction.y;
  if(t>0.){let point=origin+direction*t;let inside=abs(point.x)<cam.domain.x&&abs(point.z)<cam.domain.z;
    var color=vec3<f32>(.023,.038,.05);
    if(inside){let check=(i32(floor(point.x*4.))+i32(floor(point.z*4.)))&1;color=mix(vec3<f32>(.18,.25,.24),vec3<f32>(.22,.3,.29),f32(check));}
    hit=Hit(t,vec3<f32>(0.,1.,0.),color);
  }
  hit=walls(origin,direction,hit);
  hit=sphere(origin,direction,cam.rock.xyz,cam.rock.w,vec3<f32>(.16,.21,.22),hit);
  let boundOffset=origin-cam.body.xyz;let boundB=dot(boundOffset,direction);
  let boundD=boundB*boundB-dot(boundOffset,boundOffset)+.65*.65;
  if(boundD>=0.&&-boundB+sqrt(max(0.,boundD))>0.&&-boundB-sqrt(max(0.,boundD))<hit.distance){
  hit=sphere(origin,direction,cam.body.xyz,cam.body.w,vec3<f32>(.85,.57,.045),hit);
  let head=cam.body.xyz+vec3<f32>(0.,.34,.17);
  hit=sphere(origin,direction,head,.24,vec3<f32>(.95,.67,.055),hit);
  hit=sphere(origin,direction,head+vec3<f32>(0.,-.045,.23),.105,vec3<f32>(.95,.23,.028),hit);
  hit=sphere(origin,direction,head+vec3<f32>(.125,.07,.182),.025,vec3<f32>(.005),hit);
  hit=sphere(origin,direction,head+vec3<f32>(-.125,.07,.182),.025,vec3<f32>(.005),hit);
  }
  return hit;
}
fn lighting(hit:Hit)->vec3<f32>{return hit.color*(.46+.64*max(0.,dot(hit.normal,normalize(vec3<f32>(-3.,7.,4.)))));}
fn viewPoint(pixel:vec2<i32>,z:f32)->vec3<f32>{
  let uv=(vec2<f32>(pixel)+.5)/cam.viewport.xy;let projected=cam.inverseProjection*vec4<f32>(uv*vec2<f32>(2.,-2.)+vec2<f32>(-1.,1.),1.,1.);
  return projected.xyz*(-z/projected.z);
}
fn depthPoint(pixel:vec2<i32>,fallback:f32)->vec3<f32>{
  let xy=clamp(pixel,vec2<i32>(0),vec2<i32>(cam.viewport.xy)-1);let d=textureLoad(depth,xy,0).r;return viewPoint(xy,select(fallback,d,d>0.));
}
@fragment fn background(@builtin(position) position:vec4<f32>)->@location(0) vec4<f32>{
  let origin=cam.world[3].xyz;let direction=normalize((cam.world*vec4<f32>(viewPoint(vec2<i32>(position.xy),1.),0.)).xyz);
  let hit=scene(origin,direction);return vec4<f32>(lighting(hit),hit.distance);
}
@fragment fn fragment(@builtin(position) position:vec4<f32>)->@location(0) vec4<f32>{
  let xy=vec2<i32>(position.xy);let origin=cam.world[3].xyz;
  let direction=normalize((cam.world*vec4<f32>(viewPoint(xy,1.),0.)).xyz);
  let background=textureLoad(sceneCache,xy,0);var color=background.rgb;
  let z=textureLoad(depth,xy,0).r;
  if(z>0.){
    let vp=viewPoint(xy,z);let wp=(cam.world*vec4<f32>(vp,1.)).xyz;
    if(length(wp-origin)<background.a){
      let left=vp-depthPoint(xy-vec2<i32>(1,0),z);let right=depthPoint(xy+vec2<i32>(1,0),z)-vp;
      let up=depthPoint(xy-vec2<i32>(0,1),z)-vp;let down=vp-depthPoint(xy+vec2<i32>(0,1),z);
      let dx=select(left,right,abs(right.z)<abs(left.z));let dy=select(up,down,abs(down.z)<abs(up.z));
      var n=normalize(cross(dx,dy)+vec3<f32>(0.,0.,.0000001));if(n.z<0.){n=-n;}
      n=normalize((cam.world*vec4<f32>(n,0.)).xyz);
      let fresnel=.0204+.9796*pow(1.-max(0.,dot(n,-direction)),5.);
      let reflected=reflect(direction,n);
      let sky=mix(vec3<f32>(.065,.13,.17),vec3<f32>(.58,.77,.86),smoothstep(-.2,1.,reflected.y));
      let refracted=refract(direction,n,1./1.333);let behind=scene(wp+refracted*.015,refracted);
      let path=min(3.,behind.distance);let transmission=exp(-vec3<f32>(.65,.14,.09)*path);
      let water=lighting(behind)*transmission+vec3<f32>(.025,.23,.29)*(1.-transmission);
      let spec=pow(max(0.,dot(reflect(-normalize(vec3<f32>(-3.,7.,4.)),n),-direction)),100.);
      color=mix(water,sky,fresnel)+spec*.65;
      if(cam.appearance.w>.5){color=vec3<f32>(.09,.4,.51)*(.4+.6*max(0.,dot(n,normalize(vec3<f32>(-3.,7.,4.)))))+spec*.6;}
    }
  }
  color=pow(max(vec3<f32>(0.),color/(1.+color)*1.2),vec3<f32>(1./2.2));
  return vec4<f32>(color,1.);
}`;

// A moving camera/collider bypasses the cache rather than paying to rebuild it every frame.
export const directSurfaceShader=surfaceShader.replace('let background=textureLoad(sceneCache,xy,0);', 'let hit=scene(origin,direction);let background=vec4<f32>(lighting(hit),hit.distance);');
