import * as THREE from 'three';
import {OrbitControls} from './vendor/OrbitControls.js';
import {mergeGeometries} from './vendor/BufferGeometryUtils.js';
import {SurfaceSolver} from './solver.mjs';
import {FloatingBody} from './floating-body.mjs';
import {WATER_PRESETS,waterVertex,waterFragment} from './water-materials.mjs';
import {pondDimensions,baseSamples} from './pond-config.mjs';
let POND_WIDTH=12,POND_DEPTH=8,quality=96;

const $=id=>document.getElementById(id),canvas=$('fluid'),viewport=$('viewport');
let renderer;
try {renderer=new THREE.WebGLRenderer({canvas,antialias:true,preserveDrawingBuffer:true});}
catch(error){$('webglError').hidden=false;throw error;}
renderer.setPixelRatio(Math.min(devicePixelRatio,1.5));
renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;
renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=.95;
const world=new THREE.Scene();world.background=new THREE.Color('#e6eae3');world.fog=new THREE.Fog('#e6eae3',25,65);
const camera=new THREE.PerspectiveCamera(39,1,.1,100);
const orbit=new OrbitControls(camera,canvas);orbit.target.set(0,.45,0);orbit.enableDamping=true;orbit.dampingFactor=.07;orbit.maxPolarAngle=Math.PI*.47;orbit.minDistance=8;orbit.maxDistance=34;orbit.enablePan=false;
function home(){const fit=Math.max(POND_WIDTH/12,POND_DEPTH/8);camera.position.set(10.5*fit,12.8*fit,16*fit);orbit.maxDistance=34*fit;orbit.target.set(0,.4,0);orbit.update();}
home();
world.add(new THREE.HemisphereLight('#f5fff2','#647a57',.85));
const sun=new THREE.DirectionalLight('#fff1d1',2.3);sun.position.set(-7,14,6);sun.castShadow=true;sun.shadow.mapSize.set(1024,1024);sun.shadow.autoUpdate=false;sun.shadow.needsUpdate=true;Object.assign(sun.shadow.camera,{left:-9,right:9,top:9,bottom:-9,near:1,far:40});sun.shadow.normalBias=.035;sun.shadow.bias=-.0001;sun.shadow.radius=3;world.add(sun);
const fill=new THREE.DirectionalLight('#c3e9f1',1);fill.position.set(8,6,-5);world.add(fill);
const envCanvas=document.createElement('canvas');envCanvas.width=1024;envCanvas.height=512;
const ec=envCanvas.getContext('2d'),sky=ec.createLinearGradient(0,0,0,512);
sky.addColorStop(0,'#77959b');sky.addColorStop(.35,'#dce5d4');sky.addColorStop(.5,'#f5f1dc');sky.addColorStop(.57,'#64886c');sky.addColorStop(1,'#1b3b30');ec.fillStyle=sky;ec.fillRect(0,0,1024,512);
for(let i=0;i<30;i++){const a=i*36;ec.fillStyle=i%2?'#557459':'#71916c';ec.beginPath();ec.ellipse(a,295+Math.sin(i*2)*24,20+Math.sin(i)*12,40+Math.cos(i)*20,0,0,Math.PI*2);ec.fill();}
ec.fillStyle='#fffbed';ec.beginPath();ec.ellipse(230,145,90,36,-.1,0,Math.PI*2);ec.fill();
const environment=new THREE.CanvasTexture(envCanvas);environment.mapping=THREE.EquirectangularReflectionMapping;environment.colorSpace=THREE.SRGBColorSpace;
const pmrem=new THREE.PMREMGenerator(renderer);world.environment=pmrem.fromEquirectangular(environment).texture;pmrem.dispose();
const materialCache=new Map();
function material(color,roughness=.8){const key=color+roughness;if(!materialCache.has(key))materialCache.set(key,new THREE.MeshStandardMaterial({color,roughness}));return materialCache.get(key);}
function batchMeshes(root){
  root.updateMatrixWorld(true);const buckets=new Map(),sources=[];
  root.traverse(o=>{if(!o.isMesh)return;const key=o.material.uuid+o.castShadow+o.receiveShadow;if(!buckets.has(key))buckets.set(key,{material:o.material,cast:o.castShadow,receive:o.receiveShadow,geometries:[]});const geo=o.geometry.clone();geo.applyMatrix4(new THREE.Matrix4().copy(root.matrixWorld).invert().multiply(o.matrixWorld));buckets.get(key).geometries.push(geo);sources.push(o);});
  for(const source of sources){source.parent.remove(source);source.geometry.dispose();}
  for(const b of buckets.values()){const parts=b.geometries.map(g=>g.index?g.toNonIndexed():g);const geo=mergeGeometries(parts,false);for(const part of new Set([...parts,...b.geometries]))part.dispose();if(!geo)continue;const mesh=new THREE.Mesh(geo,b.material);mesh.castShadow=b.cast;mesh.receiveShadow=b.receive;root.add(mesh);}
}
const limestone=material('#c1baa2'),stoneLight=material('#d6d0b9'),stoneDark=material('#9a9e85'),earth=material('#a6ad95');
const basinGroup=new THREE.Group(),decorationGroup=new THREE.Group();world.add(basinGroup,decorationGroup);
function box(w,h,d,mat,x=0,y=0,z=0,parent=basinGroup){const mesh=new THREE.Mesh(new THREE.BoxGeometry(w,h,d),mat);mesh.position.set(x,y,z);mesh.castShadow=true;mesh.receiveShadow=true;parent.add(mesh);return mesh;}
box(200,.1,200,material('#e1e6dc'),0,-.76,0,world);
function buildBasin(){
  basinGroup.traverse(o=>{if(o.isMesh)o.geometry.dispose();});basinGroup.clear();
  const wx=POND_WIDTH/2+.22,dz=POND_DEPTH/2+.22;
  box(POND_WIDTH+1.08,.25,POND_DEPTH+1.08,earth,0,-.56,0);
  box(POND_WIDTH+.85,.22,POND_DEPTH+.85,limestone,0,-.4,0);
  box(POND_WIDTH+.85,.85,.4,limestone,0,.13,-dz);box(POND_WIDTH+.85,.85,.4,limestone,0,.13,dz);
  box(.4,.85,POND_DEPTH+.85,limestone,-wx,.13,0);box(.4,.85,POND_DEPTH+.85,limestone,wx,.13,0);
  const cols=Math.ceil((POND_WIDTH+.85)/.92),rows=Math.ceil((POND_DEPTH-.03)/.9);
  for(let i=0;i<cols;i++){
    const step=(POND_WIDTH+.85)/cols,p=-(POND_WIDTH+.85)/2+(i+.5)*step;
    box(step-.018,.48,.47,i%3?stoneLight:limestone,p,.77,-dz);box(step-.018,.48,.47,i%4?stoneLight:limestone,p,.77,dz);
  }
  for(let i=0;i<rows;i++){
    const step=(POND_DEPTH-.03)/rows,p=-(POND_DEPTH-.03)/2+(i+.5)*step;
    box(.47,.48,step-.018,i%3?stoneLight:limestone,-wx,.77,p);box(.47,.48,step-.018,i%3?stoneLight:limestone,wx,.77,p);
  }
  batchMeshes(basinGroup);
  const extent=Math.max(POND_WIDTH,POND_DEPTH)/2+3;
  Object.assign(sun.shadow.camera,{left:-extent,right:extent,top:extent,bottom:-extent});sun.shadow.camera.updateProjectionMatrix();sun.shadow.needsUpdate=true;
}
buildBasin();
let solver=new SurfaceSolver(96,POND_DEPTH,POND_WIDTH),level=.65,strength=.65,running=true,tool='orbit',mood='still';
let renderDirty=true;
orbit.addEventListener('change',()=>renderDirty=true);
for(const event of ['input','change','click','pointermove','pointerup'])document.addEventListener(event,()=>renderDirty=true);
let simTime=0,appearanceTime=0,last=performance.now(),fpsTime=last,frames=0,frameRate=0,refineCheck=0,eventClock=0,toastTimer;
let water,waterGeometry;
let waterStyle='clean';
const refractionTarget=new THREE.WebGLRenderTarget(800,600,{type:THREE.HalfFloatType});
refractionTarget.depthTexture=new THREE.DepthTexture(800,600,THREE.UnsignedIntType);
const reflectionTarget=new THREE.WebGLRenderTarget(600,450,{type:THREE.HalfFloatType,generateMipmaps:true,minFilter:THREE.LinearMipmapLinearFilter});
const refractionClip=new THREE.Plane(new THREE.Vector3(0,-1,0),level);
const reflectionCamera=new THREE.PerspectiveCamera();
const reflectionMatrix=new THREE.Matrix4();
const waterMaterial=new THREE.ShaderMaterial({
  uniforms:{uTime:{value:0},uEnvironment:{value:environment},uLevel:{value:level},
    uBedY:{value:-.17},uRoughness:{value:.065},uMicro:{value:.012},uScattering:{value:.018},uFilm:{value:0},uIOR:{value:1.333},
    uAbsorption:{value:new THREE.Vector3(.28,.045,.018)},uScatterColor:{value:new THREE.Vector3(.022,.10,.12)},uBedColor:{value:new THREE.Color('#998a64')},
    uNear:{value:camera.near},uFar:{value:camera.far},uSceneColor:{value:refractionTarget.texture},uSceneDepth:{value:refractionTarget.depthTexture},
    uReflection:{value:reflectionTarget.texture},uReflectionMatrix:{value:reflectionMatrix},uViewProjection:{value:new THREE.Matrix4()},uView:{value:new THREE.Matrix4()},uDuck:{value:new THREE.Vector2(.8,.3)}},
  vertexShader:waterVertex,fragmentShader:waterFragment,side:THREE.DoubleSide
});
const meshMaterial=new THREE.MeshBasicMaterial({color:'#ccf8c8',wireframe:true,transparent:true,opacity:.24,depthWrite:false});
let wire;
function rebuildWater(){
  if(water){world.remove(water,wire);waterGeometry.dispose();}
  const n=solver.n,nx=solver.nx,positions=new Float32Array(nx*n*3),normals=new Float32Array(nx*n*3),indices=[];
  for(let j=0;j<n;j++)for(let i=0;i<nx;i++){
    const k=(j*nx+i)*3;positions[k]=i*solver.dx-POND_WIDTH/2;positions[k+1]=level;positions[k+2]=j*solver.dz-POND_DEPTH/2;normals[k+1]=1;
    if(i<nx-1&&j<n-1){const a=j*nx+i;indices.push(a,a+nx,a+1,a+1,a+nx,a+nx+1);}
  }
  waterGeometry=new THREE.BufferGeometry();waterGeometry.setAttribute('position',new THREE.BufferAttribute(positions,3).setUsage(THREE.DynamicDrawUsage));waterGeometry.setAttribute('normal',new THREE.BufferAttribute(normals,3).setUsage(THREE.DynamicDrawUsage));waterGeometry.setIndex(indices);
  water=new THREE.Mesh(waterGeometry,waterMaterial);water.frustumCulled=false;world.add(water);
  wire=new THREE.Mesh(waterGeometry,meshMaterial);wire.position.y=.003;wire.visible=$('wireframe').checked;wire.frustumCulled=false;world.add(wire);
  updateMetrics();
}
function updateWater(){
  const n=solver.n,nx=solver.nx,h=solver.height,p=waterGeometry.attributes.position.array,norm=waterGeometry.attributes.normal.array;
  for(let j=0;j<n;j++)for(let i=0;i<nx;i++){
    const k=j*nx+i,a=k*3;p[a+1]=level+h[k];
    const gx=(h[j*nx+Math.min(nx-1,i+1)]-h[j*nx+Math.max(0,i-1)])/(2*solver.dx);
    const gz=(h[Math.min(n-1,j+1)*nx+i]-h[Math.max(0,j-1)*nx+i])/(2*solver.dz);
    const inv=1/Math.sqrt(1+gx*gx+gz*gz);norm[a]=-gx*inv;norm[a+1]=inv;norm[a+2]=-gz*inv;
  }
  waterGeometry.attributes.position.needsUpdate=true;waterGeometry.attributes.normal.needsUpdate=true;
  waterMaterial.uniforms.uTime.value=appearanceTime;waterMaterial.uniforms.uLevel.value=level;
}
// Small clusters of weathered rocks also act as reflecting solver obstacles.
let seed=1927;function rand(){seed=(1664525*seed+1013904223)>>>0;return seed/4294967296;}
for(const o of solver.obstacles){
  for(let i=0;i<5;i++){
    const geo=new THREE.DodecahedronGeometry(o.r*(i===0?1:.45),1),rock=new THREE.Mesh(geo,i%2?stoneDark:limestone);
    rock.position.set(o.x+(i?Math.cos(i*2)*o.r*.5:0),.68+(i===0?.08:-.1),o.z+(i?Math.sin(i*2)*o.r*.5:0));rock.scale.set(1,.65+rand()*.5,1);rock.rotation.set(rand(),rand(),rand());rock.castShadow=rock.receiveShadow=true;decorationGroup.add(rock);
  }
}
// Reeds growing from the rock islands: tapered blades, no image textures.
const reeds=new THREE.Group();decorationGroup.add(reeds);
for(let i=0;i<44;i++){
  const o=solver.obstacles[i<30?0:1],angle=rand()*Math.PI*2,r=rand()*o.r*.55;
  const x=o.x+Math.cos(angle)*r,z=o.z+Math.sin(angle)*r,len=.5+rand()*1.05;
  const curve=new THREE.QuadraticBezierCurve3(new THREE.Vector3(x,.7,z),new THREE.Vector3(x+.06,1.05+len*.5,z+.1),new THREE.Vector3(x+(rand()-.5)*.55,.8+len,z+(rand()-.5)*.55));
  const blade=new THREE.Mesh(new THREE.TubeGeometry(curve,5,.014+rand()*.012,3,false),material(i%3?'#6c8450':'#a7a063'));blade.castShadow=true;reeds.add(blade);
}
batchMeshes(decorationGroup);
// A real scene bed is captured in the refraction pass. Puddle mode raises it.
const bedCanvas=document.createElement('canvas');bedCanvas.width=bedCanvas.height=512;
const bc=bedCanvas.getContext('2d');bc.fillStyle='#d2cbbd';bc.fillRect(0,0,512,512);
for(let i=0;i<2400;i++){const v=Math.floor(115+rand()*90);bc.fillStyle=`rgb(${v+10},${v+5},${v})`;bc.beginPath();bc.ellipse(rand()*512,rand()*512,1+rand()*4,1+rand()*3,rand()*6,0,Math.PI*2);bc.fill();}
const bedTexture=new THREE.CanvasTexture(bedCanvas);bedTexture.colorSpace=THREE.SRGBColorSpace;bedTexture.wrapS=bedTexture.wrapT=THREE.RepeatWrapping;bedTexture.repeat.set(5,3.3);
const bedMaterial=new THREE.MeshStandardMaterial({color:'#998a64',roughness:.95,map:bedTexture});
const bed=new THREE.Mesh(new THREE.PlaneGeometry(POND_WIDTH,POND_DEPTH),bedMaterial);bed.rotation.x=-Math.PI/2;bed.position.y=-.17;bed.receiveShadow=true;world.add(bed);

