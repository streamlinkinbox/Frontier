//============================================================================================================================================
//                                                             CASCADESEQUENCE.JS
//============================================================================================================================================
// 📦 Full-sphere 3D radiance intervals, spatial/angular merging, diffuse feedback and visibility-tested receiver charts.

import * as THREE from 'three';
import {ConstructCascades} from './SceneSpecification.js';

const GeometryShader=`
uniform highp sampler2D TriangleTexture;
uniform highp sampler2D SolidTexture;
uniform int SolidCount;
vec3 Corner(int Triangle, int Index) { return texelFetch(TriangleTexture,ivec2(Index,Triangle),0).xyz; }
bool BoundsHit(vec3 Origin, vec3 Direction, vec3 Minimum, vec3 Maximum, float Distance)
{
    vec3 SafeDirection=mix(vec3(1e-12),Direction,greaterThan(abs(Direction),vec3(1e-12)));
    vec3 First=(Minimum-vec3(.00001)-Origin)/SafeDirection, Second=(Maximum+vec3(.00001)-Origin)/SafeDirection;
    vec3 Near=min(First,Second), Far=max(First,Second);
    return max(max(Near.x,Near.y),max(Near.z,0.0))<=min(min(Far.x,Far.y),min(Far.z,Distance));
}
bool TraceTriangle(vec3 Origin, vec3 Direction, int Triangle, inout float Distance, out vec3 Normal)
{
    vec3 First=Corner(Triangle,0), Along=Corner(Triangle,1)-First, Across=Corner(Triangle,2)-First;
    vec3 Perpendicular=cross(Direction,Across);
    float Determinant=dot(Along,Perpendicular);
    if (abs(Determinant)<1e-8) return false;
    vec3 Offset=Origin-First;
    float AlongFraction=dot(Offset,Perpendicular)/Determinant;
    if (AlongFraction<-.000001 || AlongFraction>1.000001) return false;
    vec3 Other=cross(Offset,Along);
    float AcrossFraction=dot(Direction,Other)/Determinant;
    if (AcrossFraction<-.000001 || AlongFraction+AcrossFraction>1.000001) return false;
    float Candidate=dot(Across,Other)/Determinant;
    if (Candidate<.0001 || Candidate>=Distance) return false;
    Distance=Candidate;
    Normal=normalize(cross(Along,Across));
    return true;
}
bool InsideSolid(vec3 Position)
{
    for (int Solid=0;Solid<7;++Solid)
    {
        if (Solid>=SolidCount) break;
        vec4 Minimum=texelFetch(SolidTexture,ivec2(0,Solid),0), Maximum=texelFetch(SolidTexture,ivec2(1,Solid),0);
        if (any(lessThan(Position,Minimum.xyz)) || any(greaterThan(Position,Maximum.xyz))) continue;
        bool Inside=true;
        for (int Facet=0;Facet<12;++Facet)
        {
            int Triangle=int(Minimum.w)+Facet;
            vec3 First=Corner(Triangle,0), Normal=cross(Corner(Triangle,1)-First,Corner(Triangle,2)-First);
            if (dot(Normal,Position-First)>0.0) {Inside=false;break;}
        }
        if (Inside) return true;
    }
    return false;
}
int TraceScene(vec3 Origin, vec3 Direction, inout float Distance, out vec3 Normal)
{
    int Intersection=-1;
    for (int Solid=0;Solid<7;++Solid)
    {
        if (Solid>=SolidCount) break;
        vec4 Minimum=texelFetch(SolidTexture,ivec2(0,Solid),0), Maximum=texelFetch(SolidTexture,ivec2(1,Solid),0);
        if (!BoundsHit(Origin,Direction,Minimum.xyz,Maximum.xyz,Distance)) continue;
        for (int Facet=0;Facet<12;++Facet)
        {
            vec3 CandidateNormal;
            if (TraceTriangle(Origin,Direction,int(Minimum.w)+Facet,Distance,CandidateNormal))
            {
                Intersection=Solid;
                Normal=CandidateNormal;
            }
        }
    }
    return Intersection;
}`;

