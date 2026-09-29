import './specimen-nav.js';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { createIcons, BookOpen, Download, ScanEye, Footprints, Crosshair, Shield, Box, Rotate3d, Triangle, GitFork, Tags, Focus, Maximize, Mouse, Pause, Play, SkipBack, X, Info } from 'lucide';
import './style.css';
import './hunt.css';
import { HuntView } from './hunt-view.js';
import { strikePhase } from './strike-motion.js';
import { WALK, walkContactLabel } from './walk-motion.js';
import { STANCE } from './stance-motion.js';
const icons = {BookOpen, Download, ScanEye, Footprints, Crosshair, Shield, Box, Rotate3d, Triangle, GitFork, Tags, Focus, Maximize, Mouse, Pause, Play, SkipBack, X, Info};
const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
const refreshIcons = () => createIcons({ icons, attrs: { 'aria-hidden': 'true' } });
refreshIcons();
const viewport = $('#viewport');
let toastTimer;
function toast(text) { const el = $('#toast'); el.textContent = text; el.classList.add('visible'); clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove('visible'), 2600); }

const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance', preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = .95;
renderer.outputColorSpace = THREE.SRGBColorSpace;
viewport.appendChild(renderer.domElement);
const scene = new THREE.Scene(); scene.background = new THREE.Color('#f1f1e9'); scene.fog = new THREE.Fog('#f1f1e9', .19, .55);
const camera = new THREE.PerspectiveCamera(34, 1, .0004, 3);
camera.position.set(.088, .060, .085);
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, .021, -.001); controls.enableDamping = true; controls.dampingFactor = .08;
controls.minDistance = .014; controls.maxDistance = .29; controls.maxPolarAngle = Math.PI * .51; controls.autoRotateSpeed = .55;
controls.enablePan = true;
const pmrem = new THREE.PMREMGenerator(renderer), room = new RoomEnvironment();
const env = pmrem.fromScene(room, .04).texture; scene.environment = env; scene.environmentIntensity = .55;
room.dispose(); pmrem.dispose();
const key = new THREE.DirectionalLight('#fff9ed', 2.2); key.position.set(-.06, .12, .06); key.castShadow = true;
key.shadow.mapSize.set(1024, 1024); key.shadow.camera.left = -.065; key.shadow.camera.right = .065;
key.shadow.camera.top = .075; key.shadow.camera.bottom = -.07; key.shadow.camera.near = .005; key.shadow.camera.far = .35;
key.shadow.bias = -.0003; key.shadow.normalBias = .00014; key.shadow.radius = 4;
scene.add(key);
const fill = new THREE.DirectionalLight('#e7edff', .60); fill.position.set(.1, .04, -.09); scene.add(fill);
const rim = new THREE.DirectionalLight('#f5f9ce', 1.0); rim.position.set(-.02, .05, -.1); scene.add(rim);
scene.add(new THREE.HemisphereLight('#f0f5df', '#b3b49a', .48));
const ground = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), new THREE.MeshStandardMaterial({ color: '#e8eadd', roughness: .99, metalness: 0 }));
ground.rotation.x = -Math.PI / 2; ground.position.y = -.00018; ground.receiveShadow = true; scene.add(ground);
// Very restrained measuring grid; fades before it becomes a horizon.
const grid = new THREE.Mesh(new THREE.PlaneGeometry(.25, .25), new THREE.ShaderMaterial({
  transparent: true, depthWrite: false, uniforms: { travelMM: { value: 0 }, strength: { value: .025 } },
  vertexShader: 'varying vec2 vUv; void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',
  fragmentShader: 'uniform float travelMM; uniform float strength; varying vec2 vUv; void main(){vec2 p=(vUv-.5)*250.; p.y-=travelMM; vec2 g=abs(fract(p/10.-.5)-.5)/fwidth(p/10.);float line=1.-min(min(g.x,g.y),1.);float fade=1.-smoothstep(.08,.48,length(vUv-.5));gl_FragColor=vec4(.35,.42,.27,line*strength*fade);}'
}));
grid.rotation.x = -Math.PI / 2; grid.position.y = -.000165; scene.add(grid);
let model, clips, mixer, skeletonHelper, action, currentClip = 'Idle', paused = window.matchMedia('(prefers-reduced-motion: reduce)').matches, playbackSpeed = 1, building = new URLSearchParams(location.search).has('build'), annotations = [], originalMaterials = new Map(), loaded = false;
const clock = new THREE.Clock(); let lastInterfaceTime = 0, lastFrame = 0, accumulated = 0, counted = 0;
let cameraTween = null, sceneDirty = true;
let currentMode = 'clips', hunt = null, exporting = false;
let walkCycles = 0, groundTravel = 0, walkGroundOrigin = 0;
// A feeding strike is a one-shot, not a continuous threat-display / waving loop.
const clipLoops = { Idle: true, Walk: true, Attack: false, Stance: true };
document.addEventListener('visibilitychange', () => clock.getDelta());
controls.addEventListener('change',()=>sceneDirty=true);
document.addEventListener('click',()=>sceneDirty=true);
document.addEventListener('input',()=>sceneDirty=true);
const meta = { Idle: { desc: 'Head tracking, antenna sensing & ventilation', type: 'CYCLIC MOTION' }, Walk: { desc: 'Slow wave gait · tracking ground at 2.5 mm/s', type: 'SLOW WALK' }, Attack: { desc: 'Fold → sweep & clamp → pull to mouth', type: 'PREDATORY STRIKE' }, Stance: { desc: 'Raised forelegs · wing display · held loop', type: 'THREAT DISPLAY' } };

