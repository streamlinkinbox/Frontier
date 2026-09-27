// ═══════════════════════════════════════════════════════════════════
// locomotion.js — 8-leg alternating-tetrapod gait, questing, climbing
// Leg order: [L1 L2 L3 L4 R1 R2 R3 R4]. Units: mm.
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

const clamp=THREE.MathUtils.clamp, lerp=THREE.MathUtils.lerp;
const damp=(a,b,l,dt)=>lerp(a,b,1-Math.exp(-l*dt));
const minJerk=t=>t*t*t*(t*(t*6-15)+10);
const smooth=(a,b,x)=>{ const t=clamp((x-a)/(b-a),0,1); return t*t*(3-2*t); };
const _v1=new THREE.Vector3(), _v2=new THREE.Vector3(),
      _v3=new THREE.Vector3(), _v4=new THREE.Vector3(),
      _m1=new THREE.Matrix4(), _s1=new THREE.Vector3(), _s2=new THREE.Vector3();

export class SurfaceTracker{
  constructor(world){ this.world=world; this.ray=new THREE.Raycaster(); }
  cast(origin,dir,maxDist=600){
    this.ray.set(origin,dir); this.ray.far=maxDist;
    const hits=this.ray.intersectObjects(this.world.collidables,false);
    if(!hits.length) return null;
    const h=hits[0];
    const n=h.face?h.face.normal.clone().transformDirection(h.object.matrixWorld)
                 :new THREE.Vector3(0,1,0);
    return {point:h.point.clone(),normal:n,dist:h.distance,
      obj:h.object,surf:h.object.userData.surf||'ground',rough:h.object.userData.rough??0.9};
  }
  support(pos,up){ return this.cast(_v1.copy(pos).addScaledVector(up,90),
                                   _v2.copy(up).negate(),400); }
}

// ── analytic leg IK (shared by all 8 legs) ─────────────────────────
export function solveLeg(leg,footLocal,surfUpLocal,grip,dt){
  const hip=leg.root.position;
  const fj=_s1.set(hip.x,hip.y-leg.coxaLen,hip.z);
  const ankle=_s2.set(footLocal.x,footLocal.y,footLocal.z)
    .addScaledVector(surfUpLocal,leg.tarsusLen*0.8);
  const dx=ankle.x-fj.x, dz=ankle.z-fj.z;
  const wantYaw=Math.atan2(dx,dz);
  const yaw=leg.neutralYaw+THREE.MathUtils.clamp(
    ((wantYaw-leg.neutralYaw+Math.PI*3)%(Math.PI*2))-Math.PI,-0.7,0.7);
  leg.yawG.rotation.y=damp(leg.yawG.rotation.y,yaw,12,dt);
  const sa=Math.sin(yaw), ca=Math.cos(yaw);
  const fwd=dx*sa+dz*ca, up=ankle.y-fj.y;
  const F=leg.F, T=leg.T;
  const d=clamp(Math.hypot(fwd,up),Math.abs(F-T)+1.5,(F+T)*0.985);
  const a1=Math.atan2(fwd,-up);
  const femAng=a1+Math.acos(clamp((F*F+d*d-T*T)/(2*F*d),-1,1));
  const femRot=THREE.MathUtils.clamp(-femAng,-1.9,0.45);
  leg.femurG.rotation.x=damp(leg.femurG.rotation.x,femRot,14,dt);
  leg.femurG.rotation.z=damp(leg.femurG.rotation.z,leg.side*-0.06,10,dt);
  const kneeF=Math.sin(femAng)*F, kneeU=-Math.cos(femAng)*F;
  const tibAng=Math.atan2(fwd-kneeF,-(up-kneeU));
  const rel=THREE.MathUtils.clamp((-tibAng)-femRot,0.12,2.55);
  leg.tibiaG.rotation.x=damp(leg.tibiaG.rotation.x,rel,14,dt);
  // tarsus drapes forward-down, claws bite on rough
  leg.tarsusG.rotation.x=damp(leg.tarsusG.rotation.x,-(0.34+0.22*(1-grip)),10,dt);
  leg.pre.rotation.x=damp(leg.pre.rotation.x,-(0.22-0.14*grip),10,dt);
  return clamp(d/((F+T)*0.985),0,1);
}

