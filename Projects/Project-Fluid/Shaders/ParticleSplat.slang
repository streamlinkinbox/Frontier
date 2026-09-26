#version 450
// Weighted covariance/PCA ellipsoid projection from the native Yu-Turk
// reconstruction. No spherical particle presentation is used for dense fluid.
layout(local_size_x=64) in;
struct Particle { vec4 centreWeight; vec4 axisA; vec4 axisB; vec4 axisC; };
layout(std430,binding=0) readonly buffer Particles { Particle particles[]; };
layout(std430,binding=1) buffer Pixels { uint pixels[]; };
layout(std430,binding=2) buffer Depths { uint depths[]; };
layout(std140,binding=3) uniform Params { uvec4 counts;vec4 scene;vec4 colourPaused;vec4 optical;vec4 absorptionMaterial; } p;
layout(std430,binding=4) buffer Thickness { uint thickness[]; };
void main(){
    uint id=gl_GlobalInvocationID.x;if(id>=p.counts.x)return;Particle particle=particles[id];vec3 centre=particle.centreWeight.xyz;
    vec3 camera=vec3(4.4,3.0,5.2),f=normalize(vec3(0,.9,0)-camera),right=normalize(cross(f,vec3(0,1,0))),up=normalize(cross(right,f));float focal=(float(p.counts.z)*.5)/tan(radians(26.0));
    vec3 rel=centre-camera;float z=dot(rel,f);if(z<=.05)return;
    vec2 screen=vec2(float(p.counts.y)*.5+dot(rel,right)*focal/z,float(p.counts.z)*.5-dot(rel,up)*focal/z);
    vec2 projected[3];vec3 axes[3]=vec3[3](particle.axisA.xyz,particle.axisB.xyz,particle.axisC.xyz);
    for(int i=0;i<3;i++){vec3 endpoint=rel+axes[i];float ez=dot(endpoint,f);projected[i]=vec2(float(p.counts.y)*.5+dot(endpoint,right)*focal/ez,float(p.counts.z)*.5-dot(endpoint,up)*focal/ez)-screen;}
    float m00=0,m01=0,m11=0,depthRadius2=0;for(int i=0;i<3;i++){m00+=projected[i].x*projected[i].x;m01+=projected[i].x*projected[i].y;m11+=projected[i].y*projected[i].y;float d=dot(axes[i],f);depthRadius2+=d*d;}
    float trace=m00+m11,disc=sqrt(max(0.0,(m00-m11)*(m00-m11)+4*m01*m01)),major=sqrt(max(1.0,(trace+disc)*.5)),minor=sqrt(max(1.0,(trace-disc)*.5)),angle=.5*atan(2*m01,m00-m11);vec2 axis=vec2(cos(angle),sin(angle));int extent=clamp(int(major)+1,1,72);float depthRadius=sqrt(depthRadius2);
    for(int oy=-extent;oy<=extent;oy++)for(int ox=-extent;ox<=extent;ox++){vec2 offset=vec2(ox,oy);float along=dot(offset,axis)/major,across=dot(offset,vec2(-axis.y,axis.x))/minor,d2=along*along+across*across;if(d2>1.0)continue;ivec2 pixel=ivec2(screen)+ivec2(ox,oy);if(any(lessThan(pixel,ivec2(0)))||pixel.x>=int(p.counts.y)||pixel.y>=int(p.counts.z))continue;float nz=sqrt(max(0.0,1.0-d2));uint index=uint(pixel.y)*p.counts.y+uint(pixel.x);atomicMin(depths[index],uint(clamp((z-depthRadius*nz)*1000000.0,0.0,4294967294.0)));atomicAdd(thickness[index],uint(2.0*depthRadius*nz*particle.centreWeight.w*1000000.0));}
}
