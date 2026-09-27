// ═══════════════════════════════════════════════════════════════════
// locomotion.js — research-driven movement
// · Hexapod gait continuum (wave→tetrapod→tripod, Cruse/Walknet)
// · Analytic leg IK with joint limits (never snaps, never crosses)
// · Mosquito wing kinematics: 40° stroke, 550Hz+, figure-8 deviation
// · Flight: stealth takeoff, banked cruise, flared landing
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

// ── math ───────────────────────────────────────────────────────────
const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const D2R=Math.PI/180;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));
const dampAngle=(a,b,l,dt)=>{
  let d=(b-a)%(Math.PI*2);
  if(d>Math.PI)d-=Math.PI*2; if(d<-Math.PI)d+=Math.PI*2;
  return a+d*(1-Math.exp(-l*dt));
};
const minJerk=t=>t*t*t*(t*(t*6-15)+10);
const smooth=(a,b,x)=>{ const t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t); };
const _v1=new THREE.Vector3(), _v2=new THREE.Vector3(), _v3=new THREE.Vector3(),
      _v4=new THREE.Vector3(), _q1=new THREE.Quaternion(), _m1=new THREE.Matrix4();

// ── surface tracking ───────────────────────────────────────────────
export class SurfaceTracker{
  constructor(world){ this.world=world; this.ray=new THREE.Raycaster(); }
  cast(origin, dir, maxDist=60){
    this.ray.set(origin, dir); this.ray.far=maxDist;
    const hits=this.ray.intersectObjects(this.world.collidables, false);
    if(!hits.length) return null;
    const h=hits[0];
    const n=h.face?h.face.normal.clone().transformDirection(h.object.matrixWorld):_v1.set(0,1,0).clone();
    return {point:h.point.clone(), normal:n, dist:h.distance,
      obj:h.object, surf:h.object.userData.surf||'ground', rough:h.object.userData.rough??0.9};
  }
  support(pos, up){
    _v1.copy(pos).addScaledVector(up, 9);
    _v2.copy(up).negate();
    return this.cast(_v1,_v2,40);
  }
}

