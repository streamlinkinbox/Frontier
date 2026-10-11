//============================================================================================================================================
//                                                           TRANSPORTINTEGRATOR.JS
//============================================================================================================================================
// 📦 Independent WGSL triangle traversal, directional interval tracing/merging, diffuse gather, history rejection and presentation.

export const Common = `
struct Parameters
{
    VP:mat4x4f, PreviousVP:mat4x4f, Camera:vec4f, Size:vec4f, Settings:vec4f, Lighting:vec4f, Switches:vec4f,     Light0:vec4f, Colour0:vec4f, Light1:vec4f, Colour1:vec4f, Debug:vec4f
};
struct Triangle
{
    A:vec4f, B:vec4f, C:vec4f, Normal:vec4f, Colour:vec4f, Emission:vec4f, Meta:vec4u
};
struct Node
{
    Minimum:vec3f, Start:u32, Maximum:vec3f, Count:u32, Escape:vec4u
};
struct Instance
{
    Model:mat4x4f, Inverse:mat4x4f, Meta:vec4u
};
struct Counters
{
    Rays:atomic<u32>, Nodes:atomic<u32>, Triangles:atomic<u32>, Hits:atomic<u32>
};
@group(0) @binding(0) var<uniform> P:Parameters;
@group(0) @binding(1) var<storage,read> Triangles:array<Triangle>;
@group(0) @binding(2) var<storage,read> Nodes:array<Node>;
@group(0) @binding(3) var<storage,read> Instances:array<Instance>;
@group(0) @binding(4) var<storage,read_write> Statistics:Counters;
@group(0) @binding(5) var<uniform> ShadowMatrices:array<mat4x4f,12>;
@group(0) @binding(6) var ShadowTexture:texture_depth_2d_array;
@group(0) @binding(7) var ShadowSampler:sampler_comparison;
const PI:f32=3.14159265359;
struct Hit
{
    Distance:f32, Index:i32, Normal:vec3f, Visits:u32
};
fn Bounds(Origin:vec3f,Inverse:vec3f,N:Node,Minimum:f32,Maximum:f32)->bool
{
    let A=(N.Minimum-Origin)*Inverse;
    let B=(N.Maximum-Origin)*Inverse;
    let Near=min(A,B);
    let Far=max(A,B);
    return max(max(Near.x,Near.y),max(Near.z,Minimum))<=min(min(Far.x,Far.y),min(Far.z,Maximum));
}
fn Trace(Origin:vec3f,Direction:vec3f,Minimum:f32,Maximum:f32,Any:bool)->Hit
{
    var Result=Hit(Maximum,-1,vec3f(0),0u);
    var Tests=0u;
    for(var Object=0u;Object<4u;Object++)
    {
        let Inst=Instances[Object];
        let O=(Inst.Inverse*vec4f(Origin,1)).xyz;
        let D=(Inst.Inverse*vec4f(Direction,0)).xyz;
        let Safe=select(vec3f(1e-8),D,abs(D)>vec3f(1e-8));
        let Inverse=1.0/Safe;
        var Address=Inst.Meta.x;
        loop
        {
            if(Address>=Inst.Meta.y)
            {
                break;
            }
            let N=Nodes[Address];
            Result.Visits++;
            if(!Bounds(O,Inverse,N,Minimum,Result.Distance))
            {
                Address=N.Escape.x;
                continue;
            }
            if(N.Count==0u)
            {
                Address++;
                continue;
            }
            for(var I=0u;I<N.Count;I++)
            {
                let Index=N.Start+I;
                let T=Triangles[Index];
                Tests++;
                let E=T.B.xyz-T.A.xyz;
                let F=T.C.xyz-T.A.xyz;
                let Q=cross(D,F);
                let Det=dot(E,Q);
                if(abs(Det)<1e-7)
                {
                    continue;
                }
                let S=O-T.A.xyz;
                let U=dot(S,Q)/Det;
                if(U<0.0||U>1.0)
                {
                    continue;
                }
                let R=cross(S,E);
                let V=dot(D,R)/Det;
                if(V<0.0||U+V>1.0)
                {
                    continue;
                }
                let Distance=dot(F,R)/Det;
                if(Distance>Minimum&&Distance<Result.Distance)
                {
                    Result.Distance=Distance;
                    Result.Index=i32(Index);
                    var Normal=normalize((Inst.Model*vec4f(T.Normal.xyz,0)).xyz);
                    if(dot(Normal,Direction)>0.0)
                    {
                        Normal=-Normal;
                    }
                    Result.Normal=Normal;
                    if(Any)
                    {
                        break;
                    }
                }
            }
            if(Any&&Result.Index>=0)
            {
                break;
            }
            Address=N.Escape.x;
        }
        if(Any&&Result.Index>=0)
        {
            break;
        }
    }
    if(P.Debug.x>.5)
    {
        atomicAdd(&Statistics.Rays,1u);
        atomicAdd(&Statistics.Nodes,Result.Visits);
        atomicAdd(&Statistics.Triangles,Tests);
        if(Result.Index>=0)
        {
            atomicAdd(&Statistics.Hits,1u);
        }
    }
    return Result;
}
fn Light(Position:vec3f,Normal:vec3f,Source:vec4f,Colour:vec3f,LightIndex:u32)->vec3f
{
    let Delta=Source.xyz-Position;
    let Distance=length(Delta);
    let Direction=Delta/max(Distance,.001);
    let Cosine=max(0.0,dot(Normal,Direction));
    if(Cosine<=0.0||P.Lighting.x==0.0||Source.w==0.0)
    {
        return vec3f(0);
    }
    let D=-Delta;
    let Magnitude=abs(D);
    var Face=0u;
    if(Magnitude.x>=Magnitude.y&&Magnitude.x>=Magnitude.z)
    {
        Face=select(1u,0u,D.x>0.0);
    }
    else if(Magnitude.y>=Magnitude.z)
    {
        Face=select(3u,2u,D.y>0.0);
    }
    else
    {
        Face=select(5u,4u,D.z>0.0);
    }
    let Layer=LightIndex*6u+Face;
    let Clip=ShadowMatrices[Layer]*vec4f(Position+Normal*.025,1);
    let UV=vec2f(Clip.x/Clip.w*.5+.5,.5-Clip.y/Clip.w*.5);
    let Visibility=textureSampleCompareLevel(ShadowTexture,ShadowSampler,UV,i32(Layer),Clip.z/Clip.w-.00015);
    return Colour*(Visibility*Source.w*P.Lighting.x*Cosine/(1.0+Distance*Distance));
}
fn Direct(Position:vec3f,Normal:vec3f)->vec3f
{
    return Light(Position,Normal,P.Light0,P.Colour0.xyz,0u)+Light(Position,Normal,P.Light1,P.Colour1.xyz,1u);
}
fn SurfaceRadiance(Origin:vec3f,Direction:vec3f,H:Hit)->vec3f
{
    let T=Triangles[u32(H.Index)];
    let Position=Origin+Direction*H.Distance;
    // Proxy luminaires have analytic direct lighting; excluding their emission here avoids counting it twice.
    let Emission=T.Emission.xyz*P.Lighting.x*(1.0-T.Emission.w);
    return Emission+T.Colour.xyz*Direct(Position,H.Normal)/PI;
}
fn DirectionAt(Cell:vec2u,Count:u32)->vec3f
{
    let UV=(vec2f(Cell)+.5)/f32(Count);
    let Y=1.0-2.0*UV.y;
    let Radius=sqrt(max(0.0,1.0-Y*Y));
    return vec3f(cos(UV.x*2.0*PI)*Radius,Y,sin(UV.x*2.0*PI)*Radius);
}
fn Basis(D:vec3f,I:u32)->f32
{
    switch I
    {
        case 0u:
        {
            return .282094792;
        }
        case 1u:
        {
            return .488602512*D.y;
        }
        case 2u:
        {
            return .488602512*D.z;
        }
        case 3u:
        {
            return .488602512*D.x;
        }
        case 4u:
        {
            return 1.092548431*D.x*D.y;
        }
        case 5u:
        {
            return 1.092548431*D.y*D.z;
        }
        case 6u:
        {
            return .315391565*(3.0*D.z*D.z-1.0);
        }
        case 7u:
        {
            return 1.092548431*D.x*D.z;
        }
        default:
        {
            return .546274215*(D.x*D.x-D.y*D.y);
        }
    }
}
fn MergeInterval(Near:vec4f,Far:vec4f)->vec4f
{
    return vec4f(Near.xyz+Near.w*Far.xyz,Near.w*Far.w);
}
fn Environment(Direction:vec3f)->vec3f
{
    return mix(vec3f(.3,.38,.5),vec3f(.7,.8,1),max(0.0,Direction.y))*P.Lighting.y;
}
`;
export const Geometry = `
struct Vertex
{
    @builtin(position) Clip:vec4f, @location(0) World:vec3f, @location(1) Normal:vec3f,     @location(2) @interpolate(flat) Index:u32
};
@vertex fn VertexMain(@builtin(vertex_index) Index:u32)->Vertex
{
    let T=Triangles[Index/3u];
    let Inst=Instances[T.Meta.x];
    var Point=T.A.xyz;
    if(Index%3u==1u)
    {
        Point=T.B.xyz;
    }
    if(Index%3u==2u)
    {
        Point=T.C.xyz;
    }
    let World=Inst.Model*vec4f(Point,1);
    return Vertex(P.VP*World,World.xyz,normalize((Inst.Model*vec4f(T.Normal.xyz,0)).xyz),Index/3u);
}
struct GBuffer
{
    @location(0) Position:vec4f, @location(1) Normal:vec4f, @location(2) Albedo:vec4f, @location(3) Emission:vec4f
};
@fragment fn FragmentMain(V:Vertex,@builtin(front_facing) Front:bool)->GBuffer
{
    let T=Triangles[V.Index];
    var N=V.Normal;
    if(!Front)
    {
        N=-N;
    }
    return GBuffer(vec4f(V.World,f32(T.Meta.x+1u)),vec4f(N,T.Colour.w),vec4f(T.Colour.xyz,1),vec4f(T.Emission.xyz*P.Lighting.x,T.Emission.w));
}
`;
export const ProbeFunctions = `
struct Cascade
{
    Grid:vec4u, Segment:vec4f, Upper:vec4u
};
struct Probe
{
    Position:vec4f, Normal:vec3f
};
fn ReadProbe(Cell:vec2i,Spacing:f32,Grid:vec2u)->Probe
{
    let Clamped=clamp(Cell,vec2i(0),vec2i(Grid)-1);
    var Pixel=clamp(vec2i((vec2f(Clamped)+.5)*Spacing),vec2i(0),vec2i(P.Size.xy)-1);
    var Position=textureLoad(PositionTexture,Pixel,0);
    if(Position.w==0.0)
    {
        for(var K=0u;K<4u;K++)
        {
            let Offset=(vec2f(f32(K&1u),f32(K>>1u))-.5)*Spacing*.7;
            let Candidate=clamp(Pixel+vec2i(Offset),vec2i(0),vec2i(P.Size.xy)-1);
            let Reading=textureLoad(PositionTexture,Candidate,0);
            if(Reading.w>0.0)
            {
                Pixel=Candidate;
                Position=Reading;
                break;
            }
        }
    }
    return Probe(Position,textureLoad(NormalTexture,Pixel,0).xyz);
}
fn Address(Cell:vec2u,Direction:vec2u,Grid:vec2u,Count:u32)->u32
{
    return (Cell.y*Grid.x+Cell.x)*Count*Count+Direction.y*Count+Direction.x;
}
`;
export const Cascade = `
@group(1) @binding(0) var PositionTexture:texture_2d<f32>;
@group(1) @binding(1) var NormalTexture:texture_2d<f32>;
@group(1) @binding(2) var<storage,read> Upper:array<vec4f>;
@group(1) @binding(3) var<storage,read_write> Output:array<vec4f>;
@group(1) @binding(4) var<uniform> C:Cascade;
@group(1) @binding(5) var<storage,read> SpatialWeights:array<vec4f>;
${ProbeFunctions} @compute @workgroup_size(64) fn CascadeMain(@builtin(global_invocation_id) Id:vec3u)
{
    let Count=C.Grid.z;
    let Total=C.Grid.x*C.Grid.y*Count*Count;
    if(Id.x>=Total)
    {
        return;
    }
    let ProbeIndex=Id.x/(Count*Count);
    let Cell=vec2u(ProbeIndex%C.Grid.x,ProbeIndex/C.Grid.x);
    let Angle=vec2u(Id.x%Count,(Id.x/Count)%Count);
    let Direction=DirectionAt(Angle,Count);
    let Current=ReadProbe(vec2i(Cell),C.Segment.z,C.Grid.xy);
    if(Current.Position.w==0.0)
    {
        Output[Id.x]=vec4f(0,0,0,1);
        return;
    }
    let Origin=Current.Position.xyz+Current.Normal*.045;
    let H=Trace(Origin,Direction,max(.015,C.Segment.x),C.Segment.y,false);
    var Radiance=vec3f(0);
    var Transmission=1.0;
    if(H.Index>=0)
    {
        Radiance=SurfaceRadiance(Origin,Direction,H);
        Transmission=0.0;
    }
    var Far=vec4f(Environment(Direction),1);
    if(C.Upper.z>0u&&Transmission>0.0)
    {
        let Coordinate=(vec2f(Cell)+.5)*.5-.5;
        let Base=vec2i(floor(Coordinate));
        let Fraction=fract(Coordinate);
        var Sum=vec4f(0);
        var WeightSum=0.0;
        for(var K=0u;K<4u;K++)
        {
            let Offset=vec2i(i32(K&1u),i32(K>>1u));
            let Parent=clamp(Base+Offset,vec2i(0),vec2i(C.Upper.xy)-1);
            let Weight=SpatialWeights[ProbeIndex][K];
            if(Weight<=.00001)
            {
                continue;
            }
            var Child=vec4f(0);
            for(var J=0u;J<4u;J++)
            {
                let ChildAngle=Angle*2u+vec2u(J&1u,J>>1u);
                Child+=Upper[Address(vec2u(Parent),ChildAngle,C.Upper.xy,C.Upper.z)]*.25;
            }
            Sum+=Child*Weight;
            WeightSum+=Weight;
        }
        Far=vec4f(0,0,0,1);
        if(WeightSum>.00001)
        {
            Far=Sum/WeightSum;
        }
    }
    Output[Id.x]=MergeInterval(vec4f(Radiance,Transmission),Far);
}
`;
export const Lighting = `
@group(1) @binding(0) var PositionTexture:texture_2d<f32>;
@group(1) @binding(1) var NormalTexture:texture_2d<f32>;
@group(1) @binding(2) var AlbedoTexture:texture_2d<f32>;
@group(1) @binding(3) var EmissionTexture:texture_2d<f32>;
@group(1) @binding(4) var<storage,read> Radiance:array<vec4f>;
@group(1) @binding(5) var Result:texture_storage_2d<rgba16float,write>;
@group(1) @binding(6) var<uniform> C:Cascade;
${ProbeFunctions} fn Gather(Cell:vec2i,Normal:vec3f)->vec3f
{
    let Coordinate=vec2u(clamp(Cell,vec2i(0),vec2i(C.Grid.xy)-1));
    var Sum=vec3f(0);
    let Start=(Coordinate.y*C.Grid.x+Coordinate.x)*9u;
    for(var I=0u;I<9u;I++)
    {
        let Kernel=select(select(.25,2.0/3.0,I<4u),1.0,I==0u);
        Sum+=Radiance[Start+I].xyz*Basis(Normal,I)*Kernel;
    }
    return max(Sum,vec3f(0));
}
@compute @workgroup_size(8,8) fn LightingMain(@builtin(global_invocation_id) Id:vec3u)
{
    if(any(Id.xy>=vec2u(P.Size.xy)))
    {
        return;
    }
    let Pixel=vec2i(Id.xy);
    let Position=textureLoad(PositionTexture,Pixel,0);
    if(Position.w==0.0)
    {
        textureStore(Result,Pixel,vec4f(.008,.014,.026,0));
        return;
    }
    let N=normalize(textureLoad(NormalTexture,Pixel,0).xyz);
    let Albedo=textureLoad(AlbedoTexture,Pixel,0).xyz;
    let Mode=u32(P.Switches.z);
    if(Mode==1u||Mode==3u||Mode==4u||Mode==5u||Mode==8u)
    {
        var Colour=Albedo;
        if(Mode==1u)
        {
            Colour=Albedo*Direct(Position.xyz,N)/PI+textureLoad(EmissionTexture,Pixel,0).xyz;
        }
        if(Mode==3u)
        {
            Colour=N*.5+.5;
        }
        if(Mode==5u)
        {
            let Tile=vec2u(Id.xy)/u32(C.Segment.z);
            let Edge=any(vec2u(Id.xy)%u32(C.Segment.z)==vec2u(0));
            let Hash=fract(sin(f32(Tile.x*17u+Tile.y*139u))*43758.5);
            Colour=select(vec3f(.15+Hash*.7,.4,.7-Hash*.5),vec3f(.02),Edge);
        }
        if(Mode==8u)
        {
            let H=Trace(P.Camera.xyz,normalize(Position.xyz-P.Camera.xyz),.02,80,false);
            let Cost=clamp(f32(H.Visits)/180.0,0.0,1.0);
            Colour=vec3f(Cost,1.0-abs(Cost-.5)*2.0,1.0-Cost);
        }
        textureStore(Result,Pixel,vec4f(Colour,1));
        return;
    }
    let Coordinate=(vec2f(Id.xy)+.5)/C.Segment.z-.5;
    let Base=vec2i(floor(Coordinate));
    let Fraction=fract(Coordinate);
    var Sum=vec3f(0);
    var WeightSum=0.0;
    for(var K=0u;K<4u;K++)
    {
        let Offset=vec2i(i32(K&1u),i32(K>>1u));
        let Cell=Base+Offset;
        let Other=ReadProbe(Cell,C.Segment.z,C.Grid.xy);
        if(Other.Position.w==0.0)
        {
            continue;
        }
        let Delta=Position.xyz-Other.Position.xyz;
        let Components=select(1.0-Fraction,Fraction,vec2u(Offset)>vec2u(0));
        let Weight=max(.0001,Components.x*Components.y)*pow(max(0.0,dot(N,Other.Normal)),16.0)*exp(-abs(dot(Delta,N))*12.0);
        Sum+=Gather(Cell,N)*Weight;
        WeightSum+=Weight;
    }
    var Indirect=vec3f(0);
    if(WeightSum>.0001)
    {
        Indirect=Sum/WeightSum;
    }
    let Incoming=Direct(Position.xyz,N)/PI;
    let Emission=textureLoad(EmissionTexture,Pixel,0).xyz;
    var Colour=Albedo*(Incoming+Indirect)+Emission;
    if(P.Lighting.z>.5)
    {
        let Direction=reflect(normalize(Position.xyz-P.Camera.xyz),N);
        let Origin=Position.xyz+N*.045;
        let H=Trace(Origin,Direction,.02,80,false);
        var Reflection=Environment(Direction);
        if(H.Index>=0)
        {
            let T=Triangles[u32(H.Index)];
            Reflection=T.Emission.xyz*P.Lighting.x+T.Colour.xyz*Direct(Origin+Direction*H.Distance,H.Normal)/PI;
        }
        let Roughness=textureLoad(NormalTexture,Pixel,0).w;
        let Fresnel=.04+.96*pow(1.0-max(0.0,dot(N,normalize(P.Camera.xyz-Position.xyz))),5.0);
        Colour+=Reflection*Fresnel*(1.0-Roughness);
    }
    if(Mode==2u)
    {
        Colour=Albedo*Indirect;
    }
    textureStore(Result,Pixel,vec4f(max(Colour,vec3f(0)),1));
}
`;
export const Temporal = `
@group(1) @binding(0) var Current:texture_2d<f32>;
@group(1) @binding(1) var Position:texture_2d<f32>;
@group(1) @binding(2) var Normal:texture_2d<f32>;
@group(1) @binding(3) var Previous:texture_2d<f32>;
@group(1) @binding(4) var PreviousPosition:texture_2d<f32>;
@group(1) @binding(5) var PreviousNormal:texture_2d<f32>;
@group(1) @binding(6) var Output:texture_storage_2d<rgba16float,write>;
@compute @workgroup_size(8,8) fn TemporalMain(@builtin(global_invocation_id) Id:vec3u)
{
    if(any(Id.xy>=vec2u(P.Size.xy)))
    {
        return;
    }
    let Pixel=vec2i(Id.xy);
    let Now=textureLoad(Current,Pixel,0);
    let World=textureLoad(Position,Pixel,0);
    var Colour=Now.xyz;
    var Weight=0.0;
    let Clip=P.PreviousVP*vec4f(World.xyz,1);
    let UV=vec2f(Clip.x/Clip.w*.5+.5,.5-Clip.y/Clip.w*.5);
    if(P.Switches.x>.5&&(P.Switches.z==0.0||P.Switches.z==9.0)&&World.w==1.0&&Clip.w>0.0&&all(UV>=vec2f(0))&&all(UV<vec2f(1)))
    {
        let OldPixel=vec2i(UV*P.Size.xy);
        let OldWorld=textureLoad(PreviousPosition,OldPixel,0);
        let N=textureLoad(Normal,Pixel,0).xyz;
        let OldN=textureLoad(PreviousNormal,OldPixel,0).xyz;
        if(OldWorld.w==World.w&&distance(OldWorld.xyz,World.xyz)<.09&&dot(N,OldN)>.94)
        {
            var Low=Now.xyz;
            var High=Now.xyz;
            for(var Y=-1;Y<=1;Y++)
            {
                for(var X=-1;X<=1;X++)
                {
                    let V=textureLoad(Current,clamp(Pixel+vec2i(X,Y),vec2i(0),vec2i(P.Size.xy)-1),0).xyz;
                    Low=min(Low,V);
                    High=max(High,V);
                }
            }
            let Old=clamp(textureLoad(Previous,OldPixel,0).xyz,Low,High);
            Weight=min(P.Settings.y,select(.95,.65,P.Switches.y>.5));
            Colour=mix(Now.xyz,Old,Weight);
        }
    }
    textureStore(Output,Pixel,vec4f(Colour,Weight));
}
`;
export const Presentation = `
@group(1) @binding(0) var Image:texture_2d<f32>;
@group(1) @binding(1) var ImageSampler:sampler;
@group(1) @binding(2) var<storage,read> CascadeData:array<vec4f>;
@group(1) @binding(3) var<uniform> C:Cascade;
struct Cascade
{
    Grid:vec4u, Segment:vec4f, Upper:vec4u
};
struct Varying
{
    @builtin(position) Clip:vec4f, @location(0) UV:vec2f
};
@vertex fn PresentVertex(@builtin(vertex_index) Id:u32)->Varying
{
    let UV=vec2f(f32((Id<<1u)&2u),f32(Id&2u));
    return Varying(vec4f(UV*vec2f(2,-2)+vec2f(-1,1),0,1),UV);
}
@fragment fn PresentFragment(V:Varying)->@location(0) vec4f
{
    let Mode=u32(P.Switches.z);
    let Sample=textureSampleLevel(Image,ImageSampler,V.UV,0);
    var Colour=Sample.xyz;
    if(Mode==6u||Mode==7u)
    {
        let Atlas=vec2u(clamp(V.UV,vec2f(0),vec2f(.99999))*vec2f(C.Grid.xy*C.Grid.z));
        let Cell=Atlas/C.Grid.z;
        let Angle=Atlas%C.Grid.z;
        let Index=(Cell.y*C.Grid.x+Cell.x)*C.Grid.z*C.Grid.z+Angle.y*C.Grid.z+Angle.x;
        let Reading=CascadeData[Index];
        Colour=Reading.xyz;
        if(Mode==7u)
        {
            return vec4f(vec3f(Reading.w),1);
        }
    }
    if(Mode==9u)
    {
        return vec4f(Sample.w,1.0-Sample.w,.15,1);
    }
    if(Mode==3u||Mode==4u||Mode==5u||Mode==8u)
    {
        return vec4f(Colour,1);
    }
    Colour*=P.Settings.z;
    Colour=(Colour*(2.51*Colour+.03))/(Colour*(2.43*Colour+.59)+.14);
    return vec4f(pow(clamp(Colour,vec3f(0),vec3f(1)),vec3f(1.0/2.2)),1);
}
`;

