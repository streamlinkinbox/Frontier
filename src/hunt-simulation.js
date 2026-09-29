import * as THREE from 'three';
import { HUNT, clamp, smooth } from './hunt-config.js';
const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
const rad=THREE.MathUtils.degToRad,deg=THREE.MathUtils.radToDeg;

export function placementToWorld({angle,distance,height}) {
  return V(Math.sin(rad(angle))*distance/1000,height/1000,HUNT.forelegOriginZ+Math.cos(rad(angle))*distance/1000);
}
export function boundPlacement(value) {
  const p={};for(const key of ['angle','distance','height']){
    const n=Number(value[key]);p[key]=clamp(Number.isFinite(n)?n:HUNT.initial[key],...HUNT.limits[key]);
  }return p;
}
export function worldToPlacement(point){
  const z=point.z-HUNT.forelegOriginZ;
  return boundPlacement({angle:deg(Math.atan2(point.x,z)),distance:Math.hypot(point.x,z)*1000,height:point.y*1000});
}

/** Deterministic runtime behavior. No DOM, renderer, downloads, or baked-clip edits. */
export class HuntSimulation {
  constructor(rig){
    this.rig=rig;this.autoStrike=false;this.dragging=false;this.leanSeconds=HUNT.leanSeconds;
    this.placement={...HUNT.initial};this.target=placementToWorld(this.placement);
    this.gaze=this.target.clone();this.time=0;this.phase='tracking';this.phaseTime=0;
    this.version=0;this.consumedVersion=-1;this.lastMove=0;this.lockTime=0;
    this.armed=false;this.lean=0;this.captures=0;this.misses=0;this.lastContact=null;
    this.reach=this.rig.plan(this.target);this.offset=V();this.velocity=V();
    this._poseTracking(0);
  }
  get holding(){return this.phase==='holding'||this.phase==='retracting';}
  get busy(){return ['leaning','striking','retracting','recovering','releasing'].includes(this.phase)||this.armed;}
  get lockProgress(){return clamp(this.lockTime/HUNT.lockSeconds,0,1);}
  setPlacement(value){
    if(this.holding||this.phase==='releasing')return false;
    const next=boundPlacement({...this.placement,...value}),point=placementToWorld(next);
    if(point.distanceTo(this.target)<1e-8)return true;
    this.placement=next;this.target.copy(point);this.version++;this.lastMove=this.time;this.lockTime=0;
    if(this.phase==='leaning')this._recover('Target moved — re-acquiring.',true);
    else if(['locked','released','tracking'].includes(this.phase)){this.phase='tracking';this.phaseTime=0;}
    this.reach=this.rig.plan(this.target);
    return true;
  }
  setTarget(point){return this.setPlacement(worldToPlacement(point));}
  setDragging(value){
    this.dragging=value;
    if(value){this.lockTime=0;if(this.phase==='leaning')this._recover('Target moved — re-acquiring.',true);else if(this.phase==='locked')this.phase='tracking';}
    this.lastMove=this.time;
  }
  requestStrike(){
    if(this.holding){this.release();return true;}
    if(this.phase==='released'){this.reset();return true;}
    this.reach=this.rig.plan(this.target);
    if(!this.reach.reachable||this.busy)return false;
    this.armed=true;
    if(this.phase==='locked')this._beginLean();
    return true;
  }
  reset({center=false}={}){
    if(center)this.placement={...HUNT.initial};
    this.target.copy(placementToWorld(this.placement));this.gaze.copy(this.target);
    this.phase='tracking';this.phaseTime=0;this.lockTime=0;this.armed=false;this.dragging=false;
    this.lean=0;this.version++;this.consumedVersion=-1;this.lastMove=this.time;this.velocity.set(0,0,0);
    this.committed=null;this.offset.set(0,0,0);this.lastContact=null;
    this.reach=this.rig.plan(this.target);this._poseTracking(0);
  }
  release(){
    if(!this.holding)return;
    this.recoverFrom=this.rig.lastSolutions.map(x=>({...x}));this.recoverLean=this.lean;
    this.recoveryAim=this.target.clone();this.phase='releasing';this.phaseTime=0;
    this.velocity.set(0,.015,.006);this.armed=false;this.lockTime=0;this.consumedVersion=this.version;
  }
  _recover(message,reacquire=false){
    this.recoverFrom=this.rig.lastSolutions.map(x=>({...x}));this.recoverLean=this.lean;
    this.recoveryAim=(this.committed||this.target).clone();this.phase='recovering';this.phaseTime=0;
    this.recoveryMessage=message;this.armed=reacquire&&this.armed;this.lockTime=0;
  }
  _beginLean(){
    this.committed=this.target.clone();this.plan=this.rig.plan(this.committed,1,this.time);
    if(!this.plan.reachable){this.armed=false;this.phase='tracking';return;}
    this.preload=this.rig.preload(this.plan.solutions);this.activeLeanSeconds=this.leanSeconds;this.phase='leaning';this.phaseTime=0;
    this.consumedVersion=this.version;this.startLean=this.lean;
  }
  _poseTracking(dt){
    this.lean*=Math.exp(-dt*8);
    this.rig.pose({aim:this.target,gaze:this.gaze,lean:this.lean,time:this.time,attention:this.lockProgress});
  }
  _tick(dt){
    this.time+=dt;this.phaseTime+=dt;
    this.gaze.lerp(this.target,1-Math.exp(-dt*9));
    if(['tracking','locked'].includes(this.phase)){
      this.reach=this.rig.plan(this.target,1,this.time);this._poseTracking(dt);
      const stable=!this.dragging&&this.time-this.lastMove>.10&&this.rig.aimError(this.target)<rad(4.5);
      if(stable&&this.reach.reachable){this.lockTime=Math.min(HUNT.lockSeconds,this.lockTime+dt);}
      else {this.lockTime=0;this.phase='tracking';}
      if(this.lockProgress>=1){
        this.phase='locked';
        if(this.armed||(this.autoStrike&&this.consumedVersion!==this.version))this._beginLean();
      }
      return;
    }
    if(this.phase==='leaning'){
      if(this.target.distanceTo(this.committed)>HUNT.targetMotionTolerance||this.dragging){this._recover('Target moved — re-acquiring.',true);return;}
      const u=clamp(this.phaseTime/(this.activeLeanSeconds||this.leanSeconds),0,1);
      this.lean=THREE.MathUtils.lerp(this.startLean,1,smooth(0,1,u));
      const arms=this.rig.mixSolutions(this.rig.restSolutions(),this.preload,smooth(.58,1,u));
      this.rig.pose({aim:this.committed,gaze:this.gaze,lean:this.lean,solutions:arms,time:this.time});
      if(u>=1){this.phase='striking';this.phaseTime=0;this.armed=false;this.plan=this.rig.plan(this.committed,1,this.time);this.preload=this.rig.preload(this.plan.solutions);}
      return;
    }
    if(this.phase==='striking'){
      const t=this.phaseTime;
      const solutions=this.plan.solutions.map((end,i)=>({
        ...end,
        coxa:THREE.MathUtils.lerp(this.preload[i].coxa,end.coxa,smooth(0,HUNT.sweepSeconds,t)),
        femur:THREE.MathUtils.lerp(this.preload[i].femur,end.femur,smooth(0,HUNT.sweepSeconds,t)),
        gape:t<HUNT.clampStart?THREE.MathUtils.lerp(110,126,smooth(0,HUNT.clampStart,t)):THREE.MathUtils.lerp(126,end.gape,smooth(HUNT.clampStart,HUNT.captureSeconds,t)),
      }));
      this.lean=1;
      this.rig.pose({aim:this.committed,gaze:this.gaze,lean:1,solutions,time:this.time});
      if(t>=HUNT.captureSeconds){
        this.lastContact=this.rig.contact(this.target);
        if(this.lastContact.hit){
          this.offset.copy(this.target).sub(this.lastContact.center);
          this.capturePoint=this.target.clone();this.phase='retracting';this.phaseTime=0;this.captures++;
        }else{
          this.misses++;this.consumedVersion=this.version;this._recover('Missed — the target left the committed strike.');
        }
      }
      return;
    }
    if(this.phase==='retracting'||this.phase==='holding'){
      const u=this.phase==='holding'?1:smooth(0,HUNT.retractSeconds,this.phaseTime);
      this.lean=THREE.MathUtils.lerp(1,.20,u);
      const neutral=this.rig.toWorld(this.rig.headRest.clone().add(V(0,-.46,.51)));
      const feeding=this.rig.feedingTarget(neutral,.2,this.time);
      const desired=this.capturePoint.clone().lerp(feeding,u);
      const plan=this.rig.plan(desired,this.lean,this.time,false);
      this.rig.pose({aim:desired,gaze:this.gaze,lean:this.lean,solutions:plan.solutions,time:this.time});
      this.target.copy(this.rig.gripCenter()).add(this.offset);
      if(u>=1&&this.phase!=='holding'){this.phase='holding';this.phaseTime=0;}
      return;
    }
    if(this.phase==='recovering'||this.phase==='releasing'){
      const releasing=this.phase==='releasing';
      const u=smooth(releasing ? .14 : 0,HUNT.recoverySeconds,this.phaseTime);this.lean=this.recoverLean*(1-u);
      const arms=this.rig.mixSolutions(this.recoverFrom,this.rig.restSolutions(),u);
      if(releasing){const open=smooth(0,.11,this.phaseTime)*(1-smooth(.14,.40,this.phaseTime));arms.forEach(a=>a.gape=THREE.MathUtils.lerp(a.gape,115,open));}
      this.rig.pose({aim:this.recoveryAim,gaze:this.gaze,lean:this.lean,solutions:arms,time:this.time});
      if(this.phase==='releasing'&&this.phaseTime>.10){
        this.velocity.y-=9.81*dt;this.target.addScaledVector(this.velocity,dt);
        if(this.target.y<HUNT.radiusMetres){this.target.y=HUNT.radiusMetres;this.velocity.y=Math.abs(this.velocity.y)>.07?-this.velocity.y*.14:0;this.velocity.z*=.65;}
      }
      if(u>=1){this.phase=this.phase==='releasing'?'released':'tracking';this.phaseTime=0;this.lockTime=0;this.lastMove=this.time;}
      return;
    }
    if(this.phase==='released'){this.reach=this.rig.plan(this.target);this.rig.pose({aim:this.target,gaze:this.gaze,lean:0,time:this.time,attention:0});}
  }
  update(dt){
    if(!Number.isFinite(dt)||dt<0)return;
    if(dt===0){this._tick(0);return;}
    // Fixed contact checks preserve the fast catch even when rendering skips frames.
    let remaining=Math.min(dt,10);
    while(remaining>1e-9){const step=Math.min(remaining,HUNT.fixedStep);this._tick(step);remaining-=step;}
  }
  get status(){
    const messages={
      tracking:this.dragging?'Positioning target':!this.reach.reachable?this.reach.reason:this.armed?'Acquiring target…':'Following target',
      locked:'Target locked',leaning:'Leaning in slowly',striking:'Committed strike',
      retracting:'Grip acquired · pulling in',holding:'Target held',recovering:this.recoveryMessage||'Recovering',
      releasing:'Releasing target',released:'Released · reset to try again',
    };
    return {phase:this.phase,message:messages[this.phase],reachable:this.reach.reachable,lockProgress:this.lockProgress,
      holding:this.holding,busy:this.busy,lean:this.lean,captures:this.captures,misses:this.misses,
      target:this.target.toArray(),placement:{...this.placement},autoStrike:this.autoStrike,
      progress:['holding','retracting'].includes(this.phase)?1:this.phase==='striking'?.85:this.phase==='leaning'?.2+.6*clamp(this.phaseTime/(this.activeLeanSeconds||this.leanSeconds),0,1):.2*this.lockProgress};
  }
}
