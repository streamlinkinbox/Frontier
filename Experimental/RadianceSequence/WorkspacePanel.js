//============================================================================================================================================
//                                                             WORKSPACEPANEL.JS
//============================================================================================================================================
// 📦 Interactive 3D triangle transport, cascade diagnostics and live direct-versus-reflected lighting comparison.

import * as THREE from 'three';
import {OrbitControls} from '../Ocean/lib/addons/OrbitControls.js';
import {ConstructScene,SceneDefaults} from './SceneSpecification.js';
import {CascadeSequence} from './CascadeSequence.js';

const Control=Name=>document.getElementById(Name);
const Settings={...SceneDefaults};
const Context={Selected:0,Slice:3,Column:7,Depth:7,Display:0,Error:null,Geometry:null,Ready:false};
const Scene=new THREE.Scene();
Scene.background=new THREE.Color('#232831');
const Camera=new THREE.PerspectiveCamera(42,1,.05,100);
const Renderer=new THREE.WebGLRenderer({canvas:Control('SceneCanvas'),antialias:true,preserveDrawingBuffer:true});
Renderer.setPixelRatio(Math.min(devicePixelRatio,1.5));
const Controls=new OrbitControls(Camera,Renderer.domElement);
Controls.enableDamping=true;
Controls.maxDistance=45;
Controls.minDistance=2;
const SurfaceMaterial=new THREE.RawShaderMaterial({glslVersion:THREE.GLSL3,
    uniforms:{FullLighting:{value:null},DirectLighting:{value:null},Display:{value:0},Exposure:{value:1.7}},
    vertexShader:`precision highp float;in vec3 position;in vec2 uv;uniform mat4 modelViewMatrix;uniform mat4 projectionMatrix;
        out vec2 Coordinate;void main(){Coordinate=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`,
    fragmentShader:`precision highp float;uniform sampler2D FullLighting;uniform sampler2D DirectLighting;
        uniform int Display;uniform float Exposure;in vec2 Coordinate;out vec4 Colour;
        void main()
        {
            vec3 Full=texture(FullLighting,Coordinate).rgb,Direct=texture(DirectLighting,Coordinate).rgb;
            vec3 Radiance=Display==1?Direct:(Display==2?max(Full-Direct,vec3(0.0)):Full);
            Radiance*=Exposure;
            Radiance=Radiance/(vec3(1.0)+Radiance);
            Colour=vec4(pow(max(Radiance,vec3(0.0)),vec3(1.0/2.2)),1.0);
        }`});
const Surface=new THREE.Mesh(new THREE.BufferGeometry(),SurfaceMaterial);
Scene.add(Surface);
const Samples=new THREE.Group();
Scene.add(Samples);
const SampleGeometry=new THREE.SphereGeometry(.037,7,5);
const SampleMaterial=new THREE.MeshBasicMaterial({color:'#6686ab',transparent:true,opacity:.4,depthWrite:false});
const HighlightMaterial=new THREE.MeshBasicMaterial({color:'#f9c270',depthTest:false});
const DirectionScene=new THREE.Scene();
const DirectionCamera=new THREE.Camera();
const DirectionMaterial=new THREE.RawShaderMaterial({glslVersion:THREE.GLSL3,depthTest:false,depthWrite:false,
    uniforms:{RadianceTexture:{value:null},Dimensions:{value:new THREE.Vector3()},AngularSpan:{value:2},
        Coordinate:{value:new THREE.Vector3()},Visibility:{value:false}},
    vertexShader:`in vec3 position;out vec2 Position;void main(){Position=position.xy*.5+.5;gl_Position=vec4(position,1.0);}`,
    fragmentShader:`precision highp float;precision highp int;in vec2 Position;out vec4 Colour;
        uniform sampler2D RadianceTexture;uniform ivec3 Dimensions;uniform ivec3 Coordinate;uniform int AngularSpan;uniform bool Visibility;
        void main()
        {
            vec2 TilePosition=vec2(Position.x*3.0,(1.0-Position.y)*2.0);
            ivec2 Tile=ivec2(TilePosition),Angular=clamp(ivec2(fract(TilePosition)*float(AngularSpan)),ivec2(0),ivec2(AngularSpan-1));
            int Face=Tile.x+Tile.y*3;
            ivec2 Pixel=ivec2((Coordinate.x+Dimensions.x*Coordinate.z)*AngularSpan+Angular.x,(Coordinate.y*6+Face)*AngularSpan+Angular.y);
            vec4 Interval=texelFetch(RadianceTexture,Pixel,0);
            vec3 Radiance=Visibility?vec3(Interval.a):pow(Interval.rgb/(vec3(1.0)+Interval.rgb),vec3(1.0/2.2));
            if (fract(TilePosition.x)<.012 || fract(TilePosition.y)<.02) Radiance=vec3(.12,.14,.18);
            Colour=vec4(Radiance,1.0);
        }`});
