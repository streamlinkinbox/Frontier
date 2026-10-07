import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {buildQuarry,disposeQuarry,seededRandom} from '../src/quarry.js';
const config={terraces:3,height:7,width:8,radius:24,fracture:65,debris:true,seed:28491,geology:'limestone'};
test('seeded generator is reproducible',()=>{const a=seededRandom(42),b=seededRandom(42);for(let i=0;i<100;i++)assert.equal(a(),b());});
test('quarry has deterministic finite geometry and no texture maps',()=>{
  const a=buildQuarry(config),b=buildQuarry(config);
  assert.equal(a.userData.depth,21);assert.ok(a.userData.rockCount>1000);assert.equal(a.userData.rockCount,b.userData.rockCount);
  a.children.forEach((o,i)=>{
    assert.deepEqual(o.geometry.attributes.position.array,b.children[i].geometry.attributes.position.array);
    assert.ok(o.geometry.attributes.position.array.every(Number.isFinite));
    assert.ok(o.geometry.attributes.normal.array.every(Number.isFinite));
    for(const key of ['map','normalMap','displacementMap','bumpMap','roughnessMap'])assert.equal(o.material[key],null);
    if(o.isInstancedMesh)assert.deepEqual(o.instanceMatrix.array,b.children[i].instanceMatrix.array);
  });disposeQuarry(a);disposeQuarry(b);
});
test('families change base geometry, rock templates AND placement with the same seed',()=>{
  const models=['limestone','sandstone','slate'].map(geology=>buildQuarry({...config,geology}));
  for(let i=0;i<models.length;i++)for(let j=i+1;j<models.length;j++){
    assert.notDeepEqual(models[i].children[0].geometry.attributes.position.array,models[j].children[0].geometry.attributes.position.array);
    assert.notDeepEqual(models[i].children[1].geometry.attributes.position.array,models[j].children[1].geometry.attributes.position.array);
    assert.notDeepEqual(models[i].children[1].instanceMatrix.array,models[j].children[1].instanceMatrix.array);
  }
  models.forEach(disposeQuarry);
});
test('debris does not change the base or attached rock dressing',()=>{
  const a=buildQuarry(config),b=buildQuarry({...config,debris:false});
  assert.ok(a.userData.rockCount>b.userData.rockCount);assert.equal(a.userData.faceRockCount,b.userData.faceRockCount);assert.equal(b.userData.debrisCount,0);
  assert.deepEqual(a.children[0].geometry.attributes.position.array,b.children[0].geometry.attributes.position.array);
  b.children.slice(1).forEach((o,i)=>{assert.deepEqual(o.instanceMatrix.array,a.children[i+1].instanceMatrix.array.slice(0,o.instanceMatrix.array.length));});
  [a,b].forEach(disposeQuarry);
});
test('bench surfaces face upward, floor downward only on the closed bottom',()=>{
  const q=buildQuarry(config),g=q.children[0].geometry;
  q.userData.surfaceRanges.filter(r=>r.kind==='bench').forEach(r=>{for(let i=r.start;i<r.start+r.count;i++)assert.ok(g.attributes.normal.getY(i)>.99);});
  assert.ok(g.attributes.normal.getY(0)>.99);assert.ok(g.attributes.normal.getY(g.attributes.normal.count-1)<-.99);
  disposeQuarry(q);
});
test('base cliffs have actual shaped relief, not straight extruded risers',()=>{
  const q=buildQuarry(config),p=q.children[0].geometry.attributes.position;
  const r=q.userData.surfaceRanges.find(r=>r.kind==='cliff');
  // One angular column through the face should not lie on a single straight line.
  const column=[];for(let i=r.start;i<r.start+r.count;i+=192*6)column.push(new THREE.Vector3(p.getX(i),p.getY(i),p.getZ(i)));
  const start=column[0],end=column.at(-1),line=end.clone().sub(start).normalize();
  assert.ok(column.some(v=>v.clone().sub(start).cross(line).length()>.12));disposeQuarry(q);
});
test('fracture varies dressing without breaking minimum settings',()=>{
  for(const geology of ['limestone','sandstone','slate']){
    const a=buildQuarry({...config,geology,terraces:2,height:3,width:4,fracture:10,debris:false});
    const b=buildQuarry({...config,geology,terraces:2,height:3,width:4,fracture:100,debris:false});
    assert.ok(b.userData.faceRockCount>a.userData.faceRockCount);
    for(const q of [a,b]){q.children.forEach(o=>assert.ok(o.geometry.attributes.position.array.every(Number.isFinite)));disposeQuarry(q);}
  }
});

test('different seeds change the base cliff silhouette',()=>{
  const a=buildQuarry(config),b=buildQuarry({...config,seed:992});
  assert.notDeepEqual(a.children[0].geometry.attributes.position.array,b.children[0].geometry.attributes.position.array);
  [a,b].forEach(disposeQuarry);
});
