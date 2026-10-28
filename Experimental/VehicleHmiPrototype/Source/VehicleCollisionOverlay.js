import {VehicleGeometry} from './VehicleSpecification.js';
import {ProjectGizmo} from './TransformGizmo.js';
import {TransformPoint} from './TransformProjection.js';
// Opt-in projected collision diagnostics. No scene geometry or collision state is mutated.
export class VehicleCollisionOverlay{
 constructor(H){this.Host=H;const Stage=document.getElementById('stage');if(!Stage)return;this.Canvas=document.createElement('canvas');this.Canvas.className='vehicle-collision-overlay';this.Canvas.setAttribute('aria-label','Vehicle collision diagnostic x-ray');Object.assign(this.Canvas.style,{position:'absolute',inset:'0',width:'100%',height:'100%',pointerEvents:'none',zIndex:12,display:'none'});Stage.appendChild(this.Canvas);}
 Draw(){const H=this.Host,C=this.Canvas,State=H.VehicleInspector;if(!C)return;if(!State?.ShowCollision||!H.CameraState){C.style.display='none';return;}C.style.display='block';const B=C.getBoundingClientRect(),W=Math.round(B.width),Height=Math.round(B.height);if(W<1||Height<1)return;if(C.width!==W||C.height!==Height){C.width=W;C.height=Height;}const X=C.getContext('2d');if(!X?.beginPath)return;X.clearRect(0,0,W,Height);
  const Project=P=>ProjectGizmo(P,H.CameraState,W,Height),Body=H.Scene.Occupants[H.Scene.VehicleOccupants.Body];
  X.fillStyle='#e9cb77';X.globalAlpha=.75;X.beginPath();for(const P of H.Vehicle.HullPoints??[]){const Q=Project(TransformPoint(Body.Transform,P));if(!Q)continue;X.moveTo(Q[0]+1.7,Q[1]);X.arc(Q[0],Q[1],1.7,0,Math.PI*2);}X.fill();X.globalAlpha=1;
  for(let I=0;I<4;I++){const Wheel=H.Vehicle.Wheels[I],T=H.Scene.Occupants[H.Scene.VehicleOccupants.Wheels[I]].Transform;X.strokeStyle=I===State.Wheel?'#fff1ab':'#7ed4ce';X.lineWidth=1.2;
   for(const Y of [-VehicleGeometry.TyreWidth*.5,VehicleGeometry.TyreWidth*.5]){X.beginPath();let Started=false;for(let J=0;J<=40;J++){const A=J/40*Math.PI*2,Q=Project(TransformPoint(T,[Math.cos(A)*Wheel.Radius,Y,Math.sin(A)*Wheel.Radius]));if(!Q){Started=false;continue;}if(!Started){X.moveTo(Q[0],Q[1]);Started=true;}else X.lineTo(Q[0],Q[1]);}X.stroke();}
   if(Wheel.ContactPoint){const A=Project(Wheel.ContactPoint),B=Project(Wheel.ContactPoint.map((V,I)=>V+(Wheel.GroundNormal?.[I]??(I===2?1:0))*.5));if(A&&B){X.beginPath();X.moveTo(A[0],A[1]);X.lineTo(B[0],B[1]);X.stroke();}}
  }
  X.fillStyle='#11181ce8';X.fillRect(16,90,330,46);X.fillStyle='#e8d496';X.font='11px sans-serif';X.fillText('COLLISION X-RAY · hull sample centres + wheel radii',26,109);X.fillStyle='#a5c4c0';X.fillText('Not render meshes / not the Jolt convex surface',26,126);
 }
}