// ── analytic leg IK (yaw + planar 2-bone + tarsus drape) ────────────
// NOTE: dedicated temps — callers pass shared vectors as targets.
const _s1=new THREE.Vector3(), _s2=new THREE.Vector3();
export function solveLeg(leg, footLocal, surfUpLocal, grip, dt, air=false){
  const hip=leg.root.position;
  // coxa is short & rigid: femur joint sits below hip
  const fj=_s1.set(hip.x, hip.y-leg.coxaLen, hip.z);
  // ankle target: tarsus drapes forward-down from ankle to foot
  const tLen=leg.tarsusLen*(air?0.55:0.82);
  const ankle=_s2.set(footLocal.x, footLocal.y, footLocal.z).addScaledVector(surfUpLocal, tLen);
  // — yaw: aim leg plane at ankle —
  const dx=ankle.x-fj.x, dz=ankle.z-fj.z;
  const wantYaw=Math.atan2(dx,dz);
  const yaw=leg.neutralYaw+THREE.MathUtils.clamp(
    ((wantYaw-leg.neutralYaw+Math.PI*3)%(Math.PI*2))-Math.PI, -0.75, 0.75);
  leg.yawG.rotation.y=damp(leg.yawG.rotation.y, yaw, 14, dt);
  // — planar 2-bone in yaw frame: fwd = horizontal in-plane, up = vertical —
  const sa=Math.sin(yaw), ca=Math.cos(yaw);
  const fwd=dx*sa+dz*ca, up=ankle.y-fj.y;
  const F=leg.F, T=leg.T;
  const d=clamp(Math.hypot(fwd,up), Math.abs(F-T)+0.15, (F+T)*0.985); // never straight → no pops
  // femur angle from straight-down toward +fwd (knee-up branch: femur out, tibia down)
  const a1=Math.atan2(fwd,-up);
  const femAng=a1+Math.acos(clamp((F*F+d*d-T*T)/(2*F*d),-1,1));
  const femRot=THREE.MathUtils.clamp(-femAng,-1.90,0.45);
  leg.femurG.rotation.x=damp(leg.femurG.rotation.x,femRot,16,dt);
  leg.femurG.rotation.z=damp(leg.femurG.rotation.z, leg.side*-0.06, 10, dt);
  // knee pos → tibia relative angle (world pitch −tibAng, minus femur pitch)
  const kneeF=Math.sin(femAng)*F, kneeU=-Math.cos(femAng)*F;
  const tibAng=Math.atan2(fwd-kneeF,-(up-kneeU));
  const rel=THREE.MathUtils.clamp((-tibAng)-femRot, 0.12, 2.55);
  leg.tibiaG.rotation.x=damp(leg.tibiaG.rotation.x,rel,16,dt);
  // — tarsus drape: continue forward-down from tibia, flatten toward surface —
  // (negative rotation.x = toward +Z local = forward in leg plane)
  if(air){
    leg.tarsusG.rotation.x=damp(leg.tarsusG.rotation.x,-0.18,10,dt);
    for(let i=0;i<leg.tarsals.length;i++)
      leg.tarsals[i].rotation.x=damp(leg.tarsals[i].rotation.x,-0.10,10,dt);
    leg.pre.rotation.x=damp(leg.pre.rotation.x,-0.12,10,dt);
  } else {
    leg.tarsusG.rotation.x=damp(leg.tarsusG.rotation.x,-(0.34+0.22*(1-grip)),10,dt);
    for(let i=0;i<leg.tarsals.length;i++){
      const last=i>=3;
      const target=last?-(0.34-0.26*grip):-0.15;  // distal pads flatten on smooth
      leg.tarsals[i].rotation.x=damp(leg.tarsals[i].rotation.x,target,10,dt);
      leg.tarsals[i].rotation.y=damp(leg.tarsals[i].rotation.y,leg.side*0.03,8,dt);
    }
    leg.pre.rotation.x=damp(leg.pre.rotation.x,-(0.22-0.14*grip),10,dt);
  }
  return clamp(d/((F+T)*0.985),0,1); // stretch 0..1
}

