// ═══════════════════════════════════════════════════════════════════
// main.js — tick behavior: QUEST → HUNT → BREACH → CEMENT → FEED → DROP
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';
import { Tick } from './tick.js';
import { SurfaceTracker, Gait8 } from './locomotion.js';
import { World } from './world.js';
import { Camera, HUD } from './ui.js';

const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));
const V3=(x,y,z)=>new THREE.Vector3(x,y,z);
const UP=V3(0,1,0);

const canvas=document.getElementById('scene');
const renderer=new THREE.WebGLRenderer({canvas,antialias:true});
renderer.setPixelRatio(Math.min(devicePixelRatio,2));
renderer.shadowMap.enabled=true;
renderer.shadowMap.type=THREE.PCFSoftShadowMap;
renderer.toneMapping=THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure=1.05;
function resize(){ renderer.setSize(innerWidth,innerHeight,false); cam.resize(); }
const scene=new THREE.Scene();
const world=new World(scene,renderer);
const surf=new SurfaceTracker(world);
const cam=new Camera(canvas,world);
const hud=new HUD();
addEventListener('resize',resize);

const _a=new THREE.Vector3(), _b=new THREE.Vector3(), _c=new THREE.Vector3(),
      _d=new THREE.Vector3(), _e=new THREE.Vector3(), _q=new THREE.Quaternion(),
      _m=new THREE.Matrix4(), _x=new THREE.Vector3(),
      _y=new THREE.Vector3(), _z=new THREE.Vector3();
function basisQuat(forward,up,out){
  _z.copy(forward).addScaledVector(up,-forward.dot(up)).normalize();
  _x.crossVectors(up,_z).normalize();
  _y.crossVectors(_z,_x).normalize();
  _m.makeBasis(_x,_y,_z);
  return out.setFromRotationMatrix(_m);
}
function setState(s,cls){ hud.setState(s,cls); }
function log(m){ hud.log(m); }

// ── routes ─────────────────────────────────────────────────────────
function huntRoute(cellTop){
  const DX=560, FY=205;
  return [
    {p:V3(225,2,392), n:UP.clone(), s:'HUNT'},                       // off the perch
    {p:V3(380,2,260), n:UP.clone()},
    {p:V3(540,2,140), n:UP.clone()},
    {p:V3(560,90,6),  n:V3(0,0.15,1).normalize(), s:'CLIMB'},         // bumper face
    {p:V3(560,175,6), n:V3(0,0,1)},
    {p:V3(560,FY+24,14), n:V3(0,0.5,1).normalize(), s:'BREACH'},      // duct rim
    {p:V3(560,FY+16,-50), n:UP.clone(), crouch:true},                 // tunnel (crouch)
    {p:V3(cellTop.x,FY+16,-100), n:UP.clone(), crouch:true},
    {p:V3(cellTop.x,cellTop.y+16,cellTop.z+52), n:UP.clone(), crouch:true},
  ];
}
function exitRoute(cellTop){
  const FY=205;
  return [
    {p:V3(cellTop.x,FY+16,-100), n:UP.clone(), crouch:true, s:'EXIT'},
    {p:V3(560,FY+16,-40), n:UP.clone(), crouch:true},
    {p:V3(560,FY+24,16), n:V3(0,0.5,1).normalize()},
  ];
}

