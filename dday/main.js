import * as THREE from 'three';

/* ============================================================
   OPERATION FRONTIER · D-DAY — low-poly beach assault
   Drive from the rising tide to the giant wall.
   ============================================================ */

/* ---------------- utils ---------------- */
function mulberry32(a){return function(){a|=0;a=a+0x6D2B79F5|0;let t=Math.imul(a^a>>>15,1|a);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};}
const rng = mulberry32(20440606);
const rand=(a,b)=>a+(b-a)*rng();
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const lerp=(a,b,t)=>a+(b-a)*t;
function smoothstep(e0,e1,x){const t=clamp((x-e0)/(e1-e0),0,1);return t*t*(3-2*t);}
function fnoise(x,y){
  return (Math.sin(x*1.7+y*0.9)+Math.sin(x*0.8-y*1.3+2.1)
        +Math.sin((x+y)*1.1+4.7)+0.5*Math.sin(x*2.3+y*1.9+1.3))/3.5;
}
function hash2(x,y){const s=Math.sin(x*127.1+y*311.7)*43758.5453;return s-Math.floor(s);}

/* ---------------- renderer / scene ---------------- */
const renderer=new THREE.WebGLRenderer({antialias:true});
renderer.setPixelRatio(Math.min(window.devicePixelRatio,2));
renderer.setSize(innerWidth,innerHeight);
renderer.shadowMap.enabled=true;
renderer.shadowMap.type=THREE.PCFSoftShadowMap;
renderer.outputColorSpace=THREE.SRGBColorSpace;
document.getElementById('app').appendChild(renderer.domElement);

const scene=new THREE.Scene();
scene.background=new THREE.Color(0xbfe0ef);
scene.fog=new THREE.Fog(0xc4e2ef,220,620);

const camera=new THREE.PerspectiveCamera(62,innerWidth/innerHeight,0.1,1200);
camera.position.set(0,60,120);

const hemi=new THREE.HemisphereLight(0xcfe4ef,0x8f8062,0.95);
scene.add(hemi);
const sun=new THREE.DirectionalLight(0xfff1d6,1.7);
sun.position.set(90,150,70);
sun.castShadow=true;
sun.shadow.mapSize.set(2048,2048);
sun.shadow.camera.left=-95;sun.shadow.camera.right=95;
sun.shadow.camera.top=95;sun.shadow.camera.bottom=-95;
sun.shadow.camera.near=20;sun.shadow.camera.far=420;
sun.shadow.bias=-0.0008;
scene.add(sun);scene.add(sun.target);

addEventListener('resize',()=>{
  camera.aspect=innerWidth/innerHeight;camera.updateProjectionMatrix();
  renderer.setSize(innerWidth,innerHeight);
});

/* ---------------- terrain definition ---------------- */
const WALL_Z=-282;            // wall face
const WIN_Z=-256;             // reach this = victory
const ROADS=[{x:-62,ph:1.3,amp:8},{x:0,ph:4.1,amp:7},{x:62,ph:2.6,amp:9}];
const roadCenter=(r,z)=>r.x+Math.sin(z*0.02+r.ph)*r.amp;
function roadDist(x,z){let m=1e9;for(const r of ROADS)m=Math.min(m,Math.abs(x-roadCenter(r,z)));return m;}

const MOUNDS=[ // x,z,radius,height — huge mounds of earth between the roads
  [-34,-85,18,7.5],[36,-100,20,9],[-95,-120,22,10],[98,-150,19,8],
  [-30,-175,17,7],[95,-70,16,6.5],[-120,-200,21,9],[30,-230,18,8],
  [-92,-243,15,6],[118,-215,17,7],[-128,-60,15,6],[130,-130,16,7],[27,-52,13,5]];
const CRATERS=[[-15,-70,5,2],[50,-140,6,2.4],[-70,-95,4.5,1.8],[20,-190,5.5,2.2],
  [80,-220,5,2],[-45,-222,6,2.3],[10,-115,4,1.6],[-105,-170,5,2],[42,-28,4.5,1.5]];
const TRENCHES=[{z:-95,ph:0.7},{z:-155,ph:2.9},{z:-215,ph:5.1}];
const trenchLine=(t,x)=>t.z+Math.sin(x*0.045+t.ph)*5;

function trenchFactor(x,z){
  let f=0;
  const xm=1-smoothstep(126,134,Math.abs(x));
  if(xm<=0)return 0;
  const rm=smoothstep(6,11,roadDist(x,z));
  if(rm<=0)return 0;
  for(const t of TRENCHES){
    const dz=z-trenchLine(t,x);
    f=Math.max(f,Math.exp(-(dz*dz)/(2.6*2.6)));
  }
  return f*xm*rm;
}
function moundHeight(x,z){
  let m=0;
  for(const M of MOUNDS){
    const dx=x-M[0],dz=z-M[1];
    m+=M[3]*Math.exp(-(dx*dx+dz*dz)/(M[2]*M[2]));
  }
  return m;
}
function craterDepth(x,z){
  let c=0;
  for(const C of CRATERS){
    const dx=x-C[0],dz=z-C[1];
    c+=C[3]*Math.exp(-(dx*dx+dz*dz)/(C[2]*C[2]));
  }
  return c;
}
function heightAt(x,z){
  let h=fnoise(x*0.021+3.1,z*0.019+7.7)*2.6+fnoise(x*0.008+11,z*0.008+5)*3.0;
  const rd=roadDist(x,z);
  const roadK=1-smoothstep(4.5,9.5,rd);
  h+=fnoise(x*0.13,z*0.13)*0.55*(1-roadK);          // detail, smoothed away on roads
  const beachK=smoothstep(-50,-15,z);               // flatten toward beach
  h=lerp(h,h*0.22,beachK);
  const t=Math.max(0,z+20)/90;                      // slope into ocean
  h-=16*t*t;
  h+=9.0*(1-smoothstep(-240,-20,z));                // field climbs steadily inland
  h+=moundHeight(x,z);
  h-=craterDepth(x,z);
  h-=2.5*trenchFactor(x,z);                         // trenches carved in
  const wallK=1-smoothstep(-268,-242,z);            // high plateau at the wall
  h=lerp(h,9.0,wallK*0.9);
  h+=smoothstep(128,152,Math.abs(x))*9;             // containment berms
  h-=roadK*0.25;
  return h;
}

/* ---------------- terrain mesh ---------------- */
const C_GRASS_A=new THREE.Color(0x84b95b), C_GRASS_B=new THREE.Color(0x74a94f);
const C_SAND=new THREE.Color(0xe0cf9c), C_SAND_WET=new THREE.Color(0xbfa87e);
const C_ROAD=new THREE.Color(0xa38a5b), C_TRENCH=new THREE.Color(0x6f5a3c);
const C_EARTH=new THREE.Color(0x8d7550), C_DUST=new THREE.Color(0xb3a98c);
const _c=new THREE.Color();

function faceColor(x,z,h,ny,out){
  const n01=hash2(Math.floor(x*0.5),Math.floor(z*0.5));
  out.copy(C_GRASS_A).lerp(C_GRASS_B,n01);
  const slope=1-ny;
  out.lerp(C_EARTH,smoothstep(0.10,0.30,slope)*0.85);          // steep = dirt
  out.lerp(C_TRENCH,trenchFactor(x,z)*0.85);                   // trench floor
  const rk=1-smoothstep(4.2,8.5,roadDist(x,z));
  out.lerp(C_ROAD,rk*0.92);
  if(roadDist(x,z)<1.7)out.multiplyScalar(0.93);               // worn centre
  const sandK=smoothstep(-30,-10,z);
  out.lerp(C_SAND,sandK);
  if(z>-32)out.lerp(C_SAND_WET,smoothstep(0.8,-1.4,h)*sandK);  // wet sand low
  out.lerp(C_DUST,(1-smoothstep(-268,-246,z))*0.55);           // dusty wall plateau
  const j=0.95+hash2(x*3.7,z*2.9)*0.09;
  out.multiplyScalar(j);
}

