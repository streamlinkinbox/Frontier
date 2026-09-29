import * as THREE from 'three';
import { HUNT, clamp } from './hunt-config.js';
const V = (x=0,y=0,z=0) => new THREE.Vector3(x,y,z);
const rad = THREE.MathUtils.degToRad, deg = THREE.MathUtils.radToDeg;
const X=V(1,0,0),Y=V(0,1,0);
const angle = v => deg(Math.atan2(v.y,v.z));

/**
 * Target-aware posing of the existing mantis rig, in its authored centimetres.
 * Bone lengths are never scaled. The coxa aims the hinge plane; femur and tibia
 * stay in that plane. A virtual grip circle is tangent to BOTH spined segments.
 * Analytic two-link IK puts that circle at the requested prey position.
 * The four walking feet are solved back onto their original ground anchors.
 */
export class HuntRig {
  constructor(model) {
    this.model=model; this.bones={}; this.rest=new Map();
    model.updateMatrixWorld(true);
    model.traverse(o=>{if(o.isBone){this.bones[o.name]=o;this.rest.set(o.name,{p:o.position.clone(),q:o.quaternion.clone(),s:o.scale.clone()});}});
    if(!this.bones.Root || !this.bones.Fore_L_Coxa)throw new Error('The mantis deformation rig is missing.');
    this.space=this.bones.Root.parent;
    this.scale=this.space.getWorldScale(V()).x;
    this.radius=HUNT.radiusMetres/this.scale;
    this.separation=this.radius*.45;
    this.gripRadius=Math.sqrt(this.radius**2-this.separation**2)+HUNT.skinPaddingMetres/this.scale;
    const point=n=>this.toLocal(this.bones[n].getWorldPosition(V()));
    this.proOrigin=point('Prothorax');this.headRest=point('Head');
    this.legs=[];
    for(const name of ['Middle_L','Middle_R','Hind_L','Hind_R']){
      const hip=point(`${name}_Femur`),knee=point(`${name}_Tibia`),ankle=point(`${name}_Tarsus`);
      this.legs.push({name,hip,knee,ankle,a:hip.distanceTo(knee),b:knee.distanceTo(ankle)});
    }
    this.arms=[-1,1].map(s=>{
      const side=s<0?'L':'R',name=`Fore_${side}`;
      const base=point(`${name}_Coxa`),elbow=point(`${name}_Femur`),wrist=point(`${name}_Tibia`),end=point(`${name}_Tarsus`);
      const co=elbow.clone().sub(base),fe=wrist.clone().sub(elbow),ti=end.clone().sub(wrist);
      return {s,side,name,base,localBase:this.rest.get(`${name}_Coxa`).p.clone(),co,fe,ti,
        rest:{coxa:angle(co),femur:angle(fe),gape:180+angle(ti)-angle(fe),tibia:angle(ti),tarsus:deg(Math.atan2(-.29,-.22))}};
    });
    this.lastSolutions=this.restSolutions();this.lastPosture=null;
  }
  toLocal(world){return this.space.worldToLocal(world.clone());}
  toWorld(local){return this.space.localToWorld(local.clone());}
  point(name){return this.bones[name].getWorldPosition(V());}
  reset(){
    for(const [name,r] of this.rest){const b=this.bones[name];b.position.copy(r.p);b.quaternion.copy(r.q);b.scale.copy(r.s);}
    this.model.traverse(o=>{if(o.morphTargetInfluences)o.morphTargetInfluences.fill(0);});
  }
  restSolutions(){return this.arms.map(a=>({...a.rest,yaw:0,tarsusFold:0}));}
  posture(target,lean,time=0){
    const yaw=clamp(Math.atan2(target.x,Math.max(.3,target.z-this.headRest.z)),-1.05,1.05);
    return {
      shift:V(.045*Math.sin(yaw)*lean,-.055*lean+.002*Math.sin(time*2),.18*lean),
      q:new THREE.Quaternion().setFromEuler(new THREE.Euler(.095*lean,clamp(yaw*.30,-.22,.22)*lean,0,'YXZ')),
    };
  }
  gripGeometry(arm,gape){
    const f=V(arm.fe.x,0,Math.hypot(arm.fe.y,arm.fe.z));
    const t=V(arm.ti.x,Math.sin(rad(gape-180))*Math.hypot(arm.ti.y,arm.ti.z),Math.cos(rad(gape-180))*Math.hypot(arm.ti.y,arm.ti.z));
    const included=f.clone().negate().angleTo(t);
    const bisector=f.clone().normalize().negate().add(t.clone().normalize()).normalize();
    const g=f.clone().addScaledVector(bisector,this.gripRadius/Math.sin(included/2));
    return {deltaX:arm.co.x+g.x,length:Math.hypot(g.y,g.z),angle:Math.atan2(g.y,g.z)};
  }
  solveArm(arm,target,posture,{strict=true,gape=50}={}){
    const base=arm.localBase.clone().applyQuaternion(posture.q).add(this.proOrigin).add(posture.shift);
    const center=target.clone().sub(base).applyQuaternion(posture.q.clone().invert());
    const geometry=this.gripGeometry(arm,gape);
    let yaw=Math.atan2(center.x,Math.max(.01,center.z)),desired;
    for(let i=0;i<5;i++){
      desired=center.clone().addScaledVector(V(Math.cos(yaw),0,-Math.sin(yaw)),arm.s*this.separation);
      const horizontal=Math.hypot(desired.x,desired.z);
      const z=Math.sqrt(Math.max(.00001,horizontal*horizontal-geometry.deltaX**2));
      yaw=Math.atan2(desired.x,desired.z)-Math.atan2(geometry.deltaX,z);
    }
    desired.applyQuaternion(new THREE.Quaternion().setFromAxisAngle(Y,-yaw));
    const c=Math.hypot(arm.co.y,arm.co.z),g=geometry.length,d=Math.hypot(desired.y,desired.z);
    let valid=d<c+g-.008 && d>Math.abs(c-g)+.025 && desired.z>0;
    let reason=d>=c+g-.008?'Too far — move the target closer.':d<=Math.abs(c-g)+.025?'Too close to fold around.':'Outside the forward grasp.';
    const phi=Math.atan2(desired.y,desired.z);
    const bend=Math.acos(clamp((c*c+d*d-g*g)/(2*c*Math.max(d,.001)),-1,1));
    const coxa=phi-bend;
    const femur=Math.atan2(desired.y-c*Math.sin(coxa),desired.z-c*Math.cos(coxa))-geometry.angle;
    if(strict && (Math.abs(yaw)>rad(57)||deg(coxa)<-115||deg(coxa)>55||deg(femur)<10||deg(femur)>122)){
      valid=false;reason='Outside this pose’s grasp — adjust height or distance.';
    }
    return {coxa:deg(coxa),femur:deg(femur),gape,yaw,tarsusFold:1,valid,reason};
  }
  plan(worldTarget,lean=1,time=0,strict=true){
    const target=this.toLocal(worldTarget),posture=this.posture(target,lean,time);
    const solutions=this.arms.map(arm=>this.solveArm(arm,target,posture,{strict}));
    return {target,posture,solutions,reachable:solutions.every(s=>s.valid),reason:solutions.find(s=>!s.valid)?.reason||'Within the grasp envelope.'};
  }
  feedingTarget(aimWorld,lean=.2,time=0){
    const aim=this.toLocal(aimWorld),p=this.posture(aim,lean,time);
    const local=this.headRest.clone().sub(this.proOrigin).add(V(0,-.46,.51));
    return this.toWorld(local.applyQuaternion(p.q).add(this.proOrigin).add(p.shift));
  }
  mixSolutions(a,b,t){
    return this.arms.map((_,i)=>{
      const out={};for(const key of ['coxa','femur','gape','yaw','tarsusFold'])out[key]=THREE.MathUtils.lerp(a[i][key],b[i][key],t);
      return out;
    });
  }
  preload(solutions){return solutions.map(s=>({...s,coxa:s.coxa-11,femur:Math.min(153,s.femur+50),gape:110,tarsusFold:1}));}
  solveLeg(leg,shift){
    const hip=leg.hip.clone().add(shift),target=leg.ankle;
    const direction=target.clone().sub(hip),distance=clamp(direction.length(),Math.abs(leg.a-leg.b)+.001,leg.a+leg.b-.001);direction.normalize();
    const rest=leg.knee.clone().sub(leg.hip);
    const pole=rest.clone().addScaledVector(direction,-rest.dot(direction)).normalize();
    const along=(leg.a*leg.a-leg.b*leg.b+distance*distance)/(2*distance),height=Math.sqrt(Math.max(0,leg.a*leg.a-along*along));
    const knee=hip.clone().addScaledVector(direction,along).addScaledVector(pole,height);
    const q1=new THREE.Quaternion().setFromUnitVectors(rest.normalize(),knee.clone().sub(hip).normalize());
    const q2=new THREE.Quaternion().setFromUnitVectors(leg.ankle.clone().sub(leg.knee).normalize(),target.clone().sub(knee).normalize());
    this.bones[`${leg.name}_Femur`].quaternion.copy(q1);
    this.bones[`${leg.name}_Tibia`].quaternion.copy(q1.clone().invert().multiply(q2));
    this.bones[`${leg.name}_Tarsus`].quaternion.copy(q2.clone().invert());
  }
  poseArm(arm,m){
    const r=arm.rest,plane=new THREE.Quaternion().setFromAxisAngle(Y,m.yaw);
    const hinge=angle=>plane.clone().multiply(new THREE.Quaternion().setFromAxisAngle(X,rad(angle)));
    const tibia=m.femur-180+m.gape,tarsusRelative=THREE.MathUtils.lerp(r.tarsus-r.tibia,158,m.tarsusFold);
    const qc=hinge(r.coxa-m.coxa),qf=hinge(r.femur-m.femur),qt=hinge(r.tibia-tibia),qFoot=hinge(r.tarsus-(tibia+tarsusRelative));
    this.bones[`${arm.name}_Coxa`].quaternion.copy(qc);
    this.bones[`${arm.name}_Femur`].quaternion.copy(qc.clone().invert().multiply(qf));
    this.bones[`${arm.name}_Tibia`].quaternion.copy(qf.clone().invert().multiply(qt));
    this.bones[`${arm.name}_Tarsus`].quaternion.copy(qt.clone().invert().multiply(qFoot));
  }
  pose({aim,gaze=aim,lean=0,solutions=this.restSolutions(),time=0,attention=1}){
    this.reset();
    const target=this.toLocal(aim),gazeLocal=this.toLocal(gaze),p=this.posture(target,lean,time);
    this.bones.Root.position.add(p.shift);
    this.bones.Prothorax.quaternion.copy(p.q);
    this.legs.forEach(leg=>this.solveLeg(leg,p.shift));
    this.arms.forEach((arm,i)=>this.poseArm(arm,solutions[i]));
    const head=this.headRest.clone().sub(this.proOrigin).applyQuaternion(p.q).add(this.proOrigin).add(p.shift);
    const direction=gazeLocal.clone().sub(head);
    const yaw=clamp(Math.atan2(direction.x,direction.z),-1.13,1.13);
    const pitch=clamp(-Math.atan2(direction.y,Math.hypot(direction.x,direction.z)),-.65,.85);
    const headWorld=new THREE.Quaternion().setFromEuler(new THREE.Euler(pitch,yaw,0,'YXZ'));
    this.bones.Head.quaternion.copy(p.q.clone().invert().multiply(headWorld));
    this.bones.Abdomen_02.scale.y=1+.012*Math.sin(time*2.4);
    for(const side of ['L','R'])for(let k=1;k<=4;k++){
      const b=this.bones[`Antenna_${side}_${k}`];
      b.rotation.x=.012*Math.sin(time*1.5-k*.6+(side==='L'?0:1))*(1-.6*attention);
      b.rotation.z=.014*Math.sin(time*1.9-k*.5)*(1-.6*attention);
    }
    this.model.updateMatrixWorld(true);this.lastSolutions=solutions;this.lastPosture=p;
  }
  gripPoints(){
    return this.arms.map(arm=>{
      const elbow=this.point(`${arm.name}_Femur`),wrist=this.point(`${arm.name}_Tibia`),end=this.point(`${arm.name}_Tarsus`);
      const back=elbow.clone().sub(wrist).normalize(),tibia=end.clone().sub(wrist).normalize();
      const gape=back.angleTo(tibia),bisector=back.clone().add(tibia).normalize();
      return wrist.clone().addScaledVector(bisector,this.gripRadius*this.scale/Math.max(.05,Math.sin(gape/2)));
    });
  }
  gripCenter(){const p=this.gripPoints();return p[0].add(p[1]).multiplyScalar(.5);}
  contact(target){
    const center=this.gripCenter(),error=center.distanceTo(target);
    const distances=this.arms.map(arm=>{
      const e=this.point(`${arm.name}_Femur`),w=this.point(`${arm.name}_Tibia`),t=this.point(`${arm.name}_Tarsus`);
      return [new THREE.Line3(e,w).closestPointToPoint(target,true,V()).distanceTo(target),new THREE.Line3(w,t).closestPointToPoint(target,true,V()).distanceTo(target)];
    });
    const bound=HUNT.radiusMetres+HUNT.skinPaddingMetres+.0005;
    return {hit:error<=HUNT.contactToleranceMetres&&distances.every(pair=>pair.every(d=>d<=bound)),error,distances,center};
  }
  aimError(target){
    const forward=V(0,0,1).applyQuaternion(this.bones.Head.getWorldQuaternion(new THREE.Quaternion()));
    return forward.angleTo(target.clone().sub(this.point('Head')).normalize());
  }
}
