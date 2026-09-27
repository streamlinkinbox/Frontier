// ═══════════════════════════════════════════════════════════════════
// main.js — behavior brain: perch / walk / fly / feed + render loop
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';
import { Mosquito } from './mosquito.js';
import { SurfaceTracker, Gait, WingBeat, Flight } from './locomotion.js';
import { World } from './world.js';
import { Camera, HUD } from './ui.js';

const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));
const V3=(x,y,z)=>new THREE.Vector3(x,y,z);
const RIDE=2.45;                       // body height over surface (mm)
const UP=V3(0,1,0);

// ── boot ───────────────────────────────────────────────────────────
const canvas=document.getElementById('scene');
const renderer=new THREE.WebGLRenderer({canvas, antialias:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.shadowMap.enabled=true;
renderer.shadowMap.type=THREE.PCFSoftShadowMap;
renderer.toneMapping=THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure=1.05;
function resize(){
  renderer.setSize(innerWidth,innerHeight,false);
  cam.resize();
}
const scene=new THREE.Scene();
const world=new World(scene, renderer);
const mosq=new Mosquito();
scene.add(mosq.root);
const surf=new SurfaceTracker(world);
const gait=new Gait(mosq, surf);
const wings=new WingBeat(mosq);
const flight=new Flight(mosq, surf);
const cam=new Camera(canvas, world);
const hud=new HUD();
wings.blurGain=0.7;
addEventListener('resize',resize);

// ── behavior state ─────────────────────────────────────────────────
const B={
  mode:'ground',            // 'ground' | 'air'
  state:'PERCHED',
  waypoints:[], wpI:0,
  walkSpeed:9.6, cruise:230,
  heading:2.5, up:V3(0,1,0),
  forward:V3(Math.sin(2.5),0,Math.cos(2.5)),  // surface-frame heading (wall-safe)
  timeScale:1, spin:0, spinGoal:0,
  airTarget:V3(0,30,0), airSpeed:230, arriveR:7,
  landing:null, takeoffT:-1,
  feed:null,                // {kind:'fuel'|'blood', sub:'seek'|..., t:0}
  demo:false, demoI:0,
  lastSurf:'deck', lastRough:0.5, alt:0,
  droplets:[],
};
function setState(s, cls){ B.state=s; hud.setState(s,cls); }
function log(m){ hud.log(m); }

// pose goals (all damped → nothing ever snaps)
const pose={
  headX:0, headY:0, probX:0.62, fasc:0, buckle:0,
  abCurl:0, sawT:0,
};

// ── ground helpers ─────────────────────────────────────────────────
function groundSupport(pos, up, wp){
  const s1=surf.support(pos,up);
  if(!wp) return s1;
  // transition-aware: test the waypoint's surface too, keep whichever
  // support point lies closest to the waypoint (deck→wall→ceiling…)
  const s2=surf.cast(_a.copy(pos).addScaledVector(wp.n,9), _b.copy(wp.n).negate(), 40);
  if(s2&&(!s1||s2.point.distanceToSquared(wp.p)<s1.point.distanceToSquared(wp.p))) return s2;
  return s1;
}
const _a=new THREE.Vector3(), _b=new THREE.Vector3(), _c=new THREE.Vector3(),
      _d=new THREE.Vector3(), _e=new THREE.Vector3(), _q=new THREE.Quaternion(),
      _m=new THREE.Matrix4(), _x=new THREE.Vector3(), _y=new THREE.Vector3(), _z=new THREE.Vector3();

function basisQuat(forward, up, out){
  _z.copy(forward).addScaledVector(up,-forward.dot(up)).normalize();
  _x.crossVectors(up,_z).normalize();
  _y.crossVectors(_z,_x).normalize();
  _m.makeBasis(_x,_y,_z);
  return out.setFromRotationMatrix(_m);
}

// ── commands ───────────────────────────────────────────────────────
function cmdTakeoff(hoverH=26){
  if(B.mode==='air') return;
  B.takeoffT=0; setState('TAKEOFF','fly'); log('wings spooling up — legs extend for stealth push-off');
  B.spinGoal=1;
  B.afterTakeoff=V3(mosq.root.position.x, Math.max(hoverH,mosq.root.position.y+18), mosq.root.position.z);
}
function cmdFlyTo(p, speed, arriveR=7){
  B.airTarget.copy(p); B.airSpeed=speed??B.cruise; B.arriveR=arriveR;
}
function cmdLand(point, normal, fwdHint, then){
  B.landing={stage:0, point:point.clone(), normal:normal.clone(),
    fwd:fwdHint?fwdHint.clone():B.forward.clone(), then, t:0};
  if(B.mode==='ground'){ // already down — just re-orient/plant
    finishLanding();
  } else {
    setState('LANDING','fly'); log('landing — flare, gear down');
  }
}
function finishLanding(){
  const L=B.landing;
  B.mode='ground';
  B.spinGoal=0;
  if(L){
    B.up.copy(L.normal);
    // forward hint projected into surface plane (degenerate-safe)
    _c.copy(L.fwd).addScaledVector(B.up,-L.fwd.dot(B.up));
    if(_c.lengthSq()>1e-4) B.forward.copy(_c.normalize());
    else { // hint parallel to normal (e.g. straight at a wall): pick any in-plane dir
      _c.set(0,1,0).addScaledVector(B.up,-B.up.y);
      if(_c.lengthSq()<1e-4) _c.set(1,0,0).addScaledVector(B.up,-B.up.x);
      B.forward.copy(_c.normalize());
    }
    B.heading=Math.atan2(B.forward.x,B.forward.z);
    mosq.root.position.copy(L.point).addScaledVector(L.normal,RIDE);
  }
  mosq.root.quaternion.copy(basisQuat(B.forward,B.up,_q));
  mosq.root.updateMatrixWorld(true);
  gait.plantAll(mosq.root.matrix, B.up);
  const wasFeed=B.feed&&B.feed.sub==='seek';
  const then=L&&L.then; B.landing=null;
  if(wasFeed){ B.feed.sub='probe'; B.feed.t=0; setState('FEED · PROBE','feed'); hud.feedSteps('probe'); log('tarsi taste surface — labella contact'); }
  else { setState(B.waypoints.length?'WALKING':'PERCHED'); }
  if(then) then();
}
function cmdWalk(route){
  const doWalk=()=>{
    B.waypoints=route; B.wpI=0;
    if(B.mode==='air'){ // land at route start first
      const s0=route[0];
      cmdLand(s0.p, s0.n, undefined, ()=>{});
      B.pendingWalk=true;
    } else setState('WALKING');
  };
  doWalk();
}
function cmdFeed(kind){
  const tgt=kind==='fuel'?world.feedFuel:world.feedBlood;
  mosq.setFill(mosq.fill, kind); // fluid type for incoming meal
  B.feed={kind, sub:'seek', t:0, target:tgt, rate: kind==='fuel'?0.045:0.055};
  hud.feedSteps('seek');
  if(B.mode==='ground'&&mosq.root.position.distanceTo(tgt.point)<30){
    // close enough: walk the last stretch? simpler — short hop flight
  }
  // fly approach then land on the port/dish
  if(B.mode==='ground') cmdTakeoff(20);
  setState('FEED · SEEK','feed');
  log(kind==='fuel'?'fuel vapour located — inbound to tank port':'heat + odour plume located — inbound');
  B.feedApproach=true;
}
function cmdEscape(){
  B.feed=null; hud.feedSteps(null);
  if(B.mode==='ground') cmdTakeoff(30);
  const p=mosq.root.position;
  cmdFlyTo(V3(p.x*0.3, 42, p.z*0.3+20), 320);
  setState('ESCAPE','fly'); log('escape flight — wing-loading compensated for meal');
}
function cmdWithdraw(){ if(B.feed&&B.feed.sub!=='replete'){ B.feed.sub='replete'; B.feed.t=0; } }

// route builders
function deckRoute(n=4){
  const r=[]; let guard=0;
  while(r.length<n&&guard++<60){
    const x=(Math.random()-0.5)*56, z=(Math.random()-0.5)*40;
    if(Math.hypot(x-20,z+9)<17) continue;    // keep out of the tank
    if(Math.hypot(x+17,z-11)<11) continue;   // …and the dish
    if(Math.abs(x)>30&&z<-10) continue;      // …and support columns
    r.push({p:V3(x,RIDE,z), n:UP.clone()});
  }
  return r;
}
function rockRoute(){
  const pts=[[30,10],[44,18],[34,32],[8,36],[-20,36],[-36,33]];
  return pts.map(([x,z])=>({p:V3(x,world.groundH(x,z)+RIDE,z), n:UP.clone()}));
}
function climbRoute(){
  return [
    {p:V3(0,RIDE,-20), n:V3(0,1,0)},
    {p:V3(0,RIDE,-30), n:V3(0,1,0)},
    {p:V3(0,6,-36.3), n:V3(0,0.25,1).normalize()},
    {p:V3(4,26,-36.3), n:V3(0,0,1)},
    {p:V3(6,48,-36.3), n:V3(0,0,1)},
    {p:V3(6,54.3,-30), n:V3(0,-0.4,0.9).normalize()},
    {p:V3(-2,54.3,-24), n:V3(0,-1,0)},
    {p:V3(-14,54.3,-24), n:V3(0,-1,0)},
  ];
}
function ceilingAttach(){
  return [
    {p:V3(-6,54.3,-26), n:V3(0,-1,0)},
    {p:V3(8,54.3,-22), n:V3(0,-1,0)},
    {p:V3(14,54.3,-28), n:V3(0,-1,0)},
  ];
}

// ── UI wiring ──────────────────────────────────────────────────────
hud.on('cam',m=>cam.setMode(m)); cam.onMode=m=>hud.setCamMode(m);
hud.on('takeoff',()=>{ B.feed=null; hud.feedSteps(null); B.waypoints=[]; cmdTakeoff(30);
  cmdFlyTo(V3(mosq.root.position.x*0.5,34,mosq.root.position.z*0.5),260); });
hud.on('land',()=>{ // land on whatever is below
  const p=mosq.root.position;
  const s=surf.support(p,UP)||surf.cast(p,V3(0,-1,0),120);
  if(s){ B.feed=null; hud.feedSteps(null); B.waypoints=[]; cmdLand(s.point,s.normal,undefined,()=>{}); }
  else log('no surface in reach');
});
hud.on('walkDeck',()=>{ B.feed=null; hud.feedSteps(null); cmdWalk(deckRoute(5)); log('walking — gait continuum engaged'); });
hud.on('walkRock',()=>{ B.feed=null; hud.feedSteps(null); cmdWalk(rockRoute()); log('rough-terrain traverse — claws out'); });
hud.on('climb',()=>{ B.feed=null; hud.feedSteps(null);
  if(B.mode==='air'){ log('land first — routing to deck'); const r=climbRoute();
    cmdLand(r[0].p,r[0].n,undefined,()=>{ B.waypoints=r; B.wpI=1; setState('WALKING'); });
  } else { B.waypoints=climbRoute(); B.wpI=0; setState('WALKING'); }
  log('vertical ascent — pulvilli + claws sharing load'); });
hud.on('ceiling',()=>{ B.feed=null; hud.feedSteps(null);
  const r=ceilingAttach();
  const attach=()=>cmdLand(r[0].p,r[0].n,V3(0,0,-1),()=>{ B.waypoints=r; B.wpI=1; setState('WALKING'); });
  if(B.mode==='air') attach(); else { cmdTakeoff(40); B.afterLand=attach; }
  log('inverted attach — adhesive pads engaged'); });
hud.on('feedFuel',()=>cmdFeed('fuel'));
hud.on('feedBlood',()=>cmdFeed('blood'));
hud.on('withdraw',cmdWithdraw);
hud.on('escape',cmdEscape);
hud.on('reset',()=>{
  mosq.setFill(0,'blood'); world.setTank(1); world.setDish(1);
  B.feed=null; hud.feedSteps(null); B.waypoints=[];
  mosq.root.position.set(-2,RIDE,14); B.heading=2.5; B.up.set(0,1,0);
  B.forward.set(Math.sin(2.5),0,Math.cos(2.5));
  B.mode='ground'; B.spinGoal=0; B.landing=null; B.takeoffT=-1;
  B.feedApproach=false; B.afterTakeoff=null; B.afterLand=null; B.pendingWalk=false;
  mosq.root.quaternion.copy(basisQuat(B.forward,B.up,_q));
  mosq.root.updateMatrixWorld(true); gait.plantAll(mosq.root.matrix,B.up);
  setState('PERCHED'); log('specimen reset — reservoirs refilled');
});
hud.on('speed',v=>B.walkSpeed=v);
hud.on('time',v=>B.timeScale=v);
hud.on('blur',v=>wings.blurGain=v);
hud.on('demo',on=>{ B.demo=on; B.demoI=0; B.demoT=0; log(on?'auto demo engaged':'auto demo off'); });

// wingtip trails (slow-mo only)
const trails={};
for(const k of ['L','R']){
  const g=new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(60*3),3));
  const l=new THREE.Line(g, new THREE.LineBasicMaterial({color:0x6f97ff, transparent:true, opacity:0.65}));
  l.frustumCulled=false; l.visible=false; scene.add(l);
  trails[k]={line:l, pts:[]};
}