function buildTerrain(){
  const W=340,D=540,SX=170,SZ=270;
  let g=new THREE.PlaneGeometry(W,D,SX,SZ);
  g.rotateX(-Math.PI/2);
  g.translate(0,0,-70);                       // z from -340 .. +200
  const p=g.attributes.position;
  for(let i=0;i<p.count;i++){
    p.setY(i,heightAt(p.getX(i),p.getZ(i)));
  }
  g=g.toNonIndexed();
  g.computeVertexNormals();
  const pos=g.attributes.position,nor=g.attributes.normal;
  const colors=new Float32Array(pos.count*3);
  for(let i=0;i<pos.count;i+=3){
    const cx=(pos.getX(i)+pos.getX(i+1)+pos.getX(i+2))/3;
    const cz=(pos.getZ(i)+pos.getZ(i+1)+pos.getZ(i+2))/3;
    const ch=(pos.getY(i)+pos.getY(i+1)+pos.getY(i+2))/3;
    const ny=(nor.getY(i)+nor.getY(i+1)+nor.getY(i+2))/3;
    faceColor(cx,cz,ch,ny,_c);
    for(let k=0;k<3;k++){colors[(i+k)*3]=_c.r;colors[(i+k)*3+1]=_c.g;colors[(i+k)*3+2]=_c.b;}
  }
  g.setAttribute('color',new THREE.BufferAttribute(colors,3));
  const m=new THREE.MeshStandardMaterial({vertexColors:true,flatShading:true,roughness:1,metalness:0});
  const mesh=new THREE.Mesh(g,m);
  mesh.receiveShadow=true;
  scene.add(mesh);
}
buildTerrain();

// dark sea floor far out
{
  const floor=new THREE.Mesh(new THREE.PlaneGeometry(1600,1600),
    new THREE.MeshStandardMaterial({color:0x8a7f63,roughness:1}));
  floor.rotation.x=-Math.PI/2;floor.position.y=-16;
  scene.add(floor);
}

/* ---------------- ocean (rising tide) ---------------- */
const TIDE_START=-2.4, TIDE_RATE=0.0315, TIDE_MAX=6.5;
let waterLevel=TIDE_START;
let waterGeo,waterMesh,waterBase;
{
  let g=new THREE.PlaneGeometry(420,520,72,52);
  g.rotateX(-Math.PI/2);
  g.translate(0,0,40);                        // z -220 .. +300
  g=g.toNonIndexed();
  waterGeo=g;
  waterBase=g.attributes.position.array.slice();
  const m=new THREE.MeshStandardMaterial({color:0x3d8cab,transparent:true,opacity:0.86,
    flatShading:true,roughness:0.35,metalness:0.1});
  waterMesh=new THREE.Mesh(g,m);
  scene.add(waterMesh);
}
function updateWater(t){
  const p=waterGeo.attributes.position;
  const a=p.array;
  for(let i=0;i<a.length;i+=3){
    const x=waterBase[i],z=waterBase[i+2];
    a[i+1]=waterLevel+Math.sin(x*0.14+t*1.4)*0.22+Math.cos(z*0.17+t*1.1+x*0.05)*0.18;
  }
  p.needsUpdate=true;
  waterGeo.computeVertexNormals();
}

/* ---------------- shared materials ---------------- */
const MAT={
  concrete:new THREE.MeshStandardMaterial({color:0xb4b2a6,flatShading:true,roughness:1}),
  concreteDark:new THREE.MeshStandardMaterial({color:0x8b897f,flatShading:true,roughness:1}),
  wood:new THREE.MeshStandardMaterial({color:0x8a6a44,flatShading:true,roughness:1}),
  woodDark:new THREE.MeshStandardMaterial({color:0x6d5335,flatShading:true,roughness:1}),
  steel:new THREE.MeshStandardMaterial({color:0x4c5049,flatShading:true,roughness:0.8}),
  rust:new THREE.MeshStandardMaterial({color:0x64493a,flatShading:true,roughness:1}),
  olive:new THREE.MeshStandardMaterial({color:0x6d7a4f,flatShading:true,roughness:1}),
  oliveDark:new THREE.MeshStandardMaterial({color:0x565f40,flatShading:true,roughness:1}),
  wreck:new THREE.MeshStandardMaterial({color:0x3e3a34,flatShading:true,roughness:1}),
  sandbag:new THREE.MeshStandardMaterial({color:0xc9ba8d,flatShading:true,roughness:1}),
  mine:new THREE.MeshStandardMaterial({color:0x353832,flatShading:true,roughness:0.9}),
  mineTop:new THREE.MeshStandardMaterial({color:0x51554b,flatShading:true,roughness:0.9}),
  dark:new THREE.MeshStandardMaterial({color:0x24261f,flatShading:true,roughness:1}),
  glass:new THREE.MeshStandardMaterial({color:0x27343c,flatShading:true,roughness:0.25,metalness:0.4}),
  warnY:new THREE.MeshStandardMaterial({color:0xe4c33a,flatShading:true,roughness:1}),
};

/* ---------------- colliders / mines ---------------- */
const colliders=[]; // {x,z,r}
const mines=[];     // {x,z,type:'AT'|'AP',mesh,dead}
function addCollider(x,z,r){colliders.push({x,z,r});}

/* ---------------- explosions & effects ---------------- */
const effects=[];
const flashGeo=new THREE.SphereGeometry(1,10,8);
const debrisGeo=new THREE.BoxGeometry(0.22,0.22,0.22);
const smokeGeo=new THREE.IcosahedronGeometry(1,0);
const scorchGeo=new THREE.CircleGeometry(1,12);
let shake=0;

function spawnExplosion(pos,scale=1,scorch=true){
  const fm=new THREE.MeshBasicMaterial({color:0xffd66b,transparent:true,opacity:0.95});
  const flash=new THREE.Mesh(flashGeo,fm);
  flash.position.copy(pos);flash.position.y+=0.6*scale;
  scene.add(flash);
  effects.push({type:'flash',mesh:flash,t:0,dur:0.18,scale});
  for(let i=0;i<16;i++){
    const dm=new THREE.MeshBasicMaterial({color:i%3?0x5d4a33:0x2c2c28});
    const d=new THREE.Mesh(debrisGeo,dm);
    d.position.copy(pos);d.position.y+=0.5;
    d.scale.setScalar(rand(0.6,1.7)*scale);
    const a=rand(0,Math.PI*2),sp=rand(4,13)*scale;
    effects.push({type:'debris',mesh:d,t:0,dur:rand(0.9,1.6),
      vx:Math.cos(a)*sp,vy:rand(7,17)*scale,vz:Math.sin(a)*sp,
      rx:rand(-6,6),rz:rand(-6,6)});
    scene.add(d);
  }
  for(let i=0;i<6;i++){
    const sm=new THREE.MeshBasicMaterial({color:0x555049,transparent:true,opacity:0.55});
    const s=new THREE.Mesh(smokeGeo,sm);
    s.position.set(pos.x+rand(-1,1)*scale,pos.y+rand(0.3,1.4)*scale,pos.z+rand(-1,1)*scale);
    s.scale.setScalar(rand(0.7,1.4)*scale);
    scene.add(s);
    effects.push({type:'smoke',mesh:s,t:0,dur:rand(1.6,2.4),rise:rand(1.5,3),grow:rand(1.2,2.2)});
  }
  if(scorch){
    const sc=new THREE.Mesh(scorchGeo,new THREE.MeshBasicMaterial({color:0x1c1a16,transparent:true,opacity:0.5}));
    sc.rotation.x=-Math.PI/2;
    sc.scale.setScalar(2.2*scale);
    sc.position.set(pos.x,heightAt(pos.x,pos.z)+0.04,pos.z);
    scene.add(sc);
  }
  const d=camera.position.distanceTo(pos);
  shake=Math.min(1.2,shake+Math.max(0,scale*0.9-d*0.004));
  sfxBoom(clamp(0.9*scale-d*0.002,0.08,0.9));
}
function spawnSpark(pos){
  const fm=new THREE.MeshBasicMaterial({color:0xffe9a3,transparent:true,opacity:0.9});
  const f=new THREE.Mesh(flashGeo,fm);
  f.position.copy(pos);f.scale.setScalar(0.2);
  scene.add(f);
  effects.push({type:'flash',mesh:f,t:0,dur:0.1,scale:0.28});
}
function updateEffects(dt){
  for(let i=effects.length-1;i>=0;i--){
    const e=effects[i];e.t+=dt;
    const k=e.t/e.dur;
    if(k>=1){scene.remove(e.mesh);e.mesh.material.dispose();effects.splice(i,1);continue;}
    if(e.type==='flash'){
      e.mesh.scale.setScalar((0.4+k*3.2)*e.scale);
      e.mesh.material.opacity=0.95*(1-k);
    }else if(e.type==='debris'){
      e.vy-=32*dt;
      e.mesh.position.x+=e.vx*dt;e.mesh.position.y+=e.vy*dt;e.mesh.position.z+=e.vz*dt;
      e.mesh.rotation.x+=e.rx*dt;e.mesh.rotation.z+=e.rz*dt;
      if(e.mesh.position.y<heightAt(e.mesh.position.x,e.mesh.position.z))e.t=e.dur;
    }else if(e.type==='smoke'){
      e.mesh.position.y+=e.rise*dt;
      e.mesh.scale.multiplyScalar(1+e.grow*dt*0.6);
      e.mesh.material.opacity=0.55*(1-k);
    }
  }
}

