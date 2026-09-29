import fs from 'node:fs';
import * as THREE from 'three';

/** Decode real exported bone/keyframe data without a browser or texture mock.
 * Mesh morph channels are inspected directly via accessors, not simulated here. */
export function readRig() {
  const bytes = fs.readFileSync(new URL('../public/models/mantis.glb', import.meta.url));
  const length = bytes.readUInt32LE(12);
  const gltf = JSON.parse(bytes.subarray(20, 20 + length));
  const start = 20 + length + 8;
  const components = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
  function accessor(i) {
    const a = gltf.accessors[i], view = gltf.bufferViews[a.bufferView], n = components[a.type];
    if (a.componentType !== 5126) throw new Error('Expected FLOAT accessor');
    const offset = start + (view.byteOffset || 0) + (a.byteOffset || 0), stride = view.byteStride || n * 4;
    const out = new Float32Array(a.count * n);
    for (let j = 0; j < a.count; j++) for (let k = 0; k < n; k++) out[j * n + k] = bytes.readFloatLE(offset + j * stride + k * 4);
    return out;
  }
  const jointIds = new Set(gltf.skins.flatMap(s => s.joints));
  const nodes = gltf.nodes.map((n, i) => {
    const o = jointIds.has(i) ? new THREE.Bone() : new THREE.Group();
    o.name = n.name;
    if (n.matrix) { o.matrix.fromArray(n.matrix); o.matrix.decompose(o.position, o.quaternion, o.scale); }
    if (n.translation) o.position.fromArray(n.translation);
    if (n.rotation) o.quaternion.fromArray(n.rotation);
    if (n.scale) o.scale.fromArray(n.scale);
    return o;
  });
  gltf.nodes.forEach((n, i) => n.children?.forEach(c => nodes[i].add(nodes[c])));
  const root = new THREE.Group();
  gltf.scenes[gltf.scene || 0].nodes.forEach(i => root.add(nodes[i]));
  const clips = gltf.animations.map(a => new THREE.AnimationClip(a.name, -1, a.channels.filter(c=>c.target.path!=='weights').map(c => {
    const s = a.samplers[c.sampler], path = { rotation: 'quaternion', translation: 'position', scale: 'scale' }[c.target.path];
    const Track = path === 'quaternion' ? THREE.QuaternionKeyframeTrack : THREE.VectorKeyframeTrack;
    return new Track(`${nodes[c.target.node].name}.${path}`, accessor(s.input), accessor(s.output));
  })));
  const mixer = new THREE.AnimationMixer(root);
  function pose(name, time) {
    mixer.stopAllAction();
    const clip = clips.find(c => c.name === name);
    const action = mixer.clipAction(clip).reset().setLoop(THREE.LoopOnce, 1).play();
    action.clampWhenFinished = true; action.time = Math.min(time, clip.duration);
    mixer.update(0); root.updateMatrixWorld(true);
  }
  function point(name) { return root.getObjectByName(name).getWorldPosition(new THREE.Vector3()); }
  function joint(name) { return root.getObjectByName(name); }
  return { gltf, accessor, nodes, root, clips, mixer, pose, point, joint };
}
