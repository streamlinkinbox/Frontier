/*
 * Export helpers: OBJ (with vertex colours), binary glTF and PNG screenshots.
 */
import * as THREE from '../vendor/three.module.js';

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}

/**
 * OBJ with the `v x y z r g b` vertex-colour extension.  Vertices are de-duplicated
 * so the file stays a sane size.
 */
export function exportOBJ(geometries, filename = 'cliff.obj') {
  const lines = ['# Frontier Cliff Forge - procedural quarry (vertex colours)'];
  const seen = new Map();
  let vCount = 0;
  const tmp = new THREE.Color();

  const verts = [];
  const norms = [];
  const faces = [];

  for (const geo of geometries) {
    const pos = geo.attributes.position;
    const nor = geo.attributes.normal;
    const col = geo.attributes.color;
    const index = geo.index ? geo.index.array : null;
    const triCount = pos.count / 3;
    for (let t = 0; t < triCount; t++) {
      const face = [];
      for (let k = 0; k < 3; k++) {
        const i = index ? index[t * 3 + k] : t * 3 + k;
        const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
        const nx = nor.getX(i), ny = nor.getY(i), nz = nor.getZ(i);
        let r = 1, g = 1, b = 1;
        if (col) {
          tmp.setRGB(col.getX(i), col.getY(i), col.getZ(i), THREE.LinearSRGBColorSpace);
          const h = tmp.getHex(THREE.SRGBColorSpace);
          r = ((h >> 16) & 255) / 255; g = ((h >> 8) & 255) / 255; b = (h & 255) / 255;
        }
        const key = `${x.toFixed(4)}|${y.toFixed(4)}|${z.toFixed(4)}|${nx.toFixed(3)}|${ny.toFixed(3)}|${nz.toFixed(3)}|${r.toFixed(3)}|${g.toFixed(3)}|${b.toFixed(3)}`;
        let id = seen.get(key);
        if (id === undefined) {
          id = ++vCount;
          seen.set(key, id);
          verts.push(`v ${x.toFixed(4)} ${y.toFixed(4)} ${z.toFixed(4)} ${r.toFixed(4)} ${g.toFixed(4)} ${b.toFixed(4)}`);
          norms.push(`vn ${nx.toFixed(3)} ${ny.toFixed(3)} ${nz.toFixed(3)}`);
        }
        face.push(id);
      }
      faces.push(`f ${face[0]}//${face[0]} ${face[1]}//${face[1]} ${face[2]}//${face[2]}`);
    }
  }

  const text = lines.concat(verts, norms, faces).join('\n');
  download(new Blob([text], { type: 'text/plain' }), filename);
  return { vertices: vCount, faces: faces.length };
}

/** Binary glTF (.glb) - drag straight into Blender, Unity, Unreal, Godot. */
export async function exportGLB(object, filename = 'cliff.glb') {
  const { GLTFExporter } = await import('../vendor/GLTFExporter.js');
  const exporter = new GLTFExporter();
  const buffer = await exporter.parseAsync(object, { binary: true, onlyVisible: false });
  download(new Blob([buffer], { type: 'model/gltf-binary' }), filename);
  return buffer.byteLength;
}

/** PNG snapshot of the current view. */
export function exportPNG(renderer, filename = 'cliff.png') {
  renderer.domElement.toBlob((blob) => {
    if (blob) download(blob, filename);
  }, 'image/png');
}