/* ---------------- audio (procedural) ---------------- */
let AC=null;
function audioInit(){if(!AC){AC=new (window.AudioContext||window.webkitAudioContext)();}if(AC.state==='suspended')AC.resume();}
function sfxBoom(vol=0.5){
  if(!AC)return;
  const dur=0.8,sr=AC.sampleRate,buf=AC.createBuffer(1,sr*dur,sr),d=buf.getChannelData(0);
  for(let i=0;i<d.length;i++)d[i]=(Math.random()*2-1)*Math.pow(1-i/d.length,2.2);
  const src=AC.createBufferSource();src.buffer=buf;
  const f=AC.createBiquadFilter();f.type='lowpass';f.frequency.value=380;
  const g=AC.createGain();g.gain.setValueAtTime(vol,AC.currentTime);
  g.gain.exponentialRampToValueAtTime(0.001,AC.currentTime+dur);
  src.connect(f);f.connect(g);g.connect(AC.destination);src.start();
}
function sfxShot(vol=0.12){
  if(!AC)return;
  const o=AC.createOscillator(),g=AC.createGain();
  o.type='square';o.frequency.setValueAtTime(720,AC.currentTime);
  o.frequency.exponentialRampToValueAtTime(140,AC.currentTime+0.07);
  g.gain.setValueAtTime(vol,AC.currentTime);
  g.gain.exponentialRampToValueAtTime(0.001,AC.currentTime+0.09);
  o.connect(g);g.connect(AC.destination);o.start();o.stop(AC.currentTime+0.1);
}
function sfxHit(){
  if(!AC)return;
  const o=AC.createOscillator(),g=AC.createGain();
  o.type='triangle';o.frequency.setValueAtTime(220,AC.currentTime);
  o.frequency.exponentialRampToValueAtTime(60,AC.currentTime+0.12);
  g.gain.setValueAtTime(0.25,AC.currentTime);
  g.gain.exponentialRampToValueAtTime(0.001,AC.currentTime+0.14);
  o.connect(g);g.connect(AC.destination);o.start();o.stop(AC.currentTime+0.15);
}

/* ============================================================
   PROPS
   ============================================================ */
function groundY(x,z){return heightAt(x,z);}

/* ------- sandbags (instanced) ------- */
const sandbagPlacements=[];
function sandbagRow(x1,z1,x2,z2,layers=2){
  const dx=x2-x1,dz=z2-z1,len=Math.hypot(dx,dz),n=Math.max(2,Math.round(len/0.95));
  const ang=Math.atan2(dz,dx);
  for(let L=0;L<layers;L++){
    const cnt=n-L;
    for(let i=0;i<cnt;i++){
      const t=(i+0.5+L*0.5)/n;
      const x=x1+dx*t+rand(-0.08,0.08),z=z1+dz*t+rand(-0.08,0.08);
      sandbagPlacements.push({x,z,y:groundY(x,z)+0.28+L*0.5,rot:ang+rand(-0.2,0.2)});
    }
  }
}
function sandbagArc(cx,cz,r,a0,a1,layers=2){
  const len=Math.abs(a1-a0)*r,n=Math.max(3,Math.round(len/0.95));
  for(let L=0;L<layers;L++){
    for(let i=0;i<n-L;i++){
      const a=a0+(a1-a0)*((i+0.5+L*0.5)/n);
      const x=cx+Math.cos(a)*r,z=cz+Math.sin(a)*r;
      sandbagPlacements.push({x,z,y:groundY(x,z)+0.28+L*0.5,rot:-a+rand(-0.2,0.2)});
    }
  }
}
function buildSandbags(){
  const g=new THREE.IcosahedronGeometry(0.5,0);
  g.scale(1.5,0.62,0.85);
  const im=new THREE.InstancedMesh(g,MAT.sandbag,sandbagPlacements.length);
  const M=new THREE.Matrix4(),Q=new THREE.Quaternion(),E=new THREE.Euler(),S=new THREE.Vector3(1,1,1);
  sandbagPlacements.forEach((p,i)=>{
    E.set(rand(-0.06,0.06),p.rot,rand(-0.06,0.06));Q.setFromEuler(E);
    S.set(rand(0.9,1.1),rand(0.9,1.05),rand(0.9,1.1));
    M.compose(new THREE.Vector3(p.x,p.y,p.z),Q,S);
    im.setMatrixAt(i,M);
  });
  im.castShadow=true;im.receiveShadow=true;
  scene.add(im);
}

/* ------- barbed wire ------- */
function barbedWireRow(zBase,xFrom,xTo){
  const posts=[];
  for(let x=xFrom;x<=xTo;x+=4.5){
    const z=zBase+Math.sin(x*0.07)*2;
    if(roadDist(x,z)<8)continue;             // gaps at the roads
    posts.push({x,z});
  }
  if(posts.length<2)return;
  const postGeo=new THREE.CylinderGeometry(0.06,0.08,1.5,5);
  const im=new THREE.InstancedMesh(postGeo,MAT.woodDark,posts.length);
  const M=new THREE.Matrix4(),Q=new THREE.Quaternion(),E=new THREE.Euler();
  posts.forEach((p,i)=>{
    E.set(rand(-0.12,0.12),0,rand(-0.12,0.12));Q.setFromEuler(E);
    M.compose(new THREE.Vector3(p.x,groundY(p.x,p.z)+0.7,p.z),Q,new THREE.Vector3(1,1,1));
    im.setMatrixAt(i,M);
  });
  im.castShadow=true;
  scene.add(im);
  // wire strands between consecutive posts (split at road gaps)
  const wireMat=new THREE.MeshStandardMaterial({color:0x3a3a34,roughness:1});
  let seg=[];
  const flush=()=>{
    if(seg.length>=2){
      for(const yOff of [0.45,0.95,1.3]){
        const pts=[];
        seg.forEach((p,i)=>{
          pts.push(new THREE.Vector3(p.x,groundY(p.x,p.z)+yOff+Math.sin(i*2.3)*0.08,p.z));
        });
        const curve=new THREE.CatmullRomCurve3(pts);
        const tg=new THREE.TubeGeometry(curve,seg.length*4,0.03,3,false);
        scene.add(new THREE.Mesh(tg,wireMat));
      }
    }
    seg=[];
  };
  for(let i=0;i<posts.length;i++){
    if(i>0&&Math.abs(posts[i].x-posts[i-1].x)>6){flush();}
    seg.push(posts[i]);
  }
  flush();
}

/* ------- czech hedgehogs (beach barricades) ------- */
function makeHedgehog(x,z,s=1){
  const grp=new THREE.Group();
  const beam=new THREE.BoxGeometry(0.26*s,3.2*s,0.26*s);
  const dirs=[new THREE.Vector3(1,1,0),new THREE.Vector3(-0.5,1,0.87),new THREE.Vector3(-0.5,1,-0.87)];
  const up=new THREE.Vector3(0,1,0);
  for(const d of dirs){
    const m=new THREE.Mesh(beam,MAT.rust);
    m.quaternion.setFromUnitVectors(up,d.clone().normalize());
    m.castShadow=true;
    grp.add(m);
  }
  const y=groundY(x,z);
  grp.position.set(x,y+0.85*s,z);
  grp.rotation.y=rand(0,Math.PI*2);
  scene.add(grp);
  addCollider(x,z,1.5*s);
}

