import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {readRig} from './glb-rig.mjs';
import {HuntRig} from '../src/hunt-rig.js';
import {HuntSimulation,placementToWorld,boundPlacement,worldToPlacement} from '../src/hunt-simulation.js';
import {HUNT} from '../src/hunt-config.js';
const make=()=>{const {root}=readRig();const rig=new HuntRig(root);return {rig,sim:new HuntSimulation(rig)};};
function advance(sim,seconds){for(let t=0;t<seconds-1e-8;t+=1/120)sim.update(Math.min(1/120,seconds-t));}
function until(sim,phase,seconds=5){for(let i=0;i<seconds*240&&sim.phase!==phase;i++)sim.update(1/240);assert.equal(sim.phase,phase);}

test('controller is constrained to a forward arc and rejects non-finite input',()=>{
  assert.deepEqual(boundPlacement({angle:300,distance:-10,height:NaN}),{angle:30,distance:12,height:32});
  for(const angle of [-30,0,30])for(const distance of [12,20,36]){
    const p={angle,distance,height:32},v=placementToWorld(p),back=worldToPlacement(v);
    assert(v.z>.03);assert(Math.abs(back.angle-angle)<1e-8);assert(Math.abs(back.distance-distance)<1e-8);
  }
});

test('head tracks before lock; the slow lean precedes the short committed strike',()=>{
  const {rig,sim}=make();sim.setPlacement({angle:24});sim.requestStrike();
  sim.update(.1);assert.equal(sim.phase,'tracking');assert(sim.lean===0);
  until(sim,'leaning');assert(sim.lockProgress===1);assert(rig.aimError(sim.target)<.08);
  const start=sim.time;advance(sim,.7);assert.equal(sim.phase,'leaning');assert(sim.lean>.5&&sim.lean<1);
  until(sim,'striking');assert(sim.time-start>=sim.leanSeconds-.005);
  const strikeStart=sim.time;until(sim,'retracting');assert(sim.time-strikeStart<.065);
  assert(sim.lastContact.hit);assert.equal(sim.captures,1);
});

test('target-aware IK grips left, center and right at multiple heights without limb scaling',()=>{
  const {rig}=make();
  for(const angle of [-25,0,25])for(const height of [25,32,38]){
    const target=placementToWorld({angle,distance:20,height});const plan=rig.plan(target);
    assert(plan.reachable,`reachable placement ${angle}/${height}`);
    rig.pose({aim:target,lean:1,solutions:plan.solutions});
    const contact=rig.contact(target);assert(contact.hit);assert(contact.error<HUNT.contactToleranceMetres);
    for(const arm of rig.arms){
      for(const [a,b,length] of [['Coxa','Femur',arm.co.length()],['Femur','Tibia',arm.fe.length()],['Tibia','Tarsus',arm.ti.length()]])
        assert(Math.abs(rig.point(`${arm.name}_${a}`).distanceTo(rig.point(`${arm.name}_${b}`))-length*rig.scale)<1e-9);
      for(const part of ['Femur','Tibia','Tarsus']){
        const q=rig.bones[`${arm.name}_${part}`].quaternion;
        assert(Math.abs(q.y)<1e-8&&Math.abs(q.z)<1e-8,'femur/tibia must remain true hinges');
      }
    }
  }
});

test('moving the target during the slow lean cancels and re-acquires rather than teleporting the arms',()=>{
  const {sim}=make();sim.requestStrike();until(sim,'leaning');advance(sim,.75);
  const lean=sim.lean;sim.setPlacement({angle:-22});assert.equal(sim.phase,'recovering');assert.equal(sim.lean,lean);
  until(sim,'holding',6);assert.equal(sim.captures,1);assert.equal(sim.misses,0);
});

test('a target moved after commitment causes a miss, never a remote capture',()=>{
  const {sim}=make();sim.requestStrike();until(sim,'striking');
  const committed=sim.committed.clone();sim.setPlacement({angle:30,distance:36});
  assert(sim.committed.distanceTo(committed)<1e-12);
  advance(sim,.08);assert.equal(sim.phase,'recovering');assert.equal(sim.captures,0);assert.equal(sim.misses,1);
  assert(!sim.lastContact.hit);assert(sim.target.distanceTo(committed)>.015);
});

test('out-of-reach targets are tracked but cannot start an attack',()=>{
  const {sim}=make();sim.setPlacement({distance:36,height:43});advance(sim,1);
  assert(!sim.reach.reachable);assert.equal(sim.requestStrike(),false);
  sim.autoStrike=true;advance(sim,3);assert.equal(sim.captures,0);assert.equal(sim.phase,'tracking');
});

test('all four supporting feet stay planted through lock, lean, capture and retraction',()=>{
  const {rig,sim}=make(),names=['Middle_L','Middle_R','Hind_L','Hind_R'].map(s=>`${s}_Tarsus`);
  const anchors=names.map(n=>rig.point(n));sim.setPlacement({angle:-25});sim.requestStrike();
  for(let i=0;i<840;i++){
    sim.update(1/240);names.forEach((n,j)=>assert(rig.point(n).distanceTo(anchors[j])<1e-8,`${n} slides`));
  }
  assert.equal(sim.phase,'holding');
});

test('capture preserves target position, carries it towards the mouth and releases with gravity',()=>{
  const {rig,sim}=make();sim.requestStrike();until(sim,'striking');
  const before=sim.target.clone();until(sim,'retracting');assert(sim.target.distanceTo(before)<1e-12,'no snap to a scripted grip position');
  const startDistance=sim.target.distanceTo(rig.point('Head'));until(sim,'holding');
  assert(sim.target.distanceTo(rig.point('Head'))<startDistance);
  assert(sim.target.distanceTo(rig.gripCenter())<HUNT.contactToleranceMetres);
  const held=sim.target.clone();sim.release();advance(sim,.6);
  assert.equal(sim.phase,'released');assert(sim.target.y<held.y-.01);
  assert(Math.abs(sim.target.y-HUNT.radiusMetres)<.0001);
  sim.reset();assert.equal(sim.phase,'tracking');assert(sim.target.distanceTo(placementToWorld(sim.placement))<1e-9);
});

test('fixed-step contact checks are not skipped by a long render frame',()=>{
  const a=make(),b=make();a.sim.requestStrike();b.sim.requestStrike();
  advance(a.sim,2.6);b.sim.update(2.6);
  assert.equal(a.sim.phase,'holding');assert.equal(b.sim.phase,'holding');
  assert.equal(a.sim.captures,b.sim.captures);assert(a.sim.target.distanceTo(b.sim.target)<.000001);
});

test('automatic mode makes one attempt per stationary placement',()=>{
  const {sim}=make();sim.autoStrike=true;advance(sim,2.6);assert.equal(sim.phase,'holding');assert.equal(sim.captures,1);
  advance(sim,3);assert.equal(sim.captures,1);
  sim.release();advance(sim,2);assert.equal(sim.captures,1);assert.equal(sim.phase,'released');
});
