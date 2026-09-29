import * as THREE from 'three';
import {SurfaceBuilder} from './mesh.js';
import {volume,buildSkin,fieldValue} from './implicit.js';
import {frogMaterials,skinColor,noise} from './appearance.js';
const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z),C=x=>new THREE.Color(x),TAU=Math.PI*2;
const clamp=THREE.MathUtils.clamp,lerp=THREE.MathUtils.lerp;
const smooth=(a,b,x)=>{const t=clamp((x-a)/(b-a),0,1);return t*t*(3-2*t);};

/** Original neutral-colour frog study. Centimetre authoring; metre GLB output. */
export function createFrog(){
  const root=new THREE.Group();root.name='Rana_temporaria';
  const materials=frogMaterials(),builder=new SurfaceBuilder(materials),bones=[],index={},rest={},shapes=[];
  function bone(name,point,parent=null){const b=new THREE.Bone();b.name=name;b.position.copy(parent?point.clone().sub(rest[parent].world):point);if(parent)bones[index[parent]].add(b);else root.add(b);index[name]=bones.length;bones.push(b);rest[name]={world:point.clone(),local:b.position.clone()};return index[name];}
  const body=bone('Root',V()),pelvis=bone('Pelvis',V(0,1.35,-1.90),'Root'),spine=bone('Spine',V(0,1.65,-.1),'Pelvis'),head=bone('Head',V(0,2.08,1.72),'Spine'),throat=bone('Throat',V(0,1.26,2.18),'Head');
  function addVolume(center,radii,bn,rotation,region='body'){shapes.push(volume(center,radii,bn,rotation,region));}
  function muscle(a,b,width,depth,bn,region){const rotation=new THREE.Quaternion().setFromUnitVectors(V(0,1,0),b.clone().sub(a).normalize());addVolume(a.clone().lerp(b,.5),V(width,a.distanceTo(b)/2+.085,depth),bn,rotation,region);}
  addVolume(V(0,1.35,-1.66),V(1.56,.82,1.80),pelvis);
  addVolume(V(0,1.65,-.33),V(1.80,1.04,2.24),spine);
  addVolume(V(0,1.95,1.87),V(1.61,.73,1.52),head);
  addVolume(V(0,1.73,2.96),V(1.18,.43,.55),head);
  const fore=[],hind=[],eyes=[],digits=[];
  for(const s of [-1,1]){
    const side=s<0?'L':'R';
    const eye=bone(`Eye_${side}`,V(s*1.01,2.68,2.57),'Head');eyes.push({s,side,bone:eye,center:rest[`Eye_${side}`].world.clone()});
    addVolume(V(s*1.01,2.53,2.27),V(.48,.33,.48),head);
    const hip=V(s*1.32,1.45,1.33),elbow=V(s*2.25,.74,1.12),wrist=V(s*2.04,.15,2.62);
    const a=bone(`Fore_${side}_Upper`,hip,'Spine'),b=bone(`Fore_${side}_Forearm`,elbow,`Fore_${side}_Upper`),c=bone(`Fore_${side}_Hand`,wrist,`Fore_${side}_Forearm`);
    muscle(hip,elbow,.34,.30,a,'arm');muscle(elbow,wrist,.25,.24,b,'arm');addVolume(wrist.clone().add(V(0,.005,.09)),V(.31,.12,.30),c,undefined,'arm');
    fore.push({s,side,hip,knee:elbow,ankle:wrist,a,b,c,parent:spine});
    const hh=V(s*1.12,1.45,-1.72),knee=V(s*2.53,.86,-.40),ankle=V(s*2.58,.30,-2.21),heel=V(s*2.87,.14,-.68);
    const ha=bone(`Hind_${side}_Thigh`,hh,'Pelvis'),hb=bone(`Hind_${side}_Shin`,knee,`Hind_${side}_Thigh`),hc=bone(`Hind_${side}_Tarsus`,ankle,`Hind_${side}_Shin`),hd=bone(`Hind_${side}_Foot`,heel,`Hind_${side}_Tarsus`);
    muscle(hh,knee,.72,.63,ha,'thigh');muscle(knee,ankle,.36,.33,hb,'shin');muscle(ankle,heel,.17,.16,hc,'shin');addVolume(heel.clone().add(V(0,.015,.13)),V(.29,.115,.33),hd,undefined,'shin');
    hind.push({s,side,hip:hh,knee,ankle,heel,a:ha,b:hb,c:hc,d:hd});
  }
  buildSkin(builder,shapes,skinColor,{step:.075});
  function surface(axis,a,b,sign=1){let lo=0,hi=4.2;for(let i=0;i<30;i++){const m=(lo+hi)/2,p=axis==='z'?V(a,b,m):axis==='x'?V(m,a,b):V(a,m,b);if(fieldValue(shapes,p.x,p.y,p.z)<0)lo=m;else hi=m;}return (lo+hi)/2*sign;}
  // Skin folds follow the unified skin surface, rather than floating above it.
  for(const s of [-1,1]){
    const points=Array.from({length:46},(_,i)=>{const t=i/45,z=lerp(1.90,-2.80,t),x=s*(.92+.35*Math.sin(Math.PI*t));return V(x,surface('y',x,z)+.014,z);});
    builder.tube('Dorsolateral folds',points,t=>.022+.033*Math.sin(Math.PI*t),C('#a69d7c'),(t)=>t<.57?[head,spine,clamp(t/.57,0,1)]:[spine,pelvis,(t-.57)/.43],110,10);
    const y=2.03,z=1.55,x=surface('x',y,z,s)+s*.012;
    builder.ellipsoid('Tympanum',V(x,y,z),V(.026,.236,.253),C('#bbb191'),head,48,28);
    const rim=Array.from({length:49},(_,i)=>V(x+s*.009,y+Math.cos(i/48*TAU)*.242,z+Math.sin(i/48*TAU)*.258));
    builder.tube('Mouth and nostrils',rim,.008,C('#a69a7e'),head,64,6);
    const nx=s*.54,ny=2.0,nz=surface('z',nx,ny)+.008;
    builder.ellipsoid('Mouth and nostrils',V(nx,ny,nz),V(.048,.025,.016),C('#b5a98b'),head,28,16);
  }
  const mouth=Array.from({length:65},(_,i)=>{const x=lerp(-1.47,1.47,i/64),y=1.59-.07*Math.abs(x)/1.47;return V(x,y,surface('z',x,y)+.012);});
  builder.tube('Mouth and nostrils',mouth,.013,C('#aaa184'),head,130,7);
  builder.tube('Ventral skin',mouth.map(p=>p.clone().add(V(0,-.036,.005))),.022,C('#c7bda1'),head,110,8);
  builder.ellipsoid('Ventral skin',V(0,1.27,2.21),V(.96,.23,.84),C('#c6bea7'),throat,64,36);
  const eyeQ=(s)=>new THREE.Quaternion().setFromUnitVectors(V(0,0,1),V(s*.30,.10,1).normalize());
  const lidClosed=[];
  for(const eye of eyes){
    const q=eyeQ(eye.s),ec=eye.center;
    // Iris and pupil pigments follow one continuous curved corneal surface.
    // No separate flat iris plate or floating black pupil geometry.
    builder.ellipsoid('Eye globe',ec,V(.405,.355,.34),(u,v)=>{
      const a=Math.PI*u,b=TAU*v,x=Math.sin(a)*Math.cos(b),y=-Math.cos(a),z=-Math.sin(a)*Math.sin(b);
      const pupil=(x/.72)**2+(y/.255)**2;
      const theta=Math.atan2(y,x),grain=noise(theta*24,u*60,5);
      const iris=C('#a4956d').lerp(C('#62583c'),.20+grain*.30).multiplyScalar(.92+.10*Math.max(0,z));
      return z>0?iris.lerp(C('#121610'),1-smooth(.92,1.08,pupil)):iris;
    },eye.bone,160,100,q);
    for(const sign of [1,-1]){
      const lidPoint=(u,v,closed)=>{const a=u*lerp(1.00,Math.PI/2,closed),phi=v*Math.PI;return V(Math.sin(a)*Math.cos(phi)*.418,sign*Math.cos(a)*.365,Math.sin(a)*Math.sin(phi)*.357).applyQuaternion(q).add(ec);};
      builder.grid('Eyelids',24,64,(u,v)=>lidPoint(u,v,0),C('#887e66'),eye.bone,sign===-1);
      for(let i=0;i<=24;i++)for(let j=0;j<=64;j++)lidPoint(i/24,j/64,1).toArray(lidClosed,lidClosed.length);
    }
  }
  function digit(points,radius,bn,color){
    builder.tube('Skin',points,t=>radius*(.90-.38*t+.10*Math.sin(t*TAU*3)),color,bn,26,10);
    builder.ellipsoid('Ventral skin',points.at(-1),V(radius*.62,radius*.40,radius*.77),color,bn,20,12);
  }
  for(const leg of fore){
    for(let i=0;i<4;i++){
      const k=i-1.5,length=[.63,.81,.84,.66][i],a=leg.ankle.clone().add(V(leg.s*k*.115,0,.12));
      const end=a.clone().add(V(leg.s*k*.25,-.125,length));
      const b=bone(`Finger_${leg.side}_${i+1}`,a,`Fore_${leg.side}_Hand`);digits.push({bone:b,s:leg.s,hind:false,i});
      digit([a,a.clone().lerp(end,.38).add(V(0,.036,0)),a.clone().lerp(end,.72),end],.055,b,C('#a89b7d'));
    }
  }
  for(const leg of hind){
    const toes=[];
    for(let i=0;i<5;i++){
      const k=i-2,len=[.62,.89,1.13,1.33,.96][i],a=leg.heel.clone().add(V(leg.s*k*.096,0,.12));
      const end=a.clone().add(V(leg.s*k*.24,-.12,len));
      const b=bone(`Toe_${leg.side}_${i+1}`,a,`Hind_${leg.side}_Foot`);digits.push({bone:b,s:leg.s,hind:true,i});
      const points=[a,a.clone().lerp(end,.4).add(V(0,.017,0)),a.clone().lerp(end,.75),end];
      toes.push({a,end,b});digit(points,.054,b,C('#a39778'));
    }
    for(let i=0;i<4;i++){
      const a=toes[i],b=toes[i+1];
      builder.grid('Webbing',18,14,(u,v)=>{
        const reach=.72-.26*Math.sin(Math.PI*v),left=a.a.clone().lerp(a.end,u*reach),right=b.a.clone().lerp(b.end,u*reach);
        return left.lerp(right,v).add(V(0,.006*Math.sin(Math.PI*v)*Math.sin(Math.PI*u),0));
      },(u,v)=>C('#b3a787').multiplyScalar(.93+.05*Math.cos(v*TAU*2)),(u,v)=>[a.b,b.b,v],leg.s<0);
    }
  }
  root.updateMatrixWorld(true);const skeleton=new THREE.Skeleton(bones),meshes=builder.build(root,skeleton);
  const lids=meshes.find(m=>m.material.name==='Eyelids');lids.name='Frog_eyelids';
  const lidAttribute=new THREE.Float32BufferAttribute(lidClosed,3);lidAttribute.name='Blink';
  lids.geometry.morphAttributes.position=[lidAttribute];
  const closedLids=lids.geometry.clone();closedLids.setAttribute('position',lidAttribute);closedLids.computeVertexNormals();
  const closedNormals=closedLids.attributes.normal;
  for(let i=0;i<closedNormals.count;i++)if(Math.hypot(closedNormals.getX(i),closedNormals.getY(i),closedNormals.getZ(i))<.5){const n=lids.geometry.attributes.normal;closedNormals.setXYZ(i,n.getX(i),n.getY(i),n.getZ(i));}
  lids.geometry.morphAttributes.normal=[closedNormals.clone()];closedLids.dispose();
  lids.updateMorphTargets();lids.morphTargetInfluences[0]=0;
  const X=V(1,0,0),I=new THREE.Quaternion();
  function reset(){for(const b of bones){b.position.copy(rest[b.name].local);b.quaternion.identity();b.scale.set(1,1,1);}lids.morphTargetInfluences[0]=0;}
  function pose(name,t){
    reset();const cycle=t/6*TAU;
    let blink=0;
    if(name==='Idle')blink=smooth(2.1,2.20,t)*(1-smooth(2.27,2.42,t))+smooth(4.75,4.86,t)*(1-smooth(4.92,5.06,t));
    if(name==='Blink')blink=smooth(.35,.52,t)*(1-smooth(.70,.96,t));
    lids.morphTargetInfluences[0]=blink;
    bones[throat].scale.y=name==='Idle'?1+.045*Math.sin(cycle*3):1;
    eyes.forEach(e=>bones[e.bone].position.y-=.060*blink);

  }
  const clips=[],metadata={Idle:{duration:6,loop:true,description:'Quiet resting posture, subtle throat movement and occasional blink.'},Blink:{duration:1.6,loop:false,description:'A short eyelid study.'}};
  for(const [name,data] of Object.entries(metadata)){
    const times=[],qs=bones.map(()=>[]),ps=bones.map(()=>[]),scales=[],weights=[];
    for(let f=0;f<=Math.round(data.duration*60);f++){
      const t=f/60;times.push(t);pose(name,t);bones.forEach((b,i)=>{b.quaternion.toArray(qs[i],qs[i].length);b.position.toArray(ps[i],ps[i].length);});scales.push(bones[throat].scale.y);weights.push(lids.morphTargetInfluences[0]);
    }
    const tracks=bones.map((b,i)=>new THREE.QuaternionKeyframeTrack(`${b.name}.quaternion`,times,qs[i]));
    for(const b of [body,...eyes.map(e=>e.bone)])tracks.push(new THREE.VectorKeyframeTrack(`${bones[b].name}.position`,times,ps[b]));
    tracks.push(new THREE.VectorKeyframeTrack('Throat.scale',times,scales.flatMap(s=>[1,s,1])));
    tracks.push(new THREE.NumberKeyframeTrack('Frog_eyelids.morphTargetInfluences[Blink]',times,weights));
    const clip=new THREE.AnimationClip(name,data.duration,tracks);clip.optimize();clips.push(clip);
  }
  reset();root.updateMatrixWorld(true);root.scale.setScalar(.01);
  const triangles=meshes.reduce((n,m)=>n+m.geometry.index.count/3,0);
  root.userData={species:'Rana temporaria',design:'Neutral olive-brown / stone study',units:'metres',snoutVentLengthMm:70,bones:bones.length,triangles,frontDigitsPerFoot:4,hindDigitsPerFoot:5,animationMetadata:metadata,provenance:'Original procedural surface study. Not a scan or a validated AAA production asset.'};
  return {root,clips,skeleton,materials,metadata,bones:bones.length,triangles};
}