/* ------- wooden barricade ------- */
function makeBarricade(x,z,rot){
  const grp=new THREE.Group();
  const leg=new THREE.BoxGeometry(0.18,2.0,0.18);
  for(const sx of [-1.6,1.6]){
    const a=new THREE.Mesh(leg,MAT.wood);a.position.set(sx,0.75,0);a.rotation.x=0.5;a.castShadow=true;grp.add(a);
    const b=new THREE.Mesh(leg,MAT.wood);b.position.set(sx,0.75,0);b.rotation.x=-0.5;b.castShadow=true;grp.add(b);
  }
  const plank=new THREE.BoxGeometry(4.2,0.22,0.1);
  for(const py of [0.55,1.0,1.45]){
    const p=new THREE.Mesh(plank,MAT.woodDark);
    p.position.set(0,py,(py-1.0)*-0.45);
    p.castShadow=true;grp.add(p);
  }
  grp.position.set(x,groundY(x,z),z);
  grp.rotation.y=rot;
  scene.add(grp);
  addCollider(x,z,2.1);
}

/* ------- dragon's teeth ------- */
function dragonTeethRow(x1,z1,x2,z2,n){
  const g=new THREE.ConeGeometry(0.95,1.5,4);
  for(let i=0;i<n;i++){
    const t=i/(n-1);
    const x=lerp(x1,x2,t)+rand(-0.3,0.3),z=lerp(z1,z2,t)+rand(-0.3,0.3);
    const m=new THREE.Mesh(g,MAT.concreteDark);
    m.position.set(x,groundY(x,z)+0.6,z);
    m.rotation.y=rand(0,1);
    m.castShadow=true;
    scene.add(m);
    addCollider(x,z,1.1);
  }
}

/* ------- mines ------- */
const mineATGeo=new THREE.CylinderGeometry(0.62,0.68,0.22,10);
const mineATTopGeo=new THREE.CylinderGeometry(0.2,0.24,0.1,8);
const mineAPGeo=new THREE.CylinderGeometry(0.22,0.26,0.24,7);
const prongGeo=new THREE.CylinderGeometry(0.015,0.015,0.22,3);
function placeMine(x,z,type){
  const y=groundY(x,z);
  const grp=new THREE.Group();
  if(type==='AT'){
    const b=new THREE.Mesh(mineATGeo,MAT.mine);b.castShadow=true;grp.add(b);
    const t=new THREE.Mesh(mineATTopGeo,MAT.mineTop);t.position.y=0.14;grp.add(t);
    grp.position.set(x,y+0.1,z);
  }else{
    const b=new THREE.Mesh(mineAPGeo,MAT.mine);grp.add(b);
    for(let i=0;i<3;i++){
      const p=new THREE.Mesh(prongGeo,MAT.mineTop);
      p.position.set(Math.cos(i*2.1)*0.08,0.2,Math.sin(i*2.1)*0.08);
      grp.add(p);
    }
    grp.position.set(x,y+0.08,z);
  }
  scene.add(grp);
  mines.push({x,z,type,mesh:grp,dead:false});
}
function mineSign(x,z){
  const grp=new THREE.Group();
  const post=new THREE.Mesh(new THREE.CylinderGeometry(0.05,0.06,1.2,5),MAT.woodDark);
  post.position.y=0.6;grp.add(post);
  const sign=new THREE.Mesh(new THREE.BoxGeometry(0.7,0.7,0.05),MAT.warnY);
  sign.position.y=1.25;sign.rotation.z=Math.PI/4;sign.castShadow=true;grp.add(sign);
  const skullBar=new THREE.Mesh(new THREE.BoxGeometry(0.5,0.12,0.06),MAT.dark);
  skullBar.position.y=1.25;grp.add(skullBar);
  grp.position.set(x,groundY(x,z),z);
  grp.rotation.y=rand(-0.4,0.4);
  scene.add(grp);
}

/* ------- tank ------- */
function makeTank(x,z,rotY,wrecked=false){
  const grp=new THREE.Group();
  const hullMat=wrecked?MAT.wreck:MAT.olive;
  const darkMat=wrecked?MAT.dark:MAT.oliveDark;
  for(const sx of [-1.55,1.55]){
    const tr=new THREE.Mesh(new THREE.BoxGeometry(0.95,1.0,5.2),darkMat);
    tr.position.set(sx,0.55,0);tr.castShadow=true;grp.add(tr);
    for(let w=0;w<4;w++){
      const wheel=new THREE.Mesh(new THREE.CylinderGeometry(0.42,0.42,1.0,8),MAT.dark);
      wheel.rotation.z=Math.PI/2;
      wheel.position.set(sx,0.45,-1.8+w*1.2);
      grp.add(wheel);
    }
  }
  const hull=new THREE.Mesh(new THREE.BoxGeometry(2.5,0.85,4.6),hullMat);
  hull.position.y=1.25;hull.castShadow=true;grp.add(hull);
  const glacis=new THREE.Mesh(new THREE.BoxGeometry(2.5,0.8,1.3),hullMat);
  glacis.position.set(0,1.05,2.5);glacis.rotation.x=0.55;glacis.castShadow=true;grp.add(glacis);
  const turret=new THREE.Mesh(new THREE.CylinderGeometry(1.05,1.25,0.75,8),hullMat);
  turret.position.y=2.0;turret.castShadow=true;
  if(wrecked){turret.rotation.z=0.35;turret.position.x=0.4;turret.position.y=1.9;}
  grp.add(turret);
  const barrel=new THREE.Mesh(new THREE.CylinderGeometry(0.09,0.11,3.4,6),darkMat);
  barrel.rotation.x=Math.PI/2;
  barrel.position.set(wrecked?0.4:0,wrecked?2.05:2.1,2.6);
  if(wrecked)barrel.rotation.z=0.3;
  barrel.castShadow=true;grp.add(barrel);
  const hatch=new THREE.Mesh(new THREE.CylinderGeometry(0.3,0.32,0.16,7),darkMat);
  hatch.position.set(-0.3,2.42,-0.2);grp.add(hatch);
  const y=groundY(x,z);
  grp.position.set(x,y,z);
  grp.rotation.y=rotY;
  grp.rotation.z=rand(-0.05,0.05);
  scene.add(grp);
  addCollider(x,z,3.2);
  if(wrecked){
    const sc=new THREE.Mesh(scorchGeo,new THREE.MeshBasicMaterial({color:0x1c1a16,transparent:true,opacity:0.45}));
    sc.rotation.x=-Math.PI/2;sc.scale.setScalar(4);
    sc.position.set(x,y+0.05,z);
    scene.add(sc);
  }
}

/* ------- landing craft on the beach ------- */
function makeLandingCraft(x,z,rot){
  const grp=new THREE.Group();
  const mat=MAT.steel;
  const hullF=new THREE.Mesh(new THREE.BoxGeometry(3.4,0.5,9),mat);
  hullF.position.y=0.4;grp.add(hullF);
  for(const sx of [-1.7,1.7]){
    const side=new THREE.Mesh(new THREE.BoxGeometry(0.25,1.8,9),mat);
    side.position.set(sx,1.2,0);side.castShadow=true;grp.add(side);
  }
  const back=new THREE.Mesh(new THREE.BoxGeometry(3.6,1.8,0.3),mat);
  back.position.set(0,1.2,-4.4);grp.add(back);
  const ramp=new THREE.Mesh(new THREE.BoxGeometry(3.4,0.22,3.2),MAT.rust);
  ramp.position.set(0,0.8,5.7);ramp.rotation.x=0.5;ramp.castShadow=true;grp.add(ramp);
  grp.position.set(x,groundY(x,z)+0.2,z);
  grp.rotation.y=rot;grp.rotation.z=rand(-0.06,0.06);
  scene.add(grp);
  addCollider(x,z,4.5);
}