// ── droplets (diuresis) ────────────────────────────────────────────
const dropG=new THREE.SphereGeometry(0.16,10,8);
const dropM=new THREE.MeshPhysicalMaterial({color:0xffc890, transparent:true, opacity:0.75,
  roughness:0.05, clearcoat:1});
function spawnDrop(pos, vel){
  const m=new THREE.Mesh(dropG,dropM.clone());
  m.position.copy(pos); scene.add(m);
  B.droplets.push({m, v:vel.clone(), life:3});
}
function updateDrops(dt){
  for(let i=B.droplets.length-1;i>=0;i--){
    const d=B.droplets[i];
    d.v.y-=60*dt; d.m.position.addScaledVector(d.v,dt); d.life-=dt;
    d.m.material.opacity=Math.min(0.75,d.life*0.5);
    if(d.life<=0){ scene.remove(d.m); d.m.material.dispose(); B.droplets.splice(i,1); }
  }
}

// ── feeding sequence ───────────────────────────────────────────────
function updateFeed(dt){
  const F=B.feed; if(!F) return;
  F.t+=dt;
  const headTgt={x:0.55, y:0};
  if(F.sub==='seek'){
    // approach flight handled in air update; nothing here
    return;
  }
  if(F.sub!=='probe') pose.probYaw=damp(pose.probYaw||0,0,6,dt);
  if(F.sub==='probe'){
    pose.probX=1.08; headTgt.x=0.42;
    pose.probYaw=Math.sin(F.t*19)*0.05;
    if(F.t>2.2){ F.sub='insert'; F.t=0; hud.feedSteps('insert');
      log('fascicle penetrating — labium buckling (maxillae sawing)'); }
  } else if(F.sub==='insert'){
    pose.probX=1.15; headTgt.x=0.35;
    pose.fasc=damp(pose.fasc,1,1.6,dt);
    pose.buckle=damp(pose.buckle,1,1.4,dt);
    pose.sawT+=dt;
    if(F.t>3.2){ F.sub='salivate'; F.t=0; hud.feedSteps('salivate');
      log('saliva injected — anticoagulant + anaesthetic'); }
  } else if(F.sub==='salivate'){
    pose.sawT+=dt*0.3;
    if(F.t>2.0){ F.sub='engorge'; F.t=0; hud.feedSteps('engorge');
      log('vessel found — cibarial pump running'); }
  } else if(F.sub==='engorge'){
    // pump pulse on head
    headTgt.x=0.35+Math.sin(F.t*19)*0.012;
    // engorge → drain reservoir
    const rate=F.rate;
    const nf=clamp(mosq.fill+rate*dt,0,1);
    mosq.setFill(nf, F.kind);
    if(F.kind==='fuel') world.setTank(world.tankLevel-rate*dt*0.62);
    else world.setDish(world.dishLevel-rate*dt*0.5);
    // diuresis droplets
    if(mosq.fill>0.6&&Math.random()<dt*0.5){
      mosq.cerci.updateWorldMatrix(true,false);
      _a.set(0,0,-0.1).applyMatrix4(mosq.cerci.matrixWorld);
      spawnDrop(_a, _b.set((Math.random()-0.5)*4,-2,(Math.random()-0.5)*4));
    }
    const empty=(F.kind==='fuel'?world.tankLevel:world.dishLevel)<=0.01;
    if(nf>=0.97||empty){ F.sub='replete'; F.t=0; hud.feedSteps('replete');
      log(empty?'reservoir dry — withdrawing':'replete — stretch receptors firing, withdrawing'); }
  } else if(F.sub==='replete'){
    pose.fasc=damp(pose.fasc,0,1.8,dt);
    pose.buckle=damp(pose.buckle,0,1.8,dt);
    pose.probX=0.7; headTgt.x=0.1;
    if(F.t>2.4){
      B.feed=null; hud.feedSteps(null);
      log('withdrawn — escape!');
      cmdEscape();
    }
  }
  // apply head/proboscis pose
  mosq.head.rotation.x=damp(mosq.head.rotation.x,headTgt.x,5,dt);
  mosq.head.rotation.y=damp(mosq.head.rotation.y,headTgt.y+(pose.probYaw||0)*0.3,6,dt);
  pose.probYaw=pose.probYaw||0;
}
function applyMouthparts(dt,t){
  const PB=mosq.proboscisBase;
  PB.rotation.x=damp(PB.rotation.x,pose.probX,5,dt);
  PB.rotation.y=damp(PB.rotation.y,pose.probYaw||0,8,dt);
  // labium buckle: smooth arc (each segment pitches, tip stays near surface)
  const bk=pose.buckle;
  mosq.labium.forEach((g,i)=>{
    const prof=Math.sin((i+0.5)/8*Math.PI);       // arc profile, peak mid-shaft
    g.rotation.x=damp(g.rotation.x, -0.34*bk*prof - 0.06*bk, 6, dt);
    g.rotation.y=damp(g.rotation.y, 0.10*bk*prof, 6, dt);  // slight lateral loop
  });
  // fascicle slide + micro-saw vibration
  mosq.fascicleSlide=damp(mosq.fascicleSlide||0, pose.fasc, 4, dt);
  const sl=mosq.fascicleSlide;
  mosq.fascicle.position.z=sl*1.55-0.25;
  const sawing=(B.feed&&(B.feed.sub==='insert'||B.feed.sub==='salivate'))?1:0;
  mosq.fascicle.position.x=Math.sin(t*250)*0.006*sawing;
  mosq.fascicle.rotation.z=Math.sin(t*250)*0.02*sawing;
  // labella squash when probing/feeding
  const sq=(B.feed&&B.feed.sub!=='seek')?1:0;
  mosq.labella.scale.set(1+0.3*sq,1-0.25*sq,1+0.2*sq);
}

