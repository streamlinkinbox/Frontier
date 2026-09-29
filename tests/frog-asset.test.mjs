import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {validateBytes} from 'gltf-validator';
const bytes=fs.readFileSync(new URL('../public/models/frog.glb',import.meta.url));
const size=bytes.readUInt32LE(12),gltf=JSON.parse(bytes.subarray(20,20+size)),binary=20+size+8;
const widths={SCALAR:1,VEC3:3,VEC4:4,MAT4:16};
function floats(i){const a=gltf.accessors[i],v=gltf.bufferViews[a.bufferView],n=widths[a.type];assert.equal(a.componentType,5126);const offset=binary+(v.byteOffset||0)+(a.byteOffset||0),stride=v.byteStride||n*4;return Array.from({length:a.count},(_,j)=>Array.from({length:n},(_,k)=>bytes.readFloatLE(offset+j*stride+k*4)));}
test('frog is a valid, self-contained skinned GLB in metres',async()=>{
  const report=await validateBytes(new Uint8Array(bytes),{maxIssues:100});assert.equal(report.issues.numErrors,0,JSON.stringify(report.issues));assert.equal(report.issues.numWarnings,0);
  assert.equal(bytes.readUInt32LE(0),0x46546c67);assert.equal(bytes.readUInt32LE(8),bytes.length);
  const root=gltf.nodes.find(n=>n.name==='Rana_temporaria');assert.deepEqual(root.scale,[.01,.01,.01]);assert.equal(root.extras.snoutVentLengthMm,70);
  assert(gltf.images.every(i=>i.bufferView!==undefined&&!i.uri));
});
test('frog has four front digits and five hind toes per side, with a real 39-bone rig',()=>{
  const joints=[...new Set(gltf.skins.flatMap(s=>s.joints))];assert.equal(joints.length,39);
  for(const side of ['L','R']){
    assert.equal(joints.filter(i=>gltf.nodes[i].name.startsWith(`Finger_${side}_`)).length,4);
    assert.equal(joints.filter(i=>gltf.nodes[i].name.startsWith(`Toe_${side}_`)).length,5);
  }
  const names=gltf.materials.map(m=>m.name);assert(names.includes('Webbing'));assert(names.includes('Dorsolateral folds'));assert(names.includes('Eye globe'));
});
test('frog breathing and blink are embedded animation, not viewer-only tricks',()=>{
  assert.deepEqual(gltf.animations.map(a=>a.name),['Idle','Blink']);
  for(const a of gltf.animations){const channel=a.channels.find(c=>c.target.path==='weights');assert(channel);const values=floats(a.samplers[channel.sampler].output).flat();assert(Math.max(...values)>.99);assert.equal(values[0],0);assert.equal(values.at(-1),0);}
  const idle=gltf.animations[0],channel=idle.channels.find(c=>gltf.nodes[c.target.node].name==='Throat'&&c.target.path==='scale');const scales=floats(idle.samplers[channel.sampler].output);assert(Math.max(...scales.map(v=>v[1]))>1.04);assert(Math.min(...scales.map(v=>v[1]))<.96);
});
test('frog surface normals and skin weights cover every vertex',()=>{
  let triangles=0;
  for(const mesh of gltf.meshes)for(const p of mesh.primitives){const count=gltf.accessors[p.attributes.POSITION].count;for(const key of ['NORMAL','JOINTS_0','WEIGHTS_0'])assert.equal(gltf.accessors[p.attributes[key]].count,count);triangles+=gltf.accessors[p.indices].count/3;}
  assert(triangles>100000&&triangles<400000);
});
