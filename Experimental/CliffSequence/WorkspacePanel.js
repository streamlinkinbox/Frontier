//============================================================================================================================================
//                                                             WORKSPACEPANEL.JS
//============================================================================================================================================
// 📦 Static polygon cliff authoring workspace, clay viewport, stage inspection and triangle OBJ exchange.

import * as THREE from 'three';
import {CreateGrainPanel} from './GrainPanel.js';
import {CaptureGrainSource} from './GrainSequence.js';
import {OrbitControls} from '../Ocean/lib/addons/OrbitControls.js';
import {CliffDefaults, CliffProfiles, ReadSpecification, EarliestStage, NoiseModes, FractureStyles, FormationPresets, ReadRecipe} from './CliffSpecification.js';

const Element=Id=>document.getElementById(Id);
const StageDescriptions=[
    ['Cliff mass','Buttresses, bays & crown','Seeded, all-sided cliff relief. Choose a landform preset and noise family, or use New seed for a different formation.'],
    ['Primary fractures','Joint sets, not just bedding','Geological orientation families with rough polygon cuts. New fractures terminate at existing boundaries; bedding is an optional preset.'],
    ['Bounded joints','Finite-depth rock blocks','Kinked joints split front, rear and end exposures, terminating against a retained interior core.'],
    ['Edge spalls','Local fracture cavities','Localized, asymmetric bites with four or six fracture facets. The original edge survives on both sides—not a full-edge bevel.'],
    ['Surface fissures','Shallow polygon incisions','Finite, kinked V-grooves cut into individual rock faces. Closed bottoms, bounded depth; no SDF erosion.']
];
const State={Specification:{...CliffDefaults},Result:null,Stage:1,Busy:false,DisplayStage:0,Revision:0,ReadyRevision:0,Worker:null,Dirty:false,
    Mode:'Clay',Wire:false,Selected:null,Isolated:false,Exploded:false,Milliseconds:0,Error:null};
const Scene=new THREE.Scene();
Scene.background=new THREE.Color('#282e38');
const Camera=new THREE.PerspectiveCamera(38,1,.05,500);
const Renderer=new THREE.WebGLRenderer({canvas:Element('SceneCanvas'),antialias:true,preserveDrawingBuffer:true});
Renderer.setPixelRatio(Math.min(devicePixelRatio,2));
Renderer.shadowMap.enabled=true;
Renderer.shadowMap.type=THREE.PCFSoftShadowMap;
Renderer.shadowMap.autoUpdate=false;
Renderer.toneMapping=THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure=1;
const Controls=new OrbitControls(Camera,Renderer.domElement);
Controls.enableDamping=true;
Controls.dampingFactor=.12;
Controls.minDistance=.5;
Controls.maxDistance=450;
Controls.maxPolarAngle=Math.PI*.49;
const Hemisphere=new THREE.HemisphereLight('#d7e5fc','#484952',1.25);
Scene.add(Hemisphere);
const Sun=new THREE.DirectionalLight('#fff4e5',3.2);
Sun.position.set(-26,37,24);
Sun.castShadow=true;
Sun.shadow.mapSize.set(2048,2048);
Object.assign(Sun.shadow.camera,{left:-35,right:35,top:35,bottom:-35,near:.5,far:150});
Sun.shadow.normalBias=.025;
Sun.shadow.bias=-.00004;
Sun.shadow.radius=2;
Scene.add(Sun,Sun.target);
const Fill=new THREE.DirectionalLight('#b5c9ea',.65);
Fill.position.set(22,16,-16);
Scene.add(Fill);
const Ground=new THREE.Mesh(new THREE.PlaneGeometry(200,200),new THREE.MeshStandardMaterial({color:'#333b48',roughness:1}));
Ground.rotation.x=-Math.PI/2;
Ground.position.y=-.12;
Ground.receiveShadow=true;
Scene.add(Ground);
const Grid=new THREE.GridHelper(100,20,'#424c5c','#384250');
Grid.position.y=-.105;
Grid.material.transparent=true;
Grid.material.opacity=.25;
Scene.add(Grid);
const BodyGroup=new THREE.Group();
Scene.add(BodyGroup);
const ClayMaterial=new THREE.MeshStandardMaterial({color:'#a9abad',roughness:1,metalness:0,flatShading:true,
    polygonOffset:true,polygonOffsetFactor:1,polygonOffsetUnits:1});