const lilyGroup=new THREE.Group();world.add(lilyGroup);const lilies=[];
const lilyColors=['#668b43','#7c9a46','#4f793d','#91a650','#5e8a49'];
function makeLily(x,z,r,flower=false){
  const group=new THREE.Group(),shape=new THREE.Shape();shape.moveTo(0,0);
  for(let i=0;i<=48;i++){const a=.20+i/48*(Math.PI*2-.43),rr=r*(1+.018*Math.sin(a*7));shape.lineTo(Math.cos(a)*rr,Math.sin(a)*rr);}shape.lineTo(0,0);
  const geo=new THREE.ExtrudeGeometry(shape,{depth:.024,bevelEnabled:true,bevelSize:.009,bevelThickness:.007,bevelSegments:1,steps:1});geo.rotateX(-Math.PI/2);
  const leaf=new THREE.Mesh(geo,new THREE.MeshStandardMaterial({color:lilyColors[lilies.length%lilyColors.length],roughness:.5,metalness:.03,side:THREE.DoubleSide}));leaf.castShadow=false;leaf.receiveShadow=true;group.add(leaf);
  const veinPositions=[];
  for(let i=0;i<12;i++){const a=.45+i/12*5.6;veinPositions.push(0,.034,0,Math.cos(a)*r*.89,.034,-Math.sin(a)*r*.89);}
  const veinGeo=new THREE.BufferGeometry();veinGeo.setAttribute('position',new THREE.Float32BufferAttribute(veinPositions,3));group.add(new THREE.LineSegments(veinGeo,new THREE.LineBasicMaterial({color:'#b5c96e',transparent:true,opacity:.38})));
  if(flower){
    const flowerGroup=new THREE.Group();flowerGroup.position.set(-r*.16,.05,r*.12);
    for(let layer=0;layer<2;layer++)for(let i=0;i<9;i++){
      const a=i/9*Math.PI*2+layer*.3,petal=new THREE.Mesh(new THREE.SphereGeometry(1,8,6),material(layer?'#f4c6cd':'#dea0b0',.4));
      petal.scale.set(.052,.03,.16-layer*.045);petal.position.set(Math.sin(a)*(.105-layer*.03),.035+layer*.05,Math.cos(a)*(.105-layer*.03));petal.rotation.set(-.25-layer*.4,a,0);petal.castShadow=false;flowerGroup.add(petal);
    }
    const center=new THREE.Mesh(new THREE.SphereGeometry(.052,10,8),material('#e9ca64'));center.position.y=.105;flowerGroup.add(center);batchMeshes(flowerGroup);group.add(flowerGroup);
  }
  group.position.set(x,level+.028,z);group.rotation.y=rand()*6.28;lilyGroup.add(group);
  lilies.push({group,leaf,x,z,homeX:x,homeZ:z,baseX:x,baseZ:z,r,vx:0,vz:0,angle:group.rotation.y});leaf.userData.lily=lilies.at(-1);
}
[[-1.5,1.6,.48,true],[-2.2,2.15,.39,false],[-.75,2.3,.4,false],[-2.7,.95,.35,false],[1.45,-1.8,.51,true],[2.3,-2.25,.4,false],[2.8,-1.4,.34,false],[.7,-2.5,.32,false],[1.95,-.85,.3,false],[-.85,-1.9,.29,false],[.5,1.45,.31,true],[-2.6,2.8,.28,false]].forEach(a=>makeLily(...a));
// Floating organic specks make the scale of the pond readable.
const specks=[];const speckGeo=new THREE.CircleGeometry(.018,5),speckMat=material('#c5d2a0');
for(let i=0;i<85;i++){
  const x=(rand()-.5)*(POND_WIDTH-.4),z=(rand()-.5)*(POND_DEPTH-.4);if(!solver.isWet(x,z,.1))continue;
  const s=new THREE.Mesh(speckGeo,speckMat);s.rotation.x=-Math.PI/2;s.position.set(x,level+.012,z);s.userData.u=x/12;s.userData.v=z/8;world.add(s);specks.push(s);
}
const duckState=new FloatingBody(),duck=new THREE.Group();world.add(duck);
const rubber=new THREE.MeshPhysicalMaterial({color:'#ffd12e',roughness:.3,metalness:0,clearcoat:.32,clearcoatRoughness:.25});
const orange=new THREE.MeshPhysicalMaterial({color:'#f88319',roughness:.36,clearcoat:.15});
function duckPart(scale,position,mat){const m=new THREE.Mesh(new THREE.SphereGeometry(1,24,16),mat);m.scale.set(...scale);m.position.set(...position);duck.add(m);return m;}
duckPart([.46,.34,.58],[0,.19,-.06],rubber); // buoyant belly
const tail=duckPart([.23,.18,.32],[0,.33,-.48],rubber);tail.rotation.x=-.6;
duckPart([.27,.36,.29],[0,.51,.25],rubber);
duckPart([.32,.32,.32],[0,.77,.32],rubber); // head
const beak=duckPart([.245,.095,.23],[0,.69,.61],orange);beak.rotation.x=.08;
const wingMaterial=new THREE.MeshPhysicalMaterial({color:'#eab726',roughness:.37,clearcoat:.2});
for(const side of [-1,1]){const wing=duckPart([.09,.21,.35],[side*.405,.25,-.08],wingMaterial);wing.rotation.x=-.2;duckPart([.047,.053,.034],[side*.238,.82,.51],material('#13191a',.2));duckPart([.013,.014,.013],[side*.25,.836,.534],material('#ffffff',.15));}
batchMeshes(duck);
let dragging=false,selectedLily=null,lastHit=null,duckSelected=false,duckOffset=new THREE.Vector2();
function syncDuck(dt){
  // Motion is advanced only in solver.advance(); this function synchronizes the mesh.
  duck.position.set(duckState.x,duckState.y,duckState.z);
  const g=solver.gradient(duckState.x,duckState.z);
  const q=new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,1,0),new THREE.Vector3(-g.x,1,-g.z).normalize());
  q.multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,1,0),duckState.angle));
  duck.quaternion.slerp(q,dt>0?1-Math.exp(-8*dt):1);
  waterMaterial.uniforms.uDuck.value.set(duckState.x,duckState.z);
}
function applyWaterStyle(id){
  waterStyle=id;const p=WATER_PRESETS[id],u=waterMaterial.uniforms;
  u.uAbsorption.value.fromArray(p.absorption);u.uScatterColor.value.fromArray(p.color);u.uScattering.value=p.scattering;
  u.uMicro.value=p.micro;u.uFilm.value=p.film;u.uRoughness.value=p.roughness;u.uBedColor.value.set(p.bed);bedMaterial.color.set(p.bed);
  $('roughness').value=p.roughness;$('roughnessValue').textContent=p.roughness.toFixed(3);
  $('turbidity').value=1;$('turbidityValue').textContent='1.0×';
  $('materialDescription').textContent=p.description;$('materialName').textContent=p.name;
  $('materialBadge').textContent=p.name.toUpperCase();
  updateBed();paintRange($('roughness'));paintRange($('turbidity'));
}
function updateBed(){
  const p=WATER_PRESETS[waterStyle];bed.position.y=p.depth!==null?level-p.depth:-.17;
  waterMaterial.uniforms.uBedY.value=bed.position.y;
}
function paintRange(input){const p=(Number(input.value)-Number(input.min))/(Number(input.max)-Number(input.min))*100;input.style.background=`linear-gradient(to right,#92a975 ${p}%,#e9edef ${p}%)`;}

