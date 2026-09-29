import * as THREE from 'three';

/** Pale, veined membrane; intentionally NOT an Iris oratoria black eyespot fan. */
export function createHindwingMap() {
  const c = document.createElement('canvas'); c.width = c.height = 1024;
  const g = c.getContext('2d');
  let seed = 4811;
  const random = () => { seed = (Math.imul(seed, 1664525) + 1013904223) | 0; return (seed >>> 0) / 4294967296; };
  g.fillStyle = 'rgba(213,224,189,0.52)'; g.fillRect(0,0,1024,1024);
  for(let i=0;i<=17;i++){
    const x=i/17*1024;
    g.beginPath();g.moveTo(x,0);
    g.bezierCurveTo(x+9,250,x-5,700,x,1024);
    g.strokeStyle='rgba(144,163,101,.95)';g.lineWidth=i%3===0?2.4:1.3;g.stroke();
  }
  for(let i=0;i<17;i++)for(let j=2;j<43;j++){
    const x=i/17*1024, y=(j+.24*random())/43*1024;
    g.beginPath();g.moveTo(x,y);g.quadraticCurveTo(x+30,y+4+random()*8,x+1024/17,y+(random()-.5)*13);
    g.strokeStyle='rgba(155,175,119,.69)';g.lineWidth=.8;g.stroke();
  }
  g.strokeStyle='rgba(164,180,116,.95)';g.lineWidth=4;g.strokeRect(0,0,1024,1024);
  const map = new THREE.CanvasTexture(c); map.colorSpace=THREE.SRGBColorSpace;map.anisotropy=8;
  return map;
}

/** Radial, gently corrugated unfolded membrane in the existing wing's bind space. */
export function unfoldedHindwing(side, radiusFraction, fanFraction) {
  const angle = fanFraction * 1.23;
  const radius = radiusFraction * (3.67 - .84 * fanFraction - .12 * Math.sin(fanFraction * Math.PI));
  return new THREE.Vector3(
    side * (.15 + radius * Math.sin(angle)),
    1.854 + .013 * Math.sin(fanFraction * Math.PI * 28) * radiusFraction
      + .045 * Math.sin(Math.PI * fanFraction) * Math.sin(Math.PI * radiusFraction),
    .12 - radius * Math.cos(angle),
  );
}

/** One portable morph target, on the existing 43-bone rig. No new bone rest poses. */
export function installHindwingMorph(mesh, points) {
  mesh.name='Hindwing_membrane';
  const geometry=mesh.geometry;
  if(points.length !== geometry.attributes.position.count * 3)throw new Error('Hindwing morph topology mismatch');
  const position=new THREE.Float32BufferAttribute(points,3);position.name='DisplayFan';
  const target=geometry.clone();target.setAttribute('position',position);target.computeVertexNormals();
  const normals=target.attributes.normal;
  for(let i=0;i<normals.count;i++){
    const x=normals.getX(i),y=normals.getY(i),z=normals.getZ(i);
    if(x*x+y*y+z*z < .5)normals.setXYZ(i,0,1,0);
  }
  geometry.morphAttributes.position=[position];geometry.morphAttributes.normal=[normals.clone()];
  geometry.morphTargetsRelative=false;
  mesh.updateMorphTargets();mesh.morphTargetInfluences[0]=0;
  target.dispose();
}