const CutMaterial=ClayMaterial.clone();
CutMaterial.color.set('#ffffff');
CutMaterial.vertexColors=true;
const WireMaterial=new THREE.LineBasicMaterial({color:'#111820',transparent:true,opacity:.35});
const SelectionMaterial=new THREE.LineBasicMaterial({color:'#efb063',transparent:true,opacity:.9,depthTest:true});
const CutColours={Cliff:'#929aa5',Crown:'#929aa5',Base:'#929aa5',End:'#929aa5',Back:'#929aa5',Bedding:'#8ca49d',
    Fracture:'#8ca49d',Joint:'#8ca49d',Termination:'#8ca49d',Spall:'#e0a570',Crack:'#d07969'};
let SelectionOutline=null;
let ResizePending=true;
let RenderRequested=true;
const ResizeObserverHandle=new ResizeObserver(()=>{ResizePending=true;});
ResizeObserverHandle.observe(Element('Viewport'));

function SetStatus(Message, Warning=false)
{
    Element('Status').textContent=Message;
    Element('StatusDot').style.background=Warning?'#c59a61':'#88a483';
}

function SetPressed(Id, Value)
{
    RenderRequested=true;
    Element(Id).classList.toggle('Active',Value);
    Element(Id).setAttribute('aria-pressed',String(Value));
}

function DisposeBodies()
{
    State.SourceFace=null;
    ClearSelection();
    BodyGroup.children.forEach(Body=>Body.traverse(Object=>{if(Object.geometry) Object.geometry.dispose();}));
    BodyGroup.clear();
}

function BuildRenderBody(Mesh, Index)
{
    const Positions=new Float32Array(Mesh.Triangles.length*9);
    const Colours=new Float32Array(Positions.length);
    Mesh.Triangles.forEach((Triangle,TriangleIndex)=>
    {
        const Colour=new THREE.Color(CutColours[Mesh.Tags[TriangleIndex]]||'#929aa5');
        Triangle.forEach((Vertex,Corner)=>
        {
            Positions.set(Mesh.Vertices[Vertex],TriangleIndex*9+Corner*3);
            Colours.set([Colour.r,Colour.g,Colour.b],TriangleIndex*9+Corner*3);
        });
    });
    const Geometry=new THREE.BufferGeometry();
    Geometry.setAttribute('position',new THREE.BufferAttribute(Positions,3));
    Geometry.setAttribute('color',new THREE.BufferAttribute(Colours,3));
    Geometry.computeVertexNormals();
    Geometry.computeBoundingBox();
    Geometry.computeBoundingSphere();
    const Body=new THREE.Mesh(Geometry,State.Mode==='Scars'?CutMaterial:ClayMaterial);
    Body.name=Mesh.Name;
    Body.userData={Index,Mesh,Centre:Geometry.boundingBox.getCenter(new THREE.Vector3())};
    Body.castShadow=true;
    Body.receiveShadow=true;
    const Wire=new THREE.LineSegments(new THREE.WireframeGeometry(Geometry),WireMaterial);
    Wire.name='Triangle edges';
    Wire.visible=State.Wire;
    Body.add(Wire);
    return Body;
}

function ViewStage(StageNumber)
{
    RenderRequested=true;
    Renderer.shadowMap.needsUpdate=true;
    State.Stage=Math.max(1,Math.min(5,StageNumber));
    const [Title,,Description]=StageDescriptions[State.Stage-1];
    Element('StageTitle').textContent=Title;
    Element('StageDescription').textContent=Description;
    Element('StageNumber').textContent=`0${State.Stage} / 05`;
    Element('PreviousStage').disabled=State.Stage===1;
    Element('NextStage').disabled=State.Stage===5;
    document.querySelectorAll('.StageButton').forEach(Button=>
    {
        const Selected=Number(Button.dataset.stage)===State.Stage;
        Button.classList.toggle('Active',Selected);
        Button.setAttribute('aria-pressed',String(Selected));
    });
    State.Dirty=State.Busy || !State.Result?.Stages[State.Stage-1] || State.Stage>=EarliestStage(State.Result?.Specification,State.Specification);
    Element('ExportObj').disabled=State.Dirty;
    Element('Regenerate').textContent=State.Busy?'Cancel & rebuild':`Rebuild through 0${State.Stage}`;
    document.querySelectorAll('#ParameterControls details').forEach((Section,Index)=>{Section.open=Index===State.Stage-1;});
    const ValidThrough=Math.min(State.Stage,EarliestStage(State.Result?.Specification,State.Specification)-1);
    const Stage=State.Result?.Stages.slice(0,ValidThrough).at(-1);
    State.DisplayStage=Stage?.Number||0;
    if (State.Dirty)
    {
        Element('StageDescription').textContent=`Stage ${State.Stage} needs rebuilding. ${Stage?`Showing stage ${Stage.Number} as input.`:'No current input mesh.'} ${Description}`;
        SetStatus(`Rebuild through stage ${State.Stage} · later stages will not run`,true);
    }
    const Name=State.Selected?.name;
    const Isolated=State.Isolated;
    DisposeBodies();
    if (!Stage)
    {
        Element('Metrics').textContent='No current mesh at this stage. Rebuild to continue.';
        Element('BodyCount').textContent='No current mesh';
        Element('TriangleCount').textContent='— triangles';
        return;
    }
    Stage.Meshes.forEach((Mesh,Index)=>BodyGroup.add(BuildRenderBody(Mesh,Index)));
    if (Name)
    {
        const Match=BodyGroup.children.find(Body=>Body.name===Name);
        if (Match) SelectBody(Match,false);
        State.Isolated=Isolated && !!Match;
    }
    ApplyVisibility();
    UpdateMetrics();
}