function updateLilies(dt){
  lilyGroup.visible=$('lilies').checked;
  for(const l of lilies){
    const g=solver.gradient(l.x,l.z);
    if(running&&l!==selectedLily){
      l.vx+=(-g.x*1.5+(l.homeX-l.x)*.6-l.vx*1.8)*dt;l.vz+=(-g.z*1.5+(l.homeZ-l.z)*.6-l.vz*1.8)*dt;
      const nx=l.x+l.vx*dt,nz=l.z+l.vz*dt;
      if(solver.isWet(nx,nz,l.r*.7)){l.x=nx;l.z=nz;}else{l.vx*=-.3;l.vz*=-.3;}
    }
    l.group.position.set(l.x,level+solver.sample(l.x,l.z)+.024,l.z);
    const normal=new THREE.Vector3(-g.x,1,-g.z).normalize();
    const tilt=new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,1,0),normal);
    const yaw=new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,1,0),l.angle);
    l.group.quaternion.copy(tilt.multiply(yaw));
  }
  for(const s of specks){s.position.y=level+solver.sample(s.position.x,s.position.z)+.012;s.visible=(waterStyle==='swamp'||waterStyle==='dirty')&&solver.isWet(s.position.x,s.position.z,.05);}
}
function reset(){
  const base=baseSamples(quality,POND_DEPTH),speed=solver.speed,damping=solver.damping,adaptive=solver.adaptive;
  solver=new SurfaceSolver(base,POND_DEPTH,POND_WIDTH);Object.assign(solver,{speed,damping,adaptive});simTime=0;appearanceTime=0;eventClock=0;refineCheck=0;
  for(const l of lilies){l.x=l.homeX;l.z=l.homeZ;l.vx=l.vz=0;}
  if(mood==='pulse')solver.disturb(0,0,1.2,.3);
  if(mood==='pond'&&$('autoRipples').checked){solver.disturb(.3,-.1,.35,.4);}
  release();duckState.reset(level);syncDuck(0);rebuildWater();updateWater();updateLilies(0);updateMetrics();
}
function updateMetrics(){
  $('gridCount').textContent=solver.nx+'×'+solver.n;$('spacing').textContent=(solver.dx*1000).toFixed(0);$('dt').textContent=(solver.lastDt*1000).toFixed(2);$('fps').textContent=running?(frameRate||'—'):'—';
  $('refineState').textContent=solver.n>solver.base?'Refined · '+solver.height.length.toLocaleString()+' vertices':'Base resolution';
  $('clock').textContent=Math.floor(simTime/60).toString().padStart(2,'0')+':'+(simTime%60).toFixed(2).padStart(5,'0');
}
function toast(text){$('toast').textContent=text;$('toast').classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').classList.remove('show'),2600);}
const raycaster=new THREE.Raycaster(),mouse=new THREE.Vector2(),waterPlane=new THREE.Plane(new THREE.Vector3(0,1,0),-level);
function hit(e){const r=canvas.getBoundingClientRect();mouse.set((e.clientX-r.left)/r.width*2-1,-(e.clientY-r.top)/r.height*2+1);raycaster.setFromCamera(mouse,camera);waterPlane.constant=-level;return raycaster.ray.intersectPlane(waterPlane,new THREE.Vector3());}
function selectTool(value){release();tool=value;orbit.enabled=tool==='orbit';document.querySelectorAll('[data-tool]').forEach(b=>b.classList.toggle('active',b.dataset.tool===tool));canvas.style.cursor=tool==='orbit'?'grab':tool==='ripple'?'crosshair':'pointer';$('hint').textContent={orbit:'Drag to orbit · scroll to explore · click Ripple to disturb the water',ripple:'Click or drag across the water to send out ripples',lily:'Drag a lily pad · let go and watch it return to its anchor',duck:'Grab the yellow duck and drag · release to watch it coast and leave a wake'}[tool];}
canvas.addEventListener('pointerdown',e=>{
  if(tool==='orbit')return;
  const p=hit(e);if(!p)return;
  if(!running){toast('Press Play to interact with the pond.');return;}
  dragging=true;canvas.setPointerCapture(e.pointerId);lastHit=p;
  if(tool==='ripple')solver.disturb(p.x,p.z,strength,.25);
  if(tool==='duck'){
    duckSelected=raycaster.intersectObject(duck,true).length>0||Math.hypot(p.x-duckState.x,p.z-duckState.z)<.7;
    if(duckSelected){duckOffset.set(duckState.x-p.x,duckState.z-p.z);duckState.grab(duckState.x,duckState.z);canvas.style.cursor='grabbing';}
    else toast('Grab the yellow rubber duck, then drag across the pond.');
  }
  if(tool==='lily'&&lilyGroup.visible){
    const hits=raycaster.intersectObjects(lilies.map(l=>l.leaf),false);selectedLily=hits[0]?.object.userData.lily||null;
    if(!selectedLily)selectedLily=lilies.find(l=>Math.hypot(l.x-p.x,l.z-p.z)<l.r*1.2)||null;
    if(!selectedLily)toast('Choose a green lily pad to move it.');
  }
});
canvas.addEventListener('pointermove',e=>{
  if(!dragging||!running)return;const p=hit(e);if(!p)return;
  if(tool==='duck'&&duckSelected){duckState.grab(Math.max(-POND_WIDTH/2+.5,Math.min(POND_WIDTH/2-.5,p.x+duckOffset.x)),Math.max(-POND_DEPTH/2+.5,Math.min(POND_DEPTH/2-.5,p.z+duckOffset.y)));}
  if(tool==='ripple'&&lastHit&&p.distanceTo(lastHit)>.13){solver.disturb(p.x,p.z,strength*.35,.22);lastHit=p;}
  if(tool==='lily'&&selectedLily&&solver.isWet(p.x,p.z,selectedLily.r*.7)){
    const l=selectedLily,move=Math.hypot(l.x-p.x,l.z-p.z);l.x=p.x;l.z=p.z;l.vx=l.vz=0;
    if(move>.015)solver.disturb(p.x,p.z,Math.min(.35,move*.9),l.r*.7);
  }
});
function release(){dragging=false;selectedLily=null;lastHit=null;duckSelected=false;duckState.release();if(tool==='duck')canvas.style.cursor='grab';}
canvas.addEventListener('pointerup',release);canvas.addEventListener('pointercancel',release);canvas.addEventListener('lostpointercapture',release);window.addEventListener('blur',release);
function togglePlay(){running=!running;$('playText').textContent=running?'Pause':'Play';$('playIcon').textContent=running?'Ⅱ':'▶';$('liveBadge').innerHTML=running?'<i></i> LIVE POND':'<i></i> PAUSED';}
$('play').onclick=togglePlay;$('reset').onclick=()=>{reset();toast('A fresh pond. Make a little ripple.');};$('viewReset').onclick=home;
$('scene').onchange=e=>{mood=e.target.value;$('autoRipples').checked=mood==='rain'||mood==='pond';$('sceneTitle').textContent={pond:'The garden pond',rain:'Rain on the pond',still:'A moment of stillness',pulse:'One splash, endless ripples'}[mood];reset();};
document.querySelectorAll('[data-tool]').forEach(b=>b.onclick=()=>selectTool(b.dataset.tool));
document.querySelectorAll('[data-quality]').forEach(b=>b.onclick=()=>{quality=Number(b.dataset.quality);document.querySelectorAll('[data-quality]').forEach(a=>a.classList.toggle('selected',a===b));reset();toast('Base resolution updated · pond reset');});
$('adaptive').onchange=e=>{solver.adaptive=e.target.checked;toast(solver.adaptive?'Steep waves can refine the surface.':'Refinement off. Reset to return to the base grid.');};
$('wireframe').onchange=e=>{wire.visible=e.target.checked;};
for(const id of ['wave','damping','strength','level']){
  const el=$(id),update=()=>{const val=Number(el.value);if(id==='wave')solver.speed=val;if(id==='damping')solver.damping=val;if(id==='strength')strength=val;if(id==='level'){level=val;updateBed();if(!running){duckState.y=level+solver.sample(duckState.x,duckState.z);duckState.vy=0;}}
    $(id+'Value').textContent=val.toFixed(id==='wave'?1:2)+(id==='wave'?' m/s':id==='level'?' m':'');const p=(val-Number(el.min))/(Number(el.max)-Number(el.min))*100;el.style.background=`linear-gradient(to right,#92a975 ${p}%,#e9edef ${p}%)`;};el.oninput=update;update();
}
$('capture').onclick=()=>{renderScene();canvas.toBlob(blob=>{if(!blob){toast('Unable to save this snapshot.');return;}const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='ripple-pond-'+Date.now()+'.png';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);toast('A little piece of the pond, saved.');});};
$('fullscreen').onclick=async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await viewport.requestFullscreen();}catch{toast('Fullscreen is unavailable in this browser view.');}};
$('help').onclick=()=>$('about').showModal();$('closeHelp').onclick=$('gotIt').onclick=()=>$('about').close();
window.addEventListener('keydown',e=>{if(['INPUT','SELECT','BUTTON'].includes(document.activeElement.tagName)||$('about').open)return;if(e.code==='Space'){e.preventDefault();togglePlay();}if(e.key.toLowerCase()==='r')reset();if(['1','2','3','4'].includes(e.key))selectTool(['orbit','ripple','lily','duck'][Number(e.key)-1]);});
function applyPondSize(){
  const next=pondDimensions(Number($('pondWidth').value),Number($('pondDepth').value));
  POND_WIDTH=next.width;POND_DEPTH=next.depth;
  buildBasin();bed.geometry.dispose();bed.geometry=new THREE.PlaneGeometry(POND_WIDTH,POND_DEPTH);
  bedTexture.repeat.set(POND_WIDTH/2.4,POND_DEPTH/2.4);
  for(const l of lilies){
    let x=l.baseX/12*POND_WIDTH,z=l.baseZ/8*POND_DEPTH;
    // Keep the anchored leaves away from fixed rock islands after resizing.
    for(const o of solver.obstacles){const r=Math.hypot(x-o.x,z-o.z),limit=o.r+l.r+.08;if(r<limit){const a=Math.atan2(z-o.z,x-o.x);x=o.x+Math.cos(a)*limit;z=o.z+Math.sin(a)*limit;}}
    l.homeX=Math.max(-POND_WIDTH/2+l.r+.05,Math.min(POND_WIDTH/2-l.r-.05,x));
    l.homeZ=Math.max(-POND_DEPTH/2+l.r+.05,Math.min(POND_DEPTH/2-l.r-.05,z));
  }
  for(const s of specks){s.position.x=s.userData.u*POND_WIDTH;s.position.z=s.userData.v*POND_DEPTH;}
  reset();home();renderDirty=true;
  $('dimensionsLabel').textContent=POND_WIDTH.toFixed(1)+' × '+POND_DEPTH.toFixed(1)+' m · '+POND_WIDTH*POND_DEPTH+' m²';
  $('sizeStatus').textContent='Applied '+POND_WIDTH+' × '+POND_DEPTH+' m · water and objects reset';
  $('applySize').disabled=true;toast('Pond resized · cell spacing preserved');
}
$('applySize').onclick=applyPondSize;
for(const id of ['pondWidth','pondDepth']){
  const el=$(id);el.oninput=()=>{$(id+'Value').textContent=el.value+' m';paintRange(el);$('applySize').disabled=Number($('pondWidth').value)===POND_WIDTH&&Number($('pondDepth').value)===POND_DEPTH;};paintRange(el);
}
$('autoRipples').onchange=()=>{eventClock=0;toast($('autoRipples').checked?'Automatic disturbances on.':'Automatic disturbances off · existing waves will decay.');};
document.addEventListener('visibilitychange',()=>{last=performance.now();solver.accumulator=0;release();});
$('waterMaterial').onchange=e=>applyWaterStyle(e.target.value);
$('roughness').oninput=e=>{waterMaterial.uniforms.uRoughness.value=Number(e.target.value);$('roughnessValue').textContent=Number(e.target.value).toFixed(3);paintRange(e.target);};
$('turbidity').oninput=e=>{waterMaterial.uniforms.uScattering.value=WATER_PRESETS[waterStyle].scattering*Number(e.target.value);$('turbidityValue').textContent=Number(e.target.value).toFixed(1)+'×';paintRange(e.target);};
$('duckReset').onclick=()=>{duckState.reset(level);syncDuck(0);toast('Duck returned to the centre.');};
new ResizeObserver(()=>{const r=viewport.getBoundingClientRect();renderer.setSize(r.width,r.height,false);camera.aspect=r.width/r.height;camera.fov=camera.aspect<1.1?49:39;camera.updateProjectionMatrix();renderDirty=true;const rw=Math.min(850,Math.round(r.width)),rh=Math.round(rw/r.width*r.height);refractionTarget.setSize(rw,rh);reflectionTarget.setSize(Math.min(600,rw),Math.round(Math.min(600,rw)/r.width*r.height));}).observe(viewport);
applyWaterStyle('clean');reset();
// Offscreen transmission plus a planar, obliquely clipped scene reflection.
// The final water pass mixes linear radiance; tone mapping happens only on screen.
function renderScene(){
  camera.updateMatrixWorld();
  const u=waterMaterial.uniforms;
  u.uViewProjection.value.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);
  u.uView.value.copy(camera.matrixWorldInverse);
  reflectionCamera.copy(camera);
  reflectionCamera.position.copy(camera.position);reflectionCamera.position.y=2*level-camera.position.y;
  const direction=camera.getWorldDirection(new THREE.Vector3());direction.y*=-1;
  reflectionCamera.up.set(0,-1,0);reflectionCamera.lookAt(reflectionCamera.position.clone().add(direction));reflectionCamera.updateMatrixWorld();
  reflectionCamera.projectionMatrix.copy(camera.projectionMatrix);
  const clip=new THREE.Plane(new THREE.Vector3(0,1,0),-level+.005).applyMatrix4(reflectionCamera.matrixWorldInverse);
  const cp=new THREE.Vector4(clip.normal.x,clip.normal.y,clip.normal.z,clip.constant);
  const m=reflectionCamera.projectionMatrix.elements;
  const q=new THREE.Vector4((Math.sign(cp.x)+m[8])/m[0],(Math.sign(cp.y)+m[9])/m[5],-1,(1+m[10])/m[14]);
  cp.multiplyScalar(2/cp.dot(q));m[2]=cp.x;m[6]=cp.y;m[10]=cp.z+1-.002;m[14]=cp.w;
  reflectionCamera.projectionMatrixInverse.copy(reflectionCamera.projectionMatrix).invert();
  reflectionMatrix.set(.5,0,0,.5,0,.5,0,.5,0,0,.5,.5,0,0,0,1);
  reflectionMatrix.multiply(reflectionCamera.projectionMatrix).multiply(reflectionCamera.matrixWorldInverse);
  const visibleWire=wire.visible;water.visible=false;wire.visible=false;
  const tone=renderer.toneMapping;
  renderer.toneMapping=THREE.NoToneMapping;
  renderer.setRenderTarget(reflectionTarget);renderer.render(world,reflectionCamera);
  refractionClip.constant=level;renderer.clippingPlanes=[refractionClip];
  renderer.setRenderTarget(refractionTarget);renderer.render(world,camera);renderer.clippingPlanes=[];
  renderer.toneMapping=tone;renderer.setRenderTarget(null);
  water.visible=true;wire.visible=visibleWire;renderer.render(world,camera);
}
function animate(now){
  const elapsed=Math.max(0,Math.min((now-last)/1000,.1));last=now;
  if(running){
    refineCheck+=elapsed;
    if(refineCheck>.08){if(solver.refineIfNeeded()){rebuildWater();toast('Steep wave detected · surface refined to '+solver.nx+' × '+solver.n);}refineCheck=0;}
    const advanced=solver.advance(elapsed,32,dt=>{
      // Body forcing and emitters use precisely the same fixed physics clock.
      duckState.update(solver,dt,level);
      if($('autoRipples').checked){
        appearanceTime+=dt;eventClock+=dt;
        if(mood==='rain'&&eventClock>=.10){solver.disturb((rand()-.5)*(POND_WIDTH-.5),(rand()-.5)*(POND_DEPTH-.5),.12+rand()*.13,.12);eventClock-=.10;}
        else if(mood!=='rain'&&eventClock>=1.9){solver.disturb(.5+Math.sin(solver.time*.43)*1.8,Math.cos(solver.time*.7)*1.5,.12,.3);eventClock-=1.9;}
      }
    });
    simTime=solver.time;syncDuck(advanced);updateLilies(advanced);
  }else {updateLilies(0);syncDuck(0);}
  orbit.update();if(running||renderDirty){updateWater();renderScene();renderDirty=false;frames++;}
  if(now-fpsTime>.5*1000){frameRate=Math.round(frames*1000/(now-fpsTime));frames=0;fpsTime=now;updateMetrics();}
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
window.pondDiagnostics=()=>({n:solver.n,nx:solver.nx,width:POND_WIDTH,depth:POND_DEPTH,waterStyle,duck:{x:duckState.x,z:duckState.z,y:duckState.y,vx:duckState.vx,vz:duckState.vz,wakes:duckState.wakes,selected:duckSelected},base:solver.base,refinements:solver.refinements,dx:solver.dx,dt:solver.lastDt,clampHits:solver.clampHits,accumulator:solver.accumulator,energy:solver.energy(),autoRipples:$('autoRipples').checked,quality,geometry:{width:bed.geometry.parameters.width,depth:bed.geometry.parameters.height},cfl:solver.stableDt(),time:simTime,running,level,tool,finite:solver.height.every(Number.isFinite)&&solver.velocity.every(Number.isFinite),maxHeight:solver.height.reduce((a,b)=>Math.max(a,b),-Infinity),minHeight:solver.height.reduce((a,b)=>Math.min(a,b),Infinity),lilies:lilies.map(l=>({x:l.x,z:l.z,y:l.group.position.y})),camera:camera.position.toArray(),cameraFov:camera.fov,renderCalls:renderer.info.render.calls});
