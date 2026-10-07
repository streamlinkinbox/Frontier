import * as THREE from 'three';
import {seededRandom} from './quarry.js';
import {surfaceSettings} from './surface-material.js';
import {MeshBVH} from 'three-mesh-bvh';

const exposureCache=new WeakMap();
function exposedSurface(group){
  if(exposureCache.has(group))return exposureCache.get(group);
  const positions=[],matrix=new THREE.Matrix4(),point=new THREE.Vector3();
  // Build a temporary world-space triangle union for placement ray queries.
  // Includes the continuous base, dressing and talus, never detail geometry.
  for(const source of group.children.filter(o=>o.isMesh)){
    const p=source.geometry.attributes.position,index=source.geometry.index;
    for(let instance=0;instance<(source.isInstancedMesh?source.count:1);instance++){
      if(source.isInstancedMesh)source.getMatrixAt(instance,matrix);else matrix.copy(source.matrix);
      for(let i=0;i<(index?index.count:p.count);i++){
        point.fromBufferAttribute(p,index?index.getX(i):i).applyMatrix4(matrix);positions.push(point.x,point.y,point.z);
      }
    }
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
  const bvh=new MeshBVH(geometry);
  const result={geometry,bvh};exposureCache.set(group,result);return result;
}

function flakeGeometry(){
  // A thin, lifted wedge; the back intersects the rock, the lip catches light.
  const p=[[-.5,-.5,0],[.5,-.4,0],[.38,.5,.16],[-.35,.4,.12],[-.5,-.5,-.025],[.5,-.4,-.025],[.38,.5,.12],[-.35,.4,.08]];
  const triangles=[0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,1,5,6,1,6,2,2,6,7,2,7,3,3,7,4,3,4,0];
  const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(triangles.flatMap(i=>p[i]),3));g.computeVertexNormals();return g;
}
function crystalGeometry(){
  // Short quartz-like six-sided prism with a faceted termination; local +Z is outward.
  const v=[];
  const point=(i,z,r)=>[Math.cos(i/6*Math.PI*2)*r,Math.sin(i/6*Math.PI*2)*r,z];
  for(let i=0;i<6;i++){
    const a=point(i,0,.22),b=point(i+1,0,.22),c=point(i,.65,.20),d=point(i+1,.65,.20);
    v.push(...a,...b,...c,...b,...d,...c,0,0,1,...c,...d,0,0,0,...b,...a);
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(v,3));geometry.computeVertexNormals();return geometry;
}
export function removeSurfaceDetails(group){
  const old=group.getObjectByName('Surface_microgeometry');if(!old)return;
  old.traverse(o=>{o.geometry?.dispose();o.material?.dispose();if(o.isInstancedMesh)o.dispose();});group.remove(old);
}
export function rebuildSurfaceDetails(group,settings){
  removeSurfaceDetails(group);
  const config=surfaceSettings(settings);
  const root=new THREE.Group();root.name='Surface_microgeometry';root.userData.surfaceDetail=true;
  root.userData.clusters=[];
  root.visible=config.surfaceEnabled;group.add(root);
  if(!config.peeling&&!config.crystals)return root;
  const {geometry:surface,bvh}=exposedSurface(group);
  const p=surface.attributes.position,index=surface.index;
  if(!p.count)return root;
  const detailSeed=(group.userData.settings.seed??28491)^0x251ff9;
  const dummy=new THREE.Object3D(),basis=new THREE.Matrix4();
  const a=new THREE.Vector3(),b=new THREE.Vector3(),c=new THREE.Vector3(),normal=new THREE.Vector3();
  const edge=new THREE.Vector3(),up=new THREE.Vector3(),position=new THREE.Vector3();
  const ray=new THREE.Ray();
  const geology=group.userData.formation;
  function sample(random){
    for(let attempt=0;attempt<160;attempt++){
      const v=Math.floor(random()*(index.count/3))*3;
      a.fromBufferAttribute(p,index.getX(v));b.fromBufferAttribute(p,index.getX(v+1));c.fromBufferAttribute(p,index.getX(v+2));
      normal.crossVectors(edge.copy(b).sub(a),up.copy(c).sub(a)).normalize();
      // Prefer exposed cliff faces rather than invisible backs or flat talus.
      if(normal.z<.2||normal.y>.8||normal.y<-.7)continue;
      const u=Math.sqrt(random()),w=random();
      position.copy(a).multiplyScalar(1-u).addScaledVector(b,u*(1-w)).addScaledVector(c,u*w);
      ray.origin.copy(position).addScaledVector(normal,.006);ray.direction.copy(normal);
      if(bvh.raycastFirst(ray,THREE.DoubleSide,0,2000))continue;
      edge.copy(b).sub(a).normalize();up.crossVectors(normal,edge).normalize();edge.crossVectors(up,normal).normalize();
      return {position:position.clone(),normal:normal.clone(),edge:edge.clone(),up:up.clone()};
    }
    return null;
  }
  function orientation(site){basis.makeBasis(site.edge,site.up,site.normal);dummy.quaternion.setFromRotationMatrix(basis);}
  function addMesh(name,geometry,material,matrices,colors){
    if(!matrices.length){geometry.dispose();material.dispose();return;}
    const mesh=new THREE.InstancedMesh(geometry,material,matrices.length);mesh.name=name;mesh.userData.surfaceDetail=true;
    matrices.forEach((m,i)=>{mesh.setMatrixAt(i,m);if(colors)mesh.setColorAt(i,colors[i]);});
    mesh.instanceMatrix.needsUpdate=true;mesh.castShadow=true;
    // Cliff-wide shadow texels cannot resolve these centimetre-scale facets.
    // Avoid pixelated self-shadowing; actual faceted geometry catches light.
    mesh.receiveShadow=false;root.add(mesh);
  }
  const flakes=[],crystals=[],colors=[];
  const flakeRandom=seededRandom(detailSeed),crystalRandom=seededRandom(detailSeed^0x7415ba);
  for(let i=0;i<Math.round(config.peeling*2.4);i++){
    const site=sample(flakeRandom);if(!site)continue;
    orientation(site);dummy.rotateZ(flakeRandom()*Math.PI*2);
    dummy.position.copy(site.position).addScaledVector(site.normal,-.002);
    const size=.12+flakeRandom()*.32;dummy.scale.set(size,size*(.7+flakeRandom()*.8),size*.7);
    dummy.updateMatrix();flakes.push(dummy.matrix.clone());
  }
  // Each pocket has a large central prism and smaller tilted companions.
  // Increasing abundance adds pockets, not a shader sparkle/noise mask.
  for(let i=0;i<Math.round(config.crystals*1.2);i++){
    const site=sample(crystalRandom);if(!site)continue;
    const radius=.08+crystalRandom()*.10;
    const count=12+Math.floor(crystalRandom()*9);
    root.userData.clusters.push({position:site.position.clone(),normal:site.normal.clone(),radius});
    for(let j=0;j<count;j++){
      const angle=crystalRandom()*Math.PI*2,r=j===0?0:Math.sqrt(crystalRandom())*radius;
      const target=site.position.clone().addScaledVector(site.edge,Math.cos(angle)*r).addScaledVector(site.up,Math.sin(angle)*r);
      ray.origin.copy(target).addScaledVector(site.normal,.6);ray.direction.copy(site.normal).negate();
      const hit=bvh.raycastFirst(ray,THREE.DoubleSide,0,1.2);if(!hit||Math.abs(hit.distance-.6)>.15)continue;
      orientation(site);dummy.rotateZ(angle);dummy.rotateX((crystalRandom()-.5)*.8);dummy.rotateY((crystalRandom()-.5)*.8);
      dummy.position.copy(hit.point).addScaledVector(site.normal,-.006);
      const height=j===0?.18+crystalRandom()*.12:.065+crystalRandom()*.16;
      dummy.scale.set(height*(.8+crystalRandom()*.6),height*(.8+crystalRandom()*.6),height);
      dummy.updateMatrix();crystals.push(dummy.matrix.clone());
      const tint=.65+crystalRandom()*.22;colors.push(new THREE.Color().setRGB(tint,tint*.98,tint*(geology==='slate'?1.02:.88)));
    }
  }
  addMesh('Peeling_flakes',flakeGeometry(),new THREE.MeshStandardMaterial({color:geology==='sandstone'?0xb7a17a:geology==='slate'?0x778588:0xc4bda7,roughness:.94,flatShading:true}),flakes);
  addMesh('Mineral_crystals',crystalGeometry(),new THREE.MeshStandardMaterial({color:0xffffff,roughness:.26,metalness:0,flatShading:true}),crystals,colors);
  return root;
}