const AngularShader=`
const float Pi=3.141592653589793;
vec3 DirectionAt(int Face, ivec2 Angular, int Span)
{
    vec2 Position=(vec2(Angular)+.5)/float(Span)*2.0-1.0;
    if (Face==0) return normalize(vec3(1.0,-Position.y,-Position.x));
    if (Face==1) return normalize(vec3(-1.0,-Position.y,Position.x));
    if (Face==2) return normalize(vec3(Position.x,1.0,Position.y));
    if (Face==3) return normalize(vec3(Position.x,-1.0,-Position.y));
    if (Face==4) return normalize(vec3(Position.x,-Position.y,1.0));
    return normalize(vec3(-Position.x,-Position.y,-1.0));
}
// 📝 The denominator is positive; unary atan avoids signed-zero quadrant ambiguity.
float AreaPrimitive(vec2 Position) { return atan(Position.x*Position.y/sqrt(dot(Position,Position)+1.0)); }
float SolidAngle(ivec2 Angular,int Span)
{
    vec2 Minimum=vec2(Angular)/float(Span)*2.0-1.0, Maximum=vec2(Angular+1)/float(Span)*2.0-1.0;
    return AreaPrimitive(Maximum)-AreaPrimitive(vec2(Minimum.x,Maximum.y))
        -AreaPrimitive(vec2(Maximum.x,Minimum.y))+AreaPrimitive(Minimum);
}
ivec2 Address(ivec3 Coordinate,int Face,ivec2 Angular,ivec3 Dimensions,int Span)
{
    Coordinate=clamp(Coordinate,ivec3(0),Dimensions-1);
    return ivec2((Coordinate.x+Dimensions.x*Coordinate.z)*Span+Angular.x,(Coordinate.y*6+Face)*Span+Angular.y);
}
vec3 VolumePosition(ivec3 Coordinate,ivec3 Dimensions)
{
    return vec3(-6.0,0.0,-6.0)+(vec3(Coordinate)+.5)/vec3(Dimensions)*vec3(12.0,6.0,12.0);
}
void Decode(ivec2 Pixel,ivec3 Dimensions,int Span,out ivec3 Coordinate,out int Face,out ivec2 Angular)
{
    int Horizontal=Pixel.x/Span, Vertical=Pixel.y/Span;
    Coordinate=ivec3(Horizontal%Dimensions.x,Vertical/6,Horizontal/Dimensions.x);
    Face=Vertical%6;
    Angular=Pixel%Span;
}`;

const IrradianceShader=`
uniform highp sampler2D IrradianceTexture;
uniform bool ReceiverVisibility;
vec3 IrradianceAt(vec3 Position,vec3 Normal,bool Visibility)
{
    vec3 Lattice=(Position-vec3(-6.0,0.0,-6.0))/vec3(.75)-.5;
    ivec3 Lower=ivec3(floor(Lattice));
    vec3 Fraction=fract(Lattice), Irradiance=vec3(0.0);
    float WeightSum=0.0;
    for (int CornerIndex=0;CornerIndex<8;++CornerIndex)
    {
        ivec3 Shift=ivec3(CornerIndex&1,(CornerIndex>>1)&1,(CornerIndex>>2)&1);
        ivec3 Coordinate=clamp(Lower+Shift,ivec3(0),ivec3(15,7,15));
        vec3 Weights=mix(1.0-Fraction,Fraction,vec3(Shift));
        float Weight=Weights.x*Weights.y*Weights.z;
        ivec2 Pixel=ivec2(Coordinate.x+16*Coordinate.z,Coordinate.y*6);
        vec4 Constant=texelFetch(IrradianceTexture,Pixel,0);
        if (Constant.a<.5 || Weight<.00001) continue;
        if (Visibility)
        {
            vec3 Delta=VolumePosition(Coordinate,ivec3(16,8,16))-Position;
            if (dot(Delta,Normal)<0.0) continue;
            float Distance=length(Delta)-.001;
            vec3 HitNormal;
            if (Distance>.001 && TraceScene(Position,normalize(Delta),Distance,HitNormal)>=0) continue;
        }
        vec3 Contribution=vec3(0.0);
        for (int Axis=0;Axis<3;++Axis)
        {
            int Face=Axis*2+(Normal[Axis]<0.0?1:0);
            Contribution+=texelFetch(IrradianceTexture,Pixel+ivec2(0,Face),0).rgb*Normal[Axis]*Normal[Axis];
        }
        Irradiance+=max(Contribution,vec3(0.0))*Weight;
        WeightSum+=Weight;
    }
    return WeightSum>0.0?Irradiance/WeightSum:vec3(0.0);
}`;

