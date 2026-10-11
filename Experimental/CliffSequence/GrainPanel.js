//============================================================================================================================================
//                                                                GRAINPANEL.JS
//============================================================================================================================================
// 📦 Fluid-themed material study: source-face attachment, worker-driven weathering, inspectable particles and actual polygon retreat.

import * as THREE from 'three';
import {OrbitControls} from '../Ocean/lib/addons/OrbitControls.js';
import {GrainDefaults,GrainLimits,MineralPresets,PackingProperties,ReadGrainSpecification} from './GrainSpecification.js';

export function CreateGrainPanel(Host,AcquireSource)
{
    const Icon='<svg><use href="#RockIcon"/></svg>';
    Host.innerHTML=`<aside class="Outliner"><div class="PanelHeader">Outliner <span>Material study</span></div>
        <div class="SceneCensus"><span class="CensusTile">● <b>2</b><small>Visible</small></span><span class="CensusTile">◇ <b>1</b><small>Solid patch</small></span></div>
        <label class="SceneSearch"><input id="GrainSearch" type="search" placeholder="Find a mineral…" aria-label="Find a mineral"></label>
        <div class="OutlinerEntry"><svg><use href="#CameraIcon"/></svg>Editor Camera<span class="Tile">●</span></div>
        <button class="OutlinerEntry Selected" id="GrainFocus">${Icon}<span>Material patch</span><span class="Tile">●</span></button>
        <div class="OutlinerEntry Indent" id="GrainCensus">Preparing grains…</div>
        <div class="PanelHeader StageHeader">Material library</div><div class="GrainLibrary" id="GrainLibrary"></div>
        <div class="ScopeNote"><b>PARTICLE MATERIAL</b><p>Real polygon grains.<br>No texture, noise field or SDF.<br>No displacement or baking.</p>
        <p>Isolated shallow patch. The cliff is not modified by this study.</p></div>
        <div class="OutlinerBottom"><div class="PanelHeader">Particle inspection</div><div id="GrainSelection" class="GrainSelection">Click a visible grain to inspect its bonds, water and source attachment.</div></div></aside>
        <section class="Viewport GrainViewport" id="GrainViewport"><div class="ViewportToolbar"><span class="ViewTitle">Material view</span>
        <select id="GrainChannel" aria-label="Material channel"><option value="Material">Minerals & weathering</option><option value="Clay">Clay · geometry only</option>
        <option value="Moisture">Moisture</option><option value="Bonds">Cement bonds</option><option value="Oxide">Oxidation / deposits</option>
        <option value="Exposure">Exposed layer</option><option value="Weakness">Weak seam / band</option></select>
        <span class="ToolbarSpace"></span><button id="GrainWire">Edges</button><button id="GrainFrame">Frame</button></div>
        <canvas id="GrainCanvas"></canvas><div class="ViewCaption"><span class="Eyebrow">GRAIN WEATHERING · PATCH STUDY</span><h1 id="GrainTitle">Cemented sandstone</h1>
        <p id="GrainSource">Preparing a material sample.</p></div><div class="StudyBadge" id="GrainBadge">Unweathered</div>
        <div class="GrainTransport"><button id="GrainPlay" class="Primary">Run weathering</button><button id="GrainStep">Step</button><button id="GrainTwenty">+20 cycles</button><button id="GrainDry">Dry cycle</button>
        <span id="GrainClock">Cycle 0</span></div><div class="ViewportHint">Drag orbit · Scroll zoom · Click inspect · F frame · Space run / pause</div>
        <div class="Failure" id="GrainFailure" hidden></div></section>
        <aside class="Inspector"><div class="PanelHeader">Inspector <span>Grain material</span></div><div class="InspectorScroll">
        <div class="ObjectHeader">${Icon}<div><b>Rock material</b><small>Discrete grains / explicit solid</small></div><span class="Tile">●</span></div>
        <div class="InspectorTabs"><button id="GrainParametersTab" class="Active">Parameters</button><button id="GrainDiagnosticsTab">Diagnostics</button></div>
        <div id="GrainControls"></div><details><summary>Mineral palette · no image lookup</summary><div class="ControlGroup PaletteColours">${['Quartz-rich','Feldspar-rich','Cement-rich','Iron-bearing'].map((Label,Index)=>`<label>${Label}<input type="color" id="GrainTint${Index}" aria-label="${Label} colour"></label>`).join('')}<button id="GrainPaletteReset">Preset colours</button></div></details><details open id="GrainDiagnostics"><summary>Diagnostics</summary><div class="ControlGroup"><div id="GrainMetrics" class="Metrics">Build a material patch.</div></div></details>
        <details><summary>Scope & assumptions</summary><div class="ControlGroup Small"><p>Grains use seeded packing, not a sampled noise field. Mineral colours are per-grain attributes; no texture is sampled.</p>
        <p>Water and dissolved tracer exchange across grain contacts. Cement weakens, iron-bearing grains oxidise, and evaporation leaves deposits.</p>
        <p>Exposed grains detach from a shallow layered solid. Removal exposes buried geometry and preserves a permanent backing. No vertex is pushed by a height field.</p>
        <p>This first implementation has columnar grains and top-down retreat. It does not yet simulate arbitrary 3D cleavage, undercuts, falling debris or whole-cliff erosion. Cycles are illustrative, not geological time. Grain scale is deliberately coarse for inspection.</p></div></details>
        </div><div class="InspectorFooter"><button id="GrainBuild" class="Primary">Rebuild grains</button><div><button id="GrainCapture">Sample cliff face</button><button id="GrainSave">Save study</button><button id="GrainLoad">Open</button></div><input type="file" id="GrainFile" accept=".json" hidden></div></aside>`;
    const Find=Name=>Host.querySelector(`#${Name}`);
    const Parameters={Specification:{...GrainDefaults},Active:false,Busy:false,Dirty:false,Playing:false,Revision:0,ReadyRevision:0,
        Result:null,Source:null,Error:null,Palette:null,Channel:'Material',Selected:null,Wire:false};
    const Scene=new THREE.Scene();Scene.background=new THREE.Color('#17191a');
    const Camera=new THREE.PerspectiveCamera(38,1,.0001,100);
    const Renderer=new THREE.WebGLRenderer({canvas:Find('GrainCanvas'),antialias:true,preserveDrawingBuffer:true});
    Renderer.setPixelRatio(Math.min(devicePixelRatio,2));Renderer.shadowMap.enabled=true;Renderer.shadowMap.type=THREE.PCFSoftShadowMap;
    Renderer.toneMapping=THREE.ACESFilmicToneMapping;Renderer.toneMappingExposure=1.15;
    const Controls=new OrbitControls(Camera,Renderer.domElement);Controls.enableDamping=true;Controls.minDistance=.02;Controls.maxDistance=6;
    const Sun=new THREE.DirectionalLight('#ffebd5',3.1);Sun.position.set(-1,1.3,1.5);Sun.castShadow=true;
    Sun.shadow.mapSize.set(2048,2048);Object.assign(Sun.shadow.camera,{left:-1,right:1,top:1,bottom:-1,near:.01,far:6});Sun.shadow.bias=-.00002;Sun.shadow.normalBias=.0001;
    Scene.add(Sun,new THREE.HemisphereLight('#d6e4ef','#514536',1.6));
    const Material=new THREE.MeshStandardMaterial({vertexColors:true,roughness:.86,flatShading:true,polygonOffset:true,polygonOffsetFactor:1,polygonOffsetUnits:1});
    const WireMaterial=new THREE.LineBasicMaterial({color:'#151819',transparent:true,opacity:.25});
    const SelectionMaterial=new THREE.MeshBasicMaterial({color:'#e3cf94',transparent:true,opacity:.38,depthWrite:false,polygonOffset:true,polygonOffsetFactor:-1});
    let Surface=null,Wire=null,Selection=null,GrainWorker=null,Resize=true,Requested=true;
    const Observer=new ResizeObserver(()=>{Resize=true;Requested=true;});Observer.observe(Find('GrainViewport'));
    const Fields=[['Grain packing',true,[['Seed','Packing seed'],['Resolution','Grains per side',12,36,1,''],['Size','Requested patch size',.06,1.2,.02,'m'],['Layers','Buried grain layers',2,8,1,''],['BondStrength','Initial cement strength',.15,1,.05,'×'],['WeakBand','Weak seam / band',0,1,.05,'×']]],
        ['Weathering',true,[['Rain','Rain supply',0,1,.05,'×'],['Drying','Evaporation',0,1,.05,'×'],['Runoff','Contact transport',0,1,.05,'×'],['Solvent','Cement dissolution',0,1,.05,'×'],['Oxidation','Oxidation rate',0,1,.05,'×']]]];
    Find('GrainControls').innerHTML=Fields.map(([Title,Open,Entries])=>`<details ${Open?'open':''}><summary>${Title}</summary><div class="ControlGroup">${Entries.map(([Name,Label,Minimum,Maximum,Step,Unit])=>Name==='Seed'?
        `<div class="Property"><label class="FieldLabel" for="GrainSeed">${Label}</label><div class="SeedRow"><input id="GrainSeed" type="number" min="0" max="999999"><button id="GrainNewSeed">New seed</button></div></div>`:
        `<div class="Property"><label class="FieldLabel" for="Grain${Name}">${Label}<output id="Grain${Name}Value"></output></label><input id="Grain${Name}" type="range" min="${Minimum}" max="${Maximum}" step="${Step}" data-unit="${Unit}"></div>`).join('')}</div></details>`).join('');
    Find('GrainLibrary').innerHTML=Object.entries(MineralPresets).map(([Name,Preset])=>`<button class="PresetCard" data-preset="${Name}">${Icon}<span><strong>${Preset.Label}</strong><small>Minerals · cement · susceptibility</small></span></button>`).join('');

    function RefreshControls()
    {
        for (const [Name,Amount] of Object.entries(Parameters.Specification))
        {
            const Input=Find(`Grain${Name}`);if (!Input) continue;
            Input.value=Amount;
            if (Input.type==='range')
            {
                Input.style.setProperty('--Fill',`${(Amount-Number(Input.min))/(Number(Input.max)-Number(Input.min))*100}%`);
                Find(`Grain${Name}Value`).textContent=`${Number.isInteger(Amount)?Amount:Amount.toFixed(2)} ${Input.dataset.unit}`;
            }
        }
        Host.querySelectorAll('[data-preset]').forEach(Button=>Button.classList.toggle('Active',Button.dataset.preset===Parameters.Specification.Preset));
        Find('GrainTitle').textContent=MineralPresets[Parameters.Specification.Preset].Label;
        for(let Index=0;Index<4;++Index) Find(`GrainTint${Index}`).value=(Parameters.Palette??MineralPresets[Parameters.Specification.Preset].Colours)[Index];
    }
    function Buttons()
    {
        Find('GrainPlay').textContent=Parameters.Playing?'Pause':'Run weathering';
        for (const Name of ['GrainPlay','GrainStep','GrainTwenty','GrainDry']) Find(Name).disabled=Parameters.Dirty||!Parameters.Result||!!Parameters.Error||(Parameters.Busy&&(Name!=='GrainPlay'||!Parameters.Playing));
        Find('GrainSave').disabled=Parameters.Busy||Parameters.Dirty||!Parameters.Result||!!Parameters.Error;
        Find('GrainBuild').textContent=Parameters.Busy?'Restart grains':Parameters.Dirty?'Rebuild grains · pending':'Rebuild grains';
        Find('GrainBadge').textContent=Parameters.Error?'Generation failed':Parameters.Dirty?'Packing changed · rebuild required':Parameters.Busy?'Calculating…':Parameters.Result?.Metrics.Cycle?'Weathered material':'Unweathered material';
        if(!Parameters.Busy&&!Parameters.Dirty&&!Parameters.Error&&Parameters.Result?.Boundary.Metrics.ThinTriangles)
            Find('GrainBadge').textContent+=` · ${Parameters.Result.Boundary.Metrics.ThinTriangles} angle warnings`;
    }
    function Fail(Message)
    {
        Parameters.Error=Message;Parameters.Busy=false;Parameters.Playing=false;Parameters.Result=null;
        if (Surface) Surface.visible=false;if (Wire) Wire.visible=false;if (Selection) Selection.visible=false;
        Find('GrainFailure').hidden=false;Find('GrainFailure').textContent=Message;Requested=true;Buttons();
    }
    function Frame()
    {
        const Rect=Find('GrainViewport').getBoundingClientRect();if (!Rect.width||!Rect.height) return;
        Camera.aspect=Rect.width/Rect.height;
        const Size=Parameters.Result?.Size??.48;
        const Angle=Math.min(Camera.fov*Math.PI/360,Math.atan(Math.tan(Camera.fov*Math.PI/360)*Camera.aspect));
        const Distance=Size*.83/Math.sin(Angle)*(Rect.height<500?1.12:1);
        Controls.target.set(0,Rect.height<500?Size*.035:0,-(Parameters.Result?.Depth??.005)*2);
        Camera.position.copy(Controls.target).addScaledVector(new THREE.Vector3(.22,.15,1).normalize(),Distance);
        Camera.updateProjectionMatrix();Controls.update();Requested=true;
    }
    function Colour(Column,Layer)
    {
        const Grain=Column.Grains[Layer],Preset=MineralPresets[Parameters.Result.Specification.Preset];
        const Channel=Parameters.Channel;
        if (Channel==='Clay') return new THREE.Color('#b3b1a9');
        if (!Grain) return new THREE.Color('#544f49');
        const Saturation=Grain.Water/Grain.Capacity;
        if (Channel==='Moisture') return new THREE.Color('#303335').lerp(new THREE.Color('#579cca'),Saturation);
        if (Channel==='Bonds') return new THREE.Color('#b76e4f').lerp(new THREE.Color('#c5d3b3'),Math.min(1,Grain.Bond/Grain.InitialBond));
        if (Channel==='Oxide') return new THREE.Color('#686762').lerp(new THREE.Color('#b14d25'),Math.min(1,Grain.Oxide*3+Column.Deposit/(Column.Area*Parameters.Result.Depth)*350));
        if (Channel==='Exposure') return new THREE.Color('#786857').lerp(new THREE.Color('#bdcfbd'),(Layer+1)/Parameters.Result.Specification.Layers);
        if (Channel==='Weakness') return new THREE.Color('#b7b5ad').lerp(new THREE.Color('#af674c'),Column.Weak);
        return new THREE.Color((Parameters.Palette??Preset.Colours)[Grain.Mineral]).lerp(new THREE.Color('#994a27'),Math.min(.8,Grain.Oxide+Column.Deposit/(Column.Area*Parameters.Result.Depth)*80)).multiplyScalar(1-Saturation*.13);
    }
    function Recolour()
    {
        if (!Surface||!Parameters.Result) return;
        const Boundary=Parameters.Result.Boundary,Colours=Surface.geometry.attributes.color;
        Boundary.Triangles.forEach((Triangle,Index)=>
        {
            const Tint=Colour(Parameters.Result.Columns[Boundary.Owners[Index]],Boundary.Layers[Index]);
            for (let Corner=0;Corner<3;++Corner) Colours.setXYZ(Index*3+Corner,Tint.r,Tint.g,Tint.b);
        });
        Colours.needsUpdate=true;Requested=true;
    }
    function Inspect()
    {
        if (Selection) {Scene.remove(Selection);Selection.geometry.dispose();Selection=null;}
        if (!Parameters.Selected||!Parameters.Result)
        {Find('GrainSelection').textContent='Click a visible grain to inspect its bonds, water and source attachment.';return;}
        const {Column:Index,Layer}=Parameters.Selected,Column=Parameters.Result.Columns[Index],Grain=Column?.Grains[Layer];
        if (!Column||!Grain) {Find('GrainSelection').textContent='Permanent backing · not an erodible grain';return;}
        const Attachment=Column.Attachment;
        Find('GrainSelection').innerHTML=`<b>Grain ${Index}.${Layer}</b><div class="Metrics"><span>Mineral</span><b>${['Quartz-rich','Feldspar-rich','Cement-rich','Iron-bearing'][Grain.Mineral]}</b>
            <span>Condition</span><b>${Grain.Alive?'Retained':'Detached'}</b><span>Bond / original</span><b>${Grain.Bond.toFixed(3)} / ${Grain.InitialBond.toFixed(3)}</b>
            <span>Moisture</span><b>${(Grain.Water/Grain.Capacity*100).toFixed(1)}%</b><span>Oxidation</span><b>${(Grain.Oxide*100).toFixed(1)}%</b></div>
            <p>${Attachment?`Source triangle ${Attachment.TriangleIndex??'snapshot'} · barycentric<br>${Attachment.Barycentric.map(Amount=>Amount.toFixed(4)).join(' / ')}`:'Standalone sample · no cliff attachment'}</p>`;
        const Positions=[],Boundary=Parameters.Result.Boundary;
        Boundary.Triangles.forEach((Triangle,Face)=>{if (Boundary.Owners[Face]===Index&&Boundary.Layers[Face]===Layer) for (const Vertex of Triangle) Positions.push(...Boundary.Vertices[Vertex]);});
        const Geometry=new THREE.BufferGeometry();Geometry.setAttribute('position',new THREE.Float32BufferAttribute(Positions,3));
        Selection=new THREE.Mesh(Geometry,SelectionMaterial);Scene.add(Selection);Requested=true;
    }
    function Display()
    {
        const Result=Parameters.Result,Boundary=Result.Boundary;
        if (Surface) {Scene.remove(Surface);Surface.geometry.dispose();}
        if (Wire) {Scene.remove(Wire);Wire.geometry.dispose();}
        const Geometry=new THREE.BufferGeometry();
        const Positions=Boundary.Triangles.flatMap(Triangle=>Triangle.flatMap(Index=>Boundary.Vertices[Index]));
        Geometry.setAttribute('position',new THREE.Float32BufferAttribute(Positions,3));
        Geometry.setAttribute('color',new THREE.Float32BufferAttribute(new Float32Array(Positions.length),3));Geometry.computeVertexNormals();
        Surface=new THREE.Mesh(Geometry,Material);Surface.castShadow=true;Surface.receiveShadow=true;Scene.add(Surface);
        Wire=new THREE.LineSegments(new THREE.WireframeGeometry(Geometry),WireMaterial);Wire.visible=Parameters.Wire;Scene.add(Wire);
        Recolour();Inspect();
        const Metrics=Result.Metrics,Topology=Boundary.Metrics;
        Find('GrainCensus').textContent=`${Metrics.Grains.toLocaleString()} grains · ${Result.Columns.length} contact cells`;
        Find('GrainClock').textContent=`Cycle ${Metrics.Cycle} / 2000`;
        Find('GrainSource').textContent=`${(Result.Size*100).toFixed(1)} cm patch · ${(Result.Size/Result.Specification.Resolution*1000).toFixed(1)} mm nominal grains. ${Result.Source?`${Result.Source.BodyName}, stage ${Result.Source.Stage} · frozen face sample.`:'Standalone material sample.'}`;
        Find('GrainMetrics').innerHTML=[['Retained / detached',`${Metrics.Grains-Metrics.Removed} / ${Metrics.Removed}`],['Removed solid volume',`${(Metrics.RemovedVolume*1e6).toFixed(2)} cm³`],
            ['Surface moisture',`${(Metrics.Moisture*100).toFixed(1)}%`],['Mean oxidation',`${(Metrics.Oxidation*100).toFixed(2)}%`],['Deposited tracer',Metrics.Staining.toExponential(3)],
            ['Water balance residual',Metrics.WaterResidual.toExponential(2)],['Tracer balance residual',Metrics.TracerResidual.toExponential(2)],
            ['Open / nonmanifold edges',`${Topology.OpenEdges} / ${Topology.NonmanifoldEdges}`],['Winding / zero area',`${Topology.WindingErrors} / ${Topology.ZeroArea}`],
            ['Nonmanifold vertices / duplicates',`${Topology.NonmanifoldVertices} / ${Topology.DuplicateTriangles}`],['Triangles / below 5°',`${Topology.Triangles.toLocaleString()} / ${Topology.ThinTriangles}`],
            ['Minimum angle',`${Topology.MinimumAngle.toFixed(2)}°`],['Minimum triangle area',`${Topology.MinimumArea.toExponential(2)} m²`],
            ['Degenerate-area threshold',`${Topology.AreaTolerance.toExponential(2)} m²`]].map(([Label,Reading])=>`<span>${Label}</span><b>${Reading}</b>`).join('');
        Requested=true;
    }
    function Send(Command,Count=1,Recipe=null)
    {
        if (Command==='Step'&&(Parameters.Busy||Parameters.Dirty||!Parameters.Result)) return;
        if (Command==='Step'&&Parameters.Result.Metrics.Cycle>=2000) {Parameters.Playing=false;Buttons();return;}
        const Reframe=Command!=='Step';
        if (Reframe) {GrainWorker?.terminate();GrainWorker=null;Parameters.Playing=false;Parameters.Dirty=false;Parameters.Selected=null;}
        try {if (!GrainWorker) GrainWorker=new Worker(new URL('./GrainQueue.js',import.meta.url),{type:'module'});}
        catch(Error) {Fail(Error.message);return;}
        const Revision=++Parameters.Revision;Parameters.Busy=true;Parameters.Error=null;Find('GrainFailure').hidden=true;Buttons();
        GrainWorker.onmessage=Event=>
        {
            const Result=Event.data;if (Result.Revision!==Parameters.Revision) return;
            if (Result.Error) {Fail(Result.Error);return;}
            Parameters.Result=Result;Parameters.ReadyRevision=Revision;Parameters.Busy=false;
            if (Reframe) {Parameters.Specification={...(Result.PendingSpecification??Result.Specification)};Parameters.Source=Result.Source;
                if(Command==='Restore') Parameters.Palette=Result.Palette;
                RefreshControls();}
            Display();if (Reframe) Frame();Buttons();
            if (Parameters.Playing&&Parameters.Active&&!Parameters.Dirty) setTimeout(()=>{if(Parameters.Playing) Send('Step',20);},60);
        };
        GrainWorker.onerror=Event=>{if(Revision===Parameters.Revision)Fail(Event.message||'Grain worker failed');};
        GrainWorker.postMessage({Revision,Command,Count:Command==='Step'?Math.min(Count,2000-Parameters.Result.Metrics.Cycle):Count,
            Specification:Parameters.Specification,Source:Parameters.Source,Recipe});
    }
    function CancelPacking()
    {
        Parameters.Dirty=true;Parameters.Playing=false;
        if (Parameters.Busy) {GrainWorker?.terminate();GrainWorker=null;Parameters.Busy=false;++Parameters.Revision;}
    }
    for (const Name of Object.keys(GrainLimits)) Find(`Grain${Name}`).oninput=Event=>
    {
        Parameters.Specification=ReadGrainSpecification({...Parameters.Specification,[Name]:Number(Event.target.value)});
        if (PackingProperties.includes(Name)) {CancelPacking();}
        RefreshControls();Buttons();
    };
    Host.querySelectorAll('[data-preset]').forEach(Button=>Button.onclick=()=>
    {
        Parameters.Specification.Preset=Button.dataset.preset;Parameters.Palette=null;CancelPacking();RefreshControls();Buttons();
    });
    Find('GrainNewSeed').onclick=()=>{Parameters.Specification.Seed=(Parameters.Specification.Seed+1+crypto.getRandomValues(new Uint32Array(1))[0]%999999)%1000000;CancelPacking();RefreshControls();Buttons();};
    for(let Index=0;Index<4;++Index) Find(`GrainTint${Index}`).oninput=Event=>
    {
        Parameters.Palette=(Parameters.Palette??MineralPresets[Parameters.Specification.Preset].Colours).slice();
        Parameters.Palette[Index]=Event.target.value;Recolour();
    };
    Find('GrainPaletteReset').onclick=()=>{Parameters.Palette=null;RefreshControls();Recolour();};
    Find('GrainBuild').onclick=()=>Send('Build');
    Find('GrainCapture').onclick=()=>
    {
        try {Parameters.Source=AcquireSource();Send('Build');} catch(Error) {Find('GrainFailure').hidden=false;Find('GrainFailure').textContent=Error.message;}
    };
    Find('GrainPlay').onclick=()=>{Parameters.Playing=!Parameters.Playing;Buttons();if(Parameters.Playing&&!Parameters.Busy)Send('Step',20);};
    Find('GrainStep').onclick=()=>{Parameters.Playing=false;Send('Step',1);};
    Find('GrainTwenty').onclick=()=>{Parameters.Playing=false;Send('Step',20);};
    Find('GrainDry').onclick=()=>{Parameters.Specification.Rain=0;Parameters.Specification.Drying=.9;RefreshControls();Parameters.Playing=false;Send('Step',20);};
    Find('GrainChannel').onchange=Event=>{Parameters.Channel=Event.target.value;Recolour();};
    Find('GrainWire').onclick=()=>{Parameters.Wire=!Parameters.Wire;if(Wire)Wire.visible=Parameters.Wire;Find('GrainWire').classList.toggle('Active',Parameters.Wire);Requested=true;};
    Find('GrainFrame').onclick=Find('GrainFocus').onclick=Frame;
    Find('GrainSearch').oninput=Event=>Host.querySelectorAll('[data-preset]').forEach(Button=>{Button.hidden=!Button.textContent.toLowerCase().includes(Event.target.value.toLowerCase());});
    const Diagnostic=On=>
    {
        Find('GrainControls').hidden=On;Find('GrainDiagnostics').open=true;
        Find('GrainParametersTab').classList.toggle('Active',!On);Find('GrainDiagnosticsTab').classList.toggle('Active',On);
        Find('GrainDiagnostics').scrollIntoView({block:'nearest'});
    };
    Find('GrainParametersTab').onclick=()=>Diagnostic(false);Find('GrainDiagnosticsTab').onclick=()=>Diagnostic(true);
    function Download(Content)
    {
        const Url=URL.createObjectURL(new Blob([JSON.stringify(Content,null,2)],{type:'application/json'}));
        const Link=document.createElement('a');Link.href=Url;Link.download=`GrainStudy_${Parameters.Specification.Seed}.json`;Link.click();setTimeout(()=>URL.revokeObjectURL(Url),1000);
    }
    Find('GrainSave').onclick=()=>Download({...Parameters.Result.Recipe,PendingSpecification:Parameters.Specification,Palette:Parameters.Palette});
    Find('GrainLoad').onclick=()=>Find('GrainFile').click();
    Find('GrainFile').onchange=async Event=>
    {
        try {const File=Event.target.files[0];if(!File)return;if(File.size>2000000)throw new Error('Study recipe exceeds 2 MB');Send('Restore',1,JSON.parse(await File.text()));}
        catch(Error) {Find('GrainFailure').hidden=false;Find('GrainFailure').textContent=Error.message;}
        Event.target.value='';
    };
    Renderer.domElement.addEventListener('click',Event=>
    {
        if (!Surface||!Parameters.Result||Parameters.Dirty) return;
        const Rect=Renderer.domElement.getBoundingClientRect(),Ray=new THREE.Raycaster();
        Ray.setFromCamera(new THREE.Vector2((Event.clientX-Rect.left)/Rect.width*2-1,1-(Event.clientY-Rect.top)/Rect.height*2),Camera);
        const Hit=Ray.intersectObject(Surface,false)[0];if(!Hit)return;
        Parameters.Selected={Column:Parameters.Result.Boundary.Owners[Hit.faceIndex],Layer:Parameters.Result.Boundary.Layers[Hit.faceIndex]};Inspect();
    });
    Renderer.domElement.addEventListener('webglcontextlost',Event=>{Event.preventDefault();GrainWorker?.terminate();Fail('WebGL context lost. Reload to restore the study.');});
    window.addEventListener('keydown',Event=>
    {
        if (!Parameters.Active||['INPUT','SELECT','TEXTAREA'].includes(document.activeElement.tagName)) return;
        if (Event.key.toLowerCase()==='f') Frame();
        if (Event.code==='Space') {Event.preventDefault();if(!Find('GrainPlay').disabled)Find('GrainPlay').click();}
    });
    function Animate()
    {
        requestAnimationFrame(Animate);if(!Parameters.Active)return;
        if (Resize)
        {
            const Rect=Find('GrainViewport').getBoundingClientRect();if (!Rect.width||!Rect.height)return;
            Camera.aspect=Rect.width/Rect.height;Camera.updateProjectionMatrix();Renderer.setSize(Rect.width,Rect.height,false);Resize=false;Requested=true;
        }
        const Moved=Controls.update();if(Moved||Requested){Requested=false;Renderer.render(Scene,Camera);}
    }
    RefreshControls();Buttons();Animate();
    return {Parameters,Renderer,Camera,Controls,Scene,Send,Frame,Inspect,
        SetActive:Active=>
        {
            Parameters.Active=Active;Controls.enabled=Active;Host.hidden=!Active;
            if(!Active){Parameters.Playing=false;Buttons();return;}
            Resize=true;Requested=true;
            if(!Parameters.Result&&!Parameters.Busy)
            {
                try {Parameters.Source=AcquireSource();} catch {Parameters.Source=null;}
                Send('Build');
            }
        }};
}