/* ============================================================
   THE GIANT WALL + SENTRY BUNKERS
   ============================================================ */
const sentries=[];
function makeSentryBunker(x,z,faceAngle){
  const grp=new THREE.Group();
  const y=groundY(x,z);
  // concrete body with firing slit (built from slabs)
  const back=new THREE.Mesh(new THREE.BoxGeometry(7.5,3.6,1.0),MAT.concrete);
  back.position.set(0,1.8,-2.4);back.castShadow=true;grp.add(back);
  for(const sx of [-3.4,3.4]){
    const side=new THREE.Mesh(new THREE.BoxGeometry(1.0,3.6,5.6),MAT.concrete);
    side.position.set(sx,1.8,0);side.castShadow=true;grp.add(side);
  }
  const frontLow=new THREE.Mesh(new THREE.BoxGeometry(7.8,1.7,1.1),MAT.concrete);
  frontLow.position.set(0,0.85,2.6);frontLow.castShadow=true;grp.add(frontLow);
  const frontHigh=new THREE.Mesh(new THREE.BoxGeometry(7.8,1.0,1.1),MAT.concrete);
  frontHigh.position.set(0,3.1,2.6);frontHigh.castShadow=true;grp.add(frontHigh);
  const roof=new THREE.Mesh(new THREE.BoxGeometry(8.6,0.8,7.0),MAT.concreteDark);
  roof.position.set(0,4.0,0.2);roof.castShadow=true;grp.add(roof);
  const inner=new THREE.Mesh(new THREE.BoxGeometry(6,2.2,0.3),MAT.dark);
  inner.position.set(0,1.9,1.4);grp.add(inner);
  // sentry gun on a yaw pivot poking through the slit
  const yaw=new THREE.Group();
  yaw.position.set(0,2.1,2.2);
  const mount=new THREE.Mesh(new THREE.BoxGeometry(0.5,0.5,0.8),MAT.steel);
  yaw.add(mount);
  const pitch=new THREE.Group();
  yaw.add(pitch);
  const gun=new THREE.Mesh(new THREE.CylinderGeometry(0.09,0.12,2.8,6),MAT.dark);
  gun.rotation.x=Math.PI/2;gun.position.z=1.6;
  pitch.add(gun);
  const muzzle=new THREE.Mesh(new THREE.CylinderGeometry(0.14,0.14,0.4,6),MAT.steel);
  muzzle.rotation.x=Math.PI/2;muzzle.position.z=2.9;
  pitch.add(muzzle);
  const flash=new THREE.Mesh(flashGeo,new THREE.MeshBasicMaterial({color:0xffd66b,transparent:true,opacity:0}));
  flash.scale.setScalar(0.35);flash.position.z=3.2;
  pitch.add(flash);
  grp.add(yaw);
  grp.position.set(x,y,z);
  grp.rotation.y=faceAngle;
  scene.add(grp);
  addCollider(x,z,5.2);
  // sandbag skirt
  sandbagArc(x+Math.sin(faceAngle)*5.5,z+Math.cos(faceAngle)*5.5,4.2,
    -faceAngle-Math.PI*0.15,-faceAngle+Math.PI*1.15,2);
  sentries.push({grp,yaw,pitch,flash,x,z,y:y+2.1,faceAngle,
    cooldown:rand(0.5,2),burst:0,range:115});
}

function buildWall(){
  const plateau=9.0;
  const wallGroup=new THREE.Group();
  for(let x=-176;x<=176;x+=32){
    if(Math.abs(x)<16){continue;} // gate section
    const panel=new THREE.Mesh(new THREE.BoxGeometry(32,26,5),MAT.concrete);
    panel.position.set(x,plateau+13,WALL_Z);
    panel.castShadow=true;panel.receiveShadow=true;
    wallGroup.add(panel);
    const cap=new THREE.Mesh(new THREE.BoxGeometry(32,1.6,7),MAT.concreteDark);
    cap.position.set(x,plateau+26.8,WALL_Z);
    cap.castShadow=true;wallGroup.add(cap);
    const butt=new THREE.Mesh(new THREE.BoxGeometry(3,20,8),MAT.concreteDark);
    butt.position.set(x-16,plateau+10,WALL_Z+1.5);
    butt.castShadow=true;wallGroup.add(butt);
  }
  // gate towers + sealed door
  for(const sx of [-12,12]){
    const tower=new THREE.Mesh(new THREE.BoxGeometry(9,32,9),MAT.concreteDark);
    tower.position.set(sx,plateau+16,WALL_Z);
    tower.castShadow=true;wallGroup.add(tower);
    const top=new THREE.Mesh(new THREE.BoxGeometry(10.5,2,10.5),MAT.concrete);
    top.position.set(sx,plateau+33,WALL_Z);
    top.castShadow=true;wallGroup.add(top);
    const lamp=new THREE.Mesh(new THREE.SphereGeometry(0.5,8,6),
      new THREE.MeshBasicMaterial({color:0xff4433}));
    lamp.position.set(sx,plateau+34.6,WALL_Z);
    wallGroup.add(lamp);
  }
  const lintel=new THREE.Mesh(new THREE.BoxGeometry(16,10,6),MAT.concrete);
  lintel.position.set(0,plateau+21,WALL_Z);
  lintel.castShadow=true;wallGroup.add(lintel);
  const door=new THREE.Mesh(new THREE.BoxGeometry(14,16,1.6),MAT.steel);
  door.position.set(0,plateau+8,WALL_Z+2);
  door.castShadow=true;wallGroup.add(door);
  for(let i=-1;i<=1;i+=2){
    const rib=new THREE.Mesh(new THREE.BoxGeometry(0.6,16,0.4),MAT.dark);
    rib.position.set(i*3.5,plateau+8,WALL_Z+2.9);
    wallGroup.add(rib);
  }
  scene.add(wallGroup);
  // sentry bunkers guarding the wall (facing the sea, +z)
  for(const bx of [-130,-85,-38,38,85,130]) makeSentryBunker(bx,-266,0);
  // two forward pillboxes mid-field
  makeSentryBunker(-78,-188,0.25);
  makeSentryBunker(82,-128,-0.2);
}

/* ============================================================
   WORLD POPULATION
   ============================================================ */
