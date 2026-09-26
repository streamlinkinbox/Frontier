#version 450
// Three GPU stages share one pipeline. Matches GpuSurfaceData.cpp's CPU mirror.
layout(local_size_x=64) in;
struct Kernel {vec4 centre;vec4 a;vec4 b;vec4 c;};
struct Vertex {vec4 position;vec4 normal;};
layout(std430,binding=0) readonly buffer Kernels {Kernel kernels[];};
layout(std430,binding=1) readonly buffer Offsets {uint offsets[];};
layout(std430,binding=2) readonly buffer Candidates {uint ids[];};
layout(std430,binding=3) buffer Field {float field[];};
layout(std430,binding=4) readonly buffer Lookup {int tri[];};
layout(std430,binding=5) buffer Vertices {Vertex vertices[];};
layout(std430,binding=6) buffer Indices {uint indices[];};
layout(std430,binding=7) buffer Status {uint requested;uint overflow;};
layout(push_constant) uniform Parameters {uvec4 grid;vec4 originSpacing;uvec4 config;vec4 scalar;} p;
const ivec3 corner[8]=ivec3[](ivec3(0,0,0),ivec3(1,0,0),ivec3(1,1,0),ivec3(0,1,0),ivec3(0,0,1),ivec3(1,0,1),ivec3(1,1,1),ivec3(0,1,1));
const ivec2 edge[12]=ivec2[](ivec2(0,1),ivec2(1,2),ivec2(2,3),ivec2(3,0),ivec2(4,5),ivec2(5,6),ivec2(6,7),ivec2(7,4),ivec2(0,4),ivec2(1,5),ivec2(2,6),ivec2(3,7));
uint at(ivec3 q){return (uint(q.z)*p.grid.y+uint(q.y))*p.grid.x+uint(q.x);}
ivec3 coord(uint id,uvec3 size){return ivec3(id%size.x,(id/size.x)%size.y,id/(size.x*size.y));}
vec3 point(ivec3 q){return p.originSpacing.xyz+vec3(q)*p.originSpacing.w;}
float sampleField(ivec3 q){return field[at(clamp(q,ivec3(0),ivec3(p.grid.xyz)-1))];}
vec3 gradient(ivec3 q){return vec3(sampleField(q+ivec3(1,0,0))-sampleField(q-ivec3(1,0,0)),sampleField(q+ivec3(0,1,0))-sampleField(q-ivec3(0,1,0)),sampleField(q+ivec3(0,0,1))-sampleField(q-ivec3(0,0,1)));}
void main(){
    uint id=gl_GlobalInvocationID.x,n=p.grid.x*p.grid.y*p.grid.z;
    if(p.config.y==0){
        if(id>=n)return;ivec3 q=coord(id,p.grid.xyz);uvec3 dims=(p.grid.xyz+uvec3(p.config.z-1))/p.config.z,b=uvec3(q)/p.config.z;
        uint brick=(b.z*dims.y+b.y)*dims.x+b.x;float f=0;
        for(uint j=offsets[brick];j<offsets[brick+1];++j){Kernel k=kernels[ids[j]];vec3 r=point(q)-k.centre.xyz;precise float u=dot(r,k.a.xyz),v=dot(r,k.b.xyz),w=dot(r,k.c.xyz);precise float q2=u*u+v*v+w*w;
            if(q2<1){float h=1-q2;f+=k.centre.w*h*h*h;}}
        field[id]=f;return;
    }
    if(p.config.y==1){
        if(id>=n*3)return;uint axis=id%3;ivec3 q=coord(id/3,p.grid.xyz),r=q;r[axis]++;
        vertices[id].position=vec4(0);vertices[id].normal=vec4(0);
        if(any(greaterThanEqual(r,ivec3(p.grid.xyz))))return;
        float a=field[at(q)],b=field[at(r)];if((a>=p.scalar.x)==(b>=p.scalar.x))return;
        float t=abs(b-a)>1e-8?clamp((p.scalar.x-a)/(b-a),0,1):.5;vec3 g=-mix(gradient(q),gradient(r),t);float len=length(g);
        vertices[id].position=vec4(mix(point(q),point(r),t),1);vertices[id].normal=vec4(len>1e-8?g/len:vec3(0),0);return;
    }
    uvec3 cells=p.grid.xyz-uvec3(1);if(id>=cells.x*cells.y*cells.z)return;ivec3 q=coord(id,cells);uint nodes[8];int cube=0;
    for(int c=0;c<8;++c){nodes[c]=at(q+corner[c]);if(field[nodes[c]]>=p.scalar.x)cube|=1<<c;}
    for(int j=0;j<16&&tri[cube*16+j]>=0;j+=3){uint base=atomicAdd(requested,3);if(base+3>p.config.x){atomicOr(overflow,1);continue;}
        for(int c=0;c<3;++c){ivec2 e=edge[tri[cube*16+j+(c==0?0:3-c)]];uint a=nodes[e.x],b=nodes[e.y],lo=min(a,b),delta=max(a,b)-lo;indices[base+c]=lo*3+(delta==1?0:delta==p.grid.x?1:2);}}
}