DirectionScene.add(new THREE.Mesh(new THREE.PlaneGeometry(2,2),DirectionMaterial));
let Transport;
let ResizePending=true;
let RebuildTimer;

function Fail(Message)
{
    Context.Error=Message;
    if (Transport) Transport.Busy=false;
    Control('Failure').hidden=false;
    Control('Failure').textContent=`Transport failed: ${Message}. No prebaked lighting is substituted.`;
    Control('Status').textContent='Transport failed';
    Control('StatusDot').style.background='#bb755f';
    Control('Recalculate').disabled=true;
    Control('Reset').disabled=true;
}

Renderer.debug.onShaderError=(Gl,Program,Vertex,Fragment)=>
    Fail(Gl.getProgramInfoLog(Program)||Gl.getShaderInfoLog(Fragment)||'Shader compilation failed');

function ProjectRange(Input)
{
    const Span=Number(Input.max)-Number(Input.min);
    Input.style.setProperty('--Fill',`${Span>0?(Number(Input.value)-Number(Input.min))/Span*100:0}%`);
    Input.disabled=Span<=0;
}

function FrameView()
{
    Camera.position.set(0,7.6,20.5);
    Controls.target.set(0,2,-.6);
    Controls.update();
}

function RenderSamples()
{
    Samples.clear();
    const Cascade=Transport.Cascades[Context.Selected], [Width,Height,Depth]=Cascade.Dimensions;
    Context.Slice=Math.min(Context.Slice,Height-1);
    Context.Column=Math.min(Context.Column,Width-1);
    Context.Depth=Math.min(Context.Depth,Depth-1);
    for (let Column=0;Column<Width;++Column) for (let DepthIndex=0;DepthIndex<Depth;++DepthIndex)
    {
        const Highlight=Column===Context.Column && DepthIndex===Context.Depth;
        const Sample=new THREE.Mesh(SampleGeometry,Highlight?HighlightMaterial:SampleMaterial);
        Sample.position.set(-6+(Column+.5)/Width*12,(Context.Slice+.5)/Height*6,-6+(DepthIndex+.5)/Depth*12);
        Sample.scale.setScalar(Highlight?2.5:1+Context.Selected*.35);
        Sample.visible=Highlight||Control('ShowSamples').checked;
        if (Highlight) Sample.renderOrder=10;
        Samples.add(Sample);
    }
    Control('SampleControls').innerHTML=[['Column','Sample X',Width],['Slice','Sample Y',Height],['Depth','Sample Z',Depth]].map(([Name,Label,Count])=>
        `<label class="FieldLabel" for="Sample${Name}">${Label}<output>${Context[Name]+1} / ${Count}</output></label><input id="Sample${Name}" type="range" min="0" max="${Count-1}" value="${Context[Name]}" step="1">`).join('');
    document.querySelectorAll('#SampleControls input').forEach(ProjectRange);
    for (const Name of ['Column','Slice','Depth']) Control(`Sample${Name}`).oninput=Event=>{Context[Name]=Number(Event.target.value);RenderSamples();};
    Control('CascadeDescription').textContent=`C${Cascade.Level}: ${Cascade.Dimensions.join(' × ')} spatial samples; ${6*Cascade.AngularSpan**2} directions each. Interval ${Cascade.Start.toFixed(2)}–${Cascade.End.toFixed(2)} m. ${Cascade.Samples.toLocaleString()} traced intervals.`;
    document.querySelectorAll('[data-cascade]').forEach(Button=>Button.classList.toggle('Active',Number(Button.dataset.cascade)===Context.Selected));
}