function populate(){
  buildWall();

  // --- tanks (static / knocked out) ---
  makeTank(-40,-64,2.6,false);
  makeTank(24,-118,-0.4,false);
  makeTank(76,-172,2.9,true);
  makeTank(-90,-142,0.7,false);
  makeTank(8,-38,2.4,true);
  makeTank(108,-96,-2.2,false);
  makeTank(-118,-228,0.3,false);

  // --- landing craft washed up on the beach ---
  makeLandingCraft(-38,10,0.15);
  makeLandingCraft(45,13,-0.3);
  makeLandingCraft(96,5,0.4);

  // --- czech hedgehogs on the beach ---
  for(let i=0;i<34;i++){
    const x=rand(-140,140),z=rand(-24,20);
    if(roadDist(x,z)<5.5&&z<10)continue;
    makeHedgehog(x,z,rand(0.85,1.25));
  }
  // a few scattered inland
  for(let i=0;i<10;i++){
    const x=rand(-130,130),z=rand(-240,-50);
    if(roadDist(x,z)<6)continue;
    makeHedgehog(x,z,rand(0.9,1.1));
  }

  // --- barbed wire rows (with gaps at roads) ---
  barbedWireRow(-14,-140,140);
  barbedWireRow(-76,-134,134);
  barbedWireRow(-136,-134,134);
  barbedWireRow(-196,-134,134);

  // --- wooden barricades: slalom chicanes on the roads ---
  for(const r of ROADS){
    for(const z of [-40,-115,-176,-235]){
      const c=roadCenter(r,z);
      const side=rng()>0.5?1:-1;
      makeBarricade(c+side*2.6,z,rand(-0.3,0.3));
    }
  }
  // dragon's teeth near the wall approach
  dragonTeethRow(-150,-244,-20,-247,16);
  dragonTeethRow(20,-247,150,-244,16);

  // --- trench dressing: sandbag parapets along each trench ---
  for(const t of TRENCHES){
    for(let x=-126;x<126;x+=9){
      const z=trenchLine(t,x)+3.6;
      if(roadDist(x,z)<9)continue;
      sandbagRow(x-3.6,z,x+3.6,trenchLine(t,x+7)+3.6,2);
    }
    // duckboard crossings where roads pass
    for(const r of ROADS){
      const cx=roadCenter(r,t.z);
      const plank=new THREE.Mesh(new THREE.BoxGeometry(7,0.25,9),MAT.wood);
      const z=trenchLine(t,cx);
      plank.position.set(cx,groundY(cx,z)+0.12,z);
      plank.castShadow=true;plank.receiveShadow=true;
      scene.add(plank);
    }
  }
  // sandbag nests scattered mid-field
  sandbagArc(-52,-120,3.4,-0.6,3.7,2);
  sandbagArc(40,-200,3.4,2.6,6.9,2);
  sandbagArc(-16,-160,3.2,-0.4,3.9,2);
  buildSandbags();

  // --- mines ---
  // AT mines on the roads (the fast route is the dangerous route)
  for(const r of ROADS){
    for(let i=0;i<7;i++){
      const z=rand(-245,-30);
      const c=roadCenter(r,z);
      placeMine(c+rand(-3.2,3.2),z,'AT');
    }
  }
  // AT mine belt in front of the wall
  for(let i=0;i<14;i++){
    const x=rand(-140,140),z=rand(-252,-236);
    if(roadDist(x,z)<3)continue;
    placeMine(x,z,'AT');
  }
  // AP minefields in the grass between roads
  const fields=[[-45,-20,-140,-60],[15,45,-200,-120],[-115,-70,-230,-160],[70,120,-110,-40],[-30,25,-95,-58]];
  for(const F of fields){
    for(let i=0;i<11;i++){
      const x=rand(F[0],F[1]),z=rand(F[3],F[2]);
      if(roadDist(x,z)<5)continue;
      placeMine(x,z,'AP');
    }
    mineSign(rand(F[0],F[1]),F[3]+rand(1,5));
    mineSign(rand(F[0],F[1]),F[2]-rand(1,5));
  }

  // --- clouds ---
  const cm=new THREE.MeshBasicMaterial({color:0xf7fafc});
  for(let i=0;i<9;i++){
    const c=new THREE.Group();
    for(let k=0;k<3;k++){
      const puff=new THREE.Mesh(smokeGeo,cm);
      puff.scale.set(rand(6,11),rand(2.5,4),rand(4,7));
      puff.position.set(k*7-7+rand(-2,2),rand(-1,1),rand(-3,3));
      c.add(puff);
    }
    c.position.set(rand(-260,260),rand(62,95),rand(-320,140));
    c.userData.speed=rand(0.8,1.8);
    clouds.push(c);scene.add(c);
  }
}
const clouds=[];

/* ============================================================
   THE CAR — low-poly sedan (Sentra-style silhouette)
   ============================================================ */
const car={
  group:new THREE.Group(),
  x:roadCenter(ROADS[1],0),z:0,heading:Math.PI,   // on the centre road, facing inland (-z)
  speed:0,steer:0,
  hp:100,alive:true,
  lastHitT:-99,
  wheelsF:[],wheelsAll:[],
  visY:0,visPitch:0,visRoll:0,
};
function buildCar(){
  const bodyG=new THREE.Group();
  bodyG.rotation.y=-Math.PI/2;               // profile +x -> world +z (front)
  const paint=new THREE.MeshStandardMaterial({color:0xc23b2e,flatShading:true,roughness:0.5,metalness:0.15});

  const s=new THREE.Shape();
  s.moveTo(-2.28,0.30);
  s.lineTo(-2.36,0.62);
  s.lineTo(-2.30,0.92);
  s.lineTo(-1.42,0.99);
  s.lineTo(-0.76,1.38);
  s.lineTo(0.44,1.43);
  s.lineTo(1.16,1.01);
  s.lineTo(2.24,0.93);
  s.lineTo(2.44,0.66);
  s.lineTo(2.38,0.32);
  s.lineTo(1.9,0.26);
  s.lineTo(-1.9,0.26);
  s.closePath();
  const bodyGeo=new THREE.ExtrudeGeometry(s,{depth:1.62,bevelEnabled:true,
    bevelThickness:0.06,bevelSize:0.05,bevelSegments:1,steps:1});
  bodyGeo.translate(0,0,-0.81);
  const body=new THREE.Mesh(bodyGeo,paint);
  body.castShadow=true;
  bodyG.add(body);

  // greenhouse / glass
  const g=new THREE.Shape();
  g.moveTo(-0.70,1.00);
  g.lineTo(-0.58,1.33);
  g.lineTo(0.36,1.37);
  g.lineTo(0.98,1.00);
  g.closePath();
  const glassGeo=new THREE.ExtrudeGeometry(g,{depth:1.86,bevelEnabled:false});
  glassGeo.translate(0,0,-0.93);
  const glass=new THREE.Mesh(glassGeo,MAT.glass);
  bodyG.add(glass);

  // wheels
  const tireGeo=new THREE.CylinderGeometry(0.37,0.37,0.26,10);
  tireGeo.rotateX(Math.PI/2);
  const hubGeo=new THREE.CylinderGeometry(0.17,0.17,0.28,8);
  hubGeo.rotateX(Math.PI/2);
  const tireMat=new THREE.MeshStandardMaterial({color:0x22241f,flatShading:true,roughness:1});
  const hubMat=new THREE.MeshStandardMaterial({color:0xb9bcc0,flatShading:true,roughness:0.4,metalness:0.5});
  for(const [wx,wz,front] of [[1.5,0.82,true],[1.5,-0.82,true],[-1.48,0.82,false],[-1.48,-0.82,false]]){
    const w=new THREE.Group();
    const tire=new THREE.Mesh(tireGeo,tireMat);tire.castShadow=true;
    const hub=new THREE.Mesh(hubGeo,hubMat);
    w.add(tire);w.add(hub);
    w.position.set(wx,0.37,wz);
    bodyG.add(w);
    car.wheelsAll.push(w);
    if(front)car.wheelsF.push(w);
  }
  // lights & trim
  const hlGeo=new THREE.BoxGeometry(0.07,0.15,0.36);
  const hlMat=new THREE.MeshStandardMaterial({color:0xfef6d8,emissive:0xaa9955,flatShading:true});
  const tlMat=new THREE.MeshStandardMaterial({color:0xd0342b,emissive:0x661511,flatShading:true});
  for(const zz of [-0.52,0.52]){
    const hl=new THREE.Mesh(hlGeo,hlMat);hl.position.set(2.44,0.72,zz);bodyG.add(hl);
    const tl=new THREE.Mesh(hlGeo,tlMat);tl.position.set(-2.37,0.74,zz);bodyG.add(tl);
  }
  const grille=new THREE.Mesh(new THREE.BoxGeometry(0.06,0.16,0.8),MAT.dark);
  grille.position.set(2.45,0.71,0);bodyG.add(grille);
  const mirGeo=new THREE.BoxGeometry(0.16,0.12,0.1);
  for(const zz of [-0.95,0.95]){
    const mir=new THREE.Mesh(mirGeo,paint);
    mir.position.set(0.95,1.06,zz);bodyG.add(mir);
  }
  const bumpF=new THREE.Mesh(new THREE.BoxGeometry(0.18,0.14,1.7),MAT.steel);
  bumpF.position.set(2.42,0.42,0);bodyG.add(bumpF);
  const bumpR=new THREE.Mesh(new THREE.BoxGeometry(0.18,0.14,1.7),MAT.steel);
  bumpR.position.set(-2.38,0.42,0);bodyG.add(bumpR);

  car.group.add(bodyG);
  car.group.rotation.order='YXZ';
  scene.add(car.group);
}
buildCar();

