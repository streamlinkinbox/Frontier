// Export helpers:
//  * OBJ  -> true QUAD topology (f a b c d), one group per patch, with UVs + normals
//  * GLB  -> triangulated mine mesh + instanced props, ready for engines
//  * JSON -> the spline network (nodes + control points + params)
import * as THREE from 'three';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';

export function mineToOBJ(mine) {
  const g = mine.geometry;
  const pos = g.attributes.position.array, nrm = g.attributes.normal.array, uv = g.attributes.uv.array;
  const out = [];
  out.push('# Frontier procedural mine - continuous all-quad surface (cave + road + grooved tracks)');
  out.push(`# vertices ${pos.length / 3}, quads ${mine.quads.length / 4}`);
  out.push('o FrontierMine');
  const f = (x) => (Math.round(x * 10000) / 10000).toString();
  for (let i = 0; i < pos.length; i += 3) out.push(`v ${f(pos[i])} ${f(pos[i + 1])} ${f(pos[i + 2])}`);
  for (let i = 0; i < uv.length; i += 2) out.push(`vt ${f(uv[i])} ${f(uv[i + 1])}`);
  for (let i = 0; i < nrm.length; i += 3) out.push(`vn ${f(nrm[i])} ${f(nrm[i + 1])} ${f(nrm[i + 2])}`);
  const q = mine.quads;
  for (const patch of mine.patchInfo) {
    out.push(`g ${patch.name}`);
    for (let k = patch.start; k < patch.start + patch.count; k++) {
      const a = q[k * 4] + 1, b = q[k * 4 + 1] + 1, c = q[k * 4 + 2] + 1, d = q[k * 4 + 3] + 1;
      out.push(`f ${a}/${a}/${a} ${b}/${b}/${b} ${c}/${c}/${c} ${d}/${d}/${d}`);
    }
  }
  return out.join('\n');
}

export function exportGLB(objects) {
  return new Promise((resolve, reject) => {
    const scene = new THREE.Scene();
    for (const o of objects) {
      const c = o.clone(true);
      c.traverse((m) => {
        if (m.isMesh && m.geometry && m.geometry.attributes.metal) {
          // glTF custom attributes must start with "_"
          const geo = m.geometry.clone();
          geo.setAttribute('_metal', geo.attributes.metal);
          geo.deleteAttribute('metal');
          m.geometry = geo;
          m.material = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.9 });
        }
      });
      scene.add(c);
    }
    new GLTFExporter().parse(scene, (res) => resolve(new Blob([res], { type: 'model/gltf-binary' })), reject, { binary: true });
  });
}

export function download(blobOrText, name, type = 'text/plain') {
  const blob = blobOrText instanceof Blob ? blobOrText : new Blob([blobOrText], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}