export const Projection = `
@group(1) @binding(0) var<storage,read> Radiance:array<vec4f>;
@group(1) @binding(1) var<storage,read_write> Coefficients:array<vec4f>;
@group(1) @binding(2) var<uniform> Grid:vec4u;
@compute @workgroup_size(64) fn ProjectionMain(@builtin(global_invocation_id) Id:vec3u)
{
    if(Id.x>=Grid.x*Grid.y)
    {
        return;
    }
    let Count=Grid.z;
    for(var I=0u;I<9u;I++)
    {
        var Sum=vec3f(0);
        for(var Y=0u;Y<Count;Y++)
        {
            for(var X=0u;X<Count;X++)
            {
                let Direction=DirectionAt(vec2u(X,Y),Count);
                Sum+=Radiance[Id.x*Count*Count+Y*Count+X].xyz*Basis(Direction,I);
            }
        }
        Coefficients[Id.x*9u+I]=vec4f(Sum*(4.0*PI/f32(Count*Count)),0);
    }
}
`;

export const Shadow = `
struct ShadowFace
{
    VP:mat4x4f, Meta:vec4u
};
@group(1) @binding(0) var<uniform> Face:ShadowFace;
@vertex fn ShadowVertex(@builtin(vertex_index) Index:u32)->@builtin(position) vec4f
{
    let T=Triangles[Index/3u];
    if(T.Meta.x==Face.Meta.x+2u)
    {
        return vec4f(2,2,2,1);
    }
    var Point=T.A.xyz;
    if(Index%3u==1u)
    {
        Point=T.B.xyz;
    }
    if(Index%3u==2u)
    {
        Point=T.C.xyz;
    }
    return Face.VP*Instances[T.Meta.x].Model*vec4f(Point,1);
}
`;