// ── gait controller ────────────────────────────────────────────────
const TRIPOD=[0,.5,0,.5,0,.5];                 // (L1·R2·L3)⇄(R1·L2·R3)
const WAVE=[1/3,1/6,0,5/6,4/6,3/6];            // metachronal back→front
export class Gait{
  constructor(mosq, surf){
    this.m=mosq; this.surf=surf;
    this.cycle=0; this.freq=2; this.duty=0.7; this.blend=0;
    this.speed=0; this.stride=1.4; this.stepH=0.5;
    this.legs=mosq.legs.map((leg,i)=>({
      leg, i, ph:TRIPOD[i], state:'stance',
      anchor:new THREE.Vector3(), anchorN:new THREE.Vector3(0,1,0),
      from:new THREE.Vector3(), to:new THREE.Vector3(), swingT:0,
      touchSurf:'ground', touchRough:0.9, planted:false,
    }));
    this.up=new THREE.Vector3(0,1,0);
    this.bobPh=0;
  }
  plantAll(bodyMatrix, up){
    for(const L of this.legs){
      _v1.copy(L.leg.neutral); _v1.y=-2.45;
      _v1.applyMatrix4(bodyMatrix);
      const hit=this.surf.cast(_v2.copy(_v1).addScaledVector(up,6), _v3.copy(up).negate(), 30);
      if(hit){ L.anchor.copy(hit.point); L.anchorN.copy(hit.normal);
        L.touchSurf=hit.surf; L.touchRough=hit.rough; }
      else L.anchor.copy(_v1).addScaledVector(up,-2.45);
      L.ph=TRIPOD[L.i]; L.state='stance'; L.planted=true;
    }
  }
  update(dt, bodyMatrix, velWorld, up, turnRate){
    const speed=velWorld.length();
    this.speed=damp(this.speed, speed, 4, dt);
    const sn=clamp(this.speed/22, 0, 1);          // 0 slow … 1 fast (≈22 mm/s)
    this.blend=damp(this.blend, smooth(0.2,0.8,sn), 3, dt);
    this.duty=lerp(0.82, 0.5, this.blend);
    this.stride=clamp(0.9+this.speed*0.09, 0.9, 2.7);
    this.stepH=0.42+sn*0.25;
    this.freq=clamp(this.speed/Math.max(0.6,this.stride), 0.6, 9);
    // standing still: freeze the cycle (perched mosquitoes hold stance)…
    this.marching=this.speed>0.4;
    const adv=this.marching?dt*this.freq:0;
    this.cycle=(this.cycle+adv)%1;
    this.up.copy(up);
    // body-frame velocity for foothold lead
    _v4.copy(velWorld);
    const inv=_m1.copy(bodyMatrix).invert();
    const velLocal=_v4.applyMatrix4(_m1).sub(_v3.set(0,0,0).applyMatrix4(inv));
    let gripSum=0;
    for(const L of this.legs){
      const targetOff=lerp(WAVE[L.i], TRIPOD[L.i], this.blend);
      L.ph=(L.ph+adv)%1;
      // ease phase toward (cycle+offset) without jumps
      const want=(this.cycle+targetOff)%1;
      let err=((want-L.ph+1.5)%1)-0.5;
      L.ph=(L.ph+err*Math.min(1,dt*2.0)+1)%1;
      const inStance=L.ph<this.duty;
      if(inStance && L.state==='swing'){
        // touchdown
        L.state='stance'; L.anchor.copy(L.to); L.planted=true;
      } else if(!inStance && L.state==='stance'){
        // liftoff → plan swing
        L.state='swing'; L.swingT=0;
        // current foot pos: from anchor (world)
        L.from.copy(L.anchor);
        // predict touchdown: neutral + velocity lead + turn lead
        _v1.copy(L.leg.neutral); _v1.y=-2.45;
        // turn compensation: rotate neutral by yaw lead (inner legs step shorter)
        const lead=clamp(turnRate*0.35,-0.5,0.5);
        const ca=Math.cos(lead), sa=Math.sin(lead);
        const nx=_v1.x*ca-_v1.z*sa, nz=_v1.x*sa+_v1.z*ca;
        _v1.x=nx; _v1.z=nz;
        // velocity lead in body frame
        const stanceT=this.duty/Math.max(0.6,this.freq);
        _v1.addScaledVector(velLocal, stanceT*0.55);
        // clamp to workspace ellipse (hard anti-cross guarantee)
        const n=L.leg.neutral, r=L.leg.wsR;
        _v1.x=n.x+clamp(_v1.x-n.x,-r.x,r.x);
        _v1.z=n.z+clamp(_v1.z-n.z,-r.z,r.z);
        _v1.applyMatrix4(bodyMatrix);            // → world
        const hit=this.surf.cast(_v2.copy(_v1).addScaledVector(up,5), _v3.copy(up).negate(), 26);
        if(hit){
          // neighbour separation: never step onto a sister foot
          const to=_v1.copy(hit.point);
          for(const O of this.legs){
            if(O===L||!O.planted) continue;
            _v2.copy(to).sub(O.anchor); _v2.addScaledVector(up,-_v2.dot(up));
            const d=_v2.length();
            if(d<0.55&&d>1e-4) to.addScaledVector(_v2.normalize(),(0.55-d));
          }
          L.to.copy(to); L.anchorN.copy(hit.normal);
          L.touchSurf=hit.surf; L.touchRough=hit.rough;
        } else {
          L.to.copy(L.from); // edge: hold position (searching handled by body)
        }
      }
      gripSum+= (1-L.touchRough);
      // pose foot
      let footW;
      if(L.state==='swing'){
        // …but always finish an in-progress swing (never freeze mid-air)
        const swingDur=this.marching?(1-this.duty)/Math.max(0.6,this.freq):0.22;
        L.swingT=Math.min(1, L.swingT+dt/Math.max(0.03,swingDur));
        if(L.swingT>=1&&!this.marching){ L.state='stance'; L.anchor.copy(L.to); }
        const e=minJerk(L.swingT);
        footW=_v1.copy(L.from).lerp(L.to,e)
          .addScaledVector(up, Math.sin(Math.PI*Math.min(1,L.swingT))*this.stepH);
      } else {
        footW=_v1.copy(L.anchor);
      }
      // world → body local for IK
      const footL=_v2.copy(footW).applyMatrix4(inv);
      const upL=_v3.copy(L.state==='stance'?L.anchorN:up).transformDirection(inv);
      solveLeg(L.leg, footL, upL.normalize(), 1-L.touchRough, dt, false);
    }
    this.grip=gripSum/6;
    return {duty:this.duty, blend:this.blend, sn};
  }
  gaitName(){
    if(this.speed<0.4) return 'stance';
    if(this.blend<0.33) return 'wave';
    if(this.blend<0.72) return 'tetrapod';
    return 'tripod';
  }
}