// ── ground update ──────────────────────────────────────────────────
function updateGround(dt){
  const pos=mosq.root.position;
  let vel=_c.set(0,0,0), moving=false;
  const wp=B.waypoints[B.wpI];
  // keep forward ⟂ up at all times (surface-frame heading — works on walls/ceilings)
  B.forward.addScaledVector(B.up,-B.forward.dot(B.up));
  if(B.forward.lengthSq()<1e-4) B.forward.set(0,0,-1).addScaledVector(B.up,B.up.z);
  B.forward.normalize();
  if(wp){
    _d.copy(wp.p).sub(pos); _d.addScaledVector(B.up,-_d.dot(B.up)); // on-plane
    const dist=_d.length();
    if(dist<2.2){ B.wpI++;
      if(B.wpI>=B.waypoints.length){ B.waypoints=[];
        if(B.pendingWalk){B.pendingWalk=false;}
        if(!B.feed) setState('PERCHED'); else setState('FEED · PROBE','feed');
        log('waypoint reached — holding stance');
      }
    } else {
      _d.normalize();
      // steer forward toward desired with limited turn rate (smooth arcs)
      const align=B.forward.dot(_d);
      const maxTurn=2.0*dt;
      _e.copy(B.forward).lerp(_d,clamp(maxTurn/Math.max(0.2,Math.acos(clamp(align,-1,1))),0,1)).normalize();
      const turned=Math.acos(clamp(B.forward.dot(_e),-1,1));
      B.forward.copy(_e);
      const yr=clamp(turned/Math.max(dt,1e-3),0,2.5)*Math.sign(_d.dot(_c.crossVectors(B.up,B.forward))||0);
      // slow when facing away (turn-in-place), ease near arrival
      const sp=B.walkSpeed*clamp((align+0.35)/1.35,0,1)*(dist<8?clamp(dist/8,0.25,1):1);
      vel.copy(B.forward).multiplyScalar(sp);
      pos.addScaledVector(vel,dt);
      moving=true;
      B.turnRate=yr;
    }
  }
  B.turnRate=damp(B.turnRate||0, moving?B.turnRate:0, 6, dt);
  // surface follow
  const wpN=wp?wp.n:null;
  const s=groundSupport(pos,B.up,wp);
  if(s){
    B.alt=s.dist-RIDE;
    B.lastSurf=s.surf; B.lastRough=s.rough;
    // normal blend: surface + waypoint hint when close
    _d.copy(s.normal);
    if(wpN&&wp){ const prox=clamp(1-pos.distanceTo(wp.p)/22,0,0.85); _d.lerp(wpN,prox).normalize(); }
    const k=1-Math.exp(-dt*3.2);
    B.up.lerp(_d,k).normalize();
    _e.copy(s.point).addScaledVector(B.up,RIDE);
    pos.lerp(_e,1-Math.exp(-dt*(moving?7:4)));
  }
  // orientation from surface frame
  mosq.root.quaternion.slerp(basisQuat(B.forward,B.up,_q),1-Math.exp(-dt*5));
  mosq.root.updateMatrixWorld(true);
  // gait
  const g=gait.update(dt, mosq.root.matrix, vel, B.up, B.turnRate||0);
  B.gaitInfo=g;
  // subtle body bob handled inside gait visuals via ride height? add micro:
  mosq.body.position.y=Math.sin(gait.cycle*Math.PI*2*2)*0.028*clamp(gait.speed/15,0,1);
  mosq.body.rotation.z=Math.sin(gait.cycle*Math.PI*2)*0.012*clamp(gait.speed/15,0,1);
  // abdomen posture on ground: level, slight curl by fill
  const curl=-(0.02+0.05*mosq.fill);
  mosq.abSegs.forEach((sg,i)=>{ sg.g.rotation.x=damp(sg.g.rotation.x,curl*(i?0.7:0.3),4,dt); });
  // head neutral on ground (unless feeding)
  if(!B.feed){
    mosq.head.rotation.x=damp(mosq.head.rotation.x,0.08,4,dt);
    mosq.head.rotation.y=damp(mosq.head.rotation.y,0,4,dt);
    pose.probX=0.62; pose.fasc=damp(pose.fasc,0,4,dt); pose.buckle=damp(pose.buckle,0,4,dt);
  }
  mosq.gripSmooth=damp(mosq.gripSmooth, gait.grip??0.4, 4, dt);
  return g;
}