// idx: 0:L1 1:L2 2:L3 3:L4 4:R1 5:R2 6:R3 7:R4
const TETRA=[0,0.5,0,0.5,0.5,0,0.5,0];       // (L1·R2·L3·R4) ⇄ (R1·L2·R3·L4)
const WAVE=[0.375,0.25,0.125,0,0.875,0.75,0.625,0.5];
export class Gait8{
  constructor(tick,surf){
    this.t=tick; this.surf=surf;
    this.cycle=0; this.freq=1.5; this.duty=0.72; this.blend=0;
    this.speed=0; this.stride=20; this.stepH=7; this.wsScale=1;
    this.rideH=24;
    this.legs=tick.legs.map((leg,i)=>({leg,i,ph:TETRA[i],state:'stance',
      anchor:new THREE.Vector3(),anchorN:new THREE.Vector3(0,1,0),
      from:new THREE.Vector3(),to:new THREE.Vector3(),swingT:0,
      touchRough:0.9,planted:false}));
    this.questPair=[0,4];   // front legs double as sensors
  }
  plantAll(bodyMatrix,up){
    for(const L of this.legs){
      _v1.copy(L.leg.neutral); _v1.y=-this.rideH;
      _v1.applyMatrix4(bodyMatrix);
      const hit=this.surf.cast(_v2.copy(_v1).addScaledVector(up,60),
                               _v3.copy(up).negate(),300);
      if(hit){ L.anchor.copy(hit.point); L.anchorN.copy(hit.normal);
        L.touchRough=hit.rough; }
      else L.anchor.copy(_v1).addScaledVector(up,-this.rideH);
      L.ph=TETRA[L.i]; L.state='stance'; L.planted=true;
    }
  }
  update(dt,bodyMatrix,velWorld,up,turnRate,quest,t){
    const speed=velWorld.length();
    this.speed=damp(this.speed,speed,4,dt);
    const sn=clamp(this.speed/110,0,1);
    this.blend=damp(this.blend,smooth(0.2,0.8,sn),3,dt);
    this.duty=lerp(0.8,0.55,this.blend);
    this.stride=clamp(12+this.speed*0.22,12,34);
    this.stepH=(6+sn*4);
    this.freq=clamp(this.speed/Math.max(6,this.stride),0.5,5);
    this.marching=this.speed>2.5;
    const adv=this.marching?dt*this.freq:0;
    this.cycle=(this.cycle+adv)%1;
    _v4.copy(velWorld);
    const inv=_m1.copy(bodyMatrix).invert();
    const velLocal=_v4.applyMatrix4(_m1).sub(_v3.set(0,0,0).applyMatrix4(inv));
    let gripSum=0;
    for(const L of this.legs){
      const isQuestLeg=this.questPair.includes(L.i)&&quest>0.02;
      const targetOff=lerp(WAVE[L.i],TETRA[L.i],this.blend);
      L.ph=(L.ph+adv)%1;
      const want=(this.cycle+targetOff)%1;
      let err=((want-L.ph+1.5)%1)-0.5;
      L.ph=(L.ph+err*Math.min(1,dt*2.0)+1)%1;
      const inStance=L.ph<this.duty;
      if(!isQuestLeg){
        if(inStance&&L.state==='swing'){ L.state='stance'; L.anchor.copy(L.to); }
        else if(!inStance&&L.state==='stance'){
          L.state='swing'; L.swingT=0; L.from.copy(L.anchor);
          _v1.copy(L.leg.neutral); _v1.y=-this.rideH;
          const lead=clamp(turnRate*0.35,-0.5,0.5);
          const ca=Math.cos(lead),sa=Math.sin(lead);
          const nx=_v1.x*ca-_v1.z*sa, nz=_v1.x*sa+_v1.z*ca;
          _v1.x=nx; _v1.z=nz;
          const stanceT=this.duty/Math.max(0.5,this.freq);
          _v1.addScaledVector(velLocal,stanceT*0.55);
          const n=L.leg.neutral,r=L.leg.wsR,ws=this.wsScale;
          _v1.x=n.x+clamp(_v1.x-n.x,-r.x*ws,r.x*ws);
          _v1.z=n.z+clamp(_v1.z-n.z,-r.z*ws,r.z*ws);
          _v1.applyMatrix4(bodyMatrix);
          const hit=this.surf.cast(_v2.copy(_v1).addScaledVector(up,50),
                                   _v3.copy(up).negate(),260);
          if(hit){
            const to=_v1.copy(hit.point);
            for(const O of this.legs){
              if(O===L||!O.planted) continue;
              _v2.copy(to).sub(O.anchor); _v2.addScaledVector(up,-_v2.dot(up));
              const d=_v2.length();
              if(d<7&&d>1e-4) to.addScaledVector(_v2.normalize(),(7-d));
            }
            L.to.copy(to); L.anchorN.copy(hit.normal); L.touchRough=hit.rough;
          } else L.to.copy(L.from);
        }
      }
      gripSum+=(1-L.touchRough);
      let footW;
      if(L.state==='swing'){
        const swingDur=this.marching?(1-this.duty)/Math.max(0.5,this.freq):0.3;
        L.swingT=Math.min(1,L.swingT+dt/Math.max(0.03,swingDur));
        if(L.swingT>=1&&!this.marching){ L.state='stance'; L.anchor.copy(L.to); }
        const e=minJerk(L.swingT);
        footW=_v1.copy(L.from).lerp(L.to,e)
          .addScaledVector(up,Math.sin(Math.PI*Math.min(1,L.swingT))*this.stepH);
      } else footW=_v1.copy(L.anchor);
      const footL=_v2.copy(footW).applyMatrix4(inv);
      const upL=_v3.copy(L.state==='stance'?L.anchorN:up).transformDirection(inv);
      solveLeg(L.leg,footL,upL.normalize(),1-L.touchRough,dt);
      // ── questing override: front pair raised, waving, Haller's organ out ──
      if(isQuestLeg){
        const q=quest, sd=L.leg.side;
        const wave=Math.sin(t*2.3+sd*1.4)*0.20+Math.sin(t*5.1+sd)*0.06;
        const amp=0.6+0.4*Math.sin(t*0.7);
        const qYaw=L.leg.neutralYaw*0.25+sd*0.1;
        const qFem=-1.85+wave*amp, qTib=0.55+wave*amp*1.4, qTar=-0.5;
        L.leg.yawG.rotation.y=lerp(L.leg.yawG.rotation.y,qYaw,q);
        L.leg.femurG.rotation.x=lerp(L.leg.femurG.rotation.x,qFem,q);
        L.leg.tibiaG.rotation.x=lerp(L.leg.tibiaG.rotation.x,qTib,q);
        L.leg.tarsusG.rotation.x=lerp(L.leg.tarsusG.rotation.x,qTar,q);
      }
    }
    this.grip=gripSum/8;
    return {duty:this.duty,blend:this.blend,sn};
  }
  gaitName(){
    if(this.speed<0.6) return 'stance';
    if(this.blend<0.4) return 'wave';
    return 'alt. tetrapod';
  }
}
