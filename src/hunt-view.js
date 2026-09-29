import * as THREE from 'three';
import { HuntRig } from './hunt-rig.js';
import { HuntSimulation } from './hunt-simulation.js';
import { HUNT, clamp } from './hunt-config.js';
const $=s=>document.querySelector(s);
const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
const amber=new THREE.Color('#a97a3a'),green=new THREE.Color('#628647'),red=new THREE.Color('#b66b50');

/** Scene gizmo, pointer handling and DOM controls; behavior/math remain testable separately. */
export class HuntView {
  constructor({model,scene,camera,controls,canvas,onRun,invalidate,freezeCamera}){
    Object.assign(this,{model,scene,camera,controls,canvas,onRun,invalidate,freezeCamera});
    this.rig=new HuntRig(model);this.simulation=new HuntSimulation(this.rig);this.enabled=false;this.showGuides=true;
    this.group=new THREE.Group();this.group.name='Target_test_controller';this.group.visible=false;scene.add(this.group);
    this.targetMesh=new THREE.Mesh(new THREE.SphereGeometry(HUNT.radiusMetres,40,28),new THREE.MeshStandardMaterial({color:'#bb8745',roughness:.36,metalness:.12}));
    this.targetMesh.name='Draggable_test_bead';this.targetMesh.castShadow=true;this.targetMesh.receiveShadow=true;this.group.add(this.targetMesh);
    this.ring=new THREE.Mesh(new THREE.TorusGeometry(HUNT.radiusMetres*1.72,.000075,6,64),new THREE.MeshBasicMaterial({color:amber,transparent:true,opacity:.85,depthTest:false,depthWrite:false}));this.ring.renderOrder=10;this.group.add(this.ring);
    this.guideGroup=new THREE.Group();this.group.add(this.guideGroup);
    const positions=[],indices=[];
    for(let i=0;i<=40;i++){
      const a=THREE.MathUtils.degToRad(-30+i/40*60);
      for(const r of [.012,.036])positions.push(Math.sin(a)*r,.00007,HUNT.forelegOriginZ+Math.cos(a)*r);
      if(i<40){const n=i*2;indices.push(n,n+1,n+2,n+1,n+3,n+2);}
    }
    const arcGeometry=new THREE.BufferGeometry();arcGeometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));arcGeometry.setIndex(indices);
    this.arc=new THREE.Mesh(arcGeometry,new THREE.MeshBasicMaterial({color:'#99a482',side:THREE.DoubleSide,transparent:true,opacity:.08,depthWrite:false}));this.guideGroup.add(this.arc);
    for(const r of [.012,.036]){
      const points=Array.from({length:61},(_,i)=>{const a=THREE.MathUtils.degToRad(-30+i);return V(Math.sin(a)*r,.00010,HUNT.forelegOriginZ+Math.cos(a)*r);});
      const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineDashedMaterial({color:'#929e7e',dashSize:.0009,gapSize:.00065,transparent:true,opacity:.55,depthWrite:false}));line.computeLineDistances();this.guideGroup.add(line);
    }
    this.sightLine=this.line('#9e9b71',.5);this.stem=this.line('#a8ad97',.45);
    this.guideGroup.add(this.sightLine,this.stem);
    this.groundDot=new THREE.Mesh(new THREE.RingGeometry(.0013,.0015,40),new THREE.MeshBasicMaterial({color:'#a9a887',transparent:true,opacity:.6,side:THREE.DoubleSide,depthWrite:false}));this.groundDot.rotation.x=-Math.PI/2;this.guideGroup.add(this.groundDot);
    this.tag=document.createElement('div');this.tag.id='target-tag';this.tag.className='target-tag';this.tag.setAttribute('aria-hidden','true');this.tag.innerHTML='<span class="target-tag-dot"></span><span class="target-tag-text">DRAG TARGET</span>';
    this.canvas.parentElement.append(this.tag);this.tag.hidden=true;
    this.raycaster=new THREE.Raycaster();this.dragPlane=new THREE.Plane();this.pointer=null;
    this.bindControls();this.bindPointer();this.syncControls();
  }
  line(color,opacity){const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(new Float32Array(6),3));g.setAttribute('lineDistance',new THREE.Float32BufferAttribute(new Float32Array(2),1));return new THREE.Line(g,new THREE.LineDashedMaterial({color,transparent:true,opacity,dashSize:.0012,gapSize:.00065,depthWrite:false}));}
  setLine(line,a,b){const v=line.geometry.attributes.position;v.setXYZ(0,a.x,a.y,a.z);v.setXYZ(1,b.x,b.y,b.z);v.needsUpdate=true;line.geometry.computeBoundingSphere();const distance=line.geometry.attributes.lineDistance;distance.setX(0,0);distance.setX(1,a.distanceTo(b));distance.needsUpdate=true;}
  bindControls(){
    for(const key of ['angle','distance','height'])$(`#target-${key}`).addEventListener('input',e=>{
      this.simulation.setPlacement({[key]:Number(e.target.value)});this.syncControls();this.invalidate();
    });
    $('#hunt-auto').addEventListener('change',e=>{this.simulation.autoStrike=e.target.checked;if(e.target.checked)this.onRun();this.invalidate();});
    $('#hunt-guides').addEventListener('change',e=>{this.showGuides=e.target.checked;this.invalidate();});
    $('#hunt-lean').addEventListener('input',e=>{this.simulation.leanSeconds=Number(e.target.value);$('#hunt-lean-output').textContent=`${Number(e.target.value).toFixed(2)} s`;});
    document.querySelectorAll('[data-target-preset]').forEach(button=>button.addEventListener('click',()=>{
      this.simulation.setPlacement({angle:Number(button.dataset.targetPreset),distance:20,height:32});this.syncControls();this.invalidate();
    }));
    $('#hunt-command').addEventListener('click',()=>this.command());
    $('#hunt-reset').addEventListener('click',()=>this.reset());
  }
  command(){this.simulation.requestStrike();this.onRun();this.syncControls();this.invalidate();}
  reset(){this.endDrag();this.simulation.reset();this.syncControls();this.invalidate();}
  enable(){this.enabled=true;this.group.visible=true;this.tag.hidden=false;this.simulation.reset();this.syncControls();this.update(0);}
  disable(){this.endDrag();this.enabled=false;this.group.visible=false;this.tag.hidden=true;this.rig.reset();this.model.updateMatrixWorld(true);}
  ray(event){const rect=this.canvas.getBoundingClientRect();this.raycaster.setFromCamera(new THREE.Vector2((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1),this.camera);return this.raycaster.ray;}
  hit(event){return this.ray(event).intersectsSphere(new THREE.Sphere(this.simulation.target,HUNT.radiusMetres*2.1));}
  bindPointer(){
    this.canvas.addEventListener('pointerdown',event=>{
      if(!this.enabled||this.simulation.holding||this.simulation.phase==='releasing'||event.button!==0||!this.hit(event))return;
      event.preventDefault();event.stopImmediatePropagation();this.freezeCamera();
      this.pointer=event.pointerId;this.autoOrbit=this.controls.autoRotate;this.damping=this.controls.enableDamping;
      this.controls.autoRotate=false;this.controls.enableDamping=false;this.controls.update();this.controls.enabled=false;
      this.dragPlane.setFromNormalAndCoplanarPoint(this.camera.getWorldDirection(V()),this.simulation.target);
      const hit=this.ray(event).intersectPlane(this.dragPlane,V());this.dragOffset=this.simulation.target.clone().sub(hit||this.simulation.target);
      this.canvas.setPointerCapture(event.pointerId);this.simulation.setDragging(true);this.canvas.style.cursor='grabbing';
    },true);
    this.canvas.addEventListener('pointermove',event=>{
      if(!this.enabled)return;
      if(this.pointer===event.pointerId){
        event.preventDefault();event.stopImmediatePropagation();const point=this.ray(event).intersectPlane(this.dragPlane,V());
        if(point){this.simulation.setTarget(point.add(this.dragOffset));this.syncControls();this.invalidate();}
      }else if(this.pointer===null)this.canvas.style.cursor=!this.simulation.holding&&this.hit(event)?'grab':'';
    },true);
    const up=event=>{if(this.pointer===event.pointerId){event.preventDefault();event.stopImmediatePropagation();this.endDrag();}};
    this.canvas.addEventListener('pointerup',up,true);this.canvas.addEventListener('pointercancel',up,true);
    window.addEventListener('blur',()=>this.endDrag());
    document.addEventListener('keydown',e=>{if(e.key==='Escape'&&this.pointer!==null)this.endDrag();});
  }
  endDrag(){
    if(this.pointer===null)return;
    if(this.canvas.hasPointerCapture(this.pointer))this.canvas.releasePointerCapture(this.pointer);
    this.pointer=null;this.controls.enabled=true;this.controls.enableDamping=this.damping;this.controls.autoRotate=this.autoOrbit;
    this.simulation.setDragging(false);this.canvas.style.cursor='';this.invalidate();
  }
  syncControls(){
    const sim=this.simulation;
    for(const key of ['angle','distance','height']){
      const input=$(`#target-${key}`),value=sim.placement[key];
      input.value=value;input.disabled=sim.holding||sim.phase==='releasing';
      $(`#target-${key}-output`).textContent=key==='angle'?`${Math.abs(value)<.5?'0':value>0?'+'+Math.round(value):Math.round(value)}°`:`${value.toFixed(1)} mm`;
    }
    document.querySelectorAll('[data-target-preset]').forEach(b=>b.disabled=sim.holding||sim.phase==='releasing');
  }
  update(dt){
    if(!this.enabled)return;
    this.simulation.update(dt);
    const s=this.simulation.status,target=this.simulation.target;
    this.targetMesh.position.copy(target);this.ring.position.copy(target);this.ring.quaternion.copy(this.camera.quaternion);
    const color=s.holding?green:!s.reachable||s.phase==='released'?red:s.lockProgress>=1?green:amber;
    this.ring.material.color.copy(color);this.sightLine.material.color.copy(color);
    this.ring.visible=this.showGuides&&!s.holding&&s.phase!=='releasing';
    this.guideGroup.visible=this.showGuides;
    this.sightLine.visible=!s.holding&&s.phase!=='released';this.stem.visible=!s.holding;this.groundDot.visible=!s.holding;
    this.setLine(this.sightLine,this.rig.point('Head'),target);this.setLine(this.stem,V(target.x,.00012,target.z),target);
    this.groundDot.position.set(target.x,.00013,target.z);
    const projected=target.clone().project(this.camera),w=this.canvas.clientWidth,h=this.canvas.clientHeight;
    this.tag.style.left=`${clamp((projected.x+1)*w/2,60,w-60)}px`;this.tag.style.top=`${(1-projected.y)*h/2+24}px`;
    this.tag.hidden=!this.showGuides||Math.abs(projected.x)>1||Math.abs(projected.y)>1||projected.z>1;
    this.tag.classList.toggle('held',s.holding);
    this.tag.querySelector('.target-tag-text').textContent=s.holding?'GRIPPED':s.phase==='released'?'RELEASED':this.pointer!==null?'MOVING TARGET':'DRAG TARGET';
    this.setText('#hunt-state',s.message);this.setText('#hunt-phase',s.phase.toUpperCase());
    $('#hunt-lock-fill').style.width=`${Math.round(s.lockProgress*100)}%`;
    $('#hunt-lock-meter').setAttribute('aria-valuenow',Math.round(s.lockProgress*100));
    this.setText('#hunt-lock-value',`${Math.round(s.lockProgress*100)}%`);
    $('#hunt-status-card').classList.toggle('unreachable',!s.reachable&&!s.holding);
    $('#hunt-status-card').classList.toggle('acquired',s.lockProgress>=1||s.holding);
    this.setText('#hunt-range',s.holding?'Contact confirmed on both forelegs':s.phase==='released'?'Reset to place the target in the forward arc.':s.reachable?'Within reach · forward arc':this.simulation.reach.reason);
    this.setText('#hunt-instruction',s.holding?'The bead follows the closed grippers. Release it or reset the test.':s.phase==='leaning'?'Slow approach. Move the target to make it re-acquire.':s.phase==='striking'?'Aim committed. Capture now depends on contact.':s.phase==='recovering'?'Missed or interrupted. Reposition the bead and try again.':'Drag the amber bead, or use direction, distance and height.');
    const command=$('#hunt-command');
    this.setText('#hunt-command-text',s.holding?'Release target':s.phase==='released'?'Reset target':s.busy?'Sequence in progress':'Lock & strike');
    command.disabled=!s.holding&&s.phase!=='released'&&(!s.reachable||s.busy);
    this.setText('#hunt-result',s.captures||s.misses?`${s.captures} GRIP${s.captures===1?'':'S'} · ${s.misses} MISS${s.misses===1?'':'ES'}`:'CONTACT-BASED CAPTURE');
    const phaseIndex={tracking:0,locked:1,leaning:2,striking:3,retracting:4,holding:4,recovering:0,releasing:4,released:0}[s.phase];
    document.querySelectorAll('.hunt-step').forEach((el,i)=>{el.classList.toggle('active',i===phaseIndex);el.classList.toggle('complete',i<phaseIndex);});
    this.syncControls();
  }
  setText(selector,text){const el=$(selector);if(el.textContent!==text)el.textContent=text;}
  screenTarget(){const p=this.simulation.target.clone().project(this.camera),r=this.canvas.getBoundingClientRect();return{x:r.left+(p.x+1)*r.width/2,y:r.top+(1-p.y)*r.height/2};}
}