// ── air update ─────────────────────────────────────────────────────
function updateAir(dt){
  const pos=mosq.root.position;
  // feed approach routing
  if(B.feed&&B.feed.sub==='seek'&&B.feedApproach){
    const T=B.feed.target;
    _a.copy(T.point).addScaledVector(T.normal,24); _a.y+=4;
    if(pos.distanceTo(_a)>5){ cmdFlyTo(_a,260,5); }
    else {
      B.feedApproach=false;
      // face along the surface: flat → keep heading; vertical → in-plane horizontal
      let fwd;
      if(T.normal.y>0.7) fwd=B.forward.clone();
      else { fwd=new THREE.Vector3().crossVectors(T.normal,UP);
        if(fwd.lengthSq()<1e-4) fwd.set(0,0,-1); fwd.normalize(); }
      cmdLand(T.point,T.normal,fwd,()=>{});
    }
  }
  // after-takeoff hook (ceiling attach)
  if(B.afterTakeoff&&B.takeoffT<0){
    cmdFlyTo(B.afterTakeoff,200,4);
    if(pos.distanceTo(B.afterTakeoff)<5){ B.afterTakeoff=null;
      if(B.afterLand){ const f=B.afterLand; B.afterLand=null; f(); } }
  }
  // final approach owns position+attitude (flight controller stands down)
  const landingFinal=B.landing&&B.landing.stage===1;
  const r=landingFinal?{dist:99,speed:flight.vel.length()}
    :flight.update(dt,B.airTarget,B.airSpeed,UP,B.arriveR);
  if(landingFinal) flight.vel.multiplyScalar(Math.max(0,1-dt*3));
  B.alt=(surf.cast(pos,_c.set(0,-1,0),220)?.dist??99);
  B.lastSurf='airborne';
  // flight mouthparts: proboscis tucked forward, fascicle sheathed
  pose.probX=0.34; pose.fasc=damp(pose.fasc,0,4,dt); pose.buckle=damp(pose.buckle,0,4,dt);
  pose.probYaw=damp(pose.probYaw||0,0,6,dt);
  // arrival → hold + state
  if(r.dist<2&&B.state==='TAKEOFF'){ setState('CRUISE','fly'); }
  if(B.landing) updateLanding(dt);
  mosq.gripSmooth=damp(mosq.gripSmooth,0.3,3,dt);
  return r;
}
function updateLanding(dt){
  const L=B.landing; L.t+=dt;
  const pos=mosq.root.position;
  if(L.stage===0){
    _a.copy(L.point).addScaledVector(L.normal,20); _a.y+=5;
    cmdFlyTo(_a,170,4);
    if(pos.distanceTo(_a)<5){ L.stage=1; log('final — legs reaching, wings flaring'); }
  } else {
    // translate to contact while aligning up to normal + forward to hint
    _b.copy(L.point).addScaledVector(L.normal,RIDE);
    pos.lerp(_b,1-Math.exp(-dt*3.2));
    B.up.lerp(L.normal,1-Math.exp(-dt*2.6)).normalize();
    _c.copy(L.fwd).addScaledVector(B.up,-L.fwd.dot(B.up));
    if(_c.lengthSq()>1e-4){ _c.normalize(); B.forward.lerp(_c,1-Math.exp(-dt*2.5)).normalize(); }
    mosq.root.quaternion.slerp(basisQuat(B.forward,B.up,_q),1-Math.exp(-dt*3));
    // extend legs (airBlend fake: trail targets lower)
    if(pos.distanceTo(_b)<0.9) finishLanding();
    if(L.t>14){ log('landing aborted — going around'); B.landing=null; cmdFlyTo(_a.set(pos.x,40,pos.z),260); }
  }
}
function updateTakeoff(dt){
  B.takeoffT+=dt;
  const T=B.takeoffT;
  if(B.mode==='air') return;
  if(T<0.55){
    // spool + crouch (wings already spinning via spinGoal)
    const pos=mosq.root.position;
    const s=surf.support(pos,B.up);
    if(s){ _a.copy(s.point).addScaledVector(B.up,RIDE-0.35*Math.sin(T/0.55*Math.PI)); pos.lerp(_a,1-Math.exp(-dt*6)); }
    mosq.root.updateMatrixWorld(true);
    gait.update(dt,mosq.root.matrix,_b.set(0,0,0),B.up,0);
  } else if(T<1.0){
    // leg extension: rise while feet planted
    const pos=mosq.root.position;
    pos.addScaledVector(B.up,dt*3.2);
    mosq.root.updateMatrixWorld(true);
    gait.update(dt,mosq.root.matrix,_b.set(0,0,0),B.up,0);
  } else {
    // liftoff — wings carry ≥60% (Muijres 2017)
    B.mode='air'; B.takeoffT=-1;
    flight.vel.copy(B.up).multiplyScalar(55);
    if(Math.hypot(B.forward.x,B.forward.z)>0.3)
      flight.yaw=Math.atan2(B.forward.x,B.forward.z);
    setState('TAKEOFF','fly'); log('liftoff — airborne, gear trailing');
  }
}