// ── tick agent ─────────────────────────────────────────────────────
let AGENTS=[];
class Agent{
  constructor(name,cell,perchOffset){
    this.name=name; this.cell=cell;
    this.tick=new Tick(); scene.add(this.tick.root);
    this.gait=new Gait8(this.tick,surf);
    this.forward=V3(0.3,0,-1).normalize();
    this.up=V3(0,1,0);
    this.state='QUEST'; this.t=0; this.wpI=0; this.waypoints=[];
    this.quest=1; this.excited=0; this.crouch=0;
    this.capPitch=0; this.saw=0; this.rock=0;
    this.vel=V3(0,0,0); this.turnRate=0;
    this.dropV=V3(0,0,0);
    this.lastSurf='rock'; this.lastRough=1;
    // perch start
    const P=world.perchTop.clone().add(perchOffset);
    this.tick.root.position.copy(P).add(V3(0,26,0));
    this.tick.root.quaternion.copy(basisQuat(this.forward,this.up,_q));
    this.tick.root.updateMatrixWorld(true);
    this.gait.plantAll(this.tick.root.matrix,this.up);
  }
  support(pos,up,wp){
    const s1=surf.support(pos,up);
    if(!wp) return s1;
    const s2=surf.cast(_a.copy(pos).addScaledVector(wp.n,90),
                       _b.copy(wp.n).negate(),400);
    if(s2&&(!s1||s2.point.distanceToSquared(wp.p)<s1.point.distanceToSquared(wp.p))) return s2;
    return s1;
  }
  hunt(){
    if(this.state!=='QUEST'&&this.state!=='DONE') return;
    const ct=new THREE.Vector3(); this.cell.getWorldPosition(ct); ct.y+=31;
    this.cellTop=ct;
    this.waypoints=huntRoute(ct); this.wpI=0;
    this.state='HUNT'; this.excited=1;
    log(`${this.name}: CO₂ + heat locked — Haller's organ firing`);
  }
  dropOff(){
    if(this.state==='FEED'||this.state==='ATTACH'||this.state==='CEMENT'){
      this.state='REPLETE'; this.t=0; log(`${this.name}: withdrawing early`);
    }
  }
  holdCheck(){
    // don't crowd a cell an agent-mate is working
    for(const o of AGENTS){
      if(o===this) continue;
      if((o.state==='ATTACH'||o.state==='CEMENT'||o.state==='FEED')&&o.cellTop){
        const wp=this.waypoints[this.wpI];
        if(wp&&wp.p.distanceTo(o.cellTop)<170) return true;
      }
    }
    return false;
  }
  walk(dt,speed){
    const pos=this.tick.root.position, T=this.tick;
    // keep forward ⟂ up
    this.forward.addScaledVector(this.up,-this.forward.dot(this.up));
    if(this.forward.lengthSq()<1e-4)
      this.forward.set(0,0,-1).addScaledVector(this.up,this.up.z);
    this.forward.normalize();
    const wp=this.waypoints[this.wpI];
    const vel=_c.set(0,0,0); let moving=false;
    if(wp&&!this.holdCheck()){
      _d.copy(wp.p).sub(pos); _d.addScaledVector(this.up,-_d.dot(this.up));
      const dist=_d.length();
      if(dist<16){ this.wpI++;
        if(wp.s) setState(`${this.name} · ${wp.s}`,'fly');
        if(this.wpI>=this.waypoints.length){ this.waypoints=[]; this.onRouteEnd(); }
      } else {
        _d.normalize();
        const align=this.forward.dot(_d);
        const maxTurn=1.6*dt;
        _e.copy(this.forward).lerp(_d,clamp(maxTurn/Math.max(0.2,
          Math.acos(clamp(align,-1,1))),0,1)).normalize();
        const turned=Math.acos(clamp(this.forward.dot(_e),-1,1));
        this.forward.copy(_e);
        this.turnRate=clamp(turned/Math.max(dt,1e-3),0,2)*
          Math.sign(_d.dot(_c.crossVectors(this.up,this.forward))||0);
        const sp=speed*clamp((align+0.35)/1.35,0,1)*(dist<70?clamp(dist/70,0.25,1):1);
        vel.copy(this.forward).multiplyScalar(sp);
        pos.addScaledVector(vel,dt); moving=true;
      }
    } else if(wp&&this.holdCheck()&&(this.state==='BREACH'||this.state==='HUNT')){
      setState(`${this.name} · HOLD (cell occupied)`,'feed');
    }
    this.turnRate=damp(this.turnRate||0,moving?this.turnRate:0,6,dt);
    const s=this.support(pos,this.up,wp);
    if(s){
      this.lastSurf=s.surf; this.lastRough=s.rough;
      _d.copy(s.normal);
      if(wp){ const prox=clamp(1-pos.distanceTo(wp.p)/220,0,0.85);
        _d.lerp(wp.n,prox).normalize(); }
      this.up.lerp(_d,1-Math.exp(-dt*3)).normalize();
      const ride=this.gait.rideH;
      _e.copy(s.point).addScaledVector(this.up,ride);
      pos.lerp(_e,1-Math.exp(-dt*(moving?7:4)));
    }
    this.tick.root.quaternion.slerp(basisQuat(this.forward,this.up,_q),1-Math.exp(-dt*5));
    this.tick.root.updateMatrixWorld(true);
    return vel;
  }
  update(dt,t,speed){
    const T=this.tick;
    this.t+=dt;
    this.gait.rideH=24+T.fill*14-this.crouch*9;
    this.gait.wsScale=1-this.crouch*0.45;
    this.quest=damp(this.quest,this.state==='QUEST'?1:0,2.5,dt);
    this.excited=damp(this.excited,0,0.8,dt);
    let vel=_c.set(0,0,0);
    if(this.state==='QUEST'){
      T.body.rotation.x=damp(T.body.rotation.x,-0.42,3,dt);  // nose-up questing tilt
      this.tick.root.updateMatrixWorld(true);
      vel.set(0,0,0);
      this.gait.update(dt,this.tick.root.matrix,vel,this.up,0,this.quest+this.excited*0.8,t);
    }
    else if(this.state==='HUNT'||this.state==='CLIMB'||this.state==='ENTER'||this.state==='EXIT'){
      T.body.rotation.x=damp(T.body.rotation.x,0,3,dt);
      const wp=this.waypoints[this.wpI];
      this.crouch=damp(this.crouch,wp&&wp.crouch?1:0,2.5,dt);
      if(wp&&wp.s&&this.state!==wp.s&&this.state!=='EXIT') this.state=wp.s;
      if(this.state==='EXIT'&&!this.waypoints.length){/*handled in onRouteEnd*/}
      vel=this.walk(dt,speed*(this.state==='EXIT'?0.55:1));
      this.gait.update(dt,this.tick.root.matrix,vel,this.up,this.turnRate||0,0,t);
      T.body.position.y=Math.sin(this.gait.cycle*Math.PI*2*2)*0.5*clamp(this.gait.speed/60,0,1);
    }
    else if(this.state==='ATTACH'){
      this.crouch=damp(this.crouch,1,3,dt);
      this.capPitch=damp(this.capPitch,1.05,3,dt);
      this.saw=1;                                     // chelicerae lacerating
      this.gait.update(dt,this.tick.root.matrix,_c.set(0,0,0),this.up,0,0,t);
      if(this.t>3.5){ this.state='CEMENT'; this.t=0;
        setState(`${this.name} · CEMENT`,'feed'); hud.steps('cement');
        log(`${this.name}: hypostome in — salivary cement setting`); }
    }
    else if(this.state==='CEMENT'){
      this.saw=damp(this.saw,0.15,2,dt);
      T.hypoSlide=damp(T.hypoSlide||0,1,2,dt);
      T.setCement(damp(T.cement,1,0.8,dt));
      for(const p of T.palps) p.root.rotation.y=damp(p.root.rotation.y,p.side*0.8,2,dt);
      this.gait.update(dt,this.tick.root.matrix,_c.set(0,0,0),this.up,0,0,t);
      if(this.t>6){ this.state='FEED'; this.t=0;
        setState(`${this.name} · FEED`,'feed'); hud.steps('feed');
        log(`${this.name}: cement cone hard — siphoning pack voltage`); }
    }
    else if(this.state==='FEED'){
      T.hypoSlide=damp(T.hypoSlide||0,1,2,dt);
      const rate=0.035;
      T.setFill(clamp(T.fill+rate*dt,0,1));
      world.setBattery(world.battery-rate*dt*0.5);
      if(Math.random()<dt*6){ // arcing at the bite
        T.hypostome.updateWorldMatrix(true,false);
        _a.set(0,0,20).applyMatrix4(T.hypostome.matrixWorld);
        world.burst(_a,6,10,120);
      }
      this.gait.rideH=24+T.fill*14-this.crouch*9;   // body rises as sac balloons
      this.gait.update(dt,this.tick.root.matrix,_c.set(0,0,0),this.up,0,0,t);
      const empty=world.battery<=0.01;
      if(T.fill>=0.97||empty){ this.state='REPLETE'; this.t=0; hud.steps('replete');
        log(empty?`${this.name}: pack flat — withdrawing`
                 :`${this.name}: replete (${(T.fill*40+8).toFixed(0)}× meal) — withdrawing`); }
    }
    else if(this.state==='REPLETE'){
      // cement resists: rock side to side while backing the hypostome out
      this.rock=Math.sin(this.t*7)*0.12*Math.min(1,this.t);
      T.hypoSlide=damp(T.hypoSlide||0,0,0.9,dt);
      if(this.t>4){
        T.setCement(0); this.rock=0; this.saw=0;
        this.capPitch=0;
        for(const p of T.palps) p.root.rotation.y=p.side*0.12;
        this.waypoints=exitRoute(this.cellTop); this.wpI=0;
        this.state='EXIT'; this.t=0; setState(`${this.name} · EXIT`,'fly');
      }
      this.gait.update(dt,this.tick.root.matrix,_c.set(0,0,0),this.up,0,0,t);
    }
    else if(this.state==='DROP'){
      this.dropV.y-=4000*dt;
      const pos=T.root.position;
      pos.addScaledVector(this.dropV,dt);
      T.root.rotation.z+=dt*2; T.root.rotation.x+=dt*1.3;
      if(pos.y<=26){ pos.y=26; this.state='RECOVER'; this.t=0;
        world.burst(pos,20,30,200);
        log(`${this.name}: drop-off — thud.`);
        // re-orient flat + replant
        this.up.set(0,1,0);
        this.forward.set(this.forward.x,0,this.forward.z);
        if(this.forward.lengthSq()<1e-4) this.forward.set(0,0,1);
        this.forward.normalize();
        T.root.quaternion.copy(basisQuat(this.forward,this.up,_q));
        T.root.updateMatrixWorld(true);
        this.gait.plantAll(T.root.matrix,this.up);
      }
    }
    else if(this.state==='RECOVER'){
      this.crouch=damp(this.crouch,0,3,dt);
      this.capPitch=damp(this.capPitch,0,3,dt);
      this.gait.update(dt,this.tick.root.matrix,_c.set(0,0,0),this.up,0,0,t);
      if(this.t>1.4){ this.state='DONE'; setState(`${this.name} · DONE`,'');
        this.waypoints=[{p:V3(320,2,500),n:UP.clone()}]; this.wpI=0; this.state='EXIT'; }
    }
    // capitulum rig
    T.capitulum.rotation.x=damp(T.capitulum.rotation.x,this.capPitch,4,dt);
    T.capitulum.rotation.z=this.rock;
    if(this.saw>0.02){
      const ph=t*160;
      T.chelicerae[0].root.position.z=4+Math.sin(ph)*1.4*this.saw;
      T.chelicerae[1].root.position.z=4+Math.sin(ph+Math.PI)*1.4*this.saw;
    }
    T.hypostome.position.z=(T.hypoSlide||0)*14;
    T.updateMicro(dt,t);
  }
  onRouteEnd(){
    if(this.state==='HUNT'||this.state==='CLIMB'||this.state==='ENTER'){
      if(this.cellTop){ this.state='ATTACH'; this.t=0;
        setState(`${this.name} · ATTACH`,'feed'); hud.steps('attach');
        log(`${this.name}: chelicerae cutting — breaching cell casing`); }
    } else if(this.state==='EXIT'&&this.cellTop){
      // backed out to the duct mouth → let go
      this.cellTop=null; this.state='DROP';
      this.dropV.copy(this.forward).multiplyScalar(60); this.dropV.y=30;
      setState(`${this.name} · DROP`,'fly');
    } else if(this.state==='EXIT'&&!this.cellTop){
      this.state='QUEST'; setState(`${this.name} · QUEST`,'');
    }
  }
}

