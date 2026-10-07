import test from 'node:test';
import assert from 'node:assert/strict';
import {buildMountain} from '../src/mountain.js';
import {disposeQuarry} from '../src/quarry.js';
const config={mode:'mountain',cliffShape:'spire',meshStyle:'detailed',mountainHeight:100,mountainRadius:72,ridges:5,steepness:78,fracture:65,seed:28491,geology:'limestone',debris:true};
function positions(q){return q.children[0].geometry.attributes.position.array;}
test('mountain is a positive solid with a closed summit, not a quarry pit',()=>{
  const q=buildMountain(config),p=q.children[0].geometry.attributes.position;
  let max=-Infinity,min=Infinity,top=[];
  for(let i=0;i<p.count;i++){const y=p.getY(i);min=Math.min(min,y);max=Math.max(max,y);if(y===100)top.push([p.getX(i),p.getZ(i)]);}
  assert.equal(max,100);assert.equal(min,-3);assert.ok(top.length>0);
  for(const [x,z] of top){assert.ok(Math.abs(x-q.userData.summit.x)<.001);assert.ok(Math.abs(z-q.userData.summit.z)<.001);}
  assert.equal(q.userData.mode,'mountain');assert.equal(q.userData.ridges,5);assert.ok(q.userData.faceRockCount>500);
  assert.ok(q.children[0].geometry.attributes.normal.getY(p.count-1)<-.99);
  disposeQuarry(q);
});
test('all families are deterministic, finite and texture-free',()=>{
  for(const geology of ['limestone','sandstone','slate']){
    const a=buildMountain({...config,geology}),b=buildMountain({...config,geology});
    a.children.forEach((o,i)=>{
      for(const attr of ['position','normal','color'])assert.ok(o.geometry.attributes[attr].array.every(Number.isFinite));
      assert.deepEqual(o.geometry.attributes.position.array,b.children[i].geometry.attributes.position.array);
      if(o.isInstancedMesh){assert.ok(o.instanceMatrix.array.every(Number.isFinite));assert.deepEqual(o.instanceMatrix.array,b.children[i].instanceMatrix.array);}
      for(const key of ['map','normalMap','displacementMap','bumpMap'])assert.equal(o.material[key],null);
    });[a,b].forEach(disposeQuarry);
  }
});
test('geology changes both mountain base and rock mesh',()=>{
  const models=['limestone','sandstone','slate'].map(geology=>buildMountain({...config,geology}));
  for(let i=0;i<3;i++)for(let j=i+1;j<3;j++){
    assert.notDeepEqual(positions(models[i]),positions(models[j]));
    assert.notDeepEqual(models[i].children[1].geometry.attributes.position.array,models[j].children[1].geometry.attributes.position.array);
  }models.forEach(disposeQuarry);
});
test('seed and every mountain structure control affect the base',()=>{
  const a=buildMountain(config);
  for(const change of [{seed:92},{mountainHeight:125},{mountainRadius:90},{ridges:8},{steepness:40}]){
    const b=buildMountain({...config,...change});assert.notDeepEqual(positions(a),positions(b));disposeQuarry(b);
  }disposeQuarry(a);
});
test('talus toggle preserves base and attached dressing',()=>{
  const a=buildMountain(config),b=buildMountain({...config,debris:false});
  assert.deepEqual(positions(a),positions(b));assert.equal(b.userData.debrisCount,0);assert.ok(a.userData.debrisCount>1000);assert.equal(a.userData.faceRockCount,b.userData.faceRockCount);
  b.children.slice(1).forEach((o,i)=>assert.deepEqual(o.instanceMatrix.array,a.children[i+1].instanceMatrix.array.slice(0,o.instanceMatrix.array.length)));
  [a,b].forEach(disposeQuarry);
});
test('minimum and maximum settings yield finite geometry',()=>{
  for(const change of [
    {mountainHeight:45,mountainRadius:40,ridges:3,steepness:35,fracture:10,geology:'sandstone'},
    {mountainHeight:180,mountainRadius:110,ridges:9,steepness:95,fracture:100,geology:'slate'}
  ]){
    const q=buildMountain({...config,...change});q.children.forEach(o=>{assert.ok(o.geometry.attributes.position.array.every(Number.isFinite));if(o.isInstancedMesh)assert.ok(o.instanceMatrix.array.every(Number.isFinite));});disposeQuarry(q);
  }
});
