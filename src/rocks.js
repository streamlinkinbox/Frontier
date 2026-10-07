import * as THREE from 'three';
import {ConvexGeometry} from 'three/addons/geometries/ConvexGeometry.js';

/** Small rock library built from irregular, sheared polyhedra. Distortion is
 * applied to shared vertices before hull construction: no textures or cracks. */
export function makeFacetedRock(family, random, index, color, options={}) {
  const distortion=(options.distortion ?? 65)/100;
  const elongation=(options.elongation ?? 65)/100;
  const points=[];
  const sides=family==='slate'?5:7;
  const long=family==='sandstone' ? 1+elongation*(index%3===0?.7:index%3===1?.4:.15) : 1;
  const shear=(random()-.5)*distortion*.65;
  const bend=(random()-.5)*distortion*.8;
  const taper=.65+random()*.3;
  // Staggered rings yield large uneven triangular facets rather than brick edges.
  for(let ring=0;ring<3;ring++){
    const y=[-.5,-.04,.5][ring];
    const radius=ring===1?.57: ring===0?.44:.42*taper;
    const twist=ring*.2+(random()-.5)*distortion*.4;
    for(let i=0;i<sides;i++){
      const angle=i/sides*Math.PI*2+twist;
      const r=radius*(1+(random()-.5)*distortion*.65);
      const x=Math.cos(angle)*r+shear*y+bend*(.25-y*y);
      const yy=y+(random()-.5)*distortion*.26;
      const z=Math.sin(angle)*r+(random()-.5)*distortion*.16;
      points.push(new THREE.Vector3(x*long,yy*Math.sqrt(long),z*Math.sqrt(long)*(family==='slate'?.5:1)));
    }
  }
  const geometry=new ConvexGeometry(points);
  // Keep the library centered and store real extents for world-space aspect limits.
  geometry.computeBoundingBox();
  const center=geometry.boundingBox.getCenter(new THREE.Vector3());geometry.translate(-center.x,-center.y,-center.z);
  geometry.computeBoundingBox();
  const extent=geometry.boundingBox.getSize(new THREE.Vector3());
  const colors=[];
  for(let i=0;i<geometry.attributes.position.count;i+=3){
    const c=new THREE.Color(color).multiplyScalar(.91+random()*.15);
    for(let j=0;j<3;j++)colors.push(c.r,c.g,c.b);
  }
  geometry.setAttribute('color',new THREE.Float32BufferAttribute(colors,3));
  geometry.userData={extent:extent.toArray(),style:'low-poly',family,elongation:long,distortion,triangleCount:geometry.attributes.position.count/3};
  return geometry;
}

/** Bound transformed sandstone dimensions, not just the source hull. Thin
 * placement cells must not turn an otherwise sound boulder into a shelf. */
export function fitBoulderScale(geometry, sx, sy, sz){
  const ext=geometry.userData.extent||[1,1,1];
  let dimensions=[sx*ext[0],sy*ext[1],sz*ext[2]];
  // Width is limited by the shorter in-plane axis; thickness grows with length.
  dimensions[0]=Math.min(dimensions[0],Math.max(dimensions[1],dimensions[2])*2.15);
  const longest=Math.max(...dimensions);
  dimensions=dimensions.map(d=>Math.max(d,longest/2.6));
  return dimensions.map((d,i)=>d/ext[i]);
}