function UpdateMetrics()
{
    const Stage=State.Result?.Stages[State.DisplayStage-1];
    if (!Stage) return;
    const Metrics=Stage.Metrics;
    const Format=Value=>Value.toLocaleString('en');
    Element('Metrics').innerHTML=`<span>Closed mesh objects</span><b>${Metrics.Bodies}</b>
        <span>Vertices / triangles</span><b>${Format(Metrics.Vertices)} / ${Format(Metrics.Triangles)}</b>
        <span>Open / nonmanifold edges</span><b class="Pass">${Metrics.OpenEdges} / ${Metrics.NonmanifoldEdges}</b>
        <span>Nonmanifold vertices / duplicates</span><b class="Pass">${Metrics.NonmanifoldVertices} / ${Metrics.DuplicateTriangles}</b>
        <span>Degenerate / flipped winding</span><b class="Pass">${Metrics.ZeroArea} / ${Metrics.WindingErrors}</b>
        <span>Local spalls / fissures</span><b>${Metrics.Spalls} / ${Metrics.Cracks}</b>
        <span>Rejected spalls / fissures</span><b>${Metrics.RejectedSpalls} / ${Metrics.RejectedCracks}</b>
        <span>Triangles below 5°</span><b class="${Metrics.ThinTriangles?'Warn':'Pass'}">${Metrics.ThinTriangles}</b>
        <span>Minimum triangle angle</span><b>${Metrics.MinimumAngle.toFixed(2)}°</b>
        <span>Last requested rebuild</span><b>${(State.Milliseconds/1000).toFixed(2)} s</b>`;
    Element('QualityNote').textContent=State.Dirty ? `Showing stage ${State.DisplayStage} input, not the selected stage output. Export is disabled.` : Metrics.ThinTriangles ?
        'Narrow triangles remain at some clipped intersections; counted above, not hidden. Topology checks do not prove absence of all surface intersections.' :
        'Indexed export topology checked per body. No n-gons. Display wireframe includes every triangulation edge.';
    Element('BodyCount').textContent=`${Metrics.Bodies} closed mesh objects`;
    Element('TriangleCount').textContent=`${Format(Metrics.Triangles)} triangles`;
    if (!State.Dirty) SetStatus(`Stage ${State.Stage} · ${Metrics.ThinTriangles ? `${Metrics.ThinTriangles} narrow-triangle warnings` : 'Topology checked'} · seed ${State.Result.Specification.Seed}`,!!Metrics.ThinTriangles);
}

function ClearSelection()
{
    RenderRequested=true;
    if (SelectionOutline)
    {
        SelectionOutline.removeFromParent();
        SelectionOutline.geometry.dispose();
        SelectionOutline=null;
    }
    State.Selected=null;
    State.Isolated=false;
    Element('SelectionName').textContent='Cliff formation';
    Element('SelectionDetail').textContent='Double-click a rock to inspect it.';
    Element('Isolate').disabled=true;
    Element('Isolate').textContent='Isolate rock';
}

function SelectBody(Body, Focus=true)
{
    ClearSelection();
    State.Selected=Body;
    SelectionOutline=new THREE.LineSegments(new THREE.EdgesGeometry(Body.geometry,12),SelectionMaterial);
    Body.add(SelectionOutline);
    Element('SelectionName').textContent=Body.name;
    Element('SelectionDetail').textContent=`${Body.userData.Mesh.Triangles.length.toLocaleString()} triangles · ${Body.userData.Mesh.Spalls.length} spalls · ${Body.userData.Mesh.Cracks.length} fissures`;
    Element('Isolate').disabled=false;
    if (Focus) FrameView(Body);
}

