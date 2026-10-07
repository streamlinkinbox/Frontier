import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {buildMountain} from '../src/mountain.js';
import {buildQuarry,disposeQuarry} from '../src/quarry.js';
const config={mountainHeight:100,mountainRadius:72,ridges:5,steepness:78,fracture:65,seed:28491,geology:'sandstone',distortion:100,elongation:100,detail:35,debris:true};
function checkBoulders(q){
  const matrix=new THREE.Matrix4(),scale=new THREE.Vector3(),position=new THREE.Vector3(),rotation=new THREE.Quaternion();
  for(const mesh of q.children.filter(o=>o.isInstancedMesh)){
    const ext=mesh.geometry.userData.extent;
    for(let i=0;i<mesh.count;i++){
      mesh.getMatrixAt(i,matrix);matrix.decompose(position,rotation,scale);
      const dimensions=scale.toArray().map((v,j)=>v*ext[j]);
      assert.ok(Math.max(...dimensions)/Math.min(...dimensions)<=2.60001,'World-space boulder must not become a thin shelf');
      assert.ok(matrix.elements.every(Number.isFinite));
    }
  }
}
test('default escarpment has an extended crest instead of a single apex',()=>{
  const q=buildMountain(config),p=q.children[0].geometry.attributes.position;
  const upper=[];for(let i=0;i<p.count;i++)if(p.getY(i)>70)upper.push(p.getX(i));
  assert.equal(q.userData.cliffShape,'escarpment');
  assert.ok(Math.max(...upper)-Math.min(...upper)>100,'crest spans a substantial part of the cliff');
  assert.ok(p.array.every(Number.isFinite));
  const other=buildMountain({...config,cliffShape:'spire'});assert.notDeepEqual(p.array,other.children[0].geometry.attributes.position.array);
  [q,other].forEach(disposeQuarry);
});
test('sandstone proportions are bounded after placement in both styles and landforms',()=>{
  for(const meshStyle of ['lowpoly','detailed']){
    const mountain=buildMountain({...config,meshStyle});checkBoulders(mountain);disposeQuarry(mountain);
    const quarry=buildQuarry({...config,meshStyle,terraces:3,height:7,width:8,radius:24});checkBoulders(quarry);disposeQuarry(quarry);
  }
});
test('escarpment debris toggle preserves attached rocks',()=>{
  const a=buildMountain(config),b=buildMountain({...config,debris:false});
  assert.deepEqual(a.children[0].geometry.attributes.position.array,b.children[0].geometry.attributes.position.array);
  assert.equal(a.userData.faceRockCount,b.userData.faceRockCount);
  for(let i=1;i<b.children.length;i++)assert.deepEqual(b.children[i].instanceMatrix.array,a.children[i].instanceMatrix.array.slice(0,b.children[i].instanceMatrix.array.length));
  [a,b].forEach(disposeQuarry);
});
