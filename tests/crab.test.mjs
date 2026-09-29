import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import * as THREE from 'three';
import {validateBytes} from 'gltf-validator';
import {readRig} from './glb-rig.mjs';
import {CrabRig} from '../src/crab/rig.js';
import {CrabController,TARGET_RADIUS} from '../src/crab/controller.js';
import {CRAB,gaitFoot} from '../src/crab/anatomy.js';
const file=new URL('../public/models/crab.glb',import.meta.url);
const make=()=>{const data=readRig(file),rig=new CrabRig(data.root);return {data,rig,controller:new CrabController(rig)};};
const advance=(c,t)=>{for(let i=0;i<Math.ceil(t*240);i++)c.update(1/240);};
test('crab exports as valid self-contained glTF with five clips and ten appendages',async()=>{
 const bytes=fs.readFileSync(file),report=await validateBytes(new Uint8Array(bytes),{maxIssues:100});assert.equal(report.issues.numErrors,0,JSON.stringify(report.issues));assert.equal(report.issues.numWarnings,0);
 const {data,rig}=make();assert.equal(rig.legs.length,8);assert.equal(rig.claws.length,2);assert.equal(Object.keys(rig.bones).length,59);assert.deepEqual(data.clips.map(c=>c.name),['Idle','Walk_Left','Walk_Right','Pinch_L','Pinch_R']);
 const root=data.gltf.nodes.find(n=>n.name==='Carcinus_maenas');assert.deepEqual(root.scale,[.01,.01,.01]);assert.equal(root.extras.anterolateralTeethPerSide,5);assert.equal(root.extras.frontalLobes,3);assert(data.gltf.images.every(i=>i.bufferView!==undefined&&!i.uri));
});
test('sideways gait has alternating sets of four legs, no forward translation or heading change',()=>{
 const {data,rig}=make();for(const name of ['Walk_Left','Walk_Right']){
  let sawFour=false;for(let f=0;f<192;f++){
   data.pose(name,f/120);const air=rig.legs.filter(l=>data.point(`${l.name}_Tip`).y>.0001);assert(air.length<=4);if(air.length===4)sawFour=true;
   assert(Math.abs(rig.bones.Root.position.z)<1e-9);assert(rig.bones.Root.quaternion.angleTo(new THREE.Quaternion())<1e-6);
  }assert(sawFour);
 }
});
test('stance feet remain fixed when the specified lateral controller velocity is added',()=>{
 const {data,rig}=make();let count=0;
 for(const direction of [-1,1])for(let f=2;f<185;f+=4){const t=f/120,dt=1/120,name=direction<0?'Walk_Left':'Walk_Right';
  for(const l of rig.legs){const a=gaitFoot(t,l,direction),b=gaitFoot(t+dt,l,direction);if(a.swing||b.swing)continue;
   data.pose(name,t);const p=data.point(`${l.name}_Tip`);p.x+=direction*CRAB.walkSpeed*t;
   data.pose(name,t+dt);const q=data.point(`${l.name}_Tip`);q.x+=direction*CRAB.walkSpeed*(t+dt);
   assert(p.distanceTo(q)<5e-8,`${l.name} slides`);assert(Math.abs(p.y-.00003)<3e-8);count++;
  }
 }assert(count>100);
});
test('only the movable finger opens; the lower finger is rigidly part of the propodus',()=>{
 const {data}=make();for(const side of ['L','R']){
  data.pose(`Pinch_${side}`,0);const fixed=data.point(`Chel_${side}_FixedTip`),moving=data.point(`Chel_${side}_MovingTip`);
  data.pose(`Pinch_${side}`,.90);assert(fixed.distanceTo(data.point(`Chel_${side}_FixedTip`))<1e-9);assert(moving.distanceTo(data.point(`Chel_${side}_MovingTip`))>.008);
 }
});
test('both pincers can capture, lift, hold, release and reset the test bead',()=>{
 for(const x of [-25,25]){const {rig,controller:c}=make();c.setPlacement({x,y:20,z:54});assert(c.plan.valid);assert.equal(c.plan.claw.side,x<0?'L':'R');const before=c.target.clone();assert(c.grab());advance(c,1.5);assert.equal(c.state,'hold');assert(c.lastContact.hit);assert.equal(c.catches,1);assert(c.target.y>before.y+.003);assert(rig.contact(c.activePlan,c.target).hit);c.release();advance(c,.8);assert.equal(c.state,'released');assert(Math.abs(c.target.y-TARGET_RADIUS)<1e-7);c.reset();assert.equal(c.state,'ready');assert(c.target.distanceTo(before)<1e-9);}
});
test('a moved target can miss, and out-of-reach targets never attach remotely',()=>{
 const {controller:c}=make();c.grab();advance(c,.72);assert.equal(c.state,'close');c.setPlacement({x:-65,y:38,z:80});advance(c,.8);assert.equal(c.catches,0);assert.equal(c.misses,1);assert(!c.lastContact.hit);assert(!c.plan.valid);assert(!c.grab());
});
test('controller keeps all supporting feet planted and all limb scales unchanged',()=>{
 const {rig,controller:c}=make();rig.poseClip('Idle',0);const anchors=rig.legs.map(l=>rig.bones[`${l.name}_Tip`].getWorldPosition(new THREE.Vector3()));c.grab();
 for(let i=0;i<360;i++){c.update(1/240);rig.legs.forEach((l,j)=>assert(rig.bones[`${l.name}_Tip`].getWorldPosition(new THREE.Vector3()).distanceTo(anchors[j])<1e-8));for(const b of Object.values(rig.bones))assert.deepEqual(b.scale.toArray(),[1,1,1]);}
});
test('all crab clips return seamlessly to their first pose',()=>{const {data}=make();for(const clip of data.clips)for(const track of clip.tracks){const n=track.getValueSize();for(let i=0;i<n;i++)assert(Math.abs(track.values[i]-track.values[track.values.length-n+i])<1e-6,clip.name+': '+track.name);}});

test('unchanged or non-finite placements do not re-arm automatic capture',()=>{
 const {controller:c}=make();const version=c.version,position=c.target.clone();
 c.setPlacement({...c.placement});c.setPlacement({x:NaN,y:Infinity,z:'not a distance'});
 assert.equal(c.version,version);assert(c.target.equals(position));
 c.setPlacement({x:c.placement.x+1});assert.equal(c.version,version+1);
});
test('invalid pincer selections leave the current plan and selection intact',()=>{
 const {controller:c}=make();c.selectSide('R');const plan=c.plan;
 for(const side of ['',null,'both','right']){c.selectSide(side);assert.equal(c.side,'R');assert.equal(c.plan,plan);}
 assert(c.grab());advance(c,1.5);assert.equal(c.state,'hold');
});