function ApplyVisibility()
{
    RenderRequested=true;
    Renderer.shadowMap.needsUpdate=true;
    BodyGroup.children.forEach(Body=>
    {
        Body.visible=!State.Isolated || Body===State.Selected;
        const Centre=Body.userData.Centre;
        Body.position.set(State.Exploded?Centre.x*.15:0,State.Exploded?Centre.y*.14:0,State.Exploded?Centre.z*.13:0);
    });
    Element('Isolate').textContent=State.Isolated?'Restore cliff':'Isolate rock';
}

function FrameView(Body=null, Direction=null)
{
    RenderRequested=true;
    if (!BodyGroup.children.length) return;
    const View=Element('Viewport').getBoundingClientRect();
    Camera.aspect=View.width/Math.max(1,View.height);
    const Box=new THREE.Box3().setFromObject(Body||BodyGroup);
    const Middle=Box.getCenter(new THREE.Vector3());
    const Size=Box.getSize(new THREE.Vector3());
    const Radius=Size.length()*.5;
    const Angle=Math.min(Camera.fov*Math.PI/360,Math.atan(Math.tan(Camera.fov*Math.PI/360)*Camera.aspect));
    const Distance=Radius/Math.sin(Angle)*(Body?1.10:1.13);
    const Offset=(Direction||new THREE.Vector3(.47,.25,.88)).clone().normalize();
    Controls.target.copy(Middle);
    Camera.position.copy(Middle).addScaledVector(Offset,Distance);
    Camera.near=Math.max(.02,Distance/2000);
    Camera.updateProjectionMatrix();
    Controls.update();
}

function MarkDirty()
{
    if (State.Busy)
    {
        State.Worker?.terminate();
        State.Worker=null;
        State.Busy=false;
        ++State.Revision;
        Element('Loading').hidden=true;
    }
    ViewStage(State.Stage);
}

function Generate()
{
    if (State.Busy)
    {
        State.Worker?.terminate();
        State.Worker=null;
    }
    const Revision=++State.Revision;
    const Initial=!State.Result;
    const Reframe=Initial || ['Profile','Width','Height','Depth'].some(Name=>State.Result.Specification[Name]!==State.Specification[Name]);
    State.Error=null;
    State.Busy=true;
    State.Dirty=true;
    Element('ExportObj').disabled=true;
    Element('Failure').hidden=true;
    Element('Loading').hidden=false;
    Element('LoadingTitle').textContent=`Rebuilding through stage ${State.Stage}`;
    Element('LoadingDetail').textContent='Reusing valid upstream checkpoints…';
    Element('Regenerate').textContent='Cancel & rebuild';
    SetStatus(`Building through stage ${State.Stage} only`);
    try
    {
        State.Specification=ReadSpecification(State.Specification);
        for (const [Name,Value] of Object.entries(State.Specification))
        {
            const Input=Element(Name);
            if (Input) {Input.value=Value;UpdateRange(Input);}
        }
        const GenerationWorker=State.Worker||new Worker(new URL('./GenerationQueue.js',import.meta.url),{type:'module'});
        State.Worker=GenerationWorker;
        GenerationWorker.onmessage=Event=>
        {
            const Message=Event.data;
            if (Message.Revision!==State.Revision) return;
            if (Message.Progress)
            {
                Element('LoadingDetail').textContent=`Stage ${Message.Progress} · ${StageDescriptions[Message.Progress-1][0]}`;
                return;
            }
            if (Message.Error) {FailGeneration(Message.Error);return;}
            State.Result=Message.Result;
            State.Milliseconds=Message.Milliseconds;
            State.ReadyRevision=Revision;
            State.Busy=false;
            Element('Loading').hidden=true;
            ViewStage(State.Stage);
            if (Reframe) FrameView();
            if (!State.Dirty)
            {
                const Warnings=Message.Result.Stages[State.Stage-1].Metrics.ThinTriangles;
                const Quality=Warnings?` · ${Warnings} narrow-triangle warnings`:'';
                SetStatus(`Stage ${State.Stage} ready · calculated ${Message.Result.ExecutedStages.join(', ')||'none'} · reused ${Message.Result.ReusedStages.join(', ')||'none'}${Quality}`,Warnings>0);
            }
        };
        GenerationWorker.onerror=Event=>
        {
            if (State.Revision===Revision) FailGeneration(Event.message||'Geometry worker failed to load.');
        };
        GenerationWorker.postMessage({Revision,Specification:State.Specification,Through:State.Stage});
    }
    catch (Error)
    {
        FailGeneration(Error.message);
    }
}