const TraceShader=`
uniform ivec3 Dimensions;
uniform int AngularSpan;
uniform vec2 Interval;
uniform float BounceGain;
${GeometryShader}
${AngularShader}
${IrradianceShader}
void main()
{
    ivec3 Coordinate; int Face; ivec2 Angular;
    Decode(ivec2(gl_FragCoord.xy),Dimensions,AngularSpan,Coordinate,Face,Angular);
    vec3 Direction=DirectionAt(Face,Angular,AngularSpan);
    vec3 Origin=VolumePosition(Coordinate,Dimensions)+Direction*Interval.x;
    if (InsideSolid(Origin)) {Radiance=vec4(0.0);return;}
    float Distance=Interval.y-Interval.x;
    vec3 Normal;
    int Intersection=TraceScene(Origin,Direction,Distance,Normal);
    if (Intersection<0) {Radiance=vec4(0.0,0.0,0.0,1.0);return;}
    vec3 Emission=texelFetch(SolidTexture,ivec2(3,Intersection),0).rgb;
    vec3 Reflectance=texelFetch(SolidTexture,ivec2(2,Intersection),0).rgb;
    vec3 Reflection=vec3(0.0);
    if (BounceGain>0.0 && dot(Direction,Normal)<0.0)
        Reflection=Reflectance*IrradianceAt(Origin+Direction*Distance+Normal*.045,Normal,false)*BounceGain;
    Radiance=vec4(Emission+Reflection,0.0);
}`;

const MergeShader=`
uniform highp sampler2D IntervalTexture;
uniform highp sampler2D FarTexture;
uniform ivec3 Dimensions;
uniform ivec3 FarDimensions;
uniform int AngularSpan;
uniform bool FarEnabled;
${AngularShader}
void main()
{
    ivec2 Pixel=ivec2(gl_FragCoord.xy);
    vec4 Local=texelFetch(IntervalTexture,Pixel,0);
    if (!FarEnabled || Local.a==0.0) {Radiance=Local;return;}
    ivec3 Coordinate; int Face; ivec2 Angular;
    Decode(Pixel,Dimensions,AngularSpan,Coordinate,Face,Angular);
    vec3 Lattice=(vec3(Coordinate)+.5)/vec3(Dimensions)*vec3(FarDimensions)-.5;
    ivec3 Lower=ivec3(floor(Lattice));
    vec3 Fraction=fract(Lattice);
    vec4 Far=vec4(0.0);
    float AngularWeight=0.0;
    for (int DirectionIndex=0;DirectionIndex<4;++DirectionIndex)
    {
        ivec2 FineAngular=Angular*2+ivec2(DirectionIndex&1,DirectionIndex>>1);
        float Weight=SolidAngle(FineAngular,AngularSpan*2);
        for (int CornerIndex=0;CornerIndex<8;++CornerIndex)
        {
            ivec3 Shift=ivec3(CornerIndex&1,(CornerIndex>>1)&1,(CornerIndex>>2)&1);
            vec3 SpatialWeights=mix(1.0-Fraction,Fraction,vec3(Shift));
            vec4 Sample=texelFetch(FarTexture,Address(Lower+Shift,Face,FineAngular,FarDimensions,AngularSpan*2),0);
            Far+=Sample*SpatialWeights.x*SpatialWeights.y*SpatialWeights.z*Weight;
        }
        AngularWeight+=Weight;
    }
    Far/=AngularWeight;
    Radiance=vec4(Local.rgb+Local.a*Far.rgb,Local.a*Far.a);
}`;