function addEyeResponse(material) {
  // A pseudopupil is a view-dependent optical effect, not a painted vertebrate pupil.
  // Standard PBR / compound-facet maps remain in the portable GLB.
  material.onBeforeCompile = shader => {
    shader.fragmentShader = shader.fragmentShader.replace('#include <opaque_fragment>', `
      float alignment = max(0.0, dot(normalize(normal), normalize(vViewPosition)));
      float pseudo = smoothstep(.992, .9985, alignment);
      outgoingLight *= (1.0 - .39 * pseudo);
      #include <opaque_fragment>
    `);
  };
  material.customProgramCacheKey = () => 'mantis-compound-eye-v1';
}
async function loadModel() {
  try {
    let data;
    if (building) { const { createMantis } = await import('./mantis.js'); data = createMantis(); model = data.root; clips = data.clips; }
    else { const gltf = await new GLTFLoader().loadAsync('/models/mantis.glb'); model = gltf.scene; clips = gltf.animations; }
    scene.add(model); model.updateMatrixWorld(true);
    let triangleCount = 0, boneSet = new Set();
    model.traverse(obj => {
      if (obj.isBone) boneSet.add(obj.name);
      if (obj.isMesh) {
        obj.castShadow = !['Vein ridges','Setae'].includes(obj.material?.name); obj.receiveShadow = true; obj.frustumCulled = false;
        const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
        mats.forEach(mat => { originalMaterials.set(mat, { wireframe: mat.wireframe, opacity: mat.opacity, transparent: mat.transparent, depthWrite: mat.depthWrite }); if (mat.name === 'Compound eye facets') addEyeResponse(mat); if (mat.map) mat.map.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy()); });
        triangleCount += obj.geometry.index ? obj.geometry.index.count / 3 : obj.geometry.attributes.position.count / 3;
      }
    });
    $('#triangle-count').textContent = `${(triangleCount / 1000).toFixed(1)}k triangles`;
    $('#bone-count').textContent = `${boneSet.size} deform bones`;
    skeletonHelper = new THREE.SkeletonHelper(model); skeletonHelper.material.depthTest = false; skeletonHelper.material.transparent = true; skeletonHelper.material.opacity = .9; skeletonHelper.material.linewidth = 2; skeletonHelper.visible = false; skeletonHelper.renderOrder = 12; scene.add(skeletonHelper);
    hunt = new HuntView({model,scene,camera,controls,canvas:renderer.domElement,onRun:()=>{paused=false;updatePlayButton();},invalidate:()=>sceneDirty=true,freezeCamera:()=>cameraTween=null});
    hunt.rig.reset(); model.updateMatrixWorld(true);
    mixer = new THREE.AnimationMixer(model);
    mixer.addEventListener('loop', event => { if (currentClip === 'Walk' && event.action === action) walkCycles += event.loopDelta; });
    mixer.addEventListener('finished', event => { if (event.action !== action || currentMode !== 'clips') return; paused = true; updatePlayButton(); });
    setClip('Idle', true);
    setupAnnotations(); loaded = true;
    $('#loading').classList.add('hidden');
    sceneDirty = true; window.mantisReady = true;
    window.mantis = { model, clips, mixer, renderer, scene, camera, controls, hunt, setMode, get mode() { return currentMode; }, get paused() { return paused; }, setPaused(value) { paused=Boolean(value);updatePlayButton();sceneDirty=true; }, stats: { triangles: triangleCount, bones: boneSet.size }, get walkDistance() { return groundTravel; }, setClip, exportGLB, poseAt(name, time) { setClip(name, true); action.paused = false; action.time = time; mixer.update(0); action.paused = true; paused = true; updatePlayButton(); syncWalkSurface(); renderer.render(scene, camera); }, resetView: () => setView('perspective') };
    if(!building && new URLSearchParams(location.search).get('mode') !== 'clips') setMode('hunt',true);
  } catch (error) {
    console.error(error); const loading = $('#loading'); loading.replaceChildren();
    const title = document.createElement('span'); title.textContent = 'The specimen could not load.';
    const message = document.createElement('small'); message.textContent = 'Please reload the viewer. The GLB is also available using Download model.';
    const button = document.createElement('button'); button.className = 'button primary'; button.textContent = 'Reload viewer'; button.addEventListener('click', () => location.reload());
    loading.append(title, message, button);
    window.mantisError = error.message;
  }
}
function updatePlayButton() {
  const finished=action&&action.time>=action.getClip().duration;
  $('#play-button').innerHTML=`<i data-lucide="${paused?'play':'pause'}"></i>`;
  $('#play-button').setAttribute('aria-label',paused?(finished?`Replay ${currentClip.toLowerCase()} animation`:'Play animation'):'Pause animation');
  $('#hunt-pause').innerHTML=`<i data-lucide="${paused?'play':'pause'}"></i>`;
  $('#hunt-pause').setAttribute('aria-label',paused?'Resume target test':'Pause target test');refreshIcons();
}
function syncModeUI(){
  document.body.classList.toggle('hunt-mode',currentMode==='hunt');
  $$('[data-mode]').forEach(b=>{const active=b.dataset.mode===currentMode;b.classList.toggle('active',active);b.setAttribute('aria-pressed',active);});
  $('#viewport').setAttribute('aria-label',currentMode==='hunt'?'Interactive mantis target test. Drag the amber bead, or use the direction, distance and height controls.':'Interactive 3D praying mantis. Drag to orbit, scroll to zoom, right-drag to pan.');
}
function setMode(mode,initial=false){
  if(!hunt||!loaded||!['hunt','clips'].includes(mode))return;
  if(mode===currentMode)return;
  if(mode==='hunt'){
    mixer.stopAllAction();currentMode='hunt';syncModeUI();hunt.enable();
    if(!initial)paused=false;
    $('#viewport-mode').textContent='TARGET TEST';
  }else{
    hunt.disable();currentMode='clips';syncModeUI();setClip(currentClip,true);
  }
  resize();setView($('.view-controls button.active')?.dataset.view||'perspective');updatePlayButton();sceneDirty=true;
}
function togglePlayback(){
  if(!action)return;paused=!paused;
  if(currentMode==='clips'&&!paused&&action.time>=action.getClip().duration)action.reset().play();
  updatePlayButton();sceneDirty=true;
}
function setClip(name, immediate = false) {
  if (!mixer) return;
  const next = clips.find(c => c.name === name); if (!next) return;
  if(currentMode==='hunt'){hunt.disable();currentMode='clips';syncModeUI();}
  const old = action; if (immediate) mixer.stopAllAction(); action = mixer.clipAction(next); action.reset(); action.enabled = true; action.setEffectiveWeight(1); action.timeScale = playbackSpeed;
  $('#loop-toggle').checked = clipLoops[name];
  action.setLoop(clipLoops[name] ? THREE.LoopRepeat : THREE.LoopOnce, clipLoops[name] ? Infinity : 1); action.clampWhenFinished = true; action.play();
  if (old && old !== action) { if (immediate) old.stop(); else { const fade = name === 'Stance' ? STANCE.blendSeconds : currentClip === 'Stance' && name !== 'Attack' ? .45 : .18; old.fadeOut(fade); action.fadeIn(fade); } }
  const changed = currentClip !== name; currentClip = name; action.paused = paused;
  resize();
  if (name === 'Walk') { walkCycles = 0; walkGroundOrigin = immediate ? 0 : groundTravel; }
  $('#viewport-mode').textContent = name === 'Walk' ? 'TRACKING · 2.5 MM/S' : name === 'Stance' ? 'DEFENSIVE DISPLAY' : 'ANATOMY STUDY';
  syncWalkSurface();
  if(changed && !immediate && $('.view-controls button.active')?.dataset.view === 'perspective') setView('perspective');
  $$('.motion-card').forEach(b => { const active = b.dataset.clip === name; b.classList.toggle('active', active); b.setAttribute('aria-pressed', active); });
  $('#current-clip').textContent = name; $('#clip-description').textContent = meta[name].desc; $('#clip-type').textContent = meta[name].type;
  $('#scrubber').max = next.duration; $('#scrubber').value = 0; $('#total-time').textContent = next.duration.toFixed(2).padStart(5, '0');
  $('.timeline-ticks').innerHTML = Array.from({ length: 7 }, (_, i) => `<span>${Number((next.duration * i / 6).toFixed(1))}${i === 6 ? ' s' : ''}</span>`).join('');
  updatePlayButton();
}
function setupAnnotations() {
  const specs = [ ['Head', 'COMPOUND EYES', [0, .27, .15]], ['Fore_R_Femur', 'RAPTORIAL FORELEG', [.1, .17, .77]], ['Tegmen_L', 'VEINED TEGMINA', [-.15, -.12, -1.8]], ['Hind_R_Tarsus', '5-PART TARSUS', [.12, -.07, -.13]] ];
  specs.forEach(([name, label, offset]) => {
    const bone = model.getObjectByName(name); if (!bone) return;
    const el = document.createElement('div'); el.className = 'anatomy-label'; el.textContent = label; $('#annotations').appendChild(el);
    annotations.push({ bone, offset: new THREE.Vector3(...offset), el });
  });
}
function updateAnnotations() {
  if (!$('#annotations').classList.contains('visible')) return;
  const box = viewport.getBoundingClientRect();
  for (const { bone, offset, el } of annotations) {
    const p = offset.clone(); bone.localToWorld(p); p.project(camera);
    el.style.left = `${(p.x + 1) / 2 * box.width}px`; el.style.top = `${(1 - p.y) / 2 * box.height}px`; el.style.display = Math.abs(p.x) > .98 || Math.abs(p.y) > .96 || p.z > 1 ? 'none' : '';
  }
}
function setView(view) {
  const views = { perspective: {p:[.088,.060,.085],t:[0,.021,-.001]}, front:{p:[0,.034,.135],t:[0,.024,.003]}, side:{p:[.132,.045,.012],t:[0,.021,-.002]}, top:{p:[.001,.158,.001],t:[0,.01,0]} };
  if(view === 'perspective' && currentClip === 'Attack') views.perspective = {p:[.100,.063,.105],t:[0,.023,.005]};
  if(currentClip === 'Stance') {
    views.perspective = {p:[.055,.060,.138],t:[0,.026,-.001]};
    views.front = {p:[0,.041,.156],t:[0,.027,-.002]};
    views.side = {p:[.151,.052,.012],t:[0,.027,-.002]};
  }
  if(currentMode==='hunt'){
    views.perspective={p:[.102,.068,.126],t:[0,.024,.008]};
    views.front={p:[0,.038,.168],t:[0,.025,.008]};
    views.side={p:[.151,.051,.012],t:[0,.024,.010]};
    views.top={p:[.001,.165,.018],t:[0,.01,.009]};
  }
  const v = views[view]; if (!v) return;
  cameraTween = { duration: currentMode === 'hunt' ? 380 : currentClip === 'Attack' ? 210 : 550, start: performance.now(), from:camera.position.clone(), to:new THREE.Vector3(...v.p), targetFrom:controls.target.clone(), targetTo:new THREE.Vector3(...v.t) };
  $$('.view-controls button').forEach(b => { const on = b.dataset.view === view; b.classList.toggle('active', on); b.setAttribute('aria-pressed', on); });
}
function resize() { const w = viewport.clientWidth, h = viewport.clientHeight; if (!w || !h) return; renderer.setSize(w,h); camera.aspect = w/h; camera.fov = w/h < 1 ? (currentMode === 'hunt' ? 44 : currentClip === 'Stance' ? 46 : 56) : 34; camera.updateProjectionMatrix(); sceneDirty = true; }
new ResizeObserver(resize).observe(viewport); resize();
function syncWalkSurface() {
  // An explicitly labelled tracking view of the in-place asset: scroll the
  // reference grid at the exact controller speed, so planted feet track it.
  // This is a display aid, not hidden root motion in the downloadable Walk clip.
  if (action && currentMode === 'clips' && currentClip === 'Walk') groundTravel = walkGroundOrigin + (walkCycles * action.getClip().duration + action.time) * WALK.speedMetresPerSecond;
  grid.material.uniforms.travelMM.value = (groundTravel * 1000) % 10;
  grid.material.uniforms.strength.value = currentMode === 'clips' && currentClip === 'Walk' ? .065 : .025;
}
function loop() {
  requestAnimationFrame(loop);
  const dt = clock.getDelta(), now = performance.now();
  // Animation is wall-clock playback, not a fixed-step physics simulation.
  // Clamping dt used to turn a rapid strike into a slow reach on slower devices.
  if (document.hidden) return;
  if (mixer && currentMode === 'clips' && !exporting) { action.paused = paused; mixer.update(dt); }
  syncWalkSurface();
  if (cameraTween) { const t = Math.min((now-cameraTween.start)/cameraTween.duration,1), e = t*t*(3-2*t); camera.position.lerpVectors(cameraTween.from,cameraTween.to,e); controls.target.lerpVectors(cameraTween.targetFrom,cameraTween.targetTo,e); if(t===1)cameraTween=null; }
  controls.update();
  if(currentMode==='hunt'&&hunt&&!exporting)hunt.update(paused?0:dt*playbackSpeed);
  updateAnnotations();
  if(!paused || sceneDirty || cameraTween || controls.autoRotate) { renderer.render(scene,camera); sceneDirty = false; }
  if (action && now-lastInterfaceTime>60) {
    const time = action.time, duration=action.getClip().duration;
    $('#scrubber').value=time; $('#scrubber').style.background=`linear-gradient(to right,#93a96c ${time/duration*100}%,#dfe7d2 ${time/duration*100}%)`;
    $('#current-time').textContent=time.toFixed(2).padStart(5,'0');
    $('#clip-type').textContent = currentClip === 'Attack' ? strikePhase(time) : currentClip === 'Walk' ? walkContactLabel(time) : meta[currentClip].type;
    lastInterfaceTime=now;
    // A projected 10 mm reference, not an arbitrary fixed-width ruler.
    const target=controls.target.clone(); const right=new THREE.Vector3(1,0,0).applyQuaternion(camera.quaternion).multiplyScalar(.01);
    const a=target.clone().project(camera),b=target.add(right).project(camera);
    const width=Math.abs(b.x-a.x)*viewport.clientWidth/2;
    if(width>28&&width<280)$('.scale-indicator').style.width=`${width}px`;
  }
  if(lastFrame){accumulated+=now-lastFrame;counted++;} lastFrame=now;
  if(accumulated>2400){$('#render-status').textContent=`WEBGL 2 · ${Math.round(counted*1000/accumulated)} FPS`;counted=0;accumulated=0;}
}
loadModel(); loop();

