//============================================================================================================================================
//                                                          PROBEINTEGRATOR.JS
//============================================================================================================================================
// 📦 Full-triangle raster captures, directional irradiance integration and depth-aware dynamic diffuse transport.

import * as THREE from 'three';
import { ProbeScheduler } from './ProbeScheduler.js';
import { OrbitControls } from '../Ocean/lib/addons/OrbitControls.js';

const Element = Name => document.getElementById(Name);
const Parameters = { Gi: true, Motion: true, Frozen: false, Amplitude: .45, Position: 0, Emission: 8, Sun: .35,
    Recursive: true, Visibility: true, Prioritized: true, Burst: true, Budget: 4, Display: 0, Capture: 0, Inspect: 22 };
const Grid = new THREE.Vector3(4, 3, 4), Origin = new THREE.Vector3(-2.4, .35, -2.4);
const Spacing = new THREE.Vector3(1.6, 1.15, 1.6), ProbeCount = 48, TileSize = 16, AtlasColumns = 8;
const AtlasWidth = 128, AtlasHeight = 96, FaceSize = 32;
let Renderer, Scene, Camera, Controls, ShadowCamera, ShadowTarget, ShadowMaterial, Ribbon, MarkerGroup;
let CubeTarget, InspectTarget, CubeCamera, Quad, QuadScene, QuadCamera, Integrator, Copier, Inspector;
let ReadSlot = 0, Cursor = 0, Sweeps = 0, Phase = 0, LastFrame = 0, FrameAverage = 16, CaptureStamp = 0;
let Alive = true, SweepStart = performance.now(), RefreshMilliseconds = 0, InspectionReady = false;
const Materials = [], Probes = [], Radiance = [], Moments = [];
let Schedule = new ProbeScheduler(ProbeCount), FrameNumber = 0, BurstRemaining = 0, ShadowDirty = true;
let LastSelection = [], ShadowDrawn = false, InspectorRequested = false, RevisionStart = 0;
let LastChange = 'Initial capture', LastSignature = '', FrameRevision = -1, FirstResponse = null;
const ChangedBounds = new THREE.Box3(), PreviousBounds = new THREE.Box3();
const ViewFrustum = new THREE.Frustum(), ViewProjection = new THREE.Matrix4();
const ProbeSphere = new THREE.Sphere(new THREE.Vector3(), 1.0);
let PriorityWeights = [];
const SunDirection = new THREE.Vector3(.5, 1, .45).normalize();
const ShadowTransform = new THREE.Matrix4();
const VertexSource = `
varying vec3 WorldPosition;
varying vec3 WorldNormal;
void main()
{
    vec4 Position = modelMatrix * vec4(position, 1.0);
    WorldPosition = Position.xyz;
    WorldNormal = normalize(mat3(modelMatrix) * normal);
    gl_Position = projectionMatrix * viewMatrix * Position;
}`;
const ProbeFunctions = `
uniform highp sampler2D IrradianceAtlas;
uniform highp sampler2D MomentAtlas;
uniform int VisibilityEnabled;
vec2 EncodeDirection(vec3 Direction)
{
    Direction /= abs(Direction.x) + abs(Direction.y) + abs(Direction.z);
    vec2 Coordinate = Direction.xy;
    if (Direction.z < 0.0) Coordinate = (1.0 - abs(Coordinate.yx)) * mix(vec2(-1.0), vec2(1.0), step(vec2(0.0), Coordinate));
    return Coordinate * .5 + .5;
}
vec3 DecodeDirection(vec2 Coordinate)
{
    vec3 Direction = vec3(Coordinate * 2.0 - 1.0, 1.0 - abs(Coordinate.x * 2.0 - 1.0) - abs(Coordinate.y * 2.0 - 1.0));
    if (Direction.z < 0.0) Direction.xy = (1.0 - abs(Direction.yx)) * mix(vec2(-1.0), vec2(1.0), step(vec2(0.0), Direction.xy));
    return normalize(Direction);
}
vec2 AtlasCoordinate(int Index, vec3 Direction)
{
    vec2 Tile = vec2(float(Index - (Index / 8) * 8), float(Index / 8));
    return (Tile * 16.0 + .5 + EncodeDirection(Direction) * 15.0) / vec2(128.0, 96.0);
}
vec3 InterpolateIrradiance(vec3 Position, vec3 Normal)
{
    vec3 Coordinate = clamp((Position - vec3(-2.4, .35, -2.4)) / vec3(1.6, 1.15, 1.6), vec3(0.0), vec3(3.0, 2.0, 3.0));
    ivec3 Lower = min(ivec3(floor(Coordinate)), ivec3(2, 1, 2));
    vec3 Fraction = Coordinate - vec3(Lower), Sum = vec3(0.0);
    float Total = 0.0;
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        ivec3 Offset = ivec3(Corner & 1, (Corner >> 1) & 1, (Corner >> 2) & 1);
        ivec3 Cell = Lower + Offset;
        int Index = Cell.x + Cell.y * 4 + Cell.z * 12;
        vec3 Probe = vec3(-2.4, .35, -2.4) + vec3(Cell) * vec3(1.6, 1.15, 1.6);
        vec3 Delta = Position + Normal * .04 - Probe;
        float Distance = length(Delta);
        vec3 Direction = Delta / max(Distance, .0001);
        vec4 Depth = texture2D(MomentAtlas, AtlasCoordinate(Index, Direction));
        if (Depth.a < .5) continue;
        float Visibility = 1.0;
        if (VisibilityEnabled == 1 && Distance > Depth.x + .06)
        {
            float Variance = max(Depth.y - Depth.x * Depth.x, .002);
            float Difference = Distance - Depth.x - .06;
            Visibility = Variance / (Variance + Difference * Difference);
            Visibility = Visibility * Visibility * Visibility;
            if (Visibility < .015) continue;
        }
        vec3 Blend = mix(vec3(1.0) - Fraction, Fraction, vec3(Offset));
        float Facing = max(.03, (dot(Normal, -Direction) + .25) / 1.25);
        float Weight = Blend.x * Blend.y * Blend.z * Facing * Facing * Visibility;
        Sum += texture2D(IrradianceAtlas, AtlasCoordinate(Index, Normal)).rgb * Weight;
        Total += Weight;
    }
    return Total > .00001 ? Sum / Total : vec3(0.0);
}`;
const FragmentSource = `
precision highp float;
varying vec3 WorldPosition;
varying vec3 WorldNormal;
uniform vec3 BaseColour;
uniform vec3 Emission;
uniform vec3 ProbePosition;
uniform int CapturePass;
uniform int Pattern;
uniform int GiEnabled;
uniform int DisplayMode;
uniform float SunStrength;
uniform highp sampler2D ShadowDepth;
uniform mat4 ShadowMatrix;
${ProbeFunctions}
float SolarVisibility(vec3 Normal)
{
    vec4 Projected = ShadowMatrix * vec4(WorldPosition + Normal * .012, 1.0);
    vec3 Coordinate = Projected.xyz / Projected.w * .5 + .5;
    if (any(lessThan(Coordinate, vec3(0.0))) || any(greaterThan(Coordinate, vec3(1.0)))) return 0.0;
    vec3 DerivativeHorizontal = dFdx(Coordinate), DerivativeVertical = dFdy(Coordinate);
    float Determinant = DerivativeHorizontal.x * DerivativeVertical.y - DerivativeHorizontal.y * DerivativeVertical.x;
    vec2 DepthGradient = abs(Determinant) > 1e-10 ? vec2(
        DerivativeVertical.y * DerivativeHorizontal.z - DerivativeHorizontal.y * DerivativeVertical.z,
        DerivativeHorizontal.x * DerivativeVertical.z - DerivativeVertical.x * DerivativeHorizontal.z) / Determinant : vec2(0.0);
    float Visibility = 0.0;
    for (int Row = -1; Row <= 1; ++Row) for (int Column = -1; Column <= 1; ++Column)
    {
        vec2 Offset = vec2(float(Column), float(Row)) / 512.0;
        float Depth = texture2D(ShadowDepth, Coordinate.xy + Offset).r;
        Visibility += Coordinate.z + dot(DepthGradient, Offset) - .0015 <= Depth ? 1.0 : 0.0;
    }
    return Visibility / 9.0;
}
void main()
{
    vec3 Normal = normalize(WorldNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 Albedo = BaseColour;
    if (Pattern == 1)
    {
        vec2 Cell = abs(fract(WorldPosition.xz * .65) - .5);
        float Grout = smoothstep(.476, .49, max(Cell.x, Cell.y));
        Albedo *= mix(1.0, .83, Grout);
    }
    vec3 Indirect = GiEnabled == 1 ? InterpolateIrradiance(WorldPosition, Normal) : vec3(0.0);
    vec3 Direct = vec3(1.0, .94, .83) * SunStrength * max(0.0, dot(Normal, normalize(vec3(.5, 1.0, .45)))) * SolarVisibility(Normal);
    vec3 Colour = Albedo * (Direct + Indirect) + Emission;
    if (CapturePass == 1)
    {
        gl_FragColor = vec4(Colour, length(WorldPosition - ProbePosition));
        return;
    }
    if (DisplayMode == 1) Colour = Albedo * Indirect;
    if (DisplayMode == 2)
    {
        gl_FragColor = vec4(Normal * .5 + .5, 1.0);
        return;
    }
    Colour = pow(Colour / (1.0 + Colour), vec3(1.0 / 2.2));
    gl_FragColor = vec4(Colour, 1.0);
}`;
const QuadVertex = `varying vec2 Coordinate; void main(){ Coordinate = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`;
const IntegrationFragment = `
precision highp float;
varying vec2 Coordinate;
uniform highp samplerCube Capture;
uniform vec2 TileOrigin;
uniform int DepthPass;
${ProbeFunctions}
void main()
{
    vec2 Local = (gl_FragCoord.xy - TileOrigin - .5) / 15.0;
    vec3 Normal = DecodeDirection(Local);
    vec3 Tangent = normalize(cross(abs(Normal.y) < .9 ? vec3(0,1,0) : vec3(1,0,0), Normal));
    vec3 Bitangent = cross(Normal, Tangent);
    if (DepthPass == 1)
    {
        float Mean = 0.0, Square = 0.0;
        for (int Row = -1; Row <= 1; ++Row) for (int Column = -1; Column <= 1; ++Column)
        {
            vec3 Direction = normalize(Normal + .035 * (float(Column) * Tangent + float(Row) * Bitangent));
            float Depth = textureCube(Capture, Direction).a;
            Mean += Depth; Square += Depth * Depth;
        }
        gl_FragColor = vec4(Mean / 9.0, Square / 9.0, 0.0, 1.0);
        return;
    }
    vec3 Sum = vec3(0.0);
    // Cosine-distributed hemisphere quadrature directly integrates irradiance / pi.
    for (int Sample = 0; Sample < 128; ++Sample)
    {
        float Height = (float(Sample) + .5) / 128.0;
        float Angle = float(Sample) * 2.39996322973;
        float Radius = sqrt(1.0 - Height);
        vec3 Direction = Tangent * (Radius * cos(Angle)) + Bitangent * (Radius * sin(Angle)) + Normal * sqrt(Height);
        Sum += textureCube(Capture, Direction).rgb;
    }
    gl_FragColor = vec4(Sum / 128.0, 1.0);
}`;
const InspectionFragment = `
precision highp float;
varying vec2 Coordinate;
uniform highp samplerCube Capture;
uniform highp sampler2D Atlas;
uniform int Mode;
uniform int Ready;
void main()
{
    if (Ready == 0) { gl_FragColor = vec4(.075,.085,.095,1.0); return; }
    if (Mode == 2)
    {
        vec3 Colour = texture2D(Atlas, Coordinate).rgb;
        gl_FragColor = vec4(pow(Colour / (1.0 + Colour), vec3(1.0/2.2)),1.0); return;
    }
    vec2 Cell = floor(Coordinate * vec2(3.0,2.0));
    vec2 Local = fract(Coordinate * vec2(3.0,2.0));
    if (min(min(Local.x, Local.y),min(1.0-Local.x,1.0-Local.y)) < .025)
    { gl_FragColor = vec4(.14,.16,.18,1.0); return; }
    vec2 Position = Local * 2.0 - 1.0;
    int Face = int(Cell.x) + (1-int(Cell.y)) * 3;
    vec3 Direction;
    if (Face == 0) Direction = vec3(1.0, Position.y, -Position.x);
    else if (Face == 1) Direction = vec3(-1.0, Position.y, Position.x);
    else if (Face == 2) Direction = vec3(Position.x, 1.0, -Position.y);
    else if (Face == 3) Direction = vec3(Position.x, -1.0, Position.y);
    else if (Face == 4) Direction = vec3(Position.x, Position.y, 1.0);
    else Direction = vec3(-Position.x, Position.y, -1.0);
    vec4 Value = textureCube(Capture, normalize(Direction));
    vec3 Colour = Mode == 1 ? vec3(min(Value.a / 7.0, 1.0)) : pow(Value.rgb / (1.0 + Value.rgb), vec3(1.0/2.2));
    gl_FragColor = vec4(Colour,1.0);
}`;

