//============================================================================================================================================
//                                                           DISTANCEINTEGRATOR.JS
//============================================================================================================================================
// 📦 Dynamic mesh-to-field construction, signed trilinear distance queries, sphere tracing and single-bounce diffuse gathering.

import
{
    Declarations as EarlierDeclarations,
    Deformation as EarlierDeformation,
    Traversal as EarlierTraversal,
    Raster as EarlierRaster
}
from '../DeformationIntegrator/TransportIntegrator.js';
export const Declarations = EarlierDeclarations.slice(0, EarlierDeclarations.indexOf('struct Parameters')) + `
struct Parameters
{
    Projection:mat4x4f, InverseProjection:mat4x4f, Camera:vec4f, Viewport:vec4u,     Settings:vec4f, Field:vec4u, DomainMinimum:vec4f, DomainMaximum:vec4f, Options:vec4u
}
`;
const Prefix = EarlierDeformation.slice(EarlierDeclarations.length, EarlierDeformation.indexOf('@compute'))
    .replace('@group(0) @binding(8) var Poses:texture_2d<f32>;', '');
export const Deformation = Declarations + Prefix + `
@compute @workgroup_size(64) fn Deform(@builtin(global_invocation_id) Invocation:vec3u)
{
    let Index=Invocation.x;
    if(Index>=arrayLength(&Rest))
    {
        return;
    }
    var Position=Rest[Index];
    if(Position.w>0.0)
    {
        let Fraction=clamp((Position.z+.6)/3.4,0.0,1.0);
        let Weight=Fraction*Fraction*(3.0-2.0*Fraction);
        let Amount=ParametersEntry.Settings.x;
        Position=vec4f(Position.x*(1.0+.14*Amount*Weight)+.14*Amount*Weight*sin(Position.z*3.2),             Position.y+Amount*Weight*(.15*sin(Position.z*4.2)+.11*cos(Position.x*2.7)),Position.z-Amount*Weight,Position.w);
    }
    Vertices[Index]=Position;
}
` + EarlierDeformation.slice(EarlierDeformation.indexOf('@compute @workgroup_size(64) fn Expand'));
export const Raster = Declarations + EarlierRaster.slice(EarlierDeclarations.length);
export const Traversal = Declarations + EarlierTraversal.slice(EarlierDeclarations.length);

