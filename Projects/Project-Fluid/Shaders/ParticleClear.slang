#version 450
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Particles{vec4 particles[];};
layout(std430,binding=1) buffer Pixels{uint pixels[];};
layout(std430,binding=2) buffer Depths{uint depths[];};
layout(std430,binding=4) buffer Thickness{uint thickness[];};
layout(std140,binding=3) uniform Params{uvec4 counts;vec4 scene;vec4 colourPaused;vec4 optical;vec4 absorptionMaterial;} p;
uint bgra(vec3 c){c=pow(clamp(c,0.0,1.0),vec3(1.0/2.2));uvec3 q=uvec3(c*255.0+0.5);return 0xff000000u|(q.r<<16)|(q.g<<8)|q.b;}
vec2 project(vec3 world){vec3 camera=vec3(4.4,3.0,5.2),target=vec3(0.0,0.9,0.0);vec3 f=normalize(target-camera),r=normalize(cross(f,vec3(0,1,0))),u=normalize(cross(r,f));vec3 v=world-camera;float focal=(float(p.counts.z)*.5)/tan(radians(26.0));return vec2(float(p.counts.y)*.5+dot(v,r)*focal/dot(v,f),float(p.counts.z)*.5-dot(v,u)*focal/dot(v,f));}
float segmentDistance(vec2 p0,vec2 a,vec2 b){vec2 ab=b-a;float t=clamp(dot(p0-a,ab)/max(dot(ab,ab),1e-5),0.0,1.0);return length(p0-(a+ab*t));}
void main(){uvec2 q=gl_GlobalInvocationID.xy;if(q.x>=p.counts.y||q.y>=p.counts.z)return;uint at=q.y*p.counts.y+q.x;float v=float(q.y)/float(p.counts.z);vec3 c=mix(vec3(.055,.075,.105),vec3(.22,.25,.27),v);vec3 lo=vec3(-p.scene.z,.19,-p.scene.w),hi=vec3(p.scene.z,3.7,p.scene.w);vec3 corners[8]=vec3[8](vec3(lo.x,lo.y,lo.z),vec3(hi.x,lo.y,lo.z),vec3(hi.x,lo.y,hi.z),vec3(lo.x,lo.y,hi.z),vec3(lo.x,hi.y,lo.z),vec3(hi.x,hi.y,lo.z),vec3(hi.x,hi.y,hi.z),vec3(lo.x,hi.y,hi.z));ivec2 edges[12]=ivec2[12](ivec2(0,1),ivec2(1,2),ivec2(2,3),ivec2(3,0),ivec2(4,5),ivec2(5,6),ivec2(6,7),ivec2(7,4),ivec2(0,4),ivec2(1,5),ivec2(2,6),ivec2(3,7));float d=1e9;for(int i=0;i<12;i++)d=min(d,segmentDistance(vec2(q)+.5,project(corners[edges[i].x]),project(corners[edges[i].y])));c=mix(c,vec3(1.0,.32,.05),1.0-smoothstep(1.0,2.2,d));pixels[at]=bgra(c);depths[at]=0xffffffffu;thickness[at]=0u;}
