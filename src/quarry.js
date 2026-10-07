import * as THREE from 'three';
import {createRockMaterial} from './surface-material.js';
import {makeFacetedRock,fitBoulderScale} from './rocks.js';

export function seededRandom(seed) {
  let a = seed >>> 0;
  return () => { a += 0x6D2B79F5; let t = a; t = Math.imul(t ^ t >>> 15, t | 1); t ^= t + Math.imul(t ^ t >>> 7, t | 61); return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
export const palettes = {
  limestone: { name:'LIMESTONE', rock:0xa19b80, ground:0x7c7c5c, floor:0x827d63, dark:0x514f40 },
  sandstone: { name:'SANDSTONE', rock:0xb89068, ground:0x877a53, floor:0x9c8663, dark:0x66513d },
  slate: { name:'SLATE', rock:0x78858a, ground:0x606d62, floor:0x687477, dark:0x3f4a4e }
};
// These are geometric formation rules, not just material presets.
export const formations = {
  limestone: { description:'Bedded ledges · recessed seams · jointed slabs', slope:1.6, buttress:2.5, bed:1.45, relief:.4, dip:.025, blockWidth:5.4, thickness:1.35, tilt:.035 },
  sandstone: { description:'Massive beds · erosion alcoves · broad buttresses', slope:3.5, buttress:4.2, bed:3.1, relief:.7, dip:.045, blockWidth:7.8, thickness:2.3, tilt:.07 },
  slate: { description:'Inclined cleavage · blade-like plates · scree', slope:2.4, buttress:2.1, bed:.4, relief:.18, dip:.14, blockWidth:1.25, thickness:.8, tilt:.38 }
};
export function makeOutline(random, count=48) {
  return Array.from({length:count},()=>0.965+random()*.07);
}
const TAU=Math.PI*2;
const wrap=a=>((a%TAU)+TAU)%TAU;
const lerp=THREE.MathUtils.lerp;
const clamp=THREE.MathUtils.clamp;

// Hand-shaped cross sections, varied with seeded corner cuts. Multiple rings produce
// non-convex bedding ledges, undercuts, sandstone shoulders and slate cleavage edges.
// No noise functions, texture maps or texture-based displacement.
export function makeRockTemplate(family,random,t,options={}) {
  if(options.meshStyle==='lowpoly'||family==='sandstone')return makeFacetedRock(family,random,t,palettes[family].rock,options);
  const outlines={
    limestone:[[-.5,-.27],[-.37,-.5],[.24,-.49],[.5,-.24],[.45,.38],[.16,.5],[-.36,.44],[-.5,.17]],
    sandstone:[[-.48,-.2],[-.31,-.48],[.16,-.5],[.46,-.29],[.5,.14],[.29,.47],[-.2,.5],[-.46,.29]],
    slate:[[-.5,-.43],[-.29,-.5],[.49,-.42],[.5,.27],[.33,.5],[-.5,.42]]
  };
  const rings=family==='limestone'?[[-.5,.85],[-.38,1],[-.08,.97],[.02,.81],[.12,1],[.39,1],[.5,.92]]:
    family==='sandstone'?[[-.5,.67],[-.39,.9],[-.18,1],[.22,.99],[.41,.84],[.5,.65]]:
    [[-.5,.73],[-.44,1],[-.06,.93],[.03,.77],[.1,.99],[.44,.96],[.5,.76]];
  const outline=outlines[family].map(([x,z])=>[x+(random()-.5)*.085,z+(random()-.5)*.08]);
  const verts=[],cols=[];const count=outline.length;const ringPoints=[];
  const lean=family==='slate'?.25:family==='sandstone'?.09:.03;
  for(const [y,s] of rings)ringPoints.push(outline.map(([x,z],i)=>new THREE.Vector3(x*s+y*lean,y+(i%3-1)*.018,z*s+(i%2?-.025:.025)*(t%3))));
  const base=new THREE.Color(palettes[family].rock);
  function tri(a,b,c,tone){const color=base.clone().multiplyScalar(tone);for(const p of [a,b,c]){verts.push(p.x,p.y,p.z);cols.push(color.r,color.g,color.b);}}
  for(let j=0;j<rings.length-1;j++)for(let i=0;i<count;i++){
    const k=(i+1)%count,shade=.91+random()*.13-(j===2?.06:0);
    tri(ringPoints[j][i],ringPoints[j+1][i],ringPoints[j][k],shade);
    tri(ringPoints[j][k],ringPoints[j+1][i],ringPoints[j+1][k],shade);
  }
  for(let i=0;i<count;i++){
    const k=(i+1)%count;
    tri(new THREE.Vector3(0,-.5,0),ringPoints[0][i],ringPoints[0][k],.86);
    tri(new THREE.Vector3(lean*.5,.5,0),ringPoints.at(-1)[k],ringPoints.at(-1)[i],1.02);
  }
  const geo=new THREE.BufferGeometry();geo.setAttribute('position',new THREE.Float32BufferAttribute(verts,3));geo.setAttribute('color',new THREE.Float32BufferAttribute(cols,3));geo.computeVertexNormals();return geo;
}

export function buildQuarry(settings) {
  const {terraces,height,width,radius,fracture=65,debris=true,seed}=settings;
  const geology=formations[settings.geology]?settings.geology:'limestone';
  const profile=formations[geology],palette=palettes[geology];
  // Separate streams make debris and dressing switches independent of the base formation.
  const shapeRandom=seededRandom(seed),random=seededRandom(seed^0x73ac4d),talusRandom=seededRandom(seed^0x48bce1);
  const group=new THREE.Group();group.name='Frontier_Quarry';
  const lowPoly=settings.meshStyle==='lowpoly';
  const n=lowPoly?48:192,outline=makeOutline(shapeRandom,24),depth=terraces*height;
  const pitch=width+profile.slope,outer=radius+terraces*pitch;
  const dipAngle=shapeRandom()*TAU,dipX=Math.cos(dipAngle)*profile.dip,dipZ=Math.sin(dipAngle)*profile.dip;
  // Large, explicitly constructed fracture sectors: polygonal buttresses, setbacks
  // and collapsed wedges. They shape the base BEFORE any rocks are placed.
  const sectorCount=geology==='sandstone'?18:geology==='slate'?30:24;
  const sectors=Array.from({length:sectorCount},()=>({relief:(shapeRandom()-.45)*profile.buttress,scar:shapeRandom()<.2,breakHeight:.25+shapeRandom()*.5}));
  const beds=[-depth-outer];
  while(beds.at(-1)<depth+outer){beds.push(beds.at(-1)+profile.bed*(.65+shapeRandom()*.75));}
  const bedTones=beds.map(()=>.85+shapeRandom()*.14);
  function bedAt(elevation){let low=0,high=beds.length-2;while(low<high){const mid=Math.ceil((low+high)/2);if(beds[mid]<=elevation)low=mid;else high=mid-1;}return low;}
  function point(a,r,y){
    const k=wrap(a)/TAU*outline.length;
    const shape=lerp(outline[Math.floor(k)],outline[(Math.floor(k)+1)%outline.length],k%1);
    const c=Math.cos(a),s=Math.sin(a);
    return new THREE.Vector3(Math.sign(c)*Math.pow(Math.abs(c),.86)*r*shape*1.14,y,Math.sign(s)*Math.pow(Math.abs(s),.86)*r*shape*.86);
  }
  function facePoint(level,a,t){
    t=clamp(t,0,1);
    const r=radius+level*pitch, y=level*height+t*height;
    const sector=wrap(a)/TAU*sectorCount,idx=Math.floor(sector),fraction=sector%1;
    const current=sectors[idx],next=sectors[(idx+1)%sectorCount];
    // Linear planes, not a sampled random field. Adjacent planes form broad buttresses.
    const macro=lerp(current.relief,next.relief,fraction);
    const taper=Math.min(1,t*5,(1-t)*5);
    const sample=point(a,r+profile.slope*t,y);
    const e=y-sample.x*dipX-sample.z*dipZ,bi=bedAt(e),u=clamp((e-beds[bi])/(beds[bi+1]-beds[bi]),0,1);
    // Weathered seam at the bottom, projecting cap higher in each shared bed.
    const bedRelief=profile.relief*(u<.15?lerp(.55,-.7,u/.15):u<.72?-.7:lerp(-.7,.2,(u-.72)/.28));
    const scar=current.scar?Math.max(0,1-Math.abs(t-current.breakHeight)/.3)*profile.buttress*.55:0;
    return point(a,r+profile.slope*t+macro*(.8+.2*y/depth)+bedRelief+scar*taper,y);
  }
  const material=createRockMaterial({...settings,geology});
  const vertices=[],colors=[],ranges=[];const col=new THREE.Color();
  function triangle(a,b,c,color,shade=1){col.set(color).multiplyScalar(shade);for(const p of [a,b,c]){vertices.push(p.x,p.y,p.z);colors.push(col.r,col.g,col.b);}}
  function quad(p,q,r,s,color,shade){triangle(p,q,r,color,shade);triangle(q,s,r,color,shade);}
  function band(r1,y1,r2,y2,color,shade=1){for(let i=0;i<n;i++){
    const a=i/n*TAU,b=(i+1)/n*TAU;
    quad(point(a,r1,y1),point(b,r1,y1),point(a,r2,y2),point(b,r2,y2),color,shade);
  }}
  for(let i=0;i<n;i++)triangle(new THREE.Vector3(),facePoint(0,(i+1)/n*TAU,0),facePoint(0,i/n*TAU,0),palette.floor,.98);
  const faceRows=lowPoly?3:Math.ceil(height/(geology==='slate'?.22:.32));
  for(let level=0;level<terraces;level++){
    const start=vertices.length/3;
    for(let row=0;row<faceRows;row++)for(let i=0;i<n;i++){
      const a=i/n*TAU,b=(i+1)/n*TAU,t=row/faceRows,v=(row+1)/faceRows;
      const p=facePoint(level,a,t),q=facePoint(level,b,t),r=facePoint(level,a,v),s=facePoint(level,b,v);
      const e=(p.y+r.y)/2-p.x*dipX-p.z*dipZ;
      quad(p,q,r,s,palette.rock,bedTones[bedAt(e)]);
    }
    ranges.push({kind:'cliff',start,count:vertices.length/3-start,level});
    const benchStart=vertices.length/3;
    for(let i=0;i<n;i++){
      const a=i/n*TAU,b=(i+1)/n*TAU;
      const p=facePoint(level,a,1),q=facePoint(level,b,1);
      const r=level+1<terraces?facePoint(level+1,a,0):point(a,outer,depth);
      const s=level+1<terraces?facePoint(level+1,b,0):point(b,outer,depth);
      quad(p,q,r,s,palette.ground,1);
    }
    ranges.push({kind:'bench',start:benchStart,count:vertices.length/3-benchStart,level});
  }
  band(outer,depth,outer+7,depth-.35,palette.ground);
  const strata=Math.ceil(depth/profile.bed);
  for(let layer=0;layer<strata;layer++)band(outer+7+layer/strata,lerp(depth-.35,-3,layer/strata),outer+7+(layer+1)/strata,lerp(depth-.35,-3,(layer+1)/strata),palette.rock,.59+(layer%4)*.024);
  // A bottom fan closes the model with downward-facing normals.
  for(let i=0;i<n;i++)triangle(new THREE.Vector3(0,-3,0),point(i/n*TAU,outer+8,-3),point((i+1)/n*TAU,outer+8,-3),palette.dark);
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));geometry.setAttribute('color',new THREE.Float32BufferAttribute(colors,3));geometry.computeVertexNormals();geometry.userData.surfaceRanges=ranges;
  const terrain=new THREE.Mesh(geometry,material);terrain.name='Terraced_base_mesh';terrain.receiveShadow=true;terrain.castShadow=true;group.add(terrain);

  const templates=Array.from({length:12},(_,i)=>makeRockTemplate(geology,random,i,settings));
  const buckets=templates.map(()=>[]);const dummy=new THREE.Object3D();let rockCount=0,faceRockCount=0,debrisCount=0;
  function addRock(pos,sx,sy,sz,angle,brightness,tilt=0,rng=random,isDebris=false,orientation=null){
    dummy.position.copy(pos);dummy.rotation.set((rng()-.5)*.04,angle,tilt);
    if(orientation){dummy.quaternion.copy(orientation);dummy.rotateZ(tilt);}
    const index=Math.floor(rng()*templates.length);
    if(geology==='sandstone')[sx,sy,sz]=fitBoulderScale(templates[index],sx,sy,sz);
    dummy.scale.set(sx,sy,sz);dummy.updateMatrix();
    buckets[index].push({matrix:dummy.matrix.clone(),color:new THREE.Color().setScalar(brightness)});rockCount++;
    if(isDebris)debrisCount++;else faceRockCount++;
  }
  function dress(level,a0,a1,y0,y1,tilt=0){
    const a=(a0+a1)/2,y=(y0+y1)/2,t=(y-level*height)/height;
    const pos=facePoint(level,a,t);
    const tangent=facePoint(level,a+.001,t).sub(facePoint(level,a-.001,t)).normalize();
    const angle=Math.atan2(-tangent.z,tangent.x);
    const arc=facePoint(level,a0,t).distanceTo(facePoint(level,a1,t));
    // Embed the back of the rock into the exact shaped face, leaving only the
    // fractured front proud. Local slope influences the long-axis fit.
    const up=facePoint(level,a,Math.min(1,t+.01)).sub(facePoint(level,a,Math.max(0,t-.01))).normalize();
    const normal=new THREE.Vector3().crossVectors(tangent,up).normalize();
    const fittedUp=new THREE.Vector3().crossVectors(normal,tangent).normalize();
    const orientation=new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(tangent,fittedUp,normal));
    const thickness=profile.thickness*(.8+random()*.5);
    pos.addScaledVector(normal,thickness*.06);
    addRock(pos,arc*(.98+random()*.07),(y1-y0)*(.95+random()*.12),thickness,angle,.88+random()*.16,tilt,random,false,orientation);
  }
  const fractureScale=(1.45-fracture*.009)*(lowPoly?2.8:1);
  for(let level=0;level<terraces;level++){
    const r=radius+level*pitch,low=level*height,high=low+height;
    if(geology==='slate'){
      // Upright, inclined plates along a coherent cleavage direction, not bricks.
      let a=0;
      while(a<TAU){
        const da=(.55+random()*.8)*profile.blockWidth*fractureScale/r;
        const end=Math.min(TAU,a+da);
        const split=low+height*(.35+random()*.3);
        const inclination=profile.tilt*(.8+random()*.4);
        if(random()<.25) dress(level,a,end,low,high,inclination);
        else {
          dress(level,a,end,low,split,inclination);
          if(random()>.08) dress(level,a,end,split,high,inclination);
        }
        a=end;
      }
    }else{
      // Shared geological beds traverse the entire cliff; joints subdivide each
      // bed irregularly, with occasional missing veneers exposing the shaped base.
      const minBed=bedAt(low-r*.2),maxBed=bedAt(high+r*.2)+1;
      for(let b=minBed;b<=Math.min(maxBed,beds.length-2);b++){
        let a=0;
        while(a<TAU){
          const da=profile.blockWidth*fractureScale*(.55+random()*.9)/r;
          const end=Math.min(TAU,a+da),mid=(a+end)/2;
          const sample=point(mid,r,0),offset=sample.x*dipX+sample.z*dipZ;
          const y0=Math.max(low,beds[b]+offset),y1=Math.min(high,beds[b+1]+offset);
          if(y1-y0>.18&&random()>.12){
            const inclination=Math.atan(dipX*(-Math.sin(mid))+dipZ*Math.cos(mid));
            dress(level,a,end,y0,y1,inclination);
            // Secondary spalled ledges on limestone, smaller detached blocks at
            // sandstone joint terminations. Sizes come from the bed, not uniform tiles.
            if(random()<fracture/230){
              const p=facePoint(level,mid,(y0-low)/height);p.addScaledVector(new THREE.Vector3(-Math.cos(mid),0,-Math.sin(mid)),profile.thickness*.55);
              addRock(p,(end-a)*r*.45,(y1-y0)*.23,profile.thickness*.7,-mid,.8+random()*.18,inclination);
            }
          }
          a=end;
        }
      }
    }
  }
  // Optional talus uses its own stream; turning it off never changes attached rocks.
  if(debris){
    for(let level=0;level<terraces;level++){
      const r=radius+level*pitch,y=level*height,count=Math.round(r*fracture/100*(lowPoly?.18:1)*(geology==='slate'?16:10));
      for(let j=0;j<count;j++){
        const a=talusRandom()*TAU,size=.13+Math.pow(talusRandom(),2)*(geology==='sandstone'?2.2:1.4);
        const p=facePoint(level,a,0);p.addScaledVector(new THREE.Vector3(-Math.cos(a),0,-Math.sin(a)),.6+Math.pow(talusRandom(),2)*Math.min(width*.6,4));
        const sy=geology==='slate'?size*.17:size*.6;p.y=y+sy*.48;
        addRock(p,size*(geology==='slate'?1.7:1.1),sy,size*.8,talusRandom()*TAU,.75+talusRandom()*.3,(talusRandom()-.5)*.5,talusRandom,true);
      }
    }
    for(let i=0;i<120;i++){
      const a=talusRandom()*TAU,r=Math.sqrt(talusRandom())*(radius-3),s=.1+talusRandom()*.45;
      addRock(point(a,r,s*.15),s,s*.3,s*.7,talusRandom()*TAU,.8+talusRandom()*.2,0,talusRandom,true);
    }
  }
  buckets.forEach((instances,i)=>{
    const mesh=new THREE.InstancedMesh(templates[i],material,instances.length);mesh.name=`${geology}_formation_rock_${i+1}`;
    instances.forEach((instance,j)=>{mesh.setMatrixAt(j,instance.matrix);mesh.setColorAt(j,instance.color);});
    mesh.castShadow=true;mesh.receiveShadow=true;mesh.instanceMatrix.needsUpdate=true;group.add(mesh);
  });
  group.userData={rockCount,faceRockCount,debrisCount,depth,outer,formation:geology,settings:{...settings},dip:{x:dipX,z:dipZ},surfaceRanges:ranges};
  return group;
}
export function disposeQuarry(group){
  const geometries=new Set(),materials=new Set();group.traverse(o=>{if(o.geometry)geometries.add(o.geometry);if(o.material)materials.add(o.material);if(o.isInstancedMesh)o.dispose();});geometries.forEach(g=>g.dispose());materials.forEach(m=>m.dispose());
}