$$('[data-mode]').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode)));
$$('.motion-card').forEach(b=>b.addEventListener('click',()=>{paused=false;setClip(b.dataset.clip);}));
$$('.speed-control button').forEach(b=>b.addEventListener('click',()=>{playbackSpeed=Number(b.dataset.speed); if(action)action.timeScale=playbackSpeed;$('#speed-label').textContent=`${playbackSpeed.toFixed(playbackSpeed===.25?2:1)}×`;$$('.speed-control button').forEach(x=>{const on=x===b;x.classList.toggle('active',on);x.setAttribute('aria-pressed',on);});}));
$('#loop-toggle').addEventListener('change',e=>{clipLoops[currentClip]=e.target.checked;if(action){action.setLoop(e.target.checked?THREE.LoopRepeat:THREE.LoopOnce,e.target.checked?Infinity:1);action.clampWhenFinished=true;}});
$('#play-button').addEventListener('click',togglePlayback);
$('#hunt-pause').addEventListener('click',togglePlayback);
$('#restart-button').addEventListener('click',()=>{if(action){if(currentClip==='Walk'){walkCycles=0;walkGroundOrigin=groundTravel;}action.reset().play();action.timeScale=playbackSpeed;paused=false;updatePlayButton();}});
let wasPaused=false;
$('#scrubber').addEventListener('focus',()=>{wasPaused=paused;});
$('#scrubber').addEventListener('pointerdown',()=>{wasPaused=paused;paused=true;});
$('#scrubber').addEventListener('input',e=>{if(!action)return;action.paused=false;action.time=Number(e.target.value);mixer.update(0);action.paused=true;paused=true;updatePlayButton();});
$('#scrubber').addEventListener('change',()=>{paused=wasPaused;updatePlayButton();});
$$('.view-controls button').forEach(b=>b.addEventListener('click',()=>setView(b.dataset.view)));
function toggleButton(id,fn){const el=$(id);el.addEventListener('click',()=>{const on=el.getAttribute('aria-pressed')!=='true';el.setAttribute('aria-pressed',on);el.classList.toggle('active',on);fn(on);});}
toggleButton('#rotate-toggle',on=>controls.autoRotate=on);
toggleButton('#wireframe-toggle',on=>{originalMaterials.forEach((_,mat)=>mat.wireframe=on);toast(on?'Wireframe · real mesh topology':'Textured PBR surface');});
toggleButton('#skeleton-toggle',on=>{if(skeletonHelper)skeletonHelper.visible=on;toast(on?'Deformation skeleton visible':'Skeleton hidden');});
toggleButton('#labels-toggle',on=>{$('#annotations').classList.toggle('visible',on);$('#annotations').setAttribute('aria-hidden',!on);});
$('#reset-camera').addEventListener('click',()=>{setView('perspective');controls.autoRotate=false;$('#rotate-toggle').classList.remove('active');$('#rotate-toggle').setAttribute('aria-pressed','false');toast('Camera reset');});
toggleButton('#fullscreen-button',on=>{document.body.classList.toggle('focus-mode',on);toast(on?'Focus mode · Esc to exit':'Studio layout restored');});
$('#references-button').addEventListener('click',()=>$('#references-dialog').showModal());
$('#close-references').addEventListener('click',()=>$('#references-dialog').close());
$('#references-dialog').addEventListener('click',e=>{if(e.target===$('#references-dialog')){const r=e.target.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)e.target.close();}});
document.addEventListener('keydown',e=>{if(e.repeat)return;if(e.key==='Escape'&&document.body.classList.contains('focus-mode'))$('#fullscreen-button').click();if(e.code==='Space'&&!['INPUT','BUTTON','A'].includes(document.activeElement.tagName)&&!$('#references-dialog').open){e.preventDefault();togglePlayback();}if(['1','2','3','4'].includes(e.key)&&!document.activeElement.matches('input,textarea,select,[contenteditable=true]')&&!$('#references-dialog').open){paused=false;setClip(['Idle','Walk','Attack','Stance'][Number(e.key)-1]);}if(currentMode==='hunt'&&!document.activeElement.matches('input,textarea,select,[contenteditable=true]')&&!$('#references-dialog').open){if(e.key.toLowerCase()==='f')hunt.command();if(e.key.toLowerCase()==='r')hunt.reset();}});
$('.download').addEventListener('click',e=>{if(building){e.preventDefault();exportGLB().then(buffer=>{const url=URL.createObjectURL(new Blob([buffer],{type:'model/gltf-binary'}));const a=document.createElement('a');a.href=url;a.download='frontier-mantis.glb';a.click();setTimeout(()=>URL.revokeObjectURL(url),10000);}).catch(err=>toast(err.message));}else toast('GLB: four animation clips. Target behavior runs in the viewer.');});

async function exportGLB() {
  if(!loaded)throw new Error('The specimen is still loading.');
  const wasPlaying=!paused,oldClip=currentClip,oldTime=action.time,oldMode=currentMode;
  exporting=true;paused=true;if(hunt)hunt.rig.reset();
  mixer.stopAllAction();model.traverse(obj=>{if(obj.isSkinnedMesh)obj.skeleton.pose();});model.updateMatrixWorld(true);
  const original=[];originalMaterials.forEach((value,m)=>{original.push([m,m.wireframe]);m.wireframe=false;});
  try {return await new GLTFExporter().parseAsync(model,{binary:true,animations:clips,onlyVisible:true,trs:true,maxTextureSize:2048});}
  finally{original.forEach(([m,w])=>m.wireframe=w);paused=!wasPlaying;exporting=false;if(oldMode==='hunt')hunt.update(0);else{setClip(oldClip,true);action.time=oldTime;}updatePlayButton();}
}
