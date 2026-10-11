//============================================================================================================================================
//                                                           TRANSPORTINTEGRATOR.JS
//============================================================================================================================================
// 📦 GPU vertex deformation, quad expansion, bottom-up BVH refit and progressive diffuse path integration against current triangles.

export const Declarations = `
struct Facet
{
    First:vec4f, Second:vec4f, Third:vec4f, Normal:vec4f,     Colour:vec4f, Emission:vec4f, Metadata:vec4u
}
struct Extent
{
    Minimum:vec3f, Start:u32, Maximum:vec3f, Count:u32, Escape:vec4u
}
struct Parameters
{
    Projection:mat4x4f, Camera:vec4f, Size:vec4u, Lighting:vec4f, Configuration:vec4u, Geometry:vec4u
}
`;

export const Deformation = Declarations + `
@group(0) @binding(0) var<uniform> ParametersEntry:Parameters;
@group(0) @binding(1) var<storage,read> Rest:array<vec4f>;
@group(0) @binding(2) var<storage,read_write> Vertices:array<vec4f>;
@group(0) @binding(3) var<storage,read> Connectivity:array<vec4u>;
@group(0) @binding(4) var<storage,read_write> Facets:array<Facet>;
@group(0) @binding(5) var<storage,read_write> Bounds:array<Extent>;
@group(0) @binding(6) var<storage,read> DepthIndices:array<u32>;
@group(0) @binding(7) var<uniform> DepthRange:vec4u;
@group(0) @binding(8) var Poses:texture_2d<f32>;
@compute @workgroup_size(64) fn Deform(@builtin(global_invocation_id) Invocation:vec3u)
{
    let Index=Invocation.x;
    if(Index>=arrayLength(&Rest))
    {
        return;
    }
    var Position=Rest[Index];
    let Compression=ParametersEntry.Lighting.x;
    if(Position.w==1.0)
    {
        if(ParametersEntry.Configuration.x==1u)
        {
            let Pose=clamp(Compression/.65,0.0,1.0)*16.0;
            let Lower=u32(floor(Pose));
            let Upper=min(Lower+1u,16u);
            let Coordinate=vec2i(i32(Index%64u),i32(Index/64u));
            Position=mix(textureLoad(Poses,Coordinate+vec2i(0,i32(Lower)*24),0),                 textureLoad(Poses,Coordinate+vec2i(0,i32(Upper)*24),0),fract(Pose));
        }
        else
        {
            let Relative=max(0.0,Position.y-.03);
            let Contact=pow(1.0-min(1.0,Relative/.67),2.0);
            var Height=Position.y-Compression;
            if(Relative<.67)
            {
                Height=.03+pow(.67-Compression,2.0)*Relative/(.67*.67-Compression*Relative);
            }
            Position=vec4f(Position.x*(1.0+.13*Compression*Contact),Height,                 Position.z*(1.0+.65*Compression*Contact),Position.w);
        }
    }
    else if(Position.w==2.0)
    {
        Position.y-=Compression;
    }
    Vertices[Index]=Position;
}
@compute @workgroup_size(64) fn Expand(@builtin(global_invocation_id) Invocation:vec3u)
{
    let Index=Invocation.x;
    if(Index>=arrayLength(&Connectivity))
    {
        return;
    }
    let Corners=Connectivity[Index];
    let First=Vertices[Corners.x];
    let Second=Vertices[Corners.y];
    let Third=Vertices[Corners.z];
    Facets[Index].First=First;
    Facets[Index].Second=Second;
    Facets[Index].Third=Third;
    Facets[Index].Normal=vec4f(normalize(cross(Second.xyz-First.xyz,Third.xyz-First.xyz)),0);
}
@compute @workgroup_size(64) fn Refit(@builtin(global_invocation_id) Invocation:vec3u)
{
    if(Invocation.x>=DepthRange.y)
    {
        return;
    }
    let Index=DepthIndices[DepthRange.x+Invocation.x];
    let Bound=Bounds[Index];
    var Minimum=vec3f(1e20);
    var Maximum=vec3f(-1e20);
    if(Bound.Count>0u)
    {
        for(var Offset=0u;Offset<Bound.Count;Offset++)
        {
            let Triangle=Facets[Bound.Start+Offset];
            Minimum=min(Minimum,min(Triangle.First.xyz,min(Triangle.Second.xyz,Triangle.Third.xyz))-.00001);
            Maximum=max(Maximum,max(Triangle.First.xyz,max(Triangle.Second.xyz,Triangle.Third.xyz))+.00001);
        }
    }
    else
    {
        Minimum=min(Bounds[Bound.Escape.y].Minimum,Bounds[Bound.Escape.z].Minimum);
        Maximum=max(Bounds[Bound.Escape.y].Maximum,Bounds[Bound.Escape.z].Maximum);
    }
    Bounds[Index].Minimum=Minimum;
    Bounds[Index].Maximum=Maximum;
}
`;