const A=new Agent('Tick A',null,V3(-30,0,10));
const B=new Agent('Tick B',null,V3(35,0,-15));
AGENTS=[A,B];
// assign cells after world build
A.cell=world.cells[0]; B.cell=world.cells[3];

// ── UI ─────────────────────────────────────────────────────────────
let crawlSpeed=42, timeScale=1, focus=A;
hud.on('cam',m=>cam.setMode(m)); cam.onMode=m=>hud.setCamMode(m);
hud.on('co2',()=>{ world.setIdle(true); A.hunt(); setTimeout(()=>B.hunt(),9000);
  hud.steps('breach'); log('host idling — CO₂ + heat plume rising'); });
hud.on('breachA',()=>{ world.setIdle(true); A.hunt(); });
hud.on('breachB',()=>{ world.setIdle(true); B.hunt(); });
hud.on('drop',()=>{ A.dropOff(); B.dropOff(); });
hud.on('reset',()=>{
  world.setBattery(1);
  for(const [ag,off] of [[A,V3(-30,0,10)],[B,V3(35,0,-15)]]){
    ag.tick.setFill(0); ag.tick.setCement(0); ag.tick.hypoSlide=0;
    ag.state='QUEST'; ag.t=0; ag.waypoints=[]; ag.cellTop=null; ag.quest=1;
    ag.up.set(0,1,0); ag.forward.set(0.3,0,-1).normalize();
    ag.tick.root.position.copy(world.perchTop).add(off).add(V3(0,26,0));
    ag.tick.root.quaternion.copy(basisQuat(ag.forward,ag.up,_q));
    ag.tick.root.updateMatrixWorld(true);
    ag.gait.plantAll(ag.tick.root.matrix,ag.up);
  }
  hud.steps(null); setState('QUEST',''); log('ticks reset — pack recharged');
});
hud.on('speed',v=>crawlSpeed=v);
hud.on('time',v=>timeScale=v);
hud.on('demo',on=>{ if(on){ hud.cb.co2(); } });