export const Construction = Traversal + `
@group(1) @binding(0) var Output:texture_storage_3d<rgba16float,write>;
@group(1) @binding(1) var<storage,read_write> ComponentBounds:array<vec4f>;
@compute @workgroup_size(32) fn ComponentExtents(@builtin(global_invocation_id) Invocation:vec3u)
{
    if(Invocation.x>=32u)
    {
        return;
    }
    var Minimum=vec3f(1e20);
    var Maximum=vec3f(-1e20);
    for(var Index=0u;Index<arrayLength(&Facets);Index++)
    {
        let Triangle=Facets[Index];
        if(Triangle.Metadata.z!=Invocation.x+1u)
        {
            continue;
        }
        Minimum=min(Minimum,min(Triangle.First.xyz,min(Triangle.Second.xyz,Triangle.Third.xyz))-.00001);
        Maximum=max(Maximum,max(Triangle.First.xyz,max(Triangle.Second.xyz,Triangle.Third.xyz))+.00001);
    }
    ComponentBounds[Invocation.x*2u]=vec4f(Minimum,0);
    ComponentBounds[Invocation.x*2u+1u]=vec4f(Maximum,0);
}
fn SegmentDistance(Point:vec3f,First:vec3f,Second:vec3f)->f32
{
    let Edge=Second-First;
    return length(Point-First-Edge*clamp(dot(Point-First,Edge)/dot(Edge,Edge),0.0,1.0));
}
fn FacetDistance(Point:vec3f,Triangle:Facet)->f32
{
    let First=Triangle.First.xyz;
    let Second=Triangle.Second.xyz;
    let Third=Triangle.Third.xyz;
    let Normal=cross(Second-First,Third-First);
    if(dot(cross(Second-First,Point-First),Normal)>=0.0&&dot(cross(Third-Second,Point-Second),Normal)>=0.0&&dot(cross(First-Third,Point-Third),Normal)>=0.0)
    {
        return abs(dot(Point-First,normalize(Normal)));
    }
    return min(SegmentDistance(Point,First,Second),min(SegmentDistance(Point,Second,Third),SegmentDistance(Point,Third,First)));
}
fn BoundsDistance(Point:vec3f,Bound:Extent)->f32
{
    let Outside=max(max(Bound.Minimum-Point,Point-Bound.Maximum),vec3f(0));
    return dot(Outside,Outside);
}
fn Nearest(Point:vec3f,Mask:u32)->vec3f
{
    var Distance=100.0;
    var Material=0u;
    var Component=0u;
    var Stack:array<u32,32>;
    var Count=1u;
    Stack[0]=0u;
    loop
    {
        if(Count==0u)
        {
            break;
        }
        Count--;
        let Address=Stack[Count];
        let Bound=Bounds[Address];
        if(Bound.Escape.w==0u||(Mask!=0u&&(Bound.Escape.w&Mask)==0u))
        {
            continue;
        }
        if(BoundsDistance(Point,Bound)>Distance*Distance)
        {
            continue;
        }
        if(Bound.Count==0u)
        {
            let Left=Bound.Escape.y;
            let Right=Bound.Escape.z;
            let LeftDistance=BoundsDistance(Point,Bounds[Left]);
            let RightDistance=BoundsDistance(Point,Bounds[Right]);
            let Near=select(Right,Left,LeftDistance<RightDistance);
            let Far=select(Left,Right,LeftDistance<RightDistance);
            Stack[Count]=Far;
            Stack[Count+1u]=Near;
            Count+=2u;
            continue;
        }
        for(var Offset=0u;Offset<Bound.Count;Offset++)
        {
            let Triangle=Facets[Bound.Start+Offset];
            if(Triangle.Metadata.z==0u)
            {
                continue;
            }
            if(Mask!=0u&&((1u<<(Triangle.Metadata.z-1u))&Mask)==0u)
            {
                continue;
            }
            let Reading=FacetDistance(Point,Triangle);
            if(Reading<Distance)
            {
                Distance=Reading;
                Material=Triangle.Metadata.y;
                Component=Triangle.Metadata.z;
            }
        }
    }
    return vec3f(Distance,f32(Material),f32(Component));
}
fn Interior(Point:vec3f)->u32
{
    var Candidates=0u;
    for(var Component=0u;Component<32u;Component++)
    {
        if(all(Point>=ComponentBounds[Component*2u].xyz)&&all(Point<=ComponentBounds[Component*2u+1u].xyz))
        {
            Candidates|=1u<<Component;
        }
    }
    if(Candidates==0u)
    {
        return 0u;
    }
    let Direction=normalize(vec3f(1,.37139,.17321));
    let Inverse=1.0/Direction;
    var Parity=0u;
    var Address=0u;
    loop
    {
        if(Address>=arrayLength(&Bounds))
        {
            break;
        }
        let Bound=Bounds[Address];
        if((Bound.Escape.w&Candidates)==0u)
        {
            Address=Bound.Escape.x;
            continue;
        }
        let First=(Bound.Minimum-Point)*Inverse;
        let Second=(Bound.Maximum-Point)*Inverse;
        let Near=min(First,Second);
        let Far=max(First,Second);
        if(max(max(Near.x,Near.y),max(Near.z,0.0))>min(min(Far.x,Far.y),Far.z))
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
            let Triangle=Facets[Bound.Start+Offset];
            if(Triangle.Metadata.z==0u)
            {
                continue;
            }
            if(((1u<<(Triangle.Metadata.z-1u))&Candidates)==0u)
            {
                continue;
            }
            let Edge=Triangle.Second.xyz-Triangle.First.xyz;
            let Other=Triangle.Third.xyz-Triangle.First.xyz;
            let Perpendicular=cross(Direction,Other);
            let Determinant=dot(Edge,Perpendicular);
            if(abs(Determinant)<1e-9)
            {
                continue;
            }
            let Relative=Point-Triangle.First.xyz;
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
            if(dot(Other,Tangent)/Determinant>1e-6)
            {
                Parity^=1u<<(Triangle.Metadata.z-1u);
            }
        }
        Address=Bound.Escape.x;
    }
    return Parity;
}
@compute @workgroup_size(4,4,4) fn Construct(@builtin(global_invocation_id) Invocation:vec3u)
{
    let Resolution=ParametersEntry.Field.x;
    if(any(Invocation>=vec3u(Resolution)))
    {
        return;
    }
    let Point=mix(ParametersEntry.DomainMinimum.xyz,ParametersEntry.DomainMaximum.xyz,vec3f(Invocation)/f32(Resolution-1u));
    let Inside=Interior(Point);
    var Closest=vec3f(0);
    if(Inside==0u)
    {
        Closest=Nearest(Point,0u);
    }
    else
    {
        // Standard CSG min of component signed distances, rather than a nearest internal overlap face.
        for(var Component=0u;Component<32u;Component++)
        {
            let Mask=1u<<Component;
            if((Inside&Mask)==0u)
            {
                continue;
            }
            let Candidate=Nearest(Point,Mask);
            if(Candidate.x>Closest.x)
            {
                Closest=Candidate;
            }
        }
    }
    let Signed=select(Closest.x,-Closest.x,Inside!=0u);
    textureStore(Output,vec3i(Invocation),vec4f(Signed,Closest.yz,1));
}
`;

