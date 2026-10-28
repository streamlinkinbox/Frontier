// Object-local 3D editor gizmo. Only explicit Outliner selections in Free mode can be manipulated.
import {ResolveEditorTransform,EditorGizmoBasis} from './EditorTransforms.js';
import {CreateCelestialGizmo} from './CelestialGizmoRenderer.js';
const Colours={x:'#e04b4b',y:'#4fd35a',z:'#3d7bff'},AxisIndex={x:0,y:1,z:2};
export const GizmoNavigationKeys=['KeyW','KeyA','KeyS','KeyD','KeyQ','KeyE','ArrowUp','ArrowDown','ArrowLeft','ArrowRight'];
const Dot=(A,B)=>A.reduce((S,V,I)=>S+V*B[I],0);
export function CanUseEditorGizmo(H,Shell){return !!H.FreeCamera&&!!Shell.GizmoSelection&&Shell.GizmoSelection===Shell.Selected&&!H.ParticleWorkspace?.Active;}
export function ProjectGizmo(P,C,W,H){const V=P.map((X,I)=>X-C.Position[I]),D=Dot(V,C.Forward);if(D<=.05)return null;return [W/2+Dot(V,C.Right)/D/C.TanY*H/2,H/2-Dot(V,C.Up)/D/C.TanY*H/2,D];}
export class TransformGizmo{
 constructor(Shell){
  this.Shell=Shell;this.Host=Shell.Host;this.Mode='g';this.Stage=document.getElementById('stage');if(!this.Stage)return;
  this.Ready=CreateCelestialGizmo(this.Stage).then(G=>{this.Graphics=G;this.Draw();}).catch(E=>{this.Error=String(E);this.Shell.Announce?.('Celestial gizmo could not initialise WebGL');console.warn(this.Error);});
  this.Hud=document.createElement('div');this.Hud.className='celestial-gizmo-hud';this.Hud.hidden=true;
  Object.assign(this.Hud.style,{position:'absolute',left:'50%',top:'86px',transform:'translateX(-50%)',zIndex:13,alignItems:'center',gap:'10px',padding:'8px 14px',borderRadius:'14px',background:'rgba(15,16,18,.74)',backdropFilter:'blur(28px)',border:'1px solid rgba(255,255,255,.08)',fontSize:'12px',color:'#b3b5bd',whiteSpace:'nowrap'});this.Stage.appendChild(this.Hud);
  this.Stage.addEventListener('pointerdown',E=>{
   if(!this.IsViewportEvent(E)||!this.Target())return;
   // Never capture camera look, right/middle mouse, or a click while flying with WASD.
   const Flying=GizmoNavigationKeys.some(K=>this.Host.Keys?.has(K));
   const Info=E.button===0&&!Flying?this.Graphics?.pick(E.clientX,E.clientY):null;
   if(!Info){this.CameraGesture=true;this.ClearHover();return;}
   if(this.Begin(E,Info.type==='rotate'?'r':Info.type==='scale'?'s':'g',Info.type==='plane'?null:Info.axis,Info.type==='plane'?Info.axis:null)){
    this.StopEvent(E);this.Stage.setPointerCapture?.(E.pointerId);this.Drag.PointerId=E.pointerId;this.Stage.style.cursor='grabbing';
   }
  },true);
  window.addEventListener('pointermove',E=>{
   this.Mouse=[E.clientX,E.clientY];
   if(this.Drag){if(!this.Target()){this.Cancel();return;}this.StopEvent(E);this.Move(E);return;}
   if(this.Target()&&!this.CameraGesture&&this.IsViewportEvent(E)&&!GizmoNavigationKeys.some(K=>this.Host.Keys?.has(K))){
    this.Stage.style.cursor=this.Graphics?.hover(E.clientX,E.clientY)?'grab':'';
   }else this.ClearHover();
  },true);
  window.addEventListener('pointerup',()=>{this.CameraGesture=false;if(this.Drag)this.End();},true);
  this.Stage.addEventListener('pointercancel',()=>{this.CameraGesture=false;this.Cancel();this.ClearHover();});
  this.Stage.addEventListener('lostpointercapture',()=>{this.CameraGesture=false;if(this.Drag)this.Cancel();});
  // No G/R/S/F modal shortcuts: S is camera reverse, R/F already have app controls.
  window.addEventListener('keydown',E=>{
   if(GizmoNavigationKeys.includes(E.code)||E.code==='F8'||E.code==='KeyF'){this.Cancel();this.ClearHover();return;}
   if(!this.Drag)return;
   if(/INPUT|TEXTAREA|SELECT/.test(E.target.tagName)||E.target.isContentEditable)return;
   const K=E.key.toLowerCase();
   if(K==='escape')this.Cancel();else if(K==='enter')this.End();
   else if(K in AxisIndex){this.Drag.Axis=this.Drag.Axis===K?null:K;this.Drag.Plane=null;this.Move(this.Drag.Last??this.Drag.MouseEvent);}else return;
   this.StopEvent(E);
  },true);
  window.addEventListener('blur',()=>{this.CameraGesture=false;this.Cancel();this.ClearHover();});
 }
 IsViewportEvent(E){return E.target===this.Host.Canvas||E.target===this.Stage;}
 StopEvent(E){E.preventDefault?.();E.stopImmediatePropagation?.();}
 ClearHover(){this.Graphics?.hover(-1e6,-1e6);if(this.Stage&&!this.Drag)this.Stage.style.cursor='';}
 FrameSelection(){const T=this.Target();if(!T)return;const P=T.Get();this.Host.Flight.Eye=P.map((V,I)=>V+[-18,-18,12][I]);this.Host.Flight.Yaw=Math.PI/4;this.Host.Flight.Pitch=-Math.atan2(12,Math.hypot(18,18));}
 Target(){return CanUseEditorGizmo(this.Host,this.Shell)?ResolveEditorTransform(this.Host,this.Shell.Selected):null;}
 Begin(E,Mode='g',Axis=null,Plane=null){
  const T=this.Target(),C=this.Host.CameraState;if(!T||!C||this.CameraGesture||GizmoNavigationKeys.some(K=>this.Host.Keys?.has(K))||Mode==='r'&&!T.Rotate||Mode==='s'&&!T.SetScale)return false;
  const B=this.Stage.getBoundingClientRect(),Start=T.Get(),P=ProjectGizmo(Start,C,B.width,B.height);if(!P)return false;
  P[0]+=B.left;P[1]+=B.top;
  this.Drag={T,Mode,Axis,Plane,Basis:EditorGizmoBasis(T).map(V=>V.slice()),Start,Screen:P,Mouse:[E.clientX,E.clientY],MouseEvent:E,Camera:structuredClone(C),Id:this.Shell.Selected,Angle:Math.atan2(E.clientY-P[1],E.clientX-P[0]),Distance:Math.max(12,Math.hypot(E.clientX-P[0],E.clientY-P[1])),Rotation:Mode==='r'?T.GetRotationState():null,Scale:Mode==='s'?T.GetScale():null};
  this.Mode=Mode;if(this.Shell.Selected==='vehicle'){this.Drag.WasFrozen=this.Host.Vehicle.Frozen;this.Host.Vehicle.Frozen=true;}
  this.Move(E);this.Draw();return true;
 }
 Move(E){
  const D=this.Drag;if(!D)return;if(!this.Target()||D.Id!==this.Shell.Selected){this.Cancel();return;}D.Last=E;
  const B=this.Stage.getBoundingClientRect(),P=D.Screen,DX=E.clientX-D.Mouse[0],DY=E.clientY-D.Mouse[1],PerPixel=2*P[2]*D.Camera.TanY/B.height;
  if(!D.HasMoved&&DX===0&&DY===0)return;D.HasMoved=true;
  const Project=World=>{const Q=ProjectGizmo(World,D.Camera,B.width,B.height);return Q&&[Q[0]+B.left,Q[1]+B.top];};
  const Axis=Name=>D.Basis[AxisIndex[Name]],Offset=Unit=>Project(D.Start.map((N,I)=>N+Unit[I]));
  if(D.Mode==='g'){
   let Delta;
   if(D.Plane){const [U,V]={x:[1,2],y:[0,2],z:[0,1]}[D.Plane],Q=Offset(D.Basis[U]),R=Offset(D.Basis[V]);if(!Q||!R)return;const a=Q[0]-P[0],b=R[0]-P[0],c=Q[1]-P[1],d=R[1]-P[1],Det=a*d-b*c;if(Math.abs(Det)<1e-6)return;let S=(DX*d-b*DY)/Det,T=(a*DY-c*DX)/Det;if(E.ctrlKey){S=Math.round(S*4)/4;T=Math.round(T*4)/4;}Delta=D.Start.map((_,I)=>D.Basis[U][I]*S+D.Basis[V][I]*T);}
   else if(D.Axis){const Unit=Axis(D.Axis),Q=Offset(Unit);if(!Q)return;const X=Q[0]-P[0],Y=Q[1]-P[1],Len=X*X+Y*Y;if(Len<1e-6)return;let Amount=(DX*X+DY*Y)/Len;if(E.ctrlKey)Amount=Math.round(Amount*4)/4;Delta=Unit.map(V=>V*Amount);}
   else{let X=DX*PerPixel,Y=-DY*PerPixel;if(E.ctrlKey){X=Math.round(X*4)/4;Y=Math.round(Y*4)/4;}Delta=D.Start.map((_,I)=>D.Camera.Right[I]*X+D.Camera.Up[I]*Y);}
   D.T.Set(D.Start.map((V,I)=>V+Delta[I]));D.Readout=`local ${D.Axis?.toUpperCase()??(D.Plane?D.Plane.toUpperCase()+'-plane':'view')} ${Math.hypot(...Delta).toFixed(2)} m`;
  }else if(D.Mode==='r'){
   let Angle=Math.atan2(E.clientY-P[1],E.clientX-P[0])-D.Angle;if(E.ctrlKey)Angle=Math.round(Angle/(Math.PI/36))*Math.PI/36;
   const Unit=D.Axis?Axis(D.Axis):D.Camera.Forward,Sign=D.Axis?(Dot(Unit,D.Camera.Forward)>0?1:-1):1;
   D.T.Rotate(Unit,-Angle*Sign,D.Rotation,D.Start);D.Readout=`local ${D.Axis?.toUpperCase()??'view'} ${(-Angle*Sign*180/Math.PI).toFixed(1)}°`;
  }else{
   let F=DX===0&&DY===0?1:Math.max(.01,Math.hypot(E.clientX-P[0],E.clientY-P[1])/D.Distance);
   if(D.Axis){const Q=Offset(Axis(D.Axis));if(!Q)return;const X=Q[0]-P[0],Y=Q[1]-P[1],Base=(D.Mouse[0]-P[0])*X+(D.Mouse[1]-P[1])*Y;if(Math.abs(Base)<1e-6)return;F=Math.max(.01,1+(DX*X+DY*Y)/Base);}
   if(E.ctrlKey)F=Math.max(.1,Math.round(F*10)/10);
   D.T.SetScale(D.Scale.map((V,I)=>!D.Axis||I===AxisIndex[D.Axis]?V*F:V));D.Readout=`${D.Axis?'local '+D.Axis.toUpperCase():'uniform'} ×${F.toFixed(2)}`;
  }
  this.Shell.Controls.Synchronise();this.Draw();
 }
 End(){const D=this.Drag;if(!D)return;D.T.Commit?.();this.Host.Telemetry?.Event('object_transform',{object:D.Id,mode:D.Mode,position:D.T.Get(),scale:D.T.GetScale?.()});this.Finish();}
 Cancel(){const D=this.Drag;if(!D)return;if(D.HasMoved){if(D.Mode==='s')D.T.SetScale(D.Scale);else if(D.Mode==='r')D.T.RestoreRotation(D.Rotation);else D.T.Set(D.Start);D.T.Commit?.();}this.Finish();this.Shell.Controls.Synchronise();}
 Finish(){const D=this.Drag;if(D&&'WasFrozen'in D)this.Host.Vehicle.Frozen=D.WasFrozen;this.Drag=null;if(D?.PointerId!==undefined&&this.Stage.hasPointerCapture?.(D.PointerId))this.Stage.releasePointerCapture(D.PointerId);this.Stage.style.cursor='';this.Draw();}
 Draw(){
  if(!this.Stage||!this.Hud)return;if(this.Drag&&(!CanUseEditorGizmo(this.Host,this.Shell)||this.Drag.Id!==this.Shell.Selected)){this.Cancel();return;}
  const T=this.Target(),C=this.Host.CameraState,B=this.Stage.getBoundingClientRect(),P=T&&C?ProjectGizmo(T.Get(),C,B.width,B.height):null;
  const Visible=!!P;this.Hud.hidden=!Visible;this.Hud.style.display=Visible?'flex':'none';
  if(!Visible){if(this.WasVisible)this.Graphics?.clear();this.WasVisible=false;this.DrawKey=null;if(!this.Drag)this.Stage.style.cursor='';return;}this.WasVisible=true;
  if(!this.Drag&&(this.Mode==='r'&&!T.Rotate||this.Mode==='s'&&!T.SetScale))this.Mode='g';
  const D=this.Drag,Mode=D?.Mode??this.Mode,Basis=EditorGizmoBasis(T);
  const Key=JSON.stringify([this.Shell.Selected,T.Get(),Basis,C,B.width,B.height,Mode,D?.Axis,D?.Readout,!!T.Rotate,!!T.SetScale,!!this.Graphics]);if(Key===this.DrawKey)return;this.DrawKey=Key;
  this.Graphics?.render({fov:2*Math.atan(C.TanY)*180/Math.PI,pos:C.Position,fwd:C.Forward,up:C.Up},T.Get(),P[2]*C.TanY*.22,Mode,D?{axis:D.Axis}:null,!!T.Rotate,!!T.SetScale,Basis);
  if(!D&&this.Mouse&&!this.CameraGesture&&!GizmoNavigationKeys.some(K=>this.Host.Keys?.has(K)))this.Graphics?.hover(...this.Mouse);
  this.Hud.replaceChildren();const Dot0=document.createElement('span');Object.assign(Dot0.style,{width:'8px',height:'8px',borderRadius:'50%',background:D?.Axis?Colours[D.Axis]:'#e8eef6'});this.Hud.appendChild(Dot0);
  const Label=document.createElement('span');Label.textContent=D?`${Mode==='g'?'Move':Mode==='r'?'Rotate':'Scale'} ${D.Readout??''}`:(this.Shell.Entities?.find(E=>E.Id===this.Shell.Selected)?.Name??this.Shell.Selected)+' · Local';this.Hud.appendChild(Label);
  for(const K of D?['Ctrl snap','Enter','Esc']:['g','r','s']){if(!D&&(K==='r'&&!T.Rotate||K==='s'&&!T.SetScale))continue;const Button=document.createElement(D?'span':'button');Button.textContent=D?K:{g:'Move',r:'Rotate',s:'Scale'}[K];Object.assign(Button.style,{fontSize:'10px',padding:'2px 6px',border:'none',borderRadius:'6px',background:!D&&K===Mode?'rgba(255,255,255,.22)':'rgba(255,255,255,.08)',color:'#c5c6cb',cursor:D?'default':'pointer'});if(!D){Button.type='button';Button.setAttribute('aria-label',`Gizmo ${K==='g'?'move':K==='r'?'rotate':'scale'} mode`);Button.addEventListener('click',()=>{this.Mode=K;this.Draw();});}this.Hud.appendChild(Button);}
 }
}
