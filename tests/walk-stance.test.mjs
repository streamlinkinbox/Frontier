import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import { WALK, sampleWalkingFoot } from '../src/walk-motion.js';
import { STANCE } from '../src/stance-motion.js';
import { readRig } from './glb-rig.mjs';
const rig=readRig();
const feet=['Middle_L','Middle_R','Hind_L','Hind_R'];

test('slow walk advances hind-to-middle, alternating body sides, with three supports',()=>{
  WALK.order.forEach((name,i)=>{
    const time=(i*.25+.1)*WALK.duration;
    rig.pose('Walk',time);
    const raised=feet.filter(n=>rig.point(`${n}_Tarsus`).y>.0021);
    assert.deepEqual(raised,[name]);
  });
  for(let f=0;f<288;f++){
    const time=f/120;rig.pose('Walk',time);
    assert(feet.filter(n=>rig.point(`${n}_Tarsus`).y>.001705).length<=1);
  }
});

test('planted feet are stationary when the declared forward controller speed is applied',()=>{
  let checked=0;
  for(let f=1;f<285;f+=3){
    const t=f/120,dt=1/120;
    for(const leg of feet){
      const a=sampleWalkingFoot(t,leg),b=sampleWalkingFoot(t+dt,leg);
      if(a.phase<.23||b.phase>.98||a.swing||b.swing)continue;
      rig.pose('Walk',t);const before=rig.point(`${leg}_Tarsus`);before.z+=WALK.speedMetresPerSecond*t;
      rig.pose('Walk',t+dt);const after=rig.point(`${leg}_Tarsus`);after.z+=WALK.speedMetresPerSecond*(t+dt);
      assert(before.distanceTo(after)<.00000003,`foot sliding: ${leg} at ${t}`);
      assert(Math.abs(before.y-.0017)<.00000002);
      checked++;
    }
  }
  assert(checked>100);
});

test('lift-off and touchdown match planted-foot velocity instead of stopping and slipping',()=>{
  const eps=1e-5, expected=-WALK.speedMetresPerSecond*100;
  for(const t of [0,WALK.duration*WALK.swingFraction]){
    const at=sampleWalkingFoot(t,'Hind_L'),before=sampleWalkingFoot(t-eps,'Hind_L'),after=sampleWalkingFoot(t+eps,'Hind_L');
    assert(Math.abs((at.offsetZCm-before.offsetZCm)/eps-expected)<.0001);
    assert(Math.abs((after.offsetZCm-at.offsetZCm)/eps-expected)<.0001);
    assert(Math.abs(at.liftCm)<1e-10);
    assert(Math.abs((after.liftCm-before.liftCm)/(2*eps))<1e-5);
  }
});

test('walking carries the grasping forelegs folded without an artificial arm swing',()=>{
  let reference;
  for(let f=0;f<=288;f+=12){
    rig.pose('Walk',f/120);
    const q=rig.joint('Fore_L_Femur').quaternion.clone().normalize();
    if(!reference)reference=q;else assert(reference.angleTo(q)<1e-6);
    const elbow=rig.point('Fore_L_Femur'),wrist=rig.point('Fore_L_Tibia'),end=rig.point('Fore_L_Tarsus');
    const gape=THREE.MathUtils.radToDeg(elbow.sub(wrist).angleTo(end.sub(wrist)));
    assert(gape<23);
  }
});

test('Stance raises the body and forelegs, exposing the inner coxal markings',()=>{
  rig.pose('Idle',0);const idleHead=rig.point('Head');
  rig.pose('Stance',0);const head=rig.point('Head');
  assert(head.y-idleHead.y>.006,'prothorax should rear up');
  for(const [s,side] of [[-1,'L'],[1,'R']]){
    const wrist=rig.point(`Fore_${side}_Tibia`),end=rig.point(`Fore_${side}_Tarsus`);
    assert(wrist.y>head.y+.007,'forelegs should be raised, not reaching horizontally');
    assert(s*end.x>.020,'forelegs should spread out from the head');
    const inner=new THREE.Vector3(-s,0,0).transformDirection(rig.joint(`Fore_${side}_Coxa`).matrixWorld);
    assert(inner.z>.65,'inner coxal markings should face the threat');
  }
});

test('held Stance keeps all four supporting feet planted and remains loopable',()=>{
  rig.pose('Idle',0);const anchors=feet.map(n=>rig.point(`${n}_Tarsus`));
  for(let f=0;f<=360;f+=6){
    rig.pose('Stance',f/60);
    feet.forEach((n,i)=>assert(rig.point(`${n}_Tarsus`).distanceTo(anchors[i])<.00000004));
  }
  const clip=rig.clips.find(c=>c.name==='Stance');assert.equal(clip.duration,STANCE.duration);
  for(const track of clip.tracks){const n=track.getValueSize();const a=track.values.slice(0,n),b=track.values.slice(-n);assert(Math.hypot(...a.map((v,i)=>v-b[i]))<1e-6);}
});

test('wing fans are actual GLB morph targets, animated only in Stance',()=>{
  const node=rig.gltf.nodes.find(n=>n.name==='Hindwing_membrane');assert(node);
  const mesh=rig.gltf.meshes[node.mesh];assert(mesh.primitives[0].targets[0].POSITION!==undefined);
  for(const animation of rig.gltf.animations){
    const track=animation.channels.find(c=>c.target.path==='weights');assert(track,animation.name);
    const values=rig.accessor(animation.samplers[track.sampler].output);
    if(animation.name==='Stance'){assert(Math.min(...values)>.97);assert(Math.max(...values)>.99);}
    else assert(values.every(v=>v===0),'wing fans must reset outside Stance');
  }
  rig.pose('Stance',0);
  for(const side of ['L','R'])assert(rig.joint(`Tegmen_${side}`).quaternion.angleTo(new THREE.Quaternion())>.8);
});