export const FieldQueries = Traversal + `
@group(2) @binding(0) var DistanceTexture:texture_3d<f32>;
@group(2) @binding(1) var LinearSampler:sampler;
struct Work
{
    Marches:atomic<u32>, Steps:atomic<u32>, Triangles:atomic<u32>, Exhausted:atomic<u32>
}
@group(2) @binding(2) var<storage,read_write> WorkCounters:Work;
fn Palette(Material:i32)->vec3f
{
    switch Material
    {
        case 0:
        {
            return vec3f(.18,.48,.56);
        }
        case 1:
        {
            return vec3f(.27,.3,.32);
        }
        case 2:
        {
            return vec3f(.72,.23,.07);
        }
        case 3:
        {
            return vec3f(.52,.53,.5);
        }
        case 4:
        {
            return vec3f(.065,.19,.25);
        }
        case 5:
        {
            return vec3f(.36,.12,.055);
        }
        default:
        {
            return vec3f(.055,.065,.075);
        }
    }
}
fn CellWidth()->f32
{
    let Step=(ParametersEntry.DomainMaximum.xyz-ParametersEntry.DomainMinimum.xyz)/f32(ParametersEntry.Field.x-1u);
    return max(Step.x,max(Step.y,Step.z));
}
fn BoxDistance(Point:vec3f,Centre:vec3f,Half:vec3f)->f32
{
    let Relative=abs(Point-Centre)-Half;
    return length(max(Relative,vec3f(0)))+min(max(Relative.x,max(Relative.y,Relative.z)),0.0);
}
fn DynamicDistance(Point:vec3f)->vec2f
{
    let Minimum=ParametersEntry.DomainMinimum.xyz;
    let Maximum=ParametersEntry.DomainMaximum.xyz;
    let Outside=max(max(Minimum-Point,Point-Maximum),vec3f(0));
    if(any(Outside>vec3f(0)))
    {
        return vec2f(length(Outside)+CellWidth()*.1,0);
    }
    let Resolution=f32(ParametersEntry.Field.x);
    let Coordinate=(Point-Minimum)/(Maximum-Minimum);
    let TextureCoordinate=(Coordinate*(Resolution-1.0)+.5)/Resolution;
    let Distance=textureSampleLevel(DistanceTexture,LinearSampler,TextureCoordinate,0).x;
    let Address=clamp(vec3i(round(Coordinate*(Resolution-1.0))),vec3i(0),vec3i(i32(Resolution)-1));
    return vec2f(Distance,textureLoad(DistanceTexture,Address,0).y);
}
fn WorldDistance(Point:vec3f)->vec2f
{
    var Result=DynamicDistance(Point);
    let Floor=BoxDistance(Point,vec3f(0,-.15,0),vec3f(5,.15,5));
    if(Floor<Result.x)
    {
        Result=vec2f(Floor,3);
    }
    let Backdrop=BoxDistance(Point,vec3f(0,1.7,-4),vec3f(5,1.7,.1));
    if(Backdrop<Result.x)
    {
        Result=vec2f(Backdrop,4);
    }
    let Side=BoxDistance(Point,vec3f(-4,1.25,0),vec3f(.1,1.25,4));
    if(Side<Result.x)
    {
        Result=vec2f(Side,5);
    }
    return Result;
}
fn FieldNormal(Point:vec3f)->vec3f
{
    let Step=CellWidth()*.3;
    let Gradient=vec3f(WorldDistance(Point+vec3f(Step,0,0)).x-WorldDistance(Point-vec3f(Step,0,0)).x,         WorldDistance(Point+vec3f(0,Step,0)).x-WorldDistance(Point-vec3f(0,Step,0)).x,         WorldDistance(Point+vec3f(0,0,Step)).x-WorldDistance(Point-vec3f(0,0,Step)).x);
    return select(vec3f(0,1,0),normalize(Gradient),dot(Gradient,Gradient)>1e-12);
}
struct SurfaceHit
{
    Distance:f32, Material:i32, Normal:vec3f, Steps:u32, Exhausted:u32
}
fn March(Origin:vec3f,Direction:vec3f,Limit:f32,Visibility:bool)->SurfaceHit
{
    if(ParametersEntry.Field.z>0u)
    {
        atomicAdd(&WorkCounters.Marches,1u);
    }
    let Epsilon=CellWidth()*.025;
    var Distance=0.0;
    var PreviousDistance=0.0;
    var PreviousSigned=WorldDistance(Origin).x;
    var Count=0u;
    loop
    {
        if(Count>=ParametersEntry.Options.x||Distance>=Limit)
        {
            break;
        }
        Count++;
        let Signed=WorldDistance(Origin+Direction*Distance);
        if(abs(Signed.x)<Epsilon||(Signed.x<0.0&&PreviousSigned>0.0))
        {
            if(Signed.x<0.0&&PreviousSigned>0.0)
            {
                var Lower=PreviousDistance;
                var Upper=Distance;
                for(var Iteration=0u;Iteration<7u;Iteration++)
                {
                    let Middle=(Lower+Upper)*.5;
                    if(WorldDistance(Origin+Direction*Middle).x>0.0)
                    {
                        Lower=Middle;
                    }
                    else
                    {
                        Upper=Middle;
                    }
                }
                Distance=(Lower+Upper)*.5;
            }
            if(ParametersEntry.Field.z>0u)
            {
                atomicAdd(&WorkCounters.Steps,Count);
            }
            var Normal=vec3f(0,1,0);
            if(!Visibility)
            {
                Normal=FieldNormal(Origin+Direction*Distance);
            }
            return SurfaceHit(Distance,i32(Signed.y),Normal,Count,0u);
        }
        // 📝 A shadow origin inside the reconstructed solid is occluded; primary rays normally start outside it.
        if(Visibility&&Signed.x<0.0)
        {
            if(ParametersEntry.Field.z>0u)
            {
                atomicAdd(&WorkCounters.Steps,Count);
            }
            return SurfaceHit(Distance,i32(Signed.y),vec3f(0,1,0),Count,0u);
        }
        PreviousDistance=Distance;
        PreviousSigned=Signed.x;
        Distance+=max(abs(Signed.x)*.75,CellWidth()*.0125);
    }
    let Exhausted=select(0u,1u,Distance<Limit);
    if(ParametersEntry.Field.z>0u)
    {
        atomicAdd(&WorkCounters.Steps,Count);
        atomicAdd(&WorkCounters.Exhausted,Exhausted);
    }
    return SurfaceHit(-1.0,-1,vec3f(0),Count,Exhausted);
}
fn TriangleRay(Origin:vec3f,Direction:vec3f,Limit:f32,Visibility:bool)->SurfaceHit
{
    if(ParametersEntry.Field.z>0u)
    {
        atomicAdd(&WorkCounters.Triangles,1u);
    }
    let Hit=Trace(Origin,Direction,Limit,Visibility);
    if(Hit.Index<0)
    {
        return SurfaceHit(-1,-1,vec3f(0),Hit.Visits,0u);
    }
    let Triangle=Facets[u32(Hit.Index)];
    return SurfaceHit(Hit.Distance,i32(Triangle.Metadata.y),select(Triangle.Normal.xyz,-Triangle.Normal.xyz,dot(Triangle.Normal.xyz,Direction)>0.0),Hit.Visits,0u);
}
fn VisibilityRay(Origin:vec3f,Direction:vec3f,Limit:f32,Reference:bool)->bool
{
    if(Reference)
    {
        return TriangleRay(Origin,Direction,Limit,true).Material>=0;
    }
    let Hit=March(Origin,Direction,Limit,true);
    // 📝 Exhaustion is not quietly converted into visible light: fail dark for shadow rays.
    return Hit.Material>=0||Hit.Exhausted>0u;
}
fn Direct(Point:vec3f,Normal:vec3f,Material:i32,Reference:bool)->vec3f
{
    var Result=vec3f(0);
    let Bias=select(CellWidth()*ParametersEntry.DomainMinimum.w,.002,Reference);
    for(var Light=0u;Light<2u;Light++)
    {
        let Position=select(vec3f(-3.5,6,4.5),vec3f(4,3,-3),Light>0u);
        let Emission=select(vec3f(85,75,60),vec3f(24,40,65),Light>0u);
        let Delta=Position-Point;
        let Distance=length(Delta);
        let Direction=Delta/Distance;
        let Cosine=max(0.0,dot(Normal,Direction));
        if(Cosine>0.0&&!VisibilityRay(Point+Normal*Bias,Direction,Distance-Bias,Reference))
        {
            Result+=Palette(Material)*Emission*(ParametersEntry.Settings.y*Cosine/(3.14159265*(1.0+Distance*Distance)));
        }
    }
    return Result;
}
fn Gather(Point:vec3f,Normal:vec3f,Material:i32,Reference:bool)->vec3f
{
    let Tangent=normalize(cross(select(vec3f(0,1,0),vec3f(1,0,0),abs(Normal.y)>.9),Normal));
    let Bitangent=cross(Normal,Tangent);
    let Count=ParametersEntry.Viewport.w;
    var Sum=vec3f(0);
    let Bias=select(CellWidth()*ParametersEntry.DomainMinimum.w,.002,Reference);
    for(var Sample=0u;Sample<Count;Sample++)
    {
        let Radius=sqrt((f32(Sample)+.5)/f32(Count));
        let Angle=f32(Sample)*2.39996323;
        let Direction=normalize(Tangent*(Radius*cos(Angle))+Bitangent*(Radius*sin(Angle))+Normal*sqrt(1.0-Radius*Radius));
        var Hit:SurfaceHit;
        if(Reference)
        {
            Hit=TriangleRay(Point+Normal*Bias,Direction,15.0,false);
        }
        else
        {
            Hit=March(Point+Normal*Bias,Direction,15.0,false);
        }
        if(Hit.Material>=0)
        {
            Sum+=Direct(Point+Normal*Bias+Direction*Hit.Distance,Hit.Normal,Hit.Material,Reference);
        }
    }
    return Palette(Material)*Sum/f32(Count);
}
`;

