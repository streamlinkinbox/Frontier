import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { validateBytes } from 'gltf-validator';
const bytes = fs.readFileSync(new URL('../public/models/mantis.glb', import.meta.url));
const jsonLength = bytes.readUInt32LE(12);
const gltf = JSON.parse(bytes.subarray(20, 20 + jsonLength).toString());
const binaryOffset = 20 + jsonLength + 8;
const components = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
function floats(accessorIndex) {
  const a = gltf.accessors[accessorIndex], view = gltf.bufferViews[a.bufferView];
  assert.equal(a.componentType, 5126);
  const offset = binaryOffset + (view.byteOffset || 0) + (a.byteOffset || 0);
  const stride = view.byteStride || components[a.type] * 4;
  return Array.from({ length: a.count }, (_, i) => Array.from({ length: components[a.type] }, (_, j) => bytes.readFloatLE(offset + i * stride + j * 4)));
}
test('valid, self-contained glTF 2.0 binary', async () => {
  assert.equal(bytes.readUInt32LE(0), 0x46546c67);
  assert.equal(bytes.readUInt32LE(4), 2);
  assert.equal(bytes.readUInt32LE(8), bytes.length);
  const report = await validateBytes(new Uint8Array(bytes), { maxIssues: 100 });
  assert.equal(report.issues.numErrors, 0, JSON.stringify(report.issues));
  assert.equal(report.issues.numWarnings, 0, JSON.stringify(report.issues));
  assert(gltf.images.length >= 5);
  assert(gltf.images.every(image => image.bufferView !== undefined && !image.uri));
});
test('real skinned mesh and anatomical skeleton, not image planes', () => {
  const joints = [...new Set(gltf.skins.flatMap(s => s.joints))];
  assert.equal(joints.length, 43);
  const names = joints.map(i => gltf.nodes[i].name);
  for (const name of ['Root','Head','Prothorax','Abdomen_03','Fore_L_Coxa','Fore_R_Femur','Middle_L_Tarsus','Hind_R_Tarsus','Tegmen_L','Hindwing_R','Antenna_L_4']) assert(names.includes(name), name);
  let triangles = 0;
  for (const mesh of gltf.meshes) for (const primitive of mesh.primitives) {
    assert(primitive.attributes.JOINTS_0 !== undefined);
    assert(primitive.attributes.WEIGHTS_0 !== undefined);
    triangles += gltf.accessors[primitive.indices].count / 3;
    assert(floats(primitive.attributes.POSITION).flat().every(Number.isFinite));
  }
  assert(triangles > 100000 && triangles < 400000);
});
test('four independent animation clips with real joint motion', () => {
  assert.deepEqual(gltf.animations.map(a => a.name), ['Idle','Walk','Attack','Stance']);
  for (const animation of gltf.animations) {
    assert(animation.channels.length >= 43);
    const duration = Math.max(...animation.samplers.map(s => gltf.accessors[s.input].max[0]));
    assert(Math.abs(duration - (['Idle','Stance'].includes(animation.name) ? 6 : 2.4)) < 1e-5);
    const animatedJoint = animation.name === 'Attack' ? 'Fore_L_Coxa' : animation.name === 'Walk' ? 'Middle_L_Femur' : 'Head';
    const track = animation.channels.find(c => gltf.nodes[c.target.node].name === animatedJoint && c.target.path === 'rotation');
    assert(track, `missing motion track for ${animatedJoint}`);
    const values = floats(animation.samplers[track.sampler].output);
    const deviation = Math.max(...values.map(q => Math.hypot(...q.map((v, i) => v - values[0][i]))));
    assert(deviation > (animation.name === 'Stance' ? .005 : .03), `${animation.name} must actually move ${animatedJoint}`);
    for (const q of values) assert(Math.abs(Math.hypot(...q) - 1) < 2e-5);
  }
});
test('idle and walk are seamless loops and attack recovers', () => {
  for (const animation of gltf.animations) for (const sampler of animation.samplers) {
    const values = floats(sampler.output), first = values[0], last = values.at(-1);
    assert(Math.hypot(...first.map((v, i) => v - last[i])) < 1e-4, `${animation.name} has a discontinuous loop/recovery`);
  }
});
test('export retains natural metric scale and all skin nodes are scene roots', () => {
  const root = gltf.nodes.find(n => n.name === 'Mantis_religiosa');
  assert.deepEqual(root.scale, [.01,.01,.01]);
  const roots = gltf.scenes[gltf.scene || 0].nodes;
  gltf.nodes.forEach((node, i) => { if(node.skin !== undefined) assert(roots.includes(i)); });
});