function Recalculate()
{
    clearTimeout(RebuildTimer);
    if (Context.Error) return;
    for (const [Name,Value] of Object.entries(Settings))
    {
        const Input=Control(Name);
        if (!Input) continue;
        if (Input.type==='checkbox') Input.checked=Value;
        else {Input.value=Value;ProjectRange(Input);}
        const Reading=Control(`${Name}Reading`);
        if (Reading) Reading.textContent=`${Value}${['EmitterX','OccluderX','Shear'].includes(Name)?' m':''}`;
    }
    const Geometry=ConstructScene(Settings);
    Context.Geometry=Geometry;
    Surface.geometry.dispose();
    Surface.geometry=new THREE.BufferGeometry();
    Surface.geometry.setAttribute('position',new THREE.Float32BufferAttribute(Geometry.Positions,3));
    Surface.geometry.setAttribute('uv',new THREE.Float32BufferAttribute(Geometry.Coordinates,2));
    Surface.geometry.computeBoundingSphere();
    Transport.Restart(Geometry);
    SurfaceMaterial.uniforms.FullLighting.value=Transport.FullLighting.texture;
    SurfaceMaterial.uniforms.DirectLighting.value=Transport.DirectLighting.texture;
    Context.Ready=false;
}

function BuildControls()
{
    const Fields=[['EmitterX','Emitter position X',-4,4,.1,'m'],['Power','Emission radiance',0,35,.5,''],
        ['OccluderX','Occluder position X',-3,3,.1,'m'],['Shear','Occluder top deformation',-1,1,.05,'m'],
        ['Iterations','Diffuse transport iterations',1,6,1,'']];
    Control('SceneControls').innerHTML=Fields.map(([Name,Label,Minimum,Maximum,Step,Unit])=>
        `<div class="Property"><label class="FieldLabel" for="${Name}">${Label}<output id="${Name}Reading">${Settings[Name]} ${Unit}</output></label><input id="${Name}" type="range" min="${Minimum}" max="${Maximum}" step="${Step}" value="${Settings[Name]}"></div>`).join('')+
        '<label class="Check"><input id="Visibility" type="checkbox" checked> Exact receiver-to-sample visibility</label>';
    for (const [Name,,Minimum,Maximum,,Unit] of Fields)
    {
        Control(Name).oninput=Event=>
        {
            Settings[Name]=Math.max(Minimum,Math.min(Maximum,Number(Event.target.value)));
            ProjectRange(Event.target);
            Control(`${Name}Reading`).textContent=`${Settings[Name]} ${Unit}`;
            Control('Status').textContent='Scene edited · new transport requested';
            clearTimeout(RebuildTimer);
            RebuildTimer=setTimeout(Recalculate,180);
        };
        ProjectRange(Control(Name));
    }
    Control('Visibility').checked=Settings.Visibility;
    Control('Visibility').onchange=Event=>{Settings.Visibility=Event.target.checked;Recalculate();};
}

function RenderDiagnostics()
{
    const Iteration=Transport.Iteration;
    const Intervals=Transport.Cascades.reduce((Sum,Cascade)=>Sum+Cascade.Samples,0);
    const Bytes=Intervals*2*16+256*48*16*2+Context.Geometry.ChartWidth*Context.Geometry.ChartHeight*16*2;
    Control('Metrics').innerHTML=`<span>Visible / traced triangles</span><b>${Context.Geometry.Triangles.length} / ${Context.Geometry.Triangles.length}</b>
        <span>Volume</span><b>12 × 6 × 12 m</b><span>Intervals / iteration</span><b>${Intervals.toLocaleString()}</b>
        <span>Cascade / cosine / chart targets</span><b>${(Bytes/1048576).toFixed(2)} MiB</b>
        <span>Completed iterations</span><b>${Iteration} / ${Settings.Iterations}</b>
        <span>Scene revision</span><b>${Transport.Revision}</b><span>Geometry representation</span><b>Exact triangles</b>`;
    Control('Status').textContent=Transport.Busy?`Calculating iteration ${Iteration+1} / ${Settings.Iterations} · cascade ${Transport.Level} · far → near`:
        `Transport ready · ${Iteration} iterations · 3D volume × full sphere · camera independent`;
    if (!Transport.Busy)
    {
        Context.Ready=true;
        Control('TimingNote').textContent=`${(Transport.Milliseconds/1000).toFixed(2)} s scheduled solve latency on this browser. Not a GPU timestamp or an FPS benchmark.`;
    }
}