export const Traversal = Declarations + `
@group(0) @binding(0) var<uniform> ParametersEntry:Parameters;
@group(0) @binding(1) var<storage,read> Facets:array<Facet>;
@group(0) @binding(2) var<storage,read> Bounds:array<Extent>;
struct Intersection
{
    Distance:f32, Index:i32, Visits:u32, Reserved:u32
}
fn Trace(Origin:vec3f,Direction:vec3f,Limit:f32,Visibility:bool)->Intersection
{
    var Result=Intersection(Limit,-1,0u,0u);
    let Inverse=1.0/select(vec3f(1e-12),Direction,abs(Direction)>vec3f(1e-12));
    var Address=0u;
    loop
    {
        if(Address>=arrayLength(&Bounds))
        {
            break;
        }
        let Bound=Bounds[Address];
        Result.Visits++;
        let First=(Bound.Minimum-Origin)*Inverse;
        let Second=(Bound.Maximum-Origin)*Inverse;
        let Near=min(First,Second);
        let Far=max(First,Second);
        if(max(max(Near.x,Near.y),max(Near.z,.001))>min(min(Far.x,Far.y),min(Far.z,Result.Distance)))
        {
            Address=Bound.Escape.x;
            continue;
        }
        if(Bound.Count==0u)
        {
            Address++;
            continue;
        }
        for(var Offset=0u;Offset<Bound.Count;Offset++)
        {
            let Index=Bound.Start+Offset;
            let Triangle=Facets[Index];
            let Edge=Triangle.Second.xyz-Triangle.First.xyz;
            let Other=Triangle.Third.xyz-Triangle.First.xyz;
            let Perpendicular=cross(Direction,Other);
            let Determinant=dot(Edge,Perpendicular);
            if(abs(Determinant)<1e-8)
            {
                continue;
            }
            let Relative=Origin-Triangle.First.xyz;
            let FirstWeight=dot(Relative,Perpendicular)/Determinant;
            if(FirstWeight<0.0||FirstWeight>1.0)
            {
                continue;
            }
            let Tangent=cross(Relative,Edge);
            let SecondWeight=dot(Direction,Tangent)/Determinant;
            if(SecondWeight<0.0||FirstWeight+SecondWeight>1.0)
            {
                continue;
            }
            let Distance=dot(Other,Tangent)/Determinant;
            if(Distance>.001&&Distance<Result.Distance)
            {
                Result.Distance=Distance;
                Result.Index=i32(Index);
                if(Visibility)
                {
                    return Result;
                }
            }
        }
        Address=Bound.Escape.x;
    }
    return Result;
}
`;