function ConstructTarget()
{
    return new THREE.WebGLRenderTarget(AtlasWidth, AtlasHeight, { type: THREE.HalfFloatType, minFilter: THREE.LinearFilter,
        magFilter: THREE.LinearFilter, depthBuffer: false, generateMipmaps: false });
}
function ConstructMaterial(Colour, Glow = [0,0,0], Pattern = 0)
{
    const Material = new THREE.ShaderMaterial({ vertexShader: VertexSource, fragmentShader: FragmentSource, side: THREE.DoubleSide,
        uniforms: { BaseColour: {value:new THREE.Vector3(...Colour)}, Emission:{value:new THREE.Vector3(...Glow)},
            ProbePosition:{value:new THREE.Vector3()}, CapturePass:{value:0}, Pattern:{value:Pattern},
            GiEnabled:{value:1}, DisplayMode:{value:0}, SunStrength:{value:Parameters.Sun},
            ShadowDepth:{value:ShadowTarget.depthTexture}, ShadowMatrix:{value:ShadowTransform},
            IrradianceAtlas:{value:Radiance[0].texture}, MomentAtlas:{value:Moments[0].texture}, VisibilityEnabled:{value:1} } });
    Materials.push(Material);
    return Material;
}
function AppendMesh(Geometry, Material, Position)
{
    const Mesh = new THREE.Mesh(Geometry, Material);
    Mesh.position.set(...Position); Scene.add(Mesh); return Mesh;
}
function ConstructScene()
{
    Scene = new THREE.Scene();
    const Floor = ConstructMaterial([.63,.65,.65], [0,0,0], 1);
    const Neutral = ConstructMaterial([.65,.66,.64]);
    AppendMesh(new THREE.BoxGeometry(7,.15,7), Floor, [0,-.125,0]);
    AppendMesh(new THREE.BoxGeometry(.15,3.4,6.1), ConstructMaterial([.56,.045,.023]), [-3.1,1.65,-.05]);
    AppendMesh(new THREE.BoxGeometry(6.3,3.4,.15), ConstructMaterial([.025,.23,.38]), [0,1.65,-3.1]);
    AppendMesh(new THREE.BoxGeometry(1,.6,1), Neutral, [1.65,.25,-1.25]);
    AppendMesh(new THREE.SphereGeometry(.52,32,20), Neutral, [-1.6,.63,.65]);
    AppendMesh(new THREE.CylinderGeometry(.57,.65,.18,40), ConstructMaterial([.17,.19,.20]), [-1.6,.02,.65]);
    const Emitter = ConstructMaterial([0,0,0], [8,6.1,3.5]);
    const Panel = AppendMesh(new THREE.PlaneGeometry(1.65,1.1), Emitter, [.15,2.05,-2.98]);
    Panel.name = 'Emitter';
    AppendMesh(new THREE.BoxGeometry(1.83,1.28,.08), ConstructMaterial([.055,.06,.067]), [.15,2.05,-3.04]);
    Ribbon = AppendMesh(new THREE.PlaneGeometry(1.9,2.25,28,28), ConstructMaterial([.66,.15,.035]), [0,1.27,-.45]);
    Ribbon.geometry.userData.Original = Ribbon.geometry.attributes.position.array.slice();
    MarkerGroup = new THREE.Group(); Scene.add(MarkerGroup);
    for (let Depth = 0; Depth < 4; ++Depth) for (let Height = 0; Height < 3; ++Height) for (let Column = 0; Column < 4; ++Column)
    {
        const Position = new THREE.Vector3(Column,Height,Depth).multiply(Spacing).add(Origin);
        Probes.push(Position);
        const Marker = new THREE.Mesh(new THREE.SphereGeometry(.037,7,5), new THREE.MeshBasicMaterial({color:0x88bb99}));
        Marker.position.copy(Position); MarkerGroup.add(Marker);
        const Option = document.createElement('option'); Option.value = Probes.length - 1; Option.textContent = 'Probe '+Option.value.padStart(2,'0');
        Element('InspectProbe').append(Option);
    }
    MarkerGroup.visible = false;
    Element('InspectProbe').value = Parameters.Inspect;
}
function DeformGeometry()
{
    Ribbon.updateMatrixWorld(true);
    Ribbon.geometry.computeBoundingBox();
    PreviousBounds.copy(Ribbon.geometry.boundingBox).applyMatrix4(Ribbon.matrixWorld);
    const Attribute = Ribbon.geometry.attributes.position, Original = Ribbon.geometry.userData.Original;
    for (let Index = 0; Index < Attribute.count; ++Index)
    {
        const Horizontal = Original[Index*3], Vertical = Original[Index*3+1];
        const Bend = Parameters.Amplitude * Math.sin(Vertical * 2.8 + Phase * 1.6);
        Attribute.setXYZ(Index, Horizontal * Math.cos(Bend) + .13*Math.sin(Vertical*2+Phase), Vertical,
            Horizontal * Math.sin(Bend) + Parameters.Amplitude * Math.sin(Horizontal*3.0 + Vertical*2.3 + Phase*1.8));
    }
    Attribute.needsUpdate = true; Ribbon.geometry.computeVertexNormals(); Ribbon.geometry.computeBoundingSphere();
    Ribbon.position.x = Parameters.Position;
    Ribbon.updateMatrixWorld(true); Ribbon.geometry.computeBoundingBox();
    ChangedBounds.copy(Ribbon.geometry.boundingBox).applyMatrix4(Ribbon.matrixWorld).union(PreviousBounds);
    // 📝 Prioritize both vacated/new geometry and its projected floor shadow. No probes are excluded.
    if (Parameters.Sun > 0)
    {
        const Extent = ChangedBounds.clone();
        for (let Corner = 0; Corner < 8; ++Corner)
        {
            const Point = new THREE.Vector3(Corner&1?Extent.max.x:Extent.min.x,
                Corner&2?Extent.max.y:Extent.min.y, Corner&4?Extent.max.z:Extent.min.z);
            Point.addScaledVector(SunDirection, -(Point.y + .05) / SunDirection.y);
            ChangedBounds.expandByPoint(Point);
        }
    }
    ShadowDirty = true;
}
function ClearHistory()
{
    Renderer.setScissorTest(false); Renderer.setClearColor(0,0);
    for (const Target of [...Radiance,...Moments]) { Renderer.setRenderTarget(Target); Renderer.clear(); }
    Renderer.setRenderTarget(null); Cursor = 0; Sweeps = 0; SweepStart = performance.now();
    Schedule = new ProbeScheduler(ProbeCount); FrameRevision = -1;
    RequestRefresh(Parameters.Recursive ? 4 : 1, 'History cleared');
    InspectionReady = false; InspectorRequested = true;
}
function RequestRefresh(Iterations = 4, Reason = 'Scene edit')
{
    // 📝 Coalesce slider + motion invalidations within a frame, but never drop their settling requirement.
    if (FrameRevision !== FrameNumber)
    {
        Schedule.Invalidate(FrameNumber, Iterations); FrameRevision = FrameNumber;
        RevisionStart = FrameNumber; FirstResponse = null;
    }
    else Schedule.Records.forEach(Record => { Record.Remaining = Math.max(Record.Remaining, Iterations); });
    LastChange = Reason;
}
function RequestInteraction(Reason)
{
    RequestRefresh(Parameters.Recursive ? 4 : 1, Reason);
    BurstRemaining = 2;
}
function RankProbes()
{
    Camera.updateMatrixWorld(true);
    ViewProjection.multiplyMatrices(Camera.projectionMatrix, Camera.matrixWorldInverse);
    ViewFrustum.setFromProjectionMatrix(ViewProjection);
    PriorityWeights = Probes.map((Position, Index) =>
    {
        ProbeSphere.center.copy(Position);
        const Distance = ChangedBounds.isEmpty() ? 0 : ChangedBounds.distanceToPoint(Position);
        const Proximity = 180 / (1 + Distance * Distance);
        const Visible = ViewFrustum.intersectsSphere(ProbeSphere) ? 25 : 0;
        const Inspection = InspectorRequested && Index === Parameters.Inspect ? 400 : 0;
        return Proximity + Visible + Inspection;
    });
}
function ConfigureMaterials(Capture)
{
    for (const Material of Materials)
    {
        const Uniforms = Material.uniforms;
        Uniforms.CapturePass.value = Capture ? 1 : 0;
        Uniforms.GiEnabled.value = Capture ? Number(Parameters.Recursive) : Number(Parameters.Gi);
        Uniforms.DisplayMode.value = Parameters.Display;
        Uniforms.SunStrength.value = Parameters.Sun;
        Uniforms.VisibilityEnabled.value = Number(Parameters.Visibility);
        Uniforms.IrradianceAtlas.value = Radiance[ReadSlot].texture;
        Uniforms.MomentAtlas.value = Moments[ReadSlot].texture;
    }
    Scene.getObjectByName('Emitter').material.uniforms.Emission.value.set(Parameters.Emission, Parameters.Emission*.76, Parameters.Emission*.44);
}
function CopyAtlas(Source, Destination)
{
    Renderer.setRenderTarget(Destination); Renderer.setViewport(0,0,AtlasWidth,AtlasHeight); Renderer.setScissorTest(false);
    Copier.uniforms.Source.value = Source.texture; Quad.material = Copier; Renderer.render(QuadScene, QuadCamera);
}
function CaptureProbes()
{
    const Budget = Parameters.Prioritized && Parameters.Burst && BurstRemaining > 0 ? Math.min(12, Parameters.Budget * 3) : Parameters.Budget;
    LastSelection = Schedule.Select(Budget, FrameNumber, PriorityWeights, Parameters.Prioritized);
    if (!LastSelection.length) return;
    const WriteSlot = 1 - ReadSlot;
    CopyAtlas(Radiance[ReadSlot],Radiance[WriteSlot]); CopyAtlas(Moments[ReadSlot],Moments[WriteSlot]);
    ConfigureMaterials(true); MarkerGroup.visible = false;
    for (const Index of LastSelection)
    {
        const Target = Index === Parameters.Inspect ? InspectTarget : CubeTarget;
        CubeCamera.renderTarget = Target; CubeCamera.position.copy(Probes[Index]); CubeCamera.updateMatrixWorld(true);
        for (const Material of Materials) Material.uniforms.ProbePosition.value.copy(Probes[Index]);
        // Uncovered directions represent a black environment at the capture far distance.
        Renderer.setScissorTest(false);
        const Context = Renderer.getContext();
        for (let Face = 0; Face < 6; ++Face)
        {
            Renderer.setRenderTarget(Target,Face);
            Renderer.setViewport(0,0,FaceSize,FaceSize);
            // Float attachment clear preserves a radial far distance above the normalized colour range.
            Context.clearBufferfv(Context.COLOR,0,new Float32Array([0,0,0,20]));
            Renderer.clearDepth();
            Renderer.render(Scene,CubeCamera.children[Face]);
        }
        if (Index === Parameters.Inspect) { CaptureStamp = performance.now(); InspectionReady = true; InspectorRequested = false; }
        const Column = (Index % AtlasColumns)*TileSize, Row = Math.floor(Index/AtlasColumns)*TileSize;
        Integrator.uniforms.Capture.value = Target.texture;
        Integrator.uniforms.TileOrigin.value.set(Column,Row);
        Quad.material = Integrator;
        for (let Pass = 0; Pass < 2; ++Pass)
        {
            Renderer.setRenderTarget(Pass ? Moments[WriteSlot] : Radiance[WriteSlot]);
            Renderer.setViewport(Column,Row,TileSize,TileSize); Renderer.setScissor(Column,Row,TileSize,TileSize); Renderer.setScissorTest(true);
            Integrator.uniforms.DepthPass.value = Pass; Renderer.render(QuadScene,QuadCamera);
        }
        Schedule.Complete(Index, FrameNumber);
        Cursor = (Cursor + 1) % ProbeCount;
        if (FirstResponse === null) FirstResponse = FrameNumber - RevisionStart;
    }
    const Completed = Math.min(...Schedule.Records.map(Record => Record.Captures));
    if (Completed > Sweeps)
    {
        Sweeps = Completed; RefreshMilliseconds = performance.now()-SweepStart; SweepStart = performance.now();
    }
    ReadSlot = WriteSlot; Renderer.setScissorTest(false);
}
function RenderFrame(Time)
{
    if (!Alive) return;
    const Delta = LastFrame ? Math.min((Time-LastFrame)/1000,.05) : 0; LastFrame = Time;
    FrameAverage = FrameAverage*.95 + (window.RadianceDemo.LastTimestamp ? Time-window.RadianceDemo.LastTimestamp : 16)*.05;
    window.RadianceDemo.LastTimestamp = Time;
    ++FrameNumber;
    if (Parameters.Motion)
    {
        Phase += Delta;
        if (Ribbon.visible)
        {
            DeformGeometry(); RequestRefresh(Parameters.Recursive ? 4 : 1, 'Animated vertices');
        }
    }
    const Signature = [Parameters.Emission, Parameters.Sun, Parameters.Recursive, Parameters.Visibility,
        Ribbon.visible, ...Ribbon.material.uniforms.BaseColour.value.toArray()].join('|');
    if (Signature !== LastSignature)
    {
        const PreviousLighting = LastSignature.split('|');
        if (PreviousLighting[1] !== String(Parameters.Sun) || PreviousLighting[2] !== String(Parameters.Recursive))
            ChangedBounds.makeEmpty();
        else if (PreviousLighting[0] !== String(Parameters.Emission))
            ChangedBounds.setFromObject(Scene.getObjectByName('Emitter')).expandByScalar(.8);
        ShadowDirty = true; RequestRefresh(Parameters.Recursive ? 4 : 1, 'Lighting / material / visibility'); LastSignature = Signature;
    }
    Controls.update(); RankProbes(); Renderer.info.reset();
    LastSelection = []; ShadowDrawn = false;
    MarkerGroup.visible = false;
    if (ShadowDirty || !Parameters.Prioritized)
    {
        Scene.overrideMaterial = ShadowMaterial;
        Renderer.setRenderTarget(ShadowTarget); Renderer.setViewport(0,0,512,512); Renderer.setScissorTest(false);
        Renderer.setClearColor(0xffffff,1); Renderer.clear(); Renderer.render(Scene,ShadowCamera); Scene.overrideMaterial = null;
        ShadowDirty = false; ShadowDrawn = true;
    }
    if (!Parameters.Frozen)
    {
        CaptureProbes();
        BurstRemaining = Math.max(0, BurstRemaining - 1);
    }
    ConfigureMaterials(false); MarkerGroup.visible = Element('ShowProbes').checked;
    MarkerGroup.children.forEach((Marker,Index)=>Marker.material.color.setHex(Index===Parameters.Inspect?0xffd294:0x88bb99));
    const Width = Renderer.domElement.width, Height = Renderer.domElement.height, Ratio = Renderer.getPixelRatio();
    const PanelHeight = 176, ViewHeight = Height/Ratio - PanelHeight;
    Renderer.setRenderTarget(null); Renderer.setScissorTest(false); Renderer.setViewport(0,0,Width/Ratio,Height/Ratio);
    Renderer.setClearColor(new THREE.Color(.09,.105,.12),1); Renderer.clear();
    Renderer.setViewport(0,PanelHeight,Width/Ratio,ViewHeight);
    Camera.aspect = Width/Ratio/ViewHeight; Camera.updateProjectionMatrix(); Renderer.render(Scene,Camera);
    Inspector.uniforms.Capture.value = InspectTarget.texture;
    Inspector.uniforms.Atlas.value = Radiance[ReadSlot].texture;
    Inspector.uniforms.Mode.value = Parameters.Capture;
    Inspector.uniforms.Ready.value = Parameters.Capture===2 || InspectionReady ? 1 : 0;
    Quad.material = Inspector;
    Renderer.setViewport(14,23,Math.min(Width/Ratio-28,390),112); Renderer.render(QuadScene,QuadCamera);
    Element('FrameTime').textContent = FrameAverage.toFixed(1)+' MS / RAF';
    Element('DrawCount').textContent = Renderer.info.render.calls+' DRAWS';
    Element('TriangleCount').textContent = Renderer.info.render.triangles.toLocaleString()+' TRIANGLES / FRAME';
    Element('SweepCount').textContent = Sweeps;
    Element('SweepProgress').style.width = (100-Schedule.QueryPending()/ProbeCount*100)+'%';
    Element('RefreshTime').textContent = FirstResponse === null ? 'pending' : FirstResponse+' frames';
    Element('PendingCount').textContent = Schedule.QueryPending()+' / '+ProbeCount;
    Element('UpdatedCount').textContent = LastSelection.length+' probes / '+(LastSelection.length*6)+' views';
    Element('CacheState').textContent = Schedule.QueryPending() ? LastChange :
        Parameters.Prioritized ? 'Held after bounded settling' : 'Baseline recaptures unchanged probes';
    Element('CaptureAge').textContent = InspectionReady ? Math.round(Math.max(0,performance.now()-CaptureStamp))+' ms old' : 'awaiting refresh';
    Element('FrameDescription').textContent = Parameters.Frozen ? 'PROBE CACHE FROZEN · GEOMETRY STILL LIVE' :
        Parameters.Gi ? 'WORLD-SPACE PROBES · '+(Parameters.Prioritized?'CHANGE-PRIORITIZED':'ROUND-ROBIN BASELINE') : 'GI DISABLED · DIRECT LIGHT + EMISSION ONLY';
    Element('Status').textContent = Parameters.Frozen ? 'Frozen cache / stale lighting intentional' :
        LastSelection.length ? 'All contributors retained · '+(Parameters.Prioritized?'priority refresh':'round-robin refresh') :
        'Static cache held · no probe capture work';
    window.RadianceDemo.State = { Sweeps, Cursor, Phase, Gi:Parameters.Gi, Frozen:Parameters.Frozen, Triangles:Renderer.info.render.triangles,
        Calls:Renderer.info.render.calls, ShaderErrors:window.RadianceDemo.ShaderErrors, CaptureStamp,
        Frame:FrameNumber, Revision:Schedule.Revision, Pending:Schedule.QueryPending(), Updated:LastSelection.slice(),
        Weights:PriorityWeights.slice(), ProbeRevisions:Schedule.Records.map(Record=>Record.Revision),
        ShadowDrawn, BurstRemaining, FirstResponse, Mode:Parameters.Prioritized?'priority':'round-robin' };
    window.RadianceDemo.History.push({Frame:FrameNumber, Revision:Schedule.Revision, Updated:LastSelection.slice(),
        Pending:Schedule.QueryPending(), ShadowDrawn, Calls:Renderer.info.render.calls});
    if (window.RadianceDemo.History.length > 240) window.RadianceDemo.History.shift();
    requestAnimationFrame(RenderFrame);
}
function ResizeViewport()
{
    Renderer.setSize(Element('Stage').clientWidth,Element('Stage').clientHeight,false);
}
function ResetCamera()
{
    Camera.position.set(7.5,5.3,8.7); Controls.target.set(0,1,-.35); Controls.update();
}
function ConnectControls()
{
    for (const Name of ['Amplitude','Position','Emission','Sun','Budget'])
    {
        Element(Name).addEventListener('input',Event=>
        {
            Parameters[Name] = Number(Event.target.value);
            Element(Name+'Value').textContent = Name==='Budget'?Parameters[Name]+' / 48':Parameters[Name].toFixed(Name==='Emission'||Name==='Sun'?1:2)+(Name==='Amplitude'||Name==='Position'?' m':'');
            if (Name==='Amplitude'||Name==='Position') DeformGeometry();
            if (Name!=='Budget') RequestInteraction(Name+' edited');
        });
    }
    Element('GiToggle').onclick = () =>
    {
        Parameters.Gi = !Parameters.Gi; Element('GiToggle').classList.toggle('active',Parameters.Gi);
        Element('GiToggle').setAttribute('aria-pressed',Parameters.Gi); Element('GiToggle').textContent = Parameters.Gi?'GI on':'GI off';
    };
    Element('Motion').onclick = () =>
    {
        Parameters.Motion = !Parameters.Motion; Element('Motion').classList.toggle('active',Parameters.Motion);
        Element('Motion').setAttribute('aria-pressed',Parameters.Motion); Element('Motion').textContent = Parameters.Motion?'Pause motion':'Resume motion';
    };
    Element('Freeze').onclick = () =>
    {
        Parameters.Frozen = !Parameters.Frozen; Element('Freeze').classList.toggle('active',Parameters.Frozen);
        Element('Freeze').setAttribute('aria-pressed',Parameters.Frozen);
        Element('Freeze').innerHTML = Parameters.Frozen?'Resume probe updates <span>▷</span>':'Freeze probe updates <span>II</span>';
    };
    for (const Name of ['Recursive','Visibility']) Element(Name).onchange = Event => { Parameters[Name] = Event.target.checked; RequestInteraction(Name+' edited'); };
    Element('RibbonVisible').onchange = Event => { Ribbon.visible = Event.target.checked; DeformGeometry(); RequestInteraction('Ribbon visibility'); };
    Element('ScheduleMode').onchange = Event => { Parameters.Prioritized = Event.target.value === 'priority'; RequestRefresh(4,'Scheduler changed'); };
    Element('Burst').onchange = Event => { Parameters.Burst = Event.target.checked; };
    Element('ResetView').onclick = ResetCamera;
    Element('ClearCache').onclick = ClearHistory;
    Element('DisplayMode').onchange = Event => { Parameters.Display = Number(Event.target.value); };
    Element('CaptureMode').onchange = Event =>
    {
        Parameters.Capture = Number(Event.target.value);
        Element('CaptureLabel').textContent = Parameters.Capture===2?'48 OCTAHEDRAL IRRADIANCE TILES · ACTUAL GPU ATLAS':'+X / −X / +Y · −Y / +Z / −Z';
    };
    Element('InspectProbe').onchange = Event => { Parameters.Inspect = Number(Event.target.value); InspectionReady = false; InspectorRequested = true; Schedule.Request(Parameters.Inspect, FrameNumber); };
    Element('ProbeRow').onclick = () => { Element('ShowProbes').checked = !Element('ShowProbes').checked; };
    document.querySelectorAll('[data-select]').forEach(Item => Item.onclick = () =>
    {
        document.querySelectorAll('[data-select]').forEach(Other=>Other.classList.remove('selected')); Item.classList.add('selected');
        const Views = {room:[7.5,5.3,8.7],ribbon:[4,3.2,5.5],sphere:[1.5,2.5,5.4],emitter:[4,3.4,5]};
        Camera.position.set(...Views[Item.dataset.select]); Controls.target.set(0,1,-.35); Controls.update();
    });
    const Colours = [[.66,.15,.035],[.055,.58,.22],[.24,.15,.65]];
    document.querySelectorAll('[data-colour]').forEach(Button=>Button.onclick=()=>
    {
        Ribbon.material.uniforms.BaseColour.value.set(...Colours[Number(Button.dataset.colour)]);
        document.querySelectorAll('[data-colour]').forEach(Other=>Other.classList.remove('active')); Button.classList.add('active');
        RequestInteraction('Ribbon material');
        Element('SelectionLabel').textContent = 'Ribbon / diffuse '+['orange','green','violet'][Number(Button.dataset.colour)];
    });
}
function Fail(Message)
{
    Alive = false; Element('Failure').style.display = 'grid'; Element('Failure').textContent = 'Unable to start raster GI\n\n'+Message;
    Element('Status').textContent = 'Renderer stopped — see viewport diagnostic';
}
try
{
    window.RadianceDemo = { Ready:false, ShaderErrors:[], Parameters, State:{}, History:[] };
    const Canvas = Element('Viewport'), Context = Canvas.getContext('webgl2',{antialias:true,alpha:false,preserveDrawingBuffer:true});
    if (!Context) throw new Error('WebGL 2 is required. Enable browser hardware acceleration.');
    if (!Context.getExtension('EXT_color_buffer_float')) throw new Error('Floating-point render targets are required for HDR probe captures.');
    Renderer = new THREE.WebGLRenderer({canvas:Canvas,context:Context,antialias:true});
    Renderer.setPixelRatio(1); Renderer.autoClear = false; Renderer.info.autoReset = false;
    Renderer.toneMapping = THREE.NoToneMapping; Renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    Renderer.debug.onShaderError = (Gl, Program, Vertex, Fragment) =>
    {
        const Message = Gl.getProgramInfoLog(Program)+'\n'+Gl.getShaderInfoLog(Vertex)+'\n'+Gl.getShaderInfoLog(Fragment);
        window.RadianceDemo.ShaderErrors.push(Message); console.error(Message); Fail(Message);
    };
    for (let Slot = 0; Slot < 2; ++Slot) { Radiance.push(ConstructTarget()); Moments.push(ConstructTarget()); }
    ShadowTarget = new THREE.WebGLRenderTarget(512,512,{minFilter:THREE.NearestFilter,magFilter:THREE.NearestFilter});
    ShadowTarget.depthTexture = new THREE.DepthTexture(512,512,THREE.UnsignedIntType);
    ShadowMaterial = new THREE.MeshDepthMaterial({side:THREE.DoubleSide});
    ShadowCamera = new THREE.OrthographicCamera(-5,5,5,-5,.1,20); ShadowCamera.position.copy(SunDirection).multiplyScalar(8);
    ShadowCamera.lookAt(0,0,0); ShadowCamera.updateMatrixWorld(true);
    ShadowTransform.multiplyMatrices(ShadowCamera.projectionMatrix,ShadowCamera.matrixWorldInverse);
    const CubeOptions = {type:THREE.HalfFloatType,minFilter:THREE.LinearFilter,magFilter:THREE.LinearFilter,generateMipmaps:false};
    CubeTarget = new THREE.WebGLCubeRenderTarget(FaceSize,CubeOptions); InspectTarget = new THREE.WebGLCubeRenderTarget(FaceSize,CubeOptions);
    CubeCamera = new THREE.CubeCamera(.025,20,CubeTarget);
    CubeCamera.coordinateSystem = Renderer.coordinateSystem; CubeCamera.updateCoordinateSystem();
    ConstructScene(); DeformGeometry();
    QuadScene = new THREE.Scene(); QuadCamera = new THREE.Camera();
    Integrator = new THREE.ShaderMaterial({vertexShader:QuadVertex,fragmentShader:IntegrationFragment,depthTest:false,depthWrite:false,
        uniforms:{Capture:{value:CubeTarget.texture},TileOrigin:{value:new THREE.Vector2()},DepthPass:{value:0}}});
    Copier = new THREE.ShaderMaterial({vertexShader:QuadVertex,fragmentShader:'varying vec2 Coordinate; uniform highp sampler2D Source; void main(){gl_FragColor=texture2D(Source,Coordinate);}',
        depthTest:false,depthWrite:false,uniforms:{Source:{value:null}}});
    Inspector = new THREE.ShaderMaterial({vertexShader:QuadVertex,fragmentShader:InspectionFragment,depthTest:false,depthWrite:false,
        uniforms:{Capture:{value:InspectTarget.texture},Atlas:{value:Radiance[0].texture},Mode:{value:0},Ready:{value:0}}});
    Quad = new THREE.Mesh(new THREE.PlaneGeometry(2,2),Integrator); Quad.frustumCulled = false; QuadScene.add(Quad);
    Camera = new THREE.PerspectiveCamera(43,1,.05,50); Controls = new OrbitControls(Camera,Canvas);
    Controls.enableDamping = true; Controls.minDistance = 3; Controls.maxDistance = 18;
    Controls.maxPolarAngle = Math.PI*.49; ResetCamera(); ConnectControls();
    new ResizeObserver(ResizeViewport).observe(Element('Stage')); ResizeViewport(); ClearHistory();
    Canvas.addEventListener('webglcontextlost',Event=>{Event.preventDefault();Fail('The graphics context was lost. Reload to recreate the probe resources.');});
    window.RadianceDemo.Ready = true;
    window.RadianceDemo.Clear = ClearHistory;
    window.RadianceDemo.RequestRefresh = Iterations => RequestRefresh(Iterations, 'Explicit refresh');
    window.RadianceDemo.QueryScheduling = () => ({Revision:Schedule.Revision, Cursor:Schedule.Cursor,
        Records:Schedule.Records.map(Record=>({...Record}))});
    window.RadianceDemo.ReadPixels = () =>
    {
        const Width = Canvas.width, Height = Math.max(1,Canvas.height-176);
        const Pixels = new Uint8Array(Width*Height*4);
        Context.readPixels(0,176,Width,Height,Context.RGBA,Context.UNSIGNED_BYTE,Pixels);
        return {Width,Height,Pixels:Array.from(Pixels),Error:Context.getError()};
    };
    window.RadianceDemo.GeometryDigest = () => Array.from(Ribbon.geometry.attributes.position.array);
    requestAnimationFrame(RenderFrame);
}
catch (Error) { console.error(Error); Fail(Error.message); }
