import test from 'node:test';
import assert from 'node:assert/strict';
import {buildMountain} from '../src/mountain.js';
import {makeRockTemplate,seededRandom,disposeQuarry,buildQuarry} from '../src/quarry.js';
const config={meshStyle:'lowpoly',geology:'sandstone',seed:28491,detail:35,distortion:65,elongation:65};
test('low-poly mesh is deterministic and substantially smaller than detailed',()=>{
 const a=buildMountain(config),b=buildMountain(config),d=buildMountain({...config,meshStyle:'detailed'});
 assert.ok(a.userData.baseTriangles<1000);assert.ok(a.userData.triangleCount<d.userData.triangleCount*.25);
 a.children.forEach((o,i)=>{assert.deepEqual(o.geometry.attributes.position.array,b.children[i].geometry.attributes.position.array);assert.ok(o.geometry.attributes.position.array.every(Number.isFinite));if(o.isInstancedMesh)assert.deepEqual(o.instanceMatrix.array,b.children[i].instanceMatrix.array);assert.equal(o.material.map,null);});[a,b,d].forEach(disposeQuarry);
});
test('facet detail changes resolution and low-poly works in quarry mode',()=>{
 const a=buildMountain({...config,detail:0}),b=buildMountain({...config,detail:100});assert.ok(a.userData.baseTriangles<b.userData.baseTriangles);
 const q=buildQuarry({...config,terraces:3,height:7,width:8,radius:24,fracture:65,debris:true});assert.ok(q.userData.rockCount>0);[a,b,q].forEach(disposeQuarry);
});
test('sandstone elongation and vertex distortion affect geometry',()=>{
 const a=makeRockTemplate('sandstone',seededRandom(2),0,{...config,elongation:0,distortion:0});
 const b=makeRockTemplate('sandstone',seededRandom(2),0,{...config,elongation:100,distortion:0});
 const c=makeRockTemplate('sandstone',seededRandom(2),0,{...config,elongation:0,distortion:100});
 a.computeBoundingBox();b.computeBoundingBox();assert.ok(b.boundingBox.max.x-b.boundingBox.min.x>(a.boundingBox.max.x-a.boundingBox.min.x)*1.5);
 assert.notDeepEqual(a.attributes.position.array,c.attributes.position.array);[a,b,c].forEach(g=>g.dispose());
});
