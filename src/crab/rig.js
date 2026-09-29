import * as THREE from 'three';
import {CRAB,V,X,Y,rad,clamp,gaitFoot,clawLayout,lowerFinger,upperFinger,jawPivot,lowerRadius,upperRadius} from './anatomy.js';

/** Runtime rig shared by animation baking and the live pincer controller. */
export class CrabRig {
  constructor(model){
    this.model=model;this.bones={};this.rest=new Map();model.updateMatrixWorld(true);
    model.traverse(o=>{if(o.isBone){this.bones[o.name]=o;this.rest.set(o.name,{p:o.position.clone(),q:o.quaternion.clone()});}});
    this.space=this.bones.Root.parent;this.scale=this.space.getWorldScale(V()).x;
    const point=name=>this.local(this.bones[name].getWorldPosition(V()));
    this.legs=[];for(const s of [-1,1])for(let i=0;i<4;i++){const name=`P${i+2}_${s<0?'L':'R'}`;this.legs.push({s,i,name,hip:point(`${name}_Merus`),knee:point(`${name}_Carpus`),ankle:point(`${name}_Dactyl`),tip:point(`${name}_Tip`)});}
    this.claws=clawLayout();this.last={};this.gripCache=new Map();
  }
  local(p){return this.space.worldToLocal(p.clone());}
  world(p){return this.space.localToWorld(p.clone());}
  reset(){for(const [name,r] of this.rest){const b=this.bones[name];b.position.copy(r.p);b.quaternion.copy(r.q);b.scale.set(1,1,1);}}
  legPose(leg,targetTip,shift){
    const target=targetTip.clone().sub(leg.tip.clone().sub(leg.ankle));
    const hip=leg.hip.clone().add(shift),a=leg.hip.distanceTo(leg.knee),b=leg.knee.distanceTo(leg.ankle),dir=target.clone().sub(hip),raw=dir.length(),d=clamp(raw,Math.abs(a-b)+.001,a+b-.001);dir.normalize();
    const old=leg.knee.clone().sub(leg.hip),pole=old.clone().addScaledVector(dir,-old.dot(dir)).normalize();
    const along=(a*a-b*b+d*d)/(2*d),height=Math.sqrt(Math.max(0,a*a-along*along));
    const knee=hip.clone().addScaledVector(dir,along).addScaledVector(pole,height);
    const q1=new THREE.Quaternion().setFromUnitVectors(old.normalize(),knee.clone().sub(hip).normalize());
    const q2=new THREE.Quaternion().setFromUnitVectors(leg.ankle.clone().sub(leg.knee).normalize(),target.clone().sub(knee).normalize());
    this.bones[`${leg.name}_Merus`].quaternion.copy(q1);this.bones[`${leg.name}_Carpus`].quaternion.copy(q1.clone().invert().multiply(q2));
    this.bones[`${leg.name}_Dactyl`].quaternion.copy(q2.clone().invert());
    return raw<=a+b;
  }
  jaw(claw,angle){const axis=X.clone().applyQuaternion(claw.frame);this.bones[`${claw.name}_Dactyl`].quaternion.setFromAxisAngle(axis,-angle);}
  poseClip(name,time){
    this.reset();const walking=name.startsWith('Walk_'),duration=walking?CRAB.walkDuration:name.startsWith('Pinch_')?1.8:6,cyc=time/duration*Math.PI*2,direction=name==='Walk_Left'?-1:1;
    const shift=walking?V(0,.025*Math.sin(cyc*2),0):V();this.bones.Root.position.copy(shift);
    for(const leg of this.legs){const step=walking?gaitFoot(time,leg,direction):{offset:0,lift:0};this.legPose(leg,leg.tip.clone().add(V(step.offset,step.lift,0)),shift);}
    for(const c of this.claws){let open=rad(11);if(name===`Pinch_${c.side}`){const u=clamp(time/1.8,0,1);open=rad(11+47*Math.sin(Math.PI*u)**2);}this.jaw(c,open);}
    this.bones.Eye_L.rotation.y=.055*Math.sin(cyc);this.bones.Eye_R.rotation.y=.055*Math.sin(cyc+.4);
    this.bones.Antenna_L.rotation.x=.07*Math.sin(cyc*2);this.bones.Antenna_R.rotation.x=.06*Math.sin(cyc*2+.7);
    this.bones.Maxilliped_L.rotation.z=.018*Math.sin(cyc*3);this.bones.Maxilliped_R.rotation.z=-.018*Math.sin(cyc*3);
    this.model.updateMatrixWorld(true);
  }
  gripGeometry(claw,radiusWorld){
    const key=claw.side+':'+radiusWorld;if(this.gripCache.has(key))return this.gripCache.get(key);
    const radius=radiusWorld/this.scale/claw.scale,pad=claw.s>0?.055:.035,t=.63;
    const p=lowerFinger.getPoint(t),tangent=lowerFinger.getTangent(t),normal=V(0,tangent.z,-tangent.y).normalize();
    const point=p.addScaledVector(normal,lowerRadius(t)+pad+radius);
    const distance=angle=>{let best=100;const q=new THREE.Quaternion().setFromAxisAngle(X,-angle);for(let i=0;i<=100;i++){const u=i/100,a=upperFinger.getPoint(u).sub(jawPivot).applyQuaternion(q).add(jawPivot);best=Math.min(best,a.distanceTo(point)-upperRadius(u)-pad);}return best;};
    let lo=0,hi=rad(70);if(distance(hi)<radius)return null;
    for(let i=0;i<30;i++){const mid=(lo+hi)/2;if(distance(mid)<radius)lo=mid;else hi=mid;}
    const result={point,angle:(lo+hi)/2,radius,pad};this.gripCache.set(key,result);return result;
  }
  plan(targetWorld,side='auto',radiusWorld=.0028){
    const target=this.local(targetWorld);let candidates=this.claws.filter(c=>side==='auto'||c.side===side).map(claw=>{
      const grip=this.gripGeometry(claw,radiusWorld);if(!grip)return {claw,valid:false,reason:'Target is too large for this claw.'};
      const v=target.clone().sub(claw.base),yaw=clamp(Math.atan2(v.x,v.z),-1.2,1.2),pitch=clamp(-Math.atan2(v.y,Math.hypot(v.x,v.z)),-.5,.5);
      const frame=new THREE.Quaternion().setFromEuler(new THREE.Euler(pitch,yaw,0,'YXZ'));
      const wrist=target.clone().sub(grip.point.clone().multiplyScalar(claw.scale).applyQuaternion(frame));
      const a=claw.base.distanceTo(claw.elbow),b=claw.elbow.distanceTo(claw.wrist),delta=wrist.clone().sub(claw.base),d=delta.length(),dir=delta.clone().normalize();
      let valid=d<a+b-.015&&d>Math.abs(a-b)+.035&&(wrist.z>2.35||Math.abs(wrist.x)>3.70);
      const pole=claw.elbow.clone().sub(claw.base);pole.addScaledVector(dir,-pole.dot(dir)).normalize();
      const distance=clamp(d,Math.abs(a-b)+.001,a+b-.001),along=(a*a-b*b+distance*distance)/(2*distance),height=Math.sqrt(Math.max(0,a*a-along*along));
      const elbow=claw.base.clone().addScaledVector(dir,along).addScaledVector(pole,height);
      const q1=new THREE.Quaternion().setFromUnitVectors(claw.elbow.clone().sub(claw.base).normalize(),elbow.clone().sub(claw.base).normalize());
      const q2=new THREE.Quaternion().setFromUnitVectors(claw.wrist.clone().sub(claw.elbow).normalize(),wrist.clone().sub(elbow).normalize());
      const qHand=frame.clone().multiply(claw.frame.clone().invert());
      return {claw,grip,frame,wrist,valid,reason:valid?'Within reach.':d>a+b-.015?'Too far — move the bead closer.':'Too close to the shell — move the bead forward.',q1,q2,qHand,distance:claw.wrist.distanceTo(wrist)};
    });
    candidates.sort((a,b)=>Number(b.valid)-Number(a.valid)||a.distance-b.distance);return candidates[0];
  }
  poseGrasp(plan,reach,open,time=0){
    this.poseClip('Idle',time);if(!plan)return;
    const c=plan.claw,q1=new THREE.Quaternion().slerp(plan.q1,reach),q2=new THREE.Quaternion().slerp(plan.q2,reach),qh=new THREE.Quaternion().slerp(plan.qHand,reach);
    this.bones[`${c.name}_Merus`].quaternion.copy(q1);this.bones[`${c.name}_Carpus`].quaternion.copy(q1.clone().invert().multiply(q2));this.bones[`${c.name}_Propodus`].quaternion.copy(q2.clone().invert().multiply(qh));this.jaw(c,open);
    this.model.updateMatrixWorld(true);
  }
  gripWorld(plan){return this.bones[`${plan.claw.name}_Propodus`].localToWorld(plan.grip.point.clone().multiplyScalar(plan.claw.scale).applyQuaternion(plan.claw.frame));}
  contact(plan,target){
    const error=this.gripWorld(plan).distanceTo(target),c=plan.claw;
    const distance=upper=>{let best=100;const bone=this.bones[`${c.name}_${upper?'Dactyl':'Propodus'}`];for(let i=0;i<=100;i++){const u=i/100,p=(upper?upperFinger:lowerFinger).getPoint(u);if(upper)p.sub(jawPivot);p.multiplyScalar(c.scale).applyQuaternion(c.frame);bone.localToWorld(p);const r=(upper?upperRadius(u):lowerRadius(u))+plan.grip.pad;best=Math.min(best,p.distanceTo(target)-r*c.scale*this.scale);}return best;};
    const distances=[distance(false),distance(true)],radius=plan.grip.radius*c.scale*this.scale;
    return {hit:error<.00045&&distances.every(d=>Math.abs(d-radius)<.00045),error,distances};
  }
}