// ── demo script ────────────────────────────────────────────────────
const demoSteps=[
  ()=>{ log('DEMO · takeoff'); B.feed=null; hud.feedSteps(null); B.waypoints=[]; cmdTakeoff(30); cmdFlyTo(V3(0,34,10),260); },
  ()=>{ cmdFlyTo(V3(-30,30,24),260); },
  ()=>{ const r=climbRoute(); cmdLand(r[0].p,r[0].n,V3(0,0,-1),()=>{ B.waypoints=r; B.wpI=1; setState('WALKING'); }); },
  ()=>{ if(!B.waypoints.length){ B.demoI++; B.demoT=0; cmdFeed('fuel'); } },
  ()=>{ if(!B.feed){ B.demoI++; B.demoT=0; } },   // wait for feed+escape
  ()=>{ cmdFeed('blood'); },
  ()=>{ if(!B.feed){ B.demoI++; B.demoT=0; B.demoI=0; log('DEMO · loop'); } },
];
let demoWait=[6,6,26,4,4,2,4];   // wait-steps poll every 4 s until their condition clears

// ── init pose ──────────────────────────────────────────────────────
mosq.root.position.set(-2,RIDE,14);
mosq.root.quaternion.copy(basisQuat(B.forward,B.up,_q));
mosq.root.updateMatrixWorld(true);
gait.plantAll(mosq.root.matrix,B.up);
setState('PERCHED');
log('specimen awake — perched on deck. Pick a behavior.');
resize();
cam.setMode('track',true); hud.setCamMode('track');

