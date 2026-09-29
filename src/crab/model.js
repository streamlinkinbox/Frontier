import * as THREE from 'three';
import {SurfaceBuilder} from '../frog/mesh.js';
import {crabMaterials,shellColor,limbColor} from './appearance.js';
import {CRAB,V,Y,rad,smooth,legLayout,clawLayout,clawPoint,lowerFinger,upperFinger,jawPivot,lowerRadius,upperRadius} from './anatomy.js';
import {CrabRig} from './rig.js';
const C=x=>new THREE.Color(x),TAU=Math.PI*2;

/** Original anatomical surface model; no photographed plane or hidden proxy animal. */
export function createCrab(){
  const root=new THREE.Group();root.name='Carcinus_maenas';const materials=crabMaterials(),builder=new SurfaceBuilder(materials),bones=[],index={},rest={};
  // Close every open limb loft. Exposed ring ends otherwise look like tubes,
  // especially while a joint flexes. Closed margin rings need no end caps.
  const tube=builder.tube.bind(builder);
  builder.tube=function(mat,points,radius,color,bn,segments=30,sides=12,ratio=1){
    const part=this.part(mat),start=part.p.length/3;tube(mat,points,radius,color,bn,segments,sides,ratio);
    if(points[0].distanceTo(points.at(-1))<1e-7)return;
    for(const end of [0,1]){const centre=this.vertex(part,points[end?points.length-1:0],[.5,end],typeof color==='function'?color(end,.5):color,typeof bn==='function'?bn(end,.5):bn),row=start+end*segments*(sides+1);
      for(let j=0;j<sides;j++)if(end)part.idx.push(centre,row+j,row+j+1);else part.idx.push(centre,row+j+1,row+j);
    }
  };
  const bone=(name,p,parent=null)=>{const b=new THREE.Bone();b.name=name;b.position.copy(parent?p.clone().sub(rest[parent]):p);if(parent)bones[index[parent]].add(b);else root.add(b);index[name]=bones.length;bones.push(b);rest[name]=p.clone();return index[name];};
  const body=bone('Root',V());
  const half=[[0,2.53],[.28,2.48],[.49,2.35],[.73,2.46],[1.04,2.30],[1.24,2.05],[1.55,2.02],[1.86,2.13],[2.13,2.47],[2.17,2.09],[2.61,2.08],[2.44,1.74],[3.03,1.60],[2.80,1.31],[3.39,1.05],[3.09,.81],[3.61,.46],[3.31,.23],[3.39,-.47],[3.10,-1.52],[2.57,-2.23],[1.63,-2.60],[.75,-2.72],[0,-2.76]];
  const outline=new THREE.CatmullRomCurve3([...half.map(p=>V(p[0],0,p[1])),...half.slice(1,-1).reverse().map(p=>V(-p[0],0,p[1]))],true,'catmullrom',.06);
  function relief(x,z){
    const bump=(cx,cz,w,d)=>Math.exp(-((x-cx)**2/w**2+(z-cz)**2/d**2));
    const neck=.56-.40*Math.abs(x),cervical=.073*Math.exp(-Math.pow((z-neck)/.11,2))*(1-smooth(2.1,2.9,Math.abs(x)));
    const lateral=.055*Math.exp(-Math.pow((Math.abs(x)-(1.0+.13*z))/.095,2))*(1-smooth(1.05,1.8,Math.abs(z+.35)));
    return .115*bump(0,.95,.8,.65)+.08*(bump(-1.5,-.45,.85,1.1)+bump(1.5,-.45,.85,1.1))+.07*bump(0,-1.2,.7,.6)-cervical-lateral;
  }
  const top=(u,v)=>{const edge=outline.getPoint(v%1),x=edge.x*u,z=edge.z*u,q=(x/3.61)**2+(z/3.03)**2;return V(x,2.21+.94*Math.pow(Math.max(.02,1-.72*q),1.3)+relief(x,z)*(1-smooth(.72,1.12,q)),z);};
  builder.grid('Carapace',110,240,top,(u,v)=>{const p=top(u,v);return shellColor(p.x,p.y,p.z);},body,true);
  builder.grid('Ventral plates',48,160,(u,v)=>{const e=outline.getPoint(v%1),r=1/Math.sqrt((e.x/3.06)**2+(e.z/2.35)**2);return V(e.x*r*u,1.28+.22*u*u,e.z*r*u);},C('#c4b790'),body);
  builder.grid('Limb cuticle',24,240,(u,v)=>{const e=outline.getPoint(v%1),inner=1/Math.sqrt((e.x/3.06)**2+(e.z/2.35)**2),r=THREE.MathUtils.lerp(1,inner,smooth(0,.42,u));return V(e.x*r,THREE.MathUtils.lerp(top(1,v).y-.075,1.50,u),e.z*r);},(u,v)=>{const e=outline.getPoint(v%1);return shellColor(e.x,2.0,e.z).lerp(C('#b2a681'),u*.62);},body,true);
  builder.grid('Carapace',8,240,(u,v)=>{const e=outline.getPoint(v%1);return V(e.x,top(1,v).y-.075*(1-u),e.z);},(u,v)=>{const e=outline.getPoint(v%1);return shellColor(e.x,2.1,e.z);},body);
  const margin=Array.from({length:161},(_,i)=>top(1,i/160).add(V(0,.01,0)));builder.tube('Limb cuticle',margin,.018,C('#b5ab77'),body,320,7);
  // Male's folded ventral apron, sternites and paired maxillipeds.
  // Five visible plates; the enlarged middle plate represents fused segments.
  const apron=[[-2.17,.78,.24],[-1.68,.69,.25],[-.91,.57,.58],[-.12,.41,.25],[.40,.25,.27]];
  apron.forEach(([z,w,length],i)=>builder.ellipsoid('Ventral plates',V(0,1.20,z),V(w,.073,length),C(i%2?'#c4bca0':'#b5aa86'),body,32,16));
  for(const s of [-1,1]){
    const side=s<0?'L':'R',eyeBase=V(s*1.58,2.40,2.18),eyeTip=V(s*1.89,2.67,2.60),eye=bone(`Eye_${side}`,eyeBase,'Root');
    builder.tube('Limb cuticle',[eyeBase,eyeBase.clone().lerp(eyeTip,.5),eyeTip],t=>.108-.035*t,C('#a5a078'),eye,24,16);
    builder.ellipsoid('Eyes',eyeTip,V(.152,.140,.156),C('#ffffff'),eye,48,28);
    const ab=V(s*.58,2.16,2.29),ant=bone(`Antenna_${side}`,ab,'Root');
    builder.tube('Limb cuticle',[ab,ab.clone().add(V(s*.12,.07,.28)),ab.clone().add(V(s*.21,.055,.66))],t=>.024*(1-t)+.003,C('#b8b087'),ant,30,7);
    const jaw=bone(`Maxilliped_${side}`,V(s*.28,1.63,2.26),'Root');
    builder.ellipsoid('Ventral plates',V(s*.27,1.63,2.25),V(.27,.33,.13),C('#b1a782'),jaw,40,22);
    for(let j=0;j<14;j++){const p=V(s*(.18+j*.014),1.40+j*.027,2.39);builder.tube('Setae',[p,p.clone().add(V(-s*.05,-.03,.08))],t=>.003*(1-t)+.0003,C('#eee4bc'),jaw,3,4);}
  }
  const joint=(p,r,b)=>builder.ellipsoid('Joint membranes',p,V(r*.80,r*.62,r*.80),C('#ddd3ac'),b,28,18);
  const segment=(a,b,r1,r2,bn,part)=>builder.tube('Limb cuticle',[a,a.clone().lerp(b,.48).add(V(0,.02,0)),b],t=>{const w=.63+.37*Math.pow(Math.sin(Math.PI*(.025+t*.95)),.65);return [r1*w,r2*w];},(t,v)=>limbColor(t,v,part),bn,34,20);
  for(const leg of legLayout()){
    const [attach,hip,knee,carpus,ankle,tip]=leg.points;
    const a=bone(`${leg.name}_Merus`,hip,'Root'),b=bone(`${leg.name}_Carpus`,knee,`${leg.name}_Merus`),c=bone(`${leg.name}_Propodus`,carpus,`${leg.name}_Carpus`),d=bone(`${leg.name}_Dactyl`,ankle,`${leg.name}_Propodus`);bone(`${leg.name}_Tip`,tip,`${leg.name}_Dactyl`);
    segment(attach,hip,.24,.17,body,leg.i);joint(hip,.15,a);
    segment(hip,knee,.36,.17,a,leg.i+2);joint(knee,.16,b);
    segment(knee,carpus,.24,.16,b,leg.i+3);joint(carpus,.125,c);
    segment(carpus,ankle,leg.i===3?.22:.17,leg.i===3?.10:.12,c,leg.i+4);joint(ankle,.08,d);
    builder.tube('Chelal fingers',[ankle,ankle.clone().lerp(tip,.50).add(V(leg.s*.085,-.01,0)),tip],t=>[.095*(1-t)**.8+.004,.077*(1-t)**.8+.003],t=>C('#9f9668').lerp(C('#423d2c'),smooth(.23,.97,t)),d,34,14);
    for(let j=0;j<8;j++){const p=carpus.clone().lerp(ankle,.15+j*.095).add(V(0,.07,0));builder.tube('Setae',[p,p.clone().add(V(0,.055,-.018))],t=>.002*(1-t)+.0002,C('#c6bd94'),c,3,4);}
  }
  for(const claw of clawLayout()){
    const a=bone(`${claw.name}_Merus`,claw.base,'Root'),b=bone(`${claw.name}_Carpus`,claw.elbow,`${claw.name}_Merus`),palm=bone(`${claw.name}_Propodus`,claw.wrist,`${claw.name}_Carpus`),dactyl=bone(`${claw.name}_Dactyl`,clawPoint(claw,jawPivot),`${claw.name}_Propodus`);
    bone(`${claw.name}_FixedTip`,clawPoint(claw,lowerFinger.getPoint(1)),`${claw.name}_Propodus`);bone(`${claw.name}_MovingTip`,clawPoint(claw,upperFinger.getPoint(1)),`${claw.name}_Dactyl`);
    joint(claw.base,.22,a);segment(claw.base,claw.elbow,.37,.29,a,7);joint(claw.elbow,.24,b);segment(claw.elbow,claw.wrist,.40,.33,b,8);joint(claw.wrist,.20,palm);
    const carpalTooth=claw.elbow.clone().lerp(claw.wrist,.35).add(V(-claw.s*.27,.06,0));builder.tube('Limb cuticle',[carpalTooth,carpalTooth.clone().add(V(-claw.s*.18,.10,.15)),carpalTooth.clone().add(V(-claw.s*.23,.10,.24))],t=>.105*(1-t)+.002,C('#a29b6b'),b,20,12);
    builder.grid('Limb cuticle',68,48,(u,v)=>{const bulge=Math.pow(Math.sin(Math.PI*(.02+u*.93)),.60),rx=.15+.34*bulge,ry=.19+.40*bulge,angle=v*TAU;return clawPoint(claw,V(Math.cos(angle)*rx,Math.sin(angle)*ry+.018*Math.sin(u*Math.PI),u*1.42));},(u,v)=>limbColor(u,v,9),palm);
    builder.ellipsoid('Limb cuticle',clawPoint(claw,V(0,0,1.40)),V(.27,.36,.13).multiplyScalar(claw.scale),(u,v)=>limbColor(u,v,9),palm,36,22,claw.frame);
    const finger=(curve,upper,bn)=>{
      const points=Array.from({length:45},(_,i)=>clawPoint(claw,curve.getPoint(i/44)));
      builder.tube('Chelal fingers',points,t=>{const r=(upper?upperRadius(t):lowerRadius(t))*claw.scale;return [r*.82,r];},t=>C('#b6aa78').lerp(C('#342d20'),smooth(.50,.93,t)),bn,88,20);
      const count=claw.s>0?6:10;
      for(let i=0;i<count;i++){
        const t=.18+i*(.66/(count-1)),p=curve.getPoint(t),tangent=curve.getTangent(t),normal=V(0,tangent.z,-tangent.y).normalize().multiplyScalar(upper?-1:1),r=upper?upperRadius(t):lowerRadius(t);
        p.addScaledVector(normal,r*.9);
        const size=(claw.s>0?(i===1?.105:.065):.041)*claw.scale;
        builder.ellipsoid('Denticles',clawPoint(claw,p),V(size*.70,size,size*.72),C('#d6c79d'),bn,18,12,claw.frame);
      }
    };
    finger(lowerFinger,false,palm);finger(upperFinger,true,dactyl);
  }
  root.updateMatrixWorld(true);const skeleton=new THREE.Skeleton(bones),meshes=builder.build(root,skeleton);root.scale.setScalar(.01);root.updateMatrixWorld(true);
  const rig=new CrabRig(root),clips=[],meta={Idle:{duration:6,loop:true},Walk_Left:{duration:CRAB.walkDuration,loop:true},Walk_Right:{duration:CRAB.walkDuration,loop:true},Pinch_L:{duration:1.8,loop:false},Pinch_R:{duration:1.8,loop:false}};
  for(const [name,data] of Object.entries(meta)){
    const rate=name.startsWith('Walk_')?CRAB.frameRate:60,times=[],rotations=bones.map(()=>[]),positions=[];
    for(let f=0;f<=Math.round(data.duration*rate);f++){const t=f/rate;times.push(t);rig.poseClip(name,t);bones.forEach((b,i)=>b.quaternion.toArray(rotations[i],rotations[i].length));bones[body].position.toArray(positions,positions.length);}
    const tracks=bones.map((b,i)=>new THREE.QuaternionKeyframeTrack(`${b.name}.quaternion`,times,rotations[i]));tracks.push(new THREE.VectorKeyframeTrack('Root.position',times,positions));const clip=new THREE.AnimationClip(name,data.duration,tracks);clip.optimize();clips.push(clip);
  }
  rig.reset();root.updateMatrixWorld(true);
  const triangles=meshes.reduce((n,m)=>n+m.geometry.index.count/3,0);
  root.userData={species:CRAB.species,sex:'male',units:'metres',carapaceWidthMm:72,walkingLegPairs:4,claws:2,anterolateralTeethPerSide:5,frontalLobes:3,triangles,bones:bones.length,animationMetadata:meta,lateralSpeedMetresPerSecond:CRAB.walkSpeed,provenance:'Original procedural anatomical study. Reference-informed, not a scan or verified AAA production asset.'};
  return {root,clips,rig,materials,bones:bones.length,triangles,metadata:meta};
}