export const Links = `
@group(1) @binding(0) var PositionTexture:texture_2d<f32>;
@group(1) @binding(1) var NormalTexture:texture_2d<f32>;
@group(1) @binding(2) var<storage,read_write> Weights:array<vec4f>;
@group(1) @binding(3) var<uniform> C:Cascade;
${ProbeFunctions} @compute @workgroup_size(64) fn LinkMain(@builtin(global_invocation_id) Id:vec3u)
{
    if(Id.x>=C.Grid.x*C.Grid.y)
    {
        return;
    }
    let Cell=vec2u(Id.x%C.Grid.x,Id.x/C.Grid.x);
    let Current=ReadProbe(vec2i(Cell),C.Segment.z,C.Grid.xy);
    var Result=vec4f(0);
    if(Current.Position.w==0.0)
    {
        Weights[Id.x]=Result;
        return;
    }
    let Origin=Current.Position.xyz+Current.Normal*.045;
    let Coordinate=(vec2f(Cell)+.5)*.5-.5;
    let Base=vec2i(floor(Coordinate));
    let Fraction=fract(Coordinate);
    for(var K=0u;K<4u;K++)
    {
        let Offset=vec2i(i32(K&1u),i32(K>>1u));
        let Parent=clamp(Base+Offset,vec2i(0),vec2i(C.Upper.xy)-1);
        let Other=ReadProbe(Parent,C.Segment.z*2.0,C.Upper.xy);
        if(Other.Position.w==0.0)
        {
            continue;
        }
        let Delta=Other.Position.xyz+Other.Normal*.045-Origin;
        let Distance=length(Delta);
        let Components=select(1.0-Fraction,Fraction,vec2u(Offset)>vec2u(0));
        var Weight=Components.x*Components.y*exp(-Distance/max(.2,C.Segment.y*2.0));
        if(P.Lighting.w>.5&&Distance>.08&&Weight>.001)
        {
            let Blocker=Trace(Origin,Delta/Distance,.02,max(.021,Distance-.06),true);
            if(Blocker.Index>=0)
            {
                Weight=0.0;
            }
        }
        Result[K]=Weight;
    }
    Weights[Id.x]=Result;
}
`;