const IntegrateShader=`
uniform highp sampler2D DirectionTexture;
${GeometryShader}
${AngularShader}
void main()
{
    ivec2 Pixel=ivec2(gl_FragCoord.xy);
    ivec3 Coordinate=ivec3(Pixel.x%16,Pixel.y/6,Pixel.x/16);
    int Coefficient=Pixel.y%6;
    vec3 Normal=vec3(0.0);
    Normal[Coefficient/2]=Coefficient%2==0?1.0:-1.0;
    float WeightSum=0.0;
    vec3 Integral=vec3(0.0);
    for (int DirectionIndex=0;DirectionIndex<96;++DirectionIndex)
    {
        int Face=DirectionIndex/16;
        ivec2 Angular=ivec2(DirectionIndex%4,(DirectionIndex/4)%4);
        vec3 Direction=DirectionAt(Face,Angular,4);
        float Weight=SolidAngle(Angular,4)*max(0.0,dot(Direction,Normal));
        WeightSum+=Weight;
        Integral+=texelFetch(DirectionTexture,Address(Coordinate,Face,Angular,ivec3(16,8,16),4),0).rgb*Weight;
    }
    Radiance=vec4(Integral/WeightSum,InsideSolid(VolumePosition(Coordinate,ivec3(16,8,16)))?0.0:1.0);
}`;

const ReceiverShader=`
uniform highp sampler2D ChartTexture;
uniform int ChartCount;
${GeometryShader}
${AngularShader}
${IrradianceShader}
void main()
{
    ivec2 Pixel=ivec2(gl_FragCoord.xy), Tile=Pixel/18;
    int Chart=Tile.x+Tile.y*8;
    if (Chart>=ChartCount) {Radiance=vec4(0.0);return;}
    vec2 Coordinate=clamp((vec2(Pixel%18)+.5-1.5)/15.0,0.0,1.0);
    vec3 Origin=texelFetch(ChartTexture,ivec2(0,Chart),0).xyz;
    vec3 Along=texelFetch(ChartTexture,ivec2(1,Chart),0).xyz, Across=texelFetch(ChartTexture,ivec2(2,Chart),0).xyz;
    vec3 Normal=normalize(cross(Along,Across)), Position=Origin+Along*Coordinate.x+Across*Coordinate.y;
    vec3 Reflectance=texelFetch(ChartTexture,ivec2(3,Chart),0).rgb;
    vec3 Emission=texelFetch(ChartTexture,ivec2(4,Chart),0).rgb;
    vec3 Incoming=vec3(0.0);
    if (any(greaterThan(Reflectance,vec3(0.0)))) Incoming=IrradianceAt(Position+Normal*.05,Normal,ReceiverVisibility);
    Radiance=vec4(Emission+Reflectance*Incoming,1.0);
}`;

const ScreenVertex=`in vec3 position;void main(){gl_Position=vec4(position,1.0);}`;
const ShaderPrefix=`precision highp float;precision highp int;out vec4 Radiance;\n`;
const Uniform=Value=>({value:Value});

function ConstructTexture(Width,Height,Values)
{
    const Texture=new THREE.DataTexture(new Float32Array(Values),Width,Height,THREE.RGBAFormat,THREE.FloatType);
    Texture.needsUpdate=true;
    return Texture;
}

function ConstructTarget(Width,Height,Linear=false)
{
    return new THREE.WebGLRenderTarget(Width,Height,{type:THREE.FloatType,format:THREE.RGBAFormat,depthBuffer:false,
        minFilter:Linear?THREE.LinearFilter:THREE.NearestFilter,magFilter:Linear?THREE.LinearFilter:THREE.NearestFilter});
}

