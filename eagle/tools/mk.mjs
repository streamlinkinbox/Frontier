// Build a steps.json from a compact list:  node mk.mjs out.json '<setup js>' name:ox,oy,oz[:lx,ly,lz] ...
// Camera offsets are in the eagle trunk frame (+Z beak, +Y dorsal, +X bird's left), metres.
import fs from 'fs';
const [out, setup, ...shots] = process.argv.slice(2);
const helper = `window.__ec=(o,l)=>{const g=__game,T=g.THREE;g.setFreeze(true);const p=new T.Vector3().setFromMatrixPosition(g.eagle.rig.trunk.matrixWorld),q=g.eagle.object.quaternion.clone();const c=g.camera;c.up.set(0,1,0);c.position.set(...o).applyQuaternion(q).add(p);c.lookAt(new T.Vector3(...(l||[0,0,0])).applyQuaternion(q).add(p));c.updateMatrixWorld();g.render=g.render;g._r=g._r||g.render;return 1}`;
const steps = [{ eval: `(()=>{${helper}; ${setup}; return __game.anim.label})()` }];
for (const s of shots) {
  if (s.startsWith('js=')) { steps.push({ eval: `(()=>{${s.slice(3)}; return 1})()` }); continue; }
  const [name, o, l] = s.split(':');
  steps.push({ eval: `(()=>{__ec([${o}],${l ? `[${l}]` : 'null'}); __game.renderOnly(); return 1})()` });
  steps.push({ shot: name });
}
fs.writeFileSync(out, JSON.stringify(steps, null, 1));