export const Shading = FieldQueries + `
@group(1) @binding(0) var PositionTexture:texture_2d<f32>;
@group(1) @binding(1) var NormalTexture:texture_2d<f32>;
@group(1) @binding(2) var ColourTexture:texture_2d<f32>;
@group(1) @binding(3) var Result:texture_storage_2d<rgba16float,write>;
fn CameraDirection(Pixel:vec2u)->vec3f
{
    let Coordinate=(vec2f(Pixel)+.5)/vec2f(ParametersEntry.Viewport.xy);
    let Clip=vec4f(Coordinate.x*2.0-1.0,1.0-Coordinate.y*2.0,1,1);
    let World=ParametersEntry.InverseProjection*Clip;
    return normalize(World.xyz/World.w-ParametersEntry.Camera.xyz);
}
@compute @workgroup_size(8,8) fn Shade(@builtin(global_invocation_id) Invocation:vec3u)
{
    if(any(Invocation.xy>=ParametersEntry.Viewport.xy))
    {
        return;
    }
    let Pixel=vec2i(Invocation.xy);
    let View=ParametersEntry.Viewport.z;
    if(View==3u)
    {
        var Coordinate=(vec2f(Invocation.xy)+.5)/vec2f(ParametersEntry.Viewport.xy);
        let Axis=ParametersEntry.Field.y;
        let Extent=ParametersEntry.DomainMaximum.xyz-ParametersEntry.DomainMinimum.xyz;
        let WorldAspect=select(select(Extent.x/Extent.z,Extent.z/Extent.y,Axis==0u),Extent.x/Extent.y,Axis==2u);
        let ScreenAspect=f32(ParametersEntry.Viewport.x)/f32(ParametersEntry.Viewport.y);
        if(ScreenAspect>WorldAspect)
        {
            Coordinate.x=(Coordinate.x-.5)*ScreenAspect/WorldAspect+.5;
        }
        else
        {
            Coordinate.y=(Coordinate.y-.5)*WorldAspect/ScreenAspect+.5;
        }
        if(any(Coordinate<vec2f(0))||any(Coordinate>vec2f(1)))
        {
            textureStore(Result,Pixel,vec4f(.009,.015,.024,1));
            return;
        }
        var Fraction=vec3f(Coordinate.x,ParametersEntry.Settings.w,1.0-Coordinate.y);
        if(Axis==0u)
        {
            Fraction=vec3f(ParametersEntry.Settings.w,1.0-Coordinate.y,Coordinate.x);
        }
        if(Axis==2u)
        {
            Fraction=vec3f(Coordinate.x,1.0-Coordinate.y,ParametersEntry.Settings.w);
        }
        let Point=mix(ParametersEntry.DomainMinimum.xyz,ParametersEntry.DomainMaximum.xyz,Fraction);
        let Signed=DynamicDistance(Point).x;
        let Cells=Signed/CellWidth();
        var Colour=select(vec3f(.12,.48,.9),vec3f(1,.24,.09),Signed<0.0)*( .25+.75*exp(-abs(Cells)*.2));
        if(abs(Cells)<.07)
        {
            Colour=vec3f(.95);
        }
        let Contour=abs(fract(abs(Cells))-.5);
        Colour*=select(.65,1.0,Contour>.035);
        textureStore(Result,Pixel,vec4f(Colour,1));
        return;
    }
    let Direction=CameraDirection(Invocation.xy);
    if(View==4u)
    {
        let FieldHit=March(ParametersEntry.Camera.xyz,Direction,30.0,false);
        let TriangleHit=TriangleRay(ParametersEntry.Camera.xyz,Direction,30.0,false);
        var Colour=vec3f(.01,.02,.03);
        if(FieldHit.Material>=0&&TriangleHit.Material>=0)
        {
            let Error=clamp(abs(FieldHit.Distance-TriangleHit.Distance)/(CellWidth()*2.0),0.0,1.0);
            Colour=mix(vec3f(.08,.65,.25),vec3f(1,.7,.02),Error);
            if(FieldHit.Distance-TriangleHit.Distance>CellWidth()*2.0)
            {
                Colour=vec3f(1,.08,.16);
            }
            if(TriangleHit.Distance-FieldHit.Distance>CellWidth()*2.0)
            {
                Colour=vec3f(.08,.4,1);
            }
        }
        else if(FieldHit.Material<0&&TriangleHit.Material>=0)
        {
            Colour=vec3f(1,.08,.16);
        }
        else if(FieldHit.Material>=0)
        {
            Colour=vec3f(.08,.4,1);
        }
        if(FieldHit.Exhausted>0u)
        {
            Colour=vec3f(1,0,1);
        }
        textureStore(Result,Pixel,vec4f(Colour,1));
        return;
    }
    var Point=vec3f(0);
    var Normal=vec3f(0);
    var Material=-1;
    if(View==1u||View==2u||View==5u)
    {
        let Hit=March(ParametersEntry.Camera.xyz,Direction,30.0,false);
        if(View==5u)
        {
            let Cost=clamp(f32(Hit.Steps)/f32(ParametersEntry.Options.x),0.0,1.0);
            let Colour=select(vec3f(Cost,1.0-abs(Cost-.5)*2.0,1.0-Cost),vec3f(1,0,1),Hit.Exhausted>0u);
            textureStore(Result,Pixel,vec4f(Colour,1));
            return;
        }
        Point=ParametersEntry.Camera.xyz+Direction*Hit.Distance;
        Normal=Hit.Normal;
        Material=Hit.Material;
        if(Hit.Exhausted>0u)
        {
            textureStore(Result,Pixel,vec4f(1,0,1,1));
            return;
        }
    }
    else
    {
        let Position=textureLoad(PositionTexture,Pixel,0);
        if(Position.w>0.0)
        {
            Point=Position.xyz;
            Normal=normalize(textureLoad(NormalTexture,Pixel,0).xyz);
            Material=i32(Facets[u32(Position.w)-1u].Metadata.y);
        }
    }
    if(Material<0)
    {
        textureStore(Result,Pixel,vec4f(.009,.015,.024,1));
        return;
    }
    if(View==2u||View==8u)
    {
        var Colour=Normal*.5+.5;
        if(View==8u)
        {
            Colour=mix(Colour,vec3f(.02),textureLoad(ColourTexture,Pixel,0).w*.75);
        }
        textureStore(Result,Pixel,vec4f(Colour,1));
        return;
    }
    let Reference=View==7u;
    var Colour=Direct(Point,Normal,Material,Reference);
    if(View==0u||View==6u||View==7u)
    {
        let Indirect=Gather(Point,Normal,Material,Reference);
        Colour=select(Colour+Indirect,Indirect,View==6u);
    }
    textureStore(Result,Pixel,vec4f(Colour,1));
}
`;

export const Presentation = Declarations + `
@group(0) @binding(0) var<uniform> ParametersEntry:Parameters;
@group(1) @binding(0) var Image:texture_2d<f32>;
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
    let Dimensions=vec2i(ParametersEntry.Viewport.xy);
    let Pixel=clamp(vec2i(Input.Coordinate*vec2f(Dimensions)),vec2i(0),Dimensions-1);
    var Colour=textureLoad(Image,Pixel,0).xyz;
    let View=ParametersEntry.Viewport.z;
    if(View==0u||View==1u||View==6u||View==7u||View==9u)
    {
        Colour*=ParametersEntry.Settings.z;
        Colour=(Colour*(2.51*Colour+.03))/(Colour*(2.43*Colour+.59)+.14);
        Colour=pow(clamp(Colour,vec3f(0),vec3f(1)),vec3f(1.0/2.2));
    }
    return vec4f(Colour,1);
}
`;
