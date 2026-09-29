import * as THREE from 'three';
export const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
export const CRAB=Object.freeze({species:'Carcinus maenas',widthMm:72,walkDuration:1.6,walkSpeed:.01,swingFraction:.36,targetRadius:.0028,frameRate:120});
export const X=V(1,0,0),Y=V(0,1,0);
export const rad=THREE.MathUtils.degToRad;
export const clamp=THREE.MathUtils.clamp;
export const smooth=(a,b,x)=>{const t=clamp((x-a)/(b-a),0,1);return t*t*(3-2*t);};
export function legLayout(){
  const rows=[
    [[2.56,1.73,1.15],[2.95,1.85,1.53],[4.60,2.15,2.60],[5.00,1.50,2.80],[5.65,.43,3.00],[5.30,.003,3.25]],
    [[2.88,1.72,.27],[3.20,1.84,.38],[5.18,2.22,.82],[5.76,1.62,.92],[6.62,.46,1.10],[6.20,.003,1.23]],
    [[2.89,1.71,-.72],[3.22,1.76,-.84],[5.29,2.14,-1.18],[5.94,1.43,-1.39],[6.89,.46,-1.76],[6.45,.003,-1.98]],
    [[2.55,1.66,-1.46],[2.92,1.70,-1.73],[4.51,1.93,-2.75],[4.85,1.31,-3.23],[5.24,.45,-3.92],[4.96,.003,-4.05]],
  ];
  return [-1,1].flatMap(s=>rows.map((points,i)=>({s,i,side:s<0?'L':'R',name:`P${i+2}_${s<0?'L':'R'}`,points:points.map(p=>V(p[0]*s,p[1],p[2]))})));
}
export const lowerFinger=new THREE.CatmullRomCurve3([V(0,-.24,1.01),V(0,-.29,1.55),V(0,-.19,2.16),V(0,.015,2.58)],false,'centripetal');
export const upperFinger=new THREE.CatmullRomCurve3([V(0,.40,1.06),V(0,.36,1.47),V(0,.19,2.07),V(0,.020,2.56)],false,'centripetal');
export const jawPivot=V(0,.40,1.06);
export const lowerRadius=t=>.185*Math.pow(1-t,.65)+.026;
export const upperRadius=t=>.157*Math.pow(1-t,.65)+.022;
export function clawLayout(){return [-1,1].map(s=>({s,side:s<0?'L':'R',name:`Chel_${s<0?'L':'R'}`,scale:s>0?1.1:.90,
  base:V(s*1.60,1.66,1.53),elbow:V(s*3.66,1.43,2.02),wrist:V(s*3.50,1.58,3.23),frame:new THREE.Quaternion().setFromAxisAngle(Y,-s*.49)}));}
export function clawPoint(claw,point){return point.clone().multiplyScalar(claw.scale).applyQuaternion(claw.frame).add(claw.wrist);}
export function gaitFoot(time,leg,direction){
  const group=(leg.i+(leg.s>0?0:1))%2;
  const phase=((time/CRAB.walkDuration+group*.5)%1+1)%1;
  const advance=CRAB.walkSpeed*100*CRAB.walkDuration,stroke=advance*(1-CRAB.swingFraction);
  if(phase>=CRAB.swingFraction)return {lift:0,offset:direction*(stroke/2-advance*(phase-CRAB.swingFraction)),swing:false};
  const u=phase/CRAB.swingFraction,h=u*u*u*(10+u*(-15+6*u));
  return {lift:.26*64*u*u*u*(1-u)**3,offset:direction*(-stroke/2+advance*h-advance*CRAB.swingFraction*u),swing:true};
}