export const Raster = Declarations + `
@group(0) @binding(0) var<uniform> ParametersEntry:Parameters;
@group(0) @binding(1) var<storage,read> Facets:array<Facet>;
struct Projection
{
    @builtin(position) Clip:vec4f, @location(0) Position:vec3f,     @location(1) Coordinate:vec2f, @location(2) @interpolate(flat) Index:u32
}
@vertex fn Project(@builtin(vertex_index) Index:u32)->Projection
{
    let Address=Index/3u;
    let Corner=Index%3u;
    let Triangle=Facets[Address];
    let Position=select(select(Triangle.First.xyz,Triangle.Second.xyz,Corner==1u),Triangle.Third.xyz,Corner==2u);
    var Coordinate=vec2f(0);
    if(Triangle.Metadata.x==0u)
    {
        Coordinate=select(select(vec2f(0,0),vec2f(1,0),Corner==1u),vec2f(1,1),Corner==2u);
    }
    else
    {
        Coordinate=select(select(vec2f(0,0),vec2f(1,1),Corner==1u),vec2f(0,1),Corner==2u);
    }
    return Projection(ParametersEntry.Projection*vec4f(Position,1),Position,Coordinate,Address);
}
struct Visibility
{
    @location(0) Position:vec4f, @location(1) Normal:vec4f, @location(2) Colour:vec4f
}
@fragment fn Resolve(Input:Projection)->Visibility
{
    let Triangle=Facets[Input.Index];
    let Normal=select(Triangle.Normal.xyz,-Triangle.Normal.xyz,dot(Triangle.Normal.xyz,ParametersEntry.Camera.xyz-Input.Position)<0.0);
    let Edges=min(Input.Coordinate,1.0-Input.Coordinate)/max(fwidth(Input.Coordinate),vec2f(.00001));
    let Line=1.0-smoothstep(.6,1.2,min(Edges.x,Edges.y));
    return Visibility(vec4f(Input.Position,f32(Input.Index+1u)),vec4f(Normal,0),vec4f(Triangle.Colour.xyz,Line));
}
`;

export const Integration = Traversal + `
@group(1) @binding(0) var PositionTexture:texture_2d<f32>;
@group(1) @binding(1) var NormalTexture:texture_2d<f32>;
@group(1) @binding(2) var ColourTexture:texture_2d<f32>;
@group(1) @binding(3) var Previous:texture_2d<f32>;
@group(1) @binding(4) var Result:texture_storage_2d<rgba32float,write>;
fn Random(Seed:ptr<function,u32>)->f32
{
    *Seed=(*Seed)*747796405u+2891336453u;
    let Word=(((*Seed)>>(((*Seed)>>28u)+4u))^(*Seed))*277803737u;
    return f32((Word>>22u)^Word)*(1.0/4294967296.0);
}
fn SampleLight(Position:vec3f,Normal:vec3f,Albedo:vec3f,Seed:ptr<function,u32>)->vec3f
{
    let Choice=Random(Seed);
    let Horizontal=Random(Seed);
    let Vertical=Random(Seed);
    var Point=vec3f(-2.7+2.2*Horizontal,5.4,-.3+1.6*Vertical);
    var LightNormal=vec3f(0,-1,0);
    var Emission=vec3f(12,10.5,8.5);
    if(Choice>.5)
    {
        Point=vec3f(3,1.1+2.2*Horizontal,-1.3+1.6*Vertical);
        LightNormal=vec3f(-1,0,0);
        Emission=vec3f(5,8,12);
    }
    let Delta=Point-Position;
    let Squared=dot(Delta,Delta);
    let Distance=sqrt(Squared);
    let Direction=Delta/Distance;
    let Cosine=max(0.0,dot(Normal,Direction));
    if(Cosine<=0.0||ParametersEntry.Lighting.y==0.0)
    {
        return vec3f(0);
    }
    if(Trace(Position+Normal*.002,Direction,Distance-.005,true).Index>=0)
    {
        return vec3f(0);
    }
    return Albedo*Emission*(ParametersEntry.Lighting.y*Cosine*abs(dot(LightNormal,-Direction))*7.04/(3.14159265359*Squared));
}
fn CosineDirection(Normal:vec3f,Seed:ptr<function,u32>)->vec3f
{
    let Radius=sqrt(Random(Seed));
    let Angle=6.28318530718*Random(Seed);
    let Axis=select(vec3f(0,1,0),vec3f(1,0,0),abs(Normal.y)>.9);
    let Tangent=normalize(cross(Axis,Normal));
    return normalize(Tangent*(Radius*cos(Angle))+cross(Normal,Tangent)*(Radius*sin(Angle))+Normal*sqrt(max(0.0,1.0-Radius*Radius)));
}
@compute @workgroup_size(8,8) fn Integrate(@builtin(global_invocation_id) Invocation:vec3u)
{
    if(any(Invocation.xy>=ParametersEntry.Size.xy))
    {
        return;
    }
    let Pixel=vec2i(Invocation.xy);
    let Surface=textureLoad(PositionTexture,Pixel,0);
    if(Surface.w==0.0)
    {
        textureStore(Result,Pixel,vec4f(.009,.015,.024,1));
        return;
    }
    let FirstNormal=normalize(textureLoad(NormalTexture,Pixel,0).xyz);
    let FirstColour=textureLoad(ColourTexture,Pixel,0).xyz;
    let Primary=Facets[u32(Surface.w)-1u];
    let View=ParametersEntry.Configuration.z;
    if(View==3u)
    {
        textureStore(Result,Pixel,vec4f(FirstNormal*.5+.5,1));
        return;
    }
    var Sum=vec3f(0);
    for(var Sample=0u;Sample<ParametersEntry.Configuration.w;Sample++)
    {
        var Seed=(Invocation.y*ParametersEntry.Size.x+Invocation.x)*1973u+(ParametersEntry.Size.z+Sample)*9277u+89173u;
        var Position=Surface.xyz;
        var Normal=FirstNormal;
        var Colour=FirstColour;
        var Throughput=vec3f(1);
        var Radiance=select(Primary.Emission.xyz*ParametersEntry.Lighting.y,vec3f(0),View==2u);
        for(var Bounce=0u;Bounce<ParametersEntry.Size.w;Bounce++)
        {
            let Direct=SampleLight(Position,Normal,Colour,&Seed);
            if((View!=2u||Bounce>0u)&&(View!=1u||Bounce==0u))
            {
                Radiance+=Throughput*Direct;
            }
            if(Bounce+1u>=ParametersEntry.Size.w)
            {
                break;
            }
            let Direction=CosineDirection(Normal,&Seed);
            Throughput*=Colour;
            let Origin=Position+Normal*.002;
            let Hit=Trace(Origin,Direction,40.0,false);
            if(Hit.Index<0)
            {
                break;
            }
            let Triangle=Facets[u32(Hit.Index)];
            // 📝 Explicit area-light sampling already estimates emitter paths: no second emission estimator is added here.
            if(any(Triangle.Emission.xyz>vec3f(0)))
            {
                break;
            }
            Position=Origin+Direction*Hit.Distance;
            Normal=select(Triangle.Normal.xyz,-Triangle.Normal.xyz,dot(Triangle.Normal.xyz,Direction)>0.0);
            Colour=Triangle.Colour.xyz;
        }
        Sum+=Radiance;
    }
    var Accumulated=vec3f(0);
    if(ParametersEntry.Size.z>0u)
    {
        Accumulated=textureLoad(Previous,Pixel,0).xyz*f32(ParametersEntry.Size.z);
    }
    textureStore(Result,Pixel,vec4f((Accumulated+Sum)/f32(ParametersEntry.Size.z+ParametersEntry.Configuration.w),1));
}
`;

