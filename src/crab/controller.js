import * as THREE from 'three';
import {V,rad,smooth,clamp} from './anatomy.js';
export const TARGET_RADIUS=.0028;
export class CrabController {
  constructor(rig){this.rig=rig;this.placement={x:25,y:20,z:54};this.side='auto';this.target=V(.025,.020,.054);this.state='ready';this.time=0;this.elapsed=0;this.catches=0;this.misses=0;this.plan=rig.plan(this.target,this.side,TARGET_RADIUS);this.auto=false;this.version=0;this.usedVersion=-1;this.changedAt=0;this.dragging=false;this.offset=V();this.velocity=V();}
  get held(){return this.state==='lift'||this.state==='hold';}
  get busy(){return ['reach','close','lift','release','recover'].includes(this.state);}
  setPlacement(p){if(this.held||this.state==='release')return false;for(const key of ['x','y','z'])if(p[key]!==undefined){const n=Number(p[key]);if(Number.isFinite(n))this.placement[key]=clamp(n,...({x:[-65,65],y:[10,38],z:[40,80]}[key]));}this.target.set(this.placement.x/1000,this.placement.y/1000,this.placement.z/1000);this.version++;this.changedAt=this.time;this.plan=this.rig.plan(this.target,this.side,TARGET_RADIUS);if(this.state==='released')this.state='ready';return true;}
  setTarget(p){return this.setPlacement({x:p.x*1000,y:p.y*1000,z:p.z*1000});}
  selectSide(side){if(this.busy||this.held)return;this.side=side;this.plan=this.rig.plan(this.target,this.side,TARGET_RADIUS);}
  reset(){this.target.set(this.placement.x/1000,this.placement.y/1000,this.placement.z/1000);this.state='ready';this.elapsed=0;this.plan=this.rig.plan(this.target,this.side,TARGET_RADIUS);this.version++;this.usedVersion=-1;this.changedAt=this.time;this.dragging=false;this.rig.poseClip('Idle',this.time);}
  grab(){if(this.held){this.release();return true;}if(this.state==='released'){this.reset();return true;}if(this.busy)return false;const plan=this.rig.plan(this.target,this.side,TARGET_RADIUS);if(!plan.valid){this.plan=plan;return false;}this.activePlan=plan;this.committed=this.target.clone();this.state='reach';this.elapsed=0;this.usedVersion=this.version;return true;}
  release(){if(!this.held)return;this.state='release';this.elapsed=0;this.velocity.set(0,0,.003);this.releasePlan=this.activePlan;}
  step(dt){
    this.time+=dt;this.elapsed+=dt;
    if(this.state==='ready'){
      this.plan=this.rig.plan(this.target,this.side,TARGET_RADIUS);this.rig.poseClip('Idle',this.time);
      if(this.auto&&!this.dragging&&this.time-this.changedAt>.45&&this.version!==this.usedVersion&&this.plan.valid)this.grab();
    }else if(this.state==='reach'){
      const p=smooth(0,.65,this.elapsed);this.rig.poseGrasp(this.activePlan,p,rad(11+53*p),this.time);
      if(p>=1){this.state='close';this.elapsed=0;}
    }else if(this.state==='close'){
      const p=smooth(0,.28,this.elapsed);this.rig.poseGrasp(this.activePlan,1,THREE.MathUtils.lerp(rad(64),this.activePlan.grip.angle,p),this.time);
      if(p>=1){const contact=this.rig.contact(this.activePlan,this.target);this.lastContact=contact;
        if(contact.hit){this.offset.copy(this.target).sub(this.rig.gripWorld(this.activePlan));this.captured=this.target.clone();this.liftTo=this.target.clone().add(V(0,.006,-.003));const test=this.rig.plan(this.liftTo,this.activePlan.claw.side,TARGET_RADIUS);if(!test.valid)this.liftTo.copy(this.target);this.state='lift';this.elapsed=0;this.catches++;}
        else{this.state='recover';this.elapsed=0;this.misses++;this.usedVersion=this.version;}
      }
    }else if(this.state==='lift'||this.state==='hold'){
      const p=this.state==='hold'?1:smooth(0,.42,this.elapsed),point=this.captured.clone().lerp(this.liftTo,p),plan=this.rig.plan(point,this.activePlan.claw.side,TARGET_RADIUS);
      if(plan.valid)this.activePlan=plan;
      this.rig.poseGrasp(this.activePlan,1,this.activePlan.grip.angle,this.time);this.target.copy(this.rig.gripWorld(this.activePlan)).add(this.offset);
      if(p>=1)this.state='hold';
    }else if(this.state==='release'){
      const opening=smooth(0,.18,this.elapsed);this.rig.poseGrasp(this.releasePlan,1-smooth(.22,.62,this.elapsed),THREE.MathUtils.lerp(THREE.MathUtils.lerp(this.releasePlan.grip.angle,rad(67),opening),rad(11),smooth(.35,.62,this.elapsed)),this.time);
      if(this.elapsed>.17){this.velocity.y-=9.81*dt;this.target.addScaledVector(this.velocity,dt);if(this.target.y<TARGET_RADIUS){this.target.y=TARGET_RADIUS;this.velocity.set(0,0,0);}}
      if(this.elapsed>.64){this.state='released';this.rig.poseClip('Idle',this.time);}
    }else if(this.state==='recover'){
      const p=smooth(0,.48,this.elapsed);this.rig.poseGrasp(this.activePlan,1-p,rad(11),this.time);if(p>=1){this.state='ready';this.changedAt=this.time;}
    }else this.rig.poseClip('Idle',this.time);
  }
  update(dt){if(!Number.isFinite(dt)||dt<0)return;if(dt===0){this.step(0);return;}let left=Math.min(dt,10);while(left>1e-9){const step=Math.min(left,1/240);this.step(step);left-=step;}}
}
