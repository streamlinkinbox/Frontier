#version 450
// Screen-space liquid reconstruction: bilateral front depth, thickness,
// reconstructed normal, refraction, Beer-Lambert absorption and Fresnel.
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Particles{vec4 particles[];};
layout(std430,binding=1) buffer Pixels{uint pixels[];};
layout(std430,binding=2) readonly buffer Depths{uint depths[];};
layout(std140,binding=3) uniform Params{uvec4 counts;vec4 scene;vec4 colourPaused;vec4 optical;vec4 absorptionMaterial;}p;
layout(std430,binding=4) readonly buffer Thickness{uint thickness[];};
uint bgra(vec3 c){c=pow(clamp(c,0.0,1.0),vec3(1.0/2.2));uvec3 q=uvec3(c*255.0+0.5);return 0xff000000u|(q.r<<16)|(q.g<<8)|q.b;}
vec3 background(vec2 pixel){float v=pixel.y/float(p.counts.z);return mix(vec3(.055,.075,.105),vec3(.22,.25,.27),v);}
float rawDepth(ivec2 q){q=clamp(q,ivec2(0),ivec2(p.counts.yz)-1);uint d=depths[uint(q.y)*p.counts.y+uint(q.x)];return d==0xffffffffu?1e20:float(d)*1e-6;}
float filteredDepth(ivec2 q){float centre=rawDepth(q),sum=0.0,weight=0.0;for(int y=-4;y<=4;y++)for(int x=-4;x<=4;x++){float d=rawDepth(q+ivec2(x,y));if(d>1e10)continue;float spatial=exp(-float(x*x+y*y)*.12);float range=centre>1e10?1.0:exp(-abs(d-centre));sum+=d*spatial*range;weight+=spatial*range;}return weight>0.0?sum/weight:1e20;}
float filteredThickness(ivec2 q){float sum=0.0,weight=0.0;for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){ivec2 s=clamp(q+ivec2(x,y),ivec2(0),ivec2(p.counts.yz)-1);float w=exp(-float(x*x+y*y)*.25);sum+=float(thickness[uint(s.y)*p.counts.y+uint(s.x)])*1e-6*w;weight+=w;}return sum/weight;}
vec2 projectPoint(vec3 point){vec3 camera=vec3(4.4,3.0,5.2),f=normalize(vec3(0,.9,0)-camera),right=normalize(cross(f,vec3(0,1,0))),up=normalize(cross(right,f)),rel=point-camera;float focal=(float(p.counts.z)*.5)/tan(radians(26.0)),z=dot(rel,f);return vec2(float(p.counts.y)*.5+dot(rel,right)*focal/z,float(p.counts.z)*.5-dot(rel,up)*focal/z);}
float segmentDistance(vec2 point,vec2 a,vec2 b){vec2 ab=b-a;float t=clamp(dot(point-a,ab)/dot(ab,ab),0.0,1.0);return length(point-(a+t*ab));}
void main(){
    ivec2 q=ivec2(gl_GlobalInvocationID.xy);if(q.x>=int(p.counts.y)||q.y>=int(p.counts.z))return;uint at=uint(q.y)*p.counts.y+uint(q.x);
    float depth=filteredDepth(q),thick=filteredThickness(q);if(thick<.040)depth=1e20;float fluidDepth=depth;
    if(depth<1e10){
        float dl=filteredDepth(q-ivec2(3,0)),dr=filteredDepth(q+ivec2(3,0)),dt=filteredDepth(q-ivec2(0,3)),db=filteredDepth(q+ivec2(0,3));dl=dl>1e10?depth:dl;dr=dr>1e10?depth:dr;dt=dt>1e10?depth:dt;db=db>1e10?depth:db;float dx=dr-dl,dy=db-dt;
        vec3 normal=normalize(vec3(-dx*3.0,dy*3.0,1.0));
        vec3 light=normalize(vec3(-.4,.8,.3)),halfVector=normalize(light+vec3(0,0,1));float diffuse=max(0.0,dot(normal,light));
        float f0=pow((p.optical.z-1.0)/(p.optical.z+1.0),2.0);float fresnel=f0+(1.0-f0)*pow(1.0-normal.z,5.0);
        float specular=pow(max(0.0,dot(normal,halfVector)),110.0*(1.0-p.optical.y)+14.0)*(1.0-p.optical.y*.55);
        vec2 refractedPixel=vec2(q)+normal.xy*thick*38.0;vec3 behind=background(refractedPixel);
        vec3 transmitted=behind*exp(-p.absorptionMaterial.rgb*thick*2.8)+p.colourPaused.rgb*.12;vec3 body=p.colourPaused.rgb*(.18+.65*diffuse);
        vec3 colour=mix(transmitted,body,p.optical.x)+vec3(specular)+fresnel*vec3(.38,.42,.50);pixels[at]=bgra(colour);
    }
    // Opaque striped source-scene sphere, depth-tested against reconstructed liquid.
    vec3 camera=vec3(4.4,3.0,5.2),target=vec3(0,.9,0),centre=vec3(.65,.65,0);vec3 f=normalize(target-camera),right=normalize(cross(f,vec3(0,1,0))),up=normalize(cross(right,f));vec3 rel=centre-camera;float z=dot(rel,f),focal=(float(p.counts.z)*.5)/tan(radians(26.0));vec2 screen=vec2(float(p.counts.y)*.5+dot(rel,right)*focal/z,float(p.counts.z)*.5-dot(rel,up)*focal/z);float radius=focal*.36/z;vec2 n=(vec2(q)-screen)/radius;float d2=dot(n,n);
    if(p.counts.w!=0u&&d2<=1.0){float nz=sqrt(1.0-d2),sphereDepth=z-.36*nz;if(sphereDepth<fluidDepth){vec3 normal=vec3(n.x,-n.y,nz);float lit=.18+.75*max(0.0,dot(normal,normalize(vec3(-.4,.8,.3))));vec3 c=vec3(.16,.18,.20)*lit;float stripe=mod(abs(n.x-n.y)*12.0,2.0)<1.0?1.0:.3;c=mix(c,vec3(1.0,.24,.025),stripe*.35);pixels[at]=bgra(c);}}
    // Always-visible wireframe of the solver's exact finite collision domain.
    vec3 corners[8]=vec3[8](vec3(-1.95,.19,-1.25),vec3(1.95,.19,-1.25),vec3(-1.95,3.70,-1.25),vec3(1.95,3.70,-1.25),vec3(-1.95,.19,1.25),vec3(1.95,.19,1.25),vec3(-1.95,3.70,1.25),vec3(1.95,3.70,1.25));
    int edges[24]=int[24](0,1,2,3,4,5,6,7,0,2,1,3,4,6,5,7,0,4,1,5,2,6,3,7);float edgeDistance=1e10;for(int i=0;i<24;i+=2)edgeDistance=min(edgeDistance,segmentDistance(vec2(q)+.5,projectPoint(corners[edges[i]]),projectPoint(corners[edges[i+1]])));if(edgeDistance<1.6)pixels[at]=bgra(vec3(1.0,.24,.025));
}