export class CascadeSequence
{
    constructor(Renderer)
    {
        this.Renderer=Renderer;
        if (!Renderer.capabilities.isWebGL2 || !Renderer.extensions.has('EXT_color_buffer_float') ||
            !Renderer.extensions.has('OES_texture_float_linear')) throw new Error('WebGL 2, float render targets and float linear sampling are required.');
        this.Cascades=ConstructCascades().map(Cascade=>({...Cascade,Intervals:ConstructTarget(Cascade.Width,Cascade.Height),
            Merged:ConstructTarget(Cascade.Width,Cascade.Height)}));
        this.Irradiance=[ConstructTarget(256,48),ConstructTarget(256,48)];
        this.Empty=ConstructTexture(1,1,[0,0,0,0]);
        this.Scene=new THREE.Scene();
        this.Camera=new THREE.Camera();
        this.Rectangle=new THREE.Mesh(new THREE.PlaneGeometry(2,2));
        this.Rectangle.frustumCulled=false;
        this.Scene.add(this.Rectangle);
        const Shared={TriangleTexture:Uniform(this.Empty),SolidTexture:Uniform(this.Empty),SolidCount:Uniform(0),
            IrradianceTexture:Uniform(this.Empty),ReceiverVisibility:Uniform(true)};
        this.Shared=Shared;
        const Material=(Source,Uniforms)=>new THREE.RawShaderMaterial({glslVersion:THREE.GLSL3,vertexShader:ScreenVertex,
            fragmentShader:ShaderPrefix+Source,uniforms:{...Shared,...Uniforms},depthTest:false,depthWrite:false});
        this.Trace=Material(TraceShader,{Dimensions:Uniform(new THREE.Vector3()),AngularSpan:Uniform(2),
            Interval:Uniform(new THREE.Vector2()),BounceGain:Uniform(0)});
        this.Merge=Material(MergeShader,{IntervalTexture:Uniform(this.Empty),FarTexture:Uniform(this.Empty),
            Dimensions:Uniform(new THREE.Vector3()),FarDimensions:Uniform(new THREE.Vector3()),AngularSpan:Uniform(2),FarEnabled:Uniform(false)});
        this.Integrate=Material(IntegrateShader,{DirectionTexture:Uniform(this.Empty)});
        this.Receiver=Material(ReceiverShader,{ChartTexture:Uniform(this.Empty),ChartCount:Uniform(0)});
        this.Materials=[this.Trace,this.Merge,this.Integrate,this.Receiver];
        this.Revision=0;
        this.Completed=0;
    }

    RenderInto(Material,Target)
    {
        this.Rectangle.material=Material;
        this.Renderer.setRenderTarget(Target);
        this.Renderer.render(this.Scene,this.Camera);
        this.Renderer.setRenderTarget(null);
    }