// ── main loop ──────────────────────────────────────────────────────
const clock=new THREE.Clock();
let fpsE=60, first=true, elapsed=0;
const focusV=new THREE.Vector3(), headV=new THREE.Vector3(), anch={};

function frame(){
  requestAnimationFrame(frame);
  const rawDt=Math.min(0.05,clock.getDelta());
  const dt=rawDt*B.timeScale;
  elapsed+=dt; fpsE=lerp(fpsE,1/Math.max(rawDt,1e-4),0.05);
  const t=elapsed;

  // demo driver
  if(B.demo){
    B.demoT=(B.demoT||0)+rawDt;
    if(B.demoT>=(demoWait[B.demoI]||8)){ B.demoT=0; demoSteps[B.demoI]?.(); 
      if(B.demoI!==3&&B.demoI!==4&&B.demoI!==6) B.demoI=(B.demoI+1)%demoSteps.length; }
  }
  // behavior
  if(B.takeoffT>=0) updateTakeoff(dt);
  else if(B.mode==='ground') updateGround(dt);
  else updateAir(dt);
  if(B.feed) updateFeed(dt);
  applyMouthparts(dt,t);
  // wings
  B.spin=damp(B.spin,B.spinGoal,B.spinGoal>B.spin?7:1.6,rawDt);
  mosq.wingSpin=B.spin;
  if(B.mode==='air'){ wings.diff=damp(wings.diff,clamp((flight.bank||0)*1.2,-1,1),4,rawDt); }
  else wings.diff=damp(wings.diff,0,4,rawDt);
  const wbf=wings.update(dt,B.spin,mosq.fill,B.timeScale);
  // life + world
  mosq.updateMicro(dt,t);
  world.update(dt,t);
  world.followShadow(mosq.root.position);
  updateDrops(Math.max(dt,rawDt*0.02));
  // camera
  focusV.copy(mosq.root.position); focusV.y+=1;
  mosq.head.updateWorldMatrix(true,false);
  headV.set(0,0,0.6).applyMatrix4(mosq.head.matrixWorld);
  cam.update(rawDt,focusV,headV);
  // trails
  for(const k of ['L','R']){
    const tr=trails[k], show=hud.trailsOn&&B.timeScale<0.2&&B.spin>0.5;
    tr.line.visible=show;
    if(show){
      mosq.wings[k].pitch.updateWorldMatrix(true,false);
      _a.set(mosq.wings[k].side*2.9,0,0).applyMatrix4(mosq.wings[k].pitch.matrixWorld);
      tr.pts.push(_a.x,_a.y,_a.z);
      while(tr.pts.length>60*3) tr.pts.splice(0,3);
      const attr=tr.line.geometry.attributes.position;
      for(let i=0;i<60;i++){
        const j=Math.min(tr.pts.length-3, i*3);
        attr.setXYZ(i, tr.pts[j]??_a.x, tr.pts[j+1]??_a.y, tr.pts[j+2]??_a.z);
      }
      attr.needsUpdate=true;
    } else tr.pts.length=0;
  }
  // HUD
  const strokeDeg=THREE.MathUtils.radToDeg(wings.strokeAmp+12*D2R*mosq.fill)*B.spin;
  hud.chips(wbf*B.spin+(1-B.spin)*0, B.spin>0.02?strokeDeg:0,
    B.mode==='ground'?gait.gaitName():'flight', fpsE);
  hud.gauges(mosq.fill,mosq.fluid,world.tankLevel,world.dishLevel);
  const gripTxt=B.mode==='ground'
    ? (B.lastRough>0.6?'claws':B.lastRough>0.3?'claws+pulvilli':'pulvilli')
    : '—';
  hud.tele({
    speed:B.mode==='ground'?gait.speed:flight.vel.length(),
    alt:Math.max(0,B.alt),
    surf:B.lastSurf, grip:gripTxt,
    duty:B.mode==='ground'?gait.duty.toFixed(2):'—',
    phase:B.feed?B.feed.sub.toUpperCase():(B.mode==='ground'?(gait.cycle*6).toFixed(1)+'/6':'—'),
  });
  hud.projectLabels(cam.cam, mosq.anchors(anch));
  hud.audioUpdate(wbf,B.spin,cam.dist);
  renderer.render(scene,cam.cam);
  if(first){ first=false; document.getElementById('loader').classList.add('done'); }
}
const D2R=Math.PI/180;
frame();
