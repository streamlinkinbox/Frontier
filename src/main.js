import './style.css';
import {surfaceSettings,updateRockSurface,referenceStoneFinish} from './surface-material.js';
import {rebuildSurfaceDetails} from './surface-details.js';
import {buildMountain} from './mountain.js';
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {OBJExporter} from 'three/addons/exporters/OBJExporter.js';
import {createIcons,Download,ChevronDown,Layers3,Mountain,ChevronsUpDown,Gem,Info,Box,Shuffle,Sparkles,CircleHelp,Scan,Triangle,Rotate3d,Maximize,Camera,Mouse,Ruler,Component,Search} from 'lucide';
import {buildQuarry,disposeQuarry,palettes,formations} from './quarry.js';
createIcons({icons:{Download,ChevronDown,Layers3,Mountain,ChevronsUpDown,Gem,Info,Box,Shuffle,Sparkles,CircleHelp,Scan,Triangle,Rotate3d,Maximize,Camera,Mouse,Ruler,Component,Search}});
const $=id=>document.getElementById(id);
let toastTimer;function toast(text){$('toast').textContent=text;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,3600);}
let mode='mountain';
const surfaceRanges=['oxidation','weathering','grain','layerDepth','grainHeight','grainSize','mineralFlakes','flakeMetallic','flakeRoughness','flakeSpecular','flakeClearcoat','flakeCoatRoughness','flakeColor','flakeRelief','flakeIOR','peeling','crystals'];
const ranges=['terraces','height','width','radius','fracture','mountainHeight','mountainRadius','ridges','steepness','detail','distortion','elongation'];
function updateRange(id){const e=$(id);const p=(e.value-e.min)/(e.max-e.min)*100;e.style.background=`linear-gradient(to right, #c4dba0 ${p}%, #454d3c ${p}%)`;const val=Number(e.value);$(id+'-out').innerHTML=id==='flakeIOR'?val.toFixed(2):id==='grainSize'?`${val<100?val:val<1000?(val/10).toFixed(1):(val/1000).toFixed(2)} <small>${val<100?'mm':val<1000?'cm':'m'}</small>`:id==='height'||id==='width'?`${val.toFixed(1)} <small>m</small>`:['radius','mountainHeight','mountainRadius'].includes(id)?`${val} <small>m</small>`:['fracture','steepness','detail','distortion','elongation',...surfaceRanges].includes(id)?`${val}<small>%</small>`:val;}
function markDirty(){ $('generation-status').textContent='Parameters changed';$('generation-time').textContent='Not generated'; }
ranges.forEach(id=>{updateRange(id);$(id).addEventListener('input',()=>{updateRange(id);markDirty();});});
['geology','seed','debris','meshStyle','cliffShape'].forEach(id=>$(id).addEventListener('input',markDirty));
function currentSurface(){return {surfaceEnabled:$('surfaceEnabled').checked,...Object.fromEntries(surfaceRanges.map(id=>[id,Number($(id).value)]))};}
function settings(){return {...currentSurface(),mode,cliffShape:$('cliffShape').value,meshStyle:$('meshStyle').value,...Object.fromEntries(ranges.map(id=>[id,Number($(id).value)])),seed:Math.max(0,Math.min(999999,Number($('seed').value)||0)),geology:$('geology').value,debris:$('debris').checked};}
const renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.12;
$('canvas-container').appendChild(renderer.domElement);renderer.domElement.setAttribute('aria-label','Interactive 3D mountain and quarry. Drag to orbit, scroll to zoom.');
const scene=new THREE.Scene();scene.background=new THREE.Color('#252d23');scene.fog=new THREE.FogExp2('#252d23',.0016);
const camera=new THREE.PerspectiveCamera(36,1,.015,1600);
const controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.dampingFactor=.075;controls.minDistance=.3;controls.maxDistance=600;controls.maxPolarAngle=Math.PI*.485;
const hemi=new THREE.HemisphereLight(0xe5ebef,0x424036,1.1);scene.add(hemi);
const sun=new THREE.DirectionalLight(0xfff2df,3);sun.position.set(-110,125,65);sun.castShadow=true;sun.shadow.mapSize.set(2048,2048);sun.shadow.camera.left=-170;sun.shadow.camera.right=170;sun.shadow.camera.top=170;sun.shadow.camera.bottom=-170;sun.shadow.camera.far=500;sun.shadow.normalBias=.12;sun.shadow.bias=-.0001;sun.shadow.radius=3;scene.add(sun);
const fill=new THREE.DirectionalLight(0xd0dbe3,.7);fill.position.set(80,50,-100);scene.add(fill);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(2000,2000),new THREE.MeshStandardMaterial({color:0x20281f,roughness:1}));ground.rotation.x=-Math.PI/2;ground.position.y=-3.15;ground.receiveShadow=true;scene.add(ground);
// Fine site reference lines; these do not modify the quarry surfaces.
const grid=new THREE.GridHelper(1200,120,0x46523b,0x46523b);grid.position.y=-3.12;grid.material.transparent=true;grid.material.opacity=.16;scene.add(grid);
let quarry,wireframe=false,baseOnly=false;
let renderFrames=4;
const invalidate=()=>{renderFrames=4;};
controls.addEventListener('change',invalidate);
function updateBaseView(){
  invalidate();
  quarry.children.forEach(o=>{if(o.isInstancedMesh)o.visible=!baseOnly;});
  const details=quarry.getObjectByName('Surface_microgeometry');if(details)details.visible=!baseOnly&&quarry.userData.surface.surfaceEnabled;
  $('base-view').classList.toggle('active',baseOnly);
  $('base-view').setAttribute('aria-pressed',String(baseOnly));
  $('scene-subtitle').textContent=`${palettes[quarry.userData.formation].name} · ${baseOnly?'BASE CLIFF SHAPE': 'DRESSED FORMATION'} · ${quarry.userData.mode==='mountain'?`${quarry.userData.ridges} RIDGES`:`${quarry.userData.settings.terraces} BENCHES`}`;
}
$('base-view').onclick=()=>{baseOnly=!baseOnly;updateBaseView();toast(baseOnly?'Base cliff only · bedding and buttresses remain modeled':'Rock dressing restored');};
function updateMeshUI(){
  $('lowpoly-controls').hidden=$('meshStyle').value!=='lowpoly'||mode!=='mountain';
  $('elongation-control').hidden=$('geology').value!=='sandstone';
  $('distortion').closest('.control').hidden=$('meshStyle').value!=='lowpoly'&&$('geology').value!=='sandstone';
  $('formation-description').textContent=$('meshStyle').value==='lowpoly'?({limestone:'Angular cliff slabs · broad fractured faces',sandstone:'Elongated boulders · warped and tapered forms',slate:'Tilted shards · long, angular blades'}[$('geology').value]):formations[$('geology').value].description;
}
$('geology').addEventListener('change',updateMeshUI);$('meshStyle').addEventListener('change',updateMeshUI);
function resetCamera(top=false){
  const {outer,depth}=quarry.userData,isMountain=quarry.userData.mode==='mountain';
  controls.target.set(isMountain?-outer*.06:0,depth*(isMountain?.45:.4),0);
  const box=new THREE.Box3().setFromObject(quarry),sphere=box.getBoundingSphere(new THREE.Sphere());
  controls.target.copy(sphere.center);
  const direction=new THREE.Vector3(top?.001:quarry.userData.cliffShape==='escarpment'?.7:1.7,top?3.4:isMountain?.8:1.85,top?0:2.5).normalize();
  const halfVertical=THREE.MathUtils.degToRad(camera.fov/2);
  const halfHorizontal=Math.atan(Math.tan(halfVertical)*camera.aspect);
  // Fit projected bounds rather than an overly conservative bounding sphere.
  const right=new THREE.Vector3().crossVectors(new THREE.Vector3(0,1,0),direction).normalize();
  const up=new THREE.Vector3().crossVectors(direction,right).normalize();
  let distance=0;
  for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z]){
    const offset=new THREE.Vector3(x,y,z).sub(controls.target);
    distance=Math.max(distance,offset.dot(direction)+Math.max(Math.abs(offset.dot(right))/Math.tan(halfHorizontal),Math.abs(offset.dot(up))/Math.tan(halfVertical)));
  }
  distance*=1.22;
  camera.position.copy(controls.target).addScaledVector(direction,distance);
  controls.maxDistance=Math.max(600,distance*3);camera.far=Math.max(1600,distance*6);camera.updateProjectionMatrix();
  camera.lookAt(controls.target);controls.update();invalidate();
  $('top-view').classList.toggle('active',top);$('orbit-view').classList.toggle('active',!top);
}
function updateModeUI(){
  updateMeshUI();
  const mountain=mode==='mountain';
  $('mode-mountain').setAttribute('aria-pressed',String(mountain));$('mode-quarry').setAttribute('aria-pressed',String(!mountain));
  $('mountain-controls').hidden=!mountain;$('quarry-controls').hidden=mountain;
  $('generator-title').textContent=mountain?'Mountain generator':'Quarry generator';
  $('generator-subtitle').textContent=mountain?'Raised by tectonics. Shaped by geology.':'Carved by industry. Shaped by geology.';
  $('generate-label').textContent=mountain?'Generate mountain':'Generate quarry';
  $('debris-description').textContent=mountain?'Scree beneath the cliff faces':'Rockfall along the benches';
  $('preset').querySelector('.mini-quarry').classList.toggle('mountain',mountain);
  $('preset').querySelector('strong').textContent=mountain?'Long escarpment':'Terraced quarry';
  $('preset').querySelector('div:nth-child(2)>span').textContent=mountain?'Natural · exposed rock':'Open-pit · sedimentary';
  document.querySelectorAll('[data-preset]').forEach(btn=>{btn.hidden=(['massif','spire','mesa'].includes(btn.dataset.preset))!==mountain;});
  $('preset-menu').hidden=true;
}
function switchMode(next){if(mode===next)return;mode=next;updateModeUI();generate();}
$('mode-mountain').onclick=()=>switchMode('mountain');
$('mode-quarry').onclick=()=>switchMode('quarry');
function generate(initial=false){
  clearTimeout(detailTimer);
  const t=performance.now(),config=settings();$('seed').value=config.seed;
  const next=config.mode==='mountain'?buildMountain(config):buildQuarry(config);
  next.userData.surface=surfaceSettings(config);rebuildSurfaceDetails(next,config);
  if(quarry){scene.remove(quarry);disposeQuarry(quarry);}quarry=next;scene.add(quarry);
  quarry.traverse(o=>{if(o.material)o.material.wireframe=wireframe;});
  const mountain=config.mode==='mountain';
  $('stat-count-label').textContent=mountain?'MAJOR RIDGES':'BENCHES';$('stat-height-label').textContent=mountain?'SUMMIT HEIGHT':'TOTAL DEPTH';
  $('stat-benches').textContent=String(mountain?config.ridges:config.terraces).padStart(2,'0');$('stat-depth').innerHTML=`${(mountain?config.mountainHeight:config.terraces*config.height).toFixed(1)} <small>m</small>`;
  $('scene-name').textContent=mountain?(config.cliffShape==='escarpment'?'ROCK ESCARPMENT':'CLIFF MOUNTAIN'):'TERRACED QUARRY';$('workspace-name').textContent=mountain?'Mountain 01':'Quarry 01';$('project-name').textContent=mountain?'Untitled mountain':'Untitled quarry';
  const bounds=Math.max(next.userData.outer,next.userData.depth)*1.5;
  sun.shadow.camera.left=-bounds;sun.shadow.camera.right=bounds;sun.shadow.camera.top=bounds;sun.shadow.camera.bottom=-bounds;sun.shadow.camera.far=bounds*6;
  sun.position.set(-bounds*.8,bounds*1.5,bounds*.6);sun.shadow.camera.updateProjectionMatrix();
  $('stat-rocks').textContent=quarry.userData.rockCount.toLocaleString();
  updateBaseView();updateMeshUI();
  updateMeshStats();
  $('generation-status').textContent='Generation complete';$('generation-time').textContent=`${((performance.now()-t)/1000).toFixed(2)}s`;
  resetCamera();if(!initial)toast(`${mountain?'Mountain':'Quarry'} generated · seed ${config.seed}`);
}
$('generate').onclick=()=>{ $('generate').disabled=true;$('generation-status').textContent='Building rock geometry…';requestAnimationFrame(()=>requestAnimationFrame(()=>{try{generate();}catch(e){console.error(e);toast('Could not generate this landform. Please try again.');}finally{$('generate').disabled=false;}}));};
$('randomize').onclick=()=>{$('seed').value=Math.floor(Math.random()*1000000);markDirty();};
$('preset').onclick=()=>$('preset-menu').hidden=!$('preset-menu').hidden;
const presetValues={massif:{cliffShape:'escarpment',mountainHeight:100,mountainRadius:72,ridges:5,steepness:78,fracture:65,geology:'limestone'},spire:{cliffShape:'spire',mountainHeight:150,mountainRadius:60,ridges:4,steepness:90,fracture:80,geology:'slate'},mesa:{cliffShape:'escarpment',mountainHeight:80,mountainRadius:90,ridges:7,steepness:85,fracture:55,geology:'sandstone'},classic:{terraces:5,height:7,width:8,radius:24,fracture:65,geology:'limestone'},deep:{terraces:7,height:9,width:6,radius:18,fracture:85,geology:'slate'},wide:{terraces:3,height:5,width:12,radius:36,fracture:45,geology:'limestone'}};
document.querySelectorAll('[data-preset]').forEach(btn=>btn.onclick=()=>{const p=presetValues[btn.dataset.preset];Object.entries(p).forEach(([id,val])=>{$(id).value=val;if(ranges.includes(id))updateRange(id);});$('preset').querySelector('strong').textContent=btn.textContent;$('preset').querySelector('div:nth-child(2)>span').textContent=`${mode==='mountain'?'Natural':'Open-pit'} · ${p.geology}`;$('preset-menu').hidden=true;updateMeshUI();markDirty();});
$('top-view').onclick=()=>resetCamera(true);$('orbit-view').onclick=()=>resetCamera();$('reset-view').onclick=()=>{controls.autoRotate=false;$('rotate').classList.remove('active');resetCamera();toast('Camera reset');};
$('wireframe').onclick=()=>{invalidate();wireframe=!wireframe;$('wireframe').classList.toggle('active',wireframe);quarry.traverse(o=>{if(o.material)o.material.wireframe=wireframe;});};
$('rotate').onclick=()=>{controls.autoRotate=!controls.autoRotate;controls.autoRotateSpeed=.6;$('rotate').classList.toggle('active',controls.autoRotate);};
$('help').onclick=()=>$('help-panel').hidden=!$('help-panel').hidden;
$('about').onclick=e=>{e.preventDefault();toast('Frontier · Geometry-first terrain tools. Built locally in your browser.');};
function download(blob,name){const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),5000);}
$('screenshot').onclick=()=>{renderer.render(scene,camera);renderer.domElement.toBlob(blob=>{if(blob){download(blob,`frontier-${quarry.userData.mode==='mountain'?'mountain':'quarry'}-${quarry.userData.settings.seed}.png`);toast('Screenshot saved');}});};
$('export').onclick=()=>{
  $('export').disabled=true;toast('Preparing OBJ geometry · procedural shading is not included');
  setTimeout(()=>{try{
    // OBJExporter does not expand InstancedMesh: explicitly export each transformed instance.
    const exporter=new OBJExporter(), exportGroup=new THREE.Group();
    quarry.updateMatrixWorld(true);
    quarry.traverse(o=>{if(o.isInstancedMesh){for(let i=0;i<o.count;i++){const m=new THREE.Mesh(o.geometry,o.material);o.getMatrixAt(i,m.matrix);m.matrix.premultiply(o.matrixWorld);m.matrixAutoUpdate=false;m.name=`${o.name}_${i}`;exportGroup.add(m);}}else if(o.isMesh){const m=new THREE.Mesh(o.geometry,o.material);m.matrix.copy(o.matrixWorld);m.matrixAutoUpdate=false;m.name=o.name;exportGroup.add(m);}});
    const output=exporter.parse(exportGroup);download(new Blob([`# Frontier ${quarry.userData.mode==='mountain'?'mountain':'quarry'} | units: meters | Y-up\n# Parameters: ${JSON.stringify({...quarry.userData.settings,...quarry.userData.surface})}\n`,output],{type:'text/plain'}),`frontier-${quarry.userData.mode==='mountain'?'mountain':'quarry'}-${quarry.userData.settings.seed}.obj`);toast('OBJ exported · geometry only. Save the surface recipe separately.');
  }catch(e){console.error(e);toast('Export failed. Try a smaller landform.');}finally{$('export').disabled=false;}},70);
};
function updateMeshStats(){
  let triangles=0;quarry.traverse(o=>{if(o.geometry)triangles+=(o.geometry.index?.count??o.geometry.attributes.position.count)/3*(o.isInstancedMesh?o.count:1);});
  const style=quarry.userData.settings.meshStyle;
  $('mesh-stats').textContent=`${style==='lowpoly'?'LOW-POLY':'LAYERED'} · ${Math.round(triangles).toLocaleString()} triangles`;
}
let detailTimer;
function applySurface(rebuild=false){
  if(!quarry)return;
  const values=currentSurface();
  quarry.userData.surface=updateRockSurface(quarry,values);
  if(rebuild){
    clearTimeout(detailTimer);
    detailTimer=setTimeout(()=>{rebuildSurfaceDetails(quarry,quarry.userData.surface);quarry.traverse(o=>{if(o.material)o.material.wireframe=wireframe;});updateBaseView();updateMeshStats();invalidate();},100);
  }
  const details=quarry.getObjectByName('Surface_microgeometry');if(details)details.visible=values.surfaceEnabled&&!baseOnly;
  invalidate();
}
surfaceRanges.forEach(id=>{updateRange(id);$(id).addEventListener('input',()=>{updateRange(id);applySurface(id==='peeling'||id==='crystals');});});
$('surfaceEnabled').addEventListener('change',()=>applySurface());
$('reference-stone').onclick=()=>{
  for(const [key,value] of Object.entries(referenceStoneFinish)){ $(key).value=value;updateRange(key); }
  $('surfaceEnabled').checked=true;applySurface();
  toast('Matte reference finish · visible stone relief, raised grain and mineral flecks');
};
function inspectSurface(){
  const raycaster=new THREE.Raycaster();raycaster.setFromCamera(new THREE.Vector2(0,0),camera);
  const hits=raycaster.intersectObjects(quarry.children.filter(o=>o.isMesh&&o.visible),false);
  if(!hits.length){toast('Aim the camera at a rock, then inspect again.');return;}
  let point=hits[0].point;
  // Frame a whole exposed crystal pocket, not one buried instance origin.
  const detail=quarry.getObjectByName('Surface_microgeometry');
  if(detail?.visible){
    const surfaces=quarry.children.filter(o=>o.isMesh&&o.visible);
    const pockets=(detail.userData.clusters??[]).map(cluster=>({cluster,alignment:cluster.position.clone().sub(camera.position).normalize().dot(raycaster.ray.direction)})).sort((a,b)=>b.alignment-a.alignment);
    for(const {cluster} of pockets){
      const toward=cluster.position.clone().sub(camera.position),distance=toward.length();toward.normalize();
      const alignment=toward.dot(raycaster.ray.direction);if(alignment<.92)continue;
      if(cluster.normal.dot(toward)>-.15)continue;
      const check=new THREE.Raycaster(camera.position,toward,0,distance+.1).intersectObjects(surfaces,false)[0];
      if(check&&Math.abs(check.distance-distance)<.04){point=cluster.position.clone();break;}
    }
  }
  const back=camera.position.clone().sub(point).normalize();
  controls.target.copy(point);camera.position.copy(point).addScaledVector(back,1.25);controls.update();
  $('top-view').classList.remove('active');invalidate();
  toast('Close inspection · drag to orbit this surface · Reset camera to return');
}
$('inspect-surface').onclick=inspectSurface;$('detail-view').onclick=inspectSurface;
$('surface-recipe').onclick=()=>{
  download(new Blob([JSON.stringify({format:'frontier-surface-recipe',version:7,material:'mapless-weathering-v7',units:'meters',formation:quarry.userData.formation,geometry:quarry.userData.settings,surface:quarry.userData.surface,note:'Settings only, not a portable material. Recreate the shader in the destination renderer; OBJ contains geometry only.'},null,2)],{type:'application/json'}),`frontier-surface-${quarry.userData.settings.seed}.json`);
  toast('Surface recipe saved · settings, not a baked material');
};
new ResizeObserver(()=>{const box=$('canvas-container').getBoundingClientRect();const oldAspect=camera.aspect;renderer.setSize(box.width,box.height);camera.aspect=box.width/box.height;camera.updateProjectionMatrix();if(quarry&&Math.abs(oldAspect-camera.aspect)>.01)resetCamera($('top-view').classList.contains('active'));invalidate();}).observe($('canvas-container'));
updateModeUI();generate(true);
const scaleStart=new THREE.Vector3(),scaleEnd=new THREE.Vector3(),cameraRight=new THREE.Vector3();
renderer.setAnimationLoop(()=>{
  controls.update();
  if(renderFrames<=0&&!controls.autoRotate)return;
  renderFrames--;
  // A true 10 m screen-space ruler at the orbit target, updated as the camera zooms.
  const distance=camera.position.distanceTo(controls.target);
  const ruler=distance<3?.05:distance<15?.5:10;
  document.querySelector('.scale').lastChild.textContent=distance<3?'5 CENTIMETERS':distance<15?'50 CENTIMETERS':'10 METERS';
  cameraRight.setFromMatrixColumn(camera.matrixWorld,0).multiplyScalar(ruler);
  scaleStart.copy(controls.target).project(camera);
  scaleEnd.copy(controls.target).add(cameraRight).project(camera);
  document.querySelector('.scale>span').style.width=`${Math.abs(scaleEnd.x-scaleStart.x)*renderer.domElement.clientWidth/2}px`;
  renderer.render(scene,camera);
});