function Animate()
{
    requestAnimationFrame(Animate);
    if (Context.Error || !Transport) return;
    try
    {
        if (ResizePending)
        {
            ResizePending=false;
            const Rect=Control('Viewport').getBoundingClientRect();
            Renderer.setSize(Rect.width,Rect.height,false);
            Camera.aspect=Rect.width/Rect.height;
            Camera.updateProjectionMatrix();
        }
        const Advanced=Transport.Advance();
        if (Advanced) RenderDiagnostics();
        Controls.update();
        const Width=Renderer.domElement.clientWidth, Height=Renderer.domElement.clientHeight;
        Renderer.setViewport(0,0,Width,Height);
        Renderer.setScissorTest(false);
        Renderer.render(Scene,Camera);
        if (Control('ShowDirections').checked)
        {
            const Cascade=Transport.Cascades[Context.Selected];
            const Mode=Control('DirectionMode').value;
            const Uniforms=DirectionMaterial.uniforms;
            Uniforms.RadianceTexture.value=(Mode==='Merged'?Cascade.Merged:Cascade.Intervals).texture;
            Uniforms.Dimensions.value.fromArray(Cascade.Dimensions);
            Uniforms.Coordinate.value.set(Context.Column,Context.Slice,Context.Depth);
            Uniforms.AngularSpan.value=Cascade.AngularSpan;
            Uniforms.Visibility.value=Mode==='Visibility';
            const DiagramWidth=Math.min(300,Width-40), DiagramHeight=DiagramWidth*2/3;
            Control('DirectionCaption').style.bottom=`${DiagramHeight+48}px`;
            Control('DirectionTitle').textContent=`C${Context.Selected} · ${Mode==='Merged'?'merged radiance':Mode==='Visibility'?'interval transmittance':'local interval radiance'}`;
            Renderer.setViewport(20,40,DiagramWidth,DiagramHeight);
            Renderer.setScissor(20,40,DiagramWidth,DiagramHeight);
            Renderer.setScissorTest(true);
            Renderer.render(DirectionScene,DirectionCamera);
            Renderer.setScissorTest(false);
            Renderer.setViewport(0,0,Width,Height);
        }
    }
    catch (Error) {Fail(Error.message);}
}

try
{
    Transport=new CascadeSequence(Renderer);
    BuildControls();
    Control('SceneList').innerHTML=ConstructScene().Solids.map(Solid=>`<div class="OutlinerEntry">${Solid.Name}<span class="Tile">●</span></div>`).join('');
    Control('CascadeList').innerHTML=Transport.Cascades.map(Cascade=>`<button class="StageButton" data-cascade="${Cascade.Level}"><span class="Number">C${Cascade.Level}</span><span><strong>${Cascade.Dimensions.join(' × ')} lattice</strong><small>${6*Cascade.AngularSpan**2} directions · ${Cascade.Start.toFixed(2)}–${Cascade.End.toFixed(2)} m</small></span></button>`).join('');
    document.querySelectorAll('[data-cascade]').forEach(Button=>Button.onclick=()=>{Context.Selected=Number(Button.dataset.cascade);RenderSamples();});
    for (const [Index,Name] of ['Full','Direct','Difference'].entries()) Control(Name).onclick=()=>
    {
        Context.Display=Index;
        SurfaceMaterial.uniforms.Display.value=Index;
        ['Full','Direct','Difference'].forEach((Other,OtherIndex)=>Control(Other).classList.toggle('Active',Index===OtherIndex));
    };
    Control('ShowSamples').onchange=RenderSamples;
    Control('ShowDirections').onchange=Event=>{Control('DirectionCaption').hidden=!Event.target.checked;};
    Control('Recalculate').onclick=Recalculate;
    Control('Reset').onclick=()=>{Object.assign(Settings,SceneDefaults);BuildControls();Recalculate();};
    Control('Frame').onclick=FrameView;
    window.addEventListener('keydown',Event=>{if (Event.key.toLowerCase()==='f' && document.activeElement.tagName!=='INPUT') FrameView();});
    Renderer.domElement.addEventListener('webglcontextlost',Event=>{Event.preventDefault();Fail('WebGL context lost. Reload to recreate transport targets.');});
    new ResizeObserver(()=>{ResizePending=true;}).observe(Control('Viewport'));
    window.RadianceApp={Context,Settings,Transport,Scene,Camera,Controls,Renderer,Surface,Recalculate,RenderSamples,FrameView};
    FrameView();
    RenderSamples();
    Recalculate();
    Animate();
}
catch (Error) {Fail(Error.message);}