// ── wing kinematics (Bomphrey/Muijres parameters) ──────────────────
export class WingBeat{
  constructor(mosq){
    this.m=mosq;
    this.phase=0;
    this.freq=560;                    // female Aedes/Culex cruise (Hz)
    this.strokeAmp=40*D2R;            // ≈40° — lowest of any insect
    this.devAmp=14*D2R;               // figure-8 deviation
    this.devPhase=-Math.PI/2;
    this.rotAmp=50*D2R;               // feathering amplitude
    this.rotLead=25*D2R;              // advanced rotation (rotational lift)
    this.rotMean=8*D2R;
    this.planeTilt=0.35;              // stroke-plane pitch-down (rad, rel body)
    this.diff=0;                      // L/R differential (steering)
    this.bias=0;                      // mean-stroke shift (pitch control)
    this.blurGain=0.7;                // stroke-envelope opacity at full speed
  }
  update(dt, spin, fill, timeScale){
    // load response: fed mosquitoes raise amplitude + frequency (Muijres 2017)
    const f=this.freq*(1+0.10*fill);
    this.phase=(this.phase+dt*f*Math.PI*2)%(Math.PI*2);
    const A=(this.strokeAmp+12*D2R*fill)*spin;
    const D=this.devAmp*(0.4+0.6*spin);
    const R=(this.rotAmp+10*D2R*fill)*spin;
    const p=this.phase;
    for(const key of ['L','R']){
      const W=this.m.wings[key], s=W.side;
      // NOTE: hinge angles negate for the left wing — spanwise axes point
      // opposite directions, so identical values would flap antisymmetrically.
      const Al=A*(1+s*this.diff*0.3);
      const stroke=s*(Math.sin(p)*Al) + this.bias*spin;
      const dev=s*Math.sin(2*p+this.devPhase)*D;
      const rot=s*(this.rotMean*spin+Math.sin(p+this.rotLead)*R);
      // fold pose (wings scissored over abdomen, left overlapping right)
      const fStroke=s*1.52, fDev=s>0?-0.07:0.07, fRot=s*0.08;
      const k=1-spin;
      W.tilt.rotation.x=this.planeTilt*spin+0.12*k;
      W.stroke.rotation.y=lerp(stroke,fStroke,k);
      W.dev.rotation.z=lerp(dev,fDev,k);
      W.pitch.rotation.x=lerp(rot,fRot,k);
      // blur envelope at full speed (strobe hiding), fading in slow-mo
      const slow=smooth(0.10,0.015,timeScale);   // 1 when ultra-slow
      W.blur.material.opacity=this.blurGain*spin*(1-slow)*0.5;
    }
    // halteres antiphase
    for(const H of this.m.halteres){
      H.rotation.x=Math.sin(p+Math.PI)*0.45*spin;
      H.rotation.z=H.userData.s*0.15*spin;
    }
    // antennae sweep back + proboscis tuck with flight
    for(const a of this.m.antennae) a.flag.rotation.x=damp(a.flag.rotation.x, spin*0.35, 4, dt);
    return f;
  }
}