export const Presentation = Declarations + `
@group(0) @binding(0) var<uniform> ParametersEntry:Parameters;
@group(1) @binding(0) var Radiance:texture_2d<f32>;
@group(1) @binding(1) var ColourTexture:texture_2d<f32>;
struct Screen
{
    @builtin(position) Position:vec4f, @location(0) Coordinate:vec2f
}
@vertex fn Project(@builtin(vertex_index) Index:u32)->Screen
{
    let Coordinate=vec2f(f32((Index<<1u)&2u),f32(Index&2u));
    return Screen(vec4f(Coordinate*2.0-1.0,0,1),vec2f(Coordinate.x,1.0-Coordinate.y));
}
@fragment fn Resolve(Input:Screen)->@location(0) vec4f
{
    let Dimensions=vec2i(ParametersEntry.Size.xy);
    let Pixel=clamp(vec2i(Input.Coordinate*vec2f(Dimensions)),vec2i(0),Dimensions-1);
    var Colour=textureLoad(Radiance,Pixel,0).xyz*ParametersEntry.Lighting.z;
    if(ParametersEntry.Configuration.z!=3u)
    {
        Colour=(Colour*(2.51*Colour+.03))/(Colour*(2.43*Colour+.59)+.14);
        Colour=pow(clamp(Colour,vec3f(0),vec3f(1)),vec3f(1.0/2.2));
    }
    if(ParametersEntry.Geometry.w>0u)
    {
        Colour=mix(Colour,vec3f(.52,.94,.68),textureLoad(ColourTexture,Pixel,0).w*.8);
    }
    return vec4f(Colour,1);
}
`;