function FailGeneration(Message)
{
    State.Worker?.terminate();
    State.Worker=null;
    State.Result=null;
    State.Error=Message;
    State.Busy=false;
    State.Dirty=true;
    DisposeBodies();
    Renderer.shadowMap.needsUpdate=true;
    Element('ExportObj').disabled=true;
    Element('BodyCount').textContent='No generated mesh';
    Element('Metrics').textContent='No mesh available. Generation or rendering failed.';
    Element('Loading').hidden=true;
    Element('Failure').hidden=false;
    Element('Failure').textContent=`Generation failed: ${Message} No previous or prebaked geometry has been substituted. Change the recipe and rebuild.`;
    Element('Regenerate').textContent='Retry rebuild';
    SetStatus('Generation failed · export disabled',true);
}

function Download(Name, Content, Type)
{
    const Url=URL.createObjectURL(new Blob([Content],{type:Type}));
    const Link=document.createElement('a');
    Link.href=Url;
    Link.download=Name;
    Link.click();
    setTimeout(()=>URL.revokeObjectURL(Url),1000);
}

function ObjText()
{
    if (!State.Result || State.Dirty) throw new Error('Rebuild the current recipe before exporting.');
    const Stage=State.Result.Stages[State.Stage-1];
    const Lines=[`# Frontier polygon cliff | stage ${State.Stage} | seed ${State.Result.Specification.Seed}`,
        '# metres; triangles only; untransformed source geometry; polygon-only geometry; no textures or SDF','s off'];
    let Offset=1;
    for (const Mesh of Stage.Meshes)
    {
        Lines.push(`o ${Mesh.Name.replace(/[^a-zA-Z0-9]+/g,'_')}`);
        Mesh.Vertices.forEach(Point=>Lines.push(`v ${Point.map(Value=>Value.toFixed(8)).join(' ')}`));
        Mesh.Triangles.forEach(Triangle=>Lines.push(`f ${Triangle.map(Index=>Index+Offset).join(' ')}`));
        Offset+=Mesh.Vertices.length;
    }
    return Lines.join('\n')+'\n';
}

const Groups=[
    ['Cliff mass',true,[['Profile','Landform preset · applies size'],['Seed','Formation seed'],
        ['NoiseMode','Relief noise'],['Variation','Variation strength',0,1,.05,'×'],['NoiseScale','Feature frequency',1,5,.1,'×'],
        ['Width','Width',10,80,.5,'m'],['Height','Height',10,56,.5,'m'],['Depth','Depth',8,24,.5,'m'],
        ['Relief','Buttress / bay relief',.35,1.3,.05,'×'],['Retreat','Crown retreat',.25,.65,.01,'×']]],
    ['Primary fractures',false,[['FractureStyle','Fracture preset'],['FractureSeed','Fracture seed'],
        ['Beds','Cut / bed count',4,10,1,''],['Dip','Family tilt',-8,8,.5,'°'],['Aperture','Fracture aperture',.035,.18,.005,'m'],
        ['FractureBend','Fracture roughness',0,1.5,.05,'×']]],
    ['Bounded joints',false,[['JointSpacing','Joint spacing',3,7,.2,'m'],['Penetration','Joint penetration',.55,.9,.01,'×'],['FaceRecess','Face recess scale',0,1.5,.05,'m']]],
    ['Edge spalls',true,[['SpallSize','Spall scale',.25,1.3,.05,'m'],['SpallDensity','Edge occupancy',0,1,.05,'×']]],
    ['Surface fissures',false,[['CrackLength','Maximum length',.5,2.2,.1,'m'],['CrackWidth','Mouth width',.07,.22,.01,'m'],
        ['CrackDepth','Maximum depth',.06,.3,.01,'m'],['CrackDensity','Face occupancy',0,1,.05,'×']]],
    ['Triangulation',false,[['TriangleSpan','Target edge span',.8,2.2,.1,'m']]]
];
function BuildControls()
{
    const Choices={Profile:Object.fromEntries(Object.entries(CliffProfiles).map(([Key,Profile])=>[Key,Profile.Label])),NoiseMode:NoiseModes,FractureStyle:FractureStyles};
    Element('ParameterControls').innerHTML=Groups.map(([Title,Open,Fields])=>`<details ${Open?'open':''}><summary>${Title}</summary><div class="ControlGroup">${Fields.map(([Name,Label,Minimum,Maximum,Step,Unit])=>
    {
        if (Choices[Name]) return `<div class="Property"><label class="FieldLabel" for="${Name}">${Label}</label><select id="${Name}">${Object.entries(Choices[Name]).map(([Key,Title])=>`<option value="${Key}">${Title}</option>`).join('')}</select></div>`;
        if (Name.endsWith('Seed')) return `<div class="Property"><label class="FieldLabel" for="${Name}">${Label}<span class="Subtle">repeatable variation</span></label><div class="SeedRow"><input type="number" id="${Name}" min="0" max="999999" step="1"><button id="New${Name}">New seed</button></div></div>`;
        return `<div class="Property"><label class="FieldLabel" for="${Name}">${Label}<output id="${Name}Value"></output></label><input type="range" id="${Name}" min="${Minimum}" max="${Maximum}" step="${Step}" data-unit="${Unit}"></div>`;
    }).join('')}</div></details>`).join('');
    for (const [, ,Fields] of Groups) for (const [Name] of Fields)
    {
        const Input=Element(Name);
        Input.value=State.Specification[Name];
        UpdateRange(Input);
        Input.addEventListener('input',()=>
        {
            State.Specification[Name]=Choices[Name]?Input.value:Number(Input.value);
            if (Name==='Profile')
            {
                Object.assign(State.Specification,FormationPresets[Input.value]);
                BuildControls();
            }
            UpdateRange(Input);
            MarkDirty();
        });
    }
    for (const Name of ['Seed','FractureSeed']) Element(`New${Name}`).onclick=()=>
    {
        const Next=crypto.getRandomValues(new Uint32Array(1))[0]%1000000;
        State.Specification[Name]=Next===State.Specification[Name]?(Next+1)%1000000:Next;
        Element(Name).value=State.Specification[Name];
        MarkDirty();
    };
}