    Restart(Geometry)
    {
        this.Geometry=Geometry;
        ++this.Revision;
        this.Textures?.forEach(Texture=>Texture.dispose());
        const TriangleValues=Geometry.Triangles.flatMap(Triangle=>Triangle.flatMap(Point=>[...Point,0]));
        const SolidValues=Geometry.Solids.flatMap(Solid=>[...Solid.Minimum,Solid.FirstTriangle,...Solid.Maximum,Solid.TriangleCount,
            ...Solid.Reflectance,0,...Solid.Emission,0]);
        const ChartValues=Geometry.Charts.flatMap(Chart=>[...Chart.Corners[0],0,
            ...Chart.Corners[1].map((Value,Axis)=>Value-Chart.Corners[0][Axis]),0,
            ...Chart.Corners[3].map((Value,Axis)=>Value-Chart.Corners[0][Axis]),0,...Chart.Reflectance,0,...Chart.Emission,0]);
        this.Textures=[ConstructTexture(3,Geometry.Triangles.length,TriangleValues),ConstructTexture(4,Geometry.Solids.length,SolidValues),
            ConstructTexture(5,Geometry.Charts.length,ChartValues)];
        this.Shared.TriangleTexture.value=this.Textures[0];
        this.Shared.SolidTexture.value=this.Textures[1];
        this.Shared.SolidCount.value=Geometry.Solids.length;
        this.Shared.ReceiverVisibility.value=Geometry.Settings.Visibility;
        this.Receiver.uniforms.ChartTexture.value=this.Textures[2];
        this.Receiver.uniforms.ChartCount.value=Geometry.Charts.length;
        this.FullLighting?.dispose();
        this.DirectLighting?.dispose();
        this.FullLighting=ConstructTarget(Geometry.ChartWidth,Geometry.ChartHeight,true);
        this.DirectLighting=ConstructTarget(Geometry.ChartWidth,Geometry.ChartHeight,true);
        const Colour=this.Renderer.getClearColor(new THREE.Color()), Alpha=this.Renderer.getClearAlpha();
        this.Renderer.setClearColor(0,0);
        for (const Target of [...this.Irradiance,this.FullLighting,this.DirectLighting,...this.Cascades.flatMap(Cascade=>[Cascade.Intervals,Cascade.Merged])])
        {
            this.Renderer.setRenderTarget(Target);this.Renderer.clear();
        }
        this.Renderer.setRenderTarget(null);
        this.Renderer.setClearColor(Colour,Alpha);
        this.Iteration=0;
        this.Level=3;
        this.Busy=true;
        this.Started=performance.now();
        this.Shared.IrradianceTexture.value=this.Irradiance[0].texture;
        this.Executed=[];
    }

    Advance()
    {
        if (!this.Busy) return false;
        const Cascade=this.Cascades[this.Level], Far=this.Cascades[this.Level+1];
        const Trace=this.Trace.uniforms, Merge=this.Merge.uniforms;
        Trace.Dimensions.value.fromArray(Cascade.Dimensions);
        Trace.AngularSpan.value=Cascade.AngularSpan;
        Trace.Interval.value.set(Cascade.Start,Cascade.End);
        Trace.BounceGain.value=this.Iteration?1:0;
        this.RenderInto(this.Trace,Cascade.Intervals);
        Merge.Dimensions.value.fromArray(Cascade.Dimensions);
        Merge.AngularSpan.value=Cascade.AngularSpan;
        Merge.IntervalTexture.value=Cascade.Intervals.texture;
        Merge.FarTexture.value=Far?.Merged.texture||this.Empty;
        Merge.FarDimensions.value.fromArray(Far?.Dimensions||[1,1,1]);
        Merge.FarEnabled.value=!!Far;
        this.RenderInto(this.Merge,Cascade.Merged);
        this.Executed.push({Iteration:this.Iteration+1,Level:this.Level,Samples:Cascade.Samples});
        --this.Level;
        if (this.Level<0)
        {
            this.Integrate.uniforms.DirectionTexture.value=this.Cascades[0].Merged.texture;
            this.RenderInto(this.Integrate,this.Irradiance[1]);
            this.Irradiance.reverse();
            this.Shared.IrradianceTexture.value=this.Irradiance[0].texture;
            this.RenderInto(this.Receiver,this.FullLighting);
            if (!this.Iteration) this.RenderInto(this.Receiver,this.DirectLighting);
            ++this.Iteration;
            this.Level=3;
            if (this.Iteration>=this.Geometry.Settings.Iterations)
            {
                this.Busy=false;
                this.Completed=this.Revision;
                this.Milliseconds=performance.now()-this.Started;
            }
        }
        return true;
    }

    ReadTarget(Target)
    {
        const Pixels=new Float32Array(Target.width*Target.height*4);
        this.Renderer.readRenderTargetPixels(Target,0,0,Target.width,Target.height,Pixels);
        return Pixels;
    }
}
