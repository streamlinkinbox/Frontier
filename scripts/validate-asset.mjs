import fs from 'node:fs';
import { validateBytes } from 'gltf-validator';
const file = 'public/models/mantis.glb';
const bytes = fs.readFileSync(file);
const report = await validateBytes(new Uint8Array(bytes), { uri: 'mantis.glb', maxIssues: 30 });
const jsonLength = bytes.readUInt32LE(12);
const gltf = JSON.parse(bytes.subarray(20, 20 + jsonLength).toString());
const names = gltf.animations.map(a => a.name);
const bones = new Set(gltf.skins.flatMap(s => s.joints));
const triangles = gltf.meshes.reduce((sum, m) => sum + m.primitives.reduce((n, p) => n + gltf.accessors[p.indices].count / 3, 0), 0);
console.log(JSON.stringify({ bytes: bytes.length, triangles, bones: bones.size, animations: names, validation: report.issues }, null, 2));
if (report.issues.numErrors > 0) process.exitCode = 1;
const motionMetadata = gltf.nodes.find(n=>n.name==='Mantis_religiosa')?.extras?.animationMetadata || {};
const manifest = {
  name: 'Mantis religiosa · adult female',
  file: 'mantis.glb',
  format: 'glTF 2.0 binary',
  bytes: bytes.length,
  triangles,
  bones: bones.size,
  materialCount: gltf.materials.length,
  embeddedImages: gltf.images.length,
  units: 'metres',
  nominalBodyLengthMetres: .07,
  walkTranslationMetresPerSecond: .0025,
  animations: gltf.animations.map(a => ({ name: a.name, durationSeconds: Math.max(...a.samplers.map(s => gltf.accessors[s.input].max[0])), channels: a.channels.length, sampleRateHz: motionMetadata[a.name]?.sampleRate || 60, loop: motionMetadata[a.name]?.loop, revision: motionMetadata[a.name]?.revision })),
  hindwingMorphTargets: ['DisplayFan'],
  walkPreview: 'Tracking ground; the exported Walk clip remains in-place.',
  validation: { errors: report.issues.numErrors, warnings: report.issues.numWarnings },
  notes: 'Original procedural anatomical study. Not a photogrammetric scan or production LOD mesh. No floral modifications or gameplay logic.',
};
fs.writeFileSync('public/models/asset-manifest.json', JSON.stringify(manifest, null, 2) + '\n');