function UpdateRange(Input)
{
    if (Input.type!=='range') return;
    Input.style.setProperty('--Fill',`${(Number(Input.value)-Number(Input.min))/(Number(Input.max)-Number(Input.min))*100}%`);
    const Output=Element(`${Input.id}Value`);
    if (Output) Output.textContent=`${Number(Input.value).toFixed(Number(Input.step)<.01?3:Number(Input.step)<1?2:0)} ${Input.dataset.unit||''}`;
}

StageDescriptions.forEach(([Title,Subtitle],Index)=>
{
    const Button=document.createElement('button');
    Button.className='StageButton';
    Button.dataset.stage=Index+1;
    Button.innerHTML=`<span class="Number">0${Index+1}</span><span><strong>${Title}</strong><small>${Subtitle}</small></span>`;
    Button.onclick=()=>ViewStage(Index+1);
    Element('StageList').appendChild(Button);
});
BuildControls();
ViewStage(State.Stage);
Element('Regenerate').onclick=Generate;
Element('PreviousStage').onclick=()=>ViewStage(State.Stage-1);
Element('NextStage').onclick=()=>ViewStage(State.Stage+1);
Element('Frame').onclick=()=>FrameView();
Element('Front').onclick=()=>FrameView(null,new THREE.Vector3(0,.04,1));
Element('Rear').onclick=()=>FrameView(null,new THREE.Vector3(0,.18,-1));
Element('SelectCliff').onclick=()=>{ClearSelection();ApplyVisibility();};
Element('SelectCliff').ondblclick=()=>FrameView();
Element('ShowAll').onclick=()=>{ClearSelection();ApplyVisibility();FrameView();};
Element('Isolate').onclick=()=>{State.Isolated=!State.Isolated;ApplyVisibility();};
Element('Clay').onclick=()=>
{
    State.Mode='Clay';
    BodyGroup.children.forEach(Body=>{Body.material=ClayMaterial;});
    SetPressed('Clay',true);
    SetPressed('Scars',false);
};
Element('Scars').onclick=()=>
{
    State.Mode='Scars';
    BodyGroup.children.forEach(Body=>{Body.material=CutMaterial;});
    SetPressed('Clay',false);
    SetPressed('Scars',true);
};
Element('Wire').onclick=()=>
{
    State.Wire=!State.Wire;
    BodyGroup.children.forEach(Body=>{Body.children.find(Child=>Child.name==='Triangle edges').visible=State.Wire;});
    SetPressed('Wire',State.Wire);
};
Element('Explode').onchange=Event=>{State.Exploded=Event.target.checked;ApplyVisibility();};
Element('Ground').onchange=Event=>{Ground.visible=Grid.visible=Event.target.checked;RenderRequested=true;};
Element('Shadows').onchange=Event=>{Sun.castShadow=Event.target.checked;Renderer.shadowMap.needsUpdate=true;RenderRequested=true;};
Element('LightAngle').oninput=Event=>
{
    RenderRequested=true;
    Renderer.shadowMap.needsUpdate=true;
    const Angle=Number(Event.target.value)*Math.PI/180;
    Sun.position.set(Math.sin(Angle)*40,37,Math.cos(Angle)*40);
    Element('LightValue').textContent=`${Event.target.value}°`;
};
Element('ExportObj').onclick=()=>Download(`Cliff_${State.Specification.Profile}_${State.Specification.Seed}_Stage${State.Stage}.obj`,ObjText(),'text/plain');
Element('ExportRecipe').onclick=()=>Download(`Cliff_${State.Specification.Seed}.json`,JSON.stringify({Format:'Frontier.PolygonCliff',Version:2,Specification:State.Specification},null,2),'application/json');
Element('ImportRecipe').onclick=()=>Element('RecipeFile').click();
Element('RecipeFile').onchange=async Event=>
{
    try
    {
        const Recipe=JSON.parse(await Event.target.files[0].text());
        State.Specification=ReadRecipe(Recipe);
        BuildControls();
        Generate();
    }
    catch(Error)
    {
        SetStatus(`Recipe not loaded: ${Error.message}`,true);
    }
    Event.target.value='';
};
Renderer.domElement.addEventListener('dblclick',Event=>
{
    if (!State.Result || State.Dirty) return;
    const Rect=Renderer.domElement.getBoundingClientRect();
    const Ray=new THREE.Raycaster();
    Ray.setFromCamera(new THREE.Vector2((Event.clientX-Rect.left)/Rect.width*2-1,1-(Event.clientY-Rect.top)/Rect.height*2),Camera);
    const Hit=Ray.intersectObjects(BodyGroup.children.filter(Body=>Body.visible),false)[0];
    if (Hit)
    {
        SelectBody(Hit.object);
        const Content=Hit.object.userData.Mesh;
        State.SourceFace={Triangle:Content.Triangles[Hit.faceIndex].map(Index=>Content.Vertices[Index].slice()),BodyName:Content.Name,Stage:State.Stage,TriangleIndex:Hit.faceIndex};
    }
});
window.addEventListener('keydown',Event=>
{
    if (GrainStudy.Parameters.Active||['INPUT','SELECT','TEXTAREA'].includes(document.activeElement.tagName)) return;
    if (Event.key.toLowerCase()==='f') FrameView(State.Selected);
    if (Event.key==='Escape') {ClearSelection();ApplyVisibility();}
});
Renderer.domElement.addEventListener('webglcontextlost',Event=>
{
    Event.preventDefault();
    FailGeneration('WebGL context lost. Reload the page to restore the viewport.');
    Element('Regenerate').disabled=true;
});
function Animate()
{
    requestAnimationFrame(Animate);
    if (GrainStudy.Parameters.Active) return;
    if (ResizePending)
    {
        ResizePending=false;
        RenderRequested=true;
        const Rect=Element('Viewport').getBoundingClientRect();
        Camera.aspect=Rect.width/Rect.height;
        Camera.updateProjectionMatrix();
        Renderer.setSize(Rect.width,Rect.height,false);
    }
    const Moved=Controls.update();
    if (Moved || RenderRequested)
    {
        RenderRequested=false;
        Renderer.render(Scene,Camera);
        const Along=new THREE.Vector3(1,0,0).applyQuaternion(Camera.quaternion).multiplyScalar(5);
        const A=Controls.target.clone().project(Camera);
        const B=Controls.target.clone().add(Along).project(Camera);
        const PixelsPerMetre=Math.abs(B.x-A.x)*Renderer.domElement.clientWidth*.1;
        const ScaleLength=[.01,.02,.05,.1,.2,.5,1,2,5,10,20].filter(Length=>Length*PixelsPerMetre<=150).at(-1)||.01;
        document.querySelector('.ScaleBadge span').style.width=`${ScaleLength*PixelsPerMetre}px`;
        document.querySelector('.ScaleBadge b').textContent=`${ScaleLength} m`;
    }
}
function AcquireGrainSource()
{
    if (!State.Result||State.Dirty||State.Busy) throw new Error('Build the selected cliff stage before sampling its face.');
    let Source=State.SourceFace;
    if (!Source)
    {
        let Best=-Infinity;
        for (const Content of State.Result.Stages[State.Stage-1].Meshes) for (let Index=0;Index<Content.Triangles.length;++Index)
        {
            if (State.Selected&&State.Selected.name!==Content.Name) continue;
            if (Content.Tags[Index]!=='Cliff') continue;
            const Triangle=Content.Triangles[Index].map(Vertex=>Content.Vertices[Vertex]);
            const A=new THREE.Vector3(...Triangle[1]).sub(new THREE.Vector3(...Triangle[0]));
            const B=new THREE.Vector3(...Triangle[2]).sub(new THREE.Vector3(...Triangle[0]));
            const Normal=A.cross(B),Area=Normal.length();
            if (Normal.z>0&&Area>Best) {Best=Area;Source={Triangle,BodyName:Content.Name,Stage:State.Stage,TriangleIndex:Index};}
        }
    }
    if (!Source) throw new Error('Select a larger exposed cliff face to sample.');
    return CaptureGrainSource(Source.Triangle,Source.BodyName,Source.Stage,Source.TriangleIndex);
}
const GrainStudy=CreateGrainPanel(Element('GrainWorkspace'),AcquireGrainSource);
const DocumentNames={Geometry:'Cliff formation',Material:'Grain weathering'};
function ViewDocument(Material)
{
    Element('GeometryWorkspace').hidden=Material;
    Controls.enabled=!Material;
    GrainStudy.SetActive(Material);
    for (const [Name,Active] of [['GeometryTab',!Material],['MaterialTab',Material]])
    {
        Element(Name).classList.toggle('active',Active);Element(Name).setAttribute('aria-selected',String(Active));
    }
    Element('DocumentName').value=DocumentNames[Material?'Material':'Geometry'];
    Element('DocumentExtension').textContent=Material?'.grain':'.cliff';
    Element('DocumentNote').textContent=Material?'Discrete grains · isolated source-face study · no baking':'Procedural geometry · selected-stage rebuilds';
    Element('SaveActive').textContent=Material?'Save study':'Save recipe';
    Element('OpenActive').textContent=Material?'Open study':'Open recipe';
    Element('Status').textContent=Material?'Grain material study · illustrative cycles, not geological time':'Cliff geometry · selected-stage rebuilds';
    Element('TriangleCount').hidden=Material;
    ResizePending=true;RenderRequested=true;
}
Element('GeometryTab').onclick=()=>ViewDocument(false);
Element('MaterialTab').onclick=()=>ViewDocument(true);
Element('DocumentName').oninput=Event=>
{
    const Name=GrainStudy.Parameters.Active?'Material':'Geometry';
    DocumentNames[Name]=Event.target.value;
    Element(`${Name}Tab`).querySelector('span').textContent=Event.target.value||'Untitled';
};
Element('SaveActive').onclick=()=>Element(GrainStudy.Parameters.Active?'GrainSave':'ExportRecipe').click();
Element('OpenActive').onclick=()=>Element(GrainStudy.Parameters.Active?'GrainLoad':'ImportRecipe').click();
Element('CliffSearch').oninput=Event=>document.querySelectorAll('.StageButton').forEach(Button=>{Button.hidden=!Button.textContent.toLowerCase().includes(Event.target.value.toLowerCase());});
window.GrainApp=GrainStudy;
window.CliffApp={State,Scene,Camera,Controls,Renderer,BodyGroup,Generate,ViewStage,FrameView,SelectBody,ObjText,
    SetSpecification:Specification=>{State.Specification=ReadSpecification({...State.Specification,...Specification});BuildControls();Generate();},
    FocusSpall:()=>
    {
        const Candidates=BodyGroup.children.flatMap(Body=>Body.userData.Mesh.Spalls.map(Spall=>({Body,Spall})));
        Candidates.sort((A,B)=>new THREE.Vector3(...A.Spall.Centre).distanceTo(new THREE.Vector3(0,8,0))-new THREE.Vector3(...B.Spall.Centre).distanceTo(new THREE.Vector3(0,8,0)));
        const Selected=Candidates.find(Candidate=>Candidate.Spall.Size>.45)||Candidates[0];
        if (!Selected) return false;
        SelectBody(Selected.Body,false);
        const Centre=new THREE.Vector3(...Selected.Spall.Centre);
        Controls.target.copy(Centre);
        Camera.position.copy(Centre).add(new THREE.Vector3(2,1.3,4.8));
        Controls.update();
        return Selected.Body.name;
    }};
Animate();
Generate();