// camera targets
const focusV=new THREE.Vector3(), headV=new THREE.Vector3(), anch={};
setState('QUEST','');
log('ticks questing — press CO₂ puff when the host idles.');
resize();
cam.setMode('track',true); hud.setCamMode('track');

// ── loop ───────────────────────────────────────────────────────────
const clock=new THREE.Clock();
let fpsE=60, first=true, elapsed=0;
function frame(){
  requestAnimationFrame(frame);
  const rawDt=Math.min(0.05,clock.getDelta());
  const dt=rawDt*timeScale;
  elapsed+=dt; fpsE=lerp(fpsE,1/Math.max(rawDt,1e-4),0.05);
  const t=elapsed;
  A.update(dt,t,crawlSpeed);
  B.update(dt+0.0001,t+40,crawlSpeed*0.92);
  focus=(A.state==='DONE'||A.state==='QUEST')&&B.state!=='QUEST'?B:A;
  world.update(dt,t);
  world.followShadow(focus.tick.root.position);
  focusV.copy(focus.tick.root.position); focusV.y+=20;
  focus.tick.capitulum.updateWorldMatrix(true,false);
  headV.set(0,0,30).applyMatrix4(focus.tick.capitulum.matrixWorld);
  cam.update(rawDt,focusV,headV);
  // HUD
  hud.chips(focus.gait.gaitName(),`${(focus.tick.cement*100).toFixed(0)}%`,
    world.battery>0.05?'HOST LIVE':'HOST FLAT',fpsE);
  hud.gauges(A.tick.fill,B.tick.fill,world.battery);
  hud.tele({
    speed:focus.gait.speed,
    alt:focus.tick.root.position.y, surf:focus.lastSurf,
    grip:focus.lastRough>0.6?'claws':focus.lastRough>0.3?'claws+pad':'pad',
    duty:focus.gait.duty.toFixed(2),
    phase:`${focus.name.split(' ')[1]}·${focus.state}`,
  });
  hud.projectLabels(cam.cam,focus.tick.anchors(anch));
  renderer.render(scene,cam.cam);
  if(first){ first=false; document.getElementById('loader').classList.add('done'); }
}
frame();
