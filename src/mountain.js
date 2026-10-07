import * as THREE from 'three';
import {createRockMaterial} from './surface-material.js';
import {fitBoulderScale} from './rocks.js';
import {seededRandom, palettes, formations, makeRockTemplate} from './quarry.js';

const TAU=Math.PI*2;
const lerp=THREE.MathUtils.lerp;
const clamp=THREE.MathUtils.clamp;
const wrap=a=>((a%TAU)+TAU)%TAU;
function interpolate(points,x){
  for(let i=1;i<points.length;i++)if(x<=points[i][0]){
    const [x0,y0]=points[i-1],[x1,y1]=points[i];return lerp(y0,y1,(x-x0)/(x1-x0));
  }
  return points.at(-1)[1];
}

/** Construct a positive massif, not an inverted quarry. Seeded ridge control
 * planes define the primary silhouette; geological beds cut secondary ledges.
 * There is no noise sampling, texture displacement or concentric bench loop. */
export function buildMountain(settings){
  const {mountainHeight=100,mountainRadius=72,ridges=5,steepness=78,fracture=65,seed=28491,debris=true}=settings;
  const geology=formations[settings.geology]?settings.geology:'limestone';
  const profile=formations[geology],palette=palettes[geology];
  const shape=seededRandom(seed),random=seededRandom(seed^0x123abcd),talus=seededRandom(seed^0x897af1);
  const lowPoly=settings.meshStyle!=='detailed';
  const detail=settings.detail??35;
  const distortion=(settings.distortion??65)/100;
  const cliffShape=settings.cliffShape==='spire'?'spire':'escarpment';
  const escarpment=cliffShape==='escarpment';
  const H=mountainHeight,R=mountainRadius;
  const group=new THREE.Group();group.name='Frontier_Cliff_Mountain';
  const phase=shape()*TAU;
  const crestControls=[[-1,.32],[-.85,.70],[-.67,.76],[-.53,.67],[-.35,.92],[-.14,1],[.12,.9],[.34,.94],[.57,.7],[.78,.74],[1,.36]];
  crestControls.forEach((p,i)=>{if(i!==5)p[1]*=.94+shape()*.1;});
  // Broad scar wedges are explicit triangular cuts across multiple beds.
  const scars=Array.from({length:7},()=>({angle:shape()*TAU,width:.1+shape()*.2,height:.3+shape()*.45,span:.18+shape()*.2,depth:2+shape()*3}));
  const controlPoints=Array.from({length:ridges*2},(_,i)=>({
    reach:i%2===0?.93+shape()*.22:.56+shape()*.18,
    shoulder:.42+shape()*.4,
    face:.26+shape()*.38,
    shift:(shape()-.5)*.16
  }));
  const bedding=[-H];while(bedding.at(-1)<H*2)bedding.push(bedding.at(-1)+profile.bed*(geology==='slate'?2.5:1.6)*(.7+shape()*.65));
  const bedTones=bedding.map(()=>.84+shape()*.15);
  const dipAngle=shape()*TAU,dipX=Math.cos(dipAngle)*profile.dip,dipZ=Math.sin(dipAngle)*profile.dip;
  function bedIndex(y){let lo=0,hi=bedding.length-2;while(lo<hi){const mid=Math.ceil((lo+hi)/2);if(bedding[mid]<=y)lo=mid;else hi=mid-1;}return lo;}
  function sectorAt(a){
    const k=wrap(a-phase)/TAU*controlPoints.length,i=Math.floor(k),u=k-i;
    const p=controlPoints[i],q=controlPoints[(i+1)%controlPoints.length];
    return {reach:lerp(p.reach,q.reach,u),shoulder:lerp(p.shoulder,q.shoulder,u),face:lerp(p.face,q.face,u),shift:lerp(p.shift,q.shift,u)};
  }
  function surface(a,t,detail=true){
    t=clamp(t,0,1);
    const sector=sectorAt(a),cliff=steepness/100;
    // Talus apron → inclined foot → tall near-vertical wall → jagged shoulder → summit.
    // Shoulder elevation differs between ridge planes, avoiding a ring-shaped summit.
    const toe=.12+(1-cliff)*.12,lip=sector.shoulder;
    const back=(Math.cos(a-2.7)+1)*.5;
    const wallRadius=sector.face*(.8+back*.35);
    let radial=interpolate([[0,1],[toe,wallRadius+.12+(1-cliff)*.3],[lip,wallRadius],[.92,.1+back*.08],[1,0]],t);
    // The skirt is broad and contiguous; ridges sharpen with altitude.
    radial*=R*lerp(.94+sector.reach*.08,sector.reach,Math.min(1,t*6));
    const angle=a+sector.shift*t;
    const cx=-R*.26*t,cz=R*.09*t;
    let x=cx+Math.cos(angle)*radial*1.13,z=cz+Math.sin(angle)*radial*.9,y=H*t;
    if(escarpment){
      // A long, uneven crest with a sheer sun-facing wall and a receding back.
      // The top converges to a LINE, not a central cone or a circular mesa.
      const c=Math.cos(a),sn=Math.sin(a),u=Math.sign(c)*Math.pow(Math.abs(c),.72);
      const crest=interpolate(crestControls,u);
      const front=sn>=0;
      const taper=front?interpolate([[0,1],[.14,.69+(1-cliff)*.16],[.78,.65],[.95,.3],[1,0]],t):
        interpolate([[0,1],[.25,.7],[.65,.34],[1,0]],t);
      const spur=(sector.reach-.8)*R*.2*Math.min(1,t*6)*(1-t);
      x=u*R*lerp(1.65,1.4,t)+Math.sin(a*2)*R*.04*(1-t);
      z=Math.sign(sn)*Math.pow(Math.abs(sn),.8)*R*.85*taper+Math.sign(sn)*spur;
      y=H*t*crest;
    }
    // Bedding relief diminishes on the scree apron and converges cleanly at the apex.
    if(detail&&t>toe*.75&&t<.99){
      const e=y-x*dipX-z*dipZ,bi=bedIndex(e),u=clamp((e-bedding[bi])/(bedding[bi+1]-bedding[bi]),0,1);
      const exposure=Math.min(1,(t-toe*.75)*15,(1-t)*20);
      let cut=0;
      for(const scar of scars){
        const da=Math.abs(Math.atan2(Math.sin(a-scar.angle),Math.cos(a-scar.angle)));
        cut+=Math.max(0,1-da/scar.width)*Math.max(0,1-Math.abs(t-scar.height)/scar.span)*scar.depth;
      }
      const ledge=-cut+(lowPoly?0:1)*interpolate([[0,-.28],[.12,-.7],[.26,.3],[.77,.3],[1,-.28]],u)*profile.relief*1.5*exposure;
      x+=Math.cos(angle)*ledge;z+=Math.sin(angle)*ledge;
    }
    return new THREE.Vector3(x,y,z);
  }
  function frame(a,t){
    const pos=surface(a,t);
    const tangent=surface(a+.0005,t,false).sub(surface(a-.0005,t,false)).normalize();
    const up=surface(a,Math.min(.9999,t+.0005),false).sub(surface(a,Math.max(.0001,t-.0005),false)).normalize();
    const normal=new THREE.Vector3().crossVectors(up,tangent).normalize();
    // Local x points clockwise so x × y faces out of the positive mountain.
    tangent.negate();const fittedUp=new THREE.Vector3().crossVectors(normal,tangent).normalize();
    const orientation=new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(tangent,fittedUp,normal));
    return {pos,normal,orientation};
  }
  const material=createRockMaterial({...settings,geology});
  const vertices=[],colors=[];
  function tri(a,b,c,color,shade){const col=new THREE.Color(color).multiplyScalar(shade);for(const p of [a,b,c]){vertices.push(p.x,p.y,p.z);colors.push(col.r,col.g,col.b);}}
  const columns=lowPoly?Math.round((16+detail*.32)/2)*2:192,rows=lowPoly?Math.round(7+detail*.13):Math.max(100,Math.ceil(H/.65));
  const facets=seededRandom(seed^0x642abd);
  const grid=[];
  for(let row=0;row<=rows;row++){
    const ring=[];
    for(let i=0;i<columns;i++){
      const boundary=row===0||row===rows;
      const a=(i+(boundary?0:(facets()-.5)*.65))/columns*TAU;
      const t=(row+(boundary?0:(facets()-.5)*.6))/rows;
      const p=surface(a,t);
      if(lowPoly&&!boundary){
        const offset=(facets()-.5)*distortion*Math.min(H,R)*.045;
        p.x+=Math.cos(a)*offset;p.z+=Math.sin(a)*offset;
      }
      ring.push(p);
    }
    grid.push(ring);
  }
  for(let row=0;row<rows;row++)for(let i=0;i<columns;i++){
    const a=i/columns*TAU,b=(i+1)/columns*TAU,t=row/rows,v=(row+1)/rows;
    const p=grid[row][i],q=grid[row][(i+1)%columns],r=grid[row+1][i],s=grid[row+1][(i+1)%columns];
    const scree=t<.135;
    const color=scree?palette.floor:palette.rock;
    const shade=scree?.88:lowPoly?.91+facets()*.1:bedTones[bedIndex((p.y+r.y)/2-p.x*dipX-p.z*dipZ)];
    tri(p,r,q,color,shade);if(escarpment||row<rows-1)tri(q,r,s,color,shade);
  }
  const baseCount=vertices.length/3;
  // A narrow irregular skirt grounds the talus without a quarry's cylindrical plinth.
  for(let i=0;i<columns;i++){
    const p=grid[0][i],q=grid[0][(i+1)%columns];
    const a=p.clone().setY(-3),b=q.clone().setY(-3);
    tri(a,p,b,palette.dark,.9);tri(b,p,q,palette.dark,.9);
    tri(new THREE.Vector3(0,-3,0),a,b,palette.dark,.8);
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));geometry.setAttribute('color',new THREE.Float32BufferAttribute(colors,3));geometry.computeVertexNormals();
  geometry.userData={surfaceVertexCount:baseCount};
  const base=new THREE.Mesh(geometry,material);base.name='Mountain_base_cliff';base.castShadow=true;base.receiveShadow=true;group.add(base);

  const templates=Array.from({length:12},(_,i)=>makeRockTemplate(geology,random,i,{...settings,meshStyle:lowPoly?'lowpoly':'detailed'}));
  base.updateMatrixWorld(true);
  const raycaster=new THREE.Raycaster();
  const buckets=templates.map(()=>[]);const dummy=new THREE.Object3D();let rockCount=0,faceRockCount=0,debrisCount=0;
  function rock(a,t,sx,sy,sz,tilt,rng,isDebris=false){
    let {pos,normal,orientation}=frame(a,t);
    if(lowPoly){
      // Shoot back along the local outward normal, not toward a global
      // center: an uneven crest has different elevations along its length.
      raycaster.set(pos.clone().addScaledVector(normal,R*.5),normal.clone().negate());
      const hit=raycaster.intersectObject(base,false)[0];
      if(!hit)return;
      pos=hit.point;normal=hit.face.normal.clone();
      const tangent=new THREE.Vector3().crossVectors(new THREE.Vector3(0,1,0),normal).normalize();
      const up=new THREE.Vector3().crossVectors(normal,tangent).normalize();
      orientation=new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(tangent,up,normal));
    }
    const index=Math.floor(rng()*12);
    if(geology==='sandstone')[sx,sy,sz]=fitBoulderScale(templates[index],sx,sy,sz);
    // Most of each stone remains embedded in the base, exposing fractured faces.
    pos.addScaledVector(normal,sz*(isDebris?.23:geology==='sandstone'?-.18:-.04));
    dummy.position.copy(pos);dummy.quaternion.copy(orientation);dummy.rotateZ(tilt);
    dummy.scale.set(sx,sy,sz);dummy.updateMatrix();
    buckets[index].push({matrix:dummy.matrix.clone(),color:new THREE.Color().setScalar(.86+rng()*.16)});
    rockCount++;if(isDebris)debrisCount++;else faceRockCount++;
  }
  if(lowPoly){
    // Stratified but non-row placement: broad embedded rock masses with variable
    // aspect ratios. The base remains exposed between clusters, never a brick skin.
    const count=Math.round((55+detail*1.5)*(H/100)*(.6+fracture/100));
    for(let i=0;i<count;i++){
      const a=random()*TAU,t=.18+random()*.73;
      const size=Math.min(H,R)*(.055+random()*.06);
      const sx=size*(geology==='slate'?.55:1.1);
      const sy=size*(geology==='slate'?2.4:.85+random()*1.2);
      const sz=size*(.4+random()*.45);
      rock(a,t,sx,sy,sz,(random()-.5)*(geology==='slate'?.5:.8),random);
    }
  }else{
  const density=1.5-fracture*.009;
  // Continuous, inclined strata traverse the massif. Separate joint intervals
  // avoid a regular masonry grid while maintaining coherent formation direction.
  const bandHeight=geology==='slate'?5:profile.bed*1.8;
  for(let y=H*.16;y<H*.97;){
    const h=bandHeight*(.75+random()*.6),next=Math.min(H*.98,y+h);
    let a=0;
    while(a<TAU){
      const t=clamp((y+next)/2/H,.05,.98),f=surface(a,t);
      const perimeter=surface(a+.005,t).distanceTo(surface(a-.005,t))/.01;
      const w=(geology==='slate'?1.6:profile.blockWidth*1.12)*density*(.65+random()*.8);
      const da=Math.min(.3,w/Math.max(perimeter,2)),end=Math.min(TAU,a+da),mid=(a+end)/2;
      const dip=f.x*dipX+f.z*dipZ;
      const tt=clamp(t+dip/H,.08,.975);
      if(random()>.2){
        const bottom=surface(mid,Math.max(.01,tt-(next-y)/H/2)),top=surface(mid,Math.min(.995,tt+(next-y)/H/2));
        const sx=surface(a,tt).distanceTo(surface(end,tt))*1.16;
        const sy=Math.min(bottom.distanceTo(top),(next-y)*2)*(1.03+random()*.22);
        const thickness=profile.thickness*(.8+random()*.7);
        const tilt=geology==='slate'?.28+random()*.2:Math.atan(dipX*Math.sin(mid)-dipZ*Math.cos(mid));
        rock(mid,tt,sx,sy,thickness,tilt,random);
      }
      a=end;
    }
    y=next;
  }
  }
  if(debris){
    const count=Math.round(R*(lowPoly?2.3:geology==='slate'?30:23)*fracture/65);
    for(let i=0;i<count;i++){
      const a=talus()*TAU,t=.015+Math.pow(talus(),.65)*.16;
      const s=(lowPoly? .4:.18)+Math.pow(talus(),2)*(lowPoly?4:geology==='sandstone'?2.3:1.5);
      rock(a,t,s*(geology==='slate'?1.8:1.2),s*.8,s*(geology==='slate'?.15:.65),talus()*TAU,talus,true);
    }
  }
  buckets.forEach((instances,i)=>{
    const mesh=new THREE.InstancedMesh(templates[i],material,instances.length);mesh.name=`${geology}_mountain_rock_${i+1}`;
    instances.forEach((v,j)=>{mesh.setMatrixAt(j,v.matrix);mesh.setColorAt(j,v.color);});mesh.instanceMatrix.needsUpdate=true;mesh.castShadow=true;mesh.receiveShadow=true;group.add(mesh);
  });
  geometry.computeBoundingBox();
  const actualSummit=new THREE.Vector3();
  const positions=geometry.attributes.position;
  for(let i=0;i<positions.count;i++)if(positions.getY(i)>actualSummit.y)actualSummit.fromBufferAttribute(positions,i);
  const baseTriangles=geometry.attributes.position.count/3;
  const triangleCount=baseTriangles+group.children.slice(1).reduce((n,o)=>n+o.count*o.geometry.attributes.position.count/3,0);
  group.userData={cliffShape,baseTriangles,triangleCount,meshStyle:lowPoly?'lowpoly':'detailed',mode:'mountain',formation:geology,rockCount,faceRockCount,debrisCount,depth:H,outer:R*1.3,ridges,settings:{...settings,cliffShape,mode:'mountain'},summit:escarpment?{x:actualSummit.x,y:actualSummit.y,z:actualSummit.z}:{x:-R*.26,y:H,z:R*.09}};
  return group;
}