/* ---------------- input ---------------- */
const keys={};
addEventListener('keydown',e=>{
  if(['Space','ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(e.code))e.preventDefault();
  keys[e.code]=true;
  if(e.code==='KeyR'&&state!=='menu')location.reload();
  if((e.code==='Enter'||e.code==='Space')&&state==='menu')startGame();
});
addEventListener('keyup',e=>{keys[e.code]=false;});

/* ---------------- game state / HUD ---------------- */
let state='menu';
let tideT=0,playT=0,warnPulse=0;
const $=id=>document.getElementById(id);
const hud=$('hud'),hpFill=$('hpFill'),distEl=$('dist'),timerEl=$('timer'),
  tideFill=$('tideFill'),tideVal=$('tideVal'),warnEl=$('warn'),
  speedVal=$('speedVal'),vignette=$('vignette');
$('startBtn').addEventListener('click',startGame);
$('againBtn').addEventListener('click',()=>location.reload());

function startGame(){
  if(state!=='menu')return;
  audioInit();
  state='playing';
  $('menu').classList.add('hidden');
  hud.classList.add('on');
}
function endGame(win,title,sub){
  if(state!=='playing')return;
  state=win?'won':'dead';
  const t=Math.floor(playT);
  $('endKicker').textContent=win?'MISSION ACCOMPLISHED':'MISSION FAILED';
  const et=$('endTitle');
  et.textContent=title;
  et.className=win?'win':'lose';
  $('endSub').innerHTML=sub;
  $('endStats').textContent=`TIME ${String(Math.floor(t/60)).padStart(2,'0')}:${String(t%60).padStart(2,'0')}  ·  HULL ${Math.max(0,Math.round(car.hp))}%`;
  setTimeout(()=>$('end').classList.remove('hidden'),win?600:1100);
}
function damageCar(amount,silent){
  if(!car.alive||state!=='playing')return;
  car.hp-=amount;
  car.lastHitT=playT;
  if(!silent)sfxHit();
  vignette.style.opacity=Math.min(1,0.25+amount*0.02);
  setTimeout(()=>{vignette.style.opacity=0;},180);
  if(car.hp<=0){
    car.hp=0;car.alive=false;
    spawnExplosion(car.group.position.clone(),2.2);
    car.group.visible=false;
    endGame(false,'VEHICLE DESTROYED','Your car was torn apart before reaching the wall.');
  }
}

/* ---------------- projectiles ---------------- */
const projectiles=[];
const tracerGeo=new THREE.BoxGeometry(0.1,0.1,1.5);
const tracerMat=new THREE.MeshBasicMaterial({color:0xffdd77});
const _v1=new THREE.Vector3(),_v2=new THREE.Vector3();
function fireSentry(s){
  const muzzleWorld=_v1;
  s.flash.getWorldPosition(muzzleWorld);
  // lead the car
  const carPos=car.group.position;
  const dist=muzzleWorld.distanceTo(carPos);
  const lead=dist/95*0.85;
  const vx=Math.sin(car.heading)*car.speed,vz=Math.cos(car.heading)*car.speed;
  _v2.set(carPos.x+vx*lead,carPos.y+0.7,carPos.z+vz*lead).sub(muzzleWorld).normalize();
  const spread=0.02+dist*0.00018+Math.abs(car.speed)*0.0011;
  _v2.x+=rand(-spread,spread);_v2.y+=rand(-spread,spread);_v2.z+=rand(-spread,spread);
  _v2.normalize().multiplyScalar(95);
  const m=new THREE.Mesh(tracerGeo,tracerMat);
  m.position.copy(muzzleWorld);
  m.lookAt(muzzleWorld.x+_v2.x,muzzleWorld.y+_v2.y,muzzleWorld.z+_v2.z);
  scene.add(m);
  projectiles.push({mesh:m,vx:_v2.x,vy:_v2.y,vz:_v2.z,t:0});
  s.flash.material.opacity=1;
  const d=camera.position.distanceTo(muzzleWorld);
  sfxShot(clamp(0.16-d*0.0006,0.02,0.16));
}
function updateProjectiles(dt){
  const carPos=car.group.position;
  for(let i=projectiles.length-1;i>=0;i--){
    const p=projectiles[i];
    p.t+=dt;
    const ox=p.mesh.position.x,oy=p.mesh.position.y,oz=p.mesh.position.z;
    p.mesh.position.x+=p.vx*dt;
    p.mesh.position.y+=p.vy*dt;
    p.mesh.position.z+=p.vz*dt;
    let kill=p.t>2.4;
    if(car.alive&&state==='playing'){
      // swept test: closest point on the travel segment to the car (no tunneling)
      const tx=carPos.x,ty=carPos.y+0.7,tz=carPos.z;
      const sx=p.mesh.position.x-ox,sy=p.mesh.position.y-oy,sz=p.mesh.position.z-oz;
      const segLen2=sx*sx+sy*sy+sz*sz;
      let tt=0;
      if(segLen2>0)tt=clamp(((tx-ox)*sx+(ty-oy)*sy+(tz-oz)*sz)/segLen2,0,1);
      const cx=ox+sx*tt-tx,cy=oy+sy*tt-ty,cz=oz+sz*tt-tz;
      if(cx*cx+cy*cy+cz*cz<2.3){
        damageCar(2.0);
        spawnSpark(p.mesh.position);
        kill=true;
      }
    }
    if(!kill&&p.mesh.position.y<heightAt(p.mesh.position.x,p.mesh.position.z)){
      spawnSpark(p.mesh.position);
      kill=true;
    }
    if(kill){scene.remove(p.mesh);projectiles.splice(i,1);}
  }
}
function updateSentries(dt){
  const carPos=car.group.position;
  // only the two closest sentries in range get a licence to fire
  const engaged=[];
  for(const s of sentries){
    const d=Math.hypot(carPos.x-s.x,carPos.z-s.z);
    s._dist=d;
    if(d<=s.range)engaged.push(s);
  }
  engaged.sort((a,b)=>a._dist-b._dist);
  const shooters=new Set(engaged.slice(0,2));
  for(const s of sentries){
    s.flash.material.opacity=Math.max(0,s.flash.material.opacity-dt*10);
    if(state!=='playing'||!car.alive)continue;
    const dx=carPos.x-s.x,dz=carPos.z-s.z;
    const dist=s._dist;
    if(dist>s.range)continue;
    // desired yaw relative to bunker facing
    const worldYaw=Math.atan2(dx,dz);
    let relRaw=worldYaw-s.faceAngle;
    while(relRaw>Math.PI)relRaw-=Math.PI*2;
    while(relRaw<-Math.PI)relRaw+=Math.PI*2;
    const inArc=Math.abs(relRaw)<1.15;    // can't engage targets outside the slit
    const rel=clamp(relRaw,-1.1,1.1);     // limited slit traverse
    s.yaw.rotation.y+=(rel-s.yaw.rotation.y)*Math.min(1,dt*4);
    const dy=(carPos.y+0.7)-s.y;
    s.pitch.rotation.x+=(clamp(-Math.atan2(dy,dist),-0.4,0.35)-s.pitch.rotation.x)*Math.min(1,dt*4);
    const aimed=inArc&&Math.abs(rel-s.yaw.rotation.y)<0.25;
    s.cooldown-=dt;
    if(s.cooldown<=0&&aimed&&shooters.has(s)){
      if(s.burst>0){
        fireSentry(s);
        s.burst--;
        s.cooldown=0.12;
      }else{
        s.burst=4;
        s.cooldown=rand(2.2,3.8);
      }
    }
  }
}

/* ---------------- car physics ---------------- */
function updateCar(dt){
  if(!car.alive)return;
  const driving=state==='playing';
  const fwd=driving&&(keys['KeyW']||keys['ArrowUp']);
  const back=driving&&(keys['KeyS']||keys['ArrowDown']);
  const left=driving&&(keys['KeyA']||keys['ArrowLeft']);
  const right=driving&&(keys['KeyD']||keys['ArrowRight']);
  const hand=driving&&keys['Space'];

  const onRoad=roadDist(car.x,car.z)<5.2;
  const maxF=onRoad?27:19.5;
  const maxR=9;

  if(fwd)car.speed+=16*dt;
  else if(back){
    if(car.speed>0.5)car.speed-=34*dt;
    else car.speed-=10*dt;
  }else{
    car.speed-=Math.sign(car.speed)*Math.min(Math.abs(car.speed),(onRoad?5:9)*dt);
  }
  if(hand)car.speed-=Math.sign(car.speed)*Math.min(Math.abs(car.speed),30*dt);
  car.speed=clamp(car.speed,-maxR,maxF);

  const steerIn=(left?1:0)-(right?1:0);
  car.steer+=(steerIn*0.5-car.steer)*Math.min(1,dt*7);
  const yawRate=car.steer*clamp(Math.abs(car.speed)/6,0,1.9)*Math.sign(car.speed||1)*1.05;
  car.heading+=yawRate*dt;

  car.x+=Math.sin(car.heading)*car.speed*dt;
  car.z+=Math.cos(car.heading)*car.speed*dt;

  // bounds
  if(Math.abs(car.x)>146){car.x=clamp(car.x,-146,146);car.speed*=0.5;}
  if(car.z>60){car.z=60;car.speed*=0.5;}
  if(car.z<WALL_Z+8){car.z=WALL_Z+8;car.speed*=0.3;}

  // obstacle collisions
  for(const c of colliders){
    const dx=car.x-c.x,dz=car.z-c.z;
    const d2=dx*dx+dz*dz,rr=c.r+1.15;
    if(d2<rr*rr&&d2>0.0001){
      const d=Math.sqrt(d2);
      const push=(rr-d);
      car.x+=dx/d*push;
      car.z+=dz/d*push;
      const impact=Math.abs(car.speed);
      car.speed*=0.25;
      if(impact>13){damageCar((impact-13)*1.1);shake=Math.min(1,shake+0.25);sfxHit();}
    }
  }

  // mines
  for(const m of mines){
    if(m.dead)continue;
    const dx=car.x-m.x,dz=car.z-m.z;
    const trig=m.type==='AT'?2.1:1.5;
    if(dx*dx+dz*dz<trig*trig){
      m.dead=true;
      scene.remove(m.mesh);
      const pos=new THREE.Vector3(m.x,heightAt(m.x,m.z),m.z);
      if(m.type==='AT'){
        spawnExplosion(pos,1.9);
        damageCar(55,true);
        car.speed*=0.2;
      }else{
        spawnExplosion(pos,1.05);
        damageCar(22,true);
        car.speed*=0.55;
      }
    }
  }

  // slow regen when not recently hit
  if(state==='playing'&&car.hp>0&&car.hp<100&&playT-car.lastHitT>4){
    car.hp=Math.min(100,car.hp+2.2*dt);
  }

  // ground follow + orientation
  const h=heightAt(car.x,car.z);
  const sf=Math.sin(car.heading),cf=Math.cos(car.heading);
  const hF=heightAt(car.x+sf*1.5,car.z+cf*1.5);
  const hB=heightAt(car.x-sf*1.5,car.z-cf*1.5);
  const hL=heightAt(car.x+cf*0.9,car.z-sf*0.9);
  const hR=heightAt(car.x-cf*0.9,car.z+sf*0.9);
  const pitch=-Math.atan2(hF-hB,3.0);
  const roll=Math.atan2(hR-hL,1.8);
  car.visY=lerp(car.visY||h,h,Math.min(1,dt*10));
  car.visPitch=lerp(car.visPitch,pitch,Math.min(1,dt*8));
  car.visRoll=lerp(car.visRoll,roll,Math.min(1,dt*8));
  car.group.position.set(car.x,car.visY,car.z);
  car.group.rotation.y=car.heading;
  car.group.rotation.x=car.visPitch;
  car.group.rotation.z=car.visRoll;

  // wheels
  const spin=car.speed*dt/0.37;
  for(const w of car.wheelsAll)w.children[0].rotation.z-=spin;
  for(const w of car.wheelsF)w.rotation.y=car.steer*0.85;

  // tide check
  if(state==='playing'){
    const depth=waterLevel-h;
    if(depth>0.45)damageCar(26*dt,true);
    if(depth>1.0&&car.alive){
      car.alive=false;
      car.hp=0;
      endGame(false,'SWALLOWED BY THE SEA','The tide caught you. The channel keeps what it takes.');
    }
  }

  // victory
  if(state==='playing'&&car.z<WIN_Z){
    endGame(true,'YOU REACHED THE WALL','Through the mines, the wire and the guns — the gate crews are<br/>already hauling you inside. Outstanding driving.');
  }
}

/* ---------------- camera ---------------- */
const camPos=new THREE.Vector3(0,50,110);
const camLook=new THREE.Vector3(0,0,0);
function updateCamera(dt,t){
  let targetPos,targetLook;
  if(state==='menu'){
    const a=t*0.06;
    targetPos=new THREE.Vector3(Math.sin(a)*150,70+Math.sin(t*0.1)*8,-110+Math.cos(a)*150);
    targetLook=new THREE.Vector3(0,0,-110);
    camPos.lerp(targetPos,Math.min(1,dt*0.8));
    camLook.lerp(targetLook,Math.min(1,dt*0.8));
  }else{
    const sf=Math.sin(car.heading),cf=Math.cos(car.heading);
    const back=9+Math.abs(car.speed)*0.09;
    targetPos=new THREE.Vector3(car.x-sf*back,car.visY+4.2,car.z-cf*back);
    targetLook=new THREE.Vector3(car.x+sf*7,car.visY+1.3,car.z+cf*7);
    camPos.lerp(targetPos,Math.min(1,dt*4.5));
    camLook.lerp(targetLook,Math.min(1,dt*6));
  }
  camera.position.copy(camPos);
  if(shake>0){
    camera.position.x+=rand(-1,1)*shake*0.4;
    camera.position.y+=rand(-1,1)*shake*0.3;
    shake=Math.max(0,shake-dt*2.2);
  }
  camera.lookAt(camLook);
  camera.fov=lerp(camera.fov,62+Math.abs(car.speed)*0.28,dt*3);
  camera.updateProjectionMatrix();
  // shadow frustum follows the car
  const fx=state==='menu'?0:car.x,fz=state==='menu'?-110:car.z;
  sun.position.set(fx+90,150,fz+70);
  sun.target.position.set(fx,0,fz);
}

/* ---------------- HUD update ---------------- */
function updateHUD(){
  if(state==='menu')return;
  hpFill.style.width=`${clamp(car.hp,0,100)}%`;
  hpFill.style.background=car.hp>50?'linear-gradient(90deg,#7fd06a,#a7e07d)':
    car.hp>25?'linear-gradient(90deg,#e0b74a,#f0d075)':'linear-gradient(90deg,#d05545,#f07a60)';
  const d=Math.max(0,Math.round(car.z-WIN_Z));
  distEl.innerHTML=`${d} <small>m</small>`;
  const t=Math.floor(playT);
  timerEl.textContent=`${String(Math.floor(t/60)).padStart(2,'0')}:${String(t%60).padStart(2,'0')}`;
  const tk=clamp((waterLevel-TIDE_START)/(TIDE_MAX-TIDE_START),0,1);
  tideFill.style.width=`${tk*100}%`;
  tideVal.textContent=tk<0.33?'LOW':tk<0.66?'RISING':'DANGEROUS';
  speedVal.textContent=Math.round(Math.abs(car.speed)*3.6);
  const depthAhead=waterLevel-heightAt(car.x,car.z);
  warnEl.classList.toggle('on',state==='playing'&&depthAhead>-1.6);
}

/* ---------------- main loop ---------------- */
populate();
// tiny debug handle (used by automated tests; harmless in production)
window.__DDAY={car,mines,setTide:v=>{tideT=v;},getState:()=>state,getT:()=>playT,getWater:()=>waterLevel};
const clock=new THREE.Clock();
let elapsed=0;
function loop(){
  requestAnimationFrame(loop);
  const dt=Math.min(clock.getDelta(),0.05);
  elapsed+=dt;
  if(state==='playing'){
    playT+=dt;
    tideT+=dt;
    waterLevel=Math.min(TIDE_MAX,TIDE_START+tideT*TIDE_RATE);
  }
  updateWater(elapsed);
  updateCar(dt);
  updateSentries(dt);
  updateProjectiles(dt);
  updateEffects(dt);
  updateCamera(dt,elapsed);
  updateHUD();
  for(const c of clouds){
    c.position.x+=c.userData.speed*dt;
    if(c.position.x>280)c.position.x=-280;
  }
  renderer.render(scene,camera);
}
loop();