// ── flight controller ──────────────────────────────────────────────
export class Flight{
  constructor(mosq, surf){
    this.m=mosq; this.surf=surf;
    this.vel=new THREE.Vector3();
    this.yaw=0; this.bank=0; this.climbBias=0;
    this.trailT={L:[],R:[]};
  }
  update(dt, target, speed, upHint, arriveR=6){
    const m=this.m, pos=m.root.position;
    _v1.copy(target).sub(pos);
    const dist=_v1.length();
    _v1.normalize();
    // arrival slowdown + terrain clearance
    const wantSpeed=dist<arriveR? speed*clamp(dist/arriveR,0.15,1) : speed;
    _v2.copy(_v1).multiplyScalar(wantSpeed);
    const sup=this.surf.support(pos, upHint);
    if(sup&&sup.dist<11) _v2.addScaledVector(sup.normal, (11-sup.dist)*22);
    _v2.y+=this.climbBias;
    const k=1-Math.exp(-dt*2.4);
    const prevVy=this.vel.y;
    this.vel.lerp(_v2,k);
    pos.addScaledVector(this.vel,dt);
    // attitude: face velocity, bank into turns
    const hv=_v3.set(this.vel.x,0,this.vel.z);
    if(hv.length()>8){
      const wantYaw=Math.atan2(hv.x,hv.z);
      const old=this.yaw;
      this.yaw=dampAngle(this.yaw,wantYaw,3.2,dt);
      const yr=(this.yaw-old)/Math.max(dt,1e-3);
      this.bank=damp(this.bank,clamp(-yr*0.35,-0.65,0.65),4,dt);
    } else this.bank=damp(this.bank,0,3,dt);
    const spd=this.vel.length();
    const pitch=lerp(-0.34,-0.02,clamp(spd/320,0,1)) + clamp((this.vel.y)*-0.0012,-0.2,0.25);
    // compose orientation: yaw + pitch + bank around world-up-ish frame
    _q1.setFromEuler(new THREE.Euler(pitch,this.yaw,this.bank,'YXZ'));
    m.root.quaternion.slerp(_q1,1-Math.exp(-dt*4.5));
    // abdomen: droop with hover + load
    const droop=-(0.05+0.05*clamp(1-spd/200,0,1)+0.06*m.fill);
    m.abSegs.forEach((s,i)=>{ s.g.rotation.x=damp(s.g.rotation.x,droop*(i?0.8:0.4),4,dt); });
    // trailing legs (hind pair streams back along abdomen — classic silhouette)
    const sway=Math.sin(performance.now()*0.004)*0.06;
    const trailTargets=[
      [1.25,-2.0, 1.3], [1.45,-2.2,-0.1], [0.95,-1.15,-3.3],
    ];
    m.legs.forEach((leg)=>{
      _v1.set(leg.side*(trailTargets[leg.pair][0]+sway*leg.side),
        trailTargets[leg.pair][1], trailTargets[leg.pair][2]);
      // air-load lag:hind legs rise with speed
      _v1.y+=clamp(spd/400,0,1)*(leg.pair===2?0.5:0.2);
      solveLeg(leg,_v1,_v2.set(0,1,0),0.5,dt,true);
    });
    // head stabilisation: counter-pitch gaze
    m.head.rotation.x=damp(m.head.rotation.x,-pitch*0.55,5,dt);
    m.head.rotation.y=damp(m.head.rotation.y,0,5,dt);
    return {dist, speed:spd};
  }
}
