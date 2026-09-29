/** Promote identity-transform skinned nodes to scene roots per glTF skinning rules.
 * Skeletal ancestors (including the centimetre-to-metre conversion) remain intact.
 * The mesh parent's transform is not part of glTF's skinning equation. Removing
 * this redundant parent avoids NODE_SKINNED_MESH_NON_ROOT portability warnings.
 */
export function normalizeGLB(bytes) {
  const jsonSize = bytes.readUInt32LE(12);
  const gltf = JSON.parse(bytes.subarray(20, 20 + jsonSize).toString());
  const skins = new Set(gltf.nodes.flatMap((n, i) => n.skin !== undefined ? [i] : []));
  for (const i of skins) {
    const n = gltf.nodes[i];
    if (n.matrix || n.translation || n.rotation || n.scale) throw new Error('Cannot promote a transformed skinned node without baking it.');
  }
  for (const node of gltf.nodes) if (node.children) {
    node.children = node.children.filter(i => !skins.has(i));
    if (!node.children.length) delete node.children;
  }
  const scene = gltf.scenes[gltf.scene || 0];
  scene.nodes = [...new Set([...scene.nodes, ...skins])];
  const json = Buffer.from(JSON.stringify(gltf));
  const padded = Buffer.alloc(Math.ceil(json.length / 4) * 4, 32); json.copy(padded);
  const rest = bytes.subarray(20 + jsonSize);
  const header = Buffer.alloc(20); header.writeUInt32LE(0x46546c67, 0); header.writeUInt32LE(2, 4);
  header.writeUInt32LE(20 + padded.length + rest.length, 8); header.writeUInt32LE(padded.length, 12); header.writeUInt32LE(0x4e4f534a, 16);
  return Buffer.concat([header, padded, rest]);
}
