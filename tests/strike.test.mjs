import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import * as THREE from 'three';
import { STRIKE, sampleStrike, strikePhase } from '../src/strike-motion.js';
import { readRig } from './glb-rig.mjs';
const rig = readRig();
const deg = THREE.MathUtils.radToDeg;
const elev = v => deg(Math.atan2(v.y, v.z));
const rest = { coxa: deg(Math.atan2(-1.08,-.60)), femur: deg(Math.atan2(.30,1.51)), gape: 180 + deg(Math.atan2(-.46,-1.05)) - deg(Math.atan2(.30,1.51)) };
function sample(t, side = 'L') {
  rig.pose('Attack', t);
  const base = rig.point(`Fore_${side}_Coxa`), elbow = rig.point(`Fore_${side}_Femur`), wrist = rig.point(`Fore_${side}_Tibia`), end = rig.point(`Fore_${side}_Tarsus`);
  const femur = wrist.clone().sub(elbow), tibia = end.clone().sub(wrist);
  return { base, elbow, wrist, end, femur, tibia, femurElevation: elev(femur), gape: deg(femur.clone().negate().angleTo(tibia)), head: rig.point('Head'), femurRotation: rig.joint(`Fore_${side}_Femur`).quaternion.clone() };
}

test('strike has a cocked approach, not a straight-arm punch', () => {
  const approach = sample(STRIKE.approachEnd), catchPose = sample(STRIKE.closeEnd);
  assert(approach.femurElevation > 95 && approach.femurElevation < 112);
  assert(catchPose.femurElevation > 40 && catchPose.femurElevation < 60, 'femur remains bent/upward at capture, not horizontal');
  assert(approach.wrist.y > approach.head.y, 'loaded femur is raised above the head');
  const before = sample(STRIKE.setEnd), after = sample(STRIKE.approachEnd);
  assert(before.femurRotation.angleTo(after.femurRotation) < .002, 'approach is primarily coxal movement, not premature femur extension');
});

test('fast femoral sweep and tibial closure overlap in the real GLB', () => {
  const a = sample(.511), b = sample(.526), c = sample(STRIKE.closeEnd);
  assert(b.wrist.z > a.wrist.z + .004, 'wrist must still advance during closure');
  assert(b.gape < a.gape - 25, 'tibia closes while the femur is advancing');
  assert(a.femurElevation > b.femurElevation + 20, 'rapid femoral depression drives the sweep');
  assert(c.gape >= 15 && c.gape <= 25, 'closed gripper retains a small prey space');
  assert(STRIKE.sweepEnd - STRIKE.sweepStart <= .031);
  assert(STRIKE.closeEnd - STRIKE.closeStart <= .035);
});

test('grippers stay closed and pull back towards the mouth instead of holding a reach', () => {
  const capture = sample(STRIKE.closeEnd), feeding = sample(STRIKE.retractEnd);
  const heldPoint = s => s.elbow.clone().lerp(s.wrist,.65).lerp(s.end,.16);
  assert(capture.wrist.z - feeding.wrist.z > .011, 'closed forelegs retract promptly');
  assert(heldPoint(feeding).distanceTo(feeding.head) < heldPoint(capture).distanceTo(capture.head) * .55, 'grasped region is brought toward the head');
  for(let t=STRIKE.closeEnd;t<=STRIKE.holdEnd;t+=1/120){const s=sample(t);assert(Math.abs(s.gape-20)<2,'no reopening during retraction');}
});

test('foreleg rotations remain in hinge planes; no backward flip, roll or hyperextension', () => {
  for (let t = 0; t <= STRIKE.duration; t += 1 / 120) {
    for (const side of ['L','R']) {
      const p = sample(t, side);
      assert(p.gape >= 8 && p.gape <= 130, `invalid gape at ${t}: ${p.gape}`);
      for (const segment of ['Coxa','Femur','Tibia','Tarsus']) {
        const q = rig.joint(`Fore_${side}_${segment}`).quaternion;
        assert(Math.abs(q.y) < 1e-6 && Math.abs(q.z) < 1e-6, `${segment} leaves its hinge plane`);
      }
    }
  }
});

test('leg lengths are rigid and all four support feet stay planted during the lunge', () => {
  const initial = sample(0);
  const lengths = [initial.base.distanceTo(initial.elbow),initial.elbow.distanceTo(initial.wrist),initial.wrist.distanceTo(initial.end)];
  const names = ['Middle_L','Middle_R','Hind_L','Hind_R'].map(n=>`${n}_Tarsus`);
  const anchors = names.map(n=>rig.point(n));
  for (let t=0;t<=STRIKE.duration;t+=1/120) {
    const p=sample(t), current=[p.base.distanceTo(p.elbow),p.elbow.distanceTo(p.wrist),p.wrist.distanceTo(p.end)];
    current.forEach((length,i)=>assert(Math.abs(length-lengths[i])<1e-7,'no rubber limb scaling'));
    names.forEach((n,i)=>assert(rig.point(n).distanceTo(anchors[i])<.00001,'planted supporting foot drift'));
  }
});

test('tarsi are folded away and attack retains high-rate keyframes after export', () => {
  rig.pose('Attack',STRIKE.approachEnd);
  assert(Math.abs(rig.joint('Fore_L_Tarsus').quaternion.x) > .8,'long walking foot must fold, not act as the catcher');
  const track = rig.clips.find(c=>c.name==='Attack').tracks.find(t=>t.name==='Fore_L_Tibia.quaternion');
  const times=[...track.times].filter(t=>t>=.495&&t<=.55);
  assert(times.length>=12,'240 Hz capture interval lost during export');
  for(let i=1;i<times.length;i++)assert(times[i]-times[i-1]<.00418);
});

test('reference phase curves recover to the exact rest pose', () => {
  const begin=sampleStrike(0,rest),end=sampleStrike(STRIKE.duration,rest);
  for(const key of ['coxa','femur','gape','tarsusFold','headPitch','abdomenPitch'])assert(Math.abs(begin[key]-end[key])<1e-9);
  assert.equal(strikePhase(.505),'SWEEP');
  assert.equal(strikePhase(.520),'SWEEP + CLAMP');
  assert.equal(strikePhase(.65),'PULL TO MOUTH');
  assert.equal(strikePhase(1),'HOLD');
});

test('Approved Idle and Attack bone keyframes are unchanged by the gait / stance update', () => {
  const expected=JSON.parse(fs.readFileSync(new URL('./fixtures/unaffected-clip-hashes.json',import.meta.url)));
  for(const name of ['Idle','Attack']){
    const animation=rig.gltf.animations.find(a=>a.name===name),hash=crypto.createHash('sha256');
    for(const channel of animation.channels.filter(c=>c.target.path!=='weights')){
      hash.update(rig.gltf.nodes[channel.target.node].name+':'+channel.target.path);
      const sampler=animation.samplers[channel.sampler];
      for(const i of [sampler.input,sampler.output]){const floats=rig.accessor(i);hash.update(Buffer.from(floats.buffer,floats.byteOffset,floats.byteLength));}
    }
    assert.equal(hash.digest('hex'),expected[name],`${name} changed outside the requested scope`);
  }
});
